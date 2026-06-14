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
#include "PullRequestsWidget.h"
#include "GitGroupDialog.h"
#include "ui_MainWidget.h"

#include "gui/common/RSTreeWidget.h"
#include "gui/gxs/GxsIdDetails.h"
#include "util/HandleRichText.h"
#include "gui/gxs/GxsIdTreeWidgetItem.h"
#include "gui/GitUserNotify.h"
#include "gui/settings/rsharesettings.h"
#include "interface/rsGit.h"
#include "retroshare/rsgxsflags.h"
#include "retroshare/rsservicecontrol.h"
#include "retroshare/rsgxsifacehelper.h"
#include "retroshare/rsreputations.h"
#include <retroshare/rsinit.h>
#include "services/p3Git.h"
#include "services/rsGitItems.h"
#include "services/GitManager.h"
#include "GitCommitDialog.h"

#include <QMenu>
#include <QTimer>
#include <QTime>
#include <iostream>
#include <string>

#include "gui/common/RSTreeWidget.h"
#include "util/DateTime.h"
#include "util/qtthreadsutils.h"
#include "util/misc.h"
#include <util/rsthreads.h>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QListWidget>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QLocale>
#include <QThread>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QClipboard>
#include <QTextBrowser>
#include <QTabBar>
#include <QFileInfo>
#include <QShortcut>
#include <retroshare/rsfiles.h>
#include <retroshare/rsidentity.h>

#define IMAGE_GIT ":/images/git-white.png"

MainWidget::MainWidget(QWidget *parent, RetroGitNotify *notify):
    MainPage(parent),
    // mNotify(notify),
    ui(new Ui::MainWidget)
{
    (void)notify;
    ui->setupUi(this);

    /* Set initial size the splitter */
    ui->splitter->setStretchFactor(0, 0);
    ui->splitter->setStretchFactor(1, 1);

    /* Setup Combo Box strings */
    ui->comboBox->addItem(tr("All Repositories"), 0);
    ui->comboBox->addItem(tr("My Repositories"), 1);
    ui->comboBox->addItem(tr("Subscribed Repositories"), 2);
    ui->comboBox->addItem(tr("Popular Repositories"), 3);
    ui->comboBox->addItem(tr("Other Repositories"), 4);
    connect(ui->comboBox, SIGNAL(currentIndexChanged(int)), this,
          SLOT(selectGroupSet(int)));
    connect(ui->treeWidget, SIGNAL(treeCustomContextMenuRequested(QPoint)), this,
          SLOT(groupListCustomPopupMenu(QPoint)));

    /* Setup Group Tree */
    mActiveGroupsItem = ui->treeWidget->treeWidget()->invisibleRootItem();

    /* Add the New Group button */
    QToolButton *newGroupButton = new QToolButton(this);
    newGroupButton->setIcon(QIcon(":/icons/png/add.png"));
    newGroupButton->setToolTip(tr("Create Group"));
    ui->treeWidget->addToolButton(newGroupButton);
    
    // Clear Right Pane and add modular sub-widgets
    ui->rightPaneTabWidget->clear();
    
    mGitWidget = new GitWidget(this, ui->rightPaneTabWidget);
    mCodeWidget = new CodeWidget(this, ui->rightPaneTabWidget);
    mPushesWidget = new PushesWidget(this, ui->rightPaneTabWidget);
    
    ui->rightPaneTabWidget->addTab(mGitWidget, QIcon(":/images/git.png"), tr("Working Directory"));
    ui->rightPaneTabWidget->addTab(mCodeWidget, QIcon(":/images/git.png"), tr("Files"));
    ui->rightPaneTabWidget->addTab(mPushesWidget, QIcon(":/images/git.png"), tr("Pushes / Packs"));
    
    ui->rightPaneTabWidget->setTabsClosable(true);
    connect(ui->rightPaneTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    processSettings(true);

    connect(newGroupButton, SIGNAL(clicked()), this, SLOT(createGroup()));
    connect(ui->treeWidget->treeWidget(), SIGNAL(itemSelectionChanged()), this, SLOT(onTreeSelectionChanged()));

    // Create F5 shortcut for silent repository refresh
    QShortcut *refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, SIGNAL(activated()), this, SLOT(refreshCurrentRepo()));

    // Create Commit Details panel
    mCommitDetailsWidget = new QWidget(ui->layoutWidget);
    QVBoxLayout *detailsLayout = new QVBoxLayout(mCommitDetailsWidget);
    detailsLayout->setContentsMargins(0, 10, 0, 0);
    detailsLayout->setSpacing(6);

    // Separator line 1
    QFrame *line1 = new QFrame(mCommitDetailsWidget);
    line1->setFrameShape(QFrame::HLine);
    line1->setFrameShadow(QFrame::Sunken);
    detailsLayout->addWidget(line1);

    // SHA/Hash
    mDetailsHashLabel = new QLabel(mCommitDetailsWidget);
    mDetailsHashLabel->setStyleSheet("font-weight: bold; color: #d35400; font-size: 24px; font-family: 'DejaVu Sans Mono', monospace; qproperty-alignment: AlignCenter; padding-top: 5px;");
    detailsLayout->addWidget(mDetailsHashLabel);

    // Title / Summary
    mDetailsTitleLabel = new QLabel(mCommitDetailsWidget);
    mDetailsTitleLabel->setStyleSheet("font-weight: bold; font-size: 16px; color: #2c3e50; qproperty-alignment: AlignCenter;");
    mDetailsTitleLabel->setWordWrap(true);
    detailsLayout->addWidget(mDetailsTitleLabel);

    // Body / Message description
    mDetailsBodyText = new QLabel(mCommitDetailsWidget);
    mDetailsBodyText->setStyleSheet("font-size: 13px; color: #34495e; qproperty-alignment: AlignCenter;");
    mDetailsBodyText->setWordWrap(true);
    detailsLayout->addWidget(mDetailsBodyText);

    // Author & Date Frame (grey card style)
    mDetailsAuthorFrame = new QFrame(mCommitDetailsWidget);
    mDetailsAuthorFrame->setStyleSheet("QFrame { background-color: #e0e0e0; border: none; border-radius: 4px; }");
    QVBoxLayout *authorLayout = new QVBoxLayout(mDetailsAuthorFrame);
    authorLayout->setContentsMargins(8, 8, 8, 8);
    authorLayout->setSpacing(4);

    mDetailsAuthorNameLabel = new QLabel(mDetailsAuthorFrame);
    mDetailsAuthorNameLabel->setStyleSheet("font-weight: bold; font-size: 13px; color: #2c3e50; border: none; background: transparent;");
    mDetailsAuthorNameLabel->setWordWrap(true);
    authorLayout->addWidget(mDetailsAuthorNameLabel);

    mDetailsAuthorEmailLabel = new QLabel(mDetailsAuthorFrame);
    mDetailsAuthorEmailLabel->setStyleSheet("color: #555555; font-size: 12px; border: none; background: transparent;");
    mDetailsAuthorEmailLabel->setWordWrap(true);
    authorLayout->addWidget(mDetailsAuthorEmailLabel);

    mDetailsDateLabel = new QLabel(mDetailsAuthorFrame);
    mDetailsDateLabel->setStyleSheet("color: #555555; font-size: 12px; border: none; background: transparent;");
    authorLayout->addWidget(mDetailsDateLabel);

    detailsLayout->addWidget(mDetailsAuthorFrame);

    // Separator line 4
    QFrame *line4 = new QFrame(mCommitDetailsWidget);
    line4->setFrameShape(QFrame::HLine);
    line4->setFrameShadow(QFrame::Sunken);
    detailsLayout->addWidget(line4);

    // Changed Files Tree
    mChangedFilesTree = new QTreeWidget(mCommitDetailsWidget);
    mChangedFilesTree->setHeaderHidden(true);
    mChangedFilesTree->setStyleSheet("QTreeView { border: none; background: transparent; }");
    mChangedFilesTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mChangedFilesTree, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onChangedFilesContextMenu(QPoint)));
    connect(mChangedFilesTree, SIGNAL(itemDoubleClicked(QTreeWidgetItem*,int)), this, SLOT(onChangedFilesDoubleClicked(QTreeWidgetItem*,int)));
    detailsLayout->addWidget(mChangedFilesTree);

    ui->verticalLayout->addWidget(mCommitDetailsWidget);
    mCommitDetailsWidget->hide(); // Hide initially until a commit is selected!

    loadGroupMeta();

    /* Register for Git events using the dynamic GIT event type */
    if (rsEvents) {
    RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
    rsEvents->registerEventsHandler(
        [this](std::shared_ptr<const RsEvent> event) {
          RsQThreadUtils::postToObject(
              [=]() { handleEvent_main_thread(event); }, this);
        },
        mEventHandlerId, gitEventType);
    }
    
    mFontSizeHandler.registerFontSize(ui->treeWidget->treeWidget(), 1.2f);
}

