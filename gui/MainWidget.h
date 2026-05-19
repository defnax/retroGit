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

#include "gui/RetroGitNotify.h"
#include <retroshare/rsfiles.h>
#include <retroshare/rspeers.h>

#include "gui/common/GroupTreeWidget.h"
#include <list>
#include <retroshare/rsevents.h>
#include <retroshare/rsgxsifacetypes.h>

#include <QPoint>
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
    void selectGroupSet(int index);
    void groupListCustomPopupMenu(QPoint point);
    void showGroupDetails();
    void editGroupDetails();
    
    void onPushClicked();
    void onPullClicked();
    void onCommitClicked();
    
    void onBrowseClicked();
    void onOpenFolderClicked();
    void onCloneClicked();
    void onTreeSelectionChanged();
    void onLocalPathChanged(const QString &text);

private:
    void loadGroupMeta();
    void saveRepoLocalPath(const QString &groupId, const QString &path);
    QString loadRepoLocalPath(const QString &groupId);
    void populateCommitLog(const QString &groupId);
    void populateRepoBrowser(const QString &groupId);
    void insertGroupsData(const std::list<RsGroupMetaData> &gitList);
    void GroupMetaDataToGroupItemInfo(const RsGroupMetaData &groupInfo,
                                    GroupItemInfo &groupItemInfo);
    void handleEvent_main_thread(std::shared_ptr<const RsEvent> event);
    void processSettings(bool load);

    QTreeWidgetItem *mActiveGroupsItem;
    int mGroupSet = 0;
    bool mInitialLoadDone = false;

    RsEventsHandlerId_t mEventHandlerId;

    Ui::MainWidget *ui;
    
    // UI elements for the right pane
    class QLineEdit *mLocalPathEdit;
    class QPushButton *mBtnOpenFolder;
    class QPushButton *mBtnPush;
    class QPushButton *mBtnPull;
    class QPushButton *mBtnClone;
    class QPushButton *mBtnCommit;
    class QTableWidget *mCommitTable;
    class QListWidget *mRepoBrowserList;
};

#endif // MAINPAGE_H
