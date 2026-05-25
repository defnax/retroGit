/*******************************************************************************
 * gui/TheGit/GitGroupDialog.cpp                                              *
 *                                                                             *
 * Copyright (C) 2020 by Robert Fernie       <retroshare.project@gmail.com>    *
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
#include <QBuffer>

#include "GitGroupDialog.h"
#include "gui/common/FilesDefs.h"
#include "gui/gxs/GxsIdDetails.h"
#include "interface/rsGit.h"

#include <iostream>

const uint32_t GitCreateEnabledFlags = (GXS_GROUP_FLAGS_NAME |
                                  // GXS_GROUP_FLAGS_ICON          |
                                  GXS_GROUP_FLAGS_DESCRIPTION | GXS_GROUP_FLAGS_DISTRIBUTION |
                                  // GXS_GROUP_FLAGS_PUBLISHSIGN   |
                                  // GXS_GROUP_FLAGS_SHAREKEYS     |	// disabled because the UI
                                  // doesn't handle it yet. GXS_GROUP_FLAGS_PERSONALSIGN  |
                                  // GXS_GROUP_FLAGS_COMMENTS      |
                                  // GXS_GROUP_FLAGS_EXTRA         |
                                  0);

uint32_t GitCreateDefaultsFlags = (GXS_GROUP_DEFAULTS_DISTRIB_PUBLIC |
                                   // GXS_GROUP_DEFAULTS_DISTRIB_GROUP       |
                                   // GXS_GROUP_DEFAULTS_DISTRIB_LOCAL       |

                                   GXS_GROUP_DEFAULTS_PUBLISH_OPEN |
                                   // GXS_GROUP_DEFAULTS_PUBLISH_THREADS     |
                                   // GXS_GROUP_DEFAULTS_PUBLISH_REQUIRED    |
                                   // GXS_GROUP_DEFAULTS_PUBLISH_ENCRYPTED   |

                                   // GXS_GROUP_DEFAULTS_PERSONAL_GPG        |
                                   GXS_GROUP_DEFAULTS_PERSONAL_REQUIRED |
                                   // GXS_GROUP_DEFAULTS_PERSONAL_IFNOPUB    |
                                   GXS_GROUP_DEFAULTS_PERSONAL_GROUP |

                                   // GXS_GROUP_DEFAULTS_COMMENTS_YES |
                                   GXS_GROUP_DEFAULTS_COMMENTS_NO | 0);

uint32_t GitEditEnabledFlags = GitCreateEnabledFlags;
uint32_t GitEditDefaultsFlags = GitCreateDefaultsFlags;

GitGroupDialog::GitGroupDialog(QWidget *parent)
    : GxsGroupDialog(GitCreateEnabledFlags, GitCreateDefaultsFlags, parent)
{

}

GitGroupDialog::GitGroupDialog(Mode mode, RsGxsGroupId groupId, QWidget *parent)
    : GxsGroupDialog(mode, groupId, GitEditEnabledFlags, GitEditDefaultsFlags, parent)
{

}

void GitGroupDialog::initUi()
{
    switch (mode()) {
        case MODE_CREATE:
            setUiText(UITYPE_SERVICE_HEADER, tr("Create New Git"));
            setUiText(UITYPE_BUTTONBOX_OK, tr("Create"));
            break;
        case MODE_SHOW:
            setUiText(UITYPE_SERVICE_HEADER, tr("RetroGit"));
            break;
        case MODE_EDIT:
            setUiText(UITYPE_SERVICE_HEADER, tr("Edit Git"));
            setUiText(UITYPE_BUTTONBOX_OK, tr("Update Git"));
        break;
  }

    setUiText(UITYPE_ADD_ADMINS_CHECKBOX, tr("Add Git Admins"));
    setUiText(UITYPE_CONTACTS_DOCK, tr("Select Git Admins"));
}

QPixmap GitGroupDialog::serviceImage()
{
  return FilesDefs::getPixmapFromQtResourcePath(":/images/Git.png");
}

void GitGroupDialog::prepareGitGroup(RsGitGroup &group,const RsGroupMetaData &meta)
{
    group.mMeta = meta;
    group.mGroupName = meta.mGroupName;
    group.mGroupDescription = std::string(getDescription().toUtf8());
}

bool GitGroupDialog::service_createGroup(RsGroupMetaData &meta)
{
    RsGitGroup grp;
    prepareGitGroup(grp, meta);

    bool success = rsGit->createGroup(grp);
    // TODO createGroup should refresh groupId or Data
    return success;
}

bool GitGroupDialog::service_updateGroup(const RsGroupMetaData &editedMeta)
{
    RsGitGroup grp;
    prepareGitGroup(grp, editedMeta);

    std::cerr << "GitGroupDialog::service_updateGroup() submitting changes";
    std::cerr << std::endl;

    bool success = rsGit->updateGroup(grp);
    // TODO updateGroup should refresh groupId or Data
    return success;
}

bool GitGroupDialog::service_loadGroup(const RsGxsGenericGroupData *data,Mode mode, QString &description) 
{
    std::cerr << "GitGroupDialog::service_loadGroup()";
    std::cerr << std::endl;

    const RsGitGroup *pgroup = dynamic_cast<const RsGitGroup *>(data);
    if (pgroup == nullptr) {
    std::cerr << "GitGroupDialog::service_loadGroup() Error not a RsGitGroup";
    std::cerr << std::endl;
    return false;
    }

    const RsGitGroup &group = *pgroup;
    description = QString::fromUtf8(group.mGroupDescription.c_str());

    return true;
}

bool GitGroupDialog::service_getGroupData(const RsGxsGroupId &grpId,RsGxsGenericGroupData *&data)
{
    std::cerr << "GitGroupDialog::service_getGroupData(" << grpId << ")";
    std::cerr << std::endl;

    std::list<RsGxsGroupId> groupIds({grpId});
    std::vector<RsGitGroup> groups;
    if (!rsGit->getGroups(groupIds, groups)) {
    std::cerr << "GitGroupDialog::service_loadGroup() Error getting GroupData";
    std::cerr << std::endl;
    return false;
    }

    if (groups.size() != 1) {
    std::cerr << "GitGroupDialog::service_loadGroup() Error Group.size() != 1";
    std::cerr << std::endl;
    return false;
    }

    data = new RsGitGroup(groups[0]);
    return true;
}