MainWidget::~MainWidget()
{
    if (rsEvents)
        rsEvents->unregisterEventsHandler(mEventHandlerId);

    // save settings
    processSettings(false);

    delete ui;
}

UserNotify *MainWidget::createUserNotify(QObject *parent)
{
    return new GitUserNotify(this, parent);
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

  ui->treeWidget->processSettings(load);

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
    std::cerr << "MainWidget::loadGroupMeta()";
    std::cerr << std::endl;

  RsThread::async([this]() {
    // Fetch group metadata from backend
    std::list<RsGxsGroupId> groupIds; // empty list = get all groups
    std::vector<RsGitGroup> groups;

    if (!rsGit->getGroups(groupIds, groups)) {
      std::cerr << "MainWidget::loadGroupMeta() Error getting groups from GXS"
                << std::endl;
      return;
    }

    // Convert to RsGroupMetaData for display
    std::list<RsGroupMetaData> groupMeta;
    std::map<QString, int> groupUnreadCounts;
    for (auto &group : groups) {
      groupMeta.push_back(group.mMeta);
      
      int unreadCount = 0;
      std::vector<RsGitUpdate> updates;
      if (rsGit->getUpdates(group.mMeta.mGroupId, updates)) {
          for (const auto &update : updates) {
              if (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                  unreadCount++;
              }
          }
      }
      groupUnreadCounts[QString::fromStdString(group.mMeta.mGroupId.toStdString())] = unreadCount;
    }

    // Update UI in main thread
    RsQThreadUtils::postToObject(
        [this, groupMeta, groupUnreadCounts]() {
          if (groupMeta.size() > 0) {
            insertGroupsData(groupMeta);
            
            // Set unread count for each group item (this automatically sets it bold if unread > 0)
            for (const auto &meta : groupMeta) {
                QString groupIdStr = QString::fromStdString(meta.mGroupId.toStdString());
                QTreeWidgetItem *item = nullptr;
                QTreeWidgetItemIterator it(ui->treeWidget->treeWidget());
                while (*it) {
                    if (ui->treeWidget->itemId(*it) == groupIdStr) {
                        item = *it;
                        break;
                    }
                    ++it;
                }
                if (item) {
                    int unread = 0;
                    auto itUnread = groupUnreadCounts.find(groupIdStr);
                    if (itUnread != groupUnreadCounts.end()) {
                        unread = itUnread->second;
                    }
                    ui->treeWidget->setUnreadCount(item, unread);
                }
            }
          }
        },
        this);
  });
}

void MainWidget::insertGroupsData(const std::list<RsGroupMetaData> &gitList)
{
    std::cerr << "MainWidget::insertGroupsData()";
    std::cerr << std::endl;
    
  std::list<RsGroupMetaData>::const_iterator it;

  QList<GroupItemInfo> activeList;
  std::multimap<uint32_t, GroupItemInfo> popMap;

  for (it = gitList.begin(); it != gitList.end(); ++it) {
    uint32_t flags = it->mSubscribeFlags;

    GroupItemInfo groupItemInfo;
    GroupMetaDataToGroupItemInfo(*it, groupItemInfo);

    bool add = false;
    if (mGroupSet == 0)
      add = true; // All

    if (IS_GROUP_ADMIN(flags)) {
      if (mGroupSet == 1 || mGroupSet == 0)
        add = true;
    } else if (IS_GROUP_SUBSCRIBED(flags)) {
      if (mGroupSet == 2 || mGroupSet == 0)
        add = true;
    } else {
      popMap.insert(std::make_pair(it->mPop, groupItemInfo));
    }

    if (add && (IS_GROUP_ADMIN(flags) || IS_GROUP_SUBSCRIBED(flags))) {
      activeList.push_back(groupItemInfo);
    }
  }

  // Determine how many top-popularity groups count as "popular".
  // At least 5, or 10% of the pool — whichever is larger.
  uint32_t popCount = 5;
  if (popMap.size() / 10 > popCount)
    popCount = popMap.size() / 10;

  uint32_t i = 0;
  uint32_t popLimit = 0;
  bool allPopular = true; // true when popMap fits entirely in popCount
  std::multimap<uint32_t, GroupItemInfo>::reverse_iterator rit;
  for (rit = popMap.rbegin(); rit != popMap.rend() && i < popCount; ++rit, ++i)
    ;
  if (rit != popMap.rend()) {
    // There are more items beyond the popular window.
    popLimit = rit->first;
    allPopular = false;
  }

  for (rit = popMap.rbegin(); rit != popMap.rend(); ++rit) {
    // An item is "popular" if it is within the top-popCount window,
    // i.e. its popularity is >= popLimit (or every item is popular).
    bool isPopular = allPopular || (rit->second.popularity >= (int)popLimit);
    if (!isPopular) {
      if (mGroupSet == 4 || mGroupSet == 0) // Other Repositories
        activeList.append(rit->second);
    } else {
      if (mGroupSet == 3 || mGroupSet == 0) // Popular Repositories
        activeList.append(rit->second);
    }
  }

  ui->treeWidget->fillGroupItems(mActiveGroupsItem, activeList);
}

