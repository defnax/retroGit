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
#include "interface/rsGit.h"

#include "gui/common/GroupTreeWidget.h"
#include <list>
#include <retroshare/rsevents.h>
#include <retroshare/rsgxsifacetypes.h>

#include <QPoint>
#include <QWidget>


#include "gui/gxs/GxsIdDetails.h"

class QAction;
class QLabel;

class GxsIdTableItem : public QWidget
{
    Q_OBJECT
public:
    explicit GxsIdTableItem(QWidget *parent = nullptr);
    void setId(const RsGxsId &id);
    bool getId(RsGxsId &id) const;

private:
    static void fillCallback(GxsIdDetailsType type, const RsIdentityDetails &details, QObject *object, const QVariant &data);
    RsGxsId mId;
    QLabel *mIconLabel;
    QLabel *mTextLabel;
};

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

    virtual class UserNotify *createUserNotify(QObject *parent) override;

protected:
    virtual void showEvent(QShowEvent *event) override;

private slots:
    void createGroup();
    void updateDisplay();
    void selectGroupSet(int index);
    void groupListCustomPopupMenu(QPoint point);
    void showGroupDetails();
    void editGroupDetails();
    void subscribeToGroup();
    void unsubscribeFromGroup();
    
    void onPushClicked();
    void onPullClicked();
    void onCommitClicked();
    
    void onBrowseClicked();
    void onOpenFolderClicked();
    void onCloneClicked();
    void onTreeSelectionChanged();
    void onLocalPathChanged(const QString &text);
    void onCommitSelectionChanged();
    void onChangedFilesContextMenu(const QPoint &pos);
    void onChangedFilesDoubleClicked(class QTreeWidgetItem *item, int column);
    void onCommitTableContextMenu(const QPoint &pos);
    void onTabCloseRequested(int index);
    void onRepoBrowserContextMenu(const QPoint &pos);
    void onClonesTableContextMenu(const QPoint &pos);
    void openSelectedFile();
    void pollDownloadProgress();
    void onDownloadClicked();
    void onCancelDownloadClicked();
    void onCommitReadStatusToggled(const QString &msgIdStr, bool markRead);
    void markRepositoryAsRead();

private:
    void showDiffForFile(const QString &filePath);
    void showDiffForCommit(const QString &commitHash);
    void refreshCurrentRepo();
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
    void populatePackfiles(const QString &groupId);

    QTreeWidgetItem *mActiveGroupsItem;
    int mGroupSet = 0;
    bool mInitialLoadDone = false;

    RsEventsHandlerId_t mEventHandlerId;

    Ui::MainWidget *ui;
    
    // UI elements for the right pane
    class QLineEdit *mLocalPathEdit;
    class QPushButton *mBtnBrowse;
    class QPushButton *mBtnOpenFolder;
    class QPushButton *mBtnPush;
    class QPushButton *mBtnPull;
    class QPushButton *mBtnClone;
    class QPushButton *mBtnCommit;
    class QTableWidget *mCommitTable;
    class QListWidget *mRepoBrowserList;
    class QTableWidget *mPackfilesTable;
    class QTimer *mDownloadPollTimer;
    std::vector<RsGitUpdate> mAvailableUpdates;

    struct CloneRecord {
        QString repoId;
        QString repoName;
        QString ownerId;
        QString status;
        QString time;
    };
    std::vector<CloneRecord> mCloneHistory;
    void populateClonesTable();

    class QWidget *mPackfilesTab;
    class QTableWidget *mClonesTable;
    class QLabel *mLblOwnerInfo;
    class QLabel *mLblSubscriberInfo;

    // UI elements for commit details (left pane)
    class QWidget *mCommitDetailsWidget;
    class QFrame *mDetailsAuthorFrame;
    class QLabel *mDetailsAuthorNameLabel;
    class QLabel *mDetailsAuthorEmailLabel;
    class QLabel *mDetailsHashLabel;
    class QLabel *mDetailsTitleLabel;
    class QLabel *mDetailsBodyText;
    class QLabel *mDetailsDateLabel;
    class QTreeWidget *mChangedFilesTree;

    FontSizeHandler mFontSizeHandler;
};

#endif // MAINPAGE_H
