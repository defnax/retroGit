/*******************************************************************************
 * gui/PushesWidget.h                                                          *
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

#ifndef PUSHESWIDGET_H
#define PUSHESWIDGET_H

#include <QWidget>
#include <QString>
#include <vector>
#include "interface/rsGit.h"

namespace Ui {
class PushesWidget;
}

class MainWidget;

class PushesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PushesWidget(MainWidget *mainWidget, QWidget *parent = nullptr);
    ~PushesWidget();

    void setGroupId(const QString &groupId);
    void handleGitEvent(const RsGitEvent *e);
    void clear();
    void refresh();

    struct CloneRecord {
        QString repoId;
        QString repoName;
        QString ownerId;
        QString status;
        QString time;
    };

    void addCloneRecord(const CloneRecord &rec);
    std::vector<CloneRecord>& getCloneHistory();
    void updateClonesTable();

private slots:
    void pollDownloadProgress();
    void onDownloadClicked();
    void onCancelDownloadClicked();
    void onClonesTableContextMenu(const QPoint &pos);

private:
    void populatePackfiles();
    void populateClonesTable();

    Ui::PushesWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
    std::vector<RsGitUpdate> mAvailableUpdates;
    std::vector<CloneRecord> mCloneHistory;
    class QTimer *mDownloadPollTimer;
};

#endif // PUSHESWIDGET_H
