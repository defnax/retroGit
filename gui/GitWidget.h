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
#include "interface/rsGit.h"

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
    void onBrowseClicked();
    void onCloneClicked();
    void onCommitClicked();
    void onPushClicked();
    void onPullClicked();
    void onLocalPathChanged(const QString &text);
    void onCommitSelectionChanged();
    void onCommitTableContextMenu(const QPoint &pos);
    void onCommitReadStatusToggled(const QString &msgIdStr, bool markRead);

private:
    void populateCommitLog();
    void saveRepoLocalPath(const QString &groupId, const QString &path);
    QString loadRepoLocalPath(const QString &groupId);

    Ui::GitWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
};

#endif // GITWIDGET_H
