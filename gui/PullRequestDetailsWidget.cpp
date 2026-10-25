/*******************************************************************************
 * gui/PullRequestDetailsWidget.cpp                                            *
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
 ********************************************************************************/

#include "PullRequestDetailsWidget.h"
#include "ui_PullRequestDetailsWidget.h"
#include "MainWidget.h"
#include "services/GitManager.h"

#include <QMessageBox>
#include <QTimer>
#include "gui/settings/rsharesettings.h"
#include <QDateTime>
#include <QPixmap>
#include <QIcon>
#include <QColor>
#include <QBrush>
#include <QPalette>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QApplication>
#include <QStyle>
#include <QScrollBar>
#include <QFile>
#include <QHBoxLayout>
#include <retroshare/rsinit.h>

extern RsGit *rsGit;
#include <retroshare/rsidentity.h>
extern RsIdentity *rsIdentity;

PullRequestDetailsWidget::PullRequestDetailsWidget(const QString &groupId, const RsGxsMessageId &msgId, MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PullRequestDetailsWidget)
    , mMainWidget(mainWidget)
    , mGroupId(groupId)
    , mMsgId(msgId)
    , mLoaded(false)
{
    ui->setupUi(this);

    if (QApplication::palette().base().color().lightness() >= 128) {
        QFile styleFile(":/retrogit.qss");
        if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString styleSheet = QString::fromUtf8(styleFile.readAll());
            ui->mMergePanel->setStyleSheet(styleSheet);
            ui->commentFrame->setStyleSheet(styleSheet);
            ui->mLblTitle->setStyleSheet(styleSheet);
        }
    }

    // Set tab icons
    ui->mPRTabs->setTabIcon(0, QIcon(":/images/comment-discussion-16_.png"));
    ui->mPRTabs->setTabIcon(1, QIcon(":/images/commit-24_.png"));
    ui->mPRTabs->setTabIcon(2, QIcon(":/images/file-diff-24_.png"));

    // Styling Title & Status Badge
    //ui->mLblTitle->setStyleSheet("font-size: 20px; font-weight: bold; color: palette(text);");

    // ScrollArea styling
    ui->scrollArea->setStyleSheet("QScrollArea { background-color: transparent; }");

    // Files Changed Splitter
    ui->splitter->setStretchFactor(0, 0); // List is fixed
    ui->splitter->setStretchFactor(1, 1); // Diff browser expands
    QList<int> splitterSizes;
    splitterSizes << 220 << 600;
    ui->splitter->setSizes(splitterSizes);

    ui->mFilesTree->setHeaderHidden(true);

    // Connections
    connect(ui->mBtnMerge, &QPushButton::clicked, this, &PullRequestDetailsWidget::onMergeClicked);
    connect(ui->mBtnClose, &QPushButton::clicked, this, &PullRequestDetailsWidget::onCloseClicked);
    connect(ui->mFilesTree, &QTreeWidget::itemSelectionChanged, this, &PullRequestDetailsWidget::onFileSelectionChanged);

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (isVisible() && mLoaded && !mReviewReady) refresh();
    });
    timer->start(5000);
    refresh();
}

PullRequestDetailsWidget::~PullRequestDetailsWidget()
{
    delete ui;
}

void PullRequestDetailsWidget::loadPRDetails()
{
    if (mGroupId.isEmpty()) return;

    std::vector<RsGitPullRequest> pullRequests;
    if (rsGit && rsGit->getPullRequests(RsGxsGroupId(mGroupId.toStdString()), pullRequests)) {
        for (const auto &pr : pullRequests) {
            if (pr.mMeta.mMsgId == mMsgId) {
                mPR = pr;
                mLoaded = true;
                if (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                    uint32_t token;
                    rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), pr.mMeta.mMsgId), true);
                }
                break;
            }
        }
    }
}

