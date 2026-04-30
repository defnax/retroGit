/*******************************************************************************
 * services/rsRetroGitItems.h                                                  *
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
 ********************************************************************************/

#include <map>

#include "rsitems/rsserviceids.h"
#include "serialiser/rsserial.h"
#include "rsitems/rsitem.h"
#include "interface/rsRetroGit.h" 
#include <retroshare/rsgxscommon.h>
#include <retroshare/rsgxsifacetypes.h>
#include <rsitems/rsgxsitems.h>
/**************************************************************************/

const uint16_t RS_SERVICE_TYPE_RetroGit_PLUGIN = 0xc5e5;

const uint8_t RS_PKT_SUBTYPE_RetroGit_DATA 	   = 0x01;
const uint8_t RS_PKT_SUBTYPE_RetroGit_CONFIG   = 0x02;

const uint8_t QOS_PRIORITY_RS_RetroGit = 9 ;


class RsGitGroupItem : public RsGxsGrpItem
{
public:
    RsGitGroupItem() : RsGxsGrpItem(RS_SERVICE_TYPE_RetroGit_PLUGIN, RS_PKT_SUBTYPE_GXS_GROUP_DATA_ITEM) {}
    virtual ~RsGitGroupItem() override {}

    virtual RsGitGroupItem* clone() const { return new RsGitGroupItem(*this); }

    RsGitGroup mGroup;

    // GXS methods
    virtual void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(meta);
        RS_SERIAL_PROCESS(mGroup.mGroupName);
        RS_SERIAL_PROCESS(mGroup.mGroupDescription);
    }
};


class RsGitConfigItem : public RsItem
{
public:
    RsGitConfigItem() : RsItem(RS_PKT_VERSION_SERVICE, RS_SERVICE_TYPE_RetroGit_PLUGIN, RS_PKT_SUBTYPE_RetroGit_CONFIG) {}
    virtual ~RsGitConfigItem() override {}

    virtual void clear() override { mKnownGit.clear(); }
    virtual void serial_process(RsGenericSerializer::SerializeJob j, RsGenericSerializer::SerializeContext& ctx) override
    {
        RS_SERIAL_PROCESS(mKnownGit);
    }

    std::map<uint32_t, uint32_t> mKnownGit;
};



class RsGitItem: public RsItem
{
public:
	RsGitItem(uint8_t RetroGit_subtype)
		: RsItem(RS_PKT_VERSION_SERVICE,RS_SERVICE_TYPE_RetroGit_PLUGIN,RetroGit_subtype)
	{
		setPriorityLevel(QOS_PRIORITY_RS_RetroGit) ;
	}

	virtual ~RsGitItem() {};
	virtual void clear() {};
	virtual void serial_process(RsGenericSerializer::SerializeJob, RsGenericSerializer::SerializeContext&) {}
	virtual std::ostream& print(std::ostream &out, uint16_t indent = 0) = 0 ;

	virtual bool serialise(void *data,uint32_t& size) = 0 ;	// Isn't it better that items can serialise themselves ?
	virtual uint32_t serial_size() const = 0 ; 							// deserialise is handled using a constructor
    
    RsGitGroup mGroup;
};


class RsGitDataItem: public RsGitItem
{
public:
	RsGitDataItem() :RsGitItem(RS_PKT_SUBTYPE_RetroGit_DATA) {}
	RsGitDataItem(void *data,uint32_t size) ; // de-serialization

	virtual bool serialise(void *data,uint32_t& size) ;
	virtual uint32_t serial_size() const ;

	virtual ~RsGitDataItem()
	{
	}
	virtual std::ostream& print(std::ostream &out, uint16_t indent = 0);

	uint32_t flags ;
	uint32_t data_size ;
	std::string m_msg;
	RsGxsId m_gxsId; // Optional: track origin GXS ID in the item
};


class RsGitSerialiser: public RsSerialType
{
public:
	RsGitSerialiser()
		:RsSerialType(RS_PKT_VERSION_SERVICE, RS_SERVICE_TYPE_RetroGit_PLUGIN)
	{
	}
	virtual ~RsGitSerialiser() {}

	virtual uint32_t 	size (RsItem *item)
	{
		return dynamic_cast<RsGitItem *>(item)->serial_size() ;
	}

	virtual	bool serialise  (RsItem *item, void *data, uint32_t *size)
	{
		return dynamic_cast<RsGitItem *>(item)->serialise(data,*size) ;
	}
	virtual	RsItem *deserialise(void *data, uint32_t *size);
};

class RsGxsRetroGitSerialiser : public RsServiceSerializer
{
public:
    RsGxsRetroGitSerialiser() : RsServiceSerializer(RS_SERVICE_TYPE_RetroGit_PLUGIN) {}
    virtual ~RsGxsRetroGitSerialiser() {}

    virtual RsItem *create_item(uint16_t service, uint8_t item_subtype) const override;
};
/**************************************************************************/
