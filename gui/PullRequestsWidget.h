/*******************************************************************************
 * gui/PullRequestsWidget.h                                                    *
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

#ifndef PULLREQUESTSWIDGET_H
#define PULLREQUESTSWIDGET_H

#include <QWidget>
#include <QString>
#include <vector>
#include "interface/rsGit.h"

namespace Ui {
class PullRequestsWidget;
}

class MainWidget;

class PullRequestsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PullRequestsWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent = nullptr);
    ~PullRequestsWidget();

    void refresh();

private slots:
    void onNewPRClicked();
    void onFilterTextChanged(const QString &text);
    void onRowDoubleClicked(int row, int column);
    void onViewPRDetailsClicked(const QString &msgIdStr);
    void onOpenFilterClicked();
    void onClosedFilterClicked();

private:
    void populatePRList();
    void updateFilterButtons(bool showOpenOnly, bool showClosedOnly);

    Ui::PullRequestsWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
};

#endif // PULLREQUESTSWIDGET_H