void PullRequestDetailsWidget::refresh()
{
    loadPRDetails();
    if (!mLoaded) return;
    mReviewReady = rsGit && rsGit->preparePullRequest(mPR);

    // 1. Populate Header info
    ui->mLblTitle->setText(QString::fromStdString(mPR.mTitle));
    
    // Status Badge
    bool isOpen = (mPR.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (mPR.mStatus == 0);
    if (isOpen) {
        std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
        if (GitManager::isBranchMerged(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch)) {
            isOpen = false;
            if (rsGit) {
                uint32_t token;
                rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), mMsgId), true);
            }
        }
    } else if (mPR.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) {
        if (rsGit) {
            uint32_t token;
            rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), mMsgId), true);
        }
    }
    if (isOpen) {
        ui->mLblStatus->setText(tr(" Open "));
        ui->mLblStatus->setStyleSheet("background-color: #2da44e; color: white; border-radius: 12px; font-weight: bold; font-size: 12px; padding: 4px 10px;");
    } else {
        if (mPR.mStatus == 1) {
            ui->mLblStatus->setText(tr(" Closed "));
            ui->mLblStatus->setStyleSheet("background-color: #cf222e; color: white; border-radius: 12px; font-weight: bold; font-size: 12px; padding: 4px 10px;");
        } else {
            ui->mLblStatus->setText(tr(" Merged "));
            ui->mLblStatus->setStyleSheet("background-color: #8250df; color: white; border-radius: 12px; font-weight: bold; font-size: 12px; padding: 4px 10px;");
        }
    }

    QString descMetaStr = tr("<b>%1</b> wants to merge into <code>%2</code> from <code>%3</code>")
        .arg(QString::fromStdString(mPR.mMeta.mAuthorId.toStdString().substr(0, 8)))
        .arg(QString::fromStdString(mPR.mTargetBranch))
        .arg(QString::fromStdString(mPR.mSourceGroupId.isNull() ? mPR.mSourceBranch :
            mPR.mSourceGroupId.toStdString().substr(0, 8) + ":" + mPR.mSourceBranch).toHtmlEscaped());
    ui->mLblDescriptionMeta->setText(descMetaStr);

    populateConversationTab();
    populateFilesChangedTab();
}

