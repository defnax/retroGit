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
#include "GitGroupDialog.h"
#include "ui_MainWidget.h"

#include "gui/gxs/GxsIdDetails.h"
#include "gui/settings/rsharesettings.h"
#include "interface/rsGit.h"
#include "retroshare/rsgxsflags.h"
#include "retroshare/rsservicecontrol.h"
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
    mActiveGroupsItem =
      ui->treeWidget->addCategoryItem(tr("Repositories"), QIcon(), true);

    /* Add the New Group button */
    QToolButton *newGroupButton = new QToolButton(this);
    newGroupButton->setIcon(QIcon(":/icons/png/add.png"));
    newGroupButton->setToolTip(tr("Create Group"));
    ui->treeWidget->addToolButton(newGroupButton);
    
    // Set up Right Pane (Tabs)
    QVBoxLayout *workDirLayout = new QVBoxLayout(ui->tabWidgetPage1);
    
    QLabel *lblInfo = new QLabel(tr("<b>For Repo Owners:</b> Browse to your existing local project folder and <b>Push / Publish</b>.<br/>"
                                    "<b>For Subscribers:</b> Browse to an empty folder and <b>Clone</b> to download the code."), ui->tabWidgetPage1);
    lblInfo->setWordWrap(true);
    workDirLayout->addWidget(lblInfo);

    QHBoxLayout *pathLayout = new QHBoxLayout();
    mLocalPathEdit = new QLineEdit(ui->tabWidgetPage1);
    mLocalPathEdit->setPlaceholderText(tr("Local Working Directory..."));
    QPushButton *btnBrowse = new QPushButton(tr("Browse..."), ui->tabWidgetPage1);
    mBtnOpenFolder = new QPushButton(tr("Open Folder"), ui->tabWidgetPage1);
    
    pathLayout->addWidget(mLocalPathEdit);
    pathLayout->addWidget(btnBrowse);
    pathLayout->addWidget(mBtnOpenFolder);
    workDirLayout->addLayout(pathLayout);

    QHBoxLayout *actionLayout = new QHBoxLayout();
    mBtnClone = new QPushButton(tr("Clone Repository"), ui->tabWidgetPage1);
    mBtnCommit = new QPushButton(tr("Commit Changes"), ui->tabWidgetPage1);
    mBtnPush = new QPushButton(tr("Push / Publish"), ui->tabWidgetPage1);
    mBtnPull = new QPushButton(tr("Sync / Pull"), ui->tabWidgetPage1);
    
    actionLayout->addWidget(mBtnClone);
    actionLayout->addWidget(mBtnCommit);
    actionLayout->addWidget(mBtnPush);
    actionLayout->addWidget(mBtnPull);
    workDirLayout->addLayout(actionLayout);
    
    // Commit Log Table
    QLabel *lblLog = new QLabel(tr("Recent Commits:"), ui->tabWidgetPage1);
    workDirLayout->addWidget(lblLog);
    
    mCommitTable = new QTableWidget(0, 6, ui->tabWidgetPage1);
    mCommitTable->setHorizontalHeaderLabels(QStringList() << tr("Hash") << tr("Message") << tr("Author") << tr("Date") << tr("Status") << tr("Action"));
    mCommitTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mCommitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mCommitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mCommitTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mCommitTable, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onCommitTableContextMenu(QPoint)));
    workDirLayout->addWidget(mCommitTable);
    
    mBtnPush->setEnabled(false);
    mBtnPull->setEnabled(false);
    mBtnClone->setEnabled(false);
    mBtnOpenFolder->setEnabled(false);
    mBtnCommit->setEnabled(false);
    
    // Repository Browser Tab
    QWidget *repoBrowserTab = new QWidget();
    QVBoxLayout *repoBrowserLayout = new QVBoxLayout(repoBrowserTab);
    
    QLabel *lblBrowser = new QLabel(tr("Files in the HEAD commit of the bare repository:"), repoBrowserTab);
    repoBrowserLayout->addWidget(lblBrowser);
    
    mRepoBrowserList = new QListWidget(repoBrowserTab);
    mRepoBrowserList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mRepoBrowserList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(onRepoBrowserContextMenu(QPoint)));
    connect(mRepoBrowserList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(openSelectedFile()));
    repoBrowserLayout->addWidget(mRepoBrowserList);
    
    ui->rightPaneTabWidget->addTab(repoBrowserTab, QIcon(":/images/git.png"), tr("Files"));
    
    // Packfiles / Available Pushes Tab
    mPackfilesTab = new QWidget();
    QVBoxLayout *packfilesLayout = new QVBoxLayout(mPackfilesTab);
    
    QLabel *lblPackfiles = new QLabel(tr("Available Pushes to download:"), mPackfilesTab);
    packfilesLayout->addWidget(lblPackfiles);
    
    mPackfilesTable = new QTableWidget(0, 6, mPackfilesTab);
    mPackfilesTable->setHorizontalHeaderLabels(QStringList() << tr("Author") << tr("Date") << tr("Refs Updated") << tr("Size") << tr("Status / Progress") << tr("Action"));
    mPackfilesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    mPackfilesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mPackfilesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    packfilesLayout->addWidget(mPackfilesTable);

    // Separator line
    QFrame *lineSeparator = new QFrame(mPackfilesTab);
    lineSeparator->setFrameShape(QFrame::HLine);
    lineSeparator->setFrameShadow(QFrame::Sunken);
    packfilesLayout->addWidget(lineSeparator);

    // Direct Clones section
    QLabel *lblClones = new QLabel(tr("Decentralized Clones (GxsTunnels):"), mPackfilesTab);
    packfilesLayout->addWidget(lblClones);

    mClonesTable = new QTableWidget(0, 4, mPackfilesTab);
    mClonesTable->setHorizontalHeaderLabels(QStringList() << tr("Repository") << tr("Author") << tr("Status / Progress") << tr("Date"));
    mClonesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mClonesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mClonesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mClonesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mClonesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    packfilesLayout->addWidget(mClonesTable);
    
    ui->rightPaneTabWidget->addTab(mPackfilesTab, QIcon(":/images/git.png"), tr("Pushes / Packs"));
    
    ui->rightPaneTabWidget->setTabsClosable(true);
    connect(ui->rightPaneTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    mDownloadPollTimer = new QTimer(this);
    connect(mDownloadPollTimer, SIGNAL(timeout()), this, SLOT(pollDownloadProgress()));

    processSettings(true);

    connect(newGroupButton, SIGNAL(clicked()), this, SLOT(createGroup()));
    connect(btnBrowse, SIGNAL(clicked()), this, SLOT(onBrowseClicked()));
    connect(mBtnOpenFolder, SIGNAL(clicked()), this, SLOT(onOpenFolderClicked()));
    connect(mBtnClone, SIGNAL(clicked()), this, SLOT(onCloneClicked()));
    connect(mBtnPush, SIGNAL(clicked()), this, SLOT(onPushClicked()));
    connect(mBtnPull, SIGNAL(clicked()), this, SLOT(onPullClicked()));
    connect(mBtnCommit, SIGNAL(clicked()), this, SLOT(onCommitClicked()));
    
    connect(mLocalPathEdit, SIGNAL(textChanged(QString)), this, SLOT(onLocalPathChanged(QString)));
    
    connect(ui->treeWidget->treeWidget(), SIGNAL(itemSelectionChanged()), this, SLOT(onTreeSelectionChanged()));
    connect(mCommitTable, SIGNAL(itemSelectionChanged()), this, SLOT(onCommitSelectionChanged()));

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

    /* Register for Git events using the static RsEventType::GIT */
    if (rsEvents) {
    rsEvents->registerEventsHandler(
        [this](std::shared_ptr<const RsEvent> event) {
          RsQThreadUtils::postToObject(
              [=]() { handleEvent_main_thread(event); }, this);
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

    // Load clone history
    int count = Settings->beginReadArray("CloneHistory");
    mCloneHistory.clear();
    for (int i = 0; i < count; ++i) {
        Settings->setArrayIndex(i);
        CloneRecord rec;
        rec.repoId = Settings->value("repoId").toString();
        rec.repoName = Settings->value("repoName").toString();
        rec.ownerId = Settings->value("ownerId").toString();
        rec.status = Settings->value("status").toString();
        rec.time = Settings->value("time").toString();
        mCloneHistory.push_back(rec);
    }
    Settings->endArray();
    populateClonesTable();

  } else {
    // save settings

    // state of splitter
    Settings->setValue("SplitterList", ui->splitter->saveState());

    // Save clone history
    Settings->beginWriteArray("CloneHistory");
    for (int i = 0; i < (int)mCloneHistory.size(); ++i) {
        Settings->setArrayIndex(i);
        Settings->setValue("repoId", mCloneHistory[i].repoId);
        Settings->setValue("repoName", mCloneHistory[i].repoName);
        Settings->setValue("ownerId", mCloneHistory[i].ownerId);
        Settings->setValue("status", mCloneHistory[i].status);
        Settings->setValue("time", mCloneHistory[i].time);
    }
    Settings->endArray();
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
                QTreeWidgetItem *item = ui->treeWidget->getItemFromId(groupIdStr);
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
            break;
        case RsGitEventCode::CLONE_STATUS_CHANGED:
        {
            QString status = QString::fromStdString(e->mCloneStatus);
            QString targetGroupId = QString::fromStdString(e->mGitGroupId.toStdString());
            
            // Find active clone in history and update it
            for (int i = 0; i < (int)mCloneHistory.size(); ++i) {
                if (mCloneHistory[i].repoId == targetGroupId && 
                    !mCloneHistory[i].status.contains("Successful") && 
                    !mCloneHistory[i].status.contains("Failed") && 
                    !mCloneHistory[i].status.contains("completed")) 
                {
                    mCloneHistory[i].status = status;
                    break;
                }
            }
            populateClonesTable();

            if (e->mCloneSuccess) {
                QMessageBox::information(this, tr("Clone Successful"), tr("Successfully cloned decentral repository."));
                updateDisplay();
                onTreeSelectionChanged();
            } else if (!status.isEmpty() && (status.contains("Failed") || status.contains("failed") || status.contains("down") || status.contains("not available"))) {
                QMessageBox::critical(this, tr("Clone Failed"), status);
            }
            break;
        }
        case RsGitEventCode::NEW_POST:
        {
            loadGroupMeta();
            
            // A packfile was received and unpacked into the bare repo — refresh browser/log
            QString updatedGroupId = QString::fromStdString(e->mGitGroupId.toStdString());
            
            // Check if we are a subscriber and notify about new changes to sync
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

            QTreeWidgetItem *selectedItem = ui->treeWidget->treeWidget()->currentItem();
            if (selectedItem) {
                QString currentGroupId = ui->treeWidget->itemId(selectedItem);
                if (currentGroupId == updatedGroupId) {
                    populateCommitLog(updatedGroupId);
                    populateRepoBrowser(updatedGroupId);
                    // Update button label now that repo has content
                    if (mCommitTable->rowCount() > 0)
                        mBtnPush->setText(tr("Push Local Commits"));
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

    int subscribeFlags = ui->treeWidget->subscribeFlags(groupId);

    QMenu contextMnu(this);
    QAction *action;

    action = contextMnu.addAction(QIcon(":/images/info.png"), tr("Show Group Details"), this, SLOT(showGroupDetails()));
    action->setEnabled(!groupId.isEmpty());

    action = contextMnu.addAction(QIcon(":/images/edit.png"), tr("Edit Group Details"), this, SLOT(editGroupDetails()));
    action->setEnabled(!groupId.isEmpty() && IS_GROUP_ADMIN(subscribeFlags));

    contextMnu.addSeparator();

    if (!IS_GROUP_ADMIN(subscribeFlags)) {
        if (IS_GROUP_SUBSCRIBED(subscribeFlags)) {
            action = contextMnu.addAction(QIcon(), tr("Unsubscribe"), this, SLOT(unsubscribeFromGroup()));
            action->setEnabled(!groupId.isEmpty());
        } else {
            action = contextMnu.addAction(QIcon(), tr("Subscribe"), this, SLOT(subscribeToGroup()));
            action->setEnabled(!groupId.isEmpty());
        }
    }

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
        std::vector<RsGitUpdate> updates;
        if (rsGit && rsGit->getUpdates(RsGxsGroupId(groupId.toStdString()), updates)) {
            for (const auto &update : updates) {
                if (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                    rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(groupId.toStdString()), update.mMeta.mMsgId), true);
                }
            }
            RsQThreadUtils::postToObject([this]() {
                refreshCurrentRepo();
                loadGroupMeta();
            }, this);
        }
    });
}

void MainWidget::onCommitReadStatusToggled(const QString &msgIdStr, bool markRead)
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    RsThread::async([this, groupId, msgIdStr, markRead]() {
        if (rsGit) {
            rsGit->setMessageReadStatus(
                RsGxsGrpMsgIdPair(RsGxsGroupId(groupId.toStdString()), RsGxsMessageId(msgIdStr.toStdString())),
                markRead
            );
            RsQThreadUtils::postToObject([this]() {
                refreshCurrentRepo();
                loadGroupMeta();
            }, this);
        }
    });
}

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

    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item || ui->treeWidget->itemId(item).isEmpty()) {
        mBtnPush->setEnabled(false);
        mBtnPull->setEnabled(false);
        mBtnClone->setEnabled(false);
        mLocalPathEdit->clear();
        mPackfilesTable->setRowCount(0);
        mAvailableUpdates.clear();
        if (mDownloadPollTimer) {
            mDownloadPollTimer->stop();
        }
        return;
    }
    
    // Disable text change signal temporarily to prevent overwriting settings with an empty string during transition
    mLocalPathEdit->blockSignals(true);
    QString groupId = ui->treeWidget->itemId(item);
    QString path = loadRepoLocalPath(groupId);
    mLocalPathEdit->setText(path);
    mLocalPathEdit->blockSignals(false);
    
    bool hasPath = !path.isEmpty();
    bool pathExists = false;
    bool isCloned = false;
    bool isAdmin = false;
    
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
    }

    if (hasPath) {
        QString cleanPath = QDir::cleanPath(path);
        pathExists = QDir(cleanPath).exists();
        if (pathExists && GitManager::isValidRepository(cleanPath.toStdString())) {
            isCloned = true;
        }
    }
    
    mBtnPush->setEnabled(isAdmin);
    mBtnPull->setEnabled(true);
    mBtnClone->setEnabled(!isAdmin && !isCloned);
    mBtnOpenFolder->setEnabled(hasPath && pathExists);
    mBtnCommit->setEnabled(hasPath && pathExists && isAdmin);
    
    populateCommitLog(groupId);
    populateRepoBrowser(groupId);
    populatePackfiles(groupId);
    
    if (mCommitTable->rowCount() == 0) {
        mBtnPush->setText(tr("Push & Publish"));
    } else {
        mBtnPush->setText(tr("Push Local Commits"));
    }
}

