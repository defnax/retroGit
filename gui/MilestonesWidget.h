/*******************************************************************************
 * gui/MilestonesWidget.h                                                      *
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

#ifndef RETROGIT_MILESTONES_WIDGET_H
#define RETROGIT_MILESTONES_WIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <vector>
#include "interface/rsGit.h"

namespace Ui {
class MilestonesWidget;
}

class MainWidget;

class MilestonesWidget : public QWidget
{
    Q_OBJECT

public:
    MilestonesWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent = nullptr);
    ~MilestonesWidget() override;

    QString groupId() const { return QString::fromStdString(mGroupId.toStdString()); }

private:
    void reload();
    void populate();
    void showContextMenu(const QPoint &pos);
    void createMilestone();
    void openMilestone(const QString &id);
    void deleteMilestone(const QString &id);
    // Replaces (or appends) one milestone; its pull requests are removed from
    // every other milestone, since a pull request belongs to one milestone.
    QJsonArray withMilestone(const QJsonObject &milestone) const;
    bool publish(const QJsonArray &milestones);

    Ui::MilestonesWidget *ui;
    RsGxsGroupId mGroupId;
    std::string mSnapshot;
    QJsonArray mMilestones;
    std::vector<RsGitPullRequest> mPullRequests;
    bool mOwner = false;
    bool mValid = false;
};

#endif