void PullRequestDetailsWidget::populateConversationTab()
{
    // Recreate the immutable review ref when reopening a merged PR. The target
    // branch may have moved, but the PR snapshot must remain available.
    if (rsGit) rsGit->preparePullRequest(mPR);
    // A. Description Comment
    QString authorStr = QString::fromStdString(mPR.mMeta.mAuthorId.toStdString().substr(0, 8));
    if (rsIdentity && !mPR.mMeta.mAuthorId.isNull()) {
        RsIdentityDetails identity;
        if (rsIdentity->getIdDetails(mPR.mMeta.mAuthorId, identity) && !identity.mNickname.empty())
            authorStr = QString::fromStdString(identity.mNickname);
    }
    QString dateStr = QDateTime::fromTime_t(mPR.mMeta.mPublishTs).toString("yyyy-MM-dd HH:mm");
    ui->mLblCommentHeader->setText(tr("%1 commented on %2").arg(authorStr).arg(dateStr));
    
    QString desc = QString::fromStdString(mPR.mDescription);
    ui->mTxtCommentBody->setHtml(desc.isEmpty() ? tr("<i>No description provided.</i>") : desc.toHtmlEscaped().replace("\n", "<br>"));

    // B. PR Commits List
    ui->mCommitsList->clear();
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<GitCommitInfo> commits;
    
    if (GitManager::getCommitsBetweenBranches(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch, commits)) {
        for (const auto &c : commits) {
            QString commitText = QString("[%1] %2 - by %3 (%4)")
                .arg(QString::fromStdString(c.hash))
                .arg(QString::fromStdString(c.message))
                .arg(QString::fromStdString(c.author))
                .arg(QString::fromStdString(c.date));
            QListWidgetItem *item = new QListWidgetItem(QIcon(":/images/git.png"), commitText, ui->mCommitsList);
            ui->mCommitsList->addItem(item);
        }
    }

    // C. Merge Status Panel
    bool isOpen = (mPR.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (mPR.mStatus == 0);
    if (isOpen) {
        std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
        if (GitManager::isBranchMerged(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch)) {
            isOpen = false;
        }
    }
    
    // Check if user is admin
    bool isAdmin = false;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit && rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
    }

    // Check if user can close the pull request
    bool canClose = false;
    if (isOpen) {
        if (isAdmin) {
            canClose = true;
        } else if (rsIdentity && rsIdentity->isOwnId(mPR.mMeta.mAuthorId)) {
            canClose = true;
        }
    }
    ui->mBtnClose->setVisible(canClose);
    if (canClose) {
        ui->mBtnClose->setStyleSheet("QPushButton { background-color: #f6f8fa; color: #cf222e; border-radius: 6px; padding: 6px 16px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                    "QPushButton:hover { background-color: #f3f4f6; }"
                                    "QPushButton:pressed { background-color: #e5e7eb; }");
    }

    int badgeSize = 24;
    if (isOpen) {
        ui->mMergePanel->setProperty("mergeStatus", "open");
        ui->mMergePanel->style()->unpolish(ui->mMergePanel);
        ui->mMergePanel->style()->polish(ui->mMergePanel);
        ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-pull-request-green.png").pixmap(badgeSize, badgeSize));
        
        if (isAdmin) {
            const bool ready = mReviewReady;
            std::string mergeReason;
            const bool mergeable = ready && GitManager::canFastForward(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch, &mergeReason);
            ui->mLblMergeStatus->setText(!ready
                ? tr("Waiting for the proposed commits to download. Keep this tab open.")
                : mergeable ? tr("The reviewed commit can be merged without discarding target history.")
                : QString::fromStdString(mergeReason));
            ui->mBtnMerge->setVisible(true);
            ui->mBtnMerge->setEnabled(mergeable);
            ui->mBtnMerge->setText(tr("Merge pull request"));
            ui->mBtnMerge->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 6px 16px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                        "QPushButton:hover { background-color: #2c974b; }"
                                        "QPushButton:pressed { background-color: #298e46; }"
                                        "QPushButton:disabled { background-color: #e1e4e8; color: #6a737d; border-color: #d1d5da; }");
        } else {
            ui->mLblMergeStatus->setText(tr("<b>Pending Admin approval.</b><br>Only repository administrators can merge this pull request."));
            ui->mBtnMerge->setVisible(false);
        }
    } else {
        ui->mBtnMerge->setVisible(false);
        if (mPR.mStatus == 1) {
            ui->mMergePanel->setProperty("mergeStatus", "closed");
            ui->mMergePanel->style()->unpolish(ui->mMergePanel);
            ui->mMergePanel->style()->polish(ui->mMergePanel);
            ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-pull-request-closed.png").pixmap(badgeSize, badgeSize));
            ui->mLblMergeStatus->setText(tr("<span style='color: #cf222e; font-weight: bold;'>Pull request successfully closed.</span><br>Changes were not merged."));
        } else {
            ui->mMergePanel->setProperty("mergeStatus", "merged");
            ui->mMergePanel->style()->unpolish(ui->mMergePanel);
            ui->mMergePanel->style()->polish(ui->mMergePanel);
            ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-merge-pink.png").pixmap(badgeSize, badgeSize));
            ui->mLblMergeStatus->setText(tr("<span style='color: #8250df; font-weight: bold;'>Pull request successfully merged.</span><br>Changes are now integrated into the target branch."));
        }
    }
}

void PullRequestDetailsWidget::populateFilesChangedTab()
{
    if (rsGit) rsGit->preparePullRequest(mPR);
    ui->mFilesTree->clear();
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<GitCommitFileChange> changedFiles;

    if (GitManager::getPRChangedFiles(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch, changedFiles)) {
        for (const auto &file : changedFiles) {
            QString filePath = QString::fromStdString(file.path);
            QStringList parts = filePath.split('/');
            
            QTreeWidgetItem *parent = nullptr;
            for (int i = 0; i < parts.size() - 1; ++i) {
                QString dirName = parts[i];
                QTreeWidgetItem *found = nullptr;
                
                int childCount = parent ? parent->childCount() : ui->mFilesTree->topLevelItemCount();
                for (int j = 0; j < childCount; ++j) {
                    QTreeWidgetItem *child = parent ? parent->child(j) : ui->mFilesTree->topLevelItem(j);
                    if (child->text(0) == dirName && child->data(0, Qt::UserRole).toString().isEmpty()) {
                        found = child;
                        break;
                    }
                }
                
                if (!found) {
                    found = new QTreeWidgetItem();
                    found->setText(0, dirName);
                    found->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
                    if (parent) {
                        parent->addChild(found);
                    } else {
                        ui->mFilesTree->addTopLevelItem(found);
                    }
                }
                parent = found;
            }
            
            QTreeWidgetItem *fileItem = new QTreeWidgetItem();
            fileItem->setText(0, parts.last());
            if (file.status == '+' || file.status == '?') {
                fileItem->setIcon(0, QIcon(":/images/file-added-16_.png"));
            } else if (file.status == '~') {
                fileItem->setIcon(0, QIcon(":/images/file-diff-2.png"));
            } else {
                fileItem->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
            }
            fileItem->setData(0, Qt::UserRole, filePath);
            
            if (parent) {
                parent->addChild(fileItem);
            } else {
                ui->mFilesTree->addTopLevelItem(fileItem);
            }
        }
        
        ui->mFilesTree->expandAll();
        selectFirstFileItem(ui->mFilesTree->invisibleRootItem());
    }
}

