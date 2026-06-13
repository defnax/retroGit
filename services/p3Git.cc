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
#include <string>
#include <vector>

#include "gui/RetroGitNotify.h"
#include "p3Git.h"
#include "rsGitItems.h"
#include "GitManager.h"
#include <QJsonDocument>
#include <QVariantMap>

RsGit *rsGit = NULL;

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
      RsGxsIfaceHelper(static_cast<RsGxsIface &>(*this)), mRetroGitMtx("p3Git"), mNotify(notifier), mGxsTunnels(NULL)
{
    rsGit = this;
}

p3Git::~p3Git()
{
    rsGit = NULL;
}

RsServiceInfo p3Git::getServiceInfo()
{
    const std::string RETRO_GIT_APP_NAME = "RetroGit";
    const uint16_t RETRO_GIT_APP_MAJOR_VERSION = 1;
    const uint16_t RETRO_GIT_APP_MINOR_VERSION = 0;
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

void p3Git::service_tick()
{
    std::list<PendingPackfile> completed;
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
    
    for (const auto& pending : completed) {
        std::string repoPath = GitManager::getBareRepoPath(pending.groupId.toStdString());
        FileInfo fi;
        if (rsFiles && rsFiles->FileDetails(pending.fileHash, RS_FILE_HINTS_LOCAL, fi)) {
            std::cout << "p3Git: Pending packfile downloaded: " << fi.path << ", unpacking..." << std::endl;
            if (GitManager::unpackPackfileFromFile(repoPath, fi.path, pending.refUpdates)) {
                uint32_t pToken;
                setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(pending.groupId, pending.msgId), true);
                
                // Update pending offline clones & pulls
                RS_STACK_MUTEX(mRetroGitMtx);
                for (auto &poc : mPendingOfflineClones) {
                    if (poc.groupId == pending.groupId) {
                        poc.pendingMsgIds.erase(pending.msgId);
                    }
                }
                for (auto &pop : mPendingOfflinePulls) {
                    if (pop.groupId == pending.groupId) {
                        pop.pendingMsgIds.erase(pending.msgId);
                    }
                }
            }
            
            if (rsEvents) {
                RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
                auto ev = std::make_shared<RsGitEvent>(gitEventType);
                ev->mGitGroupId = pending.groupId;
                ev->mGitEventCode = RsGitEventCode::NEW_POST;
                rsEvents->postEvent(ev);
            }
        }
    }

    // Check completed offline clones
    std::vector<PendingOfflineClone> completedClones;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto it = mPendingOfflineClones.begin(); it != mPendingOfflineClones.end(); ) {
            if (it->pendingMsgIds.empty()) {
                completedClones.push_back(*it);
                it = mPendingOfflineClones.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto &pc : completedClones) {
        std::string barePath = GitManager::getBareRepoPath(pc.groupId.toStdString());
        std::cout << "p3Git: Offline clone: all packfiles downloaded. Cloning bare repo to " << pc.localPath << std::endl;
        postCloneStatus(pc.groupId, "Offline clone: writing working tree...", false);
        if (GitManager::cloneRepository(barePath, pc.localPath)) {
            postCloneStatus(pc.groupId, "Offline clone completed successfully!", true);
        } else {
            postCloneStatus(pc.groupId, "Offline clone failed to write working tree.", false);
        }
    }

    // Check completed offline pulls
    std::vector<PendingOfflinePull> completedPulls;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        for (auto it = mPendingOfflinePulls.begin(); it != mPendingOfflinePulls.end(); ) {
            if (it->pendingMsgIds.empty()) {
                completedPulls.push_back(*it);
                it = mPendingOfflinePulls.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto &pp : completedPulls) {
        std::cout << "p3Git: Offline sync: all packfiles downloaded. Syncing working tree at " << pp.localPath << std::endl;
        bool hasWorkingDir = !pp.localPath.empty() && GitManager::isValidRepository(pp.localPath);
        if (hasWorkingDir) {
            postCloneStatus(pp.groupId, "Offline sync: updating working tree...", false);
            if (GitManager::pullRepository(pp.localPath)) {
                postCloneStatus(pp.groupId, "Offline sync completed successfully!", true);
            } else {
                postCloneStatus(pp.groupId, "Offline sync failed to update working tree.", false);
            }
        } else {
            postCloneStatus(pp.groupId, "Offline sync completed successfully!", true);
        }
    }
}

RsTokenService *p3Git::getTokenService()
{
    return RsGenExchange::getTokenService();
}

void p3Git::notifyChanges(std::vector<RsGxsNotify *> &changes)
{
  std::cerr << "p3Git::notifyChanges() " << changes.size() << " changes"
            << std::endl;

  for (auto it = changes.begin(); it != changes.end(); ++it) {
    /* ---- Message changes (new commit / post) ---- */
    RsGxsMsgChange *msgChange = dynamic_cast<RsGxsMsgChange *>(*it);
    if (msgChange) {
      std::cerr << "p3Git: Message change notification. Type: ";
      switch (msgChange->getType()) {
        case RsGxsNotify::TYPE_PUBLISHED: std::cerr << "TYPE_PUBLISHED"; break;
        case RsGxsNotify::TYPE_RECEIVED_NEW: std::cerr << "TYPE_RECEIVED_NEW"; break;
        case RsGxsNotify::TYPE_PROCESSED: std::cerr << "TYPE_PROCESSED"; break;
        case RsGxsNotify::TYPE_MESSAGE_DELETED: std::cerr << "TYPE_MESSAGE_DELETED"; break;
        default: std::cerr << msgChange->getType(); break;
      }
      std::cerr << ", GroupId: " << msgChange->mGroupId.toStdString()
                << ", MsgId: " << msgChange->mMsgId.toStdString();
      if (msgChange->mNewMsgItem) {
          RsGitMsgItem* gitMsg = dynamic_cast<RsGitMsgItem*>(msgChange->mNewMsgItem);
          if (gitMsg) {
              std::cerr << ", MsgType: " << (gitMsg->mGitMsgType == 1 ? "UPDATE" : gitMsg->mGitMsgType == 2 ? "PULL_REQUEST" : "UNKNOWN")
                        << ", Author Identity: " << gitMsg->meta.mAuthorId.toStdString();
          }
      }
      std::cerr << std::endl;

      if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW ||
          msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED) {
          
        bool unpackedImmediately = false;
        if (msgChange->mNewMsgItem) {
            RsGitMsgItem* gitMsg = dynamic_cast<RsGitMsgItem*>(msgChange->mNewMsgItem);
            if (gitMsg && gitMsg->mGitMsgType == 1) // UPDATE
            {
                std::string repoPath = GitManager::getBareRepoPath(msgChange->mGroupId.toStdString());
                if (!gitMsg->mFiles.empty()) {
                    const RsGxsFile& packFile = gitMsg->mFiles[0];
                    FileInfo info;
                    if (rsFiles && rsFiles->alreadyHaveFile(packFile.mHash, info)) {
                        if (GitManager::unpackPackfileFromFile(repoPath, info.path, gitMsg->mRefUpdates)) {
                            unpackedImmediately = true;
                        }
                    } else {
                        PendingPackfile pending;
                        pending.groupId = msgChange->mGroupId;
                        pending.msgId = msgChange->mMsgId;
                        pending.fileHash = packFile.mHash;
                        pending.refUpdates = gitMsg->mRefUpdates;
                        
                        RS_STACK_MUTEX(mRetroGitMtx);
                        mPendingPackfiles.push_back(pending);
                    }
                } else if (!gitMsg->mPackfileData.empty()) {
                    if (GitManager::unpackPackfile(repoPath, gitMsg->mPackfileData, gitMsg->mRefUpdates)) {
                        unpackedImmediately = true;
                    }
                }
            }
        }

        if (unpackedImmediately) {
          uint32_t pToken;
          setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(msgChange->mGroupId, msgChange->mMsgId), true);
          if (rsEvents) {
            RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
            auto ev = std::make_shared<RsGitEvent>(gitEventType);
            ev->mGitGroupId = msgChange->mGroupId;
            ev->mGitMsgId = msgChange->mMsgId;
            ev->mGitEventCode = RsGitEventCode::NEW_POST;
            rsEvents->postEvent(ev);
          }
        }
      }
      continue;
    }

    /* ---- Group changes (new repo / subscription / update) ---- */
    RsGxsGroupChange *grpChange = dynamic_cast<RsGxsGroupChange *>(*it);
    if (grpChange) {
      std::cerr << "p3Git: Group change notification. Type: ";
      switch (grpChange->getType()) {
        case RsGxsNotify::TYPE_PUBLISHED: std::cerr << "TYPE_PUBLISHED"; break;
        case RsGxsNotify::TYPE_RECEIVED_NEW: std::cerr << "TYPE_RECEIVED_NEW"; break;
        case RsGxsNotify::TYPE_PROCESSED: std::cerr << "TYPE_PROCESSED"; break;
        case RsGxsNotify::TYPE_UPDATED: std::cerr << "TYPE_UPDATED"; break;
        case RsGxsNotify::TYPE_GROUP_DELETED: std::cerr << "TYPE_GROUP_DELETED"; break;
        default: std::cerr << grpChange->getType(); break;
      }
      std::cerr << ", GroupId: " << grpChange->mGroupId.toStdString();
      if (grpChange->mNewGroupItem) {
          RsGitGroupItem* gitItem = dynamic_cast<RsGitGroupItem*>(grpChange->mNewGroupItem);
          if (gitItem) {
              std::cerr << ", Name: \"" << gitItem->mGroup.mGroupName << "\""
                        << ", Creator Identity: " << gitItem->meta.mAuthorId.toStdString()
                        << ", Originator Peer: " << gitItem->meta.mOriginator.toStdString();
          }
      }
      std::cerr << std::endl;

      if (rsEvents) {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);
        ev->mGitGroupId = grpChange->mGroupId;

        switch (grpChange->getType()) {
        case RsGxsNotify::TYPE_PUBLISHED:
        case RsGxsNotify::TYPE_PROCESSED:
        case RsGxsNotify::TYPE_RECEIVED_NEW:
        case RsGxsNotify::TYPE_UPDATED:
          {
              std::string repoPath = GitManager::getBareRepoPath(grpChange->mGroupId.toStdString());
              GitManager::initRepository(repoPath);
          }

          if (grpChange->getType() == RsGxsNotify::TYPE_PUBLISHED)
              ev->mGitEventCode = RsGitEventCode::GIT_UPDATED;
          else if (grpChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW)
              ev->mGitEventCode = RsGitEventCode::NEW_GIT;
          else // TYPE_PROCESSED or TYPE_UPDATED
              ev->mGitEventCode = RsGitEventCode::GIT_UPDATED;

          rsEvents->postEvent(ev);
          break;

        default:
          break;
        }
      }
    }
  }
}

