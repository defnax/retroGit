/*******************************************************************************
 * gui/PullRequestDetailsWidget.h                                              *
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

#ifndef PULLREQUESTDETAILSWIDGET_H
#define PULLREQUESTDETAILSWIDGET_H

#include <QWidget>
#include <QString>
#include "interface/rsGit.h"

namespace Ui {
class PullRequestDetailsWidget;
}

class MainWidget;

class PullRequestDetailsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PullRequestDetailsWidget(const QString &groupId, const RsGxsMessageId &msgId, MainWidget *mainWidget, QWidget *parent = nullptr);
    ~PullRequestDetailsWidget();

    void refresh();
    const RsGxsMessageId& getMsgId() const { return mMsgId; }

private slots:
    void onMergeClicked();
    void onCloseClicked();
    void onFileSelectionChanged();

private:
    void loadPRDetails();
    void populateConversationTab();
    void populateFilesChangedTab();
    bool selectFirstFileItem(class QTreeWidgetItem *item);

    Ui::PullRequestDetailsWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
    RsGxsMessageId mMsgId;

    RsGitPullRequest mPR;
    bool mLoaded;
};

#endif // PULLREQUESTDETAILSWIDGET_H
