/*******************************************************************************
 * services/p3Git.cc                                                           *
 *                                                                             *
 * Copyright (C) 2026 RetroShare Team <retroshare.project@gmail.com>           *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Affero General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Affero General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Affero General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

#include "gui/RetroGitNotify.h"
#include "p3Git.h"
#include "rsGitItems.h"
#include "GitManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <retroshare/rsfiles.h>
#include <retroshare/rsgxscircles.h>
#include <retroshare/rsservicecontrol.h>

RsGit *rsGit = NULL;

static std::atomic_bool sRetroGitDebugEnabled(false);
bool retroGitDebugEnabled() { return sRetroGitDebugEnabled.load(std::memory_order_relaxed); }
void setRetroGitDebugEnabled(bool enabled) { sRetroGitDebugEnabled.store(enabled, std::memory_order_relaxed); }

namespace {
// At most this many tunnel requests are answered at the same time; each one
// packs a whole repository.
const size_t MAX_CONCURRENT_SERVED_REQUESTS = 2;
// Upper bound for repository data queued for sending over tunnels.
const qint64 MAX_OUTGOING_BYTES = 2LL * GitTunnelTransfer::MaxSize;
// Chunks handed to the tunnel service per tick and transfer (tick = 100 ms).
const int CHUNKS_PER_TICK = 2;
// Offline clones/pulls give up when the packs do not arrive in this time.
const auto OFFLINE_TIMEOUT = std::chrono::hours(24);
// Fire-and-forget tokens still pending after this long are cancelled.
const auto TOKEN_TIMEOUT = std::chrono::minutes(5);

bool isUnprocessed(const RsMsgMetaData &meta)
{
    return meta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;
}
}

static uint32_t retroGitAuthenPolicy()
{
    uint32_t policy = 0;
    uint8_t flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN |
                 GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
    flag |= GXS_SERV::MSG_AUTHEN_ROOT_AUTHOR_SIGN;
    RsGenExchange::setAuthenPolicyFlag(flag, policy,
                                     RsGenExchange::PUBLIC_GRP_BITS);
    RsGenExchange::setAuthenPolicyFlag(flag, policy,
                                     RsGenExchange::RESTRICTED_GRP_BITS);
    RsGenExchange::setAuthenPolicyFlag(flag, policy,
                                     RsGenExchange::PRIVATE_GRP_BITS);

    flag = 0;
    RsGenExchange::setAuthenPolicyFlag(flag, policy,
                                     RsGenExchange::GRP_OPTION_BITS);

    return policy;
}

p3Git::p3Git(RsGeneralDataService *gds, RsNetworkExchangeService *nes,RsGixs *gixs, RetroGitNotify *notifier)
    : RsGenExchange(gds, nes, new RsGxsRetroGitSerialiser(),RS_SERVICE_TYPE_RetroGit_PLUGIN, gixs,retroGitAuthenPolicy()),
      RsGxsIfaceHelper(static_cast<RsGxsIface &>(*this)), mRetroGitMtx("p3Git"), mNotify(notifier), mGxsTunnels(NULL),
      mLastSlowTick(), mCacheMtx("p3GitCache"), mGroupsValid(false), mGroupsGeneration(0),
      mStopJobs(false), mStopping(false)
{
    rsGit = this;
    mWorker = std::thread(&p3Git::workerLoop, this);
}

p3Git::~p3Git()
{
    stopWorker();
    rsGit = NULL;
}

/* ------------------------------------------------------------------------ */
/* Background worker                                                         */
/* ------------------------------------------------------------------------ */

void p3Git::workerLoop()
{
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mJobMutex);
            mJobCv.wait(lock, [this]() { return mStopJobs || !mJobs.empty(); });
            if (mStopJobs) return;
            job = std::move(mJobs.front());
            mJobs.pop_front();
        }
        try {
            job();
        } catch (const std::exception &e) {
            std::cerr << "RetroGit: background job failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "RetroGit: background job failed" << std::endl;
        }
    }
}

void p3Git::runJob(std::function<void()> job)
{
    if (mStopping) return;
    {
        std::lock_guard<std::mutex> lock(mJobMutex);
        mJobs.push_back(std::move(job));
    }
    mJobCv.notify_one();
}

void p3Git::stopWorker()
{
    mStopping = true;
    {
        std::lock_guard<std::mutex> lock(mJobMutex);
        mStopJobs = true;
        mJobs.clear();
    }
    mJobCv.notify_all();
    if (mWorker.joinable()) {
        if (mWorker.get_id() == std::this_thread::get_id()) mWorker.detach();
        else mWorker.join();
    }
}

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

void p3Git::postGitEvent(RsGitEventCode code, const RsGxsGroupId &groupId, const RsGxsMessageId &msgId)
{
    if (!rsEvents) return;
    RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
    auto ev = std::make_shared<RsGitEvent>(gitEventType);
    ev->mGitGroupId = groupId;
    ev->mGitMsgId = msgId;
    ev->mGitEventCode = code;
    rsEvents->postEvent(ev);
}

void p3Git::postCloneStatus(const RsGxsGroupId &groupId, const std::string &status, bool success,
                           uint64_t received, uint64_t total)
{
    if (rsEvents) {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);
        ev->mGitGroupId = groupId;
        ev->mGitEventCode = RsGitEventCode::CLONE_STATUS_CHANGED;
        ev->mCloneStatus = status;
        ev->mCloneSuccess = success;
        ev->mCloneBytesReceived = received;
        ev->mCloneBytesTotal = total;
        rsEvents->postEvent(ev);
    }
}

/* ------------------------------------------------------------------------ */
/* Service plumbing                                                          */
/* ------------------------------------------------------------------------ */

RsServiceInfo p3Git::getServiceInfo()
{
    const std::string RETRO_GIT_APP_NAME = "RetroGit";
    const uint16_t RETRO_GIT_APP_MAJOR_VERSION = 1;
    const uint16_t RETRO_GIT_APP_MINOR_VERSION = 1;
    const uint16_t RETRO_GIT_MIN_MAJOR_VERSION = 1;
    const uint16_t RETRO_GIT_MIN_MINOR_VERSION = 0;

    return RsServiceInfo(RS_SERVICE_TYPE_RetroGit_PLUGIN,
        RETRO_GIT_APP_NAME,
        RETRO_GIT_APP_MAJOR_VERSION,
        RETRO_GIT_APP_MINOR_VERSION,
        RETRO_GIT_MIN_MAJOR_VERSION,
        RETRO_GIT_MIN_MINOR_VERSION);
}

RsSerialiser *p3Git::setupSerialiser()
{
    RsSerialiser *rss = new RsSerialiser;
    rss->addSerialType(new RsGitConfigSerializer());
    return rss;
}

bool p3Git::saveList(bool &cleanup, std::list<RsItem *> &saveList)
{
    cleanup = true;

    RsGitConfigItem *item = new RsGitConfigItem();

    {
    RS_STACK_MUTEX(mRetroGitMtx);
    item->mKnownGit = mKnownGit;
    }

    saveList.push_back(item);
    return true;
}

bool p3Git::loadList(std::list<RsItem *> &loadList)
{
    while(!loadList.empty())
    {
        RsItem *item = loadList.front();
        loadList.pop_front();

        RsGitConfigItem *configItem = dynamic_cast<RsGitConfigItem *>(item);

        if(configItem != NULL)
        {
          RS_STACK_MUTEX(mRetroGitMtx);
          mKnownGit = configItem->mKnownGit;
        }

        delete item;
    }
    return true;
}

RsTokenService *p3Git::getTokenService()
{
    return RsGenExchange::getTokenService();
}

bool p3Git::getGitStatistics(GxsServiceStatistic &stats)
{
    return getServiceStatistics(stats);
}

/* ------------------------------------------------------------------------ */
/* Token bookkeeping                                                         */
/* ------------------------------------------------------------------------ */

void p3Git::trackToken(uint32_t token)
{
    if (!token) return;
    RS_STACK_MUTEX(mRetroGitMtx);
    mTrackedTokens.push_back(std::make_pair(token, std::chrono::steady_clock::now()));
}

void p3Git::acknowledgeTrackedTokens()
{
    std::list<std::pair<uint32_t, std::chrono::steady_clock::time_point> > tokens;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        tokens.swap(mTrackedTokens);
    }
    const auto now = std::chrono::steady_clock::now();
    for (auto it = tokens.begin(); it != tokens.end(); ) {
        const auto status = requestStatus(it->first);
        if (status == RsTokenService::PENDING || status == RsTokenService::PARTIAL) {
            if (now - it->second > TOKEN_TIMEOUT) {
                cancelRequest(it->first);
                it = tokens.erase(it);
            } else ++it;
            continue;
        }
        // Release the token and its notification entry inside libretroshare.
        RsGxsGrpMsgIdPair msgId;
        RsGxsGroupId groupId;
        if (!acknowledgeMsg(it->first, msgId)) acknowledgeGrp(it->first, groupId);
        it = tokens.erase(it);
    }
    if (!tokens.empty()) {
        RS_STACK_MUTEX(mRetroGitMtx);
        mTrackedTokens.splice(mTrackedTokens.end(), tokens);
    }
}

/* ------------------------------------------------------------------------ */
/* Cached reads                                                              */
/* ------------------------------------------------------------------------ */

void p3Git::invalidateGroups()
{
    RS_STACK_MUTEX(mCacheMtx);
    mGroupsValid = false;
    mGroupsCache.clear();
    ++mGroupsGeneration;
    // Pull request status depends on the owner stored in the group.
    mMsgCache.clear();
    for (auto &generation : mMsgGeneration) ++generation.second;
}

void p3Git::invalidateMessages(const RsGxsGroupId &groupId)
{
    RS_STACK_MUTEX(mCacheMtx);
    mMsgCache.erase(groupId);
    ++mMsgGeneration[groupId];
}

