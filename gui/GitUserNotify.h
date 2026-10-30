/*******************************************************************************
 * gui/GitUserNotify.h                                                         *
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

#ifndef GITUSERNOTIFY_H
#define GITUSERNOTIFY_H

#include "gui/common/UserNotify.h"
#include "retroshare/rsevents.h"

class MainWidget;

class GitUserNotify : public UserNotify
{
    Q_OBJECT

public:
    explicit GitUserNotify(MainWidget *mainWidget, QObject *parent = nullptr);
    virtual ~GitUserNotify() override;

    virtual bool hasSetting(QString *name, QString *group) override;

protected:
    virtual void startUpdate() override;

private:
    virtual QIcon getIcon() override;
    virtual QIcon getMainIcon(bool hasNew) override;
    virtual unsigned int getNewCount() override;

    virtual void iconClicked() override;

    MainWidget       *mMainWidget;
    unsigned int      mNewCount;
    RsEventsHandlerId_t mEventHandlerId;
    class QTimer     *mUpdateTimer;
};

#endif // GITUSERNOTIFY_H