void MainWidget::populateCommitLog(const QString &groupId)
{
    mCommitTable->setRowCount(0);
    
    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    
    QString rawPath = mLocalPathEdit->text().trimmed();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }
    
    std::vector<GitCommitInfo> commits;
    bool gotLog = GitManager::getCommitLog(repoPath, commits);
    
    std::vector<GitLocalChange> localChanges;
    bool hasLocalChanges = false;
    bool statusOk = GitManager::getLocalChanges(repoPath, localChanges);
    if (statusOk && !localChanges.empty()) {
        hasLocalChanges = true;
    }
    
    // Load GXS updates to map commits and find undownloaded commits
    std::set<std::string> localCommitShas;
    for (const auto &c : commits) {
        localCommitShas.insert(c.hash);
        localCommitShas.insert(c.full_hash);
    }
    
    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(groupId.toStdString()), updates);
    }
    
    std::vector<RsGitUpdate> undownloadedUpdates;
    std::set<std::string> unreadCommitShas;
    std::map<std::string, std::pair<RsGxsMessageId, bool>> commitToMsgId;
    std::map<std::string, std::string> commitToMsgName;
    
    for (const auto &update : updates) {
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);
        
        // Check if this update has been downloaded
        bool isDownloaded = false;
        for (const auto &pair : update.mRefUpdates) {
            commitToMsgId[pair.second] = {update.mMeta.mMsgId, isUnread};
            commitToMsgName[pair.second] = update.mMeta.mMsgName;
            if (isUnread) {
                unreadCommitShas.insert(pair.second);
            }
            if (localCommitShas.count(pair.second)) {
                isDownloaded = true;
            }
        }
        
        if (!isDownloaded) {
            undownloadedUpdates.push_back(update);
        }
    }
    
    int offset = hasLocalChanges ? 1 : 0;
    int totalRows = undownloadedUpdates.size() + commits.size() + offset;
    mCommitTable->setRowCount(totalRows);
    
    if (hasLocalChanges) {
        QTableWidgetItem *itemHash = new QTableWidgetItem("*");
        itemHash->setData(Qt::UserRole, QString("LOCAL_CHANGES"));
        QFont font = itemHash->font();
        font.setBold(true);
        itemHash->setFont(font);
        
        QTableWidgetItem *itemMsg = new QTableWidgetItem(tr("Local changes (Uncommitted)"));
        itemMsg->setFont(font);
        itemMsg->setForeground(QBrush(QColor("#d35400"))); // Orange to highlight local changes
        
        QTableWidgetItem *itemAuth = new QTableWidgetItem("");
        QTableWidgetItem *itemDate = new QTableWidgetItem("");
        QTableWidgetItem *itemStatus = new QTableWidgetItem("-");
        QTableWidgetItem *itemAction = new QTableWidgetItem("-");
        
        mCommitTable->setItem(0, 0, itemHash);
        mCommitTable->setItem(0, 1, itemMsg);
        mCommitTable->setItem(0, 2, itemAuth);
        mCommitTable->setItem(0, 3, itemDate);
        mCommitTable->setItem(0, 4, itemStatus);
        mCommitTable->setItem(0, 5, itemAction);
    }
    
    // Get creator and own identity for Pull button
    RsGxsId creatorId;
    QString repoName;
    bool isAdmin = false;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
        creatorId = groups[0].mMeta.mAuthorId;
        repoName = QString::fromUtf8(groups[0].mGroupName.c_str());
    }
    
    RsGxsId ownId;
    if (rsIdentity) {
        std::list<RsGxsId> ownIds;
        rsIdentity->getOwnIds(ownIds);
        if (!ownIds.empty()) {
            ownId = ownIds.front();
        }
    }
    
    // 1. Populate un-downloaded updates
    for (int i = 0; i < (int)undownloadedUpdates.size(); ++i) {
        const auto &update = undownloadedUpdates[i];
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);
        
        QString targetSha = "";
        if (!update.mRefUpdates.empty()) {
            targetSha = QString::fromStdString(update.mRefUpdates.begin()->second);
        }
        
        QTableWidgetItem *itemHash = new QTableWidgetItem(targetSha.left(8));
        itemHash->setData(Qt::UserRole, targetSha);
        
        // Show MsgName ("New Commit: <msg>" only when unread, otherwise strip it)
        QString msgName = QString::fromStdString(update.mMeta.mMsgName);
        if (isUnread) {
            if (!msgName.startsWith("New Commit:")) {
                msgName = "New Commit: " + msgName;
            }
        } else {
            if (msgName.startsWith("New Commit:")) {
                msgName = msgName.mid(11).trimmed();
            }
        }
        QTableWidgetItem *itemMsg = new QTableWidgetItem(msgName);
        
        // Author
        QString author = tr("Anonymous");
        if (!update.mMeta.mAuthorId.isNull() && rsIdentity) {
            RsIdentityDetails idDetails;
            if (rsIdentity->getIdDetails(update.mMeta.mAuthorId, idDetails)) {
                author = QString::fromUtf8(idDetails.mNickname.c_str());
            }
        }
        QTableWidgetItem *itemAuth = new QTableWidgetItem(author);
        
        // Date
        QString dateStr = QDateTime::fromTime_t(update.mMeta.mPublishTs).toString(Qt::SystemLocaleShortDate);
        QTableWidgetItem *itemDate = new QTableWidgetItem(dateStr);
        
        if (isUnread) {
            QFont font = itemHash->font();
            font.setBold(true);
            itemHash->setFont(font);
            itemMsg->setFont(font);
            itemAuth->setFont(font);
            itemDate->setFont(font);
        }
        
        int rowIdx = i + offset;
        mCommitTable->setItem(rowIdx, 0, itemHash);
        mCommitTable->setItem(rowIdx, 1, itemMsg);
        mCommitTable->setItem(rowIdx, 2, itemAuth);
        mCommitTable->setItem(rowIdx, 3, itemDate);
        
        // Status Column
        QPushButton *btnStatus = new QPushButton(isUnread ? tr("Mark Read") : tr("Mark Unread"), mCommitTable);
        if (isUnread) {
            btnStatus->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
        } else {
            btnStatus->setStyleSheet("QPushButton { color: #7f8c8d; }");
        }
        QString msgIdStr = QString::fromStdString(update.mMeta.mMsgId.toStdString());
        connect(btnStatus, &QPushButton::clicked, [this, msgIdStr, isUnread]() {
            onCommitReadStatusToggled(msgIdStr, isUnread);
        });
        mCommitTable->setCellWidget(rowIdx, 4, btnStatus);
        
        // Action Column: Pull Button (since it is not downloaded yet)
        if (!creatorId.isNull() && !ownId.isNull()) {
            QPushButton *btnPull = new QPushButton(tr("Pull"), mCommitTable);
            btnPull->setStyleSheet("QPushButton { font-weight: bold; color: #2980b9; }");
            QString localPath = mLocalPathEdit->text().trimmed();
            connect(btnPull, &QPushButton::clicked, [this, groupId, creatorId, ownId, localPath, repoName]() {
                CloneRecord rec;
                rec.repoId = groupId;
                rec.repoName = repoName;
                rec.ownerId = QString::fromStdString(creatorId.toStdString());
                rec.status = tr("Requesting secure pull tunnel...");
                rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
                mCloneHistory.insert(mCloneHistory.begin(), rec);
                populateClonesTable();
                
                if (mPackfilesTab) {
                    ui->rightPaneTabWidget->setCurrentWidget(mPackfilesTab);
                }
                
                if (!rsGit->requestPullOverTunnel(RsGxsGroupId(groupId.toStdString()), creatorId, ownId, localPath.toStdString())) {
                    if (!mCloneHistory.empty()) {
                        mCloneHistory[0].status = tr("Failed to initiate sync request.");
                        populateClonesTable();
                    }
                }
            });
            mCommitTable->setCellWidget(rowIdx, 5, btnPull);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }
    
    // 2. Populate downloaded commits
    int undownloadedCount = undownloadedUpdates.size();
    for (int i = 0; i < (int)commits.size(); i++) {
        int rowIdx = undownloadedCount + i + offset;
        
        QTableWidgetItem *itemHash = new QTableWidgetItem(QString::fromStdString(commits[i].hash));
        itemHash->setData(Qt::UserRole, QString::fromStdString(commits[i].full_hash));
        QTableWidgetItem *itemAuth = new QTableWidgetItem(QString::fromStdString(commits[i].author));
        QTableWidgetItem *itemDate = new QTableWidgetItem(QString::fromStdString(commits[i].date));
        
        // Check read/unread status
        bool hasGxsMsg = false;
        bool isUnread = false;
        RsGxsMessageId gxsMsgId;
        
        auto itMsg = commitToMsgId.find(commits[i].full_hash);
        if (itMsg == commitToMsgId.end()) {
            itMsg = commitToMsgId.find(commits[i].hash);
        }
        if (itMsg != commitToMsgId.end()) {
            hasGxsMsg = true;
            gxsMsgId = itMsg->second.first;
            isUnread = itMsg->second.second;
        }
        
        if (isUnread) {
            QFont font = itemHash->font();
            font.setBold(true);
            itemHash->setFont(font);
            itemAuth->setFont(font);
            itemDate->setFont(font);
        }
        
        mCommitTable->setItem(rowIdx, 0, itemHash);
        mCommitTable->setItem(rowIdx, 2, itemAuth);
        mCommitTable->setItem(rowIdx, 3, itemDate);
        
        // Format message column (show "New Commit: <msg>" if GXS update)
        QString textToShow = QString::fromStdString(commits[i].message);
        auto nameIt = commitToMsgName.find(commits[i].full_hash);
        if (nameIt == commitToMsgName.end()) {
            nameIt = commitToMsgName.find(commits[i].hash);
        }
        
        if (nameIt != commitToMsgName.end()) {
            QString msgName = QString::fromStdString(nameIt->second);
            if (isUnread) {
                if (!msgName.startsWith("New Commit:")) {
                    msgName = "New Commit: " + msgName;
                }
            } else {
                if (msgName.startsWith("New Commit:")) {
                    msgName = msgName.mid(11).trimmed();
                }
            }
            QTableWidgetItem *itemMsg = new QTableWidgetItem(msgName);
            if (isUnread) {
                QFont font = itemMsg->font();
                font.setBold(true);
                itemMsg->setFont(font);
            }
            mCommitTable->setItem(rowIdx, 1, itemMsg);
        } else {
            QTableWidgetItem *itemMsg = new QTableWidgetItem(textToShow);
            if (isUnread) {
                QFont font = itemMsg->font();
                font.setBold(true);
                itemMsg->setFont(font);
            }
            mCommitTable->setItem(rowIdx, 1, itemMsg);
        }
        
        // Status column with button or -
        if (hasGxsMsg) {
            QPushButton *btnStatus = new QPushButton(isUnread ? tr("Mark Read") : tr("Mark Unread"), mCommitTable);
            if (isUnread) {
                btnStatus->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            } else {
                btnStatus->setStyleSheet("QPushButton { color: #7f8c8d; }");
            }
            QString msgIdStr = QString::fromStdString(gxsMsgId.toStdString());
            connect(btnStatus, &QPushButton::clicked, [this, msgIdStr, isUnread]() {
                onCommitReadStatusToggled(msgIdStr, isUnread);
            });
            mCommitTable->setCellWidget(rowIdx, 4, btnStatus);
        } else {
            QTableWidgetItem *itemStatus = new QTableWidgetItem("-");
            mCommitTable->setItem(rowIdx, 4, itemStatus);
        }
        
        // Action column: show "Push" button if it is our unpushed local commit
        if (isAdmin && !hasGxsMsg) {
            QPushButton *btnPushRow = new QPushButton(tr("Push"), mCommitTable);
            btnPushRow->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            connect(btnPushRow, &QPushButton::clicked, this, &MainWidget::onPushClicked);
            mCommitTable->setCellWidget(rowIdx, 5, btnPushRow);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }
    
    // Update button states based on repository status
    bool hasPath = !rawPath.isEmpty();
    bool pathExists = false;
    bool isCloned = false;
    if (hasPath) {
        QString cleanPath = QDir::cleanPath(rawPath);
        pathExists = QDir(cleanPath).exists();
        if (pathExists && GitManager::isValidRepository(cleanPath.toStdString())) {
            isCloned = true;
        }
    }
    
    mBtnPush->setEnabled(isAdmin);
    bool hasUndownloaded = (undownloadedUpdates.size() > 0);
    mBtnPull->setEnabled(isCloned && hasUndownloaded);
    mBtnClone->setEnabled(!isAdmin && !isCloned);
    mBtnOpenFolder->setEnabled(hasPath && pathExists);
    mBtnCommit->setEnabled(hasPath && pathExists && isAdmin && hasLocalChanges);
}