bool p3Git::loadGroups(std::vector<RsGitGroup> &groups)
{
    uint32_t token;
    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;
    if (!requestGroupInfo(token, opts) ||
        waitToken(token, std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE) {
        std::cerr << "p3Git: group request failed or timed out." << std::endl;
        return false;
    }

    std::vector<RsGxsGrpItem *> grpItems;
    if (!RsGenExchange::getGroupData(token, grpItems)) {
        std::cerr << "p3Git: failed to get group data for token " << token << std::endl;
        return false;
    }

    for (RsGxsGrpItem *item : grpItems) {
        std::unique_ptr<RsGxsGrpItem> owner(item);
        RsGitGroupItem *gitItem = dynamic_cast<RsGitGroupItem *>(item);
        if (!gitItem) continue;
        RsGitGroup g = gitItem->mGroup;
        g.mMeta = gitItem->meta;
        if (g.mGroupName.empty() && !g.mMeta.mGroupName.empty())
            g.mGroupName = g.mMeta.mGroupName;
        groups.push_back(g);
    }
    if (retroGitDebugEnabled())
        std::cerr << "p3Git: loaded " << groups.size() << " groups from the database" << std::endl;
    return true;
}

bool p3Git::cachedGroups(std::vector<RsGitGroup> &groups)
{
    uint64_t generation;
    {
        RS_STACK_MUTEX(mCacheMtx);
        if (mGroupsValid) {
            groups = mGroupsCache;
            return true;
        }
        generation = mGroupsGeneration;
    }
    std::vector<RsGitGroup> loaded;
    if (!loadGroups(loaded)) return false;
    {
        RS_STACK_MUTEX(mCacheMtx);
        if (generation == mGroupsGeneration) {
            mGroupsCache = loaded;
            mGroupsValid = true;
        }
    }
    groups.swap(loaded);
    return true;
}

bool p3Git::getGroups(const std::list<RsGxsGroupId> &groupIds,std::vector<RsGitGroup> &groups)
{
    std::vector<RsGitGroup> all;
    if (!cachedGroups(all)) return false;
    if (groupIds.empty()) {
        groups.insert(groups.end(), all.begin(), all.end());
        return true;
    }
    const std::set<RsGxsGroupId> wanted(groupIds.begin(), groupIds.end());
    for (const auto &group : all)
        if (wanted.count(group.mMeta.mGroupId)) groups.push_back(group);
    // An empty result is a valid state.
    return true;
}

bool p3Git::loadMessages(const RsGxsGroupId &groupId, MsgCache &out, bool withPackData)
{
    uint32_t token;
    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;
    std::list<RsGxsGroupId> groupIds = { groupId };
    if (!requestMsgInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE)
        return false;

    std::map<RsGxsGroupId, std::vector<RsGitMsgItem*> > msgItems;
    if (!getMsgDataT<RsGitMsgItem>(token, msgItems)) return false;

    std::vector<std::unique_ptr<RsGitMsgItem> > owned;
    for (auto &group : msgItems)
        for (auto *item : group.second) owned.emplace_back(item);
    const std::vector<RsGitMsgItem*> &vec = msgItems[groupId];

    std::vector<RsGitGroup> groups;
    getGroups({groupId}, groups);
    const RsGxsId owner = groups.empty() ? RsGxsId() : groups.front().mMeta.mAuthorId;

    // Pull request status updates: only the owner, or the author closing their own proposal.
    std::map<RsGxsMessageId, uint32_t> statusUpdates;
    std::map<RsGxsMessageId, rstime_t> statusTimes;
    std::map<RsGxsMessageId, RsGitMsgItem*> proposalsById;
    for (auto *proposal : vec)
        if (proposal && proposal->mGitMsgType == 2)
            proposalsById[proposal->meta.mMsgId] = proposal;
    for (auto *status : vec) {
        if (!status || status->mGitMsgType != 3 || (status->mStatus != 1 && status->mStatus != 2)) continue;
        auto proposalIt = proposalsById.find(status->meta.mParentId);
        if (proposalIt == proposalsById.end()) continue;
        auto *proposal = proposalIt->second;
        const bool authorized = (!owner.isNull() && status->meta.mAuthorId == owner) ||
            (status->mStatus == 1 && status->meta.mAuthorId == proposal->meta.mAuthorId);
        if (authorized && (status->meta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD))
            out.unreadPRStatuses[proposal->meta.mMsgId].push_back(status->meta.mMsgId);
        if (authorized && status->meta.mPublishTs >= statusTimes[proposal->meta.mMsgId]) {
            statusUpdates[proposal->meta.mMsgId] = status->mStatus;
            statusTimes[proposal->meta.mMsgId] = status->meta.mPublishTs;
        }
    }

    for (auto *gitMsg : vec) {
        if (!gitMsg) continue;
        // Repository updates are root messages, which only the owner can sign.
        // Child messages can be posted by any subscriber and must never move branches.
        if (gitMsg->mGitMsgType == 1 && gitMsg->meta.mParentId.isNull()) {
            RsGitUpdate update;
            update.mMeta = gitMsg->meta;
            if (withPackData) update.mPackfileData = gitMsg->mPackfileData;
            update.mRefUpdates = gitMsg->mRefUpdates;
            update.mFiles = gitMsg->mFiles;
            out.updates.push_back(update);
        } else if (gitMsg->mGitMsgType == 2) {
            RsGitPullRequest pr;
            pr.mMeta = gitMsg->meta;
            // Present a proposal and its authenticated status replies as one unread item.
            if (out.unreadPRStatuses.count(pr.mMeta.mMsgId))
                pr.mMeta.mMsgStatus |= GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
            pr.mTitle = gitMsg->mTitle;
            pr.mDescription = gitMsg->mDescription;
            pr.mTargetBranch = gitMsg->mTargetBranch;
            pr.mSourceBranch = gitMsg->mSourceBranch;
            pr.mSourceGroupId = gitMsg->mSourceGroupId;
            pr.mSourceCommit = gitMsg->mSourceCommit;
            if (withPackData) pr.mPackfileData = gitMsg->mPackfileData;
            pr.mStatus = 0; // Only authenticated status replies can close or merge a proposal.
            pr.mFiles = gitMsg->mFiles;
            auto status = statusUpdates.find(pr.mMeta.mMsgId);
            if (status != statusUpdates.end()) pr.mStatus = status->second;
            out.pullRequests.push_back(pr);
        }
    }
    if (retroGitDebugEnabled())
        std::cerr << "RetroGit PR DEBUG: group " << groupId.toStdString() << " messages=" << vec.size()
                  << " updates=" << out.updates.size() << " pullRequests=" << out.pullRequests.size() << std::endl;
    return true;
}

bool p3Git::cachedMessages(const RsGxsGroupId &groupId, MsgCache &out)
{
    uint64_t generation;
    {
        RS_STACK_MUTEX(mCacheMtx);
        auto it = mMsgCache.find(groupId);
        if (it != mMsgCache.end()) {
            out = it->second;
            return true;
        }
        generation = mMsgGeneration[groupId];
    }
    MsgCache loaded;
    if (!loadMessages(groupId, loaded, false)) return false;
    {
        RS_STACK_MUTEX(mCacheMtx);
        if (generation == mMsgGeneration[groupId]) mMsgCache[groupId] = loaded;
    }
    out = std::move(loaded);
    return true;
}

bool p3Git::getUpdates(const RsGxsGroupId &groupId, std::vector<RsGitUpdate> &updates)
{
    MsgCache cache;
    if (!cachedMessages(groupId, cache)) return false;
    updates.insert(updates.end(), cache.updates.begin(), cache.updates.end());
    return true;
}

bool p3Git::getPullRequests(const RsGxsGroupId &groupId, std::vector<RsGitPullRequest> &pullRequests)
{
    MsgCache cache;
    if (!cachedMessages(groupId, cache)) return false;
    pullRequests.insert(pullRequests.end(), cache.pullRequests.begin(), cache.pullRequests.end());
    return true;
}

/* ------------------------------------------------------------------------ */
/* Change notifications                                                      */
/* ------------------------------------------------------------------------ */

void p3Git::notifyChanges(std::vector<RsGxsNotify *> &changes)
{
    // Read/processed flag changes are reported once per group and batch.
    std::set<RsGxsGroupId> statusChangedGroups;

    for (RsGxsNotify *change : changes) {
        // The service owns the notifications (and the items they carry).
        std::unique_ptr<RsGxsNotify> changeDeleter(change);
        if (!change) continue;

        /* ---- Message changes (new commit / post) ---- */
        if (RsGxsMsgChange *msgChange = dynamic_cast<RsGxsMsgChange *>(change)) {
            invalidateMessages(msgChange->mGroupId);
            const auto type = msgChange->getType();
            RsGitMsgItem *gitMsg = dynamic_cast<RsGitMsgItem*>(msgChange->mNewMsgItem);
            if (retroGitDebugEnabled())
                std::cerr << "p3Git: message change type=" << type << " group=" << msgChange->mGroupId.toStdString()
                          << " msg=" << msgChange->mMsgId.toStdString()
                          << " gitType=" << (gitMsg ? gitMsg->mGitMsgType : 0) << std::endl;

            if (type == RsGxsNotify::TYPE_PROCESSED) {
                statusChangedGroups.insert(msgChange->mGroupId);
                continue;
            }
            if (type != RsGxsNotify::TYPE_RECEIVED_NEW && type != RsGxsNotify::TYPE_PUBLISHED) continue;
            if (!gitMsg) continue;

            if (gitMsg->mGitMsgType == 1 && gitMsg->meta.mParentId.isNull()) {
                // Owner-signed root update: unpacking can take long, do it on the worker.
                const RsGxsGroupId groupId = msgChange->mGroupId;
                const RsGxsMessageId msgId = msgChange->mMsgId;
                const std::string packData = gitMsg->mFiles.empty() ? gitMsg->mPackfileData : std::string();
                const RsFileHash fileHash = gitMsg->mFiles.empty() ? RsFileHash() : gitMsg->mFiles[0].mHash;
                const std::map<std::string, std::string> refs = gitMsg->mRefUpdates;
                const rstime_t publishTs = gitMsg->meta.mPublishTs;
                runJob([this, groupId, msgId, packData, fileHash, refs, publishTs]() {
                    unpackRootUpdateJob(groupId, msgId, packData, fileHash, refs, publishTs);
                });
            } else if (gitMsg->mGitMsgType == 2 || gitMsg->mGitMsgType == 3) {
                postGitEvent(RsGitEventCode::NEW_POST, msgChange->mGroupId, msgChange->mMsgId);
            }
            continue;
        }

        /* ---- Group changes (new repo / subscription / update) ---- */
        if (RsGxsGroupChange *grpChange = dynamic_cast<RsGxsGroupChange *>(change)) {
            invalidateGroups();
            const auto type = grpChange->getType();
            if (retroGitDebugEnabled())
                std::cerr << "p3Git: group change type=" << type << " group=" << grpChange->mGroupId.toStdString() << std::endl;
            switch (type) {
            case RsGxsNotify::TYPE_PUBLISHED:
            case RsGxsNotify::TYPE_PROCESSED:
            case RsGxsNotify::TYPE_RECEIVED_NEW:
            case RsGxsNotify::TYPE_UPDATED:
            {
                const RsGxsGroupId groupId = grpChange->mGroupId;
                runJob([groupId]() {
                    const std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
                    if (!GitManager::isValidRepository(repoPath)) GitManager::initRepository(repoPath);
                });
                postGitEvent(type == RsGxsNotify::TYPE_RECEIVED_NEW ? RsGitEventCode::NEW_GIT
                                                                    : RsGitEventCode::GIT_UPDATED, groupId);
                break;
            }
            case RsGxsNotify::TYPE_GROUP_DELETED:
                postGitEvent(RsGitEventCode::GIT_UPDATED, grpChange->mGroupId);
                break;
            default:
                break;
            }
            continue;
        }

        // Anything else (e.g. deleted messages) may change what a group contains.
        invalidateMessages(change->mGroupId);
        if (change->getType() == RsGxsNotify::TYPE_MESSAGE_DELETED)
            statusChangedGroups.insert(change->mGroupId);
    }

    for (const auto &groupId : statusChangedGroups)
        postGitEvent(RsGitEventCode::POST_UPDATED, groupId);
}

void p3Git::unpackRootUpdateJob(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, const std::string &packData,
                                const RsFileHash &fileHash, const std::map<std::string, std::string> &refUpdates,
                                rstime_t publishTs)
{
    const std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    bool unpacked = false;
    if (!fileHash.isNull()) {
        FileInfo info;
        if (rsFiles && rsFiles->alreadyHaveFile(fileHash, info)) {
            unpacked = GitManager::unpackPackfileFromFile(repoPath, info.path, refsNotOlderThan(groupId, refUpdates, publishTs));
        } else {
            PendingPackfile pending;
            pending.groupId = groupId;
            pending.msgId = msgId;
            pending.fileHash = fileHash;
            pending.refUpdates = refUpdates;
            pending.publishTs = publishTs;
            RS_STACK_MUTEX(mRetroGitMtx);
            bool already = false;
            for (const auto &p : mPendingPackfiles)
                if (p.fileHash == fileHash && p.groupId == groupId) already = true;
            if (!already) mPendingPackfiles.push_back(pending);
            return;
        }
    } else {
        unpacked = GitManager::unpackPackfile(repoPath, packData, refsNotOlderThan(groupId, refUpdates, publishTs));
    }
    recordUnpackResult(groupId, msgId, refUpdates, publishTs, unpacked);
    postGitEvent(unpacked ? RsGitEventCode::NEW_POST : RsGitEventCode::POST_UPDATED, groupId, msgId);
}

std::map<std::string, std::string> p3Git::refsNotOlderThan(const RsGxsGroupId &groupId,
        const std::map<std::string, std::string> &refUpdates, rstime_t publishTs)
{
    std::map<std::string, std::string> result;
    RS_STACK_MUTEX(mRetroGitMtx);
    const auto &times = mRefTimes[groupId];
    for (const auto &ref : refUpdates) {
        auto it = times.find(ref.first);
        if (it == times.end() || publishTs >= it->second) result.insert(ref);
    }
    return result;
}

void p3Git::noteRefsApplied(const RsGxsGroupId &groupId, const std::map<std::string, std::string> &refUpdates, rstime_t publishTs)
{
    RS_STACK_MUTEX(mRetroGitMtx);
    auto &times = mRefTimes[groupId];
    for (const auto &ref : refUpdates) {
        rstime_t &time = times[ref.first];
        if (publishTs > time) time = publishTs;
    }
}

void p3Git::recordUnpackResult(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId,
                               const std::map<std::string, std::string> &refUpdates, rstime_t publishTs, bool unpacked)
{
    const RsGxsGrpMsgIdPair key(groupId, msgId);
    if (!unpacked) {
        // Objects were kept but their history is incomplete (an earlier pack is
        // missing). Retried when another pack of this repository arrives.
        RS_STACK_MUTEX(mRetroGitMtx);
        IncompleteUpdate incomplete;
        incomplete.refUpdates = refUpdates;
        incomplete.publishTs = publishTs;
        mIncompleteUpdates[key] = incomplete;
        return;
    }
    noteRefsApplied(groupId, refUpdates, publishTs);
    std::vector<RsGxsGrpMsgIdPair> nowComplete{key};
    std::vector<std::pair<RsGxsGrpMsgIdPair, IncompleteUpdate> > retry;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        mIncompleteUpdates.erase(key);
        for (const auto &entry : mIncompleteUpdates)
            if (entry.first.first == groupId) retry.push_back(entry);
    }
    // Re-apply in publish order so that the newest state wins.
    std::sort(retry.begin(), retry.end(), [](const std::pair<RsGxsGrpMsgIdPair, IncompleteUpdate> &a,
                                             const std::pair<RsGxsGrpMsgIdPair, IncompleteUpdate> &b) {
        return a.second.publishTs < b.second.publishTs;
    });
    const std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    for (const auto &entry : retry) {
        const auto refs = refsNotOlderThan(groupId, entry.second.refUpdates, entry.second.publishTs);
        if (GitManager::applyRefUpdates(repoPath, refs)) {
            noteRefsApplied(groupId, refs, entry.second.publishTs);
            nowComplete.push_back(entry.first);
        }
    }
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (const auto &done : nowComplete) mIncompleteUpdates.erase(done);
    }
    for (const auto &done : nowComplete) {
        uint32_t token;
        setMessageProcessedStatus(token, done, true);
    }
}