// Blocking Interfaces.
bool p3Git::createGroup(RsGitGroup &group)
{
    uint32_t token;
    return createGroup(token, group) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Git::getGroups(const std::list<RsGxsGroupId> &groupIds,std::vector<RsGitGroup> &groups)
{
    std::cerr << "p3Git::getGroups() called. Requested groupIds count: " << groupIds.size() << std::endl;
    if (rsServiceControl) {
        std::set<RsPeerId> onlinePeers;
        rsServiceControl->getPeersConnected(getServiceInfo().mServiceType, onlinePeers);
        std::cerr << "p3Git::getGroups() - Online peers supporting RetroGit: " << onlinePeers.size() << std::endl;
        for (auto const& peer : onlinePeers) {
            std::cerr << "  - PeerId: " << peer.toStdString() << std::endl;
        }
    }
    uint32_t token;
    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

    if (groupIds.empty()) {
    if (!requestGroupInfo(token, opts) ||
        waitToken(token, std::chrono::milliseconds(5000)) !=
            RsTokenService::COMPLETE) {
      std::cerr << "p3Git::getGroups() token wait failed/timed out for general request." << std::endl;
      return false;
    }
    } else {
    if (!requestGroupInfo(token, opts, groupIds) ||
        waitToken(token, std::chrono::milliseconds(5000)) !=
            RsTokenService::COMPLETE) {
      std::cerr << "p3Git::getGroups() token wait failed/timed out for specific request." << std::endl;
      return false;
    }
    }

    std::vector<RsGxsGrpItem *> grpItems;
    if (!RsGenExchange::getGroupData(token, grpItems)) {
        std::cerr << "p3Git::getGroups() failed to get group data for token: " << token << std::endl;
        return false; // Real error: token or data retrieval failed
    }

    std::cerr << "p3Git::getGroups() retrieved " << grpItems.size() << " group items from GXS database." << std::endl;

    for (std::vector<RsGxsGrpItem *>::iterator it = grpItems.begin();
         it != grpItems.end(); ++it) {
        RsGitGroupItem *gitItem = dynamic_cast<RsGitGroupItem *>(*it);
        if (gitItem) {
            RsGitGroup g = gitItem->mGroup;
            g.mMeta = gitItem->meta;
            if (g.mGroupName.empty() && !g.mMeta.mGroupName.empty()) {
                g.mGroupName = g.mMeta.mGroupName;
            }
            groups.push_back(g);
            std::cerr << "p3Git::getGroups() populated group name: \"" << g.mGroupName << "\""
                      << ", meta name: \"" << g.mMeta.mGroupName << "\""
                      << ", GroupId: " << g.mMeta.mGroupId.toStdString()
                      << ", Raw Flags: " << g.mMeta.mSubscribeFlags
                      << std::endl;
        } else {
            std::cerr << "p3Git::getGroups() dynamic_cast to RsGitGroupItem failed for retrieved item!" << std::endl;
        }
        delete *it;
    }

    std::cerr << "p3Git::getGroups() returning true. Populated groups count: " << groups.size() << std::endl;
    // Return true even when groups is empty — that is a valid state
    return true;
}

// Function which will update the edited information in the git group
bool p3Git::createGroup(uint32_t &token, RsGitGroup &group)
{
    // Create a GXS Group Item
    RsGitGroupItem *grpItem = new RsGitGroupItem();

    // Populate the item with the provided group data
    grpItem->mGroup = group;
    grpItem->meta =
      group.mMeta; // Ensure metadata (like creator GxsId) is preserved

    // Publish to GXS (Generic Exchange Service)
    this->publishGroup(token, grpItem);

    return true;
}

bool p3Git::updateGroup(uint32_t &token, RsGitGroup &group)
{
    // Create a GXS Group Item
    RsGitGroupItem *grpItem = new RsGitGroupItem();

    // Populate the item with the provided group data
    grpItem->mGroup = group;
    grpItem->meta = group.mMeta; // Ensure metadata (like creator GxsId) is preserved

    // Publish to GXS (Generic Exchange Service)
    this->RsGenExchange::updateGroup(token, grpItem);

    return true;
}

bool p3Git::updateGroup(RsGitGroup &group)
{
    uint32_t token;
    return updateGroup(token, group) && waitToken(token) == RsTokenService::COMPLETE;
}

bool p3Git::publishGitUpdate(uint32_t &token, RsGitUpdate &update)
{
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
    
    this->publishMsg(token, msgItem);
    return true;
}

bool p3Git::publishPullRequest(uint32_t &token, RsGitPullRequest &pr)
{
    RsGitMsgItem *msgItem = new RsGitMsgItem();
    msgItem->meta = pr.mMeta;
    
    msgItem->meta.mParentId.clear();
    msgItem->meta.mThreadId.clear();
    
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
    msgItem->mPackfileData = pr.mPackfileData;
    msgItem->mStatus = pr.mStatus;
    msgItem->mFiles = pr.mFiles;
    
    this->publishMsg(token, msgItem);
    return true;
}

bool p3Git::subscribeToGroup(uint32_t &token, const RsGxsGroupId &groupId,bool subscribe_flag)
{
    bool response = RsGenExchange::subscribeToGroup(token, groupId, subscribe_flag);

    if (response && rsEvents)
    {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);
        ev->mGitGroupId = groupId;
        ev->mGitEventCode = RsGitEventCode::SUBSCRIBE_STATUS_CHANGED;
        rsEvents->postEvent(ev);
    }

    return response;
}

