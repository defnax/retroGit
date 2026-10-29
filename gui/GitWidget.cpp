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

#include <QComboBox>
#include <QFormLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QHeaderView>
#include <QMenu>
#include <QTimer>
#include <QPointer>
#include <QElapsedTimer>
#include <QApplication>
#include <QPainter>
#include <QEvent>
#include <iostream>
#include <set>
#include "util/qtthreadsutils.h"
#include <util/rsthreads.h>

namespace {
// Fingerprint of a working tree: HEAD plus the list of changed files.
std::string workingTreeState(const std::string &head, const std::vector<GitLocalChange> &changes)
{
    std::string state = head;
    for (const auto &change : changes) {
        state += '\n';
        state += change.status;
        state += change.path;
    }
    return state;
}
}

extern RsGit *rsGit;
extern RsPeers *rsPeers;
extern RsIdentity *rsIdentity;

GitWidget::GitWidget(MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::GitWidget)
    , mMainWidget(mainWidget)
{
    ui->setupUi(this);
    ui->mCommitTable->viewport()->installEventFilter(this);

    // Configure Commit Table
    ui->mCommitTable->setColumnCount(6);
    ui->mCommitTable->setHorizontalHeaderLabels(QStringList() << tr("Hash") << tr("Message") << tr("Author") << tr("Date") << tr("Status") << tr("Action"));
    ui->mCommitTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->mCommitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->mCommitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->mCommitTable->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->mBtnInitialize, &QPushButton::clicked, this, &GitWidget::onInitializeClicked);
    connect(ui->mBtnFork, &QPushButton::clicked, this, &GitWidget::onForkClicked);
    connect(ui->mBtnClone, &QPushButton::clicked, this, &GitWidget::onCloneClicked);
    connect(ui->mBtnCommit, &QPushButton::clicked, this, &GitWidget::onCommitClicked);
    connect(ui->mBtnPush, &QPushButton::clicked, this, &GitWidget::onPushClicked);
    connect(ui->mBtnPull, &QPushButton::clicked, this, &GitWidget::onPullClicked);
    connect(ui->mBtnRebase, &QPushButton::clicked, this, &GitWidget::onRebaseClicked);
    connect(ui->mCommitTable, &QTableWidget::itemSelectionChanged, this, &GitWidget::onCommitSelectionChanged);
    connect(ui->mCommitTable, &QTableWidget::customContextMenuRequested, this, &GitWidget::onCommitTableContextMenu);

    // Detect edits made outside RetroShare. Only a cheap status check runs, in
    // the background; the view is rebuilt when the working tree changed.
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &GitWidget::pollWorkingTree);
    timer->start(5000);

    clear();
}

GitWidget::~GitWidget()
{
    ui->mCommitTable->viewport()->removeEventFilter(this);
    delete ui;
}

void GitWidget::clear()
{
    mLocalPath.clear();
    ui->mBtnFork->hide();
    ui->mBtnInitialize->setVisible(false);
    ui->mBtnClone->setVisible(false);
    ui->mBtnInitialize->setEnabled(false);
    ui->mBtnClone->setEnabled(false);
    ui->mBtnCommit->setEnabled(false);
    ui->mBtnPush->setEnabled(false);
    ui->mBtnPull->setEnabled(false);
    ui->mBtnRebase->setVisible(false);
    ui->mBtnRebase->setEnabled(false);
    ui->mLblOwnerInfo->setVisible(false);
    ui->mLblSubscriberInfo->setVisible(false);
    mNeedsSubscription = false;
    ui->mCommitTable->viewport()->update();
    ui->lblLog->show();
    ui->mCommitTable->show();
    ui->mCommitTable->setRowCount(0);
    mGroupId.clear();

    mLastGroupId.clear();
    mLastRepoPath.clear();
    mLastSelectedBranch.clear();
    mLastCommitHashes.clear();
    mLastLocalChanges.clear();
    mLastUpdates.clear();
}

void GitWidget::setGroupId(const QString &groupId, bool doRefresh)
{
    mGroupId = groupId;
    mStatusKnown = false;
    mStatusChanges.clear();
    mLastWorkingTreeState.clear();
    if (mGroupId.isEmpty()) {
        clear();
        return;
    }

    mLocalPath = loadRepoLocalPath(mGroupId).trimmed();

    if (doRefresh) refresh();
}

