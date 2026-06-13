/*******************************************************************************
 * gui/PushesWidget.cpp                                                        *
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

#include "PushesWidget.h"
#include "ui_PushesWidget.h"
#include "MainWidget.h"
#include "services/GitManager.h"
#include <QTimer>
#include <QDateTime>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QDir>
#include "util/misc.h"
#include <iostream>

extern RsGit *rsGit;
extern RsFiles *rsFiles;
extern RsIdentity *rsIdentity;

PushesWidget::PushesWidget(MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PushesWidget)
    , mMainWidget(mainWidget)
{
    ui->setupUi(this);

    // Setup Packfiles Table
    ui->mPackfilesTable->setColumnCount(6);
    ui->mPackfilesTable->setHorizontalHeaderLabels(QStringList() << tr("Author") << tr("Date") << tr("Refs Updated") << tr("Size") << tr("Status / Progress") << tr("Action"));
    ui->mPackfilesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->mPackfilesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mPackfilesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Setup Clones Table
    ui->mClonesTable->setColumnCount(4);
    ui->mClonesTable->setHorizontalHeaderLabels(QStringList() << tr("Repository") << tr("Author") << tr("Status / Progress") << tr("Date"));
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->mClonesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mClonesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->mClonesTable->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->mClonesTable, &QTableWidget::customContextMenuRequested, this, &PushesWidget::onClonesTableContextMenu);

    mDownloadPollTimer = new QTimer(this);
    connect(mDownloadPollTimer, SIGNAL(timeout()), this, SLOT(pollDownloadProgress()));

    clear();
}

PushesWidget::~PushesWidget()
{
    delete ui;
}

void PushesWidget::clear()
{
    ui->mPackfilesTable->setRowCount(0);
    ui->mClonesTable->setRowCount(0);
    mAvailableUpdates.clear();
    mDownloadPollTimer->stop();
    mGroupId.clear();
}

void PushesWidget::setGroupId(const QString &groupId)
{
    mGroupId = groupId;
    if (mGroupId.isEmpty()) {
        clear();
        return;
    }

    refresh();
}

void PushesWidget::refresh()
{
    if (mGroupId.isEmpty()) return;

    populatePackfiles();
    populateClonesTable();
}

void PushesWidget::handleGitEvent(const RsGitEvent *e)
{
    if (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString()))
        return;

    if (e->mGitEventCode == RsGitEventCode::CLONE_STATUS_CHANGED) {
        // Find active clone in history and update it
        QString status = QString::fromStdString(e->mCloneStatus);
        for (int i = 0; i < (int)mCloneHistory.size(); ++i) {
            if (mCloneHistory[i].repoId == mGroupId && 
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
            mMainWidget->updateDisplay();
            mMainWidget->triggerTreeSelectionChanged();
        } else if (!status.isEmpty() && (status.contains("Failed") || status.contains("failed") || status.contains("down") || status.contains("not available"))) {
            QMessageBox::critical(this, tr("Clone Failed"), status);
        }
    } else if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED) {
        refresh();
    }
}

void PushesWidget::addCloneRecord(const CloneRecord &rec)
{
    mCloneHistory.insert(mCloneHistory.begin(), rec);
    populateClonesTable();
}

std::vector<PushesWidget::CloneRecord>& PushesWidget::getCloneHistory()
{
    return mCloneHistory;
}

void PushesWidget::updateClonesTable()
{
    populateClonesTable();
}

void PushesWidget::onDownloadClicked()
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

    refresh();
}

void PushesWidget::onCancelDownloadClicked()
{
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString fileHashStr = btn->property("fileHash").toString();
    RsFileHash fileHash(fileHashStr.toStdString());

    if (rsFiles) {
        rsFiles->FileCancel(fileHash);
    }

    refresh();
}

void PushesWidget::pollDownloadProgress()
{
    if (mGroupId.isEmpty()) {
        mDownloadPollTimer->stop();
        return;
    }

    bool activeDownloads = false;
    bool completedAny = false;

    for (int i = 0; i < ui->mPackfilesTable->rowCount(); ++i) {
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
                std::cout << "PushesWidget::pollDownloadProgress: packfile completed, unpacking " << file.mHash.toStdString() << std::endl;
                if (rsGit->unpackUpdate(RsGxsGroupId(mGroupId.toStdString()), update.mMeta.mMsgId, file.mHash, update.mRefUpdates)) {
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
                ui->mPackfilesTable->setItem(i, 4, new QTableWidgetItem(statusStr));
            }
        }
    }

    if (completedAny) {
        mMainWidget->refreshGitWidget();
        mMainWidget->refreshCodeWidget();
        refresh();
    } else if (!activeDownloads) {
        mDownloadPollTimer->stop();
        refresh();
    }
}

void PushesWidget::onClonesTableContextMenu(const QPoint &pos)
{
    QTableWidgetItem *item = ui->mClonesTable->itemAt(pos);
    if (!item) return;

    QMenu contextMnu(this);
    QAction *actionClear = contextMnu.addAction(tr("Clear"));
    QAction *actionClearAll = contextMnu.addAction(tr("Clear All"));

    QAction *selectedAct = contextMnu.exec(ui->mClonesTable->mapToGlobal(pos));
    if (selectedAct == actionClear) {
        int selectedRow = item->row();
        std::vector<CloneRecord> filtered;
        for (const auto &rec : mCloneHistory) {
            if (rec.repoId == mGroupId) filtered.push_back(rec);
        }
        if (selectedRow >= 0 && selectedRow < (int)filtered.size()) {
            CloneRecord target = filtered[selectedRow];
            for (auto it = mCloneHistory.begin(); it != mCloneHistory.end(); ++it) {
                if (it->repoId == target.repoId && it->time == target.time && it->status == target.status) {
                    mCloneHistory.erase(it);
                    break;
                }
            }
        }
        populateClonesTable();
    } else if (selectedAct == actionClearAll) {
        for (auto it = mCloneHistory.begin(); it != mCloneHistory.end(); ) {
            if (it->repoId == mGroupId) {
                it = mCloneHistory.erase(it);
            } else {
                ++it;
            }
        }
        populateClonesTable();
    }
}

void PushesWidget::populatePackfiles()
{
    ui->mPackfilesTable->setRowCount(0);
    mAvailableUpdates.clear();
    if (mGroupId.isEmpty()) return;

    // Retrieve local commit history
    std::string repoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    QString rawPath = mMainWidget->getLocalPath();
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            repoPath = localPath.toStdString();
        }
    }

    std::vector<GitCommitInfo> commits;
    GitManager::getCommitLog(repoPath, commits);

    std::set<std::string> localCommitShas;
    for (const auto &c : commits) {
        localCommitShas.insert(c.hash);
        localCommitShas.insert(c.full_hash);
    }

    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
    }

    // Filter updates
    for (const auto &update : updates) {
        bool isDownloaded = false;
        for (const auto &pair : update.mRefUpdates) {
            if (localCommitShas.count(pair.second)) {
                isDownloaded = true;
                break;
            }
        }
        if (!isDownloaded) {
            mAvailableUpdates.push_back(update);
        }
    }

    std::sort(mAvailableUpdates.begin(), mAvailableUpdates.end(), [](const RsGitUpdate &a, const RsGitUpdate &b) {
        return a.mMeta.mPublishTs > b.mMeta.mPublishTs; // Descending (newest first)
    });

    ui->mPackfilesTable->setRowCount(mAvailableUpdates.size());

    bool hasPendingDownloads = false;

    for (int i = 0; i < (int)mAvailableUpdates.size(); ++i) {
        const auto &update = mAvailableUpdates[i];
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);

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

        // Refs list
        QString refsStr = "";
        for (auto const& pair : update.mRefUpdates) {
            refsStr += QString::fromStdString(pair.first) + " -> " + QString::fromStdString(pair.second).left(8) + "; ";
        }
        QTableWidgetItem *itemRefs = new QTableWidgetItem(refsStr);

        ui->mPackfilesTable->setItem(i, 0, itemAuth);
        if (!update.mMeta.mAuthorId.isNull()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mPackfilesTable);
            authorWidget->setId(update.mMeta.mAuthorId);
            ui->mPackfilesTable->setCellWidget(i, 0, authorWidget);
        }

        ui->mPackfilesTable->setItem(i, 1, itemDate);
        ui->mPackfilesTable->setItem(i, 2, itemRefs);

        if (update.mFiles.empty()) {
            QTableWidgetItem *itemSize = new QTableWidgetItem(tr("Inline"));
            ui->mPackfilesTable->setItem(i, 3, itemSize);

            QTableWidgetItem *itemStatus = new QTableWidgetItem(isUnread ? tr("Downloaded, unpacking...") : tr("Completed (Inline)"));
            ui->mPackfilesTable->setItem(i, 4, itemStatus);

            QTableWidgetItem *itemAction = new QTableWidgetItem(tr("-"));
            ui->mPackfilesTable->setItem(i, 5, itemAction);
        } else {
            const auto &file = update.mFiles[0];
            QTableWidgetItem *itemSize = new QTableWidgetItem(misc::friendlyUnit(file.mSize));
            ui->mPackfilesTable->setItem(i, 3, itemSize);

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
                    if (rsGit && rsGit->unpackUpdate(RsGxsGroupId(mGroupId.toStdString()), update.mMeta.mMsgId, file.mHash, update.mRefUpdates)) {
                        isUnprocessed = false;
                    }
                }

                QTableWidgetItem *itemStatus = new QTableWidgetItem(isUnprocessed ? tr("Downloaded, unpacking...") : tr("Completed"));
                ui->mPackfilesTable->setItem(i, 4, itemStatus);

                QTableWidgetItem *itemAction = new QTableWidgetItem(tr("-"));
                ui->mPackfilesTable->setItem(i, 5, itemAction);
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

                ui->mPackfilesTable->setItem(i, 4, new QTableWidgetItem(statusStr));

                if (isDownloading) {
                    hasPendingDownloads = true;
                    QPushButton *btnCancel = new QPushButton(tr("Cancel"), ui->mPackfilesTable);
                    btnCancel->setProperty("fileHash", QString::fromStdString(file.mHash.toStdString()));
                    connect(btnCancel, &QPushButton::clicked, this, &PushesWidget::onCancelDownloadClicked);
                    ui->mPackfilesTable->setCellWidget(i, 5, btnCancel);
                } else {
                    QPushButton *btnDl = new QPushButton(tr("Download"), ui->mPackfilesTable);
                    btnDl->setProperty("fileHash", QString::fromStdString(file.mHash.toStdString()));
                    btnDl->setProperty("fileName", QString::fromStdString(file.mName));
                    btnDl->setProperty("fileSize", (qlonglong)file.mSize);
                    connect(btnDl, &QPushButton::clicked, this, &PushesWidget::onDownloadClicked);
                    ui->mPackfilesTable->setCellWidget(i, 5, btnDl);
                }
            }
        }
    }

    if (hasPendingDownloads) {
        mDownloadPollTimer->start(1000);
    } else {
        mDownloadPollTimer->stop();
    }
}

void PushesWidget::populateClonesTable()
{
    ui->mClonesTable->setRowCount(0);
    if (mGroupId.isEmpty()) return;

    std::vector<CloneRecord> filteredHistory;
    for (const auto &rec : mCloneHistory) {
        if (rec.repoId == mGroupId) {
            filteredHistory.push_back(rec);
        }
    }

    ui->mClonesTable->setRowCount(filteredHistory.size());

    for (int i = 0; i < (int)filteredHistory.size(); ++i) {
        QTableWidgetItem *itemRepo = new QTableWidgetItem(filteredHistory[i].repoName.isEmpty() ? filteredHistory[i].repoId : filteredHistory[i].repoName);
        QTableWidgetItem *itemOwner = new QTableWidgetItem();
        if (filteredHistory[i].ownerId.isEmpty()) {
            itemOwner->setText(tr("Anonymous"));
        }
        QTableWidgetItem *itemStatus = new QTableWidgetItem(filteredHistory[i].status);
        QTableWidgetItem *itemTime = new QTableWidgetItem(filteredHistory[i].time);

        if (filteredHistory[i].status.contains("Requesting") || filteredHistory[i].status.contains("secured") || filteredHistory[i].status.contains("Unpacking")) {
            QFont font = itemStatus->font();
            font.setBold(true);
            itemStatus->setFont(font);
            itemStatus->setForeground(QBrush(QColor("#2980b9")));
        } else if (filteredHistory[i].status.contains("Successful") || filteredHistory[i].status.contains("completed")) {
            itemStatus->setForeground(QBrush(QColor("#27ae60")));
        } else if (filteredHistory[i].status.contains("Failed") || filteredHistory[i].status.contains("down")) {
            itemStatus->setForeground(QBrush(QColor("#c0392b")));
        }

        ui->mClonesTable->setItem(i, 0, itemRepo);
        ui->mClonesTable->setItem(i, 1, itemOwner);
        if (!filteredHistory[i].ownerId.isEmpty()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mClonesTable);
            authorWidget->setId(RsGxsId(filteredHistory[i].ownerId.toStdString()));
            ui->mClonesTable->setCellWidget(i, 1, authorWidget);
        }
        ui->mClonesTable->setItem(i, 2, itemStatus);
        ui->mClonesTable->setItem(i, 3, itemTime);
    }
}
