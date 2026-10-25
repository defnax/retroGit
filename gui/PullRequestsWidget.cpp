/*******************************************************************************
 * gui/PullRequestsWidget.cpp                                                  *
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

#include "PullRequestsWidget.h"
#include "gui/settings/rsharesettings.h"
#include <QInputDialog>
#include "ui_PullRequestsWidget.h"
#include "MainWidget.h"
#include "GitNewPRDialog.h"
#include "services/GitManager.h"
#include <util/rsthreads.h>

#include <QTableWidget>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QIcon>
#include <QFont>
#include <QBrush>
#include <QColor>
#include <QHBoxLayout>

extern RsGit *rsGit;
extern RsIdentity *rsIdentity;

PullRequestsWidget::PullRequestsWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PullRequestsWidget)
    , mMainWidget(mainWidget)
    , mGroupId(groupId)
{
    ui->setupUi(this);

    // New PR button styling (GitHub-like green)
    ui->mBtnNewPR->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 6px 12px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                "QPushButton:hover { background-color: #2c974b; }"
                                "QPushButton:pressed { background-color: #298e46; }");

    // Search bar styling
    ui->mSearchEdit->setStyleSheet("QLineEdit { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px; font-size: 13px; }"
                                  "QLineEdit:focus { border: 1px solid #0969da; }");

    // Configure filter buttons
    ui->mBtnOpenFilter->setFlat(true);
    ui->mBtnClosedFilter->setFlat(true);

    // Configure PR Table to look like GitHub list
    ui->mPRTable->setColumnCount(4);
    ui->mPRTable->setShowGrid(false);
    ui->mPRTable->verticalHeader()->setVisible(false);
    ui->mPRTable->verticalHeader()->setDefaultSectionSize(54); // Ensure enough height for 2-line text, avatar, and buttons
    ui->mPRTable->horizontalHeader()->setVisible(false);
    ui->mPRTable->setColumnWidth(0, 40); // Compact column for status icon
    ui->mPRTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); // Title and details stretch
    ui->mPRTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents); // Size to author ID/avatar length
    ui->mPRTable->setColumnWidth(3, 110); // Proper width for Merge button and status labels
    ui->mPRTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mPRTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->mPRTable->setStyleSheet("QTableWidget { border: 1px solid palette(mid); border-radius: 6px; background-color: transparent; color: palette(text); }"
                                "QTableWidget::item { border-bottom: 1px solid #d0d7de; }");

    connect(ui->mBtnNewPR, &QPushButton::clicked, this, &PullRequestsWidget::onNewPRClicked);
    connect(ui->mSearchEdit, &QLineEdit::textChanged, this, &PullRequestsWidget::onFilterTextChanged);
    connect(ui->mPRTable, &QTableWidget::cellDoubleClicked, this, &PullRequestsWidget::onRowDoubleClicked);
    connect(ui->mBtnOpenFilter, &QPushButton::clicked, this, &PullRequestsWidget::onOpenFilterClicked);
    connect(ui->mBtnClosedFilter, &QPushButton::clicked, this, &PullRequestsWidget::onClosedFilterClicked);

    refresh();
}

PullRequestsWidget::~PullRequestsWidget()
{
    delete ui;
}

void PullRequestsWidget::refresh()
{
    populatePRList();
}

void PullRequestsWidget::onNewPRClicked()
{
    if (mGroupId.isEmpty()) return;

    if (!rsGit || !mMainWidget) return;
    std::vector<RsGitGroup> groups;
    if (!rsGit->getGroups({}, groups)) return;
    // When opened on a fork, offer its upstream as the target. When opened on
    // an original, offer owned forks as sources as well as an owned original.
    RsGitGroup current;
    bool found = false;
    for (const auto &g : groups) if (g.mMeta.mGroupId.toStdString() == mGroupId.toStdString()) { current = g; found = true; }
    if (!found) return;
    RsGxsGroupId targetId = current.mUpstreamGroupId.isNull() ? current.mMeta.mGroupId : current.mUpstreamGroupId;
    RsGitGroup target;
    QStringList labels;
    std::vector<RsGitGroup> sources;
    for (const auto &g : groups) {
        if (g.mMeta.mGroupId == targetId) target = g;
        if (IS_GROUP_ADMIN(g.mMeta.mSubscribeFlags) &&
            (g.mMeta.mGroupId == targetId || g.mUpstreamGroupId == targetId)) {
            sources.push_back(g);
            labels << QString::fromStdString(g.mGroupName) + " (" + QString::fromStdString(g.mMeta.mGroupId.toStdString().substr(0, 8)) + ")";
        }
    }
    if (sources.empty() || target.mMeta.mGroupId.isNull()) {
        QMessageBox::information(this, tr("Create Pull Request"), tr("Fork this repository first, then commit changes in your fork. The upstream repository must remain subscribed."));
        return;
    }
    int index = 0;
    for (size_t i = 0; i < sources.size(); ++i) if (sources[i].mMeta.mGroupId == current.mMeta.mGroupId) index = static_cast<int>(i);
    if (sources.size() > 1) {
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, tr("Create Pull Request"), tr("Source repository"), labels, index, false, &ok);
        if (!ok) return;
        index = labels.indexOf(choice);
    }
    const auto sourceGroup = sources.at(index);
    if (!rsIdentity || !rsIdentity->isOwnId(sourceGroup.mMeta.mAuthorId)) {
        QMessageBox::warning(this, tr("Create Pull Request"),
            tr("The selected fork is owned by %1. Select an identity that owns this fork before creating a Pull Request.")
                .arg(QString::fromStdString(sourceGroup.mMeta.mAuthorId.toStdString().substr(0, 8))));
        return;
    }
    Settings->beginGroup("RetroGit_WorkingDirs");
    const QString localPath = Settings->value(QString::fromStdString(sourceGroup.mMeta.mGroupId.toStdString())).toString();
    Settings->endGroup();
    const std::string sourcePath = localPath.isEmpty() ? GitManager::getBareRepoPath(sourceGroup.mMeta.mGroupId.toStdString()) : localPath.toStdString();
    const std::string targetPath = GitManager::getBareRepoPath(targetId.toStdString());
    std::vector<std::string> sourceBranches, targetBranches;
    std::string currentBranch, targetBranch;
    if (!GitManager::getBranches(sourcePath, sourceBranches, currentBranch) || sourceBranches.empty() ||
        !GitManager::getBranches(targetPath, targetBranches, targetBranch) || targetBranches.empty()) {
        QMessageBox::warning(this, tr("Create Pull Request"), tr("Commit your changes and download the upstream repository before creating a pull request."));
        return;
    }
    QStringList src, dst;
    for (const auto &branch : sourceBranches) src << QString::fromStdString(branch);
    for (const auto &branch : targetBranches) dst << QString::fromStdString(branch);
    GitNewPRDialog dlg(src, QString::fromStdString(currentBranch), this, dst,
        QString::fromStdString(sourceGroup.mGroupName), QString::fromStdString(target.mGroupName));
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.getTitle().isEmpty()) {
        QMessageBox::warning(this, tr("Create Pull Request"), tr("Title cannot be empty."));
        return;
    }
    RsGitPullRequest pr;
    pr.mMeta.mGroupId = targetId;
    pr.mMeta.mAuthorId = sourceGroup.mMeta.mAuthorId;
    pr.mSourceGroupId = sourceGroup.mMeta.mGroupId;
    pr.mTitle = dlg.getTitle().toStdString();
    pr.mDescription = dlg.getDescription().toStdString();
    pr.mSourceBranch = dlg.getSourceBranch().toStdString();
    pr.mTargetBranch = dlg.getTargetBranch().toStdString();
    std::string pack;
    std::map<std::string, std::string> refs;
    if (!GitManager::createPackfileForRef(sourcePath, pr.mSourceBranch, pack, refs) || refs.size() != 1) {
        QMessageBox::critical(this, tr("Create Pull Request"), tr("Could not package the proposed commits."));
        return;
    }
    pr.mSourceCommit = refs.begin()->second;
    // Check the exact proposed snapshot against upstream without changing its branches.
    const std::string reviewRef = "refs/retrogit/pull/preview";
    if (!GitManager::unpackPackfile(targetPath, pack, {}) ||
        !GitManager::setReviewRef(targetPath, reviewRef, pr.mSourceCommit) ||
        GitManager::isBranchMerged(targetPath, reviewRef, pr.mTargetBranch)) {
        QMessageBox::warning(this, tr("Create Pull Request"), tr("There are no new commits to propose, or the proposed commits could not be read."));
        return;
    }
    if (mMainWidget->publishPullRequestWithPack(pr, pack)) {
        QMessageBox::information(this, tr("Pull Request Queued"), tr("Your pull request was queued for publication to the upstream repository."));
        mMainWidget->showPullRequests(QString::fromStdString(targetId.toStdString()));
    } else QMessageBox::critical(this, tr("Create Pull Request"), tr("Could not queue the pull request. Check the RetroGit debug log for 'RetroGit PR DEBUG' messages."));
}

void PullRequestsWidget::onFilterTextChanged(const QString &text)
{
    Q_UNUSED(text);
    populatePRList();
}

void PullRequestsWidget::onRowDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    QTableWidgetItem *item = ui->mPRTable->item(row, 0);
    if (item) {
        QString msgIdStr = item->data(Qt::UserRole).toString();
        if (!msgIdStr.isEmpty()) {
            onViewPRDetailsClicked(msgIdStr);
        }
    }
}

void PullRequestsWidget::onViewPRDetailsClicked(const QString &msgIdStr)
{
    RsGxsMessageId msgId(msgIdStr.toStdString());
    mMainWidget->showPullRequestDetails(mGroupId, msgId);
}

void PullRequestsWidget::populatePRList()
{
    ui->mPRTable->setRowCount(0);
    if (mGroupId.isEmpty()) return;

    std::vector<RsGitPullRequest> pullRequests;
    if (!rsGit || !rsGit->getPullRequests(RsGxsGroupId(mGroupId.toStdString()), pullRequests)) {
        ui->mBtnOpenFilter->setText("0 Open");
        ui->mBtnClosedFilter->setText("0 Closed / Merged");
        return;
    }

    // Determine admin status
    bool isAdmin = false;
    std::list<RsGxsGroupId> groupIds({RsGxsGroupId(mGroupId.toStdString())});
    std::vector<RsGitGroup> groups;
    if (rsGit->getGroups(groupIds, groups) && !groups.empty()) {
        uint32_t flags = groups[0].mMeta.mSubscribeFlags;
        isAdmin = IS_GROUP_ADMIN(flags);
    }

    int openCount = 0;
    int closedCount = 0;

    QString filter = ui->mSearchEdit->text().trimmed().toLower();
    bool showOpenOnly = true;
    bool showClosedOnly = false;

    if (filter.contains("is:closed") || filter.contains("is:merged")) {
        showOpenOnly = false;
        showClosedOnly = true;
    } else if (filter.contains("is:pr")) {
        // default filters
    }

    std::vector<RsGitPullRequest> filteredPRs;

    for (const auto &pr : pullRequests) {
        bool isOpen = (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (pr.mStatus == 0);
        if (isOpen) {
            std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
            if (GitManager::isBranchMerged(bareRepoPath, pr.sourceRef(), pr.mTargetBranch)) {
                isOpen = false;
                if (rsGit) {
                    uint32_t token;
                    rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), pr.mMeta.mMsgId), true);
                }
            }
        } else if (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) {
            if (rsGit) {
                uint32_t token;
                rsGit->setMessageProcessedStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), pr.mMeta.mMsgId), true);
            }
        }
        
        if (isOpen) {
            openCount++;
        } else {
            closedCount++;
        }

        // Apply filters
        if (showOpenOnly && !isOpen) continue;
        if (showClosedOnly && isOpen) continue;

        // Apply text filter if any
        QString prTitle = QString::fromStdString(pr.mTitle).toLower();
        if (!filter.isEmpty() && !filter.contains("is:") && !prTitle.contains(filter)) {
            continue;
        }

        filteredPRs.push_back(pr);
    }

    ui->mBtnOpenFilter->setText(tr("%1 Open").arg(openCount));
    ui->mBtnClosedFilter->setText(tr("%1 Closed / Merged").arg(closedCount));
    updateFilterButtons(showOpenOnly, showClosedOnly);

    ui->mPRTable->setRowCount(filteredPRs.size());

    for (int i = 0; i < (int)filteredPRs.size(); i++) {
        const auto &pr = filteredPRs[i];
        bool isOpen = (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED) && (pr.mStatus == 0);
        if (isOpen) {
            std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
            if (GitManager::isBranchMerged(bareRepoPath, pr.sourceRef(), pr.mTargetBranch)) {
                isOpen = false;
            }
        }

        // Column 0: Status Icon
        QTableWidgetItem *itemIcon = new QTableWidgetItem();
        QString msgIdStr = QString::fromStdString(pr.mMeta.mMsgId.toStdString());
        itemIcon->setData(Qt::UserRole, msgIdStr);
        itemIcon->setToolTip(isOpen ? tr("Open pull request") : (pr.mStatus == 1 ? tr("Closed pull request") : tr("Merged pull request")));
        ui->mPRTable->setItem(i, 0, itemIcon);

        QLabel *lblIcon = new QLabel(ui->mPRTable);
        QString iconPath = isOpen ? ":/images/git-pull-request-green.png" : (pr.mStatus == 1 ? ":/images/git-pull-request-closed.png" : ":/images/git-merge-pink.png");
        QPixmap pixmap(iconPath);
        int iconSize = 28;
        lblIcon->setPixmap(pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        lblIcon->setAlignment(Qt::AlignCenter);
        lblIcon->setStyleSheet("background-color: transparent;");
        lblIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
        lblIcon->setToolTip(isOpen ? tr("Open pull request") : (pr.mStatus == 1 ? tr("Closed pull request") : tr("Merged pull request")));
        ui->mPRTable->setCellWidget(i, 0, lblIcon);

        // Column 1: Info (Title & description metadata)
        QString titleStr = QString::fromStdString(pr.mTitle);
        QString authorName = QString::fromStdString(pr.mMeta.mAuthorId.toStdString().substr(0, 8));
        if (rsIdentity && !pr.mMeta.mAuthorId.isNull()) {
            RsIdentityDetails identity;
            if (rsIdentity->getIdDetails(pr.mMeta.mAuthorId, identity) && !identity.mNickname.empty())
                authorName = QString::fromStdString(identity.mNickname);
        }
        QString detailsStr = tr("#%1 opened %2 by %3 from %4 into %5")
            .arg(i + 1)
            .arg(QDateTime::fromTime_t(pr.mMeta.mPublishTs).toString("yyyy-MM-dd HH:mm"))
            .arg(authorName.toHtmlEscaped())
            .arg(QString::fromStdString(pr.mSourceBranch))
            .arg(QString::fromStdString(pr.mTargetBranch));

        QTableWidgetItem *itemInfo = new QTableWidgetItem("  " + titleStr + "\n  " + detailsStr);
        QFont infoFont = itemInfo->font();
        infoFont.setBold(true);
        itemInfo->setFont(infoFont);
        itemInfo->setToolTip(tr("<b>Pull Request #%1</b><br>Author: <b>%2</b><br>Identity: %3<br><i>Hover the author avatar for the RetroShare profile.</i>")
            .arg(i + 1).arg(authorName.toHtmlEscaped())
            .arg(QString::fromStdString(pr.mMeta.mAuthorId.toStdString()).toHtmlEscaped()));
        QFont font = itemInfo->font();
        font.setBold(isOpen);
        itemInfo->setFont(font);
        ui->mPRTable->setItem(i, 1, itemInfo);

        // Column 2: Author Id Widget
        if (!pr.mMeta.mAuthorId.isNull()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mPRTable);
            authorWidget->setId(pr.mMeta.mAuthorId);
            ui->mPRTable->setCellWidget(i, 2, authorWidget);
        } else {
            ui->mPRTable->setItem(i, 2, new QTableWidgetItem("-"));
        }

        // Column 3: Actions
        {
            QWidget *container = new QWidget(ui->mPRTable);
            QHBoxLayout *layout = new QHBoxLayout(container);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setAlignment(Qt::AlignCenter);

            QPushButton *btnAction = new QPushButton(container);
            if (isOpen && isAdmin) {
                btnAction->setText(tr("Review"));
                btnAction->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 4px 12px; font-weight: bold; min-height: 26px; max-height: 26px; }"
                                        "QPushButton:hover { background-color: #2c974b; }"
                                        "QPushButton:pressed { background-color: #298e46; }");
            } else {
                btnAction->setText(isOpen ? tr("View") : tr("View"));
                btnAction->setStyleSheet("QPushButton { background-color: #f6f8fa; color: #24292f; border-radius: 6px; padding: 4px 12px; border: 1px solid rgba(27, 31, 36, 0.15); min-height: 26px; max-height: 26px; }"
                                        "QPushButton:hover { background-color: #f3f4f6; }"
                                        "QPushButton:pressed { background-color: #ebecf0; }");
            }
            
            connect(btnAction, &QPushButton::clicked, [this, msgIdStr]() {
                onViewPRDetailsClicked(msgIdStr);
            });
            
            layout->addWidget(btnAction);
            ui->mPRTable->setCellWidget(i, 3, container);
        }
    }
}

void PullRequestsWidget::onOpenFilterClicked()
{
    ui->mSearchEdit->setText("is:open");
    populatePRList();
}

void PullRequestsWidget::onClosedFilterClicked()
{
    ui->mSearchEdit->setText("is:closed");
    populatePRList();
}

void PullRequestsWidget::updateFilterButtons(bool showOpenOnly, bool showClosedOnly)
{
    if (showOpenOnly) {
        ui->mBtnOpenFilter->setStyleSheet("QPushButton { color: #2da44e; font-weight: bold; border: none; padding: 4px 8px; background: transparent; }"
                                           "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
        ui->mBtnClosedFilter->setStyleSheet("QPushButton { color: #57606a; border: none; padding: 4px 8px; background: transparent; }"
                                             "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
    } else if (showClosedOnly) {
        ui->mBtnOpenFilter->setStyleSheet("QPushButton { color: #57606a; border: none; padding: 4px 8px; background: transparent; }"
                                           "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
        ui->mBtnClosedFilter->setStyleSheet("QPushButton { color: #8250df; font-weight: bold; border: none; padding: 4px 8px; background: transparent; }"
                                             "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
    } else {
        ui->mBtnOpenFilter->setStyleSheet("QPushButton { color: #57606a; border: none; padding: 4px 8px; background: transparent; }"
                                           "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
        ui->mBtnClosedFilter->setStyleSheet("QPushButton { color: #57606a; border: none; padding: 4px 8px; background: transparent; }"
                                             "QPushButton:hover { background-color: #f3f4f6; border-radius: 4px; }");
    }
}
