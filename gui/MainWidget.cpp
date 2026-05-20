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
#include <QTime>
#include <iostream>
#include <string>

#include "gui/common/RSTreeWidget.h"
#include "util/DateTime.h"
#include "util/qtthreadsutils.h"
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
#include <retroshare/rsfiles.h>

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
    
    mCommitTable = new QTableWidget(0, 4, ui->tabWidgetPage1);
    mCommitTable->setHorizontalHeaderLabels(QStringList() << tr("Hash") << tr("Message") << tr("Author") << tr("Date"));
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
    
    QLabel *lblBrowser = new QLabel(tr("Files in the HEAD commit of the bare repository (Network State):"), repoBrowserTab);
    repoBrowserLayout->addWidget(lblBrowser);
    
    mRepoBrowserList = new QListWidget(repoBrowserTab);
    repoBrowserLayout->addWidget(mRepoBrowserList);
    
    ui->rightPaneTabWidget->addTab(repoBrowserTab, QIcon(":/images/git.png"), tr("Repository Browser"));
    ui->rightPaneTabWidget->setTabsClosable(true);
    connect(ui->rightPaneTabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));

    // load settings
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
  RsThread::async([this]() {
    // Fetch group metadata from backend
    std::list<RsGxsGroupId> groupIds; // empty list = get all groups
    std::vector<RsGitGroup> groups;

    if (!rsGit->getGroups(groupIds, groups)) {
      std::cerr << "MainWidget::loadGroupMeta() Error getting groups"
                << std::endl;
      return;
    }

    // Convert to RsGroupMetaData for display
    std::list<RsGroupMetaData> groupMeta;
    for (auto &group : groups) {
      groupMeta.push_back(group.mMeta);
    }

    // Update UI in main thread
    RsQThreadUtils::postToObject(
        [this, groupMeta]() {
          if (groupMeta.size() > 0) {
            insertGroupsData(groupMeta);
          }
        },
        this);
  });
}

