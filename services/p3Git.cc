/*******************************************************************************
 * services/p3Git.cc                                                           *
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

#include <iostream>
#include <string>

#include "p3Git.h"
#include "rsGitItems.h"
#include "gui/RetroGitNotify.h"

#include "gxs/rsgenexchange.h"

RsGit *rsRetroGit = NULL;

static uint32_t retroGitAuthenPolicy()
{
	uint32_t policy = 0;
	uint8_t flag = GXS_SERV::MSG_AUTHEN_ROOT_PUBLISH_SIGN | GXS_SERV::MSG_AUTHEN_CHILD_AUTHOR_SIGN;
	flag |= GXS_SERV::MSG_AUTHEN_ROOT_AUTHOR_SIGN;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PUBLIC_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::RESTRICTED_GRP_BITS);
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::PRIVATE_GRP_BITS);

	flag = 0;
	RsGenExchange::setAuthenPolicyFlag(flag, policy, RsGenExchange::GRP_OPTION_BITS);

	return policy;
}

p3Git::p3Git(RsGeneralDataService* gds, RsNetworkExchangeService* nes, RsGixs *gixs, RetroGitNotify *notifier)
	: RsGenExchange(gds, nes, new RsGxsRetroGitSerialiser(), RS_SERVICE_TYPE_RetroGit_PLUGIN, gixs, retroGitAuthenPolicy()),
      mRetroGitMtx("p3Git"), mNotify(notifier)
{
	rsRetroGit = this;
}

p3Git::~p3Git()
{
	rsRetroGit = NULL;
}

RsServiceInfo p3Git::getServiceInfo()
{
    const std::string RETRO_GIT_APP_NAME = "RetroGit";
    const uint16_t RETRO_GIT_APP_MAJOR_VERSION  =       1;
    const uint16_t RETRO_GIT_APP_MINOR_VERSION  =       0;
    const uint16_t RETRO_GIT_MIN_MAJOR_VERSION  =       1;
    const uint16_t RETRO_GIT_MIN_MINOR_VERSION  =       0;

	return RsServiceInfo(RS_SERVICE_TYPE_RetroGit_PLUGIN,
		RETRO_GIT_APP_NAME,
		RETRO_GIT_APP_MAJOR_VERSION,
		RETRO_GIT_APP_MINOR_VERSION,
		RETRO_GIT_MIN_MAJOR_VERSION,
		RETRO_GIT_MIN_MINOR_VERSION);
}

RsSerialiser* p3Git::setupSerialiser()
{
    RsSerialiser* rss = new RsSerialiser;
    rss->addSerialType(new RsGxsRetroGitSerialiser());
    return rss;
}

bool p3Git::saveList(bool& cleanup, std::list<RsItem*>& saveList)
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

bool p3Git::loadList(std::list<RsItem*>& loadList)
{
    while(!loadList.empty())
    {
        RsItem *item = loadList.front();
        loadList.pop_front();

        RsGitConfigItem *configItem = dynamic_cast<RsGitConfigItem*>(item);

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
    return;
}

RsTokenService* p3Git::getTokenService() {

	return RsGenExchange::getTokenService();
}

void p3Git::notifyChanges(std::vector<RsGxsNotify *> &)
{
}

// Blocking Interfaces.
bool p3Git::createGroup(RsGitGroup &group)
{
	uint32_t token;
	//return createGroup(token, group) && waitToken(token) == RsTokenService::COMPLETE;
}

// Function which will update the edited information in the git group
bool p3Git::createGroup(uint32_t &token, RsGitGroup &group)
{
    // Create a GXS Group Item
    RsGitGroupItem* grpItem = new RsGitGroupItem();
    
    // Populate the item with the provided group data
    grpItem->mGroup = group;
    grpItem->meta = group.mMeta; // Ensure metadata (like creator GxsId) is preserved

    // Publish to GXS (Generic Exchange Service)
    this->publishGroup(token, grpItem);

    return true;
}

RsItem *RsGxsRetroGitSerialiser::create_item(uint16_t service, uint8_t item_subtype) const
{
    if (service != RS_SERVICE_TYPE_RetroGit_PLUGIN)
        return NULL;

    if (item_subtype == RS_PKT_SUBTYPE_GXS_GROUP_DATA_ITEM)
        return new RsGitGroupItem();
    else if (item_subtype == RS_PKT_SUBTYPE_RetroGit_CONFIG)
        return new RsGitConfigItem();

    return NULL;
}
