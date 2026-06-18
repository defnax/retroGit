/*******************************************************************************
 * gui/CodeWidget.cpp                                                          *
 *                                                                             *
 * Copyright (C) 2026 RetroShare Team <retroshare.project@gmail.com>           *
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

#include "CodeWidget.h"
#include "ui_CodeWidget.h"
#include "MainWidget.h"
#include "GitBranchDialog.h"
#include "services/GitManager.h"
#include <QDir>
#include <QPushButton>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QStyle>
#include <retroshare/rsinit.h>

extern RsGit *rsGit;

class RepoBrowserItem : public QTreeWidgetItem
{
public:
    RepoBrowserItem(QTreeWidget *parent) : QTreeWidgetItem(parent) {}
    RepoBrowserItem(QTreeWidgetItem *parent) : QTreeWidgetItem(parent) {}

    bool operator<(const QTreeWidgetItem &other) const override
    {
        bool thisIsFolder = this->data(0, Qt::UserRole).toString().isEmpty();
        bool otherIsFolder = other.data(0, Qt::UserRole).toString().isEmpty();

        if (thisIsFolder != otherIsFolder) {
            return thisIsFolder; // Folders on top
        }

        return this->text(0).localeAwareCompare(other.text(0)) < 0;
    }
};

CodeWidget::CodeWidget(MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CodeWidget)
    , mMainWidget(mainWidget)
{
    ui->setupUi(this);

    ui->mRepoBrowserList->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->mRepoBrowserList->setHeaderHidden(true);
    connect(ui->mRepoBrowserList, &QTreeWidget::customContextMenuRequested, this, &CodeWidget::onRepoBrowserContextMenu);
    connect(ui->mRepoBrowserList, &QTreeWidget::itemDoubleClicked, this, &CodeWidget::openSelectedFile);
    connect(ui->mBtnOpenFolder, &QPushButton::clicked, this, &CodeWidget::onOpenFolderClicked);
    connect(ui->mBtnCreateBranch, &QPushButton::clicked, this, &CodeWidget::onCreateBranchClicked);
    connect(ui->mBtnPullRequests, &QPushButton::clicked, this, &CodeWidget::onPullRequestsClicked);
    connect(ui->mBranchCombo, &QComboBox::currentTextChanged, this, &CodeWidget::onBranchComboChanged);

    int iconSize = qMax(fontMetrics().height(), 24);
    ui->mBranchCombo->setIconSize(QSize(iconSize, iconSize));

    QIcon prIcon(":/images/git-pull-request.png");
    ui->mBtnPullRequests->setIcon(prIcon);
    ui->mBtnPullRequests->setIconSize(QSize(iconSize, iconSize));
    ui->mBtnPullRequests->setStyleSheet("QPushButton { border: none; background-color: transparent; padding: 4px 8px; font-weight: bold; color: #24292f; }"
                                       "QPushButton:hover { background-color: #f3f4f6; border-radius: 6px; }");

    clear();
}

CodeWidget::~CodeWidget()
{
    delete ui;
}

void CodeWidget::clear()
{
    ui->mRepoBrowserList->clear();
    ui->mBtnOpenFolder->setEnabled(false);
    ui->mBtnCreateBranch->setEnabled(false);
    ui->mBtnCreateBranch->setVisible(false);
    ui->mBtnPullRequests->setText(tr("Pull requests (0)"));
    ui->mBtnPullRequests->setEnabled(false);

    ui->mBranchCombo->blockSignals(true);
    ui->mBranchCombo->clear();
    ui->mBranchCombo->blockSignals(false);


    ui->mBranchCountLabel->setText("0 Branches");
    ui->mTagCountLabel->setText("0 Tags");
    mGroupId.clear();
    mSelectedBranchOrTag.clear();
}

void CodeWidget::setGroupId(const QString &groupId)
{
    mGroupId = groupId;
    if (mGroupId.isEmpty()) {
        clear();
        return;
    }

    refresh();
}

void CodeWidget::refresh()
{
    if (mGroupId.isEmpty()) return;

    // Check if user is Admin of the repository
    bool isAdmin = false;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
    }

    QString rawPath = mMainWidget->getLocalPath();
    ui->mBtnOpenFolder->setEnabled(!rawPath.isEmpty() && QDir(rawPath).exists());
    ui->mBtnCreateBranch->setEnabled(isAdmin);
    ui->mBtnCreateBranch->setVisible(isAdmin);
    ui->mBtnPullRequests->setEnabled(true);

    // Fetch count of open pull requests
    int openPRCount = 0;
    std::vector<RsGitPullRequest> pullRequests;
    if (rsGit && rsGit->getPullRequests(RsGxsGroupId(mGroupId.toStdString()), pullRequests)) {
        std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
        for (const auto &pr : pullRequests) {
            bool isOpen = (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (pr.mStatus == 0);
            if (isOpen) {
                if (GitManager::isBranchMerged(bareRepoPath, pr.mSourceBranch, pr.mTargetBranch)) {
                    isOpen = false;
                    // Automatically mark as processed locally
                    uint32_t token;
                    rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), pr.mMeta.mMsgId), true);
                }
            } else if (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) {
                // Automatically mark as processed locally if status is Closed or Merged
                uint32_t token;
                rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), pr.mMeta.mMsgId), true);
            }
            if (isOpen) {
                openPRCount++;
            }
        }
    }
    ui->mBtnPullRequests->setText(tr("Pull requests (%1)").arg(openPRCount));




    // 1. Fetch branches and tags from bare repository
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> branches;
    std::string currentBranch;
    std::vector<std::string> tags;

    // Load icons
    int iconSize = qMax(fontMetrics().height(), 24);
    QPixmap branchPixmap(":/images/git-branch-2.png");
    QPixmap tagPixmap(":/images/tag.png");
    ui->mBranchCountIcon->setPixmap(branchPixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->mTagCountIcon->setPixmap(tagPixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Disable signals during combo repopulation to prevent multiple refreshes
    ui->mBranchCombo->blockSignals(true);
    QString previousSelected = ui->mBranchCombo->currentText();
    ui->mBranchCombo->clear();

    if (GitManager::getBranches(bareRepoPath, branches, currentBranch)) {
        ui->mBranchCountLabel->setText(tr("%1 Branches").arg(branches.size()));

        // Add branches to combo
        for (const std::string& branch : branches) {
            ui->mBranchCombo->addItem(QIcon(":/images/git-branch-2.png"), QString::fromStdString(branch));
        }
    } else {
        ui->mBranchCountLabel->setText(tr("0 Branches"));
    }

    if (GitManager::getTags(bareRepoPath, tags)) {
        ui->mTagCountLabel->setText(tr("%1 Tags").arg(tags.size()));

        // Add tags to combo
        for (const std::string& tag : tags) {
            ui->mBranchCombo->addItem(QIcon(":/images/tag.png"), QString::fromStdString(tag));
        }
    } else {
        ui->mTagCountLabel->setText(tr("0 Tags"));
    }


    // Restore selection or select default active branch
    int selectIdx = -1;
    if (!previousSelected.isEmpty()) {
        selectIdx = ui->mBranchCombo->findText(previousSelected);
    }
    if (selectIdx == -1 && !currentBranch.empty()) {
        selectIdx = ui->mBranchCombo->findText(QString::fromStdString(currentBranch));
    }
    if (selectIdx == -1 && ui->mBranchCombo->count() > 0) {
        selectIdx = 0;
    }

    if (selectIdx != -1) {
        ui->mBranchCombo->setCurrentIndex(selectIdx);
        mSelectedBranchOrTag = ui->mBranchCombo->currentText();
    } else {
        mSelectedBranchOrTag.clear();
    }
    ui->mBranchCombo->blockSignals(false);

    // 2. Populate file list for selected branch/tag
    populateRepoBrowser(mSelectedBranchOrTag);

    if (mMainWidget) {
        mMainWidget->refreshGitWidget();
    }
}

void CodeWidget::handleGitEvent(const RsGitEvent *e)
{
    if (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString()))
        return;

    if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED || e->mGitEventCode == RsGitEventCode::POST_UPDATED) {
        refresh();
    }
}

void CodeWidget::onOpenFolderClicked()
{
    QString localPath = mMainWidget->getLocalPath();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    } else {
        QMessageBox::warning(this, tr("Warning"), tr("Local working directory does not exist or is not set."));
    }
}

void CodeWidget::onRepoBrowserContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = ui->mRepoBrowserList->itemAt(pos);
    if (!item) return;

    // Only show context menu for files
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    QMenu contextMnu(this);
    contextMnu.addAction(QIcon(":/images/open.png"), tr("Open File"), this, &CodeWidget::openSelectedFile);
    contextMnu.exec(ui->mRepoBrowserList->mapToGlobal(pos));
}

void CodeWidget::openSelectedFile()
{
    QTreeWidgetItem *item = ui->mRepoBrowserList->currentItem();
    if (!item) return;

    QString selectedFile = item->data(0, Qt::UserRole).toString();
    if (selectedFile.isEmpty()) return; // Skip folders

    QString rawPath = mMainWidget->getLocalPath();
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
        std::string barePath = GitManager::getBareRepoPath(mGroupId.toStdString());
        QString tempDir = QDir::cleanPath(QString::fromStdString(RsAccounts::AccountDirectory()) + "/retrogit_temp/previews/" + mGroupId);
        QDir().mkpath(tempDir);

        QFileInfo fileInfo(selectedFile);
        if (!fileInfo.path().isEmpty() && fileInfo.path() != ".") {
            QDir().mkpath(tempDir + "/" + fileInfo.path());
        }

        filePath = tempDir + "/" + selectedFile;

        if (GitManager::extractFile(barePath, selectedFile.toStdString(), filePath.toStdString(), mSelectedBranchOrTag.toStdString())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to extract and open file from repository."));
        }
    }
}

void CodeWidget::populateRepoBrowser(const QString &branchOrTag)
{
    ui->mRepoBrowserList->clear();
    if (mGroupId.isEmpty()) return;

    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> files;

    if (GitManager::getRepoFiles(bareRepoPath, files, branchOrTag.toStdString())) {
        QIcon folderIcon(":/images/file-directory-fill-24_.png");
        QIcon fileIcon(":/images/file-16_.png");

        QMap<QString, RepoBrowserItem*> folderItems;

        for (const std::string& file : files) {
            QString filePath = QString::fromStdString(file);
            QStringList parts = filePath.split('/');

            RepoBrowserItem *parentItem = nullptr;
            QString currentPath = "";

            // Loop through parent directories
            for (int i = 0; i < parts.size() - 1; ++i) {
                if (parts[i].isEmpty()) continue;
                if (i > 0) currentPath += "/";
                currentPath += parts[i];

                if (folderItems.contains(currentPath)) {
                    parentItem = folderItems[currentPath];
                } else {
                    RepoBrowserItem *newFolderItem;
                    if (parentItem) {
                        newFolderItem = new RepoBrowserItem(parentItem);
                    } else {
                        newFolderItem = new RepoBrowserItem(ui->mRepoBrowserList);
                    }
                    newFolderItem->setText(0, parts[i]);
                    newFolderItem->setIcon(0, folderIcon);
                    newFolderItem->setData(0, Qt::UserRole, ""); // Empty marks it as a folder
                    folderItems[currentPath] = newFolderItem;
                    parentItem = newFolderItem;
                }
            }

            // Add the file leaf item
            if (!parts.isEmpty() && !parts.last().isEmpty()) {
                RepoBrowserItem *fileItem;
                if (parentItem) {
                    fileItem = new RepoBrowserItem(parentItem);
                } else {
                    fileItem = new RepoBrowserItem(ui->mRepoBrowserList);
                }
                fileItem->setText(0, parts.last());
                fileItem->setIcon(0, fileIcon);
                fileItem->setData(0, Qt::UserRole, filePath);
            }
        }

        // Sort items: folders on top, then alphabetical order
        ui->mRepoBrowserList->sortItems(0, Qt::AscendingOrder);
    }
}

void CodeWidget::onBranchComboChanged(const QString &text)
{
    if (text.isEmpty()) return;
    mSelectedBranchOrTag = text;
    populateRepoBrowser(mSelectedBranchOrTag);

    if (mMainWidget) {
        mMainWidget->refreshGitWidget();
    }
}

void CodeWidget::onCreateBranchClicked()
{
    if (mGroupId.isEmpty()) return;

    // 1. Fetch current branches and active branch
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> branches;
    std::string currentBranch;

    if (!GitManager::getBranches(bareRepoPath, branches, currentBranch)) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to read repository branches."));
        return;
    }

    QStringList qBranches;
    for (const std::string& b : branches) {
        qBranches.append(QString::fromStdString(b));
    }

    // 2. Open GitBranchDialog
    GitBranchDialog dlg(qBranches, QString::fromStdString(currentBranch), this);
    if (dlg.exec() == QDialog::Accepted) {
        QString newBranchName = dlg.getBranchName();
        QString sourceBranchName = dlg.getSourceBranch();

        if (newBranchName.isEmpty()) {
            QMessageBox::warning(this, tr("Warning"), tr("Branch name cannot be empty."));
            return;
        }

        // 3. Create the new branch and switch HEAD to it
        if (GitManager::createBranch(bareRepoPath, newBranchName.toStdString(), sourceBranchName.toStdString())) {
            QMessageBox::information(this, tr("Success"), tr("Branch '%1' created successfully.").arg(newBranchName));
            refresh();
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to create branch."));
        }
    }
}

void CodeWidget::onPullRequestsClicked()
{
    if (mGroupId.isEmpty()) return;
    mMainWidget->showPullRequests(mGroupId);
}


