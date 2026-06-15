/*******************************************************************************
 * gui/GitWidget.cpp                                                           *
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

#include "GitWidget.h"
#include "ui_GitWidget.h"
#include "MainWidget.h"
#include "services/GitManager.h"
#include "gui/settings/rsharesettings.h"
#include "gui/common/FilesDefs.h"
#include "GitCommitDialog.h"
#include "CodeWidget.h"

#include <QFileDialog>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QHeaderView>
#include <QMenu>
#include <iostream>

extern RsGit *rsGit;
extern RsPeers *rsPeers;
extern RsIdentity *rsIdentity;

GitWidget::GitWidget(MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GitWidget)
    , mMainWidget(mainWidget)
{
    ui->setupUi(this);

    // Configure Commit Table
    ui->mCommitTable->setColumnCount(6);
    ui->mCommitTable->setHorizontalHeaderLabels(QStringList() << tr("Hash") << tr("Message") << tr("Author") << tr("Date") << tr("Status") << tr("Action"));
    ui->mCommitTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->mCommitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mCommitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->mCommitTable->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->mBtnBrowse, &QPushButton::clicked, this, &GitWidget::onBrowseClicked);
    connect(ui->mBtnClone, &QPushButton::clicked, this, &GitWidget::onCloneClicked);
    connect(ui->mBtnCommit, &QPushButton::clicked, this, &GitWidget::onCommitClicked);
    connect(ui->mBtnPush, &QPushButton::clicked, this, &GitWidget::onPushClicked);
    connect(ui->mBtnPull, &QPushButton::clicked, this, &GitWidget::onPullClicked);
    connect(ui->mLocalPathEdit, &QLineEdit::textChanged, this, &GitWidget::onLocalPathChanged);
    connect(ui->mCommitTable, &QTableWidget::itemSelectionChanged, this, &GitWidget::onCommitSelectionChanged);
    connect(ui->mCommitTable, &QTableWidget::customContextMenuRequested, this, &GitWidget::onCommitTableContextMenu);

    clear();
}

GitWidget::~GitWidget()
{
    delete ui;
}

void GitWidget::clear()
{
    ui->mLocalPathEdit->blockSignals(true);
    ui->mLocalPathEdit->clear();
    ui->mLocalPathEdit->blockSignals(false);
    ui->mLocalPathEdit->setEnabled(false);
    ui->mBtnBrowse->setEnabled(false);
    ui->mBtnClone->setEnabled(false);
    ui->mBtnCommit->setEnabled(false);
    ui->mBtnPush->setEnabled(false);
    ui->mBtnPull->setEnabled(false);
    ui->mLblOwnerInfo->setVisible(false);
    ui->mLblSubscriberInfo->setVisible(false);
    ui->mCommitTable->setRowCount(0);
    mGroupId.clear();
}

void GitWidget::setGroupId(const QString &groupId)
{
    mGroupId = groupId;
    if (mGroupId.isEmpty()) {
        clear();
        return;
    }

    ui->mLocalPathEdit->blockSignals(true);
    QString path = loadRepoLocalPath(mGroupId);
    ui->mLocalPathEdit->setText(path);
    ui->mLocalPathEdit->blockSignals(false);

    refresh();
}

void GitWidget::refresh()
{
    if (mGroupId.isEmpty()) return;

    QString path = ui->mLocalPathEdit->text().trimmed();
    bool hasPath = !path.isEmpty();
    bool pathExists = false;
    bool isCloned = false;
    bool isAdmin = false;
    bool isSubscribed = false;

    // Fetch repository flags
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
        isSubscribed = IS_GROUP_SUBSCRIBED(flags);
    }

    if (hasPath) {
        QString cleanPath = QDir::cleanPath(path);
        pathExists = QDir(cleanPath).exists();
        if (pathExists && GitManager::isValidRepository(cleanPath.toStdString())) {
            isCloned = true;
        }
    }

    ui->mBtnPush->setEnabled(isAdmin);
    ui->mBtnPull->setEnabled(true);
    if (!isSubscribed) {
        ui->mBtnClone->setEnabled(false);
    } else {
        ui->mBtnClone->setEnabled(!isAdmin && !isCloned);
    }
    ui->mBtnCommit->setEnabled(hasPath && pathExists && isAdmin);

    ui->mBtnBrowse->setEnabled(isSubscribed || isAdmin);
    ui->mLocalPathEdit->setEnabled(isSubscribed || isAdmin);

    ui->mLblOwnerInfo->setVisible(isAdmin);
    ui->mLblSubscriberInfo->setVisible(isSubscribed && !isAdmin);

    populateCommitLog();

    if (ui->mCommitTable->rowCount() == 0) {
        ui->mBtnPush->setText(tr("Push & Publish"));
    } else {
        ui->mBtnPush->setText(tr("Push Local Commits"));
    }
}

void GitWidget::handleGitEvent(const RsGitEvent *e)
{
    if (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString()))
        return;

    if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED) {
        refresh();
    }
}

void GitWidget::saveRepoLocalPath(const QString &groupId, const QString &path)
{
    Settings->beginGroup("RetroGit_WorkingDirs");
    Settings->setValue(groupId, path);
    Settings->endGroup();
}

QString GitWidget::loadRepoLocalPath(const QString &groupId)
{
    Settings->beginGroup("RetroGit_WorkingDirs");
    QString path = Settings->value(groupId).toString();
    Settings->endGroup();
    return path;
}

void GitWidget::onBrowseClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"),
                                                 ui->mLocalPathEdit->text(),
                                                 QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        ui->mLocalPathEdit->setText(dir);
    }
}

void GitWidget::onLocalPathChanged(const QString &text)
{
    bool isAdmin = false;
    if (!mGroupId.isEmpty()) {
        std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
        std::vector<RsGitGroup> groups;
        if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
            uint32_t flags = groups[0].mMeta.mSubscribeFlags;
            isAdmin = IS_GROUP_ADMIN(flags);
        }
    }
    ui->mBtnCommit->setEnabled(!text.isEmpty() && isAdmin);

    if (mGroupId.isEmpty()) return;
    QString cleanText = QDir::cleanPath(text.trimmed());
    saveRepoLocalPath(mGroupId, cleanText);
    refresh();
    mMainWidget->updateDisplay();
}

void GitWidget::onCloneClicked()
{
    if (mGroupId.isEmpty()) return;

    QString localPath = ui->mLocalPathEdit->text().trimmed();
    if (localPath.isEmpty()) {
        QMessageBox::warning(this, tr("Error"), tr("Please select a local working directory first."));
        return;
    }

    RsGxsId creatorId;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    QString repoName = mGroupId;
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

    mMainWidget->logCloneAttempt(mGroupId, repoName, QString::fromStdString(creatorId.toStdString()), ownId, localPath);
}

void GitWidget::onPullClicked()
{
    if (mGroupId.isEmpty()) return;

    QString localPath = ui->mLocalPathEdit->text().trimmed();

    RsGxsId creatorId;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    QString repoName = mGroupId;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        creatorId = groups[0].mMeta.mAuthorId;
        repoName = QString::fromUtf8(groups[0].mGroupName.c_str());
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

    mMainWidget->logPullAttempt(mGroupId, repoName, QString::fromStdString(creatorId.toStdString()), ownId, localPath);
}

void GitWidget::onCommitClicked()
{
    if (mGroupId.isEmpty()) return;
    QString localPath = ui->mLocalPathEdit->text().trimmed();

    RsGxsId ownId;
    if (rsIdentity) {
        std::list<RsGxsId> ownIds;
        rsIdentity->getOwnIds(ownIds);
        if (!ownIds.empty()) {
            ownId = ownIds.front();
        }
    }

    if (ownId.isNull()) {
        QMessageBox::critical(this, tr("Commit Failed"), tr("You need a local GXS identity to commit changes. Please create one in the identities page."));
        return;
    }

    std::string authorName = ownId.toStdString();
    std::string authorEmail = "noreply@retroshare.net";
    RsIdentityDetails details;
    if (rsIdentity && rsIdentity->getIdDetails(ownId, details)) {
        authorName = details.mNickname;
        authorEmail = details.mNickname + "@" + ownId.toStdString();
    }

    // 1. Fetch branches from bare repository (source of truth for the project)
    std::string barePath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> branches;
    std::string currentBranch;
    GitManager::getBranches(barePath, branches, currentBranch);

    // 2. Fetch current active branch from the local working directory
    std::vector<std::string> localBranches;
    std::string localCurrentBranch;
    GitManager::getBranches(localPath.toStdString(), localBranches, localCurrentBranch);
    if (!localCurrentBranch.empty()) {
        currentBranch = localCurrentBranch;
    }

    QStringList qBranches;
    for (const auto& b : branches) {
        qBranches.append(QString::fromStdString(b));
    }
    // Also include any local-only branches if they exist
    for (const auto& b : localBranches) {
        QString qb = QString::fromStdString(b);
        if (!qBranches.contains(qb)) {
            qBranches.append(qb);
        }
    }

    GitCommitDialog diag(QString::fromStdString(authorName), QString::fromStdString(authorEmail),
                         ownId, qBranches, QString::fromStdString(currentBranch), this);
    if (diag.exec() == QDialog::Accepted) {
        QString msg = diag.getCommitMessage();
        QString name = diag.getAuthorName();
        QString email = diag.getAuthorEmail();
        QString targetBranch = diag.getTargetBranch();

        if (GitManager::commitChanges(localPath.toStdString(), msg.toStdString(),
                                      name.toStdString(), email.toStdString(),
                                      targetBranch.toStdString())) {
            
            // If the user selected a different branch than they were currently on, checkout that branch!
            if (!targetBranch.isEmpty() && targetBranch.toStdString() != localCurrentBranch) {
                GitManager::checkoutBranch(localPath.toStdString(), targetBranch.toStdString());
            }

            QMessageBox::information(this, tr("Commit Successful"), tr("Local changes committed successfully."));
            refresh();
            mMainWidget->updateDisplay();
        } else {
            QMessageBox::critical(this, tr("Commit Failed"), tr("Failed to commit local changes. Check file permissions or status."));
        }
    }
}

void GitWidget::onPushClicked()
{
    if (mGroupId.isEmpty()) return;
    QString localPath = ui->mLocalPathEdit->text().trimmed();
    std::string repoPath = localPath.toStdString();

    if (!GitManager::isValidRepository(repoPath)) {
        QMessageBox::critical(this, tr("Push Failed"), tr("Local path is not a valid git repository."));
        return;
    }

    std::string packfileData;
    std::map<std::string, std::string> refUpdates;
    if (GitManager::createPackfile(repoPath, packfileData, refUpdates)) {
        // Unpack locally immediately so the owner's sync repo is instantly updated!
        std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
        GitManager::unpackPackfile(bareRepoPath, packfileData, refUpdates);

        std::vector<RsGitUpdate> updates;
        if (rsGit) {
            rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
        }
        for (const auto &update : updates) {
            for (const auto &pair : refUpdates) {
                if (update.mRefUpdates.count(pair.first) && update.mRefUpdates.at(pair.first) == pair.second) {
                    QMessageBox::information(this, tr("Already Published"), tr("This commit history has already been published to GXS."));
                    return;
                }
            }
        }

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
        update.mMeta.mGroupId = RsGxsGroupId(mGroupId.toStdString());
        update.mRefUpdates = refUpdates;
        update.mMeta.mMsgName = QString("New Commit: %1").arg(QString::fromStdString(commitMsg)).toStdString();

        if (packfileData.size() <= 200000) {
            update.mPackfileData = packfileData;
            uint32_t token;
            if (rsGit) {
                rsGit->publishGitUpdate(token, update);
                QMessageBox::information(this, tr("Push Successful"), tr("Local commits published to GXS network!"));
            }
        } else {
            mMainWidget->hashAndPublishPackfile(mGroupId, packfileData, update);
        }
    } else {
        QMessageBox::critical(this, tr("Push Failed"), tr("Failed to open local Git repository or create packfile. Ensure the directory is a valid Git repository."));
    }
}

void GitWidget::onCommitSelectionChanged()
{
    QList<QTableWidgetItem *> selected = ui->mCommitTable->selectedItems();
    if (selected.isEmpty()) {
        mMainWidget->hideCommitDetails();
        return;
    }

    int row = selected.first()->row();
    QTableWidgetItem *hashItem = ui->mCommitTable->item(row, 0);
    if (!hashItem) {
        mMainWidget->hideCommitDetails();
        return;
    }

    QString fullHash = hashItem->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) {
        fullHash = hashItem->text();
    }

    // Determine current repo path
    std::string repoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    QString rawPath = ui->mLocalPathEdit->text().trimmed();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }

    mMainWidget->showCommitDetails(fullHash, QString::fromStdString(repoPath));
}

void GitWidget::onCommitTableContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = ui->mCommitTable->itemAt(pos);
    if (!item) return;

    int row = item->row();
    QTableWidgetItem *hashItem = ui->mCommitTable->item(row, 0);
    if (!hashItem) return;

    QString fullHash = hashItem->data(Qt::UserRole).toString();
    if (fullHash.isEmpty()) fullHash = hashItem->text();

    QMenu menu(this);
    QAction *actionDiff = nullptr;
    QAction *actionDiscard = nullptr;
    QAction *actionMarkRead = nullptr;

    if (fullHash == "LOCAL_CHANGES") {
        actionDiff = menu.addAction(tr("Show Diff of Local Changes"));
        actionDiscard = menu.addAction(tr("Discard Changes"));
    } else {
        actionDiff = menu.addAction(tr("Show Commit Diff"));
        actionMarkRead = menu.addAction(tr("Mark Repository As Read"));
    }

    QAction *selected = menu.exec(ui->mCommitTable->mapToGlobal(pos));
    if (!selected) return;

    if (selected == actionDiff) {
        mMainWidget->showDiffForCommit(fullHash);
    } else if (selected == actionDiscard) {
        QString localPath = ui->mLocalPathEdit->text().trimmed();
        if (!localPath.isEmpty()) {
            if (QMessageBox::question(this, tr("Discard Changes"),
                                      tr("Are you sure you want to discard all local uncommitted changes? This action cannot be undone."),
                                      QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                if (GitManager::discardLocalChanges(localPath.toStdString())) {
                    QMessageBox::information(this, tr("Success"), tr("Local uncommitted changes discarded successfully."));
                    refresh();
                    mMainWidget->updateDisplay();
                } else {
                    QMessageBox::critical(this, tr("Error"), tr("Failed to discard local changes. Check file permissions."));
                }
            }
        }
    } else if (selected == actionMarkRead) {
        mMainWidget->markRepositoryAsRead();
    }
}

void GitWidget::onCommitReadStatusToggled(const QString &msgIdStr, bool markRead)
{
    if (mGroupId.isEmpty()) return;
    RsGxsMessageId msgId(msgIdStr.toStdString());
    if (rsGit) {
        rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), msgId), markRead);
    }
    refresh();
}

void GitWidget::populateCommitLog()
{
    ui->mCommitTable->setRowCount(0);
    if (mGroupId.isEmpty()) return;

    std::string repoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    QString rawPath = ui->mLocalPathEdit->text().trimmed();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }

    std::string selectedBranch = "";
    if (mMainWidget) {
        CodeWidget *codeWidget = mMainWidget->getCodeWidget();
        if (codeWidget) {
            selectedBranch = codeWidget->getSelectedBranchOrTag().toStdString();
        }
    }

    std::vector<GitCommitInfo> commits;
    bool gotLog = GitManager::getCommitLog(repoPath, commits, selectedBranch);

    std::vector<GitLocalChange> localChanges;
    bool hasLocalChanges = false;
    bool statusOk = GitManager::getLocalChanges(repoPath, localChanges);
    if (statusOk && !localChanges.empty()) {
        hasLocalChanges = true;
    }

    std::set<std::string> localCommitShas;
    for (const auto &c : commits) {
        localCommitShas.insert(c.hash);
        localCommitShas.insert(c.full_hash);
    }

    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
    }

    std::set<std::string> allRepoCommitShas;
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    GitManager::getAllCommitShas(bareRepoPath, allRepoCommitShas);
    if (repoPath != bareRepoPath) {
        GitManager::getAllCommitShas(repoPath, allRepoCommitShas);
    }

    std::vector<RsGitUpdate> undownloadedUpdates;
    std::set<std::string> unreadCommitShas;
    std::map<std::string, std::pair<RsGxsMessageId, bool>> commitToMsgId;
    std::map<std::string, std::string> commitToMsgName;
    std::map<std::string, RsGxsId> commitToAuthorId;

    for (const auto &update : updates) {
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);
        bool isDownloaded = false;
        for (const auto &pair : update.mRefUpdates) {
            commitToMsgId[pair.second] = {update.mMeta.mMsgId, isUnread};
            commitToMsgName[pair.second] = update.mMeta.mMsgName;
            commitToAuthorId[pair.second] = update.mMeta.mAuthorId;
            if (isUnread) {
                unreadCommitShas.insert(pair.second);
            }
            if (localCommitShas.count(pair.second) || allRepoCommitShas.count(pair.second)) {
                isDownloaded = true;
            }
        }
        if (!isDownloaded) {
            undownloadedUpdates.push_back(update);
        }
    }

    int offset = hasLocalChanges ? 1 : 0;
    int totalRows = undownloadedUpdates.size() + commits.size() + offset;
    ui->mCommitTable->setRowCount(totalRows);

    if (hasLocalChanges) {
        QTableWidgetItem *itemHash = new QTableWidgetItem("*");
        itemHash->setData(Qt::UserRole, QString("LOCAL_CHANGES"));
        QFont font = itemHash->font();
        font.setBold(true);
        itemHash->setFont(font);

        QTableWidgetItem *itemMsg = new QTableWidgetItem(tr("Local changes (Uncommitted)"));
        itemMsg->setFont(font);
        itemMsg->setForeground(QBrush(QColor("#d35400")));

        QTableWidgetItem *itemAuth = new QTableWidgetItem("");
        QTableWidgetItem *itemDate = new QTableWidgetItem("");
        QTableWidgetItem *itemStatus = new QTableWidgetItem("-");
        QTableWidgetItem *itemAction = new QTableWidgetItem("-");

        ui->mCommitTable->setItem(0, 0, itemHash);
        ui->mCommitTable->setItem(0, 1, itemMsg);
        ui->mCommitTable->setItem(0, 2, itemAuth);
        ui->mCommitTable->setItem(0, 3, itemDate);
        ui->mCommitTable->setItem(0, 4, itemStatus);
        ui->mCommitTable->setItem(0, 5, itemAction);
    }

    // Get creator for pull
    RsGxsId creatorId;
    QString repoName;
    bool isAdmin = false;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
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

    // 1. Populate undownloaded updates
    for (int i = 0; i < (int)undownloadedUpdates.size(); ++i) {
        const auto &update = undownloadedUpdates[i];
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);

        QString targetSha = "";
        if (!update.mRefUpdates.empty()) {
            targetSha = QString::fromStdString(update.mRefUpdates.begin()->second);
        }

        QTableWidgetItem *itemHash = new QTableWidgetItem(targetSha.left(8));
        itemHash->setData(Qt::UserRole, targetSha);

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

        QTableWidgetItem *itemAuth = new QTableWidgetItem();
        if (update.mMeta.mAuthorId.isNull()) {
            itemAuth->setText(tr("Anonymous"));
        }

#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
        QString dateStr = QLocale().toString(QDateTime::fromSecsSinceEpoch(update.mMeta.mPublishTs), QLocale::ShortFormat);
#else
        QString dateStr = QDateTime::fromTime_t(update.mMeta.mPublishTs).toString(Qt::SystemLocaleShortDate);
#endif
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
        ui->mCommitTable->setItem(rowIdx, 0, itemHash);
        ui->mCommitTable->setItem(rowIdx, 1, itemMsg);
        ui->mCommitTable->setItem(rowIdx, 2, itemAuth);
        if (!update.mMeta.mAuthorId.isNull()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mCommitTable);
            authorWidget->setId(update.mMeta.mAuthorId);
            ui->mCommitTable->setCellWidget(rowIdx, 2, authorWidget);
        }
        ui->mCommitTable->setItem(rowIdx, 3, itemDate);

        QPushButton *btnStatus = new QPushButton(isUnread ? tr("Mark Read") : tr("Mark Unread"), ui->mCommitTable);
        if (isUnread) {
            btnStatus->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
        } else {
            btnStatus->setStyleSheet("QPushButton { color: #7f8c8d; }");
        }
        QString msgIdStr = QString::fromStdString(update.mMeta.mMsgId.toStdString());
        connect(btnStatus, &QPushButton::clicked, [this, msgIdStr, isUnread]() {
            onCommitReadStatusToggled(msgIdStr, isUnread);
        });
        ui->mCommitTable->setCellWidget(rowIdx, 4, btnStatus);

        if (!creatorId.isNull()) {
            QPushButton *btnPull = new QPushButton(tr("Pull"), ui->mCommitTable);
            btnPull->setStyleSheet("QPushButton { font-weight: bold; color: #2980b9; }");
            QString localPath = ui->mLocalPathEdit->text().trimmed();
            connect(btnPull, &QPushButton::clicked, [this, creatorId, ownId, localPath, repoName]() {
                mMainWidget->logPullAttempt(mGroupId, repoName, QString::fromStdString(creatorId.toStdString()), ownId, localPath);
            });
            ui->mCommitTable->setCellWidget(rowIdx, 5, btnPull);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }

    // 2. Populate downloaded commits
    int undownloadedCount = undownloadedUpdates.size();
    for (int i = 0; i < (int)commits.size(); i++) {
        int rowIdx = undownloadedCount + i + offset;

        QTableWidgetItem *itemHash = new QTableWidgetItem(QString::fromStdString(commits[i].hash));
        itemHash->setData(Qt::UserRole, QString::fromStdString(commits[i].full_hash));

        RsGxsId gxsAuthorId;
        auto itAuth = commitToAuthorId.find(commits[i].full_hash);
        if (itAuth == commitToAuthorId.end()) {
            itAuth = commitToAuthorId.find(commits[i].hash);
        }
        if (itAuth != commitToAuthorId.end()) {
            gxsAuthorId = itAuth->second;
        }

        QTableWidgetItem *itemAuth = nullptr;
        if (!gxsAuthorId.isNull()) {
            itemAuth = new QTableWidgetItem();
        } else {
            itemAuth = new QTableWidgetItem(QString::fromStdString(commits[i].author));
            itemAuth->setIcon(FilesDefs::getIconFromQtResourcePath(":/icons/svg/people2.svg"));
        }

        QTableWidgetItem *itemDate = new QTableWidgetItem(QString::fromStdString(commits[i].date));

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

        ui->mCommitTable->setItem(rowIdx, 0, itemHash);
        ui->mCommitTable->setItem(rowIdx, 2, itemAuth);
        if (!gxsAuthorId.isNull()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mCommitTable);
            authorWidget->setId(gxsAuthorId);
            ui->mCommitTable->setCellWidget(rowIdx, 2, authorWidget);
        }
        ui->mCommitTable->setItem(rowIdx, 3, itemDate);

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
            ui->mCommitTable->setItem(rowIdx, 1, itemMsg);
        } else {
            QTableWidgetItem *itemMsg = new QTableWidgetItem(textToShow);
            if (isUnread) {
                QFont font = itemMsg->font();
                font.setBold(true);
                itemMsg->setFont(font);
            }
            ui->mCommitTable->setItem(rowIdx, 1, itemMsg);
        }

        if (hasGxsMsg) {
            QPushButton *btnStatus = new QPushButton(isUnread ? tr("Mark Read") : tr("Mark Unread"), ui->mCommitTable);
            if (isUnread) {
                btnStatus->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            } else {
                btnStatus->setStyleSheet("QPushButton { color: #7f8c8d; }");
            }
            QString msgIdStr = QString::fromStdString(gxsMsgId.toStdString());
            connect(btnStatus, &QPushButton::clicked, [this, msgIdStr, isUnread]() {
                onCommitReadStatusToggled(msgIdStr, isUnread);
            });
            ui->mCommitTable->setCellWidget(rowIdx, 4, btnStatus);
        } else {
            QTableWidgetItem *itemStatus = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 4, itemStatus);
        }

        if (isAdmin && !hasGxsMsg && i == 0) {
            QPushButton *btnPushRow = new QPushButton(tr("Push"), ui->mCommitTable);
            btnPushRow->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            connect(btnPushRow, &QPushButton::clicked, this, &GitWidget::onPushClicked);
            ui->mCommitTable->setCellWidget(rowIdx, 5, btnPushRow);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }

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

    ui->mBtnPush->setEnabled(isAdmin);
    bool hasUndownloaded = (undownloadedUpdates.size() > 0);
    ui->mBtnPull->setEnabled(isCloned && hasUndownloaded);
    ui->mBtnClone->setEnabled(!isAdmin && !isCloned);
    ui->mBtnCommit->setEnabled(hasPath && pathExists && isAdmin && hasLocalChanges);
}

QString GitWidget::getLocalPath() const
{
    return ui->mLocalPathEdit->text().trimmed();
}