void MainWidget::selectGroupSet(int index)
{
  mGroupSet = index;
  updateDisplay();
}

void MainWidget::GroupMetaDataToGroupItemInfo(const RsGroupMetaData &groupInfo,GroupItemInfo &groupItemInfo)
{
  groupItemInfo.id = QString::fromStdString(groupInfo.mGroupId.toStdString());
  groupItemInfo.name = QString::fromUtf8(groupInfo.mGroupName.c_str());
  //groupItemInfo.description = QString(); // description not in RsGroupMetaData
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
    const RsGitEvent *e = dynamic_cast<const RsGitEvent *>(event.get());

    if (!e)
    return;

    switch (e->mGitEventCode) {
        case RsGitEventCode::NEW_GIT:
            updateDisplay(); // Refresh global list
            break;
        case RsGitEventCode::GIT_UPDATED:
            updateDisplay(); // Refresh global list
            break;
        case RsGitEventCode::SUBSCRIBE_STATUS_CHANGED:
            updateDisplay();
            onTreeSelectionChanged();
            break;
        case RsGitEventCode::NEW_POST:
        {
            loadGroupMeta();
            
            // Check if we are a subscriber and notify about new changes to sync
            QString updatedGroupId = QString::fromStdString(e->mGitGroupId.toStdString());
            bool isAdmin = false;
            std::list<RsGxsGroupId> groupIds({RsGxsGroupId(updatedGroupId.toStdString())});
            std::vector<RsGitGroup> groups;
            if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
                uint32_t flags = groups[0].mMeta.mSubscribeFlags;
                isAdmin = IS_GROUP_ADMIN(flags);
                
                if (!isAdmin) {
                    QString repoName = QString::fromUtf8(groups[0].mGroupName.c_str());
                    QMessageBox::information(this, tr("New Updates Available"),
                        tr("The repository '%1' has new commits published by the owner. Please pull/sync to update your local files.").arg(repoName));
                }
            }
            break;
        }
        case RsGitEventCode::READ_STATUS_CHANGED:
        {
            loadGroupMeta();
            refreshCurrentRepo();
            break;
        }

    default:
        break;
    }

    // Forward events to active sub-widgets
    if (mGitWidget) mGitWidget->handleGitEvent(e);
    if (mCodeWidget) mCodeWidget->handleGitEvent(e);
    if (mPushesWidget) mPushesWidget->handleGitEvent(e);

    for (int i = 3; i < ui->rightPaneTabWidget->count(); ++i) {
        PullRequestsWidget *prWidget = qobject_cast<PullRequestsWidget*>(ui->rightPaneTabWidget->widget(i));
        if (prWidget) {
            prWidget->refresh();
        }
    }
}

void MainWidget::groupListCustomPopupMenu(QPoint /*point*/)
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
        return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
        return;

    int subscribeFlags = item->data(GTW_COLUMN_DATA, Qt::UserRole + 3).toInt();

    QMenu contextMnu(this);
    QAction *action;

    if (!IS_GROUP_ADMIN(subscribeFlags)) {
        if (IS_GROUP_SUBSCRIBED(subscribeFlags)) {
            action = contextMnu.addAction(QIcon(), tr("Unsubscribe"), this, SLOT(unsubscribeFromGroup()));
            action->setEnabled(!groupId.isEmpty());
        } else {
            action = contextMnu.addAction(QIcon(), tr("Subscribe"), this, SLOT(subscribeToGroup()));
            action->setEnabled(!groupId.isEmpty());
        }
    }

    contextMnu.addSeparator();

    action = contextMnu.addAction(QIcon(":/images/info.png"), tr("Show Group Details"), this, SLOT(showGroupDetails()));
    action->setEnabled(!groupId.isEmpty());

    action = contextMnu.addAction(QIcon(":/images/edit.png"), tr("Edit Group Details"), this, SLOT(editGroupDetails()));
    action->setEnabled(!groupId.isEmpty() && IS_GROUP_ADMIN(subscribeFlags));

    if (IS_GROUP_SUBSCRIBED(subscribeFlags)) {
        int unreadCount = 0;
        std::vector<RsGitUpdate> updates;
        if (rsGit && rsGit->getUpdates(RsGxsGroupId(groupId.toStdString()), updates)) {
            for (const auto &update : updates) {
                if (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                    unreadCount++;
                }
            }
        }
        if (unreadCount > 0) {
            contextMnu.addSeparator();
            action = contextMnu.addAction(QIcon(), tr("Mark Repository as Read"), this, SLOT(markRepositoryAsRead()));
            action->setEnabled(true);
        }
    }

    contextMnu.exec(QCursor::pos());
}

void MainWidget::markRepositoryAsRead()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
        return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
        return;

    RsThread::async([this, groupId]() {
        if (rsGit) {
            std::vector<RsGitUpdate> updates;
            if (rsGit->getUpdates(RsGxsGroupId(groupId.toStdString()), updates)) {
                for (const auto &update : updates) {
                    if (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                        rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(groupId.toStdString()), update.mMeta.mMsgId), true);
                    }
                }
            }

            std::vector<RsGitPullRequest> pullRequests;
            if (rsGit->getPullRequests(RsGxsGroupId(groupId.toStdString()), pullRequests)) {
                for (const auto &pr : pullRequests) {
                    if (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                        rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(groupId.toStdString()), pr.mMeta.mMsgId), true);
                    }
                }
            }

            RsQThreadUtils::postToObject([this]() {
                refreshCurrentRepo();
                loadGroupMeta();
            }, this);
        }
    });
}