bool p3Git::subscribe(const RsGxsGroupId &groupId, bool subscribe_flag)
{
    uint32_t token;
    return subscribeToGroup(token, groupId, subscribe_flag);
}

bool p3Git::setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId, bool read)
{
    uint32_t token;
    setMessageReadStatus(token, msgId, read);

    if(waitToken(token) != RsTokenService::COMPLETE)
    {
        std::cerr << "p3Git::setMessageReadStatus() waitToken failed" << std::endl;
        return false;
    }

    RsGxsGrpMsgIdPair p;
    acknowledgeMsg(token, p);
    return true;
}

void p3Git::setMessageReadStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool read)
{
    /* Always remove status unprocessed */
    uint32_t mask = GXS_SERV::GXS_MSG_STATUS_GUI_NEW | GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    uint32_t status = GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD;
    if (read) status = 0;

    setMsgStatusFlags(token, msgId, status, mask);

    if (rsEvents)
    {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);

        ev->mGitMsgId = msgId.second;
        ev->mGitGroupId = msgId.first;
        ev->mGitEventCode = RsGitEventCode::READ_STATUS_CHANGED;
        rsEvents->postEvent(ev);
    }
}

bool p3Git::getGitStatistics(GxsServiceStatistic &stats)
{
    return getServiceStatistics(stats);
}