void MainWidget::populateRepoBrowser(const QString &groupId)
{
    mRepoBrowserList->clear();
    
    std::string bareRepoPath = GitManager::getBareRepoPath(groupId.toStdString());
    std::vector<std::string> files;
    
    if (GitManager::getRepoFiles(bareRepoPath, files)) {
        for (const std::string& file : files) {
            mRepoBrowserList->addItem(QString::fromStdString(file));
        }
    }
}

void MainWidget::onLocalPathChanged(const QString &text)
{
    mBtnOpenFolder->setEnabled(!text.isEmpty());
    
    bool isAdmin = false;
    if (ui->treeWidget && ui->treeWidget->treeWidget()) {
        QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
        if (item) {
            QString groupId = ui->treeWidget->itemId(item);
            if (!groupId.isEmpty()) {
                std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
                std::vector<RsGitGroup> groups;
                if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
                    uint32_t flags = groups[0].mMeta.mSubscribeFlags;
                    isAdmin = IS_GROUP_ADMIN(flags);
                }
            }
        }
    }
    mBtnCommit->setEnabled(!text.isEmpty() && isAdmin);
    
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (item) {
        QString groupId = ui->treeWidget->itemId(item);
        if (!groupId.isEmpty()) {
            QString cleanText = text.trimmed();
            if (!cleanText.isEmpty()) {
                cleanText = QDir::cleanPath(cleanText);
            }
            saveRepoLocalPath(groupId, cleanText);
            populateCommitLog(groupId);
        }
    }
}