void GitWidget::refresh()
{
    if (mGroupId.isEmpty()) return;

    QString path = mLocalPath;
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

    const bool needsSubscription = !groups.empty() && !isAdmin && !isSubscribed;
    if (mNeedsSubscription != needsSubscription) {
        mNeedsSubscription = needsSubscription;
        mLastGroupId.clear(); // Rebuild the commit rows after subscribing.
        ui->mCommitTable->viewport()->update();
    }

    if (hasPath) {
        QString cleanPath = QDir::cleanPath(path);
        pathExists = QDir(cleanPath).exists();
        if (pathExists && GitManager::isValidRepository(cleanPath.toStdString())) {
            isCloned = true;
        }
    }

    // The working-tree status is slow on large repositories (and on Windows):
    // it is computed in the background, this uses the last known result.
    bool hasLocalChanges = false;
    std::vector<GitLocalChange> localChanges;
    bool localChangesKnown = false;
    if (isCloned) {
        const std::string cleanPath = QDir::cleanPath(path).toStdString();
        if (mStatusKnown && mStatusPath == cleanPath) {
            localChanges = mStatusChanges;
            localChangesKnown = true;
            hasLocalChanges = !localChanges.empty();
        }
        // Re-check after every refresh (e.g. after a commit); the view is
        // rebuilt only if the result differs.
        startStatusCheck();
    }

    ui->mBtnFork->setVisible(!isAdmin && isSubscribed);
    const bool isFork = isAdmin && !groups.empty() && !groups.front().mUpstreamGroupId.isNull();
    ui->mBtnRebase->setVisible(isFork && isCloned);
    ui->mBtnRebase->setEnabled(isFork && isCloned);
    ui->mBtnFork->setEnabled(mPendingForkPath.isEmpty());
    ui->mBtnPush->setEnabled(isAdmin && isCloned && !mPushInProgress);
    ui->mBtnPush->setToolTip(tr("Publish local commits to this repository."));
    ui->mBtnInitialize->setVisible(isAdmin && !isCloned);
    ui->mBtnInitialize->setEnabled(isAdmin && !isCloned);
    ui->mBtnClone->setVisible(!isAdmin && !isCloned);
    ui->mBtnClone->setText(tr("Clone Repository"));
    ui->mBtnClone->setToolTip(tr("Download this repository to a local folder."));
    ui->mBtnPull->setEnabled(isCloned);
    ui->mBtnPull->setText(isAdmin ? tr("Update Working Folder") : tr("Sync / Pull"));
    ui->mBtnPull->setToolTip(isAdmin
        ? tr("Apply published changes from RetroGit's local cache to your working folder.")
        : tr("Download missing repository data and update your working folder."));
    const bool cloneInProgress = rsGit && rsGit->isCloneInProgress(RsGxsGroupId(mGroupId.toStdString()));
    ui->mBtnClone->setProperty("cloneInProgress", cloneInProgress);
    if (cloneInProgress) {
        ui->mBtnClone->setEnabled(false);
        ui->mBtnClone->setText(tr("Cloning..."));
        ui->mBtnClone->setToolTip(tr("A repository clone is already in progress. Please wait for it to finish."));
    } else if (!isSubscribed) {
        ui->mBtnClone->setEnabled(false);
    } else {
        ui->mBtnClone->setEnabled(!isAdmin && !isCloned);
    }
    ui->mBtnCommit->setEnabled(hasPath && pathExists && isAdmin && hasLocalChanges);

    ui->mLblOwnerInfo->setVisible(isAdmin);
    ui->mLblOwnerInfo->setText(isCloned
        ? tr("<b>For Repo Owners:</b> Add or edit project files, then <b>Commit Changes</b> and <b>Push / Publish</b>.")
        : tr("<b>For Repo Owners:</b> Choose your project folder with <b>Initialize Repository</b>, add files and <b>Commit Changes</b>, then <b>Push / Publish</b>."));
    ui->mLblSubscriberInfo->setVisible(isSubscribed && !isAdmin);
    ui->mLblSubscriberInfo->setText(isCloned
        ? tr("<b>For Subscribers:</b> Use <b>Sync / Pull</b> to download repository updates.")
        : tr("<b>For Subscribers:</b> Click <b>Clone Repository</b> and choose an empty local folder to download the code."));

    if (isAdmin && !groups.empty() && !groups.front().mUpstreamGroupId.isNull()) {
        const QString forkName = QString::fromStdString(groups.front().mGroupName).toHtmlEscaped();
        const QString upstreamId = QString::fromStdString(groups.front().mUpstreamGroupId.toStdString()).toHtmlEscaped();
        ui->mLblOwnerInfo->setText(tr("<b>Fork: %1</b>: Commit and publish changes in your fork, then open Pull Requests to propose them upstream.").arg(forkName));
        ui->mLblOwnerInfo->setToolTip(tr("Fork repository: %1\nUpstream repository ID: %2").arg(forkName, upstreamId));
    }

    if (mNeedsSubscription)
        ui->mCommitTable->setRowCount(0);
    else
        populateCommitLog(localChangesKnown ? &localChanges : nullptr);

    if (ui->mCommitTable->rowCount() == 0) {
        ui->mBtnPush->setText(tr("Push & Publish"));
    } else {
        ui->mBtnPush->setText(tr("Push Local Commits"));
    }
}

