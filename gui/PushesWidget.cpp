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
#include <QLocale>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <retroshare/rsinit.h>
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
    // Open RetroGit's internal folders inside the RetroShare account directory.
    auto openAccountFolder = [this](const QString &name, const QString &title) {
        const QString path = QDir(QString::fromStdString(RsAccounts::AccountDirectory())).absoluteFilePath(name);
        if (!QDir().mkpath(path)) {
            QMessageBox::warning(this, title, tr("Could not create the folder:\n%1").arg(path));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this, title, tr("Could not open the folder:\n%1").arg(path));
    };
    ui->tempButton->setText(tr("Open Temp"));
    ui->tempButton->setToolTip(tr("Open the RetroGit temporary folder (retrogit_temp): published packs and file previews."));
    connect(ui->tempButton, &QPushButton::clicked, this, [this, openAccountFolder]() {
        openAccountFolder("retrogit_temp", tr("Open Temp Folder"));
    });
    ui->reposButton->setText(tr("Open Repositories"));
    ui->reposButton->setToolTip(tr("Open the internal RetroGit repositories folder (retrogit_repos): the local copies used to serve clones."));
    connect(ui->reposButton, &QPushButton::clicked, this, [this, openAccountFolder]() {
        openAccountFolder("retrogit_repos", tr("Open Repository Folder"));
    });

    // Setup Packfiles Table
    ui->mPackfilesTable->setColumnCount(6);
    ui->mPackfilesTable->setHorizontalHeaderLabels(QStringList() << tr("Author") << tr("Date") << tr("Refs Updated") << tr("Size") << tr("Status / Progress") << tr("Action"));
    ui->mPackfilesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->mPackfilesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->mPackfilesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mPackfilesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Setup Clones Table
    ui->mClonesTable->setColumnCount(6);
    ui->mClonesTable->setHorizontalHeaderLabels(QStringList() << tr("Repository") << tr("Author") << tr("Status") << tr("Progress") << tr("Received / Total") << tr("Date / Time"));
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    ui->mClonesTable->setColumnWidth(0, 180);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    ui->mClonesTable->setColumnWidth(3, 160);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->mClonesTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
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

namespace {
// Transfer status messages that end a clone or pull without success.
// (Plain "down" would also match "downloading required packfiles".)
bool isFailedStatus(const QString &status)
{
    static const char *const markers[] = {
        "failed", "timed out", "went down", "not available", "unavailable", "could not",
        "invalid", "restricted", "busy", "exceeds", "wrong repository"
    };
    for (const char *marker : markers)
        if (status.contains(QLatin1String(marker), Qt::CaseInsensitive)) return true;
    return false;
}
}

void PushesWidget::handleGitEvent(const RsGitEvent *e)
{
    if (e->mGitEventCode != RsGitEventCode::CLONE_STATUS_CHANGED &&
        (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString())))
        return;

    if (e->mGitEventCode == RsGitEventCode::CLONE_STATUS_CHANGED) {
        // Find active clone in history and update it
        QString status = QString::fromStdString(e->mCloneStatus);
        for (int i = 0; i < (int)mCloneHistory.size(); ++i) {
            if (mCloneHistory[i].repoId == QString::fromStdString(e->mGitGroupId.toStdString()) &&
                !mCloneHistory[i].status.contains("Successful") && 
                !mCloneHistory[i].status.contains("Failed") && 
                !mCloneHistory[i].status.contains("completed")) 
            {
                mCloneHistory[i].status = status;
                if (e->mCloneBytesTotal > 0) {
                    mCloneHistory[i].received = e->mCloneBytesReceived;
                    mCloneHistory[i].total = e->mCloneBytesTotal;
                }
                mCloneHistory[i].complete = e->mCloneSuccess;
                break;
            }
        }
        const bool terminal = e->mCloneSuccess || isFailedStatus(status);
        // Progress events arrive for every received chunk: rebuild the table at most a few times per second.
        if (terminal) populateClonesTable();
        else scheduleClonesTableUpdate();

        if (e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString())) return;

        // Event handler: never open modal dialogs here.
        if (e->mCloneSuccess) {
            if (status.contains("sync", Qt::CaseInsensitive)) {
                // The completed transfer is already shown in the table. Avoid
                // interrupting the user with a popup for every synchronized pack.
            } else {
                mMainWidget->showNotice(tr("Clone Successful"), tr("Successfully cloned decentral repository."));
            }
            mMainWidget->updateDisplay();
            mMainWidget->triggerTreeSelectionChanged();
        } else if (!status.isEmpty() && isFailedStatus(status)) {
            mMainWidget->showNotice(status.contains("sync", Qt::CaseInsensitive) ? tr("Sync Failed") : tr("Clone Failed"), status);
        }
    } else if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED || e->mGitEventCode == RsGitEventCode::POST_UPDATED) {
        refresh();
    }
}