void MainWidget::onBrowseClicked()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"), "", QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        mLocalPathEdit->setText(QDir::cleanPath(dir));
        mBtnOpenFolder->setEnabled(true);
        // save is handled by textChanged

        bool isAdmin = false;
        std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
        std::vector<RsGitGroup> groups;
        if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
            uint32_t flags = groups[0].mMeta.mSubscribeFlags;
            isAdmin = IS_GROUP_ADMIN(flags);
        }

        if (isAdmin && !GitManager::isValidRepository(dir.toStdString())) {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, 
                tr("Initialize Repository?"), 
                tr("The selected directory is not a Git repository. As the owner of this repository, you need a local Git repository to commit and push. Would you like to initialize a new Git repository here?"),
                QMessageBox::Yes | QMessageBox::No
            );
            if (reply == QMessageBox::Yes) {
                if (GitManager::initRepository(dir.toStdString(), false)) {
                    QMessageBox::information(this, tr("Success"), tr("Successfully initialized a new Git repository."));
                    populateCommitLog(groupId);
                } else {
                    QMessageBox::critical(this, tr("Error"), tr("Failed to initialize Git repository."));
                }
            }
        }
    }
}

void MainWidget::onOpenFolderClicked()
{
    QString localPath = mLocalPathEdit->text();
    if (!localPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    }
}

