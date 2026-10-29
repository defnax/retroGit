/*******************************************************************************
 * gui/MainWidget.h                                                            *
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

#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <retroshare-gui/mainpage.h>

#include "gui/RetroGitNotify.h"
#include <retroshare/rsfiles.h>
#include <retroshare/rspeers.h>
#include "interface/rsGit.h"

#include "gui/common/GroupTreeWidget.h"
#include <list>
#include <retroshare/rsevents.h>
#include <retroshare/rsgxsifacetypes.h>

#include <QPoint>
#include <QPointer>
#include <QWidget>
#include <set>
#include <atomic>
#include <functional>
#include <mutex>
#include "services/GitManager.h"


#include "gui/gxs/GxsIdDetails.h"

#include "gui/GitWidget.h"
#include "gui/CodeWidget.h"
#include "gui/PushesWidget.h"

class QAction;
class QLabel;
class QTimer;
#include <QMessageBox>

/** Progress shared between a background job and its progress dialog. */
struct RetroGitProgress
{
    std::atomic<bool> canceled{false};
    std::atomic<uint64_t> current{0};
    std::atomic<uint64_t> total{0};
    std::mutex mutex;
    std::string stage;

    /** Callback for GitManager; returns false once the user canceled. */
    GitManager::ProgressFn reporter()
    {
        return [this](const char *name, uint64_t value, uint64_t maximum) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stage = name;
            }
            current = value;
            total = maximum;
            return !canceled.load();
        };
    }
};

/** One file (or untracked folder, ending in '/') in the commit details tree. */
struct ChangedFileEntry {
    QString path;
    char status;
    QString color; // empty: default text color
};

class GxsIdTableItem : public QWidget
{
    Q_OBJECT
public:
    explicit GxsIdTableItem(QWidget *parent = nullptr);
    void setId(const RsGxsId &id);
    bool getId(RsGxsId &id) const;

private:
    static void fillCallback(GxsIdDetailsType type, const RsIdentityDetails &details, QObject *object, const QVariant &data);
    RsGxsId mId;
    QLabel *mIconLabel;
    QLabel *mTextLabel;
};

namespace Ui
{
class MainWidget;
}

class MainWidget : public MainPage
{
    Q_OBJECT

public:
    explicit MainWidget(QWidget *parent, RetroGitNotify *notify);
    ~MainWidget();

    virtual class UserNotify *createUserNotify(QObject *parent) override;

    // Public Coordinator/Helper methods for sub-widgets
    void logCloneAttempt(const QString &groupId, const QString &repoName, const QString &ownerId, const RsGxsId &ownId, const QString &localPath);
    void logPullAttempt(const QString &groupId, const QString &repoName, const QString &ownerId, const RsGxsId &ownId, const QString &localPath);
    void hashAndPublishPackfile(const QString &groupId, const std::string &packfileData, RsGitUpdate &update);
    bool publishPullRequestWithPack(RsGitPullRequest &pr, const std::string &pack);
    bool preparePackAttachment(const QString &groupId, const std::string &pack, RsGxsFile &attachment);
    /** A new temporary pack path for a repository. */
    QString newTempPackPath(const QString &groupId);
    /** Share an already written pack file through RetroShare file transfer. */
    bool hashPackFile(const QString &packPath, RsGxsFile &attachment);
    /** Publish an update whose pack was written to packPath (inline or as attachment). */
    void publishPackFile(const QString &groupId, const QString &packPath, uint64_t size, RsGitUpdate &update);
    /** Run long work in the background with a cancelable progress dialog.
     *  'work' runs on a worker thread, 'done' on the GUI thread (error is empty when canceled). */
    void runWithProgress(const QString &title,
                         std::function<bool(RetroGitProgress &progress, QString &error)> work,
                         std::function<void(bool ok, const QString &error)> done);
    void refreshGitWidget();
    void refreshCodeWidget();
    void showCommitDetails(const QString &commitHash, const QString &repoPath);
    void hideCommitDetails();
    void showDiffForCommit(const QString &commitHash);
    void showPullRequests(const QString &groupId);
    void showPullRequestDetails(const QString &groupId, const RsGxsMessageId &msgId);
    void markRepositoryAsRead();
    void triggerTreeSelectionChanged();
    QString getLocalPath() const;
    QString getSelectedGroupId() const;
    /** Show a single non-modal notice (safe to call from event handlers). */
    void showNotice(const QString &title, const QString &text);
    class GitWidget* getGitWidget() const { return mGitWidget; }
    class CodeWidget* getCodeWidget() const { return mCodeWidget; }

public slots:
    void updateDisplay();

protected:
    virtual void showEvent(QShowEvent *event) override;
    virtual void changeEvent(QEvent *event) override;

private slots:
    void createGroup();
    void selectGroupSet(int index);
    void groupListCustomPopupMenu(QPoint point);
    void showGroupDetails();
    void editGroupDetails();
    void subscribeToGroup();
    void unsubscribeFromGroup();
    
    void onTreeSelectionChanged();
    void onChangedFilesContextMenu(const QPoint &pos);
    void onChangedFilesDoubleClicked(class QTreeWidgetItem *item, int column);
    void onTabCloseRequested(int index);

private:
    void showDiffForFile(const QString &filePath);
    void fillChangedFilesTree(const std::vector<ChangedFileEntry> &entries, bool prefixStatus);
    quint64 mDetailsRequestId = 0;
    void refreshCurrentRepo();
    void loadGroupMeta();
    void saveRepoLocalPath(const QString &groupId, const QString &path);
    QString loadRepoLocalPath(const QString &groupId);
    void insertGroupsData(const std::list<RsGroupMetaData> &gitList);
    void GroupMetaDataToGroupItemInfo(const RsGroupMetaData &groupInfo,
                                    GroupItemInfo &groupItemInfo);
    void handleEvent_main_thread(std::shared_ptr<const RsEvent> event);
    void scheduleEventProcessing();
    void processPendingEvents();
    void checkNewPostNotices(const RsGxsGroupId &groupId);
    void processSettings(bool load);
    void applyRetroGitLightStyles();

    QTreeWidgetItem *mActiveGroupsItem;
    int mGroupSet = 0;
    bool mInitialLoadDone = false;
    bool mGroupMetaLoadPending = false;
    bool mGroupMetaReloadRequested = false;

    RsEventsHandlerId_t mEventHandlerId = 0;

    // Coalesced backend events (see processPendingEvents()).
    QTimer *mEventTimer = nullptr;
    bool mPendingGroupReload = false;
    bool mPendingRepoRefresh = false;
    bool mPendingSelectionRefresh = false;
    bool mPendingPullRequestRefresh = false;
    std::set<RsGxsGroupId> mPendingNewPostGroups;
    int mModalDepth = 0;
    QPointer<QMessageBox> mNotice;

    Ui::MainWidget *ui;
    
    GitWidget *mGitWidget;
    CodeWidget *mCodeWidget;
    PushesWidget *mPushesWidget;
    QString mCurrentCommitHash;

    // UI elements for commit details (left pane)
    class QWidget *mCommitDetailsWidget;
    class QFrame *mDetailsAuthorFrame;
    class QLabel *mDetailsAuthorNameLabel;
    class QLabel *mDetailsAuthorEmailLabel;
    class QLabel *mDetailsHashLabel;
    class QLabel *mDetailsTitleLabel;
    class QLabel *mDetailsBodyText;
    class QLabel *mDetailsDateLabel;
    class QTreeWidget *mChangedFilesTree;

    FontSizeHandler mFontSizeHandler;
};

#endif // MAINPAGE_H
