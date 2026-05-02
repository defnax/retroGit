/*******************************************************************************
 * gui/MainWidget.cpp                                                          *
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

#include "MainWidget.h"
#include "ui_MainWidget.h"
#include "GitGroupDialog.h"

#include "services/p3Git.h"
#include "interface/rsGit.h"
#include "services/rsGitItems.h"
#include "retroshare/rsservicecontrol.h"
#include "retroshare/rsgxsflags.h"
#include "gui/gxs/GxsIdDetails.h"
#include "gui/settings/rsharesettings.h"

#include <iostream>
#include <string>
#include <QTime>
#include <QMenu>

#include <util/rsthreads.h>
#include "util/qtthreadsutils.h"
#include "util/DateTime.h"
#define IMAGE_GIT           ":/images/git.png"

MainWidget::MainWidget(QWidget *parent, RetroGitNotify *notify) :
	MainPage(parent),
	//mNotify(notify),
	ui(new Ui::MainWidget)
{
	(void)notify;
	ui->setupUi(this);

    /* Set initial size the splitter */
    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);

    /* Setup Group Tree */
    mYourGroups = ui->treeWidget->addCategoryItem(tr("My Groups"), QIcon(), true);
    mSubscribedGroups = ui->treeWidget->addCategoryItem(tr("Subscribed Groups"), QIcon(), true);
    mPopularGroups = ui->treeWidget->addCategoryItem(tr("Popular Groups"), QIcon(), false);
    mOtherGroups = ui->treeWidget->addCategoryItem(tr("Other Groups"), QIcon(), false);

    /* Add the New Group button */
    QToolButton *newGroupButton = new QToolButton(this);
    newGroupButton->setIcon(QIcon(":/icons/png/add.png"));
    newGroupButton->setToolTip(tr("Create Group"));
    ui->treeWidget->addToolButton(newGroupButton);

    // load settings
    processSettings(true);

    connect(newGroupButton, SIGNAL(clicked()), this, SLOT(createGroup()));
	connect( ui->toolButton_createGit, SIGNAL(clicked()), this, SLOT(createGroup()));

	loadGroupMeta();

	/* Register for Git events using the static RsEventType::GIT */
	if (rsEvents)
	{
		rsEvents->registerEventsHandler(
			[this](std::shared_ptr<const RsEvent> event) {
				RsQThreadUtils::postToObject([=]() {
					handleEvent_main_thread(event);
				}, this);
			},
			mEventHandlerId, RsEventType::GIT);
	}
}

MainWidget::~MainWidget()
{
	if (rsEvents)
		rsEvents->unregisterEventsHandler(mEventHandlerId);

	delete ui;
	
	// save settings
	processSettings(false);
}

void MainWidget::showEvent(QShowEvent *event)
{
	MainPage::showEvent(event);
	
	// Load data once when the dialog is first shown
	if (!mInitialLoadDone)
	{
		mInitialLoadDone = true;
		updateDisplay();
	}
}

void MainWidget::processSettings(bool load)
{
	Settings->beginGroup("RetrGit");

	if (load) {
		// load settings

		// state of splitter
		ui->splitter->restoreState(Settings->value("SplitterList").toByteArray());

	} else {
		// save settings

		// state of splitter
		Settings->setValue("SplitterList", ui->splitter->saveState());
	}

	Settings->endGroup();
}

void MainWidget::createGroup()
{
	GitGroupDialog gitCreate(this);
	gitCreate.exec();
}

void MainWidget::loadGroupMeta()
{
	RsThread::async([this]()
	{
		// Fetch group metadata from backend
		std::list<RsGxsGroupId> groupIds; // empty list = get all groups
		std::vector<RsGitGroup> groups;
		
		if (!rsGit->getGroups(groupIds, groups))
		{
			std::cerr << "MainWidget::loadGroupMeta() Error getting groups" << std::endl;
			return;
		}

		// Convert to RsGroupMetaData for display
		std::list<RsGroupMetaData> groupMeta;
		for (auto& group : groups)
		{
			groupMeta.push_back(group.mMeta);
		}

		// Update UI in main thread
		RsQThreadUtils::postToObject([this, groupMeta]()
		{
			if (groupMeta.size() > 0)
			{
				insertGroupsData(groupMeta);
			}
		}, this);
	});
}

