/*******************************************************************************
 * gui/GitUserNotify.cpp                                                       *
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

#include "GitUserNotify.h"
#include "MainWidget.h"
#include "gui/MainWindow.h"
#include "gui/common/FilesDefs.h"
#include "util/qtthreadsutils.h"
#include "util/rsthreads.h"
#include "interface/rsGit.h"
#include "retroshare/rsevents.h"
#include "retroshare/rsgxsifacetypes.h"

#include <QPointer>
#include <QTimer>
#include <QApplication>

GitUserNotify::GitUserNotify(MainWidget *mainWidget, QObject *parent) :
    UserNotify(parent), mMainWidget(mainWidget), mNewCount(0), mEventHandlerId(0)
{
    // Bursts of events (e.g. many messages marked read) trigger a single
    // statistics request instead of one background thread per event.
    mUpdateTimer = new QTimer(this);
    mUpdateTimer->setSingleShot(true);
    mUpdateTimer->setInterval(1000);
    connect(mUpdateTimer, &QTimer::timeout, this, [this]() { updateIcon(); });

    /* Register for Git events so the icon updates whenever new commits arrive
       or a read/unread state change is posted from the backend. */
    if (rsEvents) {
        RsEventType gitEventType = (RsEventType)rsEvents->getDynamicEventType("GIT");
        QPointer<GitUserNotify> self(this);
        rsEvents->registerEventsHandler(
            [self](std::shared_ptr<const RsEvent> event) {
                const RsGitEvent *e = dynamic_cast<const RsGitEvent *>(event.get());
                if (e && e->mGitEventCode == RsGitEventCode::CLONE_STATUS_CHANGED) return;
                // Checked on the GUI thread: the notifier may be gone by then.
                RsQThreadUtils::postToObject([self]() {
                    if (self && !self->mUpdateTimer->isActive()) self->mUpdateTimer->start();
                }, qApp);
            },
            mEventHandlerId, gitEventType);
    }
}

GitUserNotify::~GitUserNotify()
{
    if (rsEvents && mEventHandlerId != 0) {
        rsEvents->unregisterEventsHandler(mEventHandlerId);
    }
}

bool GitUserNotify::hasSetting(QString *name, QString *group)
{
    if (name)  *name  = tr("RetroGit");
    if (group) *group = "RetroGit";
    return true;
}

void GitUserNotify::startUpdate()
{
    mNewCount = 0;

    QPointer<GitUserNotify> self(this);

    RsThread::async([self]()
    {
        unsigned int newCount = 0;
        if (rsGit)
        {
            GxsServiceStatistic stats;
            if (rsGit->getGitStatistics(stats))
            {
                newCount = stats.mNumThreadMsgsUnread + stats.mNumChildMsgsUnread;
            }
        }

        RsQThreadUtils::postToObject([self, newCount]()
        {
            if (!self) return;
            self->mNewCount = newCount;
            self->update();
        }, self);
    });
}

unsigned int GitUserNotify::getNewCount()
{
    return mNewCount;
}

QIcon GitUserNotify::getIcon()
{
    return FilesDefs::getIconFromQtResourcePath(":/images/git.png");
}

QIcon GitUserNotify::getMainIcon(bool hasNew)
{
    return FilesDefs::getIconFromQtResourcePath(
        hasNew ? ":/images/git-notify.png" : ":/images/git.png");
}

void GitUserNotify::iconClicked()
{
    if (mMainWidget) {
        /* showWindow(MainPage*) raises the window and switches to our page */
        MainWindow::showWindow(mMainWidget);
    }
}