bool p3Git::getUpdates(const RsGxsGroupId &groupId, std::vector<RsGitUpdate> &updates)
{
    uint32_t token;
    RsTokReqOptions opts;
    opts.mReqType = GXS_REQUEST_TYPE_MSG_DATA;
    std::list<RsGxsGroupId> groupIds = { groupId };
    if (!requestMsgInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE) {
        return false;
    }
    std::map<RsGxsGroupId, std::vector<RsGitMsgItem*> > msgItems;
    if (getMsgDataT<RsGitMsgItem>(token, msgItems)) {
        auto &vec = msgItems[groupId];
        for (auto *gitMsg : vec) {
            if (gitMsg && gitMsg->mGitMsgType == 1) { // UPDATE
                RsGitUpdate update;
                update.mMeta = gitMsg->meta;
                update.mPackfileData = gitMsg->mPackfileData;
                update.mRefUpdates = gitMsg->mRefUpdates;
                update.mFiles = gitMsg->mFiles;
                updates.push_back(update);
            }
            delete gitMsg;
        }
        return true;
    }
    return false;
}

bool p3Git::unpackUpdate(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, const RsFileHash &fileHash, const std::map<std::string, std::string> &refUpdates)
{
    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    FileInfo fi;
    if (rsFiles && rsFiles->FileDetails(fileHash, RS_FILE_HINTS_LOCAL, fi)) {
        std::cout << "p3Git::unpackUpdate: Unpacking manually downloaded packfile: " << fi.path << std::endl;
        if (GitManager::unpackPackfileFromFile(repoPath, fi.path, refUpdates)) {
            if (rsEvents) {
                RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
                auto ev = std::make_shared<RsGitEvent>(gitEventType);
                ev->mGitGroupId = groupId;
                ev->mGitMsgId = msgId;
                ev->mGitEventCode = RsGitEventCode::NEW_POST;
                rsEvents->postEvent(ev);
            }
            uint32_t pToken;
            setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(groupId, msgId), true);
            return true;
        }
    }
    return false;
}

