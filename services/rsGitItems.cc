/*******************************************************************************
 * services/rsRetroGitItems.cc                                                *
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

#include <stdexcept>
#include "serialiser/rsbaseserial.h"
#include "serialiser/rstlvbase.h"

#include "services/rsGitItems.h"

/***
#define RSSERIAL_DEBUG 1
***/

#include <iostream>

#define HOLLERITH_LEN_SPEC 4
/*************************************************************************/

std::ostream& RsGitDataItem::print(std::ostream &out, uint16_t indent)
{
	printRsItemBase(out, "RsGitDataItem", indent);
	uint16_t int_Indent = indent + 2;
	printIndent(out, int_Indent);
	out << "flags: " << flags << std::endl;

	printIndent(out, int_Indent);
	out << "data size: " << std::hex << data_size << std::dec << std::endl;

	printRsItemEnd(out, "RsGitDataItem", indent);
	return out;
}

/*************************************************************************/
uint32_t RsGitDataItem::serial_size() const
{
	uint32_t s = 8; /* header */
	s += 4; /* flags */
	s += 4; /* data_size  */
	//s += m_msg.length()+HOLLERITH_LEN_SPEC; /* data */
	s += getRawStringSize(m_msg);

	return s;
}

/* serialise the data to the buffer */
bool RsGitDataItem::serialise(void *data, uint32_t& pktsize)
{
	uint32_t tlvsize = serial_size() ;
	uint32_t offset = 0;

	if (pktsize < tlvsize)
		return false; /* not enough space */

	pktsize = tlvsize;

	bool ok = true;

	ok &= setRsItemHeader(data, tlvsize, PacketId(), tlvsize);

#ifdef RSSERIAL_DEBUG
	std::cerr << "RsGitSerialiser::serialiseRetroGitDataItem() Header: " << ok << std::endl;
	std::cerr << "RsGitSerialiser::serialiseRetroGitDataItem() Size: " << tlvsize << std::endl;
#endif

	/* skip the header */
	offset += 8;

	/* add mandatory parts first */
	ok &= setRawUInt32(data, tlvsize, &offset, flags);
	ok &= setRawUInt32(data, tlvsize, &offset, data_size);


	ok &= setRawString(data, tlvsize, &offset, m_msg );
	std::cout << "string sizes: " << getRawStringSize(m_msg) << " OR " << m_msg.size() << "\n";

	if (offset != tlvsize)
	{
		ok = false;
		std::cerr << "RsGitSerialiser::serialiseRetroGitPingItem() Size Error! " << std::endl;
		std::cerr << "expected " << tlvsize << " got " << offset << std::endl;
		std::cerr << "m_msg looks like " << m_msg << std::endl;
	}

	return ok;
}
/* serialise the data to the buffer */

/*************************************************************************/
/*************************************************************************/

RsGitDataItem::RsGitDataItem(void *data, uint32_t pktsize)
	: RsGitItem(RS_PKT_SUBTYPE_RetroGit_DATA)
{
	/* get the type and size */
	uint32_t rstype = getRsItemId(data);
	uint32_t rssize = getRsItemSize(data);

	uint32_t offset = 0;

	if ((RS_PKT_VERSION_SERVICE != getRsItemVersion(rstype)) || (RS_SERVICE_TYPE_RetroGit_PLUGIN != getRsItemService(rstype)) || (RS_PKT_SUBTYPE_RetroGit_DATA != getRsItemSubType(rstype)))
		throw std::runtime_error("Wrong packet subtype") ;

	if (pktsize < rssize)    /* check size */
		throw std::runtime_error("Not enough space") ;

	bool ok = true;

	/* skip the header */
	offset += 8;

	/* get mandatory parts first */
	ok &= getRawUInt32(data, rssize, &offset, &flags);
	ok &= getRawUInt32(data, rssize, &offset, &data_size);


	ok &= getRawString(data, rssize, &offset, m_msg );

	if (offset != rssize)
		throw std::runtime_error("Serialization error.") ;

	if (!ok)
		throw std::runtime_error("Serialization error.") ;
}
/*************************************************************************/

RsItem* RsGitSerialiser::deserialise(void *data, uint32_t *pktsize)
{
#ifdef RSSERIAL_DEBUG
	std::cerr << "RsGitSerialiser::deserialise()" << std::endl;
#endif

	/* get the type and size */
	uint32_t rstype = getRsItemId(data);

	if ((RS_PKT_VERSION_SERVICE != getRsItemVersion(rstype)) || (RS_SERVICE_TYPE_RetroGit_PLUGIN != getRsItemService(rstype)))
		return NULL ;

	try
	{
		switch(getRsItemSubType(rstype))
		{
		case RS_PKT_SUBTYPE_RetroGit_DATA:
			return new RsGitDataItem(data, *pktsize);

		default:
			return NULL;
		}
	}
	catch(std::exception& e)
	{
		std::cerr << "RsGitSerialiser: deserialization error: " << e.what() << std::endl;
		return NULL;
	}
}


/*************************************************************************/