void MainWidget::insertGroupsData(const std::list<RsGroupMetaData> &gitList)
{
	std::list<RsGroupMetaData>::const_iterator it;

	QList<GroupItemInfo> adminList;
	QList<GroupItemInfo> subList;
	QList<GroupItemInfo> popList;
	QList<GroupItemInfo> otherList;
	std::multimap<uint32_t, GroupItemInfo> popMap;

	for (it = gitList.begin(); it != gitList.end(); ++it) {
		/* sort it into Publish (Own), Subscribed, Popular and Other */
		uint32_t flags = it->mSubscribeFlags;

		GroupItemInfo groupItemInfo;
		GroupMetaDataToGroupItemInfo(*it, groupItemInfo);

		if (IS_GROUP_ADMIN(flags)) {
			adminList.push_back(groupItemInfo);
		} else if (IS_GROUP_SUBSCRIBED(flags)) {
			/* subscribed forum */
			subList.push_back(groupItemInfo);
		} else {
			/* rate the others by popularity */
			popMap.insert(std::make_pair(it->mPop, groupItemInfo));
		}
	}

	/* iterate backwards through popMap - take the top 5 or 10% of list */
	uint32_t popCount = 5;
	if (popCount < popMap.size() / 10)
	{
		popCount = popMap.size() / 10;
	}

	uint32_t i = 0;
	uint32_t popLimit = 0;
	std::multimap<uint32_t, GroupItemInfo>::reverse_iterator rit;
	for(rit = popMap.rbegin(); ((rit != popMap.rend()) && (i < popCount)); ++rit, i++) ;
	if (rit != popMap.rend()) {
		popLimit = rit->first;
	}

	for (rit = popMap.rbegin(); rit != popMap.rend(); ++rit) {
		if (rit->second.popularity < (int) popLimit) {
			otherList.append(rit->second);
		} else {
			popList.append(rit->second);
		}
	}

	/* now we can add them in as a tree! */
	ui->treeWidget->fillGroupItems(mYourGroups, adminList);
	ui->treeWidget->fillGroupItems(mSubscribedGroups, subList);
	ui->treeWidget->fillGroupItems(mPopularGroups, popList);
	ui->treeWidget->fillGroupItems(mOtherGroups, otherList);
}

void MainWidget::GroupMetaDataToGroupItemInfo(const RsGroupMetaData &groupInfo, GroupItemInfo &groupItemInfo)
{
	groupItemInfo.id = QString::fromStdString(groupInfo.mGroupId.toStdString());
	groupItemInfo.name = QString::fromUtf8(groupInfo.mGroupName.c_str());
	//groupItemInfo.description = QString::fromUtf8(groupInfo.forumDesc);
	groupItemInfo.popularity = groupInfo.mPop;
	groupItemInfo.lastpost = DateTime::DateTimeFromTime_t(groupInfo.mLastPost);
	groupItemInfo.subscribeFlags = groupInfo.mSubscribeFlags;

	groupItemInfo.icon = GxsIdDetails::makeDefaultGroupIcon(groupInfo.mGroupId, IMAGE_GIT, GxsIdDetails::ORIGINAL);
}

void MainWidget::updateDisplay()
{
	// Load all group metadata
	loadGroupMeta();
}

void MainWidget::handleEvent_main_thread(std::shared_ptr<const RsEvent> event)
{
	const RsGitEvent *e = dynamic_cast<const RsGitEvent*>(event.get());

	if (!e)
		return;

	switch (e->mGitEventCode)
	{
		case RsGitEventCode::NEW_GIT:
			updateDisplay(); // Refresh global list
		case RsGitEventCode::GIT_UPDATED:
			updateDisplay(); // Refresh global list
		case RsGitEventCode::SUBSCRIBE_STATUS_CHANGED:
			updateDisplay();
			break;

		default:
			break;
	}
}