bool PullRequestDetailsWidget::selectFirstFileItem(QTreeWidgetItem *item)
{
    if (!item) return false;
    
    QString filePath = item->data(0, Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        ui->mFilesTree->setCurrentItem(item);
        return true;
    }
    
    for (int i = 0; i < item->childCount(); ++i) {
        if (selectFirstFileItem(item->child(i))) {
            return true;
        }
    }
    return false;
}

void PullRequestDetailsWidget::onMergeClicked()
{
    loadPRDetails();
    std::vector<RsGitGroup> groups;
    if (!rsGit || !mLoaded || mPR.mStatus != 0 ||
        !rsGit->getGroups({RsGxsGroupId(mGroupId.toStdString())}, groups) || groups.empty() ||
        !IS_GROUP_ADMIN(groups.front().mMeta.mSubscribeFlags) || !rsGit->preparePullRequest(mPR)) return;

    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());

    // Prepare the publication before changing the target so canceled/failed hashing
    // cannot leave a merged PR with no transferable commits.
    std::string pack;
    std::map<std::string, std::string> refs;
    if (!GitManager::createPackfileForRef(bareRepoPath, mPR.sourceRef(), pack, refs)) return;
    RsGitUpdate update;
    update.mMeta.mGroupId = RsGxsGroupId(mGroupId.toStdString());
    update.mMeta.mAuthorId = groups.front().mMeta.mAuthorId;
    size_t prNumber = 1;
    std::vector<RsGitPullRequest> allPullRequests;
    if (rsGit->getPullRequests(RsGxsGroupId(mGroupId.toStdString()), allPullRequests)) {
        for (size_t i = 0; i < allPullRequests.size(); ++i)
            if (allPullRequests[i].mMeta.mMsgId == mPR.mMeta.mMsgId) { prNumber = i + 1; break; }
    }
    update.mMeta.mMsgName = "Merge PR #" + std::to_string(prNumber) + ": " + mPR.mTitle;
    const auto snapshot = refs.find(mPR.sourceRef().rfind("refs/", 0) == 0 ? mPR.sourceRef() : "refs/heads/" + mPR.sourceRef());
    if (snapshot == refs.end()) return;
    update.mRefUpdates["refs/heads/" + mPR.mTargetBranch] = snapshot->second;
    if (pack.size() <= 200000) update.mPackfileData = pack;
    else {
        RsGxsFile attachment;
        if (!mMainWidget || !mMainWidget->preparePackAttachment(mGroupId, pack, attachment)) return;
        update.mFiles.push_back(attachment);
    }
    if (GitManager::mergeBranch(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch)) {
        uint32_t token;
        if (!rsGit->publishGitUpdate(token, update)) {
            QMessageBox::warning(this, tr("Publication Failed"), tr("The merge exists locally but could not be queued for publication. Publish the target branch before closing this request."));
            return;
        }
        uint32_t statusToken;
        if (rsGit->closePullRequest(statusToken, RsGxsGroupId(mGroupId.toStdString()), mMsgId, 2))
            rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), mMsgId), true);
        QMessageBox::information(this, tr("Pull Request Merged"), tr("The reviewed commit was merged and the repository update was queued for publication."));

        // Automatically pull the merged changes into the local working directory if configured
        if (mMainWidget) {
            Settings->beginGroup("RetroGit_WorkingDirs");
            QString localPath = Settings->value(mGroupId).toString();
            Settings->endGroup();
            if (!localPath.isEmpty()) {
                std::string pullError;
                if (!GitManager::pullRepository(localPath.toStdString(), bareRepoPath, &pullError, mPR.mTargetBranch))
                    QMessageBox::warning(this, tr("Merged; Working Folder Not Updated"), QString::fromStdString(pullError));
                mMainWidget->refreshGitWidget();
            }
        }

        mLoaded = false; // Force reload
        refresh();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Merge failed. Ensure branches exist and are compatible."));
    }
}