/* ------------------------------------------------------------------------ */
/* Periodic work                                                             */
/* ------------------------------------------------------------------------ */

void p3Git::service_tick()
{
    // Runs on the GXS service thread every 100 ms: only cheap work here.
    sendOutgoingChunks();

    const auto now = std::chrono::steady_clock::now();
    if (now - mLastSlowTick < std::chrono::seconds(1)) return;
    mLastSlowTick = now;

    acknowledgeTrackedTokens();

    std::vector<RsGxsTunnelId> expired;
    std::vector<RsGxsTunnelId> waiting;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (const auto& transfer : mTransferActivity) {
            if (now - transfer.second > std::chrono::minutes(2))
                expired.push_back(transfer.first);
            else if (!mSentRequests.count(transfer.first)) waiting.push_back(transfer.first);
        }
    }
    for (const auto& id : waiting) {
        RsGxsTunnelService::GxsTunnelInfo info;
        if (mGxsTunnels && mGxsTunnels->getTunnelInfo(id, info) &&
            info.tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_CAN_TALK)
            notifyTunnelStatus(id, info.tunnel_status);
    }
    for (const auto& id : expired)
        failTunnelTransfer(id, "Repository transfer timed out. Check that the owner has published commits and both clients use the updated plugin, then retry.");

    // Downloaded packs are unpacked on the worker.
    std::vector<PendingPackfile> completed;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto it = mPendingPackfiles.begin(); it != mPendingPackfiles.end(); ) {
            FileInfo fi;
            if (rsFiles && rsFiles->alreadyHaveFile(it->fileHash, fi)) {
                completed.push_back(*it);
                it = mPendingPackfiles.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto &pending : completed)
        runJob([this, pending]() { unpackDownloadedJob(pending); });

    // Offline clones/pulls whose packs are all present, or that timed out.
    std::vector<RsGxsGroupId> ready, timedOut;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto &entry : mPendingOffline) {
            PendingOffline &offline = entry.second;
            if (offline.preparing || offline.finalizing) continue;
            if (offline.pendingMsgIds.empty()) {
                offline.finalizing = true;
                ready.push_back(entry.first);
            } else if (now - offline.started > OFFLINE_TIMEOUT) {
                timedOut.push_back(entry.first);
            }
        }
        for (const auto &groupId : timedOut) mPendingOffline.erase(groupId);
    }
    for (const auto &groupId : ready)
        runJob([this, groupId]() { finalizeOfflineJob(groupId); });
    for (const auto &groupId : timedOut)
        postCloneStatus(groupId, "Offline transfer failed: the published packs could not be downloaded. Please retry.", false);
}