void MainWidget::onCloneClicked()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    QString localPath = mLocalPathEdit->text();
    if (localPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a local working directory first."));
        return;
    }

    // Direct decentralised clone over secure GxsTunnel only
    RsGxsId creatorId;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
    std::vector<RsGitGroup> groups;
    QString repoName = groupId;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        creatorId = groups[0].mMeta.mAuthorId;
        repoName = QString::fromUtf8(groups[0].mGroupName.c_str());
    }

    if (creatorId.isNull()) {
        QMessageBox::critical(this, tr("Clone Failed"), tr("Could not locate repository creator ID to clone from."));
        return;
    }

    RsGxsId ownId;
    if (rsIdentity) {
        std::list<RsGxsId> ownIds;
        rsIdentity->getOwnIds(ownIds);
        if (!ownIds.empty()) {
            ownId = ownIds.front();
        }
    }

    if (ownId.isNull()) {
        QMessageBox::critical(this, tr("Clone Failed"), tr("You need a local GXS identity to establish secure clone tunnels. Please create one in the identities page."));
        return;
    }

    // Add clone request to history
    CloneRecord rec;
    rec.repoId = groupId;
    rec.repoName = repoName;
    rec.ownerId = QString::fromStdString(creatorId.toStdString());
    rec.status = tr("Requesting secure tunnel...");
    rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    mCloneHistory.insert(mCloneHistory.begin(), rec);
    populateClonesTable();

    // Switch tab to Pushes / Packs
    if (mPackfilesTab) {
        ui->rightPaneTabWidget->setCurrentWidget(mPackfilesTab);
    }

    if (!rsGit->requestCloneOverTunnel(RsGxsGroupId(groupId.toStdString()), creatorId, ownId, localPath.toStdString())) {
        if (!mCloneHistory.empty()) {
            mCloneHistory[0].status = tr("Failed to initiate request.");
            populateClonesTable();
        }
        QMessageBox::critical(this, tr("Clone Failed"), tr("Failed to initiate clone tunnel request."));
    }
}

void MainWidget::onPushClicked()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;
    
    QString localPath = mLocalPathEdit->text();
    if (localPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select your local working directory (where your commits are) to push."));
        return;
    }

    if (!GitManager::isValidRepository(localPath.toStdString())) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            tr("Initialize Repository?"), 
            tr("The selected directory is not a Git repository. You need a Git repository with commits to push. Would you like to initialize a new Git repository here?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            if (!GitManager::initRepository(localPath.toStdString(), false)) {
                QMessageBox::critical(this, tr("Error"), tr("Failed to initialize Git repository."));
                return;
            }
        } else {
            return;
        }
    }

    // Use the LOCAL working tree to generate the packfile, NOT the bare repo!
    std::string repoPath = localPath.toStdString();
    std::string packfileData;
    std::map<std::string, std::string> refUpdates;
    
    std::cout << "Attempting to create packfile from " << repoPath << " ..." << std::endl;
    if (GitManager::createPackfile(repoPath, packfileData, refUpdates)) {
        
        // Unpack locally immediately so the owner's sync repo is instantly updated!
        std::string bareRepoPath = GitManager::getBareRepoPath(groupId.toStdString());
        GitManager::unpackPackfile(bareRepoPath, packfileData, refUpdates);

        // Update the UI immediately
        populateCommitLog(groupId);
        populateRepoBrowser(groupId);

        // Get the details of the latest commit to include in the GXS update message
        std::vector<GitCommitInfo> latestCommits;
        std::string commitAuthor = "Unknown";
        std::string commitMsg = "Push Update";
        std::string commitDate = "";
        if (GitManager::getCommitLog(repoPath, latestCommits) && !latestCommits.empty()) {
            commitAuthor = latestCommits[0].author;
            commitMsg = latestCommits[0].message;
            while (!commitMsg.empty() && (commitMsg.back() == '\n' || commitMsg.back() == '\r')) {
                commitMsg.pop_back();
            }
            commitDate = latestCommits[0].date;
        }

        RsGitUpdate update;
        update.mMeta.mGroupId = RsGxsGroupId(groupId.toStdString());
        update.mRefUpdates = refUpdates;
        
        // Format MsgName as "New Commit: <msg>"
        update.mMeta.mMsgName = QString("New Commit: %1").arg(
            QString::fromStdString(commitMsg)
        ).toStdString();

        if (packfileData.size() <= 200000) {
            // Send inline
            update.mPackfileData = packfileData;
            
            uint32_t token;
            if (rsGit) {
                rsGit->publishGitUpdate(token, update);
                QMessageBox::information(this, tr("Push Successful"), tr("Local commits published to GXS network!"));
            } else {
                std::cerr << "rsGit not initialized!" << std::endl;
            }
        } else {
            // Write packfileData to a temporary file
            QString tempDir = QDir::cleanPath(QDir::homePath() + "/.retroshare/retrogit_temp");
            QDir().mkpath(tempDir);
            QString tempFilePath = tempDir + QString("/pack_%1.pack").arg(QDateTime::currentMSecsSinceEpoch());
            
            QFile file(tempFilePath);
            if (!file.open(QIODevice::WriteOnly)) {
                QMessageBox::critical(this, tr("Push Failed"), tr("Failed to write temporary packfile to disk."));
                return;
            }
            file.write(packfileData.data(), packfileData.size());
            file.close();
            
            // Share via rsFiles
            if (rsFiles) {
                TransferRequestFlags flags = RS_FILE_REQ_ANONYMOUS_ROUTING;
                rsFiles->ExtraFileHash(tempFilePath.toStdString(), 3600 * 24 * 30, flags); // Share for 30 days
                
                // Wait for hashing to complete
                FileInfo fi;
                int retries = 200; // 200 * 50ms = 10 seconds max
                bool hashOk = false;
                
                // Show a simple progress dialog so the user knows it is hashing
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
                
                // Build attachment
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
                } else {
                    std::cerr << "rsGit not initialized!" << std::endl;
                }
            } else {
                QMessageBox::critical(this, tr("Push Failed"), tr("RetroShare file transfer service is not available."));
            }
        }
    } else {
        QMessageBox::critical(this, tr("Push Failed"), tr("Failed to open local Git repository or create packfile. Ensure the directory is a valid Git repository."));
    }
}

void MainWidget::refreshCurrentRepo()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    populateCommitLog(groupId);
    populateRepoBrowser(groupId);
    populatePackfiles(groupId);
}