void MainWidget::insertGroupsData(const std::list<RsGroupMetaData> &gitList)
{
  std::list<RsGroupMetaData>::const_iterator it;

  QList<GroupItemInfo> activeList;
  std::multimap<uint32_t, GroupItemInfo> popMap;
  QList<GroupItemInfo> popList;
  QList<GroupItemInfo> otherList;

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

  uint32_t popCount = 5;
  if (popCount < popMap.size() / 10) {
    popCount = popMap.size() / 10;
  }

  uint32_t i = 0;
  uint32_t popLimit = 0;
  std::multimap<uint32_t, GroupItemInfo>::reverse_iterator rit;
  for (rit = popMap.rbegin(); ((rit != popMap.rend()) && (i < popCount));
       ++rit, i++)
    ;
  if (rit != popMap.rend()) {
    popLimit = rit->first;
  }

  for (rit = popMap.rbegin(); rit != popMap.rend(); ++rit) {
    if (rit->second.popularity < (int)popLimit) {
      if (mGroupSet == 4 || mGroupSet == 0)
        activeList.append(rit->second);
    } else {
      if (mGroupSet == 3 || mGroupSet == 0)
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
        case RsGitEventCode::NEW_POST:
        {
            // A packfile was received and unpacked into the bare repo — refresh browser/log
            QString updatedGroupId = QString::fromStdString(e->mGitGroupId.toStdString());
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

    contextMnu.exec(QCursor::pos());
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

    while (ui->rightPaneTabWidget->count() > 2) {
        QWidget *tab = ui->rightPaneTabWidget->widget(2);
        ui->rightPaneTabWidget->removeTab(2);
        delete tab;
    }

    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (!item || ui->treeWidget->itemId(item).isEmpty()) {
        mBtnPush->setEnabled(false);
        mBtnPull->setEnabled(false);
        mBtnClone->setEnabled(false);
        mLocalPathEdit->clear();
        return;
    }
    
    // Disable text change signal temporarily to prevent overwriting settings with an empty string during transition
    mLocalPathEdit->blockSignals(true);
    QString groupId = ui->treeWidget->itemId(item);
    QString path = loadRepoLocalPath(groupId);
    mLocalPathEdit->setText(path);
    mLocalPathEdit->blockSignals(false);
    
    bool hasPath = !path.isEmpty();
    bool isAdmin = false;
    
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(groupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
    }
    
    mBtnPush->setEnabled(isAdmin);
    mBtnPull->setEnabled(true);
    mBtnClone->setEnabled(!isAdmin);
    mBtnOpenFolder->setEnabled(hasPath);
    mBtnCommit->setEnabled(hasPath);
    
    populateCommitLog(groupId);
    populateRepoBrowser(groupId);
    
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
    
    QString localPath = mLocalPathEdit->text();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        repoPath = localPath.toStdString();
    }
    
    std::vector<GitCommitInfo> commits;
    
    if (GitManager::getCommitLog(repoPath, commits)) {
        mCommitTable->setRowCount(commits.size());
        for (int i = 0; i < (int)commits.size(); i++) {
            QTableWidgetItem *itemHash = new QTableWidgetItem(QString::fromStdString(commits[i].hash));
            itemHash->setData(Qt::UserRole, QString::fromStdString(commits[i].full_hash));
            QTableWidgetItem *itemMsg = new QTableWidgetItem(QString::fromStdString(commits[i].message));
            QTableWidgetItem *itemAuth = new QTableWidgetItem(QString::fromStdString(commits[i].author));
            QTableWidgetItem *itemDate = new QTableWidgetItem(QString::fromStdString(commits[i].date));
            
            mCommitTable->setItem(i, 0, itemHash);
            mCommitTable->setItem(i, 1, itemMsg);
            mCommitTable->setItem(i, 2, itemAuth);
            mCommitTable->setItem(i, 3, itemDate);
        }
    }
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
    mBtnCommit->setEnabled(!text.isEmpty());
    
    if (!ui->treeWidget || !ui->treeWidget->treeWidget()) return;
    QTreeWidgetItem *item = ui->treeWidget->treeWidget()->currentItem();
    if (item) {
        QString groupId = ui->treeWidget->itemId(item);
        if (!groupId.isEmpty()) {
            saveRepoLocalPath(groupId, text);
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
        mLocalPathEdit->setText(dir);
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

    std::string bareRepoPath = GitManager::getBareRepoPath(groupId.toStdString());
    
    if (GitManager::cloneRepository(bareRepoPath, localPath.toStdString())) {
        QMessageBox::information(this, tr("Clone Successful"), tr("Successfully checked out bare repository to:\n") + localPath);
    } else {
        QMessageBox::critical(this, tr("Clone Failed"), tr("Failed to clone repository. Make sure the target folder is empty or does not exist."));
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

        RsGitUpdate update;
        update.mMeta.mGroupId = RsGxsGroupId(groupId.toStdString());
        update.mRefUpdates = refUpdates;

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

void MainWidget::onPullClicked()
{
    QMessageBox::information(this, tr("Pull"), tr("GXS synchronizes bare repositories automatically in the background. To update your working tree, open a terminal in your working directory and run 'git pull'."));
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
    QString localPath = mLocalPathEdit->text();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        repoPath = localPath.toStdString();
    }
    
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
            QStringList pathParts = QString::fromStdString(filePath).split('/');
            QTreeWidgetItem *parentItem = nullptr;
            
            for (int i = 0; i < pathParts.size(); ++i) {
                QString part = pathParts[i];
                if (part.isEmpty()) continue;
                
                QTreeWidgetItem *childItem = nullptr;
                int childCount = parentItem ? parentItem->childCount() : mChangedFilesTree->topLevelItemCount();
                for (int j = 0; j < childCount; ++j) {
                    QTreeWidgetItem *item = parentItem ? parentItem->child(j) : mChangedFilesTree->topLevelItem(j);
                    if (item->text(0) == part) {
                        childItem = item;
                        break;
                    }
                }
                
                if (!childItem) {
                    childItem = new QTreeWidgetItem();
                    childItem->setText(0, part);
                    
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

void MainWidget::onTabCloseRequested(int index)
{
    if (index < 2) return; // Prevent closing the core tabs ("Working Directory", "Repository Browser")
    
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
    QString localPath = mLocalPathEdit->text();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        repoPath = localPath.toStdString();
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

    // Hide close buttons for the first two tabs (indices 0 and 1)
    QTabBar *bar = ui->rightPaneTabWidget->findChild<QTabBar*>();
    if (bar) {
        bar->setTabButton(0, QTabBar::RightSide, nullptr);
        bar->setTabButton(1, QTabBar::RightSide, nullptr);
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
    QString localPath = mLocalPathEdit->text();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        repoPath = localPath.toStdString();
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
    html += "<div class='header-banner'>" + tr("Commit Diff: ") + commitHash.left(8) + "</div>";

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

    QString tabTitle = tr("Diff: ") + commitHash.left(8);
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
    }
}


