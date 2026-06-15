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

PullRequestsWidget::PullRequestsWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PullRequestsWidget)
    , mMainWidget(mainWidget)
    , mGroupId(groupId)
{
    ui->setupUi(this);

    // Styling Title (GitHub-like)
    ui->mLblTitle->setStyleSheet("font-size: 18px; font-weight: bold; color: #24292f;");

    // New PR button styling (GitHub-like green)
    ui->mBtnNewPR->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 6px 12px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                "QPushButton:hover { background-color: #2c974b; }"
                                "QPushButton:pressed { background-color: #298e46; }");

    // Search bar styling
    ui->mSearchEdit->setStyleSheet("QLineEdit { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px; font-size: 13px; }"
                                  "QLineEdit:focus { border: 1px solid #0969da; }");

    // Count labels styling
    ui->mLblOpenCount->setStyleSheet("font-weight: bold; color: #24292f; margin-left: 10px;");
    ui->mLblClosedCount->setStyleSheet("color: #57606a; margin-left: 10px;");

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
    ui->mPRTable->setStyleSheet("QTableWidget { border: 1px solid #d0d7de; border-radius: 6px; background-color: white; }"
                                "QTableWidget::item { border-bottom: 1px solid #d0d7de; }");

    connect(ui->mBtnNewPR, &QPushButton::clicked, this, &PullRequestsWidget::onNewPRClicked);
    connect(ui->mSearchEdit, &QLineEdit::textChanged, this, &PullRequestsWidget::onFilterTextChanged);
    connect(ui->mPRTable, &QTableWidget::cellDoubleClicked, this, &PullRequestsWidget::onRowDoubleClicked);

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

    GitNewPRDialog dlg(qBranches, QString::fromStdString(currentBranch), this);
    if (dlg.exec() == QDialog::Accepted) {
        QString title = dlg.getTitle();
        QString desc = dlg.getDescription();
        QString source = dlg.getSourceBranch();
        QString target = dlg.getTargetBranch();

        if (title.isEmpty()) {
            QMessageBox::warning(this, tr("Warning"), tr("Title cannot be empty."));
            return;
        }

        RsGitPullRequest pr;
        pr.mMeta.mGroupId = RsGxsGroupId(mGroupId.toStdString());
        pr.mTitle = title.toStdString();
        pr.mDescription = desc.toStdString();
        pr.mSourceBranch = source.toStdString();
        pr.mTargetBranch = target.toStdString();
        pr.mStatus = 0; // Open

        uint32_t token;
        if (rsGit && rsGit->publishPullRequest(token, pr)) {
            QMessageBox::information(this, tr("Success"), tr("Pull request opened successfully."));
            refresh();
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to open pull request."));
        }
    }
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
        ui->mLblOpenCount->setText("0 Open");
        ui->mLblClosedCount->setText("0 Closed");
        return;
    }

    // Mark all pull requests in this repository as read asynchronously
    RsThread::async([pullRequests, groupId = mGroupId.toStdString()]() {
        if (rsGit) {
            for (const auto &pr : pullRequests) {
                if (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD) {
                    rsGit->setMessageReadStatus(RsGxsGrpMsgIdPair(RsGxsGroupId(groupId), pr.mMeta.mMsgId), true);
                }
            }
        }
    });

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
        bool isOpen = (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);
        
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

    ui->mLblOpenCount->setText(tr("%1 Open").arg(openCount));
    ui->mLblClosedCount->setText(tr("%1 Closed / Merged").arg(closedCount));

    ui->mPRTable->setRowCount(filteredPRs.size());

    for (int i = 0; i < (int)filteredPRs.size(); i++) {
        const auto &pr = filteredPRs[i];
        bool isOpen = (pr.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED);

        // Column 0: Status Icon
        QTableWidgetItem *itemIcon = new QTableWidgetItem();
        QString msgIdStr = QString::fromStdString(pr.mMeta.mMsgId.toStdString());
        itemIcon->setData(Qt::UserRole, msgIdStr);
        itemIcon->setToolTip(isOpen ? tr("Open pull request") : tr("Merged pull request"));
        ui->mPRTable->setItem(i, 0, itemIcon);

        QLabel *lblIcon = new QLabel(ui->mPRTable);
        QString iconPath = isOpen ? ":/images/git-pull-request-green.png" : ":/images/git-merge.png";
        QPixmap pixmap(iconPath);
        int iconSize = 28;
        lblIcon->setPixmap(pixmap.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        lblIcon->setAlignment(Qt::AlignCenter);
        lblIcon->setStyleSheet("background-color: transparent;");
        lblIcon->setAttribute(Qt::WA_TransparentForMouseEvents);
        lblIcon->setToolTip(isOpen ? tr("Open pull request") : tr("Merged pull request"));
        ui->mPRTable->setCellWidget(i, 0, lblIcon);

        // Column 1: Info (Title & description metadata)
        QString titleStr = QString::fromStdString(pr.mTitle);
        QString detailsStr = tr("#%1 opened %2 by %3 from %4 into %5")
            .arg(i + 1)
            .arg(QDateTime::fromTime_t(pr.mMeta.mPublishTs).toString("yyyy-MM-dd HH:mm"))
            .arg(QString::fromStdString(pr.mMeta.mAuthorId.toStdString().substr(0, 8)))
            .arg(QString::fromStdString(pr.mSourceBranch))
            .arg(QString::fromStdString(pr.mTargetBranch));

        QTableWidgetItem *itemInfo = new QTableWidgetItem("  " + titleStr + "\n  " + detailsStr);
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
                btnAction->setText(tr("Merge"));
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
