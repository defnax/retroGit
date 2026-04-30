/*******************************************************************************
 * interface/rsRetroGit.h                                                    *
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

#include <stdint.h>
#include <string>
#include <vector>
#include <retroshare/rstypes.h>
#include <retroshare/rsgxscommon.h> // For RsGxsMeta

#include <QVariantMap>
#include <QString>

class RsGit ;
extern RsGit *rsRetroGit;
 
static const uint32_t CONFIG_TYPE_RetroGit_PLUGIN = 0xe001 ;

/**
 * @brief Data structure representing a RetroGit Group (Repository)
 */
struct RsGitGroup
{
    std::string mGroupName;
    std::string mGroupDescription;
    
    // GXS Metadata (handles IDs, timestamps, signatures)
    RsGroupMetaData mMeta; 
};

class RsGit
{
public:
    virtual ~RsGit() {}

    /**
     * @brief Create a new RetroGit group/repository.
     * @param[out] token A token to track the progress of the creation.
     * @param[in] group The group data to be published.
     * @return true if the request was successfully queued.
     */
    virtual bool createGroup(uint32_t &token, RsGitGroup &group) = 0;
    
    // Blocking Interfaces.
    virtual bool createGroup(RsGitGroup &group) = 0;
};

extern RsGit *rsRetroGit;