bool GitWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->mCommitTable->viewport() &&
        event->type() == QEvent::Paint && mNeedsSubscription) {
        QWidget *viewport = ui->mCommitTable->viewport();
        QPainter painter(viewport);
        painter.fillRect(viewport->rect(), viewport->palette().base());
        painter.setPen(viewport->palette().color(QPalette::Text));
        painter.drawText(viewport->rect().adjusted(20, 20, -20, -20),
                        Qt::AlignCenter | Qt::TextWordWrap,
                        tr("Subscribe to this repository to clone its code and receive updates.\n"
                           "Right-click the repository in the list and choose Subscribe."));
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void GitWidget::pollWorkingTree()
{
    // Transfers can change state before a working tree exists or changes.
    if (isVisible() && !mGroupId.isEmpty() && rsGit) {
        const bool cloning = rsGit->isCloneInProgress(RsGxsGroupId(mGroupId.toStdString()));
        if (cloning != ui->mBtnClone->property("cloneInProgress").toBool())
            refresh();
    }
    if (!isVisible()) return;
    // Back off when the check is slow (very large folders): at most 10% of the
    // time is spent checking, never more often than every 5 seconds.
    if (mLastStatusFinished.isValid() &&
        mLastStatusFinished.elapsed() < qMax<qint64>(5000, 10 * mLastStatusCostMs)) return;
    startStatusCheck();
}

bool GitWidget::cachedLocalChanges(const std::string &path, std::vector<GitLocalChange> &changes) const
{
    if (!mStatusKnown || mStatusPath != path) return false;
    changes = mStatusChanges;
    return true;
}

void GitWidget::startStatusCheck()
{
    if (mPollInFlight || mGroupId.isEmpty() || mLocalPath.isEmpty()) return;
    mPollInFlight = true;
    QPointer<GitWidget> self(this);
    const QString groupId = mGroupId;
    const std::string path = QDir::cleanPath(mLocalPath).toStdString();
    RsThread::async([self, groupId, path]() {
        QElapsedTimer timer;
        timer.start();
        std::vector<GitLocalChange> changes;
        std::string state;
        if (GitManager::isValidRepository(path)) {
            GitManager::getLocalChanges(path, changes);
            state = workingTreeState(GitManager::headCommit(path), changes);
        }
        const qint64 costMs = timer.elapsed();
        RsQThreadUtils::postToObject([self, groupId, path, changes, state, costMs]() {
            if (!self) return;
            self->mPollInFlight = false;
            self->mLastStatusCostMs = costMs;
            self->mLastStatusFinished.start();
            if (groupId != self->mGroupId || path != QDir::cleanPath(self->mLocalPath).toStdString()) {
                self->startStatusCheck(); // another repository was selected meanwhile
                return;
            }
            if (state.empty()) return;
            self->mStatusPath = path;
            self->mStatusChanges = changes;
            self->mStatusKnown = true;
            if (state == self->mLastWorkingTreeState) return;
            // Do not rebuild the table under an open dialog or menu; the next poll retries.
            if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
            self->mLastWorkingTreeState = state;
            self->refresh();
        }, qApp);
    });
}

void GitWidget::handleGitEvent(const RsGitEvent *e)
{
    if (!mPendingForkPath.isEmpty() && e->mGitGroupId == mPendingFork.mUpstreamGroupId &&
        e->mGitEventCode == RsGitEventCode::CLONE_STATUS_CHANGED) {
        if (e->mCloneSuccess && GitManager::isValidRepository(mPendingForkPath.toStdString())) finishFork();
        else if (e->mCloneStatus.find("Failed") != std::string::npos || e->mCloneStatus.find("failed") != std::string::npos || e->mCloneStatus.find("timed out") != std::string::npos || e->mCloneStatus.find("went down") != std::string::npos) {
            mPendingForkPath.clear();
            // Called from an event handler: never open a modal dialog here.
            mMainWidget->showNotice(tr("Fork Failed"), QString::fromStdString(e->mCloneStatus));
        }
    }

    if (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString()))
        return;

    if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED || e->mGitEventCode == RsGitEventCode::POST_UPDATED) {
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

QString GitWidget::chooseRepositoryPath(bool clone)
{
    QDialog dialog(this);
    dialog.setWindowTitle(clone ? tr("Clone a repository") : tr("Initialize repository"));
    dialog.setMinimumWidth(500);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(12);
    auto *hint = new QLabel(clone
        ? tr("Choose an empty folder where the repository will be downloaded.")
        : tr("Choose your project folder to initialize or connect an existing Git repository."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *label = new QLabel(tr("Local path"), &dialog);
    layout->addWidget(label);
    auto *pathLayout = new QHBoxLayout;
    auto *pathEdit = new QLineEdit(mLocalPath, &dialog);
    pathEdit->setPlaceholderText(tr("Select a local folder"));
    label->setBuddy(pathEdit);
    auto *browse = new QPushButton(tr("Choose..."), &dialog);
    pathLayout->addWidget(pathEdit, 1);
    pathLayout->addWidget(browse);
    layout->addLayout(pathLayout);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *accept = buttons->addButton(clone ? tr("Clone") : tr("Initialize"), QDialogButtonBox::AcceptRole);
    accept->setDefault(true);
    accept->setEnabled(!pathEdit->text().trimmed().isEmpty());
    layout->addWidget(buttons);
    connect(pathEdit, &QLineEdit::textChanged, &dialog, [accept](const QString &text) {
        accept->setEnabled(!text.trimmed().isEmpty());
    });
    connect(browse, &QPushButton::clicked, &dialog, [&dialog, pathEdit]() {
        const QString path = QFileDialog::getExistingDirectory(&dialog, tr("Select Local Folder"),
            pathEdit->text().isEmpty() ? QDir::homePath() : pathEdit->text(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!path.isEmpty()) pathEdit->setText(path);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, pathEdit, clone]() {
        const QString path = pathEdit->text().trimmed();
        if (path.isEmpty() || !QDir::isAbsolutePath(path) || !QDir(path).exists()) {
            QMessageBox::warning(&dialog, tr("Invalid Local Path"), tr("Choose an existing folder using an absolute path."));
            return;
        }
        if (clone && !QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) {
            QMessageBox::warning(&dialog, tr("Folder Is Not Empty"), tr("Choose an empty folder for the cloned repository."));
            return;
        }
        dialog.accept();
    });
    pathEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted) return QString();
    return QDir::cleanPath(pathEdit->text().trimmed());
}

void GitWidget::setLocalPath(const QString &path)
{
    mLocalPath = path;
    saveRepoLocalPath(mGroupId, path);
    refresh();
    mMainWidget->updateDisplay();
}

void GitWidget::onInitializeClicked()
{
    if (mGroupId.isEmpty() || !rsGit) return;
    std::vector<RsGitGroup> groups;
    std::list<RsGxsGroupId> ids({RsGxsGroupId(mGroupId.toStdString())});
    if (!rsGit->getGroups(ids, groups) || groups.empty() ||
        !IS_GROUP_ADMIN(groups[0].mMeta.mSubscribeFlags)) return;

    const QString path = chooseRepositoryPath(false);
    if (path.isEmpty()) return;
    std::string error;
    if (GitManager::isValidRepository(path.toStdString(), &error)) {
        setLocalPath(path);
        return;
    }
    // Do not reinitialize an existing repository that failed to open (for
    // example because of ownership, permissions, or damaged metadata).
    QDir dir(path);
    QFileInfo metadata(dir.filePath(".git"));
    if (metadata.exists() || metadata.isSymLink() ||
        dir.exists("HEAD") || dir.exists("objects")) {
        QMessageBox::critical(this, tr("Cannot Open Repository"),
            tr("This folder contains Git metadata and will not be reinitialized.\n\n%1")
                .arg(QString::fromStdString(error)));
        return;
    }
    if (!GitManager::initRepository(path.toStdString(), false, &error)) {
        QMessageBox::critical(this, tr("Initialization Failed"), QString::fromStdString(error));
        return;
    }
    setLocalPath(path);
    QMessageBox::information(this, tr("Repository Initialized"),
        tr("Add your project files to this folder, then use Commit Changes to create your first commit. After committing, use Push / Publish."));
}

void GitWidget::onCloneClicked()
{
    if (mGroupId.isEmpty()) return;
    if (rsGit && rsGit->isCloneInProgress(RsGxsGroupId(mGroupId.toStdString()))) {
        refresh();
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

    const QString localPath = chooseRepositoryPath(true);
    if (localPath.isEmpty()) return;
    setLocalPath(localPath);

    RsGxsId ownId;
    if (rsIdentity) {
        std::list<RsGxsId> ownIds;
        rsIdentity->getOwnIds(ownIds);
        if (!ownIds.empty()) {
            ownId = ownIds.front();
        }
    }

    mMainWidget->logCloneAttempt(mGroupId, repoName, QString::fromStdString(creatorId.toStdString()), ownId, localPath);
    refresh();
}

void GitWidget::onForkClicked()
{
    if (!rsGit || !rsIdentity || mGroupId.isEmpty() || !mPendingForkPath.isEmpty()) return;
    std::vector<RsGitGroup> groups;
    if (!rsGit->getGroups({RsGxsGroupId(mGroupId.toStdString())}, groups) || groups.empty()) return;
    const RsGitGroup upstream = groups.front();
    if (IS_GROUP_ADMIN(upstream.mMeta.mSubscribeFlags)) return;
    std::list<RsGxsId> identities;
    rsIdentity->getOwnIds(identities);
    if (identities.empty()) {
        QMessageBox::warning(this, tr("Fork Repository"), tr("Create a RetroShare identity before forking a repository."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Fork repository"));
    dialog.setMinimumWidth(500);
    auto *layout = new QVBoxLayout(&dialog);
    auto *info = new QLabel(tr("Create your own repository with the original history. Choose a new empty folder for your fork."), &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(QString::fromStdString(upstream.mGroupName) + "-fork", &dialog);
    auto *identity = new QComboBox(&dialog);
    for (const auto &id : identities) {
        RsIdentityDetails details;
        rsIdentity->getIdDetails(id, details);
        identity->addItem(QString::fromStdString(details.mNickname) + " (" + QString::fromStdString(id.toStdString().substr(0, 8)) + ")",
            QString::fromStdString(id.toStdString()));
    }
    auto *path = new QLineEdit(&dialog);
    auto *choose = new QPushButton(tr("Choose..."), &dialog);
    auto *row = new QHBoxLayout;
    row->addWidget(path, 1);
    row->addWidget(choose);
    form->addRow(tr("Repository name"), name);
    form->addRow(tr("Owner identity"), identity);
    form->addRow(tr("Local path"), row);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->addButton(tr("Fork"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(choose, &QPushButton::clicked, &dialog, [&dialog, path]() {
        const QString dir = QFileDialog::getExistingDirectory(&dialog, tr("Select Empty Folder"), QDir::homePath());
        if (!dir.isEmpty()) path->setText(dir);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, name, path]() {
        const QString dir = path->text().trimmed();
        if (name->text().trimmed().isEmpty() || !QDir::isAbsolutePath(dir) || !QDir(dir).exists() ||
            !QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty()) {
            QMessageBox::warning(&dialog, tr("Fork Repository"), tr("Enter a repository name and choose an existing empty folder."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    mPendingFork = RsGitGroup();
    mPendingFork.mGroupName = name->text().trimmed().toStdString();
    mPendingFork.mGroupDescription = upstream.mGroupDescription;
    mPendingFork.mUpstreamGroupId = upstream.mMeta.mGroupId;
    mPendingFork.mMeta.mGroupName = mPendingFork.mGroupName;
    mPendingFork.mMeta.mAuthorId = RsGxsId(identity->currentData().toString().toStdString());
    // Preserve the original distribution boundary when publishing a fork.
    mPendingFork.mMeta.mGroupFlags = upstream.mMeta.mGroupFlags;
    mPendingFork.mMeta.mSignFlags = upstream.mMeta.mSignFlags;
    mPendingFork.mMeta.mCircleType = upstream.mMeta.mCircleType;
    mPendingFork.mMeta.mCircleId = upstream.mMeta.mCircleId;
    mPendingFork.mMeta.mInternalCircle = upstream.mMeta.mInternalCircle;
    mPendingForkPath = QDir::cleanPath(path->text().trimmed());
    // Prefer the downloaded repository; otherwise the existing transfer service downloads it.
    const std::string source = GitManager::getBareRepoPath(upstream.mMeta.mGroupId.toStdString());
    std::vector<std::string> branches;
    std::string current;
    if (GitManager::getBranches(source, branches, current) && !branches.empty()) {
        if (GitManager::cloneRepository(source, mPendingForkPath.toStdString())) finishFork();
        else {
            mPendingForkPath.clear();
            QMessageBox::critical(this, tr("Fork Failed"), tr("Could not copy the downloaded repository into the chosen folder."));
        }
    } else {
        bool queued = rsGit->requestCloneOverTunnel(upstream.mMeta.mGroupId, upstream.mMeta.mAuthorId,
            mPendingFork.mMeta.mAuthorId, mPendingForkPath.toStdString());
        if (!queued) {
            mPendingForkPath.clear();
            QMessageBox::critical(this, tr("Fork Failed"), tr("Could not start downloading the original repository. Clone it first and retry."));
        } else QMessageBox::information(this, tr("Creating Fork"), tr("Downloading the original repository. Keep RetroGit open; your fork will be created when the transfer completes."));
    }
    refresh();
}

void GitWidget::finishFork()
{
    if (mPendingForkPath.isEmpty()) return;
    const QString path = mPendingForkPath;
    mPendingForkPath.clear();
    if (!GitManager::materializeForkBranches(path.toStdString())) {
        QMessageBox::critical(this, tr("Fork Failed"), tr("Could not create the fork. The downloaded files remain in your chosen folder."));
        return;
    }
    // Packing a large history takes minutes: never on the GUI thread.
    const RsGitGroup forkTemplate = mPendingFork;
    const QString packPath = mMainWidget->newTempPackPath(QString::fromStdString(forkTemplate.mUpstreamGroupId.toStdString()) + "-fork");
    auto refs = std::make_shared<std::map<std::string, std::string> >();
    auto size = std::make_shared<uint64_t>(0);
    QPointer<GitWidget> self(this);
    MainWidget *mainWidget = mMainWidget;
    mMainWidget->runWithProgress(tr("Preparing fork..."),
        [path, packPath, refs, size](RetroGitProgress &progress, QString &failure) {
            bool canceled = false;
            if (!GitManager::createPackfileToFile(path.toStdString(), packPath.toStdString(), *refs, {}, size.get(),
                                                  progress.reporter(), &canceled) || refs->empty()) {
                if (!canceled) failure = QObject::tr("Could not create the fork. The downloaded files remain in your chosen folder.");
                return false;
            }
            return true;
        },
        [self, mainWidget, path, packPath, refs, size, forkTemplate](bool ok, const QString &failure) {
            if (!ok) {
                QFile::remove(packPath);
                if (!failure.isEmpty()) QMessageBox::critical(self, QObject::tr("Fork Failed"), failure);
                return;
            }
            RsGitGroup fork = forkTemplate;
            if (!rsGit || !rsGit->createGroup(fork)) {
                QFile::remove(packPath);
                QMessageBox::critical(self, QObject::tr("Fork Failed"), QObject::tr("Could not create the fork. The downloaded files remain in your chosen folder."));
                return;
            }
            const QString id = QString::fromStdString(fork.mMeta.mGroupId.toStdString());
            Settings->beginGroup("RetroGit_WorkingDirs");
            Settings->setValue(id, path);
            Settings->endGroup();
            const std::string bare = GitManager::getBareRepoPath(fork.mMeta.mGroupId.toStdString());
            const std::string upstream = GitManager::getBareRepoPath(fork.mUpstreamGroupId.toStdString());
            const std::map<std::string, std::string> forkRefs = *refs;
            mainWidget->runWithProgress(QObject::tr("Setting up fork..."),
                [bare, upstream, path, packPath, forkRefs](RetroGitProgress &progress, QString &failure) {
                    if (!GitManager::unpackPackfileFromFile(bare, packPath.toStdString(), forkRefs, progress.reporter()) ||
                        !GitManager::configureFork(path.toStdString(), bare, upstream)) {
                        if (!progress.canceled)
                            failure = QObject::tr("The fork was created, but local setup failed. Your files are preserved; select the fork under My Repositories and retry publishing.");
                        return false;
                    }
                    return true;
                },
                [self, mainWidget, fork, packPath, forkRefs, size](bool ok, const QString &failure) {
                    if (!ok) {
                        QFile::remove(packPath);
                        if (!failure.isEmpty()) QMessageBox::warning(self, QObject::tr("Fork Created"), failure);
                        mainWidget->updateDisplay();
                        return;
                    }
                    RsGitUpdate update;
                    update.mMeta.mGroupId = fork.mMeta.mGroupId;
                    update.mMeta.mAuthorId = fork.mMeta.mAuthorId;
                    update.mMeta.mMsgName = "Initial fork history";
                    update.mRefUpdates = forkRefs;
                    mainWidget->publishPackFile(QString::fromStdString(fork.mMeta.mGroupId.toStdString()), packPath, *size, update);
                    mainWidget->updateDisplay();
                    if (self)
                        self->ui->mLblOwnerInfo->setText(QObject::tr("<b>Fork created:</b> It is available under My Repositories. Create a branch, commit your changes, then open Pull Requests to propose them upstream."));
                });
        });
}

void GitWidget::onPullClicked()
{
    if (mGroupId.isEmpty()) return;

    QString localPath = mLocalPath;
    if (localPath.isEmpty() || !QDir(localPath).exists() ||
        !GitManager::isValidRepository(localPath.toStdString())) {
        refresh();
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

void GitWidget::onRebaseClicked()
{
    if (mGroupId.isEmpty() || mLocalPath.isEmpty()) return;
    std::vector<RsGitGroup> groups;
    if (!rsGit || !rsGit->getGroups({RsGxsGroupId(mGroupId.toStdString())}, groups) || groups.empty() ||
        groups.front().mUpstreamGroupId.isNull()) return;
    std::vector<std::string> branches;
    std::string branch;
    const std::string upstream = GitManager::getBareRepoPath(groups.front().mUpstreamGroupId.toStdString());
    if (!GitManager::getBranches(upstream, branches, branch) || branch.empty()) {
        QMessageBox::warning(this, tr("Rebase Failed"), tr("The upstream repository has no available branch."));
        return;
    }
    std::string current;
    std::vector<std::string> localBranches;
    if (!GitManager::getBranches(mLocalPath.toStdString(), localBranches, current) || current.empty()) return;
    std::string error;
    if (GitManager::rebaseBranch(mLocalPath.toStdString(), upstream, current, branch, &error)) {
        refresh();
        QMessageBox::information(this, tr("Rebase Successful"), tr("Your fork branch was rebased onto the latest upstream branch."));
    } else QMessageBox::warning(this, tr("Rebase Failed"), QString::fromStdString(error));
}

void GitWidget::onCommitClicked()
{
    if (mGroupId.isEmpty()) return;
    QString localPath = mLocalPath;

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

    // Commit targets must exist in the working repository, not just its serving cache.
    std::vector<std::string> branches;
    std::string currentBranch;
    GitManager::getBranches(localPath.toStdString(), branches, currentBranch);

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

    // An unborn branch has a HEAD name but is absent from the branch iterator.
    // Keep it selectable for the initial commit, even if the sync repo has branches.
    if (!currentBranch.empty() && !qBranches.contains(QString::fromStdString(currentBranch))) {
        qBranches.append(QString::fromStdString(currentBranch));
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
    if (mGroupId.isEmpty() || mPushInProgress) return;
    QString localPath = mLocalPath;
    std::string repoPath = localPath.toStdString();

    std::string error;
    if (!GitManager::isValidRepository(repoPath, &error)) {
        QMessageBox::critical(this, tr("Push Failed"),
            tr("Cannot open the local Git repository. For a new project folder, use Initialize Repository first.\n\n%1")
                .arg(QString::fromStdString(error)));
        return;
    }

    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
    }
    const std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    // Only send objects that earlier updates did not publish yet. Every tenth
    // update is self-contained so peers can still clone after old messages expire.
    std::set<std::string> published;
    if (!updates.empty() && updates.size() % 10 != 0) {
        for (const auto &update : updates)
            for (const auto &ref : update.mRefUpdates) published.insert(ref.second);
        // The local serving cache must be able to import the pack as well
        // (e.g. after moving to another computer it starts empty).
        published = GitManager::filterExistingCommits(bareRepoPath, published);
    }

    std::map<std::string, std::string> refUpdates;
    if (!GitManager::getPublishableRefs(repoPath, refUpdates)) {
        QMessageBox::critical(this, tr("Push Failed"), tr("Failed to open local Git repository. Ensure the directory is a valid Git repository."));
        return;
    }
    if (refUpdates.empty()) {
        QMessageBox::information(this, tr("Nothing to Publish"),
            tr("Add files and use Commit Changes to create your first commit before publishing."));
        return;
    }
    std::vector<std::string> publishedRefs;
    bool allAlreadyPublished = true;
    for (const auto &pair : refUpdates) {
        bool thisRefPublished = false;
        for (const auto &update : updates) {
            if (update.mRefUpdates.count(pair.first) && update.mRefUpdates.at(pair.first) == pair.second) {
                thisRefPublished = true;
                break;
            }
        }
        if (thisRefPublished) publishedRefs.push_back(pair.first);
        else allAlreadyPublished = false;
    }
    if (allAlreadyPublished) {
        QMessageBox::information(this, tr("Already Published"), tr("This commit history has already been published to GXS."));
        return;
    }
    // Do not rewind published branches when pushing a different local branch.
    for (const auto& name : publishedRefs) refUpdates.erase(name);

    // Packing and importing a large history takes minutes: never on the GUI thread.
    struct PushResult { uint64_t size = 0; std::string commitMsg = "Push Update"; };
    auto result = std::make_shared<PushResult>();
    const QString groupId = mGroupId;
    const QString packPath = mMainWidget->newTempPackPath(groupId);
    mPushInProgress = true;
    ui->mBtnPush->setEnabled(false);

    QPointer<GitWidget> self(this);
    MainWidget *mainWidget = mMainWidget;
    mMainWidget->runWithProgress(tr("Publishing repository..."),
        [repoPath, bareRepoPath, packPath, published, refUpdates, result](RetroGitProgress &progress, QString &failure) {
            std::map<std::string, std::string> allRefs;
            bool canceled = false;
            if (!GitManager::createPackfileToFile(repoPath, packPath.toStdString(), allRefs, published,
                                                  &result->size, progress.reporter(), &canceled)) {
                if (!canceled) failure = QObject::tr("Failed to create the packfile. Check disk space and retry.");
                return false;
            }
            // Import locally first so this client can serve clones immediately.
            if (!GitManager::unpackPackfileFromFile(bareRepoPath, packPath.toStdString(), refUpdates, progress.reporter())) {
                QFile::remove(packPath);
                if (!progress.canceled)
                    failure = QObject::tr("Failed to update the local repository used to serve clones. Check disk space and file permissions, then retry.");
                return false;
            }
            // Name the update after what it publishes: the commit of a single
            // branch, otherwise the number of branches and tags.
            if (refUpdates.size() == 1) {
                std::vector<GitCommitInfo> latestCommits;
                if (GitManager::getCommitLog(repoPath, latestCommits, refUpdates.begin()->first) && !latestCommits.empty()) {
                    result->commitMsg = latestCommits[0].message;
                    while (!result->commitMsg.empty() && (result->commitMsg.back() == '\n' || result->commitMsg.back() == '\r'))
                        result->commitMsg.pop_back();
                }
            } else {
                result->commitMsg = QObject::tr("%1 branches and tags").arg(refUpdates.size()).toStdString();
            }
            return true;
        },
        [self, mainWidget, groupId, packPath, refUpdates, result](bool ok, const QString &failure) {
            if (self) {
                self->mPushInProgress = false;
                self->refresh();
            }
            if (!ok) {
                QFile::remove(packPath);
                if (!failure.isEmpty()) QMessageBox::critical(self, QObject::tr("Push Failed"), failure);
                return;
            }
            RsGitUpdate update;
            update.mMeta.mGroupId = RsGxsGroupId(groupId.toStdString());
            update.mRefUpdates = refUpdates;
            update.mMeta.mMsgName = QString("New Commit: %1").arg(QString::fromStdString(result->commitMsg)).toStdString();
            mainWidget->publishPackFile(groupId, packPath, result->size, update);
        });
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
    QString rawPath = mLocalPath;
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists() && GitManager::isValidRepository(localPath.toStdString())) {
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
        QString localPath = mLocalPath;
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
        // Non-blocking: the backend reports the change, which refreshes this view.
        uint32_t token;
        rsGit->setMessageReadStatus(token, RsGxsGrpMsgIdPair(RsGxsGroupId(mGroupId.toStdString()), msgId), markRead);
    }
}

void GitWidget::populateCommitLog(const std::vector<GitLocalChange> *workingChanges)
{
    if (mGroupId.isEmpty()) {
        ui->mCommitTable->setRowCount(0);
        mLastGroupId.clear();
        mLastRepoPath.clear();
        mLastSelectedBranch.clear();
        mLastCommitHashes.clear();
        mLastLocalChanges.clear();
        mLastUpdates.clear();
        return;
    }

    std::string repoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    QString rawPath = mLocalPath;
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists() && GitManager::isValidRepository(localPath.toStdString())) {
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
    GitManager::getCommitLog(repoPath, commits, selectedBranch);

    std::vector<GitLocalChange> localChanges;
    bool hasLocalChanges = false;
    // Never computed here: the status of the working folder comes from the
    // background check (refresh()); the serving cache has no working tree.
    bool statusOk = false;
    if (workingChanges && !rawPath.isEmpty() && repoPath == QDir::cleanPath(rawPath).toStdString()) {
        localChanges = *workingChanges;
        statusOk = true;
    }
    if (statusOk && !localChanges.empty()) {
        hasLocalChanges = true;
    }

    std::vector<RsGitUpdate> updates;
    if (rsGit) {
        rsGit->getUpdates(RsGxsGroupId(mGroupId.toStdString()), updates);
    }

    // State comparison for early return (detect real-time changes without clearing selection/flickering)
    std::vector<std::string> currentCommitHashes;
    for (const auto &c : commits) {
        currentCommitHashes.push_back(c.full_hash);
    }

    std::vector<std::pair<std::string, char>> currentLocalChanges;
    for (const auto &c : localChanges) {
        currentLocalChanges.push_back({c.path, c.status});
    }

    std::vector<std::pair<std::string, uint32_t>> currentUpdates;
    for (const auto &u : updates) {
        currentUpdates.push_back({u.mMeta.mMsgId.toStdString(), u.mMeta.mMsgStatus});
    }

    if (mGroupId == mLastGroupId &&
        repoPath == mLastRepoPath &&
        selectedBranch == mLastSelectedBranch &&
        currentCommitHashes == mLastCommitHashes &&
        currentLocalChanges == mLastLocalChanges &&
        currentUpdates == mLastUpdates) {
        return; // No changes to display, skip clearing/rebuilding the table
    }

    mLastGroupId = mGroupId;
    mLastRepoPath = repoPath;
    mLastSelectedBranch = selectedBranch;
    mLastCommitHashes = currentCommitHashes;
    mLastLocalChanges = currentLocalChanges;
    mLastUpdates = currentUpdates;

    ui->mCommitTable->setRowCount(0);

    std::set<std::string> localCommitShas;
    for (const auto &c : commits) {
        localCommitShas.insert(c.hash);
        localCommitShas.insert(c.full_hash);
    }

    // Look up only the published commits instead of walking the whole history.
    std::set<std::string> publishedShas;
    for (const auto &update : updates)
        for (const auto &pair : update.mRefUpdates) publishedShas.insert(pair.second);
    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    const std::set<std::string> allRepoCommitShas = GitManager::filterExistingCommits(
        GitManager::isValidRepository(repoPath) ? repoPath : bareRepoPath, publishedShas);

    std::vector<RsGitUpdate> undownloadedUpdates;
    std::set<std::string> unreadCommitShas;
    std::map<std::string, std::pair<RsGxsMessageId, bool>> commitToMsgId;
    std::map<std::string, std::string> commitToMsgName;
    std::map<std::string, RsGxsId> commitToAuthorId;

    for (const auto &update : updates) {
        bool isUnread = (update.mMeta.mMsgStatus & GXS_SERV::GXS_MSG_STATUS_GUI_UNREAD);
        // An update is considered downloaded only when ALL its ref SHAs exist locally.
        // This prevents updates with mixed old+new branch refs from being hidden.
        bool isDownloaded = !update.mRefUpdates.empty();
        for (const auto &pair : update.mRefUpdates) {
            // Never overwrite an UNREAD entry with a READ entry for the same commit SHA.
            // This prevents a later READ GXS update (containing an unchanged branch ref) from
            // hiding the unread notification of the original push that introduced this commit.
            auto existingIt = commitToMsgId.find(pair.second);
            bool shouldUpdate = (existingIt == commitToMsgId.end()) ||  // no entry yet
                                isUnread ||                              // new entry is unread (always wins)
                                !existingIt->second.second;              // existing is also read (safe to overwrite)
            if (shouldUpdate) {
                commitToMsgId[pair.second] = {update.mMeta.mMsgId, isUnread};
                commitToMsgName[pair.second] = update.mMeta.mMsgName;
                commitToAuthorId[pair.second] = update.mMeta.mAuthorId;
            }
            if (isUnread) {
                unreadCommitShas.insert(pair.second);
            }
            if (!localCommitShas.count(pair.second) && !allRepoCommitShas.count(pair.second)) {
                isDownloaded = false;
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
        QString dateStr = QLocale::system().toString(
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(update.mMeta.mPublishTs)),
            QLocale::ShortFormat);
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
        // Queued: the handler may rebuild the table and delete this button.
        connect(btnStatus, &QPushButton::clicked, this, [this, msgIdStr, isUnread]() {
            onCommitReadStatusToggled(msgIdStr, isUnread);
        }, Qt::QueuedConnection);
        ui->mCommitTable->setCellWidget(rowIdx, 4, btnStatus);

        // Pulling an individual published update is a subscriber workflow.
        // Repository owners publish their local commits instead of pulling each row.
        if (!isAdmin && !creatorId.isNull()) {
            QPushButton *btnPull = new QPushButton(isAdmin ? tr("Update Working Folder") : tr("Pull"), ui->mCommitTable);
            btnPull->setStyleSheet("QPushButton { font-weight: bold; color: #2980b9; }");
            QString localPath = mLocalPath;
            connect(btnPull, &QPushButton::clicked, this, [this, creatorId, ownId, localPath, repoName]() {
                mMainWidget->logPullAttempt(mGroupId, repoName, QString::fromStdString(creatorId.toStdString()), ownId, localPath);
            }, Qt::QueuedConnection);
            ui->mCommitTable->setCellWidget(rowIdx, 5, btnPull);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }

    // Commits made with RetroGit use "<nickname>@<identity id>" as e-mail.
    // Only that proves a RetroShare identity wrote the commit: Git names are
    // free text (a GitHub author may use the same name as an identity).
    auto identityFromEmail = [](const std::string &email) {
        const size_t at = email.rfind('@');
        if (at == std::string::npos) return RsGxsId();
        const std::string hex = email.substr(at + 1);
        if (hex.size() != 32 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            return RsGxsId();
        return RsGxsId(hex);
    };

    // 2. Populate downloaded commits
    int undownloadedCount = undownloadedUpdates.size();
    for (int i = 0; i < (int)commits.size(); i++) {
        int rowIdx = undownloadedCount + i + offset;

        QTableWidgetItem *itemHash = new QTableWidgetItem(QString::fromStdString(commits[i].hash));
        itemHash->setData(Qt::UserRole, QString::fromStdString(commits[i].full_hash));

        // A downloaded commit always shows its own Git author and message. One
        // published update can move many branches at once: its name and its
        // publisher only describe the update (shown in the tooltip).
        RsGxsId gxsAuthorId;
        auto itAuth = commitToAuthorId.find(commits[i].full_hash);
        if (itAuth == commitToAuthorId.end()) {
            itAuth = commitToAuthorId.find(commits[i].hash);
        }
        if (itAuth != commitToAuthorId.end()) {
            gxsAuthorId = itAuth->second;
        }

        QTableWidgetItem *itemAuth = new QTableWidgetItem(QString::fromStdString(commits[i].author));
        itemAuth->setIcon(FilesDefs::getIconFromQtResourcePath(":/icons/svg/people2.svg"));

        // Show a RetroShare identity only for commits made with RetroGit.
        const RsGxsId commitIdentity = identityFromEmail(commits[i].email);

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

        QTableWidgetItem *itemMsg = new QTableWidgetItem(QString::fromStdString(commits[i].message));
        if (isUnread) {
            QFont font = itemMsg->font();
            font.setBold(true);
            itemMsg->setFont(font);
        }

        auto nameIt = commitToMsgName.find(commits[i].full_hash);
        if (nameIt == commitToMsgName.end()) {
            nameIt = commitToMsgName.find(commits[i].hash);
        }
        if (hasGxsMsg) {
            QString publisher = tr("Anonymous");
            if (!gxsAuthorId.isNull()) {
                publisher = QString::fromStdString(gxsAuthorId.toStdString()).left(8);
                RsIdentityDetails details;
                if (rsIdentity && rsIdentity->getIdDetails(gxsAuthorId, details) && !details.mNickname.empty())
                    publisher = QString::fromStdString(details.mNickname);
            }
            QString updateName = nameIt != commitToMsgName.end() ? QString::fromStdString(nameIt->second) : QString();
            if (updateName.startsWith("New Commit:")) updateName = updateName.mid(11).trimmed();
            const QString tip = tr("Published by %1 in the update \"%2\"").arg(publisher, updateName);
            itemHash->setToolTip(tip);
            itemMsg->setToolTip(tip);
        }

        ui->mCommitTable->setItem(rowIdx, 0, itemHash);
        ui->mCommitTable->setItem(rowIdx, 1, itemMsg);
        if (!commitIdentity.isNull()) {
            // The identity widget replaces the plain Git name and icon:
            // leave the cell underneath empty so they are not drawn twice.
            itemAuth->setText(QString());
            itemAuth->setIcon(QIcon());
            itemAuth->setToolTip(QString::fromStdString(commits[i].author));
        }
        ui->mCommitTable->setItem(rowIdx, 2, itemAuth);
        if (!commitIdentity.isNull()) {
            GxsIdTableItem *authorWidget = new GxsIdTableItem(ui->mCommitTable);
            authorWidget->setId(commitIdentity);
            ui->mCommitTable->setCellWidget(rowIdx, 2, authorWidget);
        }
        ui->mCommitTable->setItem(rowIdx, 3, itemDate);

        if (hasGxsMsg) {
            QPushButton *btnStatus = new QPushButton(isUnread ? tr("Mark Read") : tr("Mark Unread"), ui->mCommitTable);
            if (isUnread) {
                btnStatus->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            } else {
                btnStatus->setStyleSheet("QPushButton { color: #7f8c8d; }");
            }
            QString msgIdStr = QString::fromStdString(gxsMsgId.toStdString());
            connect(btnStatus, &QPushButton::clicked, this, [this, msgIdStr, isUnread]() {
                onCommitReadStatusToggled(msgIdStr, isUnread);
            }, Qt::QueuedConnection);
            ui->mCommitTable->setCellWidget(rowIdx, 4, btnStatus);
        } else {
            QTableWidgetItem *itemStatus = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 4, itemStatus);
        }

        if (isAdmin && !hasGxsMsg && i == 0) {
            QPushButton *btnPushRow = new QPushButton(tr("Push"), ui->mCommitTable);
            btnPushRow->setStyleSheet("QPushButton { font-weight: bold; color: #27ae60; }");
            connect(btnPushRow, &QPushButton::clicked, this, &GitWidget::onPushClicked, Qt::QueuedConnection);
            ui->mCommitTable->setCellWidget(rowIdx, 5, btnPushRow);
        } else {
            QTableWidgetItem *itemAction = new QTableWidgetItem("-");
            ui->mCommitTable->setItem(rowIdx, 5, itemAction);
        }
    }

    bool hasPath = !rawPath.isEmpty();
    bool pathExists = hasPath && QDir(QDir::cleanPath(rawPath)).exists();

    ui->mBtnPush->setEnabled(isAdmin && pathExists && !mPushInProgress);
    ui->mBtnPush->setToolTip(tr("Publish local commits to this repository."));
    ui->mBtnCommit->setEnabled(hasPath && pathExists && isAdmin && hasLocalChanges);
}

QString GitWidget::getLocalPath() const
{
    return mLocalPath;
}