void p3Git::unpackDownloadedJob(const PendingPackfile &pending)
{
    const std::string repoPath = GitManager::getBareRepoPath(pending.groupId.toStdString());
    FileInfo fi;
    bool unpacked = false;
    if (rsFiles && rsFiles->FileDetails(pending.fileHash, RS_FILE_HINTS_LOCAL, fi)) {
        std::cout << "p3Git: Pending packfile downloaded: " << fi.path << ", unpacking..." << std::endl;
        unpacked = GitManager::unpackPackfileFromFile(repoPath, fi.path,
            refsNotOlderThan(pending.groupId, pending.refUpdates, pending.publishTs));
        recordUnpackResult(pending.groupId, pending.msgId, pending.refUpdates, pending.publishTs, unpacked);
    }
    {
        // The pack is present now even when its history is still incomplete;
        // offline transfers re-apply all branch updates once every pack is in.
        RS_STACK_MUTEX(mRetroGitMtx);
        mUnpacking.erase(RsGxsGrpMsgIdPair(pending.groupId, pending.msgId));
        auto offline = mPendingOffline.find(pending.groupId);
        if (offline != mPendingOffline.end()) offline->second.pendingMsgIds.erase(pending.msgId);
    }
    postGitEvent(unpacked ? RsGitEventCode::NEW_POST : RsGitEventCode::POST_UPDATED, pending.groupId, pending.msgId);
}

bool p3Git::unpackUpdate(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, const RsFileHash &fileHash, const std::map<std::string, std::string> &refUpdates)
{
    FileInfo fi;
    if (!rsFiles || !rsFiles->FileDetails(fileHash, RS_FILE_HINTS_LOCAL, fi)) return false;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        // Already unpacked but waiting for an earlier pack: retried automatically.
        if (mIncompleteUpdates.count(RsGxsGrpMsgIdPair(groupId, msgId))) return false;
        if (!mUnpacking.insert(RsGxsGrpMsgIdPair(groupId, msgId)).second) return true; // already queued
        for (auto it = mPendingPackfiles.begin(); it != mPendingPackfiles.end(); )
            if (it->fileHash == fileHash && it->groupId == groupId) it = mPendingPackfiles.erase(it); else ++it;
    }
    PendingPackfile pending;
    pending.groupId = groupId;
    pending.msgId = msgId;
    pending.fileHash = fileHash;
    pending.refUpdates = refUpdates;
    MsgCache cache;
    if (cachedMessages(groupId, cache))
        for (const auto &update : cache.updates)
            if (update.mMeta.mMsgId == msgId) pending.publishTs = update.mMeta.mPublishTs;
    // Unpacking is done on the worker; a NEW_POST event reports completion.
    runJob([this, pending]() { unpackDownloadedJob(pending); });
    return true;
}

/* ------------------------------------------------------------------------ */
/* Publishing                                                                */
/* ------------------------------------------------------------------------ */

bool p3Git::createGroupImpl(uint32_t &token, RsGitGroup &group)
{
    RsGitGroupItem *grpItem = new RsGitGroupItem();
    grpItem->mGroup = group;
    grpItem->meta = group.mMeta; // Ensure metadata (like creator GxsId) is preserved
    publishGroup(token, grpItem);
    return true;
}

bool p3Git::updateGroupImpl(uint32_t &token, RsGitGroup &group)
{
    RsGitGroupItem *grpItem = new RsGitGroupItem();
    grpItem->mGroup = group;
    grpItem->meta = group.mMeta;
    RsGenExchange::updateGroup(token, grpItem);
    return true;
}

bool p3Git::createGroup(uint32_t &token, RsGitGroup &group)
{
    const bool ok = createGroupImpl(token, group);
    trackToken(token);
    return ok;
}

bool p3Git::updateGroup(uint32_t &token, RsGitGroup &group)
{
    const bool ok = updateGroupImpl(token, group);
    trackToken(token);
    return ok;
}

// Blocking Interfaces.
bool p3Git::createGroup(RsGitGroup &group)
{
    uint32_t token;
    return createGroupImpl(token, group) && waitToken(token) == RsTokenService::COMPLETE
        && acknowledgeTokenGrp(token, group.mMeta.mGroupId);
}

bool p3Git::updateGroup(RsGitGroup &group)
{
    uint32_t token;
    if (!updateGroupImpl(token, group) || waitToken(token) != RsTokenService::COMPLETE) return false;
    RsGxsGroupId groupId;
    acknowledgeTokenGrp(token, groupId);
    return true;
}

bool p3Git::publishGitUpdate(uint32_t &token, RsGitUpdate &update)
{
    // Larger packs must be attached as files: GXS items are limited in size.
    if (update.mPackfileData.size() > RETROGIT_MAX_INLINE_PACK) {
        std::cerr << "p3Git: inline pack too large (" << update.mPackfileData.size() << " bytes); attach it as a file." << std::endl;
        return false;
    }

    RsGitMsgItem *msgItem = new RsGitMsgItem();
    msgItem->meta = update.mMeta;

    // Clear child fields to ensure GXS identifies it as a root message and signs it correctly
    msgItem->meta.mParentId.clear();
    msgItem->meta.mThreadId.clear();

    if (msgItem->meta.mMsgName.empty()) {
        msgItem->meta.mMsgName = "Git Push Update";
    }

    if (msgItem->meta.mAuthorId.isNull() && rsIdentity) {
        std::vector<RsGitGroup> groups;
        if (getGroups({msgItem->meta.mGroupId}, groups) && !groups.empty() && !groups[0].mMeta.mAuthorId.isNull() && rsIdentity->isOwnId(groups[0].mMeta.mAuthorId)) {
            msgItem->meta.mAuthorId = groups[0].mMeta.mAuthorId;
        } else {
            std::list<RsGxsId> ownIds;
            rsIdentity->getOwnIds(ownIds);
            if (!ownIds.empty()) {
                msgItem->meta.mAuthorId = ownIds.front();
            }
        }
    }

    msgItem->mGitMsgType = 1; // UPDATE
    msgItem->mPackfileData = update.mPackfileData;
    msgItem->mRefUpdates = update.mRefUpdates;
    msgItem->mFiles = update.mFiles;

    publishMsg(token, msgItem);
    trackToken(token);
    return true;
}

void p3Git::preparePullRequestJob(RsGitPullRequest pr)
{
    const std::string path = GitManager::getBareRepoPath(pr.mMeta.mGroupId.toStdString());
    bool imported = false;
    if (!pr.mFiles.empty()) {
        FileInfo info;
        if (rsFiles && rsFiles->alreadyHaveFile(pr.mFiles.front().mHash, info))
            imported = GitManager::unpackPackfileFromFile(path, info.path, {});
    } else {
        // Cached pull requests carry no pack data: read this one from the database.
        MsgCache full;
        if (loadMessages(pr.mMeta.mGroupId, full, true))
            for (const auto &candidate : full.pullRequests)
                if (candidate.mMeta.mMsgId == pr.mMeta.mMsgId && !candidate.mPackfileData.empty())
                    // Import objects only: incoming branch names must never overwrite upstream branches.
                    imported = GitManager::unpackPackfile(path, candidate.mPackfileData, {});
    }
    const bool ready = imported && GitManager::setReviewRef(path, pr.sourceRef(), pr.mSourceCommit);
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        mPreparingPullRequests.erase(pr.mMeta.mMsgId);
    }
    if (ready) postGitEvent(RsGitEventCode::POST_UPDATED, pr.mMeta.mGroupId, pr.mMeta.mMsgId);
}

bool p3Git::preparePullRequest(const RsGitPullRequest &pr)
{
    if (pr.mSourceCommit.empty()) return true; // Legacy same-repository PR.
    if (pr.mMeta.mMsgId.isNull() || pr.mSourceGroupId.isNull()) return false;
    const std::string path = GitManager::getBareRepoPath(pr.mMeta.mGroupId.toStdString());
    // Fast path: the review reference already exists (no history walk).
    if (GitManager::setReviewRef(path, pr.sourceRef(), pr.mSourceCommit)) return true;

    if (!pr.mFiles.empty() && rsFiles) {
        const auto &file = pr.mFiles.front();
        FileInfo info;
        if (!rsFiles->alreadyHaveFile(file.mHash, info)) {
            bool request = false;
            {
                RS_STACK_MUTEX(mRetroGitMtx);
                request = mRequestedFiles.insert(file.mHash).second;
            }
            if (request) {
                std::list<RsPeerId> sources;
                rsFiles->FileRequest(file.mName, file.mHash, file.mSize, "", RS_FILE_REQ_ANONYMOUS_ROUTING, sources);
            }
            return false;
        }
    }
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        if (!mPreparingPullRequests.insert(pr.mMeta.mMsgId).second) return false; // already queued
    }
    // Importing can take long: done on the worker, a POST_UPDATED event reports success.
    runJob([this, pr]() { preparePullRequestJob(pr); });
    return false;
}