bool p3Git::setMessageProcessedStatus(const RsGxsGrpMsgIdPair &msgId, bool processed)
{
    uint32_t token;
    setMessageProcessedStatus(token, msgId, processed);
    return waitToken(token) == RsTokenService::COMPLETE;
}

void p3Git::setMessageProcessedStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool processed)
{
    uint32_t mask = GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;
    uint32_t status = processed ? 0 : GXS_SERV::GXS_MSG_STATUS_UNPROCESSED;
    setMsgStatusFlags(token, msgId, status, mask);

    if (rsEvents) {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);
        ev->mGitGroupId = msgId.first;
        ev->mGitMsgId = msgId.second;
        ev->mGitEventCode = RsGitEventCode::POST_UPDATED;
        rsEvents->postEvent(ev);
    }
}

bool p3Git::requestOfflineClone(const RsGxsGroupId &groupId, const std::string &localPath)
{
    std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
    if (!GitManager::isValidRepository(barePath)) {
        if (!GitManager::initRepository(barePath, true)) {
            std::cerr << "p3Git: Failed to initialize bare repository for offline clone: " << barePath << std::endl;
            postCloneStatus(groupId, "Failed to initialize local bare repository.", false);
            return false;
        }
    }

    postCloneStatus(groupId, "Offline clone: checking GXS database...", false);

    std::vector<RsGitUpdate> updates;
    if (!getUpdates(groupId, updates)) {
        postCloneStatus(groupId, "Offline clone: failed to retrieve updates from GXS database.", false);
        return false;
    }

    std::sort(updates.begin(), updates.end(), [](const RsGitUpdate &a, const RsGitUpdate &b) {
        return a.mMeta.mPublishTs < b.mMeta.mPublishTs;
    });

    std::set<RsGxsMessageId> pendingMsgIds;
    bool hasUnprocessedFiles = false;

    for (auto &update : updates) {
        bool isUnprocessed = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);
        if (!isUnprocessed) continue;

        if (!update.mPackfileData.empty()) {
            if (GitManager::unpackPackfile(barePath, update.mPackfileData, update.mRefUpdates)) {
                uint32_t pToken;
                setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(groupId, update.mMeta.mMsgId), true);
            } else {
                std::cerr << "p3Git: Offline clone: failed to unpack inline packfile for msg " << update.mMeta.mMsgId.toStdString() << std::endl;
            }
        }
        else if (!update.mFiles.empty()) {
            const RsGxsFile &file = update.mFiles[0];
            FileInfo info;
            if (rsFiles && rsFiles->alreadyHaveFile(file.mHash, info)) {
                if (GitManager::unpackPackfileFromFile(barePath, info.path, update.mRefUpdates)) {
                    uint32_t pToken;
                    setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(groupId, update.mMeta.mMsgId), true);
                } else {
                    std::cerr << "p3Git: Offline clone: failed to unpack packfile from file " << info.path << " for msg " << update.mMeta.mMsgId.toStdString() << std::endl;
                }
            } else {
                hasUnprocessedFiles = true;
                pendingMsgIds.insert(update.mMeta.mMsgId);

                // Request the file from remote peers who have it
                if (rsFiles) {
                    FileInfo fileInfo;
                    rsFiles->FileDetails(file.mHash, RS_FILE_HINTS_REMOTE, fileInfo);
                    std::list<RsPeerId> sources;
                    for (const auto &peer : fileInfo.peers) {
                        sources.push_back(peer.peerId);
                    }
                    TransferRequestFlags flags = RS_FILE_REQ_ANONYMOUS_ROUTING;
                    rsFiles->FileRequest(file.mName, file.mHash, file.mSize, "", flags, sources);
                }

                // Add to mPendingPackfiles
                RS_STACK_MUTEX(mRetroGitMtx);
                bool alreadyPending = false;
                for (const auto &p : mPendingPackfiles) {
                    if (p.fileHash == file.mHash && p.groupId == groupId) {
                        alreadyPending = true;
                        break;
                    }
                }
                if (!alreadyPending) {
                    PendingPackfile pending;
                    pending.groupId = groupId;
                    pending.msgId = update.mMeta.mMsgId;
                    pending.fileHash = file.mHash;
                    pending.refUpdates = update.mRefUpdates;
                    mPendingPackfiles.push_back(pending);
                }
            }
        }
    }

    if (!hasUnprocessedFiles) {
        postCloneStatus(groupId, "Offline clone: writing working tree...", false);
        if (GitManager::cloneRepository(barePath, localPath)) {
            postCloneStatus(groupId, "Offline clone completed successfully!", true);
            return true;
        } else {
            postCloneStatus(groupId, "Offline clone failed to write working tree.", false);
            return false;
        }
    } else {
        RS_STACK_MUTEX(mRetroGitMtx);
        PendingOfflineClone poc;
        poc.groupId = groupId;
        poc.localPath = localPath;
        poc.pendingMsgIds = pendingMsgIds;

        bool exists = false;
        for (const auto &c : mPendingOfflineClones) {
            if (c.groupId == groupId) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            mPendingOfflineClones.push_back(poc);
        }
        postCloneStatus(groupId, "Offline clone: downloading required packfiles...", false);
        return true;
    }
}

