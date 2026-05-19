/*******************************************************************************
 * services/rsGitItems.h                                                       *
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

#ifndef SERVICES_RS_GIT_ITEMS_H
#define SERVICES_RS_GIT_ITEMS_H

#include <map>

#include "rsitems/rsserviceids.h"
#include "rsitems/rsitem.h"
#include "interface/rsGit.h"
#include <retroshare/rsgxscommon.h>
#include <retroshare/rsgxsifacetypes.h>
#include <rsitems/rsgxsitems.h>
#include "serialiser/rsserializer.h"

/**************************************************************************/

/* Service and config service IDs */
const uint16_t RS_SERVICE_TYPE_RetroGit_PLUGIN        = 0xc5e5;
const uint16_t RS_SERVICE_TYPE_RetroGit_PLUGIN_CONFIG = 0xc5e6;

/* Item subtypes for the GXS service (RS_SERVICE_TYPE_RetroGit_PLUGIN) */
const uint8_t RS_PKT_SUBTYPE_RetroGit_GROUP  = 0x02;  // GXS group item (repository metadata)
const uint8_t RS_PKT_SUBTYPE_RetroGit_MSG    = 0x03;  // GXS message item (commit/issue metadata)


/* Item subtypes for the config service (RS_SERVICE_TYPE_RetroGit_PLUGIN_CONFIG) */
const uint8_t RS_PKT_SUBTYPE_RetroGit_CONFIG = 0x01;  // Persistent plugin config

const uint8_t QOS_PRIORITY_RS_RetroGit = 9;

/**************************************************************************/
/* GXS Group Item — represents a Git Repository group                     */
/**************************************************************************/

class RsGitGroupItem : public RsGxsGrpItem
{
public:
    RsGitGroupItem()
        : RsGxsGrpItem(RS_SERVICE_TYPE_RetroGit_PLUGIN, RS_PKT_SUBTYPE_RetroGit_GROUP) {}

    virtual ~RsGitGroupItem() override {}

    RsGitGroup mGroup;

    // Do NOT serialize 'meta' here — the GXS layer handles it automatically
    virtual void serial_process(RsGenericSerializer::SerializeJob j,
                                RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(mGroup.mGroupName);
        RS_SERIAL_PROCESS(mGroup.mGroupDescription);
    }

    void clear() override
    {
        mGroup.mGroupName.clear();
        mGroup.mGroupDescription.clear();
    }
};

/**************************************************************************/
/* GXS Msg Item — represents a Git commit/issue                           */
/**************************************************************************/

class RsGitMsgItem : public RsGxsMsgItem
{
public:
    RsGitMsgItem()
        : RsGxsMsgItem(RS_SERVICE_TYPE_RetroGit_PLUGIN, RS_PKT_SUBTYPE_RetroGit_MSG) {}

    virtual ~RsGitMsgItem() override {}

    uint32_t mGitMsgType = 0; // 1 = UPDATE, 2 = PULL_REQUEST
    std::string mPackfileData;
    std::map<std::string, std::string> mRefUpdates;
    std::vector<RsGxsFile> mFiles;
    
    // For pull requests
    std::string mTitle;
    std::string mDescription;
    std::string mTargetBranch;
    std::string mSourceBranch;
    uint32_t mStatus = 0;

    virtual void serial_process(RsGenericSerializer::SerializeJob j,
                                RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(mGitMsgType);
        RS_SERIAL_PROCESS(mPackfileData);
        RS_SERIAL_PROCESS(mRefUpdates);
        RS_SERIAL_PROCESS(mFiles);
        RS_SERIAL_PROCESS(mTitle);
        RS_SERIAL_PROCESS(mDescription);
        RS_SERIAL_PROCESS(mTargetBranch);
        RS_SERIAL_PROCESS(mSourceBranch);
        RS_SERIAL_PROCESS(mStatus);
    }

    void clear() override
    {
        mGitMsgType = 0;
        mPackfileData.clear();
        mRefUpdates.clear();
        mFiles.clear();
        mTitle.clear();
        mDescription.clear();
        mTargetBranch.clear();
        mSourceBranch.clear();
        mStatus = 0;
    }
};

/**************************************************************************/
/* Config Item — persists plugin state across restarts                    */
/**************************************************************************/

struct RsGitConfigItem : public RsItem
{
    RsGitConfigItem()
        : RsItem(RS_PKT_VERSION_SERVICE, RS_SERVICE_TYPE_RetroGit_PLUGIN_CONFIG,
                 RS_PKT_SUBTYPE_RetroGit_CONFIG) {}

    virtual ~RsGitConfigItem() {}

    void serial_process(RsGenericSerializer::SerializeJob j,
                        RsGenericSerializer::SerializeContext& ctx)
    { RS_SERIAL_PROCESS(mKnownGit); }

    void clear() { mKnownGit.clear(); }

    std::map<uint32_t, uint32_t> mKnownGit;
};

/**************************************************************************/
/* GXS Serializer — handles group items for the GXS exchange layer        */
/**************************************************************************/

class RsGxsRetroGitSerialiser : public RsServiceSerializer
{
public:
    RsGxsRetroGitSerialiser()
        : RsServiceSerializer(RS_SERVICE_TYPE_RetroGit_PLUGIN) {}

    virtual ~RsGxsRetroGitSerialiser() {}

    RsItem* create_item(uint16_t service_id, uint8_t item_sub_id) const override
    {
        if (service_id != RS_SERVICE_TYPE_RetroGit_PLUGIN)
            return nullptr;

        switch (item_sub_id)
        {
        case RS_PKT_SUBTYPE_RetroGit_GROUP: return new RsGitGroupItem();
        case RS_PKT_SUBTYPE_RetroGit_MSG: return new RsGitMsgItem();
        default:
            return nullptr;
        }
    }
};

/**************************************************************************/
/* Config Serializer — handles config items for saveList/loadList         */
/**************************************************************************/

class RsGitConfigSerializer : public RsServiceSerializer
{
public:
    RsGitConfigSerializer()
        : RsServiceSerializer(RS_SERVICE_TYPE_RetroGit_PLUGIN_CONFIG) {}

    virtual ~RsGitConfigSerializer() {}

    RsItem* create_item(uint16_t service_id, uint8_t item_sub_id) const override
    {
        if (service_id != RS_SERVICE_TYPE_RetroGit_PLUGIN_CONFIG)
            return nullptr;

        switch (item_sub_id)
        {
        case RS_PKT_SUBTYPE_RetroGit_CONFIG: return new RsGitConfigItem();
        default:
            return nullptr;
        }
    }
};

/**************************************************************************/

#endif // SERVICES_RS_GIT_ITEMS_H