bool p3Git::publishPullRequest(uint32_t &token, RsGitPullRequest &pr)
{
    if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: publish title='" << pr.mTitle << "' target=" << pr.mMeta.mGroupId.toStdString() << " source=" << pr.mSourceGroupId.toStdString() << " commit=" << pr.mSourceCommit << std::endl;
    if (pr.mPackfileData.size() + pr.mDescription.size() + pr.mTitle.size() > RETROGIT_MAX_INLINE_PACK ||
        pr.mDescription.size() > RETROGIT_MAX_DESCRIPTION) {
        if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: pull request message too large" << std::endl;
        return false;
    }
    std::vector<RsGitGroup> sources;
    if (!pr.mSourceCommit.empty() &&
        (pr.mSourceGroupId.isNull() || !getGroups({pr.mSourceGroupId}, sources) || sources.empty() ||
         !IS_GROUP_ADMIN(sources.front().mMeta.mSubscribeFlags) ||
         !rsIdentity || !rsIdentity->isOwnId(pr.mMeta.mAuthorId) ||
         pr.mMeta.mAuthorId != sources.front().mMeta.mAuthorId)) { if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: source identity validation failed" << std::endl; return false; }
    std::vector<RsGitUpdate> updates;
    if (!getUpdates(pr.mMeta.mGroupId, updates) || updates.empty()) {
        // Older repositories may expose their Git history through child
        // messages while root update metadata is unavailable locally. Use the
        // latest PR message as the thread parent instead of rejecting a valid PR.
        std::vector<RsGitPullRequest> existing;
        if (!getPullRequests(pr.mMeta.mGroupId, existing) || existing.empty()) {
            if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: target has no attachable messages" << std::endl;
            return false;
        }
        pr.mMeta.mParentId = existing.back().mMeta.mMsgId;
        pr.mMeta.mThreadId = existing.back().mMeta.mThreadId.isNull()
            ? existing.back().mMeta.mMsgId : existing.back().mMeta.mThreadId;
        if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: using existing PR message as parent" << std::endl;
    }
    else {
        pr.mMeta.mParentId = updates.back().mMeta.mMsgId;
        pr.mMeta.mThreadId = pr.mMeta.mParentId;
    }
    // Proposals are author-signed children of a published upstream update.
    // Contributors do not possess the upstream root-message publishing key.
    RsGitMsgItem *msgItem = new RsGitMsgItem();
    msgItem->meta = pr.mMeta;

    if (msgItem->meta.mMsgName.empty()) {
        msgItem->meta.mMsgName = pr.mTitle.empty() ? "Git Pull Request" : pr.mTitle;
    }

    if (msgItem->meta.mAuthorId.isNull() && rsIdentity) {
        std::vector<RsGitGroup> groups;
        if (getGroups({msgItem->meta.mGroupId}, groups) && !groups.empty() && !groups[0].mMeta.mAuthorId.isNull() && rsIdentity->isOwnId(groups[0].mMeta.mAuthorId)) {
            msgItem->meta.mAuthorId = groups[0].mMeta.mAuthorId;
        } else {
            std::list<RsGxsId> ownIds;
            rsIdentity->getOwnIds(ownIds);
            if (!ownIds.empty()) {
                msgItem->meta.mAuthorId = ownIds.front();
            }
        }
    }

    msgItem->mGitMsgType = 2; // PULL_REQUEST
    msgItem->mTitle = pr.mTitle;
    msgItem->mDescription = pr.mDescription;
    msgItem->mTargetBranch = pr.mTargetBranch;
    msgItem->mSourceBranch = pr.mSourceBranch;
    msgItem->mSourceGroupId = pr.mSourceGroupId;
    msgItem->mSourceCommit = pr.mSourceCommit;
    msgItem->mPackfileData = pr.mPackfileData;
    msgItem->mStatus = pr.mStatus;
    msgItem->mFiles = pr.mFiles;

    publishMsg(token, msgItem);
    trackToken(token);
    if (retroGitDebugEnabled()) std::cerr << "RetroGit PR DEBUG: publishMsg queued" << std::endl;
    return true;
}

bool p3Git::closePullRequest(uint32_t &token, const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, uint32_t status)
{
    if (!rsIdentity || (status != 1 && status != 2)) return false;
    std::vector<RsGitPullRequest> prs;
    std::vector<RsGitGroup> groups;
    if (!getGroups({groupId}, groups) || groups.empty() || !getPullRequests(groupId, prs)) return false;
    RsGxsId signer;
    RsGxsMessageId threadId = msgId;
    for (const auto &pr : prs) if (pr.mMeta.mMsgId == msgId) {
        if (!pr.mMeta.mThreadId.isNull()) threadId = pr.mMeta.mThreadId;
        if (rsIdentity->isOwnId(groups.front().mMeta.mAuthorId)) signer = groups.front().mMeta.mAuthorId;
        else if (status == 1 && rsIdentity->isOwnId(pr.mMeta.mAuthorId)) signer = pr.mMeta.mAuthorId;
    }
    if (signer.isNull()) return false;
    RsGitMsgItem *msgItem = new RsGitMsgItem();
    msgItem->meta.mGroupId = groupId;
    msgItem->meta.mParentId = msgId;
    msgItem->meta.mThreadId = threadId;
    msgItem->meta.mAuthorId = signer;

    msgItem->mGitMsgType = 3; // PULL_REQUEST_STATUS_UPDATE
    msgItem->mStatus = status; // 1 = Closed, 2 = Merged
    msgItem->meta.mMsgName = "PR Status Update";

    publishMsg(token, msgItem);
    trackToken(token);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Subscriptions and status flags                                            */
/* ------------------------------------------------------------------------ */

bool p3Git::subscribeToGroup(uint32_t &token, const RsGxsGroupId &groupId,bool subscribe_flag)
{
    bool response = RsGenExchange::subscribeToGroup(token, groupId, subscribe_flag);
    invalidateGroups();
    if (response) {
        trackToken(token);
        postGitEvent(RsGitEventCode::SUBSCRIBE_STATUS_CHANGED, groupId);
    }
    return response;
}

bool p3Git::subscribe(const RsGxsGroupId &groupId, bool subscribe_flag)
{
    uint32_t token;
    if (!RsGenExchange::subscribeToGroup(token, groupId, subscribe_flag)) return false;
    // Wait so that the caller sees the new subscription state.
    const bool done = waitToken(token) == RsTokenService::COMPLETE;
    RsGxsGroupId acknowledged;
    if (done) acknowledgeGrp(token, acknowledged);
    invalidateGroups();
    postGitEvent(RsGitEventCode::SUBSCRIBE_STATUS_CHANGED, groupId);
    return done;
}

void p3Git::setMsgReadImpl(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool read)
{
    if (read) {
        MsgCache cache;
        if (cachedMessages(msgId.first, cache)) {
            auto statuses = cache.unreadPRStatuses.find(msgId.second);
            if (statuses != cache.unreadPRStatuses.end()) {
                for (const auto &statusId : statuses->second) {
                    uint32_t statusToken = 0;
                    setMsgStatusFlags(statusToken, RsGxsGrpMsgIdPair(msgId.first, statusId), 0,
                        GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);
                    trackToken(statusToken);
                }
            }
        }
    }
    /* Always remove status unprocessed */
    uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    uint32_t status = GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    if (read) status = 0;
    setMsgStatusFlags(token, msgId, status, mask);
    invalidateMessages(msgId.first);
    // The GUI is told through notifyChanges() once the flag is stored,
    // and only when it actually changed.
}

void p3Git::setMsgProcessedImpl(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool processed)
{
    uint32_t mask = GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;
    uint32_t status = processed ? 0 : GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;
    setMsgStatusFlags(token, msgId, status, mask);
    invalidateMessages(msgId.first);
}

bool p3Git::setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId, bool read)
{
    uint32_t token;
    setMsgReadImpl(token, msgId, read);
    if(waitToken(token) != RsTokenService::COMPLETE)
    {
        std::cerr << "p3Git::setMessageReadStatus() waitToken failed" << std::endl;
        return false;
    }
    RsGxsGrpMsgIdPair p;
    acknowledgeMsg(token, p);
    invalidateMessages(msgId.first);
    return true;
}

void p3Git::setMessageReadStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool read)
{
    setMsgReadImpl(token, msgId, read);
    trackToken(token);
}

bool p3Git::setMessageProcessedStatus(const RsGxsGrpMsgIdPair &msgId, bool processed)
{
    uint32_t token;
    setMsgProcessedImpl(token, msgId, processed);
    if (waitToken(token) != RsTokenService::COMPLETE) return false;
    RsGxsGrpMsgIdPair p;
    acknowledgeMsg(token, p);
    invalidateMessages(msgId.first);
    return true;
}

void p3Git::setMessageProcessedStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool processed)
{
    setMsgProcessedImpl(token, msgId, processed);
    trackToken(token);
}

/* ------------------------------------------------------------------------ */
/* Offline clone / pull (published packs)                                    */
/* ------------------------------------------------------------------------ */

bool p3Git::requestOffline(const RsGxsGroupId &groupId, const std::string &localPath, bool isPull)
{
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        if (mPendingOffline.count(groupId)) return true; // already running
        PendingOffline offline;
        offline.groupId = groupId;
        offline.localPath = localPath;
        offline.isPull = isPull;
        offline.started = std::chrono::steady_clock::now();
        mPendingOffline[groupId] = offline;
    }
    postCloneStatus(groupId, isPull ? "Offline sync: checking GXS database..." : "Offline clone: checking GXS database...", false);
    runJob([this, groupId]() { startOfflineJob(groupId); });
    return true;
}

bool p3Git::requestOfflineClone(const RsGxsGroupId &groupId, const std::string &localPath)
{
    return requestOffline(groupId, localPath, false);
}

bool p3Git::requestOfflinePull(const RsGxsGroupId &groupId, const std::string &localPath)
{
    std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
    if (!GitManager::isValidRepository(barePath)) {
        std::cerr << "p3Git: Offline pull failed: local repository not initialized." << std::endl;
        postCloneStatus(groupId, "Offline pull failed: local repository not initialized.", false);
        return false;
    }
    return requestOffline(groupId, localPath, true);
}

