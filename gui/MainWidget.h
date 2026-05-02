/*******************************************************************************
 * gui/MainWidget.h                                                            *
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

#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <retroshare-gui/mainpage.h>

#include <retroshare/rsfiles.h>
#include <retroshare/rspeers.h>
#include "gui/RetroGitNotify.h"

#include <list>
#include <retroshare/rsgxsifacetypes.h>
#include <retroshare/rsevents.h>
#include "gui/common/GroupTreeWidget.h"

#include <QWidget>

class QAction;

namespace Ui
{
class MainWidget;
}

class MainWidget : public MainPage
{
	Q_OBJECT

public:
	explicit MainWidget(QWidget *parent, RetroGitNotify *notify);
	~MainWidget();

protected:
	virtual void showEvent(QShowEvent *event) override;

private slots:
	void createGroup();
	void updateDisplay();

private:
	void loadGroupMeta();
	void insertGroupsData(const std::list<RsGroupMetaData> &gitList);
	void GroupMetaDataToGroupItemInfo(const RsGroupMetaData &groupInfo, GroupItemInfo &groupItemInfo);
	void handleEvent_main_thread(std::shared_ptr<const RsEvent> event);
	void processSettings(bool load);

	QTreeWidgetItem *mYourGroups;
	QTreeWidgetItem *mSubscribedGroups;
	QTreeWidgetItem *mPopularGroups;
	QTreeWidgetItem *mOtherGroups;
	bool mInitialLoadDone = false;

	RsEventsHandlerId_t mEventHandlerId;

	Ui::MainWidget *ui;

};

#endif // MAINPAGE_H
