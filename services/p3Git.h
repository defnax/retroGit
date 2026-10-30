/*******************************************************************************
 * services/p3Git.h                                                            *
 *                                                                             *
 * Copyright (C) 2020 RetroShare Team <retroshare.project@gmail.com>           *
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

#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include "TunnelTransfer.h"
#include "gxs/rsgenexchange.h"
#include "plugins/rspqiservice.h"
#include "retroshare/rsidentity.h"
#include "rsitems/rsconfigitems.h"
#include "serialiser/rstlvbase.h"
#include "services/p3service.h"
#include "services/rsGitItems.h"
#include <retroshare/rsgxsifacehelper.h>
#include <retroshare/rsgxstunnel.h>
#include <interface/rsGit.h>

bool retroGitDebugEnabled();
void setRetroGitDebugEnabled(bool enabled);

class RetroGitNotify;

class p3Git: public RsGenExchange, public RsGit, public p3Config, public RsGxsIfaceHelper, public RsGxsTunnelService::RsGxsTunnelClientService
{
public:
    p3Git(RsGeneralDataService *gds, RsNetworkExchangeService *nes, RsGixs *gixs,RetroGitNotify *notifier);
    virtual ~p3Git() override;

    virtual RsServiceInfo getServiceInfo() override;
    virtual RsTokenService *getTokenService() override;

    /** Stop the background git worker. Call before stopping the service threads. */
    void stopWorker();

    // GXS methods
    virtual RsSerialiser *setupSerialiser() override;
    virtual bool saveList(bool &cleanup, std::list<RsItem *> &saveList) override;
    virtual bool loadList(std::list<RsItem *> &loadList) override;
    virtual void service_tick() override;
    virtual void notifyChanges(std::vector<RsGxsNotify *> &changes) override;

    // RsGit interface
    virtual bool createGroup(uint32_t &token, RsGitGroup &group) override;
    virtual bool updateGroup(uint32_t &token, RsGitGroup &group) override;
    // Blocking Interfaces.
    virtual bool createGroup(RsGitGroup &group) override;
    virtual bool updateGroup(RsGitGroup &group) override;

    virtual bool publishGitUpdate(uint32_t &token, RsGitUpdate &update) override;
    bool preparePullRequest(const RsGitPullRequest &pr) override;
    virtual bool publishPullRequest(uint32_t &token, RsGitPullRequest &pr) override;
    virtual bool closePullRequest(uint32_t &token, const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, uint32_t status) override;
    virtual bool getGroups(const std::list<RsGxsGroupId> &groupIds, std::vector<RsGitGroup> &groups) override;
    virtual bool getUpdates(const RsGxsGroupId &groupId, std::vector<RsGitUpdate> &updates) override;
    virtual bool getPullRequests(const RsGxsGroupId &groupId, std::vector<RsGitPullRequest> &pullRequests) override;
    virtual bool unpackUpdate(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, const RsFileHash &fileHash, const std::map<std::string, std::string> &refUpdates) override;
    virtual bool setMessageProcessedStatus(const RsGxsGrpMsgIdPair &msgId, bool processed) override;
    virtual void setMessageProcessedStatus(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool processed) override;
    virtual bool subscribeToGroup(uint32_t &token, const RsGxsGroupId &groupId, bool subscribe) override;
    virtual bool subscribe(const RsGxsGroupId &groupId, bool subscribe) override;
    virtual bool setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId,bool read) override;
    virtual void setMessageReadStatus(uint32_t &token,const RsGxsGrpMsgIdPair &msgId,bool read) override;
    virtual bool requestCloneOverTunnel(const RsGxsGroupId &groupId, const RsGxsId &toId, const RsGxsId &ownId, const std::string &localPath) override;
    virtual bool requestPullOverTunnel(const RsGxsGroupId &groupId, const RsGxsId &toId, const RsGxsId &ownId, const std::string &localPath) override;
    virtual bool requestOfflineClone(const RsGxsGroupId &groupId, const std::string &localPath) override;
    virtual bool requestOfflinePull(const RsGxsGroupId &groupId, const std::string &localPath) override;
    virtual bool isCloneInProgress(const RsGxsGroupId &groupId) override;
    virtual bool getGitStatistics(GxsServiceStatistic &stats) override;

    // RsGxsTunnelClientService methods
    virtual void notifyTunnelStatus(const RsGxsTunnelId& tunnel_id, uint32_t tunnel_status) override;
    virtual void receiveData(const RsGxsTunnelId& id, unsigned char *data, uint32_t data_size) override;
    virtual void connectToGxsTunnelService(RsGxsTunnelService *tunnel_service) override;
    virtual bool acceptDataFromPeer(const RsGxsId& gxs_id, const RsGxsTunnelId& tunnel_id, bool am_I_client_side) override;

