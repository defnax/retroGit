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

    // Styling Title & Status Badge (GitHub-like)
    ui->mLblTitle->setStyleSheet("font-size: 20px; font-weight: bold; color: #24292f;");
    
    // Boxed comment styling
    ui->commentFrame->setStyleSheet("QFrame#commentFrame { border: 1px solid #d0d7de; border-radius: 6px; background-color: white; }");
    ui->mLblCommentHeader->setStyleSheet("QLabel#mLblCommentHeader { background-color: #f6f8fa; border-bottom: 1px solid #d0d7de; padding: 8px; font-weight: bold; color: #57606a; border-top-left-radius: 5px; border-top-right-radius: 5px; }");
    ui->mTxtCommentBody->setStyleSheet("QTextEdit#mTxtCommentBody { border: none; background-color: transparent; padding: 12px; font-size: 14px; color: #24292f; }");
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
                break;
            }
        }
    }
}

void PullRequestDetailsWidget::refresh()
{
    loadPRDetails();
    if (!mLoaded) return;

    // 1. Populate Header info
    ui->mLblTitle->setText(QString::fromStdString(mPR.mTitle));
    
    // Status Badge
    bool isOpen = (mPR.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (mPR.mStatus == 0);
    if (isOpen) {
        std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
        if (GitManager::isBranchMerged(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch)) {
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
        .arg(QString::fromStdString(mPR.mSourceBranch));
    ui->mLblDescriptionMeta->setText(descMetaStr);

    populateConversationTab();
    populateFilesChangedTab();
}

void PullRequestDetailsWidget::populateConversationTab()
{
    // A. Description Comment
    QString authorStr = QString::fromStdString(mPR.mMeta.mAuthorId.toStdString().substr(0, 8));
    QString dateStr = QDateTime::fromTime_t(mPR.mMeta.mPublishTs).toString("yyyy-MM-dd HH:mm");
    ui->mLblCommentHeader->setText(tr("%1 commented on %2").arg(authorStr).arg(dateStr));
    
    QString desc = QString::fromStdString(mPR.mDescription);
    ui->mTxtCommentBody->setHtml(desc.isEmpty() ? tr("<i>No description provided.</i>") : desc.toHtmlEscaped().replace("\n", "<br>"));

    // B. PR Commits List
    ui->mCommitsList->clear();
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<GitCommitInfo> commits;
    
    if (GitManager::getCommitsBetweenBranches(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch, commits)) {
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
        if (GitManager::isBranchMerged(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch)) {
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
        ui->mMergePanel->setStyleSheet("QFrame#mMergePanel { border: 1px solid #d0d7de; border-radius: 6px; background-color: #f6f8fa; }");
        ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-pull-request-green.png").pixmap(badgeSize, badgeSize));
        
        if (isAdmin) {
            ui->mLblMergeStatus->setText(tr("<b>This branch has no conflicts.</b><br>Merging can be performed automatically."));
            ui->mBtnMerge->setVisible(true);
            ui->mBtnMerge->setEnabled(true);
            ui->mBtnMerge->setText(tr("Merge pull request"));
            ui->mBtnMerge->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 6px 16px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                        "QPushButton:hover { background-color: #2c974b; }"
                                        "QPushButton:pressed { background-color: #298e46; }");
        } else {
            ui->mLblMergeStatus->setText(tr("<b>Pending Admin approval.</b><br>Only repository administrators can merge this pull request."));
            ui->mBtnMerge->setVisible(false);
        }
    } else {
        ui->mBtnMerge->setVisible(false);
        if (mPR.mStatus == 1) {
            ui->mMergePanel->setStyleSheet("QFrame#mMergePanel { border: 1px solid #ffc1c0; border-radius: 6px; background-color: #ffebe9; }");
            ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-pull-request-closed.png").pixmap(badgeSize, badgeSize));
            ui->mLblMergeStatus->setText(tr("<span style='color: #cf222e; font-weight: bold;'>Pull request successfully closed.</span><br>Changes were not merged."));
        } else {
            ui->mMergePanel->setStyleSheet("QFrame#mMergePanel { border: 1px solid #d0d7de; border-radius: 6px; background-color: #fbe8ff; }");
            ui->mLblMergeIcon->setPixmap(QIcon(":/images/git-merge-pink.png").pixmap(badgeSize, badgeSize));
            ui->mLblMergeStatus->setText(tr("<span style='color: #8250df; font-weight: bold;'>Pull request successfully merged.</span><br>Changes are now integrated into the target branch."));
        }
    }
}

void PullRequestDetailsWidget::populateFilesChangedTab()
{
    ui->mFilesTree->clear();
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> changedFiles;

    if (GitManager::getPRChangedFiles(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch, changedFiles)) {
        for (const auto &file : changedFiles) {
            QString filePath = QString::fromStdString(file);
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
            fileItem->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_FileIcon));
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
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());

    // Perform fast-forward merge
    if (GitManager::mergeBranch(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch)) {
        // Mark PR as processed in GXS exchange
        uint32_t token;
        if (rsGit) {
            rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), mMsgId), true);
            uint32_t statusToken;
            rsGit->closePullRequest(statusToken, RsGxsGroupId(mGroupId.toStdString()), mMsgId, 2);
        }

        // Publish the merged commits as a repository update to the GXS network
        std::string packfileData;
        std::map<std::string, std::string> refUpdates;
        if (GitManager::createPackfileForRef(bareRepoPath, mPR.mTargetBranch, packfileData, refUpdates)) {
            RsGitUpdate update;
            update.mMeta.mGroupId = RsGxsGroupId(mGroupId.toStdString());
            update.mRefUpdates = refUpdates;
            update.mMeta.mMsgName = QString("Merge PR: %1").arg(QString::fromStdString(mPR.mTitle)).toStdString();

            if (packfileData.size() <= 200000) {
                update.mPackfileData = packfileData;
                uint32_t publishToken;
                if (rsGit) {
                    rsGit->publishGitUpdate(publishToken, update);
                }
            } else if (mMainWidget) {
                mMainWidget->hashAndPublishPackfile(mGroupId, packfileData, update);
            }
        }

        QMessageBox::information(this, tr("Success"), tr("Pull request merged successfully and updates published to the network."));

        // Automatically pull the merged changes into the local working directory if configured
        if (mMainWidget) {
            QString localPath = mMainWidget->getLocalPath();
            if (!localPath.isEmpty()) {
                GitManager::pullRepository(localPath.toStdString());
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

    if (!GitManager::getPRFileDiff(bareRepoPath, mPR.mSourceBranch, mPR.mTargetBranch, filePath.toStdString(), diffLines)) {
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