void MainWidget::onPullClicked()
{
    refreshCurrentRepo();

    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    // Check if we are owner/admin or subscriber
    bool isAdmin = false;
    RsGxsId creatorId;
    QString repoName;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
        creatorId = groups[0].mMeta.mAuthorId;
        repoName = QString::fromUtf8(groups[0].mGroupName.c_str());
    }

    if (isAdmin) {
        QMessageBox::information(this, tr("Pull / Sync"), 
            tr("You are the repository owner/admin. Remote changes are pushed by you, so there are no remote changes to pull. Repository status has been refreshed."));
        return;
    }

    if (creatorId.isNull()) {
        QMessageBox::warning(this, tr("Pull Failed"), tr("Could not find the repository owner's identity."));
        return;
    }

    RsGxsId ownId;
    if (rsIdentity) {
        std::list<RsGxsId> ownIds;
        rsIdentity->getOwnIds(ownIds);
        if (!ownIds.empty()) {
            ownId = ownIds.front();
        }
    }

    if (ownId.isNull()) {
        QMessageBox::critical(this, tr("Pull Failed"), tr("You need a local GXS identity to establish secure pull tunnels. Please create one in the identities page."));
        return;
    }

    QString localPath = mLocalPathEdit->text().trimmed();

    // Log pull request to clones/syncs history
    CloneRecord rec;
    rec.repoId = groupId;
    rec.repoName = repoName;
    rec.ownerId = QString::fromStdString(creatorId.toStdString());
    rec.status = tr("Requesting secure pull tunnel...");
    rec.time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    mCloneHistory.insert(mCloneHistory.begin(), rec);
    populateClonesTable();

    // Switch tab to Available Pushes / Clones progress
    if (mPackfilesTab) {
        ui->rightPaneTabWidget->setCurrentWidget(mPackfilesTab);
    }

    if (!rsGit->requestPullOverTunnel(RsGxsGroupId(groupId.toStdString()), creatorId, ownId, localPath.toStdString())) {
        if (!mCloneHistory.empty()) {
            mCloneHistory[0].status = tr("Failed to initiate sync request.");
            populateClonesTable();
        }
        QMessageBox::critical(this, tr("Pull Failed"), tr("Failed to initiate pull tunnel request."));
    }
}

void MainWidget::onCommitClicked()
{
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) return;

    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) return;

    QString localPath = mLocalPathEdit->text();
    if (localPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a local working directory first."));
        return;
    }

    if (!GitManager::isValidRepository(localPath.toStdString())) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            tr("Initialize Repository?"), 
            tr("The selected directory is not a Git repository. You need a Git repository to commit changes. Would you like to initialize a new Git repository here?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            if (!GitManager::initRepository(localPath.toStdString(), false)) {
                QMessageBox::critical(this, tr("Error"), tr("Failed to initialize Git repository."));
                return;
            }
        } else {
            return;
        }
    }

    std::string authorName = "RetroGit User";
    std::string authorEmail = "user@retroshare";
    if (rsPeers) {
        authorName = rsPeers->getPeerName(rsPeers->getOwnId());
        if (authorName.empty()) {
            authorName = "RetroGit User";
        }
        authorEmail = rsPeers->getOwnId().toStdString() + "@retroshare";
    }

    GitCommitDialog dlg(QString::fromStdString(authorName), QString::fromStdString(authorEmail), this);
    if (dlg.exec() == QDialog::Accepted) {
        QString commitMsg = dlg.getCommitMessage();
        if (commitMsg.isEmpty()) {
            QMessageBox::warning(this, tr("Error"), tr("Commit message cannot be empty."));
            return;
        }

        std::string finalAuthor = dlg.getAuthorName().toStdString();
        std::string finalEmail = dlg.getAuthorEmail().toStdString();

        if (GitManager::commitChanges(localPath.toStdString(), commitMsg.toStdString(), finalAuthor, finalEmail)) {
            QMessageBox::information(this, tr("Success"), tr("Successfully committed local changes!\nClick 'Push / Publish' to share your commits with the network."));
            populateCommitLog(groupId);
            
            // Update Push button text
            if (mCommitTable->rowCount() == 0) {
                mBtnPush->setText(tr("Push & Publish"));
            } else {
                mBtnPush->setText(tr("Push Local Commits"));
            }
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to commit changes. Make sure there are modified or untracked files in the working directory."));
        }
    }
}

void MainWidget::onCommitSelectionChanged()
{
    QList<QTableWidgetItem *> selected = mCommitTable->selectedItems();
    if (selected.isEmpty()) {
        mCommitDetailsWidget->hide();
        return;
    }

    int row = selected.first()->row();
    QTableWidgetItem *hashItem = mCommitTable->item(row, 0);
    if (!hashItem) {
        mCommitDetailsWidget->hide();
        return;
    }
    
    QString fullHash = hashItem->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) {
        fullHash = hashItem->text();
    }
    
    QTreeWidgetItem *repoItem = ui->treeWidget->treeWidget()->currentItem();
    if (!repoItem) {
        mCommitDetailsWidget->hide();
        return;
    }
    QString groupId = ui->treeWidget->itemId(repoItem);
    if (groupId.isEmpty()) {
        mCommitDetailsWidget->hide();
        return;
    }
    
    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    QString rawPath = mLocalPathEdit->text().trimmed();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }
    
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

    // Get current selected commit full hash
    QList<QTableWidgetItem *> selected = mCommitTable->selectedItems();
    if (selected.isEmpty()) return;
    int row = selected.first()->row();
    QTableWidgetItem *hashItem = mCommitTable->item(row, 0);
    if (!hashItem) return;
    QString fullHash = hashItem->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) {
        fullHash = hashItem->text();
    }

    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    QString rawPath = mLocalPathEdit->text().trimmed();
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

void MainWidget::onCommitTableContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = mCommitTable->itemAt(pos);
    if (!item) return;

    int row = item->row();
    QTableWidgetItem *hashItem = mCommitTable->item(row, 0);
    if (!hashItem) return;
    QString fullHash = hashItem->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) {
        fullHash = hashItem->text();
    }

    QMenu contextMenu(this);
    QAction *diffAction = contextMenu.addAction(tr("Show diff"));

    QAction *selectedAction = contextMenu.exec(mCommitTable->viewport()->mapToGlobal(pos));
    if (selectedAction == diffAction) {
        showDiffForCommit(fullHash);
    }
}