void p3Git::startOfflineJob(const RsGxsGroupId &groupId)
{
    bool isPull = false;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto it = mPendingOffline.find(groupId);
        if (it == mPendingOffline.end()) return;
        isPull = it->second.isPull;
    }
    const std::string label = isPull ? "Offline sync" : "Offline clone";
    auto abort = [&](const std::string &reason) {
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            mPendingOffline.erase(groupId);
        }
        postCloneStatus(groupId, reason, false);
    };

    const std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
    if (!GitManager::isValidRepository(barePath) && !GitManager::initRepository(barePath, true)) {
        abort(label + ": failed to initialize local bare repository.");
        return;
    }

    MsgCache messages;
    if (!loadMessages(groupId, messages, true)) {
        abort(label + ": failed to retrieve updates from GXS database.");
        return;
    }
    std::vector<RsGitUpdate> &updates = messages.updates;
    std::sort(updates.begin(), updates.end(), [](const RsGitUpdate &a, const RsGitUpdate &b) {
        return a.mMeta.mPublishTs < b.mMeta.mPublishTs;
    });

    std::set<RsGxsMessageId> pendingMsgIds;
    std::vector<std::pair<RsGxsMessageId, IncompleteUpdate> > inOrder;
    for (auto &update : updates) {
        if (mStopping) return;
        if (!isUnprocessed(update.mMeta)) continue;
        IncompleteUpdate entry;
        entry.refUpdates = update.mRefUpdates;
        entry.publishTs = update.mMeta.mPublishTs;
        inOrder.push_back(std::make_pair(update.mMeta.mMsgId, entry));
        // Objects only here: branches are set in publish order once every pack is present.

        if (!update.mFiles.empty()) {
            const RsGxsFile &file = update.mFiles[0];
            FileInfo info;
            if (rsFiles && rsFiles->alreadyHaveFile(file.mHash, info)) {
                if (!GitManager::unpackPackfileFromFile(barePath, info.path, {}))
                    std::cerr << "p3Git: " << label << ": pack " << info.path << " not complete yet for msg "
                              << update.mMeta.mMsgId.toStdString() << std::endl;
                continue;
            }
            pendingMsgIds.insert(update.mMeta.mMsgId);
            if (rsFiles) {
                // Request the file from remote peers who have it
                FileInfo fileInfo;
                rsFiles->FileDetails(file.mHash, RS_FILE_HINTS_REMOTE, fileInfo);
                std::list<RsPeerId> sources;
                for (const auto &peer : fileInfo.peers) sources.push_back(peer.peerId);
                rsFiles->FileRequest(file.mName, file.mHash, file.mSize, "", RS_FILE_REQ_ANONYMOUS_ROUTING, sources);
            }
            RS_STACK_MUTEX(mRetroGitMtx);
            bool alreadyPending = false;
            for (const auto &p : mPendingPackfiles)
                if (p.fileHash == file.mHash && p.groupId == groupId) alreadyPending = true;
            if (!alreadyPending) {
                PendingPackfile pending;
                pending.groupId = groupId;
                pending.msgId = update.mMeta.mMsgId;
                pending.fileHash = file.mHash;
                pending.refUpdates = update.mRefUpdates;
                pending.publishTs = update.mMeta.mPublishTs;
                mPendingPackfiles.push_back(pending);
            }
        } else if (!GitManager::unpackPackfile(barePath, update.mPackfileData, {})) {
            // Objects are kept; branches are applied again once all packs are in.
            std::cerr << "p3Git: " << label << ": inline pack not complete yet for msg "
                      << update.mMeta.mMsgId.toStdString() << std::endl;
        }
    }

    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto it = mPendingOffline.find(groupId);
        if (it == mPendingOffline.end()) return;
        it->second.pendingMsgIds = pendingMsgIds;
        it->second.updatesInOrder = inOrder;
        it->second.preparing = false;
    }
    if (!pendingMsgIds.empty())
        postCloneStatus(groupId, label + ": downloading required packfiles...", false);
    // service_tick() finalizes as soon as no pack is pending.
}

void p3Git::finalizeOfflineJob(const RsGxsGroupId &groupId)
{
    PendingOffline offline;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto it = mPendingOffline.find(groupId);
        if (it == mPendingOffline.end()) return;
        offline = it->second;
    }
    const std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());

    // Packs may have completed in any order: apply branch updates in publish order now.
    bool allApplied = true;
    for (const auto &update : offline.updatesInOrder) {
        const auto refs = refsNotOlderThan(groupId, update.second.refUpdates, update.second.publishTs);
        if (GitManager::applyRefUpdates(barePath, refs)) {
            noteRefsApplied(groupId, refs, update.second.publishTs);
            {
                RS_STACK_MUTEX(mRetroGitMtx);
                mIncompleteUpdates.erase(RsGxsGrpMsgIdPair(groupId, update.first));
            }
            uint32_t token;
            setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(groupId, update.first), true);
        } else {
            allApplied = false;
        }
    }
    if (!allApplied)
        std::cerr << "p3Git: some published updates of " << groupId.toStdString()
                  << " are missing history; the newest complete state is used." << std::endl;

    if (offline.isPull) {
        bool hasWorkingDir = !offline.localPath.empty() && GitManager::isValidRepository(offline.localPath);
        if (hasWorkingDir) {
            postCloneStatus(groupId, "Offline sync: updating working tree...", false);
            std::string error;
            if (GitManager::pullRepository(offline.localPath, barePath, &error))
                postCloneStatus(groupId, "Offline sync completed successfully!", true);
            else
                postCloneStatus(groupId, "Offline sync failed to update working tree. " + error, false);
        } else {
            postCloneStatus(groupId, "Offline sync completed successfully!", true);
        }
    } else {
        postCloneStatus(groupId, "Offline clone: writing working tree...", false);
        if (GitManager::cloneRepository(barePath, offline.localPath))
            postCloneStatus(groupId, "Offline clone completed successfully!", true);
        else
            postCloneStatus(groupId, "Offline clone failed to write working tree.", false);
    }
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        mPendingOffline.erase(groupId);
    }
    postGitEvent(RsGitEventCode::NEW_POST, groupId);
}

bool p3Git::isCloneInProgress(const RsGxsGroupId &groupId)
{
    RS_STACK_MUTEX(mRetroGitMtx);
    for (const auto &clone : mPendingClones)
        if (clone.second.groupId == groupId) return true;
    auto offline = mPendingOffline.find(groupId);
    return offline != mPendingOffline.end() && !offline->second.isPull;
}

/* ------------------------------------------------------------------------ */
/* Direct transfers over GXS tunnels                                         */
/* ------------------------------------------------------------------------ */

bool p3Git::requestCloneOverTunnel(const RsGxsGroupId &groupId, const RsGxsId &toId, const RsGxsId &ownId, const std::string &localPath)
{
    if (!mGxsTunnels) {
        std::cerr << "p3Git: requestCloneOverTunnel failed. mGxsTunnels is NULL. Falling back to offline clone..." << std::endl;
        return requestOfflineClone(groupId, localPath);
    }

    RsGxsTunnelId tunnelId;
    uint32_t error_code = 0;

    std::cout << "p3Git: Requesting direct clone tunnel to owner GXS ID: " << toId << std::endl;
    postCloneStatus(groupId, "Requesting direct secure tunnel to peer...", false);

    if (mGxsTunnels->requestSecuredTunnel(toId, ownId, tunnelId, RETRO_GIT_GXS_TUNNEL_SERVICE_ID, error_code)) {
        RS_STACK_MUTEX(mRetroGitMtx);
        if (mPendingClones.count(tunnelId) || mPendingPulls.count(tunnelId)) return false;
        PendingClone pc;
        mTransferActivity[tunnelId] = std::chrono::steady_clock::now();
        pc.groupId = groupId;
        pc.localPath = localPath;
        mPendingClones[tunnelId] = pc;
        mTunnelToGxsIdMap[tunnelId] = toId;
        std::cout << "p3Git: Clone tunnel requested. Tunnel ID: " << tunnelId << std::endl;
        return true;
    } else {
        std::cerr << "p3Git: requestSecuredTunnel failed, error code: " << error_code << ". Falling back to offline clone..." << std::endl;
        return requestOfflineClone(groupId, localPath);
    }
}

bool p3Git::requestPullOverTunnel(const RsGxsGroupId &groupId, const RsGxsId &toId, const RsGxsId &ownId, const std::string &localPath)
{
    if (!mGxsTunnels) {
        std::cerr << "p3Git: requestPullOverTunnel failed. mGxsTunnels is NULL. Falling back to offline pull..." << std::endl;
        return requestOfflinePull(groupId, localPath);
    }

    RsGxsTunnelId tunnelId;
    uint32_t error_code = 0;

    std::cout << "p3Git: Requesting direct pull tunnel to owner GXS ID: " << toId << std::endl;
    postCloneStatus(groupId, "Requesting direct secure tunnel to peer...", false);

    if (mGxsTunnels->requestSecuredTunnel(toId, ownId, tunnelId, RETRO_GIT_GXS_TUNNEL_SERVICE_ID, error_code)) {
        RS_STACK_MUTEX(mRetroGitMtx);
        if (mPendingClones.count(tunnelId) || mPendingPulls.count(tunnelId)) return false;
        PendingPull pp;
        mTransferActivity[tunnelId] = std::chrono::steady_clock::now();
        pp.groupId = groupId;
        pp.localPath = localPath;
        mPendingPulls[tunnelId] = pp;
        mTunnelToGxsIdMap[tunnelId] = toId;
        std::cout << "p3Git: Pull tunnel requested. Tunnel ID: " << tunnelId << std::endl;
        return true;
    } else {
        std::cerr << "p3Git: requestSecuredTunnel failed, error code: " << error_code << ". Falling back to offline pull..." << std::endl;
        return requestOfflinePull(groupId, localPath);
    }
}

void p3Git::forgetTunnel(const RsGxsTunnelId& id)
{
    RS_STACK_MUTEX(mRetroGitMtx);
    mPendingClones.erase(id);
    mPendingPulls.erase(id);
    mTransferActivity.erase(id);
    mIncomingTransfers.erase(id);
    mSentRequests.erase(id);
    mTunnelToGxsIdMap.erase(id);
    mOutgoing.erase(id);
    mLastProgressPost.erase(id);
    mServingTunnels.erase(id);
}

