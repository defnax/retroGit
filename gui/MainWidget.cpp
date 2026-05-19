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