void MainWidget::showDiffForCommit(const QString &commitHash)
{
    QTreeWidgetItem *repoItem = ui->treeWidget->treeWidget()->currentItem();
    if (!repoItem) return;
    
    QString groupId = ui->treeWidget->itemId(repoItem);
    if (groupId.isEmpty()) return;

    std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
    QString rawPath = mLocalPathEdit->text().trimmed();
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

void MainWidget::populatePackfiles(const QString &groupId)
{
    mPackfilesTable->setRowCount(0);
    mAvailableUpdates.clear();
    
    RsThread::async([this, groupId]() {
        std::vector<RsGitUpdate> updates;
        if (rsGit && rsGit->getUpdates(RsGxsGroupId(groupId.toStdString()), updates)) {
            RsQThreadUtils::postToObject([this, updates, groupId]() {
                // Check if the selected group has changed in the meantime
                QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
                if (!item || ui->treeWidget->itemId(item) != groupId) {
                    return;
                }
                
                mAvailableUpdates = updates;
                mPackfilesTable->setRowCount(updates.size());
                
                bool hasPendingDownloads = false;
                
                for (int i = 0; i < (int)updates.size(); ++i) {
                    const auto &update = updates[i];
                    
                    // Author Name
                    QString author = tr("Anonymous");
                    if (!update.mMeta.mAuthorId.isNull()) {
                        if (rsIdentity) {
                            RsIdentityDetails idDetails;
                            if (rsIdentity->getIdDetails(update.mMeta.mAuthorId, idDetails)) {
                                author = QString::fromUtf8(idDetails.mNickname.c_str());
                            }
                        }
                        if (author.isEmpty() || author == tr("Anonymous")) {
                            author = QString::fromStdString(update.mMeta.mAuthorId.toStdString()).left(12);
                        }
                    }
                    QTableWidgetItem *itemAuth = new QTableWidgetItem(author);
                    mPackfilesTable->setItem(i, 0, itemAuth);
                    
                    // Date
                    QString dateStr = QDateTime::fromTime_t(update.mMeta.mPublishTs).toString(Qt::SystemLocaleShortDate);
                    QTableWidgetItem *itemDate = new QTableWidgetItem(dateStr);
                    mPackfilesTable->setItem(i, 1, itemDate);
                    
                    // Refs Updated
                    QStringList refs;
                    for (const auto &pair : update.mRefUpdates) {
                        refs << QString::fromStdString(pair.first);
                    }
                    QTableWidgetItem *itemRefs = new QTableWidgetItem(refs.join(", "));
                    itemRefs->setToolTip(refs.join("\n"));
                    mPackfilesTable->setItem(i, 2, itemRefs);
                    
                    // Size and Status/Action
                    if (update.mFiles.empty()) {
                        // Inline update (unpacked immediately)
                        bool isUnprocessed = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);
                        if (isUnprocessed) {
                            if (!update.mPackfileData.empty()) {
                                std::string repoPath = GitManager::getBareRepoPath(groupId.toStdString());
                                if (GitManager::unpackPackfile(repoPath, update.mPackfileData, update.mRefUpdates)) {
                                    if (rsGit) {
                                        rsGit->setMessageProcessedStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(groupId.toStdString()), update.mMeta.mMsgId), true);
                                    }
                                    isUnprocessed = false;
                                }
                            }
                        }

                        QTableWidgetItem *itemSize = new QTableWidgetItem(tr("Inline"));
                        mPackfilesTable->setItem(i, 3, itemSize);
                        
                        QTableWidgetItem *itemStatus = new QTableWidgetItem(isUnprocessed ? tr("Downloaded, unpacking...") : tr("Completed (Inline)"));
                        mPackfilesTable->setItem(i, 4, itemStatus);
                        
                        QTableWidgetItem *itemAction = new QTableWidgetItem(tr("-"));
                        mPackfilesTable->setItem(i, 5, itemAction);
                    } else {
                        const auto &file = update.mFiles[0];
                        QTableWidgetItem *itemSize = new QTableWidgetItem(misc::friendlyUnit(file.mSize));
                        mPackfilesTable->setItem(i, 3, itemSize);
                        
                        // Query status
                        FileInfo info;
                        bool hasFile = rsFiles && rsFiles->alreadyHaveFile(file.mHash, info);
                        
                        if (!hasFile && rsFiles) {
                            rsFiles->FileDetails(file.mHash, RS_FILE_HINTS_DOWNLOAD | RS_FILE_HINTS_SPEC_ONLY, info);
                            if (info.downloadStatus == FT_STATE_COMPLETE) {
                                hasFile = true;
                            }
                        }
                        
                        if (hasFile) {
                            bool isUnprocessed = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);
                            if (isUnprocessed) {
                                std::cout << "MainWidget::populatePackfiles: Found fully downloaded but unprocessed update, unpacking: " << file.mHash.toStdString() << std::endl;
                                if (rsGit && rsGit->unpackUpdate(RsGxsGroupId(groupId.toStdString()), update.mMeta.mMsgId, file.mHash, update.mRefUpdates)) {
                                    isUnprocessed = false;
                                }
                            }

                            QTableWidgetItem *itemStatus = new QTableWidgetItem(isUnprocessed ? tr("Downloaded, unpacking...") : tr("Completed"));
                            mPackfilesTable->setItem(i, 4, itemStatus);
                            
                            QTableWidgetItem *itemAction = new QTableWidgetItem(tr("-"));
                            mPackfilesTable->setItem(i, 5, itemAction);
                        } else {
                            // Not downloaded yet or in progress
                            QString statusStr = tr("Remote");
                            bool isDownloading = false;
                            
                            if (rsFiles) {
                                switch (info.downloadStatus) {
                                    case FT_STATE_DOWNLOADING:
                                        {
                                            double percent = 0.0;
                                            if (info.size > 0) percent = (double)info.avail * 100.0 / (double)info.size;
                                            statusStr = tr("Downloading (%1%)").arg(QString::number(percent, 'f', 1));
                                            isDownloading = true;
                                        }
                                        break;
                                    case FT_STATE_WAITING:
                                        statusStr = tr("Waiting");
                                        isDownloading = true;
                                        break;
                                    case FT_STATE_QUEUED:
                                        statusStr = tr("Queued");
                                        isDownloading = true;
                                        break;
                                    case FT_STATE_PAUSED:
                                        statusStr = tr("Paused");
                                        isDownloading = true;
                                        break;
                                    case FT_STATE_CHECKING_HASH:
                                        statusStr = tr("Checking hash");
                                        isDownloading = true;
                                        break;
                                    default:
                                        break;
                                }
                            }
                            
                            QTableWidgetItem *itemStatus = new QTableWidgetItem(statusStr);
                            mPackfilesTable->setItem(i, 4, itemStatus);
                            
                            if (isDownloading) {
                                hasPendingDownloads = true;
                                QPushButton *btnCancel = new QPushButton(tr("Cancel"), mPackfilesTable);
                                btnCancel->setProperty("fileHash", QString::fromStdString(file.mHash.toStdString()));
                                connect(btnCancel, SIGNAL(clicked()), this, SLOT(onCancelDownloadClicked()));
                                mPackfilesTable->setCellWidget(i, 5, btnCancel);
                            } else {
                                QPushButton *btnDl = new QPushButton(tr("Download"), mPackfilesTable);
                                btnDl->setProperty("fileHash", QString::fromStdString(file.mHash.toStdString()));
                                btnDl->setProperty("fileName", QString::fromStdString(file.mName));
                                btnDl->setProperty("fileSize", (qlonglong)file.mSize);
                                connect(btnDl, SIGNAL(clicked()), this, SLOT(onDownloadClicked()));
                                mPackfilesTable->setCellWidget(i, 5, btnDl);
                            }
                        }
                    }
                }
                
                if (hasPendingDownloads) {
                    mDownloadPollTimer->start(1000); // Poll every second
                } else {
                    mDownloadPollTimer->stop();
                }
            }, this);
        }
    });
}

void MainWidget::onDownloadClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    QString fileHashStr = btn->property("fileHash").toString();
    QString fileName = btn->property("fileName").toString();
    qlonglong fileSize = btn->property("fileSize").toLongLong();
    
    RsFileHash fileHash(fileHashStr.toStdString());
    
    std::list<RsPeerId> sources;
    FileInfo fileInfo;
    if (rsFiles) {
        rsFiles->FileDetails(fileHash, RS_FILE_HINTS_REMOTE, fileInfo);
        for (const auto &peer : fileInfo.peers) {
            sources.push_back(peer.peerId);
        }
        
        TransferRequestFlags flags = RS_FILE_REQ_ANONYMOUS_ROUTING;
        rsFiles->FileRequest(fileName.toStdString(), fileHash, fileSize, "", flags, sources);
    }
    
    // Trigger UI refresh
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (item) {
        populatePackfiles(ui->treeWidget->itemId(item));
    }
}

