/*******************************************************************************
 * gui/GitWidget.h                                                             *
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

#ifndef GITWIDGET_H
#define GITWIDGET_H

#include <QWidget>
#include <QString>
#include <memory>
#include <vector>
#include <string>
#include <utility>
#include "interface/rsGit.h"

struct GitLocalChange;

namespace Ui {
class GitWidget;
}

class MainWidget;

class GitWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GitWidget(MainWidget *mainWidget, QWidget *parent = nullptr);
    ~GitWidget();

    void setGroupId(const QString &groupId);
    void handleGitEvent(const RsGitEvent *e);
    void clear();
    void refresh();
    QString getLocalPath() const;

private slots:
    void onInitializeClicked();
    void onCloneClicked();
    void onForkClicked();
    void onCommitClicked();
    void onPushClicked();
    void onPullClicked();
    void onRebaseClicked();
    void onCommitSelectionChanged();
    void onCommitTableContextMenu(const QPoint &pos);
    void onCommitReadStatusToggled(const QString &msgIdStr, bool markRead);
    void pollWorkingTree();

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool mNeedsSubscription = false;
    void finishFork();
    RsGitGroup mPendingFork;
    QString mPendingForkPath;
    QString chooseRepositoryPath(bool clone);
    void setLocalPath(const QString &path);
    void populateCommitLog(const std::vector<GitLocalChange> *workingChanges = nullptr);
    void saveRepoLocalPath(const QString &groupId, const QString &path);
    QString loadRepoLocalPath(const QString &groupId);

    Ui::GitWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
    QString mLocalPath;

    QString mLastGroupId;
    std::string mLastRepoPath;
    std::string mLastSelectedBranch;
    std::vector<std::string> mLastCommitHashes;
    std::vector<std::pair<std::string, char>> mLastLocalChanges;
    std::vector<std::pair<std::string, uint32_t>> mLastUpdates;

    std::string mLastWorkingTreeState;
    bool mPollInFlight = false;
};

#endif // GITWIDGET_H