// onCommitReadStatusToggled is handled locally by GitWidget.

void MainWidget::showGroupDetails()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
        return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
        return;

    GitGroupDialog dialog(GxsGroupDialog::MODE_SHOW,RsGxsGroupId(groupId.toStdString()), this);
    dialog.exec();
}

void MainWidget::editGroupDetails()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
    return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
    return;

    GitGroupDialog dialog(GxsGroupDialog::MODE_EDIT,RsGxsGroupId(groupId.toStdString()), this);
    dialog.exec();
}

void MainWidget::subscribeToGroup()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
        return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
        return;

    if (rsGit) {
        if (rsGit->subscribe(RsGxsGroupId(groupId.toStdString()), true)) {
            updateDisplay();
            onTreeSelectionChanged();
        }
    }
}

void MainWidget::unsubscribeFromGroup()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget())
        return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item)
        return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty())
        return;

    if (rsGit) {
        if (rsGit->subscribe(RsGxsGroupId(groupId.toStdString()), false)) {
            updateDisplay();
            onTreeSelectionChanged();
        }
    }
}

void MainWidget::saveRepoLocalPath(const QString &groupId, const QString &path)
{
    Settings->beginGroup("RetroGit_WorkingDirs");
    Settings->setValue(groupId, path);
    Settings->endGroup();
}

QString MainWidget::loadRepoLocalPath(const QString &groupId)
{
    Settings->beginGroup("RetroGit_WorkingDirs");
    QString path = Settings->value(groupId).toString();
    Settings->endGroup();
    return path;
}

void MainWidget::onTreeSelectionChanged()
{
    if (mCommitDetailsWidget) {
        mCommitDetailsWidget->hide();
    }

    while (ui->rightPaneTabWidget->count() > 3) {
        QWidget *tab = ui->rightPaneTabWidget->widget(3);
        ui->rightPaneTabWidget->removeTab(3);
        delete tab;
    }

    QString groupId = getSelectedGroupId();
    if (mGitWidget) mGitWidget->setGroupId(groupId);
    if (mCodeWidget) mCodeWidget->setGroupId(groupId);
    if (mPushesWidget) mPushesWidget->setGroupId(groupId);
}

void MainWidget::triggerTreeSelectionChanged()
{
    onTreeSelectionChanged();
}

QString MainWidget::getSelectedGroupId() const
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return QString();
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return QString();
    return ui->treeWidget->itemId(item);
}

QString MainWidget::getLocalPath() const
{
    if (mGitWidget) {
        return mGitWidget->getLocalPath();
    }
    return QString();
}

void MainWidget::refreshGitWidget()
{
    if (mGitWidget) mGitWidget->refresh();
}

void MainWidget::refreshCodeWidget()
{
    if (mCodeWidget) mCodeWidget->refresh();
}

void MainWidget::hideCommitDetails()
{
    if (mCommitDetailsWidget) {
        mCommitDetailsWidget->hide();
    }
}

void MainWidget::logCloneAttempt(const QString &groupId, const QString &repoName, const QString &ownerId, const RsGxsId &ownId, const QString &localPath)
{
    PushesWidget::CloneRecord rec;
    rec.repoId = groupId;
    rec.repoName = repoName;
    rec.ownerId = ownerId;
    rec.status = ownId.isNull() ? tr("GXS Identity missing. Initiating offline clone...") : tr("Requesting secure tunnel...");
    rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    
    if (mPushesWidget) {
        mPushesWidget->addCloneRecord(rec);
        ui->rightPaneTabWidget->setCurrentWidget(mPushesWidget);
    }

    bool success = false;
    if (!ownId.isNull()) {
        success = rsGit->requestCloneOverTunnel(RsGxsGroupId(groupId.toStdString()), RsGxsId(ownerId.toStdString()), ownId, localPath.toStdString());
    } else {
        success = rsGit->requestOfflineClone(RsGxsGroupId(groupId.toStdString()), localPath.toStdString());
    }

    if (!success) {
        if (mPushesWidget && !mPushesWidget->getCloneHistory().empty()) {
            mPushesWidget->getCloneHistory()[0].status = ownId.isNull() ? tr("Failed to initiate offline clone.") : tr("Failed to initiate request.");
            mPushesWidget->updateClonesTable();
        }
        QMessageBox::critical(this, tr("Clone Failed"), tr("Could not initiate clone request. Please check connections or GXS services."));
    }
}

void MainWidget::logPullAttempt(const QString &groupId, const QString &repoName, const QString &ownerId, const RsGxsId &ownId, const QString &localPath)
{
    PushesWidget::CloneRecord rec;
    rec.repoId = groupId;
    rec.repoName = repoName;
    rec.ownerId = ownerId;
    rec.status = ownId.isNull() ? tr("GXS Identity missing. Initiating offline pull...") : tr("Requesting secure pull tunnel...");
    rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    if (mPushesWidget) {
        mPushesWidget->addCloneRecord(rec);
        ui->rightPaneTabWidget->setCurrentWidget(mPushesWidget);
    }

    bool success = false;
    if (!ownId.isNull()) {
        success = rsGit->requestPullOverTunnel(RsGxsGroupId(groupId.toStdString()), RsGxsId(ownerId.toStdString()), ownId, localPath.toStdString());
    } else {
        success = rsGit->requestOfflinePull(RsGxsGroupId(groupId.toStdString()), localPath.toStdString());
    }

    if (!success) {
        if (mPushesWidget && !mPushesWidget->getCloneHistory().empty()) {
            mPushesWidget->getCloneHistory()[0].status = ownId.isNull() ? tr("Failed to initiate offline pull.") : tr("Failed to initiate sync request.");
            mPushesWidget->updateClonesTable();
        }
        QMessageBox::critical(this, tr("Pull Failed"), ownId.isNull() ? tr("Failed to initiate offline pull.") : tr("Failed to initiate pull tunnel request."));
    }
}