private:
    struct PendingPackfile
    {
        RsGxsGroupId groupId;
        RsGxsMessageId msgId;
        RsFileHash fileHash;
        std::string fileName;
        uint64_t fileSize = 0;
        std::map<std::string, std::string> refUpdates;
        rstime_t publishTs = 0;
    };

    struct IncompleteUpdate {
        std::map<std::string, std::string> refUpdates;
        rstime_t publishTs = 0;
    };

    struct PendingClone {
        RsGxsGroupId groupId;
        std::string localPath;
    };

    struct PendingPull {
        RsGxsGroupId groupId;
        std::string localPath;
    };

    /** Offline clone or pull waiting for published packs to download. */
    struct PendingOffline {
        RsGxsGroupId groupId;
        std::string localPath;
        bool isPull = false;
        bool preparing = true;     // the database scan has not finished yet
        bool finalizing = false;   // the working tree is being written
        std::set<RsGxsMessageId> pendingMsgIds;
        // Every unprocessed update in publish order, re-applied at the end so
        // packs that completed out of order still update the branches.
        std::vector<std::pair<RsGxsMessageId, IncompleteUpdate> > updatesInOrder;
        std::chrono::steady_clock::time_point started;
    };

    struct OutgoingTransfer {
        QByteArray response;
        int offset = 0;
        QString groupId;
    };

    struct MsgCache {
        std::map<RsGxsMessageId, std::vector<RsGxsMessageId>> unreadPRStatuses;
        std::vector<RsGitUpdate> updates;
        std::vector<RsGitPullRequest> pullRequests;
    };

    // --- helpers ---
    void postGitEvent(RsGitEventCode code, const RsGxsGroupId &groupId, const RsGxsMessageId &msgId = RsGxsMessageId());
    void postCloneStatus(const RsGxsGroupId &groupId, const std::string &status, bool success,
                         uint64_t received = 0, uint64_t total = 0);
    void failTunnelTransfer(const RsGxsTunnelId& id, const std::string& reason);
    void fallBackToOffline(const RsGxsTunnelId& id, const std::string& reason);
    /** Point branches/tags at the positions in the owner's signed updates. */
    bool applySignedRefs(const RsGxsGroupId &groupId, const std::string &barePath,
                         const std::map<std::string, std::string> &tunnelRefs, std::string &error);
    void forgetTunnel(const RsGxsTunnelId& id);
    void sendTransferError(const RsGxsTunnelId& id, const QString& groupId, const QString& error);

    // Background worker: all git and database work that can take long runs
    // here, never on the GXS service thread, the tunnel thread or the GUI.
    void runJob(std::function<void()> job);
    void workerLoop();

    // Cached database reads (pack data stripped), invalidated by notifyChanges().
    bool cachedGroups(std::vector<RsGitGroup> &groups);
    bool cachedMessages(const RsGxsGroupId &groupId, MsgCache &out);
    bool loadGroups(std::vector<RsGitGroup> &groups);
    bool loadMessages(const RsGxsGroupId &groupId, MsgCache &out, bool withPackData);
    void invalidateGroups();
    void invalidateMessages(const RsGxsGroupId &groupId);

    // Tokens of fire-and-forget requests are acknowledged later so that
    // libretroshare can release them.
    void trackToken(uint32_t token);
    void acknowledgeTrackedTokens();

    // Non-tracked implementations used by the blocking variants.
    bool createGroupImpl(uint32_t &token, RsGitGroup &group);
    bool updateGroupImpl(uint32_t &token, RsGitGroup &group);
    void setMsgReadImpl(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool read);
    void setMsgProcessedImpl(uint32_t &token, const RsGxsGrpMsgIdPair &msgId, bool processed);

    // Jobs (worker thread)
    void unpackRootUpdateJob(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId, const std::string &packData,
                             const RsGxsFile &file, const std::map<std::string, std::string> &refUpdates,
                             rstime_t publishTs);
    // Updates can be processed out of publish order (downloads finish in any
    // order): an older update must never move a branch set by a newer one.
    std::map<std::string, std::string> refsNotOlderThan(const RsGxsGroupId &groupId,
                             const std::map<std::string, std::string> &refUpdates, rstime_t publishTs);
    void noteRefsApplied(const RsGxsGroupId &groupId, const std::map<std::string, std::string> &refUpdates, rstime_t publishTs);
    void unpackDownloadedJob(const PendingPackfile &pending);
    void recordUnpackResult(const RsGxsGroupId &groupId, const RsGxsMessageId &msgId,
                            const std::map<std::string, std::string> &refUpdates, rstime_t publishTs, bool unpacked);
    void startOfflineJob(const RsGxsGroupId &groupId);
    void finalizeOfflineJob(const RsGxsGroupId &groupId);
    void preparePullRequestJob(RsGitPullRequest pr);
    void serveRequestJob(const RsGxsTunnelId &id, const RsGxsId &requester, QString type, QString groupIdStr,
                         std::set<std::string> known, bool chunked);
    void handleResponseJob(const RsGxsTunnelId &id, QByteArray json);
    bool requestOffline(const RsGxsGroupId &groupId, const std::string &localPath, bool isPull);
    void sendOutgoingChunks();

    // --- state ---
    std::map<RsGxsTunnelId, std::chrono::steady_clock::time_point> mTransferActivity;
    std::map<RsGxsTunnelId, GitTunnelTransfer::Assembly> mIncomingTransfers;
    std::set<RsGxsTunnelId> mSentRequests;
    RsMutex mRetroGitMtx;
    RetroGitNotify *mNotify;
    std::map<uint32_t, uint32_t> mKnownGit;
    std::list<PendingPackfile> mPendingPackfiles;
    std::set<RsGxsGrpMsgIdPair> mUnpacking;            // downloaded packs queued on the worker
    std::map<RsGxsGrpMsgIdPair, IncompleteUpdate> mIncompleteUpdates; // waiting for earlier packs
    std::map<RsGxsGroupId, std::map<std::string, rstime_t> > mRefTimes; // publish time of the update that set each ref
    RsGxsTunnelService *mGxsTunnels;
    std::map<RsGxsTunnelId, PendingClone> mPendingClones;
    std::map<RsGxsTunnelId, PendingPull> mPendingPulls;
    std::map<RsGxsTunnelId, RsGxsId> mTunnelToGxsIdMap;
    std::map<RsGxsGroupId, PendingOffline> mPendingOffline;
    std::set<RsGxsTunnelId> mServingTunnels;           // requests being answered (rate limit)
    std::map<RsGxsTunnelId, OutgoingTransfer> mOutgoing;
    std::map<RsGxsTunnelId, std::chrono::steady_clock::time_point> mLastProgressPost;
    std::set<RsGxsMessageId> mPreparingPullRequests;
    std::set<RsFileHash> mRequestedFiles;
    std::list<std::pair<uint32_t, std::chrono::steady_clock::time_point> > mTrackedTokens;
    std::chrono::steady_clock::time_point mLastSlowTick;

    RsMutex mCacheMtx;
    bool mGroupsValid;
    uint64_t mGroupsGeneration;
    std::vector<RsGitGroup> mGroupsCache;
    std::map<RsGxsGroupId, MsgCache> mMsgCache;
    std::map<RsGxsGroupId, uint64_t> mMsgGeneration;

    std::mutex mJobMutex;
    std::condition_variable mJobCv;
    std::deque<std::function<void()> > mJobs;
    bool mStopJobs;
    std::atomic<bool> mStopping;
    std::thread mWorker;
};
