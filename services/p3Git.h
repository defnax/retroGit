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

#include <QVariantMap>
#include <list>
#include <set>
#include <string>

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

class RetroGitNotify;

class p3Git: public RsGenExchange, public RsGit, public p3Config, public RsGxsIfaceHelper, public RsGxsTunnelService::RsGxsTunnelClientService
{
public:
    p3Git(RsGeneralDataService *gds, RsNetworkExchangeService *nes, RsGixs *gixs,RetroGitNotify *notifier);
    virtual ~p3Git() override;

    virtual RsServiceInfo getServiceInfo() override;

    virtual RsTokenService *getTokenService() override;

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
    virtual bool publishPullRequest(uint32_t &token, RsGitPullRequest &pr) override;

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
        std::map<std::string, std::string> refUpdates;
    };

    struct PendingClone {
        RsGxsGroupId groupId;
        std::string localPath;
    };

    struct PendingPull {
        RsGxsGroupId groupId;
        std::string localPath;
    };

    struct PendingOfflineClone {
        RsGxsGroupId groupId;
        std::string localPath;
        std::set<RsGxsMessageId> pendingMsgIds;
    };

    struct PendingOfflinePull {
        RsGxsGroupId groupId;
        std::string localPath;
        std::set<RsGxsMessageId> pendingMsgIds;
    };

    void postCloneStatus(const RsGxsGroupId &groupId, const std::string &status, bool success);

    RsMutex mRetroGitMtx;
    RetroGitNotify *mNotify;
    std::map<uint32_t, uint32_t> mKnownGit;
    std::list<PendingPackfile> mPendingPackfiles;

    RsGxsTunnelService *mGxsTunnels;
    std::map<RsGxsTunnelId, PendingClone> mPendingClones;
    std::map<RsGxsTunnelId, PendingPull> mPendingPulls;
    std::map<RsGxsTunnelId, RsGxsId> mTunnelToGxsIdMap;

    std::vector<PendingOfflineClone> mPendingOfflineClones;
    std::vector<PendingOfflinePull> mPendingOfflinePulls;
};