void MainWidget::hashAndPublishPackfile(const QString &groupId, const std::string &packfileData, RsGitUpdate &update)
{
    QString tempDir = QDir::cleanPath(QString::fromStdString(RsAccounts::AccountDirectory()) + "/retrogit_temp/" + groupId);
    QDir().mkpath(tempDir);
    QString tempFilePath = tempDir + QString("/pack_%1.pack").arg(QDateTime::currentMSecsSinceEpoch());
    
    QFile file(tempFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Push Failed"), tr("Failed to write temporary packfile to disk."));
        return;
    }
    file.write(packfileData.data(), packfileData.size());
    file.close();
    
    if (rsFiles) {
        TransferRequestFlags flags = RS_FILE_REQ_ANONYMOUS_ROUTING;
        rsFiles->ExtraFileHash(tempFilePath.toStdString(), 3600 * 24 * 30, flags);
        
        FileInfo fi;
        int retries = 200;
        bool hashOk = false;
        
        QProgressDialog progress(tr("Hashing packfile..."), tr("Cancel"), 0, retries, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.show();
        
        for (int i = 0; i < retries; ++i) {
            progress.setValue(i);
            QCoreApplication::processEvents();
            if (progress.wasCanceled()) {
                break;
            }
            if (rsFiles->ExtraFileStatus(tempFilePath.toStdString(), fi)) {
                hashOk = true;
                break;
            }
            QThread::msleep(50);
        }
        progress.setValue(retries);
        
        if (!hashOk) {
            QMessageBox::critical(this, tr("Push Failed"), tr("Hashing the packfile timed out or was canceled. Please try again."));
            return;
        }
        
        RsGxsFile attachment;
        attachment.mName = fi.fname;
        attachment.mSize = fi.size;
        attachment.mHash = fi.hash;
        
        update.mFiles.push_back(attachment);
        
        uint32_t token;
        if (rsGit) {
            rsGit->publishGitUpdate(token, update);
            QMessageBox::information(this, tr("Push Successful"), 
                tr("Packfile of size %1 KB has been hashed and shared as a P2P attachment. Published metadata update successfully!").arg(packfileData.size() / 1024));
        }
    } else {
        QMessageBox::critical(this, tr("Push Failed"), tr("RetroShare file transfer service is not available."));
    }
}

void MainWidget::refreshCurrentRepo()
{
    if (mGitWidget) mGitWidget->refresh();
    if (mCodeWidget) mCodeWidget->refresh();
    if (mPushesWidget) mPushesWidget->refresh();
}

void MainWidget::showCommitDetails(const QString &fullHash, const QString &qRepoPath)
{
    mCurrentCommitHash = fullHash;
    std::string repoPath = qRepoPath.toStdString();
    if (fullHash == "LOCAL_CHANGES") {
        mDetailsAuthorNameLabel->setText("");
        mDetailsAuthorEmailLabel->hide();
        mDetailsHashLabel->setText(tr("Local"));
        mDetailsHashLabel->setToolTip(tr("Local changes"));
        mDetailsHashLabel->setAlignment(Qt::AlignCenter);
        mDetailsTitleLabel->setText(tr("Local changes (Uncommitted)"));
        mDetailsTitleLabel->setAlignment(Qt::AlignCenter);
        
        mDetailsBodyText->setText(tr("Uncommitted changes in the working directory."));
        mDetailsBodyText->setStyleSheet("color: #34495e; font-style: normal; font-size: 13px;");
        mDetailsBodyText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        
        QDateTime now = QDateTime::currentDateTime();
        mDetailsDateLabel->setText(now.toString("yyyy-MM-dd hh:mm"));
        
        mChangedFilesTree->clear();
        
        std::vector<GitLocalChange> changes;
        if (GitManager::getLocalChanges(repoPath, changes)) {
            for (const GitLocalChange& change : changes) {
                QString normalizedPath = QString::fromStdString(change.path).replace('\\', '/');
                QStringList pathParts = normalizedPath.split('/');
                QTreeWidgetItem *parentItem = nullptr;
                
                for (int i = 0; i < pathParts.size(); ++i) {
                    QString part = pathParts[i];
                    if (part.isEmpty()) continue;
                    
                    QTreeWidgetItem *childItem = nullptr;
                    int childCount = parentItem ? parentItem->childCount() : mChangedFilesTree->topLevelItemCount();
                    for (int j = 0; j < childCount; ++j) {
                        QTreeWidgetItem *item = parentItem ? parentItem->child(j) : mChangedFilesTree->topLevelItem(j);
                        if (item->data(0, Qt::UserRole + 1).toString() == part) {
                            childItem = item;
                            break;
                        }
                    }
                    
                    if (!childItem) {
                        childItem = new QTreeWidgetItem();
                        childItem->setData(0, Qt::UserRole + 1, part);
                        
                        if (i == pathParts.size() - 1) {
                            QString prefixedText = QString("%1 %2").arg(change.status).arg(part);
                            childItem->setText(0, prefixedText);
                            childItem->setForeground(0, QBrush(QColor(QString::fromStdString(change.color_hex))));
                            childItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
                            childItem->setData(0, Qt::UserRole, QString::fromStdString(change.path));
                        } else {
                            childItem->setText(0, part);
                            childItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                        }
                        
                        if (parentItem) {
                            parentItem->addChild(childItem);
                        } else {
                            mChangedFilesTree->addTopLevelItem(childItem);
                        }
                    }
                    parentItem = childItem;
                }
            }
            mChangedFilesTree->expandAll();
            mCommitDetailsWidget->show();
        } else {
            mCommitDetailsWidget->hide();
        }
    } else {
        std::string authorName, authorEmail, summary, body, date;
        std::vector<std::string> changedFiles;
        
        if (GitManager::getCommitDetails(repoPath, fullHash.toStdString(),
                                         authorName, authorEmail,
                                         summary, body,
                                         date, changedFiles)) {
            
            mDetailsAuthorNameLabel->setText(QString::fromStdString(authorName));
            if (authorEmail.empty()) {
                mDetailsAuthorEmailLabel->hide();
            } else {
                mDetailsAuthorEmailLabel->setText(QString::fromStdString(authorEmail));
                mDetailsAuthorEmailLabel->show();
            }
            mDetailsHashLabel->setText(fullHash.left(8));
            mDetailsHashLabel->setToolTip(fullHash);
            mDetailsHashLabel->setAlignment(Qt::AlignCenter);
            mDetailsTitleLabel->setText(QString::fromStdString(summary));
            mDetailsTitleLabel->setAlignment(Qt::AlignCenter);
            
            if (body.empty()) {
                mDetailsBodyText->setText(tr("<No description provided>"));
                mDetailsBodyText->setStyleSheet("color: #777777; font-style: italic; font-size: 13px;");
                mDetailsBodyText->setAlignment(Qt::AlignCenter);
            } else {
                mDetailsBodyText->setText(QString::fromStdString(body));
                mDetailsBodyText->setStyleSheet("color: #34495e; font-style: normal; font-size: 13px;");
                mDetailsBodyText->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            }
            
            mDetailsDateLabel->setText(QString::fromStdString(date));
            
            mChangedFilesTree->clear();
            
            for (const std::string& filePath : changedFiles) {
                QString normalizedPath = QString::fromStdString(filePath).replace('\\', '/');
                QStringList pathParts = normalizedPath.split('/');
                QTreeWidgetItem *parentItem = nullptr;
                
                for (int i = 0; i < pathParts.size(); ++i) {
                    QString part = pathParts[i];
                    if (part.isEmpty()) continue;
                    
                    QTreeWidgetItem *childItem = nullptr;
                    int childCount = parentItem ? parentItem->childCount() : mChangedFilesTree->topLevelItemCount();
                    for (int j = 0; j < childCount; ++j) {
                        QTreeWidgetItem *item = parentItem ? parentItem->child(j) : mChangedFilesTree->topLevelItem(j);
                        if (item->data(0, Qt::UserRole + 1).toString() == part) {
                            childItem = item;
                            break;
                        }
                    }
                    
                    if (!childItem) {
                        childItem = new QTreeWidgetItem();
                        childItem->setText(0, part);
                        childItem->setData(0, Qt::UserRole + 1, part);
                        
                        if (i == pathParts.size() - 1) {
                            childItem->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
                            childItem->setData(0, Qt::UserRole, QString::fromStdString(filePath));
                        } else {
                            childItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                        }
                        
                        if (parentItem) {
                            parentItem->addChild(childItem);
                        } else {
                            mChangedFilesTree->addTopLevelItem(childItem);
                        }
                    }
                    parentItem = childItem;
                }
            }
            
            mChangedFilesTree->expandAll();
            mCommitDetailsWidget->show();
        } else {
            mCommitDetailsWidget->hide();
        }
    }
}

void MainWidget::onChangedFilesContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = mChangedFilesTree->itemAt(pos);
    if (!item) return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return; // It's a folder or invalid

    QMenu menu(this);
    
    QAction *historyAction = menu.addAction(tr("History"));
    historyAction->setEnabled(false);
    
    QAction *blameAction = menu.addAction(tr("Blame"));
    blameAction->setEnabled(false);
    
    QAction *diffAction = menu.addAction(tr("Diff"));
    
    QAction *openFolderAction = menu.addAction(tr("Open containing folder"));
    openFolderAction->setEnabled(false);
    
    QAction *copyPathAction = menu.addAction(tr("Copy path"));

    QAction *selected = menu.exec(mChangedFilesTree->mapToGlobal(pos));
    if (selected == diffAction) {
        showDiffForFile(filePath);
    } else if (selected == copyPathAction) {
        QApplication::clipboard()->setText(filePath);
    }
}

