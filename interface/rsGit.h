/*******************************************************************************
 * interface/rsGit.h                                                           *
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

#ifndef INTERFACE_RS_GIT_H
#define INTERFACE_RS_GIT_H

#include <retroshare/rsevents.h>
#include <retroshare/rsgxscommon.h> // For RsGxsMeta
#include <retroshare/rsgxsifacetypes.h>
#include <retroshare/rstypes.h>
#include <stdint.h>
#include <string>
#include <vector>


#include <QString>
#include <QVariantMap>


class RsGit;
extern RsGit *rsGit;

static const uint32_t CONFIG_TYPE_RetroGit_PLUGIN = 0xe001;

/**
 * @brief Data structure representing a RetroGit Group (Repository)
 */
struct RsGitGroup : public RsGxsGenericGroupData
{
      std::string mGroupName;
      std::string mGroupDescription;
};

struct RsGitUpdate
{
    RsMsgMetaData mMeta;
    std::string mPackfileData;
    std::map<std::string, std::string> mRefUpdates;
    std::vector<RsGxsFile> mFiles;
};

struct RsGitPullRequest
{
    RsMsgMetaData mMeta;
    std::string mTitle;
    std::string mDescription;
    std::string mTargetBranch;
    std::string mSourceBranch;
    std::string mPackfileData;
    uint32_t mStatus;
    std::vector<RsGxsFile> mFiles;
};

enum class RsGitEventCode: uint8_t
{
    UNKNOWN                       = 0x00,  // event not recognized
    NEW_GIT                       = 0x01,  // emitted when new git is received
    NEW_POST                      = 0x02, // this event happens when there is a new commit
    SUBSCRIBE_STATUS_CHANGED      = 0x03, // this event happens when we subscribe a git repository
    POST_UPDATED                  = 0x04, // this event happens when there is any change to a commit
    GIT_UPDATED                   = 0x05, // this event happens when there is any change to the git account
    READ_STATUS_CHANGED           = 0x06 // existing message has been read or set to unread
};

struct RsGitEvent : RsEvent
{
    RsGitEvent()
      : RsEvent(RsEventType::GIT),
      mGitEventCode(RsGitEventCode::UNKNOWN) {}

    RsGitEventCode mGitEventCode;
    RsGxsGroupId mGitGroupId;
    RsGxsMessageId mGitMsgId;
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
    virtual bool updateGroup(uint32_t &token, RsGitGroup &group) = 0;

    // Blocking Interfaces.
    virtual bool createGroup(RsGitGroup &group) = 0;
    virtual bool updateGroup(RsGitGroup &group) = 0;

    /**
     * @brief Publish a Git Push/Update (Packfile + Refs) to a Repository.
     */
    virtual bool publishGitUpdate(uint32_t &token, RsGitUpdate &update) = 0;

    /**
     * @brief Publish a Pull Request to a Repository.
     */
    virtual bool publishPullRequest(uint32_t &token, RsGitPullRequest &pr) = 0;

    virtual bool getGroups(const std::list<RsGxsGroupId> &groupIds,std::vector<RsGitGroup> &groups) = 0;

    /**
    * @brief Subscribe or unsubscribe to a RetroGit group
    * @param token Request token
    * @param groupId ID of the group
    * @param subscribe True to subscribe, false to unsubscribe
    * @return true if successful
    */
    virtual bool subscribeToGroup(uint32_t &token, const RsGxsGroupId &groupId,bool subscribe) = 0;

    /**
    * @brief Blocking/Simplified subscribe or unsubscribe to a RetroGit group
    * @param groupId ID of the group
    * @param subscribe True to subscribe, false to unsubscribe
    * @return true if successful
    */
    virtual bool subscribe(const RsGxsGroupId &groupId, bool subscribe) = 0;

    /**
    * @brief Set the read status of a message (commit/issue)
    * @param msgId The Group ID and Message ID pair
    * @param read True to mark as read, false for unread
    * @return true if successful
    */
    virtual bool setMessageReadStatus(const RsGxsGrpMsgIdPair &msgId,bool read) = 0;

    /**
    * @brief Async set the read status of a message
    * @param token Request token
    * @param msgId The Group ID and Message ID pair
    * @param read True to mark as read, false for unread
    */
    virtual void setMessageReadStatus(uint32_t &token,const RsGxsGrpMsgIdPair &msgId,bool read) = 0;
};

extern RsGit *rsGit;

#endif // INTERFACE_RS_GIT_H