void p3Git::failTunnelTransfer(const RsGxsTunnelId& id, const std::string& reason)
{
    RsGxsGroupId groupId;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto clone = mPendingClones.find(id);
        auto pull = mPendingPulls.find(id);
        if (clone != mPendingClones.end()) groupId = clone->second.groupId;
        else if (pull != mPendingPulls.end()) groupId = pull->second.groupId;
    }
    forgetTunnel(id);
    if (!groupId.isNull()) {
        postCloneStatus(groupId, reason, false);
        if (mGxsTunnels) mGxsTunnels->closeExistingTunnel(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID);
    }
}

void p3Git::sendTransferError(const RsGxsTunnelId& id, const QString& groupId, const QString& error)
{
    if (!mGxsTunnels) return;
    QJsonObject response;
    response["type"] = "transfer_error";
    response["group_id"] = groupId;
    response["error"] = error;
    QByteArray bytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
    mGxsTunnels->sendData(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID,
        reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size());
}

void p3Git::notifyTunnelStatus(const RsGxsTunnelId& tunnel_id, uint32_t tunnel_status)
{
    RsGxsGroupId groupId;
    std::string localPath;
    bool isPull = false;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto it = mPendingClones.find(tunnel_id);
        if (it != mPendingClones.end()) {
            groupId = it->second.groupId;
            localPath = it->second.localPath;
        } else {
            auto it2 = mPendingPulls.find(tunnel_id);
            if (it2 != mPendingPulls.end()) {
                groupId = it2->second.groupId;
                localPath = it2->second.localPath;
                isPull = true;
            }
        }
    }

    const bool down = tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_REMOTELY_CLOSED ||
                      tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_TUNNEL_DN;

    if (groupId.isNull()) {
        // Tunnel opened by a peer (we serve it): drop its bookkeeping when it closes.
        if (down) forgetTunnel(tunnel_id);
        return;
    }

    if (tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_CAN_TALK) {
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            if (!mSentRequests.insert(tunnel_id).second) return;
            mTransferActivity[tunnel_id] = std::chrono::steady_clock::now();
        }
        std::cout << "p3Git: Tunnel secured (tunnel_id=" << tunnel_id << "). Sending " << (isPull ? "pull_req" : "clone_req") << "..." << std::endl;
        postCloneStatus(groupId, isPull ? "Tunnel secured. Requesting sync data..." : "Tunnel secured. Requesting repository data...", false);

        QJsonObject req;
        req["type"] = isPull ? "pull_req" : "clone_req";
        req["group_id"] = QString::fromStdString(groupId.toStdString());
        req["chunked"] = true;
        if (isPull) {
            // Only advertise objects in the cache where the response is imported.
            QJsonArray have;
            for (const auto& tip : GitManager::knownTips(GitManager::getBareRepoPath(groupId.toStdString()))) {
                if (have.size() >= 256) break;
                have.append(QString::fromStdString(tip));
            }
            req["have"] = have;
        }
        const QByteArray msg = QJsonDocument(req).toJson(QJsonDocument::Compact);
        if (!mGxsTunnels->sendData(tunnel_id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID, (const uint8_t*)msg.constData(), msg.size()))
            failTunnelTransfer(tunnel_id, "Failed to send repository request. Please retry.");
    }
    else if (down)
    {
        std::cout << "p3Git: Tunnel went down. Falling back to offline method for group: " << groupId.toStdString() << std::endl;
        postCloneStatus(groupId, "Tunnel disconnected. Falling back to offline sync...", false);
        forgetTunnel(tunnel_id);
        // Asynchronous: never scan the database on the tunnel thread.
        if (isPull) requestOfflinePull(groupId, localPath);
        else requestOfflineClone(groupId, localPath);
    }
}

void p3Git::sendOutgoingChunks()
{
    if (!mGxsTunnels) return;
    std::vector<std::pair<RsGxsTunnelId, QByteArray> > frames;
    std::vector<RsGxsTunnelId> finished;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto &entry : mOutgoing) {
            OutgoingTransfer &transfer = entry.second;
            for (int i = 0; i < CHUNKS_PER_TICK && transfer.offset < transfer.response.size(); ++i) {
                QJsonObject frame;
                frame["type"] = "transfer_chunk";
                frame["group_id"] = transfer.groupId;
                frame["size"] = transfer.response.size();
                frame["index"] = transfer.offset / GitTunnelTransfer::ChunkSize;
                frame["data"] = QString::fromLatin1(transfer.response.mid(transfer.offset, GitTunnelTransfer::ChunkSize).toBase64());
                frames.push_back(std::make_pair(entry.first, QJsonDocument(frame).toJson(QJsonDocument::Compact)));
                transfer.offset += GitTunnelTransfer::ChunkSize;
            }
            if (transfer.offset >= transfer.response.size()) finished.push_back(entry.first);
        }
        for (const auto &id : finished) {
            mOutgoing.erase(id);
            mServingTunnels.erase(id);
        }
    }
    std::set<RsGxsTunnelId> failed;
    for (const auto &frame : frames) {
        if (failed.count(frame.first)) continue;
        if (!mGxsTunnels->sendData(frame.first, RETRO_GIT_GXS_TUNNEL_SERVICE_ID,
                reinterpret_cast<const uint8_t*>(frame.second.constData()), frame.second.size())) {
            failed.insert(frame.first);
        }
    }
    for (const auto &id : failed) {
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            mOutgoing.erase(id);
            mServingTunnels.erase(id);
        }
        std::cerr << "p3Git: failed to queue repository transfer chunk on tunnel " << id << std::endl;
    }
}

void p3Git::serveRequestJob(const RsGxsTunnelId &id, const RsGxsId &requester, QString type, QString groupIdStr,
                            std::set<std::string> known, bool chunked)
{
    auto finish = [this, &id]() {
        RS_STACK_MUTEX(mRetroGitMtx);
        if (!mOutgoing.count(id)) mServingTunnels.erase(id);
    };
    auto fail = [&](const QString &error) {
        sendTransferError(id, groupIdStr, error);
        finish();
    };
    if (mStopping) { finish(); return; }

    const RsGxsGroupId groupId(groupIdStr.toStdString());
    // Only the owner serves a repository, and only a public one: anybody can
    // open a tunnel to any identity, so circle-restricted repositories are only
    // distributed through GXS, which enforces the circle.
    std::vector<RsGitGroup> groups;
    if (!getGroups({groupId}, groups) || groups.empty() ||
        !IS_GROUP_ADMIN(groups.front().mMeta.mSubscribeFlags)) {
        fail("Repository is unavailable on this client.");
        return;
    }
    const uint32_t circleType = groups.front().mMeta.mCircleType;
    if (circleType != static_cast<uint32_t>(RsGxsCircleType::PUBLIC) &&
        circleType != static_cast<uint32_t>(RsGxsCircleType::UNKNOWN)) {
        std::cerr << "p3Git: refusing tunnel transfer of restricted repository " << groupIdStr.toStdString()
                  << " to " << requester << std::endl;
        fail("This repository is restricted. Use Offline clone (published packs) instead.");
        return;
    }

    const std::string barePath = GitManager::getBareRepoPath(groupIdStr.toStdString());
    if (!GitManager::isValidRepository(barePath)) {
        std::cerr << "p3Git: Requested repository is not valid locally: " << barePath << std::endl;
        fail("Repository is unavailable on this client. Commit and Push / Publish on the owner first.");
        return;
    }

    std::string packfileData;
    std::map<std::string, std::string> refUpdates;
    if (!GitManager::createPackfile(barePath, packfileData, refUpdates, known)) {
        std::cerr << "p3Git: Failed to create packfile for group " << groupIdStr.toStdString() << std::endl;
        fail("Failed to create repository packfile on the owner.");
        return;
    }
    if (refUpdates.empty()) {
        fail("Repository has no published commits. Commit and Push / Publish on the owner first.");
        return;
    }
    std::cout << "p3Git: Generated packfile of size " << packfileData.size() << " bytes. Sending..." << std::endl;

    // Build the JSON response by hand: base64 needs no escaping and this avoids
    // several full-size copies of the pack (QVariant, UTF-16 QString, ...).
    QJsonObject refs;
    for (auto const& pair : refUpdates)
        refs[QString::fromStdString(pair.first)] = QString::fromStdString(pair.second);
    QJsonObject header;
    header["type"] = (type == "pull_req") ? "pull_res" : "clone_res";
    header["group_id"] = groupIdStr;
    header["ref_updates"] = refs;
    QByteArray response = QJsonDocument(header).toJson(QJsonDocument::Compact);
    response.chop(1); // remove closing '}'
    const qint64 encodedSize = (static_cast<qint64>(packfileData.size()) + 2) / 3 * 4;
    if (response.size() + encodedSize + 16 > GitTunnelTransfer::MaxSize) {
        fail("Repository exceeds the 128 MiB tunnel response limit. Use published pack downloads instead.");
        return;
    }
    response.reserve(response.size() + static_cast<int>(encodedSize) + 16);
    response.append(",\"packfile\":\"");
    response.append(QByteArray::fromRawData(packfileData.data(), static_cast<int>(packfileData.size())).toBase64());
    response.append("\"}");
    std::string().swap(packfileData);

    if (!chunked) {
        if (response.size() > GitTunnelTransfer::ChunkSize)
            fail("Update the receiving RetroGit plugin to support chunked repository transfers.");
        else {
            if (!mGxsTunnels->sendData(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID,
                    reinterpret_cast<const uint8_t*>(response.constData()), response.size()))
                std::cerr << "p3Git: Failed to queue repository response." << std::endl;
            finish();
        }
        return;
    }

    {
        RS_STACK_MUTEX(mRetroGitMtx);
        qint64 queued = 0;
        for (const auto &transfer : mOutgoing) queued += transfer.second.response.size() - transfer.second.offset;
        if (queued + response.size() <= MAX_OUTGOING_BYTES) {
            OutgoingTransfer transfer;
            transfer.response = response;
            transfer.groupId = groupIdStr;
            mOutgoing[id] = transfer;
            return; // sent progressively by service_tick()
        }
    }
    fail("The owner is busy sending other repositories. Please retry later.");
}