bool p3Git::requestOfflinePull(const RsGxsGroupId &groupId, const std::string &localPath)
{
    std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
    if (!GitManager::isValidRepository(barePath)) {
        std::cerr << "p3Git: Offline pull failed: local repository not initialized." << std::endl;
        postCloneStatus(groupId, "Offline pull failed: local repository not initialized.", false);
        return false;
    }

    postCloneStatus(groupId, "Offline sync: checking GXS database...", false);

    std::vector<RsGitUpdate> updates;
    if (!getUpdates(groupId, updates)) {
        postCloneStatus(groupId, "Offline sync: failed to retrieve updates from GXS database.", false);
        return false;
    }

    std::sort(updates.begin(), updates.end(), [](const RsGitUpdate &a, const RsGitUpdate &b) {
        return a.mMeta.mPublishTs < b.mMeta.mPublishTs;
    });

    std::set<RsGxsMessageId> pendingMsgIds;
    bool hasUnprocessedFiles = false;

    for (auto &update : updates) {
        bool isUnprocessed = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);
        if (!isUnprocessed) continue;

        if (!update.mPackfileData.empty()) {
            if (GitManager::unpackPackfile(barePath, update.mPackfileData, update.mRefUpdates)) {
                uint32_t pToken;
                setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(groupId, update.mMeta.mMsgId), true);
            } else {
                std::cerr << "p3Git: Offline sync: failed to unpack inline packfile for msg " << update.mMeta.mMsgId.toStdString() << std::endl;
            }
        }
        else if (!update.mFiles.empty()) {
            const RsGxsFile &file = update.mFiles[0];
            FileInfo info;
            if (rsFiles && rsFiles->alreadyHaveFile(file.mHash, info)) {
                if (GitManager::unpackPackfileFromFile(barePath, info.path, update.mRefUpdates)) {
                    uint32_t pToken;
                    setMessageProcessedStatus(pToken, RsGxsGrpMsgIdPair(groupId, update.mMeta.mMsgId), true);
                } else {
                    std::cerr << "p3Git: Offline sync: failed to unpack packfile from file " << info.path << " for msg " << update.mMeta.mMsgId.toStdString() << std::endl;
                }
            } else {
                hasUnprocessedFiles = true;
                pendingMsgIds.insert(update.mMeta.mMsgId);

                // Request the file from remote peers who have it
                if (rsFiles) {
                    FileInfo fileInfo;
                    rsFiles->FileDetails(file.mHash, RS_FILE_HINTS_REMOTE, fileInfo);
                    std::list<RsPeerId> sources;
                    for (const auto &peer : fileInfo.peers) {
                        sources.push_back(peer.peerId);
                    }
                    TransferRequestFlags flags = RS_FILE_REQ_ANONYMOUS_ROUTING;
                    rsFiles->FileRequest(file.mName, file.mHash, file.mSize, "", flags, sources);
                }

                // Add to mPendingPackfiles
                RS_STACK_MUTEX(mRetroGitMtx);
                bool alreadyPending = false;
                for (const auto &p : mPendingPackfiles) {
                    if (p.fileHash == file.mHash && p.groupId == groupId) {
                        alreadyPending = true;
                        break;
                    }
                }
                if (!alreadyPending) {
                    PendingPackfile pending;
                    pending.groupId = groupId;
                    pending.msgId = update.mMeta.mMsgId;
                    pending.fileHash = file.mHash;
                    pending.refUpdates = update.mRefUpdates;
                    mPendingPackfiles.push_back(pending);
                }
            }
        }
    }

    if (!hasUnprocessedFiles) {
        bool hasWorkingDir = !localPath.empty() && GitManager::isValidRepository(localPath);
        if (hasWorkingDir) {
            postCloneStatus(groupId, "Offline sync: updating working tree...", false);
            if (GitManager::pullRepository(localPath)) {
                postCloneStatus(groupId, "Offline sync completed successfully!", true);
                return true;
            } else {
                postCloneStatus(groupId, "Offline sync failed to update working tree.", false);
                return false;
            }
        } else {
            postCloneStatus(groupId, "Offline sync completed successfully!", true);
            return true;
        }
    } else {
        RS_STACK_MUTEX(mRetroGitMtx);
        PendingOfflinePull pop;
        pop.groupId = groupId;
        pop.localPath = localPath;
        pop.pendingMsgIds = pendingMsgIds;

        bool exists = false;
        for (const auto &p : mPendingOfflinePulls) {
            if (p.groupId == groupId) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            mPendingOfflinePulls.push_back(pop);
        }
        postCloneStatus(groupId, "Offline sync: downloading required packfiles...", false);
        return true;
    }
}

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
        PendingClone pc;
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
        PendingPull pp;
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