void MainWidget::onCancelDownloadClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    
    QString fileHashStr = btn->property("fileHash").toString();
    RsFileHash fileHash(fileHashStr.toStdString());
    
    if (rsFiles) {
        rsFiles->FileCancel(fileHash);
    }
    
    // Trigger UI refresh
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (item) {
        populatePackfiles(ui->treeWidget->itemId(item));
    }
}

void MainWidget::pollDownloadProgress()
{
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item) {
        mDownloadPollTimer->stop();
        return;
    }
    QString groupId = ui->treeWidget->itemId(item);
    if (groupId.isEmpty()) {
        mDownloadPollTimer->stop();
        return;
    }
    
    bool activeDownloads = false;
    bool completedAny = false;
    
    for (int i = 0; i < mPackfilesTable->rowCount(); ++i) {
        if (i >= (int)mAvailableUpdates.size()) break;
        const auto &update = mAvailableUpdates[i];
        if (update.mFiles.empty()) continue;
        
        const auto &file = update.mFiles[0];
        FileInfo info;
        bool hasFile = rsFiles && rsFiles->alreadyHaveFile(file.mHash, info);
        if (!hasFile && rsFiles) {
            rsFiles->FileDetails(file.mHash, RS_FILE_HINTS_DOWNLOAD | RS_FILE_HINTS_SPEC_ONLY, info);
            if (info.downloadStatus == FT_STATE_COMPLETE) {
                hasFile = true;
            }
        }
        
        if (hasFile) {
            if (rsGit) {
                std::cout << "MainWidget::pollDownloadProgress: packfile completed, unpacking " << file.mHash.toStdString() << std::endl;
                if (rsGit->unpackUpdate(RsGxsGroupId(groupId.toStdString()), update.mMeta.mMsgId, file.mHash, update.mRefUpdates)) {
                    completedAny = true;
                }
            }
        } else {
            QString statusStr = tr("Remote");
            bool isDownloading = false;
            
            if (rsFiles) {
                switch (info.downloadStatus) {
                    case FT_STATE_DOWNLOADING:
                        {
                            double percent = 0.0;
                            if (info.size > 0) percent = (double)info.avail * 100.0 / (double)info.size;
                            statusStr = tr("Downloading (%1%)").arg(QString::number(percent, 'f', 1));
                            isDownloading = true;
                        }
                        break;
                    case FT_STATE_WAITING:
                        statusStr = tr("Waiting");
                        isDownloading = true;
                        break;
                    case FT_STATE_QUEUED:
                        statusStr = tr("Queued");
                        isDownloading = true;
                        break;
                    case FT_STATE_PAUSED:
                        statusStr = tr("Paused");
                        isDownloading = true;
                        break;
                    case FT_STATE_CHECKING_HASH:
                        statusStr = tr("Checking hash");
                        isDownloading = true;
                        break;
                    default:
                        break;
                }
            }
            
            if (isDownloading) {
                activeDownloads = true;
                mPackfilesTable->setItem(i, 4, new QTableWidgetItem(statusStr));
            }
        }
    }
    
    if (completedAny) {
        populateCommitLog(groupId);
        populateRepoBrowser(groupId);
        populatePackfiles(groupId);
        
        if (mCommitTable->rowCount() > 0) {
            mBtnPush->setText(tr("Push Local Commits"));
        }
    } else if (!activeDownloads) {
        mDownloadPollTimer->stop();
        populatePackfiles(groupId);
    }
}

void MainWidget::populateClonesTable()
{
    if (!mClonesTable) return;
    
    mClonesTable->setRowCount(0);
    mClonesTable->setRowCount(mCloneHistory.size());
    
    for (int i = 0; i < (int)mCloneHistory.size(); ++i) {
        QTableWidgetItem *itemRepo = new QTableWidgetItem(mCloneHistory[i].repoName.isEmpty() ? mCloneHistory[i].repoId : mCloneHistory[i].repoName);
        QTableWidgetItem *itemOwner = new QTableWidgetItem(mCloneHistory[i].ownerId);
        QTableWidgetItem *itemStatus = new QTableWidgetItem(mCloneHistory[i].status);
        QTableWidgetItem *itemTime = new QTableWidgetItem(mCloneHistory[i].time);
        
        // Highlight active clones
        if (mCloneHistory[i].status.contains("Requesting") || mCloneHistory[i].status.contains("secured") || mCloneHistory[i].status.contains("Unpacking")) {
            QFont font = itemStatus->font();
            font.setBold(true);
            itemStatus->setFont(font);
            itemStatus->setForeground(QBrush(QColor("#2980b9"))); // Blue for in-progress
        } else if (mCloneHistory[i].status.contains("Successful") || mCloneHistory[i].status.contains("completed")) {
            itemStatus->setForeground(QBrush(QColor("#27ae60"))); // Green for success
        } else if (mCloneHistory[i].status.contains("Failed") || mCloneHistory[i].status.contains("down")) {
            itemStatus->setForeground(QBrush(QColor("#c0392b"))); // Red for failure
        }
        
        mClonesTable->setItem(i, 0, itemRepo);
        mClonesTable->setItem(i, 1, itemOwner);
        mClonesTable->setItem(i, 2, itemStatus);
        mClonesTable->setItem(i, 3, itemTime);
    }
}

void MainWidget::onRepoBrowserContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = mRepoBrowserList->itemAt(pos);
    if (!item) return;
    
    QMenu contextMnu(this);
    contextMnu.addAction(QIcon(":/images/open.png"), tr("Open File"), this, SLOT(openSelectedFile()));
    contextMnu.exec(mRepoBrowserList->mapToGlobal(pos));
}

void MainWidget::openSelectedFile()
{
    QListWidgetItem *item = mRepoBrowserList->currentItem();
    if (!item) return;
    
    QString selectedFile = item->text();
    
    QTreeWidgetItem *repoItem = ui->treeWidget->treeWidget()->currentItem();
    if (!repoItem) return;
    QString groupId = ui->treeWidget->itemId(repoItem);
    if (groupId.isEmpty()) return;
    
    QString rawPath = mLocalPathEdit->text().trimmed();
    QString filePath;
    bool opened = false;
    
    // 1. Try to open from working directory if it exists
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            filePath = QDir(localPath).filePath(selectedFile);
            if (QFile::exists(filePath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                opened = true;
            }
        }
    }
    
    // 2. If not opened, extract from bare repository to temp folder and open
    if (!opened) {
        std::string barePath = GitManager::getBareRepoPath(groupId.toStdString());
        QString tempDir = QDir::cleanPath(QDir::homePath() + "/.retroshare/retrogit_temp/previews/" + groupId);
        QDir().mkpath(tempDir);
        
        // Maintain directory structure inside tempDir if file is in subdirectory
        QFileInfo fileInfo(selectedFile);
        if (!fileInfo.path().isEmpty() && fileInfo.path() != ".") {
            QDir().mkpath(tempDir + "/" + fileInfo.path());
        }
        
        filePath = tempDir + "/" + selectedFile;
        
        if (GitManager::extractFile(barePath, selectedFile.toStdString(), filePath.toStdString())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to extract and open file from repository."));
        }
    }
}