void MainWidget::onChangedFilesDoubleClicked(QTreeWidgetItem *item, int column)
{
    (void)column;
    if (!item) return;
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        showDiffForFile(filePath);
    }
}

void MainWidget::onTabCloseRequested(int index)
{
    if (index < 3) return; // Prevent closing the core tabs ("Working Directory", "Repository Browser", "Pushes / Packs")
    
    QWidget *tab = ui->rightPaneTabWidget->widget(index);
    ui->rightPaneTabWidget->removeTab(index);
    delete tab;
}

void MainWidget::showDiffForFile(const QString &filePath)
{
    QTreeWidgetItem *repoItem = ui->treeWidget->treeWidget()->currentItem();
    if (!repoItem) return;
    
    QString groupId = ui->treeWidget->itemId(repoItem);
    if (groupId.isEmpty()) return;

    QString fullHash = mCurrentCommitHash;
    if (fullHash.isEmpty()) return;

    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    QString rawPath = getLocalPath();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }

    std::vector<GitDiffLine> diffLines;
    if (!GitManager::getFileDiff(repoPath, fullHash.toStdString(), filePath.toStdString(), diffLines)) {
        QMessageBox::warning(this, tr("Diff Error"), tr("Failed to retrieve diff for the selected file."));
        return;
    }

    // Now construct the HTML for the diff view
    QString html = "<html><head><style>";
    html += "body { background-color: #ffffff; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; font-size: 12px; margin: 10px; }";
    html += ".header-banner { background-color: #e0a941; font-weight: bold; font-size: 13px; color: #222222; padding: 6px 10px; text-align: center; margin-bottom: 10px; border-radius: 3px; border: 1px solid #c79230; }";
    html += ".file-header { font-weight: bold; background-color: #2c3e50; color: #ffffff; padding: 6px 10px; margin-top: 15px; margin-bottom: 0px; border-radius: 3px 3px 0 0; font-size: 12px; }";
    html += ".file-header-detail { background-color: #34495e; color: #cccccc; padding: 3px 10px; font-size: 11px; margin-bottom: 5px; }";
    html += ".hunk-header { font-weight: bold; color: #222222; font-size: 12px; margin-top: 10px; padding: 6px 10px; background-color: #bebebe; border: 1px solid #a8a8a8; border-bottom: none; border-radius: 3px 3px 0 0; }";
    html += ".hunk-card { background-color: #ffffff; border: 1px solid #a8a8a8; border-radius: 0 0 3px 3px; padding: 5px; margin-bottom: 15px; }";
    html += ".line { white-space: pre-wrap; padding: 1px 4px; margin: 0; line-height: 1.35; font-size: 12px; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; }";
    html += ".line-add { background-color: #c6efce; color: #006100; }";
    html += ".line-del { background-color: #ffc7ce; color: #9c0006; }";
    html += ".line-ctx { color: #111111; }";
    html += "</style></head><body>";

    // Path Banner
    html += "<div class='header-banner'>" + filePath.toHtmlEscaped() + "</div>";

    bool inHunk = false;
    for (const GitDiffLine& line : diffLines) {
        QString lineText = QString::fromStdString(line.text);
        while (lineText.endsWith('\n') || lineText.endsWith('\r')) {
            lineText.chop(1);
        }
        QString escapedText = lineText.toHtmlEscaped();

        if (line.origin == 'F') {
            if (inHunk) {
                html += "</div>"; // Close previous hunk card
                inHunk = false;
            }
            if (lineText.startsWith("diff --git")) {
                html += "<div class='file-header'>" + escapedText + "</div>";
            } else {
                html += "<div class='file-header-detail'>" + escapedText + "</div>";
            }
        } else if (line.origin == 'H') {
            if (inHunk) {
                html += "</div>"; // Close previous hunk card
            }
            html += "<div class='hunk-header'>" + escapedText + "</div>";
            html += "<div class='hunk-card'>";
            inHunk = true;
        } else {
            if (!inHunk) {
                // If there's content before any hunk header, open a default hunk card
                html += "<div class='hunk-card'>";
                inHunk = true;
            }

            if (line.origin == '+') {
                html += "<div class='line line-add'>+ " + escapedText + "</div>";
            } else if (line.origin == '-') {
                html += "<div class='line line-del'>- " + escapedText + "</div>";
            } else if (line.origin == ' ') {
                html += "<div class='line line-ctx'>  " + escapedText + "</div>";
            } else {
                html += "<div class='line line-ctx'>" + escapedText + "</div>";
            }
        }
    }
    if (inHunk) {
        html += "</div>"; // Close final hunk card
    }

    html += "</body></html>";

    // Open/find tab
    // Check if a tab for this file is already open
    QString tabTitle = QFileInfo(filePath).fileName();
    int tabIndex = -1;
    for (int i = 2; i < ui->rightPaneTabWidget->count(); ++i) {
        if (ui->rightPaneTabWidget->tabText(i) == tabTitle) {
            tabIndex = i;
            break;
        }
    }

    if (tabIndex != -1) {
        // Tab exists, update content and select it
        QWidget *existingTab = ui->rightPaneTabWidget->widget(tabIndex);
        QTextBrowser *browser = existingTab->findChild<QTextBrowser*>();
        if (browser) {
            browser->setStyleSheet("QTextBrowser { border: none; background-color: #ffffff; }");
            browser->setHtml(html);
        }
        ui->rightPaneTabWidget->setCurrentIndex(tabIndex);
    } else {
        // Create new tab
        QWidget *diffTab = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(diffTab);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        QTextBrowser *diffBrowser = new QTextBrowser(diffTab);
        diffBrowser->setReadOnly(true);
        diffBrowser->setLineWrapMode(QTextEdit::NoWrap);
        diffBrowser->setStyleSheet("QTextBrowser { border: none; background-color: #ffffff; }");
        diffBrowser->setHtml(html);
        layout->addWidget(diffBrowser);

        int newIndex = ui->rightPaneTabWidget->addTab(diffTab, QIcon(":/images/git.png"), tabTitle);
        ui->rightPaneTabWidget->setCurrentIndex(newIndex);
    }

    // Hide close buttons for the first three tabs (indices 0, 1, and 2)
    QTabBar *bar = ui->rightPaneTabWidget->findChild<QTabBar*>();
    if (bar) {
        bar->setTabButton(0, QTabBar::RightSide, nullptr);
        bar->setTabButton(1, QTabBar::RightSide, nullptr);
        bar->setTabButton(2, QTabBar::RightSide, nullptr);
    }
}

