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
#include <filesystem>

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
// Offline clones/pulls give up when the packs do not arrive in this time.
const auto OFFLINE_TIMEOUT = std::chrono::hours(24);
// Fire-and-forget tokens still pending after this long are cancelled.
const auto TOKEN_TIMEOUT = std::chrono::minutes(5);

// True when the published commits are already in the repository (e.g. our own
// update coming back from GXS): their branches can be set without re-importing
// a pack that may be hundreds of MB.
bool applyIfAlreadyPresent(const std::string &repoPath, const std::map<std::string, std::string> &allRefs,
                           const std::map<std::string, std::string> &refsToApply)
{
    if (allRefs.empty()) return false;
    std::set<std::string> targets;
    for (const auto &ref : allRefs) targets.insert(ref.second);
    return GitManager::filterExistingCommits(repoPath, targets).size() == targets.size() &&
           GitManager::applyRefUpdates(repoPath, refsToApply);
}

// Path of a published pack available on this computer. A finished download
// is found even when the download folder is not shared (or not indexed yet);
// after a restart it is found in the download folder by name and size.
bool findPackFile(const RsFileHash &hash, const std::string &name, uint64_t size, std::string &path)
{
    if (!rsFiles || hash.isNull()) return false;
    FileInfo info;
    if (rsFiles->FileDetails(hash, RS_FILE_HINTS_DOWNLOAD | RS_FILE_HINTS_SPEC_ONLY, info) &&
        info.downloadStatus == FT_STATE_COMPLETE && !info.path.empty()) {
        path = info.path;
        return true;
    }
    if (rsFiles->alreadyHaveFile(hash, info) && !info.path.empty()) { // shared or extra (own) files
        path = info.path;
        return true;
    }
    // The name comes from a remote message: accept a plain file name only.
    if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos)
        return false;
    const std::string candidate = rsFiles->getDownloadDirectory() + "/" + name;
    std::error_code ec;
    const auto found = std::filesystem::file_size(std::filesystem::u8path(candidate), ec);
    if (ec || found != size) return false;
    path = candidate;
    return true;
}

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
      RsGxsIfaceHelper(static_cast<RsGxsIface &>(*this)), mRetroGitMtx("p3Git"), mNotify(notifier),
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
                const RsGxsFile file = gitMsg->mFiles.empty() ? RsGxsFile() : gitMsg->mFiles[0];
                const std::map<std::string, std::string> refs = gitMsg->mRefUpdates;
                const rstime_t publishTs = gitMsg->meta.mPublishTs;
                runJob([this, groupId, msgId, packData, file, refs, publishTs]() {
                    unpackRootUpdateJob(groupId, msgId, packData, file, refs, publishTs);
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
                                const RsGxsFile &file, const std::map<std::string, std::string> &refUpdates,
                                rstime_t publishTs)
{
    const std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    const RsFileHash &fileHash = file.mHash;
    bool unpacked = false;
    if (!fileHash.isNull()) {
        std::string packPath;
        if (findPackFile(fileHash, file.mName, file.mSize, packPath)) {
            const auto refs = refsNotOlderThan(groupId, refUpdates, publishTs);
            unpacked = applyIfAlreadyPresent(repoPath, refUpdates, refs) ||
                       GitManager::unpackPackfileFromFile(repoPath, packPath, refs);
        } else {
            PendingPackfile pending;
            pending.groupId = groupId;
            pending.msgId = msgId;
            pending.fileHash = fileHash;
            pending.fileName = file.mName;
            pending.fileSize = file.mSize;
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
        const auto refs = refsNotOlderThan(groupId, refUpdates, publishTs);
        unpacked = applyIfAlreadyPresent(repoPath, refUpdates, refs) ||
                   GitManager::unpackPackfile(repoPath, packData, refs);
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
    const auto now = std::chrono::steady_clock::now();
    if (now - mLastSlowTick < std::chrono::seconds(1)) return;
    mLastSlowTick = now;

    acknowledgeTrackedTokens();

    // Downloaded packs are unpacked on the worker.
    std::vector<PendingPackfile> completed;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto it = mPendingPackfiles.begin(); it != mPendingPackfiles.end(); ) {
            std::string packPath;
            if (findPackFile(it->fileHash, it->fileName, it->fileSize, packPath)) {
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
    std::string packPath;
    bool unpacked = false;
    if (findPackFile(pending.fileHash, pending.fileName, pending.fileSize, packPath)) {
        std::cout << "p3Git: Pending packfile downloaded: " << packPath << ", unpacking..." << std::endl;
        const auto refs = refsNotOlderThan(pending.groupId, pending.refUpdates, pending.publishTs);
        unpacked = applyIfAlreadyPresent(repoPath, pending.refUpdates, refs) ||
                   GitManager::unpackPackfileFromFile(repoPath, packPath, refs);
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
    // Name and size of the attachment help to find it after a restart.
    PendingPackfile pending;
    pending.groupId = groupId;
    pending.msgId = msgId;
    pending.fileHash = fileHash;
    pending.refUpdates = refUpdates;
    MsgCache cache;
    if (cachedMessages(groupId, cache))
        for (const auto &update : cache.updates)
            if (update.mMeta.mMsgId == msgId) {
                pending.publishTs = update.mMeta.mPublishTs;
                for (const auto &file : update.mFiles)
                    if (file.mHash == fileHash) { pending.fileName = file.mName; pending.fileSize = file.mSize; }
            }
    std::string packPath;
    if (!findPackFile(fileHash, pending.fileName, pending.fileSize, packPath)) return false;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        // Already unpacked but waiting for an earlier pack: retried automatically.
        if (mIncompleteUpdates.count(RsGxsGrpMsgIdPair(groupId, msgId))) return false;
        if (!mUnpacking.insert(RsGxsGrpMsgIdPair(groupId, msgId)).second) return true; // already queued
        for (auto it = mPendingPackfiles.begin(); it != mPendingPackfiles.end(); )
            if (it->fileHash == fileHash && it->groupId == groupId) it = mPendingPackfiles.erase(it); else ++it;
    }
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

bool p3Git::updateMilestones(const RsGxsGroupId &groupId, const std::string &expected, const std::string &data)
{
    if (data.size() > 64000) return false;
    std::vector<RsGitGroup> groups;
    invalidateGroups();
    if (!getGroups({groupId}, groups) || groups.size() != 1 ||
        !IS_GROUP_ADMIN(groups.front().mMeta.mSubscribeFlags) ||
        groups.front().mMilestones != expected) return false;
    auto group = groups.front();
    group.mMilestones = data;
    const bool ok = updateGroup(group);
    invalidateGroups();
    return ok;
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
        const auto &file = pr.mFiles.front();
        std::string packPath;
        if (findPackFile(file.mHash, file.mName, file.mSize, packPath))
            imported = GitManager::unpackPackfileFromFile(path, packPath, {});
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
        std::string packPath;
        if (!findPackFile(file.mHash, file.mName, file.mSize, packPath)) {
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
            std::string packPath;
            if (findPackFile(file.mHash, file.mName, file.mSize, packPath)) {
                if (!GitManager::unpackPackfileFromFile(barePath, packPath, {}))
                    std::cerr << "p3Git: " << label << ": pack " << packPath << " not complete yet for msg "
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
                pending.fileName = file.mName;
                pending.fileSize = file.mSize;
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
    auto offline = mPendingOffline.find(groupId);
    return offline != mPendingOffline.end() && !offline->second.isPull;
}
