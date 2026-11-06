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
#include "interface/rsGit.h"

class QComboBox;
class QTreeWidget;
class QLineEdit;
class QTextEdit;
class QCheckBox;
class QDateEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class MainWidget;

class MilestonesWidget : public QWidget
{
public:
    MilestonesWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent);
    QString groupId() const { return QString::fromStdString(mGroupId.toStdString()); }
private:
    void reload();
    void populate();
    void select();
    void filterRequests();
    void createMilestoneDialog();
    void showDetailsDialog(const QString &id);
    void save();
    void remove();
    bool publish(const QJsonArray &milestones);
    QString selectedId() const;
    RsGxsGroupId mGroupId;
    std::string mSnapshot;
    QJsonArray mMilestones;
    std::vector<RsGitPullRequest> mPullRequests;
    bool mOwner = false;
    QComboBox *mFilter;
    QComboBox *mPRFilter;
    QTreeWidget *mList;
    QWidget *mEmptyState;
    QPushButton *mEmptyCreate;
    QTreeWidget *mRequests;
    QLineEdit *mTitle;
    QTextEdit *mDescription;
    QCheckBox *mHasDueDate;
    QDateEdit *mDueDate;
    QCheckBox *mClosed;
    QPushButton *mNew;
    QPushButton *mSave;
    QPushButton *mDelete;
    QLabel *mSummary;
};
#endif
