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

#include <iostream>
#include <string>
#include <chrono>

#include "p3Git.h"
#include "rsGitItems.h"
#include "gui/RetroGitNotify.h"

#include "gxs/rsgenexchange.h"

RsGit *rsGit = NULL;

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
      mRetroGitMtx("p3Git"), mNotify(notifier), RsGxsIfaceHelper(static_cast<RsGxsIface&>(*this))
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
    rss->addSerialType(new RsGitConfigSerializer());
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

void p3Git::notifyChanges(std::vector<RsGxsNotify *> &changes)
{
    std::cerr << "p3Git::notifyChanges() " << changes.size() << " changes" << std::endl;

    for (auto it = changes.begin(); it != changes.end(); ++it)
    {
        /* ---- Message changes (new commit / post) ---- */
        RsGxsMsgChange *msgChange = dynamic_cast<RsGxsMsgChange*>(*it);
        if (msgChange)
        {
            if (msgChange->getType() == RsGxsNotify::TYPE_RECEIVED_NEW ||
                msgChange->getType() == RsGxsNotify::TYPE_PUBLISHED)
            {
                if (rsEvents)
                {
                    auto ev = std::make_shared<RsGitEvent>();
                    ev->mGitGroupId = msgChange->mGroupId;
                    ev->mGitMsgId   = msgChange->mMsgId;
                    ev->mGitEventCode = RsGitEventCode::NEW_POST;
                    rsEvents->postEvent(ev);
                }
            }
            continue;
        }

        /* ---- Group changes (new repo / subscription / update) ---- */
        RsGxsGroupChange *grpChange = dynamic_cast<RsGxsGroupChange*>(*it);
        if (grpChange && rsEvents)
        {
            auto ev = std::make_shared<RsGitEvent>();
            ev->mGitGroupId = grpChange->mGroupId;

            switch (grpChange->getType())
            {
            case RsGxsNotify::TYPE_PUBLISHED:
                // Fired when a group is subscribed/unsubscribed
                ev->mGitEventCode = RsGitEventCode::SUBSCRIBE_STATUS_CHANGED;
                rsEvents->postEvent(ev);
                break;

            case RsGxsNotify::TYPE_RECEIVED_NEW:
                // A new repository arrived from the network
                ev->mGitEventCode = RsGitEventCode::NEW_GIT;
                rsEvents->postEvent(ev);
                break;

            case RsGxsNotify::TYPE_PROCESSED:
                // An existing repository was updated
                ev->mGitEventCode = RsGitEventCode::GIT_UPDATED;
                rsEvents->postEvent(ev);
                break;

            default:
                break;
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

bool p3Git::getGroups(const std::list<RsGxsGroupId>& groupIds, std::vector<RsGitGroup>& groups)
{
	uint32_t token;
	RsTokReqOptions opts;
	opts.mReqType = GXS_REQUEST_TYPE_GROUP_DATA;

	if (groupIds.empty())
	{
        if (!requestGroupInfo(token, opts) || waitToken(token,std::chrono::milliseconds(5000)) != RsTokenService::COMPLETE )
		{
			return false;
		}
	}
	else
	{
		if (!requestGroupInfo(token, opts, groupIds) || waitToken(token) != RsTokenService::COMPLETE )
		{
			return false;
		}
	}
	
	std::vector<RsGxsGrpItem*> grpItems;
	if (!RsGenExchange::getGroupData(token, grpItems)) {
		return false;
	}

	for (std::vector<RsGxsGrpItem*>::iterator it = grpItems.begin(); it != grpItems.end(); ++it) {
		RsGitGroupItem* gitItem = dynamic_cast<RsGitGroupItem*>(*it);
		if (gitItem) {
			RsGitGroup g = gitItem->mGroup;
			g.mMeta = gitItem->meta;
			groups.push_back(g);
		}
		delete *it;
	}

	return !groups.empty();
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



bool p3Git::subscribeToGroup(uint32_t& token, const RsGxsGroupId& groupId, bool subscribe_flag)
{
    bool response = RsGenExchange::subscribeToGroup(token, groupId, subscribe_flag);

    if (response && rsEvents)
    {
        auto ev = std::make_shared<RsGitEvent>();
        ev->mGitGroupId = groupId;
        ev->mGitEventCode = RsGitEventCode::SUBSCRIBE_STATUS_CHANGED;
        rsEvents->postEvent(ev);
    }

    return response;
}

bool p3Git::subscribe(const RsGxsGroupId& groupId, bool subscribe_flag)
{
    uint32_t token;
    return subscribeToGroup(token, groupId, subscribe_flag);
}

bool p3Git::setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId, bool read)
{
    uint32_t token;
    setMessageReadStatus(token, msgId, read);
	
	if(waitToken(token) != RsTokenService::COMPLETE )
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
        auto ev = std::make_shared<RsGitEvent>();

        ev->mGitMsgId = msgId.second;
        ev->mGitGroupId = msgId.first;
        ev->mGitEventCode = RsGitEventCode::READ_STATUS_CHANGED;
        rsEvents->postEvent(ev);
    }
}
