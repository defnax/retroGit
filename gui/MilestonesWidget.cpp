/*******************************************************************************
 * gui/MilestonesWidget.cpp                                                    *
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

#include "MilestonesWidget.h"
#include "ui_MilestonesWidget.h"
#include "MilestoneDialog.h"
#include "MainWidget.h"
#include <QFile>
#include <QHeaderView>
#include <QIcon>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QUuid>

static const int MaxMilestoneData = 64000; // must match p3Git::updateMilestones()

MilestonesWidget::MilestonesWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MilestonesWidget)
    , mGroupId(groupId.toStdString())
{
    Q_UNUSED(mainWidget);
    ui->setupUi(this);
    ui->mList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->mEmptyIcon->setPixmap(QIcon(":/images/milestone.png").pixmap(40, 40));

    connect(ui->mRefresh, &QPushButton::clicked, this, [this]() { reload(); });
    connect(ui->mFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { populate(); });
    connect(ui->mNew, &QPushButton::clicked, this, [this]() { createMilestone(); });
    connect(ui->mEmptyCreate, &QPushButton::clicked, this, [this]() { createMilestone(); });
    connect(ui->mList, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showContextMenu(pos); });
    connect(ui->mList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item) openMilestone(item->data(0, Qt::UserRole).toString());
    });

    QFile styleFile(":/retrogit.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    reload();
}

MilestonesWidget::~MilestonesWidget()
{
    delete ui;
}

void MilestonesWidget::reload()
{
    std::vector<RsGitGroup> groups;
    mPullRequests.clear();
    mOwner = false;
    mMilestones = QJsonArray();
    mValid = rsGit && rsGit->getGroups({mGroupId}, groups) && groups.size() == 1 &&
        rsGit->getPullRequests(mGroupId, mPullRequests);
    if (mValid) {
        mSnapshot = groups.front().mMilestones;
        if (!mSnapshot.empty()) {
            const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(mSnapshot));
            mValid = doc.isObject() && doc.object().value("version").toInt() == 1 && doc.object().value("milestones").isArray();
            if (mValid) mMilestones = doc.object().value("milestones").toArray();
        }
        mOwner = mValid && IS_GROUP_ADMIN(groups.front().mMeta.mSubscribeFlags);
    }
    ui->mNew->setVisible(mOwner);
    ui->mEmptyCreate->setVisible(mOwner);
    populate();
}

void MilestonesWidget::populate()
{
    ui->mList->clear(); // also deletes the progress bars set as item widgets
    const bool showClosed = ui->mFilter->currentIndex() == 1;
    int open = 0, closed = 0;
    for (const auto &value : mMilestones) {
        const auto milestone = value.toObject();
        const bool isClosed = milestone.value("closed").toBool();
        isClosed ? ++closed : ++open;
        if (isClosed != showClosed) continue;
        const auto ids = milestone.value("pullRequests").toArray();
        int pending = 0, done = 0;
        for (const auto &pr : mPullRequests) {
            if (!ids.contains(QString::fromStdString(pr.mMeta.mMsgId.toStdString()))) continue;
            pr.mStatus == 0 ? ++pending : ++done;
        }
        const int total = pending + done;
        const int percent = total ? 100 * done / total : 0;
        const QString dueDate = milestone.value("dueDate").toString();
        auto item = new QTreeWidgetItem(ui->mList, {milestone.value("title").toString(),
            dueDate.isEmpty() ? tr("No due date") : dueDate,
            tr("%1% complete").arg(percent), QString::number(pending), QString::number(done)});
        item->setData(0, Qt::UserRole, milestone.value("id").toString());
        auto progress = new QProgressBar;
        progress->setRange(0, 100);
        progress->setValue(percent);
        ui->mList->setItemWidget(item, 2, progress);
    }

    const bool hasVisibleMilestones = ui->mList->topLevelItemCount() > 0;
    ui->mStack->setCurrentWidget(hasVisibleMilestones ? ui->mListPage : ui->mEmptyPage);
    if (hasVisibleMilestones) ui->mList->setCurrentItem(ui->mList->topLevelItem(0));

    if (mMilestones.isEmpty()) {
        ui->mEmptyTitle->setText(mOwner ? tr("You haven't created any milestones.")
                                        : tr("This repository has no milestones yet."));
        ui->mEmptyDescription->setText(mOwner
            ? tr("Use milestones to create collections of pull requests for a release or project.")
            : tr("The repository owner can publish milestones."));
    } else {
        ui->mEmptyTitle->setText(showClosed ? tr("No closed milestones.") : tr("No open milestones."));
        ui->mEmptyDescription->setText(showClosed
            ? tr("Closed milestones are listed here.")
            : tr("All milestones of this repository are closed."));
    }

    if (!mValid) {
        ui->mSummary->setText(tr("Milestones could not be loaded, or use an unsupported format. Refresh to retry."));
        ui->mSummary->setVisible(true);
    } else {
        ui->mSummary->setText(tr("%1 open, %2 closed milestones. Progress counts closed and merged pull requests.").arg(open).arg(closed));
        ui->mSummary->setVisible(hasVisibleMilestones);
    }
}

void MilestonesWidget::showContextMenu(const QPoint &pos)
{
    const auto item = ui->mList->itemAt(pos);
    if (!item) return;
    ui->mList->setCurrentItem(item);
    // Copy the id now: the list may be rebuilt while an action runs.
    const QString id = item->data(0, Qt::UserRole).toString();

    QMenu menu(this);
    QAction *open = menu.addAction(mOwner ? tr("Edit milestone") : tr("View details"));
    QAction *remove = mOwner ? menu.addAction(tr("Delete milestone")) : nullptr;
    QAction *chosen = menu.exec(ui->mList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == open) openMilestone(id);
    else if (chosen == remove) deleteMilestone(id);
}

void MilestonesWidget::createMilestone()
{
    if (!mOwner) return;
    const QJsonObject milestone{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"title", QString()},
        {"description", QString()},
        {"dueDate", QString()},
        {"closed", false},
        {"pullRequests", QJsonArray()}};
    MilestoneDialog dialog(milestone, mMilestones, mPullRequests, true, true, this);
    if (dialog.exec() != QDialog::Accepted) return;
    publish(withMilestone(dialog.milestone()));
}

void MilestonesWidget::openMilestone(const QString &id)
{
    if (id.isEmpty()) return;
    QJsonObject selected;
    for (const auto &value : mMilestones)
        if (value.toObject().value("id").toString() == id) selected = value.toObject();
    if (selected.isEmpty()) return;

    MilestoneDialog dialog(selected, mMilestones, mPullRequests, mOwner, false, this);
    if (dialog.exec() != QDialog::Accepted || !mOwner) return;
    publish(withMilestone(dialog.milestone()));
}

void MilestonesWidget::deleteMilestone(const QString &id)
{
    if (!mOwner || id.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete milestone"),
            tr("Delete this milestone and its assignments? Pull requests will be kept.")) != QMessageBox::Yes)
        return;
    QJsonArray updated;
    for (const auto &value : mMilestones)
        if (value.toObject().value("id").toString() != id) updated.append(value);
    publish(updated);
}

QJsonArray MilestonesWidget::withMilestone(const QJsonObject &milestone) const
{
    const QString id = milestone.value("id").toString();
    const QJsonArray assigned = milestone.value("pullRequests").toArray();
    QJsonArray updated;
    bool replaced = false;
    for (const auto &value : mMilestones) {
        auto other = value.toObject();
        if (other.value("id").toString() == id) {
            updated.append(milestone);
            replaced = true;
            continue;
        }
        QJsonArray remaining;
        for (const auto &pr : other.value("pullRequests").toArray())
            if (!assigned.contains(pr)) remaining.append(pr);
        other["pullRequests"] = remaining;
        updated.append(other);
    }
    if (!replaced) updated.append(milestone);
    return updated;
}

bool MilestonesWidget::publish(const QJsonArray &milestones)
{
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"milestones", milestones}}).toJson(QJsonDocument::Compact);
    if (data.size() > MaxMilestoneData) {
        QMessageBox::warning(this, tr("Milestones"), tr("Milestone metadata exceeds the 64 KB repository limit. Shorten descriptions or remove unused milestones."));
        return false;
    }
    if (!mOwner || !rsGit || !rsGit->updateMilestones(mGroupId, mSnapshot, data.toStdString())) {
        QMessageBox::warning(this, tr("Milestones"), tr("Publication failed. The repository may have changed or you may no longer own it. Refresh and retry."));
        return false;
    }
    reload();
    return true;
}