// onCommitTableContextMenu is handled by GitWidget.

void MainWidget::showDiffForCommit(const QString &commitHash)
{
    QTreeWidgetItem *repoItem = ui->treeWidget->treeWidget()->currentItem();
    if (!repoItem) return;
    
    QString groupId = ui->treeWidget->itemId(repoItem);
    if (groupId.isEmpty()) return;

    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    QString rawPath = getLocalPath();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }

    std::vector<GitDiffLine> diffLines;
    if (!GitManager::getFileDiff(repoPath, commitHash.toStdString(), "", diffLines)) {
        QMessageBox::warning(this, tr("Diff Error"), tr("Failed to retrieve diff for the selected commit."));
        return;
    }

    // Construct the HTML for the diff view
    QString html = "<html><head><style>";
    html += "body { background-color: #ffffff; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; font-size: 12px; margin: 10px; }";
    html += ".header-banner { background-color: #e0a941; font-weight: bold; font-size: 13px; color: #222222; padding: 6px 10px; text-align: center; margin-bottom: 10px; border-radius: 3px; border: 1px solid #c79230; }";
    html += ".file-header { font-weight: bold; background-color: #2c3e50; color: #ffffff; padding: 6px 10px; margin-top: 15px; margin-bottom: 0px; border-radius: 3px 3px 0 0; font-size: 12px; }";
    html += ".file-header-detail { background-color: #34495e; color: #cccccc; padding: 3px 10px; font-size: 11px; margin-bottom: 5px; }";
    html += ".hunk-header { font-weight: bold; color: #222222; font-size: 12px; margin-top: 10px; padding: 6px 10px; background-color: #bebebe; border: 1px solid #a8a8a8; border-bottom: none; border-radius: 3px 3px 0 0; }";
    html += ".hunk-card { background-color: #ffffff; border: 1px solid #a8a8a8; border-radius: 0 0 3px 3px; padding: 5px; margin-bottom: 15px; }";
    html += ".line { white-space: pre-wrap; padding: 1px 4px; margin: 0; line-height: 1.35; font-size: 12px; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; }";
    html += ".line-add { background-color: #c6efce; color: #006100; }";
    html += ".line-del { background-color: #ffc7ce; color: #9c0006; }";
    html += ".line-ctx { color: #111111; }";
    html += "</style></head><body>";

    // Path Banner
    QString bannerText;
    if (commitHash == "LOCAL_CHANGES") {
        bannerText = tr("Local Changes Diff");
    } else {
        bannerText = tr("Commit Diff: ") + commitHash.left(8);
    }
    html += "<div class='header-banner'>" + bannerText + "</div>";

    bool inHunk = false;
    for (const GitDiffLine& line : diffLines) {
        QString lineText = QString::fromStdString(line.text);
        while (lineText.endsWith('\n') || lineText.endsWith('\r')) {
            lineText.chop(1);
        }
        QString escapedText = lineText.toHtmlEscaped();

        if (line.origin == 'F') {
            if (inHunk) {
                html += "</div>"; // Close previous hunk card
                inHunk = false;
            }
            if (lineText.startsWith("diff --git")) {
                html += "<div class='file-header'>" + escapedText + "</div>";
            } else {
                html += "<div class='file-header-detail'>" + escapedText + "</div>";
            }
        } else if (line.origin == 'H') {
            if (inHunk) {
                html += "</div>"; // Close previous hunk card
            }
            html += "<div class='hunk-header'>" + escapedText + "</div>";
            html += "<div class='hunk-card'>";
            inHunk = true;
        } else {
            if (!inHunk) {
                html += "<div class='hunk-card'>";
                inHunk = true;
            }

            if (line.origin == '+') {
                html += "<div class='line line-add'>+ " + escapedText + "</div>";
            } else if (line.origin == '-') {
                html += "<div class='line line-del'>- " + escapedText + "</div>";
            } else if (line.origin == ' ') {
                html += "<div class='line line-ctx'>  " + escapedText + "</div>";
            } else {
                html += "<div class='line line-ctx'>" + escapedText + "</div>";
            }
        }
    }
    if (inHunk) {
        html += "</div>"; // Close final hunk card
    }

    html += "</body></html>";

    QString tabTitle;
    if (commitHash == "LOCAL_CHANGES") {
        tabTitle = tr("Diff: Local Changes");
    } else {
        tabTitle = tr("Diff: ") + commitHash.left(8);
    }

    int tabIndex = -1;
    for (int i = 2; i < ui->rightPaneTabWidget->count(); ++i) {
        if (ui->rightPaneTabWidget->tabText(i) == tabTitle) {
            tabIndex = i;
            break;
        }
    }

    if (tabIndex != -1) {
        QWidget *existingTab = ui->rightPaneTabWidget->widget(tabIndex);
        QTextBrowser *browser = existingTab->findChild<QTextBrowser*>();
        if (browser) {
            browser->setStyleSheet("QTextBrowser { border: none; background-color: #ffffff; }");
            browser->setHtml(html);
        }
        ui->rightPaneTabWidget->setCurrentIndex(tabIndex);
    } else {
        QWidget *diffTab = new QWidget();
        QVBoxLayout *layout = new QVBoxLayout(diffTab);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        QTextBrowser *diffBrowser = new QTextBrowser(diffTab);
        diffBrowser->setReadOnly(true);
        diffBrowser->setLineWrapMode(QTextEdit::NoWrap);
        diffBrowser->setStyleSheet("QTextBrowser { border: none; background-color: #ffffff; }");
        diffBrowser->setHtml(html);
        layout->addWidget(diffBrowser);

        int newIndex = ui->rightPaneTabWidget->addTab(diffTab, QIcon(":/images/git.png"), tabTitle);
        ui->rightPaneTabWidget->setCurrentIndex(newIndex);
    }

    QTabBar *bar = ui->rightPaneTabWidget->findChild<QTabBar*>();
    if (bar) {
        bar->setTabButton(0, QTabBar::RightSide, nullptr);
        bar->setTabButton(1, QTabBar::RightSide, nullptr);
        bar->setTabButton(2, QTabBar::RightSide, nullptr);
    }
}




