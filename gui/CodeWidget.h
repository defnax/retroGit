/*******************************************************************************
 * gui/CodeWidget.h                                                            *
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

#ifndef CODEWIDGET_H
#define CODEWIDGET_H

#include <QWidget>
#include <QString>
#include "interface/rsGit.h"

namespace Ui {
class CodeWidget;
}

class MainWidget;

class CodeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CodeWidget(MainWidget *mainWidget, QWidget *parent = nullptr);
    ~CodeWidget();

    void setGroupId(const QString &groupId);
    void handleGitEvent(const RsGitEvent *e);
    void clear();
    void refresh();

private slots:
    void onOpenFolderClicked();
    void openSelectedFile();
    void onRepoBrowserContextMenu(const QPoint &pos);

private:
    void populateRepoBrowser();

    Ui::CodeWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
};

#endif // CODEWIDGET_H