void PullRequestDetailsWidget::onFileSelectionChanged()
{
    QTreeWidgetItem *selected = ui->mFilesTree->currentItem();
    if (!selected) {
        ui->mDiffBrowser->clear();
        return;
    }

    QString filePath = selected->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) {
        return;
    }
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<GitDiffLine> diffLines;

    if (!GitManager::getPRFileDiff(bareRepoPath, mPR.sourceRef(), mPR.mTargetBranch, filePath.toStdString(), diffLines)) {
        ui->mDiffBrowser->setHtml(tr("<i>Failed to load diff for %1</i>").arg(filePath.toHtmlEscaped()));
        return;
    }

    // Now construct the HTML for the diff view
    QString html = "<html><head><style>";
    html += "body { background-color: #ffffff; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; font-size: 12px; margin: 10px; }";
    html += ".header-banner { background-color: #f6f8fa; font-weight: bold; font-size: 13px; color: #24292f; padding: 8px 12px; margin-bottom: 10px; border-radius: 6px; border: 1px solid #d0d7de; }";
    html += ".file-header { font-weight: bold; background-color: #24292f; color: #ffffff; padding: 6px 10px; margin-top: 15px; margin-bottom: 0px; border-radius: 6px 6px 0 0; font-size: 12px; }";
    html += ".file-header-detail { background-color: #f6f8fa; color: #57606a; padding: 4px 10px; font-size: 11px; margin-bottom: 5px; border-bottom: 1px solid #d0d7de; }";
    html += ".hunk-header { font-weight: bold; color: #0969da; font-size: 12px; margin-top: 10px; padding: 6px 10px; background-color: #ddf4ff; border: 1px solid #d0d7de; border-bottom: none; border-radius: 6px 6px 0 0; }";
    html += ".hunk-card { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 0 0 6px 6px; padding: 6px; margin-bottom: 15px; }";
    html += ".line { white-space: pre-wrap; padding: 1px 4px; margin: 0; line-height: 1.4; font-size: 12px; font-family: 'DejaVu Sans Mono', 'Consolas', 'Courier New', monospace; }";
    html += ".line-add { background-color: #dafbe1; color: #1a7f37; }";
    html += ".line-del { background-color: #ffebe9; color: #cf222e; }";
    html += ".line-ctx { color: #24292f; }";
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
                html += "<div class='hunk-card'>";
                inHunk = true;
            }
            if (line.origin == '+') {
                html += "<pre class='line line-add'>+ " + escapedText + "</pre>";
            } else if (line.origin == '-') {
                html += "<pre class='line line-del'>- " + escapedText + "</pre>";
            } else {
                html += "<pre class='line line-ctx'>  " + escapedText + "</pre>";
            }
        }
    }

    if (inHunk) {
        html += "</div>"; // Close final hunk card
    }

    html += "</body></html>";
    ui->mDiffBrowser->setHtml(html);
}

void PullRequestDetailsWidget::onCloseClicked()
{
    if (QMessageBox::question(this, tr("Close Pull Request"),
                              tr("Are you sure you want to close this pull request?"),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    uint32_t token;
    if (rsGit && rsGit->closePullRequest(token, RsGxsGroupId(mGroupId.toStdString()), mMsgId, 1)) {
        rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), mMsgId), true);
        QMessageBox::information(this, tr("Success"), tr("Pull request closed successfully."));
        mLoaded = false; // Force reload
        refresh();
    } else {
        QMessageBox::critical(this, tr("Error"), tr("Failed to close pull request."));
    }
}