GxsIdTableItem::GxsIdTableItem(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);
    
    mIconLabel = new QLabel(this);
    mTextLabel = new QLabel(this);
    
    layout->addWidget(mIconLabel);
    layout->addWidget(mTextLabel);
    layout->addStretch();
}

void GxsIdTableItem::setId(const RsGxsId &id)
{
    mId = id;
    if (id.isNull()) {
        mTextLabel->setText(tr("Anonymous"));
        mIconLabel->setPixmap(QPixmap());
    } else {
        GxsIdDetails::process(mId, fillCallback, this);
    }
}

bool GxsIdTableItem::getId(RsGxsId &id) const
{
    id = mId;
    return true;
}

void GxsIdTableItem::fillCallback(GxsIdDetailsType type, const RsIdentityDetails &details, QObject *object, const QVariant &)
{
    GxsIdTableItem *item = dynamic_cast<GxsIdTableItem*>(object);
    if (!item) return;
    
    item->mTextLabel->setText(GxsIdDetails::getNameForType(type, details));
    
    QList<QIcon> icons;
    if (type == GXS_ID_DETAILS_TYPE_DONE) {
        GxsIdDetails::getIcons(details, icons, GxsIdDetails::ICON_TYPE_AVATAR);
    }
    
    QPixmap combinedPixmap;
    if (!icons.empty()) {
        int iconSize = static_cast<int>(item->fontMetrics().height() * 1.7);
        GxsIdDetails::GenerateCombinedPixmap(combinedPixmap, icons, iconSize);
    }
    item->mIconLabel->setPixmap(combinedPixmap);
    
    QString t = GxsIdDetails::getComment(details);
    
    if (rsReputations) {
        RsReputationInfo repInfo;
        if (rsReputations->getReputationInfo(details.mId, details.mPgpId, repInfo)) {
            int idx = t.indexOf("<br/>Votes:");
            if (idx != -1) {
                t = t.left(idx);
            }
            t += QString("<br/>%1: <b>+%2</b> <b>-%3</b>")
                 .arg(tr("Votes"))
                 .arg(repInfo.mFriendsPositiveVotes)
                 .arg(repInfo.mFriendsNegativeVotes);
        }
    }
    
    QPixmap pix;
    if (details.mAvatar.mSize == 0 || !GxsIdDetails::loadPixmapFromData(details.mAvatar.mData, details.mAvatar.mSize, pix, GxsIdDetails::LARGE)) {
        pix = GxsIdDetails::makeDefaultIcon(details.mId, GxsIdDetails::LARGE);
    }
    
    int S = item->fontMetrics().height();
    QString embeddedImage;
    if (RsHtml::makeEmbeddedImage(pix.scaled(QSize(5*S, 5*S), Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage(), embeddedImage, -1)) {
        embeddedImage.insert(embeddedImage.indexOf("src="), "style=\"float:left\" ");
        t = "<table><tr><td>" + embeddedImage + "</td><td>" + t + "</td></tr></table>";
    }
    item->setToolTip(t);
}

void MainWidget::showPullRequests(const QString &groupId)
{
    QString tabTitle = tr("Pull Requests");
    int tabIndex = -1;
    for (int i = 3; i < ui->rightPaneTabWidget->count(); ++i) {
        PullRequestsWidget *prWidget = qobject_cast<PullRequestsWidget*>(ui->rightPaneTabWidget->widget(i));
        if (prWidget) {
            tabIndex = i;
            break;
        }
    }

    if (tabIndex != -1) {
        ui->rightPaneTabWidget->setCurrentIndex(tabIndex);
    } else {
        PullRequestsWidget *prWidget = new PullRequestsWidget(groupId, this, ui->rightPaneTabWidget);
        int newIndex = ui->rightPaneTabWidget->addTab(prWidget, QIcon(":/images/git-pull-request.png"), tabTitle);
        ui->rightPaneTabWidget->setCurrentIndex(newIndex);
    }

    // Hide close buttons for the first three tabs
    QTabBar *bar = ui->rightPaneTabWidget->findChild<QTabBar*>();
    if (bar) {
        bar->setTabButton(0, QTabBar::RightSide, nullptr);
        bar->setTabButton(1, QTabBar::RightSide, nullptr);
        bar->setTabButton(2, QTabBar::RightSide, nullptr);
    }
}