void PushesWidget::addCloneRecord(const CloneRecord &rec)
{
    mCloneHistory.insert(mCloneHistory.begin(), rec);
    // Keep the transfer history bounded.
    if (mCloneHistory.size() > 200) mCloneHistory.resize(200);
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
            // Only packs that were not imported yet; the import runs in the background.
            if (rsGit && (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_UNPROCESSED)) {
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

    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
    }

    // Look the published commits up directly (the log only covers the latest 100 commits).
    std::set<std::string> publishedShas;
    for (const auto &update : updates)
        for (const auto &pair : update.mRefUpdates) publishedShas.insert(pair.second);
    const std::set<std::string> localCommitShas = GitManager::filterExistingCommits(repoPath, publishedShas);

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
        QString dateStr = QLocale::system().toString(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(update.mMeta.mPublishTs)),
            QLocale::ShortFormat);
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
                    // Queued: the handler rebuilds the table, which deletes this button.
                    connect(btnCancel, &QPushButton::clicked, this, &PushesWidget::onCancelDownloadClicked, Qt::QueuedConnection);
                    ui->mPackfilesTable->setCellWidget(i, 5, btnCancel);
                } else {
                    QPushButton *btnDl = new QPushButton(tr("Download"), ui->mPackfilesTable);
                    btnDl->setProperty("fileHash", QString::fromStdString(file.mHash.toStdString()));
                    btnDl->setProperty("fileName", QString::fromStdString(file.mName));
                    btnDl->setProperty("fileSize", (qlonglong)file.mSize);
                    connect(btnDl, &QPushButton::clicked, this, &PushesWidget::onDownloadClicked, Qt::QueuedConnection);
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

void PushesWidget::scheduleClonesTableUpdate()
{
    if (!mClonesTableTimer) {
        mClonesTableTimer = new QTimer(this);
        mClonesTableTimer->setSingleShot(true);
        mClonesTableTimer->setInterval(250);
        connect(mClonesTableTimer, &QTimer::timeout, this, &PushesWidget::populateClonesTable);
    }
    if (!mClonesTableTimer->isActive()) mClonesTableTimer->start();
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
        itemTime->setToolTip(filteredHistory[i].time);
        itemStatus->setToolTip(filteredHistory[i].status);

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
        const auto& record = filteredHistory[i];
        auto* progress = new QProgressBar(ui->mClonesTable);
        progress->setRange(0, 100);
        progress->setTextVisible(true);
        const int percent = record.total ? int(qMin<uint64_t>(record.received, record.total) * 100 / record.total) : 0;
        if (record.complete) {
            progress->setValue(100);
            progress->setFormat(tr("Done"));
            progress->setToolTip(tr("The repository was transferred and written to the working folder."));
        } else if (isFailedStatus(record.status)) {
            // A failed transfer must not look like it is still waiting.
            progress->setValue(percent);
            progress->setFormat(tr("Failed"));
            progress->setStyleSheet("QProgressBar { color: #c0392b; font-weight: bold; } QProgressBar::chunk { background-color: #e6b0aa; }");
            progress->setToolTip(record.status);
        } else if (record.total && record.received >= record.total) {
            progress->setValue(100);
            progress->setFormat(tr("Unpacking..."));
            progress->setToolTip(tr("Repository data received; unpacking and checkout are running."));
        } else if (record.total) {
            progress->setValue(percent);
            progress->setFormat(tr("%p%"));
            progress->setToolTip(tr("Receiving repository data from the owner."));
        } else {
            progress->setValue(0);
            progress->setFormat(tr("Waiting"));
            progress->setToolTip(tr("Waiting for the owner's response. The download starts when the secure tunnel is ready."));
        }
        ui->mClonesTable->setCellWidget(i, 3, progress);
        QString size = record.total ? tr("%1 / %2").arg(misc::friendlyUnit(record.received), misc::friendlyUnit(record.total)) : tr("Unknown");
        auto* sizeItem = new QTableWidgetItem(size);
        sizeItem->setToolTip(tr("Transferred repository response, including encoding and metadata. The total becomes known when data arrives."));
        ui->mClonesTable->setItem(i, 4, sizeItem);
        ui->mClonesTable->setItem(i, 5, itemTime);
    }
}