void p3Git::handleResponseJob(const RsGxsTunnelId &id, QByteArray json)
{
    RsGxsGroupId expected;
    bool expectsPull = false;
    std::string localPath;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto clone = mPendingClones.find(id);
        auto pull = mPendingPulls.find(id);
        if (clone != mPendingClones.end()) { expected = clone->second.groupId; localPath = clone->second.localPath; }
        else if (pull != mPendingPulls.end()) { expected = pull->second.groupId; localPath = pull->second.localPath; expectsPull = true; }
    }
    if (expected.isNull()) return;

    std::string packfileData;
    std::map<std::string, std::string> refUpdates;
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(json, &error);
        json.clear();
        const QJsonObject map = doc.object();
        const QString type = map.value("type").toString();
        if (doc.isNull() || type != (expectsPull ? "pull_res" : "clone_res") ||
            map.value("group_id").toString().toStdString() != expected.toStdString()) {
            failTunnelTransfer(id, "Invalid repository transfer response received.");
            return;
        }
        const QJsonObject refs = map.value("ref_updates").toObject();
        for (auto it = refs.begin(); it != refs.end(); ++it)
            refUpdates[it.key().toStdString()] = it.value().toString().toStdString();
        QByteArray base64Pack = map.value("packfile").toString().toLatin1();
        doc = QJsonDocument();
        const QByteArray decoded = QByteArray::fromBase64(base64Pack);
        base64Pack.clear();
        packfileData.assign(decoded.constData(), decoded.size());
    }

    // The transfer is complete: stop tracking the tunnel before the long work.
    forgetTunnel(id);
    if (mGxsTunnels) mGxsTunnels->closeExistingTunnel(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID);

    const std::string barePath = GitManager::getBareRepoPath(expected.toStdString());
    if (!expectsPull) {
        postCloneStatus(expected, "Received repository data. Unpacking...", false);
        if (GitManager::unpackPackfile(barePath, packfileData, refUpdates)) {
            noteRefsApplied(expected, refUpdates, time(NULL)); // the owner's current state
            postCloneStatus(expected, "Repository unpacked. Writing working tree...", false);
            if (GitManager::cloneRepository(barePath, localPath)) {
                std::cout << "p3Git: Successfully cloned/checked out bare repo to working path: " << localPath << std::endl;
                postCloneStatus(expected, "Clone completed successfully!", true);
                postGitEvent(RsGitEventCode::NEW_POST, expected);
            } else {
                std::cerr << "p3Git: Failed to clone bare repo to working path: " << localPath << std::endl;
                postCloneStatus(expected, "Failed to write working tree to selected path.", false);
            }
        } else {
            std::cerr << "p3Git: Failed to unpack packfile to bare repository: " << barePath << std::endl;
            postCloneStatus(expected, "Failed to unpack repository packfile.", false);
        }
        return;
    }

    postCloneStatus(expected, "Received repository changes. Unpacking...", false);
    if (!GitManager::unpackPackfile(barePath, packfileData, refUpdates)) {
        postCloneStatus(expected, "Failed to unpack sync packfile.", false);
        return;
    }
    noteRefsApplied(expected, refUpdates, time(NULL)); // the owner's current state
    bool pullSuccess = false;
    if (!localPath.empty() && GitManager::isValidRepository(localPath)) {
        postCloneStatus(expected, "Syncing working tree...", false);
        std::string error;
        if (GitManager::pullRepository(localPath, barePath, &error)) {
            postCloneStatus(expected, "Sync completed successfully!", true);
            pullSuccess = true;
        } else {
            postCloneStatus(expected, "Failed to update working tree with new changes. " + error, false);
        }
    } else {
        postCloneStatus(expected, "Bare repository synced successfully!", true);
        pullSuccess = true;
    }
    if (pullSuccess) postGitEvent(RsGitEventCode::NEW_POST, expected);
}

void p3Git::receiveData(const RsGxsTunnelId& id, unsigned char *data, uint32_t data_size)
{
    // Runs on the GXS tunnel thread (shared with distant chat): parse and
    // dispatch only, heavy work goes to the worker.
    const QByteArray message(reinterpret_cast<const char*>(data), static_cast<int>(data_size));
    free(data); // Ownership was transferred to us by the tunnel service.

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        std::cerr << "p3Git: Failed to parse received JSON data from tunnel." << std::endl;
        failTunnelTransfer(id, "Invalid repository response received.");
        return;
    }
    const QJsonObject map = doc.object();
    const QString type = map.value("type").toString();

    if (type == "transfer_chunk" || type == "transfer_error" ||
        type == "clone_res" || type == "pull_res") {
        RsGxsGroupId expected;
        bool expectsPull = false;
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            auto clone = mPendingClones.find(id);
            auto pull = mPendingPulls.find(id);
            if (clone != mPendingClones.end()) expected = clone->second.groupId;
            else if (pull != mPendingPulls.end()) {
                expected = pull->second.groupId;
                expectsPull = true;
            }
        }
        if (expected.isNull()) return;
        if (map.value("group_id").toString().toStdString() != expected.toStdString() ||
            (type == "clone_res" && expectsPull) || (type == "pull_res" && !expectsPull)) {
            failTunnelTransfer(id, "Received a response for the wrong repository or operation.");
            return;
        }
        if (type == "transfer_error") {
            failTunnelTransfer(id, "Owner could not send repository: " + map.value("error").toString().left(1024).toStdString());
            return;
        }
        if (type != "transfer_chunk") {
            // Legacy single-message response.
            runJob([this, id, message]() { handleResponseJob(id, message); });
            return;
        }

        QByteArray complete;
        int state;
        uint64_t received = 0, total = 0;
        bool advanced = false;
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            auto& assembly = mIncomingTransfers[id];
            const size_t previous = assembly.chunks.size();
            state = assembly.add(map.toVariantMap(), complete);
            advanced = assembly.chunks.size() > previous;
            if (advanced)
                mTransferActivity[id] = std::chrono::steady_clock::now();
            if (state >= 0) {
                total = assembly.size;
                for (const auto& chunk : assembly.chunks) received += chunk.second.size();
            }
            if (state != 0) mIncomingTransfers.erase(id);
        }
        // One progress event per chunk would flood the GUI: at most 4 per second.
        bool report = false;
        if (advanced && state >= 0) {
            const auto now = std::chrono::steady_clock::now();
            RS_STACK_MUTEX(mRetroGitMtx);
            auto &last = mLastProgressPost[id];
            if (state == 1 || now - last >= std::chrono::milliseconds(250)) {
                last = now;
                report = true;
            }
            if (state == 1) mLastProgressPost.erase(id);
        }
        if (report)
            postCloneStatus(expected, "Receiving repository data", false, received, total);
        if (state < 0) {
            failTunnelTransfer(id, "Invalid repository transfer chunk received.");
            return;
        }
        if (state == 1)
            runJob([this, id, complete]() { handleResponseJob(id, complete); });
        return;
    }

    if (type == "clone_req" || type == "pull_req") {
        const QString groupIdStr = map.value("group_id").toString();
        std::cout << "p3Git: Received request " << type.toStdString() << " for group " << groupIdStr.toStdString() << std::endl;

        // Validate the identifier before using it as part of a filesystem path.
        RsGxsGroupId requested(groupIdStr.toStdString());
        if (requested.isNull() || requested.toStdString() != groupIdStr.toStdString()) {
            sendTransferError(id, groupIdStr, "Invalid repository identifier.");
            return;
        }

        std::set<std::string> known;
        if (type == "pull_req") {
            const QJsonArray have = map.value("have").toArray();
            for (int i = 0; i < have.size() && i < 256; ++i) {
                const std::string tip = have.at(i).toString().toStdString();
                if (tip.size() == 40) known.insert(tip);
            }
        }

        RsGxsId requester;
        {
            // One request per tunnel and a small global limit: each request
            // packs a whole repository.
            RS_STACK_MUTEX(mRetroGitMtx);
            auto peer = mTunnelToGxsIdMap.find(id);
            if (peer != mTunnelToGxsIdMap.end()) requester = peer->second;
            if (mServingTunnels.count(id) || mServingTunnels.size() >= MAX_CONCURRENT_SERVED_REQUESTS) {
                requester = RsGxsId();
                known.clear();
            } else {
                mServingTunnels.insert(id);
                const bool chunked = map.value("chunked").toBool();
                runJob([this, id, peerId = requester, type, groupIdStr, known, chunked]() {
                    serveRequestJob(id, peerId, type, groupIdStr, known, chunked);
                });
                return;
            }
        }
        sendTransferError(id, groupIdStr, "The owner is busy sending other repositories. Please retry later.");
    }
}

void p3Git::connectToGxsTunnelService(RsGxsTunnelService *tunnel_service)
{
    mGxsTunnels = tunnel_service;
    if (mGxsTunnels) {
        std::cout << "p3Git: Registering GXS Tunnel client (Service ID: " << RETRO_GIT_GXS_TUNNEL_SERVICE_ID << ")" << std::endl;
        if (!mGxsTunnels->registerClientService(RETRO_GIT_GXS_TUNNEL_SERVICE_ID, this)) {
            std::cerr << "p3Git: FAILED to register GXS Tunnel client!" << std::endl;
        }
    }
}

bool p3Git::acceptDataFromPeer(const RsGxsId& gxs_id, const RsGxsTunnelId& tunnel_id, bool am_I_client_side)
{
    if (retroGitDebugEnabled())
        std::cout << "p3Git: acceptDataFromPeer: gxs=" << gxs_id
                  << " tunnel=" << tunnel_id
                  << " side=" << (am_I_client_side ? "client" : "server") << std::endl;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        mTunnelToGxsIdMap[tunnel_id] = gxs_id;
    }
    return true;
}
