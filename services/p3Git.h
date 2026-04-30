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

#include <list>
#include <string>
#include <QVariantMap>

#include "services/rsGitItems.h"
#include "services/p3service.h"
#include "serialiser/rstlvbase.h"
#include "rsitems/rsconfigitems.h"
#include "plugins/rspqiservice.h"
#include "retroshare/rsidentity.h"
#include "gxs/rsgenexchange.h"

#include <retroshare/rsgxstunnel.h>

#include <interface/rsRetroGit.h>

class RetroGitNotify ;

class p3Git: public RsGenExchange, public RsGit, public p3Config
{
public:
	p3Git(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs, RetroGitNotify *notifier);
	virtual ~p3Git() override;

	virtual RsServiceInfo getServiceInfo() override;

    virtual RsTokenService* getTokenService() override;

    // GXS methods
    virtual RsSerialiser* setupSerialiser() override;
    virtual bool saveList(bool &cleanup, std::list<RsItem *>&saveList) override;
    virtual bool loadList(std::list<RsItem *>& loadList) override;

    virtual void service_tick() override;
    virtual void notifyChanges(std::vector<RsGxsNotify *> &changes) override;

    // RsGit interface
    virtual bool createGroup(uint32_t &token, RsGitGroup &group) override;
    
    // Blocking Interfaces.
    virtual bool createGroup(RsGitGroup &group) override;

private:
	RsMutex mRetroGitMtx;
	RetroGitNotify *mNotify ;
    std::map<uint32_t, uint32_t> mKnownGit;
};