void p3Git::postCloneStatus(const RsGxsGroupId &groupId, const std::string &status, bool success)
{
    if (rsEvents) {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        auto ev = std::make_shared<RsGitEvent>(gitEventType);
        ev->mGitGroupId = groupId;
        ev->mGitEventCode = RsGitEventCode::CLONE_STATUS_CHANGED;
        ev->mCloneStatus = status;
        ev->mCloneSuccess = success;
        rsEvents->postEvent(ev);
    }
}

void p3Git::notifyTunnelStatus(const RsGxsTunnelId& tunnel_id, uint32_t tunnel_status)
{
    RsGxsGroupId groupId;
    std::string localPath;
    bool isPull = false;
    bool found = false;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        auto it = mPendingClones.find(tunnel_id);
        if (it != mPendingClones.end()) {
            groupId = it->second.groupId;
            localPath = it->second.localPath;
            found = true;
        } else {
            auto it2 = mPendingPulls.find(tunnel_id);
            if (it2 != mPendingPulls.end()) {
                groupId = it2->second.groupId;
                localPath = it2->second.localPath;
                isPull = true;
                found = true;
            }
        }
    }

    if (groupId.isNull()) return;

    if (tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_CAN_TALK) {
        std::cout << "p3Git: Tunnel secured (tunnel_id=" << tunnel_id << "). Sending " << (isPull ? "pull_req" : "clone_req") << "..." << std::endl;
        postCloneStatus(groupId, isPull ? "Tunnel secured. Requesting sync data..." : "Tunnel secured. Requesting repository data...", false);

        // Send JSON message
        QVariantMap req;
        req["type"] = isPull ? "pull_req" : "clone_req";
        req["group_id"] = QString::fromStdString(groupId.toStdString());

        QJsonDocument doc = QJsonDocument::fromVariant(req);
        std::string msg = doc.toJson().toStdString();

        mGxsTunnels->sendData(tunnel_id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID, (const uint8_t*)msg.c_str(), msg.size());
    }
    else if (tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_REMOTELY_CLOSED ||
             tunnel_status == RsGxsTunnelService::RS_GXS_TUNNEL_STATUS_TUNNEL_DN)
    {
        std::cout << "p3Git: Tunnel went down/closed (tunnel_id=" << tunnel_id << ")" << std::endl;
        if (found) {
            std::cout << "p3Git: Tunnel went down. Falling back to offline method for group: " << groupId.toStdString() << std::endl;
            postCloneStatus(groupId, "Tunnel disconnected. Falling back to offline sync...", false);
            {
                RS_STACK_MUTEX(mRetroGitMtx);
                mPendingClones.erase(tunnel_id);
                mPendingPulls.erase(tunnel_id);
                mTunnelToGxsIdMap.erase(tunnel_id);
            }
            if (isPull) {
                requestOfflinePull(groupId, localPath);
            } else {
                requestOfflineClone(groupId, localPath);
            }
        } else {
            postCloneStatus(groupId, "Secure tunnel went down.", false);
            {
                RS_STACK_MUTEX(mRetroGitMtx);
                mPendingClones.erase(tunnel_id);
                mPendingPulls.erase(tunnel_id);
                mTunnelToGxsIdMap.erase(tunnel_id);
            }
        }
    }
}

