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

class p3Git: public RsGenExchange, public RsGit, public p3Config, public RsGxsIfaceHelper
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

    virtual bool subscribeToGroup(uint32_t &token, const RsGxsGroupId &groupId, bool subscribe) override;
    virtual bool subscribe(const RsGxsGroupId &groupId, bool subscribe) override;

    virtual bool setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId,bool read) override;
    virtual void setMessageReadStatus(uint32_t &token,const RsGxsGrpMsgIdPair &msgId,bool read) override;

private:
    struct PendingPackfile
    {
        RsGxsGroupId groupId;
        RsFileHash fileHash;
        std::map<std::string, std::string> refUpdates;
    };

    RsMutex mRetroGitMtx;
    RetroGitNotify *mNotify;
    std::map<uint32_t, uint32_t> mKnownGit;
    std::list<PendingPackfile> mPendingPackfiles;
};