void p3Git::receiveData(const RsGxsTunnelId& id, unsigned char *data, uint32_t data_size)
{
    std::string msg((const char*)data, data_size);
    free(data); // Free the memory as per ownership rules

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(msg));
    if (doc.isNull()) {
        std::cerr << "p3Git: Failed to parse received JSON data from tunnel." << std::endl;
        return;
    }

    QVariantMap map = doc.toVariant().toMap();
    QString type = map["type"].toString();

    if (type == "clone_req" || type == "pull_req") {
        QString groupIdStr = map["group_id"].toString();
        std::cout << "p3Git: Received request " << type.toStdString() << " for group " << groupIdStr.toStdString() << std::endl;

        std::string barePath = GitManager::getBareRepoPath(groupIdStr.toStdString());
        if (!GitManager::isValidRepository(barePath)) {
            std::cerr << "p3Git: Requested repository is not valid locally: " << barePath << std::endl;
            return;
        }

        std::string packfileData;
        std::map<std::string, std::string> refUpdates;
        if (GitManager::createPackfile(barePath, packfileData, refUpdates)) {
            std::cout << "p3Git: Generated packfile of size " << packfileData.size() << " bytes. Sending..." << std::endl;
            QByteArray base64Pack = QByteArray(packfileData.data(), packfileData.size()).toBase64();

            QVariantMap resp;
            resp["type"] = (type == "pull_req") ? "pull_res" : "clone_res";
            resp["group_id"] = groupIdStr;
            resp["packfile"] = QString::fromLatin1(base64Pack);

            QVariantMap refs;
            for (auto const& pair : refUpdates) {
                refs[QString::fromStdString(pair.first)] = QString::fromStdString(pair.second);
            }
            resp["ref_updates"] = refs;

            QJsonDocument respDoc = QJsonDocument::fromVariant(resp);
            std::string respStr = respDoc.toJson().toStdString();

            mGxsTunnels->sendData(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID, (const uint8_t*)respStr.c_str(), respStr.size());
        } else {
            std::cerr << "p3Git: Failed to create packfile for group " << groupIdStr.toStdString() << std::endl;
        }
    }
    else if (type == "clone_res") {
        QString groupIdStr = map["group_id"].toString();
        RsGxsGroupId groupId(groupIdStr.toStdString());
        std::cout << "p3Git: Received clone response for group " << groupIdStr.toStdString() << std::endl;

        PendingClone pc;
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            auto it = mPendingClones.find(id);
            if (it != mPendingClones.end()) {
                pc = it->second;
                mPendingClones.erase(it);
                mTunnelToGxsIdMap.erase(id);
            }
        }

        if (pc.groupId.isNull()) {
            std::cerr << "p3Git: Received clone response but no pending clone found for tunnel " << id << std::endl;
            return;
        }

        postCloneStatus(groupId, "Received repository data. Unpacking...", false);

        QByteArray base64Pack = map["packfile"].toString().toLatin1();
        QByteArray packfileData = QByteArray::fromBase64(base64Pack);

        QVariantMap refs = map["ref_updates"].toMap();
        std::map<std::string, std::string> refUpdates;
        for (auto it = refs.begin(); it != refs.end(); ++it) {
            refUpdates[it.key().toStdString()] = it.value().toString().toStdString();
        }

        std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
        if (GitManager::unpackPackfile(barePath, std::string(packfileData.data(), packfileData.size()), refUpdates)) {
            postCloneStatus(groupId, "Repository unpacked. Writing working tree...", false);

            if (GitManager::cloneRepository(barePath, pc.localPath)) {
                std::cout << "p3Git: Successfully cloned/checked out bare repo to working path: " << pc.localPath << std::endl;
                postCloneStatus(groupId, "Clone completed successfully!", true);
            } else {
                std::cerr << "p3Git: Failed to clone bare repo to working path: " << pc.localPath << std::endl;
                postCloneStatus(groupId, "Failed to write working tree to selected path.", false);
            }
        } else {
            std::cerr << "p3Git: Failed to unpack packfile to bare repository: " << barePath << std::endl;
            postCloneStatus(groupId, "Failed to unpack repository packfile.", false);
        }

        // Close the tunnel now that we've finished transfer
        mGxsTunnels->closeExistingTunnel(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID);
    }
    else if (type == "pull_res") {
        QString groupIdStr = map["group_id"].toString();
        RsGxsGroupId groupId(groupIdStr.toStdString());
        std::cout << "p3Git: Received pull response for group " << groupIdStr.toStdString() << std::endl;

        PendingPull pp;
        {
            RS_STACK_MUTEX(mRetroGitMtx);
            auto it = mPendingPulls.find(id);
            if (it != mPendingPulls.end()) {
                pp = it->second;
                mPendingPulls.erase(it);
                mTunnelToGxsIdMap.erase(id);
            }
        }

        if (pp.groupId.isNull()) {
            std::cerr << "p3Git: Received pull response but no pending pull found for tunnel " << id << std::endl;
            return;
        }

        postCloneStatus(groupId, "Received repository changes. Unpacking...", false);

        QByteArray base64Pack = map["packfile"].toString().toLatin1();
        QByteArray packfileData = QByteArray::fromBase64(base64Pack);

        QVariantMap refs = map["ref_updates"].toMap();
        std::map<std::string, std::string> refUpdates;
        for (auto it = refs.begin(); it != refs.end(); ++it) {
            refUpdates[it.key().toStdString()] = it.value().toString().toStdString();
        }

        std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
        if (GitManager::unpackPackfile(barePath, std::string(packfileData.data(), packfileData.size()), refUpdates)) {
            bool hasWorkingDir = !pp.localPath.empty() && GitManager::isValidRepository(pp.localPath);
            if (hasWorkingDir) {
                postCloneStatus(groupId, "Syncing working tree...", false);
                if (GitManager::pullRepository(pp.localPath)) {
                    postCloneStatus(groupId, "Sync completed successfully!", true);
                } else {
                    postCloneStatus(groupId, "Failed to update working tree with new changes.", false);
                }
            } else {
                postCloneStatus(groupId, "Bare repository synced successfully!", true);
            }
        } else {
            postCloneStatus(groupId, "Failed to unpack sync packfile.", false);
        }

        // Close the tunnel now that we've finished transfer
        mGxsTunnels->closeExistingTunnel(id, RETRO_GIT_GXS_TUNNEL_SERVICE_ID);
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
    std::cout << "p3Git: acceptDataFromPeer: gxs=" << gxs_id
              << " tunnel=" << tunnel_id
              << " side=" << (am_I_client_side ? "client" : "server") << std::endl;
    {
        RS_STACK_MUTEX(mRetroGitMtx);
        mTunnelToGxsIdMap[tunnel_id] = gxs_id;
    }
    return true;
}
