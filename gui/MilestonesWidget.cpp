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
#include "MainWidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QIcon>
#include <QFile>
#include <QTextEdit>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

MilestonesWidget::MilestonesWidget(const QString &groupId, MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent), mGroupId(groupId.toStdString())
{
    setWindowTitle(tr("Milestones"));
    auto layout = new QVBoxLayout(this);
    auto toolbar = new QHBoxLayout;
    mFilter = new QComboBox(this);
    mFilter->addItems({tr("Open milestones"), tr("Closed milestones")});
    toolbar->addWidget(mFilter);
    toolbar->addStretch();
    auto refresh = new QPushButton(tr("Refresh"), this);
    toolbar->addWidget(refresh);
    mNew = new QPushButton(tr("New milestone"), this);
    toolbar->addWidget(mNew);
    layout->addLayout(toolbar);
    mList = new QTreeWidget(this);
    mList->setHeaderLabels({tr("Milestone"), tr("Due date"), tr("Progress"), tr("Open PRs"), tr("Closed / merged PRs")});
    mList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mList->setRootIsDecorated(false);
    mList->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(mList);
    mEmptyState = new QWidget(this);
    auto emptyLayout = new QVBoxLayout(mEmptyState);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto emptyIcon = new QLabel(mEmptyState);
    emptyIcon->setPixmap(QIcon(":/images/milestone.png").pixmap(40, 40));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyIcon);
    auto emptyTitle = new QLabel(tr("You haven't created any milestones."), mEmptyState);
    emptyTitle->setProperty("emptyRole", "title");
    emptyTitle->setAlignment(Qt::AlignCenter);
    QFont emptyTitleFont = emptyTitle->font();
    emptyTitleFont.setBold(true);
    emptyTitleFont.setPointSize(emptyTitleFont.pointSize() + 2);
    emptyTitle->setFont(emptyTitleFont);
    emptyLayout->addWidget(emptyTitle);
    auto emptyDescription = new QLabel(tr("Use milestones to create collections of pull requests for a release or project."), mEmptyState);
    emptyDescription->setProperty("emptyRole", "description");
    emptyDescription->setAlignment(Qt::AlignCenter);
    emptyDescription->setWordWrap(true);
    emptyLayout->addWidget(emptyDescription);
    mEmptyCreate = new QPushButton(tr("Create a milestone"), mEmptyState);
    mEmptyCreate->setProperty("buttonRole", "primary");
    emptyLayout->addWidget(mEmptyCreate, 0, Qt::AlignCenter);
    mEmptyState->setMinimumHeight(240);
    layout->addWidget(mEmptyState);
    mSummary = new QLabel(this);
    mSummary->setTextFormat(Qt::PlainText);
    layout->addWidget(mSummary);
    auto form = new QFormLayout;
    mTitle = new QLineEdit(this);
    mTitle->setMaxLength(200);
    form->addRow(tr("Title"), mTitle);
    mDescription = new QTextEdit(this);
    mDescription->setAcceptRichText(false);
    mDescription->setMaximumHeight(90);
    form->addRow(tr("Description"), mDescription);
    auto dates = new QHBoxLayout;
    mHasDueDate = new QCheckBox(tr("Due date"), this);
    mDueDate = new QDateEdit(QDate::currentDate(), this);
    mDueDate->setCalendarPopup(true);
    mDueDate->setDisplayFormat("yyyy-MM-dd");
    mClosed = new QCheckBox(tr("Closed milestone"), this);
    dates->addWidget(mHasDueDate);
    dates->addWidget(mDueDate);
    dates->addWidget(mClosed);
    form->addRow(dates);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Pull requests — check to assign; double-click to view. Each PR belongs to one milestone."), this));
    mPRFilter = new QComboBox(this);
    mPRFilter->addItems({tr("All pull requests"), tr("Open pull requests"), tr("Closed / merged pull requests")});
    layout->addWidget(mPRFilter);
    mRequests = new QTreeWidget(this);
    mRequests->setRootIsDecorated(false);
    mRequests->setHeaderLabels({tr("Pull request"), tr("Status"), tr("Current milestone")});
    mRequests->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(mRequests);
    auto actions = new QHBoxLayout;
    mDelete = new QPushButton(tr("Delete milestone"), this);
    mSave = new QPushButton(tr("Publish milestone"), this);
    actions->addWidget(mDelete);
    actions->addStretch();
    actions->addWidget(mSave);
    layout->addLayout(actions);
    connect(refresh, &QPushButton::clicked, this, [this]() { reload(); });
    connect(mFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { populate(); });
    connect(mPRFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { filterRequests(); });
    connect(mList, &QTreeWidget::itemSelectionChanged, this, [this]() { select(); });
    connect(mList, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto item = mList->itemAt(pos);
        if (!item) return;
        mList->setCurrentItem(item);
        QMenu menu(this);
        auto details = menu.addAction(tr("View details"));
        connect(details, &QAction::triggered, this, [this, item]() {
            showDetailsDialog(item->data(0, Qt::UserRole).toString());
        });
        menu.exec(mList->viewport()->mapToGlobal(pos));
    });
    connect(mNew, &QPushButton::clicked, this, [this]() { createMilestoneDialog(); });
    connect(mEmptyCreate, &QPushButton::clicked, this, [this]() { createMilestoneDialog(); });
    connect(mSave, &QPushButton::clicked, this, [this]() { save(); });
    connect(mDelete, &QPushButton::clicked, this, [this]() { remove(); });
    connect(mHasDueDate, &QCheckBox::toggled, this, [this](bool enabled) { mDueDate->setEnabled(mOwner && enabled); });
    connect(mRequests, &QTreeWidget::itemDoubleClicked, this, [mainWidget, groupId](QTreeWidgetItem *item, int) {
        if (!mainWidget) return;
        const RsGxsMessageId id(item->data(0, Qt::UserRole).toString().toStdString());
        mainWidget->showPullRequestDetails(groupId, id);
    });
    reload();

    // Details are deliberately opened from the milestone context menu so the
    // list tab stays compact and mirrors the repository milestone overview.
    mTitle->hide();
    mDescription->hide();
    mHasDueDate->hide();
    mDueDate->hide();
    mClosed->hide();
    mPRFilter->hide();
    mRequests->hide();
    mDelete->hide();
    mSave->hide();
    for (auto label : findChildren<QLabel *>()) {
        if (label->text() == tr("Title") || label->text() == tr("Description") ||
            label->text().startsWith(tr("Pull requests"))) label->hide();
    }
    QFile styleFile(":/retrogit.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));
}

QString MilestonesWidget::selectedId() const
{
    const auto items = mList->selectedItems();
    return items.isEmpty() ? QString() : items.front()->data(0, Qt::UserRole).toString();
}

void MilestonesWidget::reload()
{
    std::vector<RsGitGroup> groups;
    mPullRequests.clear();
    mOwner = false;
    mMilestones = QJsonArray();
    bool valid = rsGit && rsGit->getGroups({mGroupId}, groups) && groups.size() == 1 &&
        rsGit->getPullRequests(mGroupId, mPullRequests);
    if (valid) {
        mSnapshot = groups.front().mMilestones;
        if (!mSnapshot.empty()) {
            const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(mSnapshot));
            valid = doc.isObject() && doc.object().value("version").toInt() == 1 && doc.object().value("milestones").isArray();
            if (valid) mMilestones = doc.object().value("milestones").toArray();
        }
        mOwner = valid && IS_GROUP_ADMIN(groups.front().mMeta.mSubscribeFlags);
    }
    mNew->setVisible(mOwner);
    mNew->setProperty("buttonRole", "primary");
    mEmptyCreate->setVisible(mOwner);
    mSave->setVisible(mOwner);
    mDelete->setVisible(mOwner);
    mTitle->setReadOnly(!mOwner);
    mDescription->setReadOnly(!mOwner);
    mHasDueDate->setEnabled(mOwner);
    mClosed->setEnabled(mOwner);
    populate();
    if (!valid) mSummary->setText(tr("Milestones could not be loaded, or use an unsupported format. Refresh to retry."));
}

void MilestonesWidget::populate()
{
    mList->clear();
    int open = 0, closed = 0;
    for (const auto &value : mMilestones) {
        const auto milestone = value.toObject();
        const bool isClosed = milestone.value("closed").toBool();
        isClosed ? ++closed : ++open;
        if (isClosed != (mFilter->currentIndex() == 1)) continue;
        const auto ids = milestone.value("pullRequests").toArray();
        int pending = 0, done = 0;
        for (const auto &pr : mPullRequests) {
            if (!ids.contains(QString::fromStdString(pr.mMeta.mMsgId.toStdString()))) continue;
            pr.mStatus == 0 ? ++pending : ++done;
        }
        const int total = pending + done;
        auto item = new QTreeWidgetItem(mList, {milestone.value("title").toString(),
            milestone.value("dueDate").toString().isEmpty() ? tr("No due date") : milestone.value("dueDate").toString(),
            tr("%1% complete").arg(total ? 100 * done / total : 0), QString::number(pending), QString::number(done)});
        item->setData(0, Qt::UserRole, milestone.value("id").toString());
        auto progress = new QProgressBar(mList);
        progress->setRange(0, 100);
        progress->setValue(total ? 100 * done / total : 0);
        mList->setItemWidget(item, 2, progress);
    }
    const bool hasVisibleMilestones = mList->topLevelItemCount() > 0;
    mList->setVisible(hasVisibleMilestones);
    mEmptyState->setVisible(!hasVisibleMilestones);
    mSummary->setVisible(hasVisibleMilestones);
    mSummary->setText(tr("%1 open, %2 closed milestones. Progress counts closed and merged pull requests.").arg(open).arg(closed));
    if (mList->topLevelItemCount()) mList->setCurrentItem(mList->topLevelItem(0));
    else {
        select();
        mSummary->setText(tr("No %1 milestones. %2").arg(mFilter->currentIndex() ? tr("closed") : tr("open"),
            mOwner ? tr("Click New milestone to create one.") : tr("The repository owner can publish milestones.")));
    }
}

void MilestonesWidget::select()
{
    const QString id = selectedId();
    QJsonObject selected;
    for (const auto &value : mMilestones) if (value.toObject().value("id").toString() == id) selected = value.toObject();
    mTitle->setText(selected.value("title").toString());
    mDescription->setPlainText(selected.value("description").toString());
    const auto date = QDate::fromString(selected.value("dueDate").toString(), Qt::ISODate);
    mHasDueDate->setChecked(date.isValid());
    mDueDate->setDate(date.isValid() ? date : QDate::currentDate());
    mDueDate->setEnabled(mOwner && date.isValid());
    mClosed->setChecked(selected.value("closed").toBool());
    mDelete->setEnabled(mOwner && !id.isEmpty());
    mRequests->clear();
    const auto assigned = selected.value("pullRequests").toArray();
    for (const auto &pr : mPullRequests) {
        const QString prId = QString::fromStdString(pr.mMeta.mMsgId.toStdString());
        if (!mOwner && !assigned.contains(prId)) continue;
        QString current;
        for (const auto &value : mMilestones) {
            const auto milestone = value.toObject();
            if (milestone.value("pullRequests").toArray().contains(prId)) current = milestone.value("title").toString();
        }
        auto item = new QTreeWidgetItem(mRequests, {QString::fromStdString(pr.mTitle),
            pr.mStatus == 0 ? tr("Open") : (pr.mStatus == 1 ? tr("Closed") : tr("Merged")), current});
        item->setData(0, Qt::UserRole, prId);
        item->setData(1, Qt::UserRole, pr.mStatus != 0);
        if (mOwner) item->setCheckState(0, assigned.contains(prId) ? Qt::Checked : Qt::Unchecked);
    }
    filterRequests();
}

void MilestonesWidget::showDetailsDialog(const QString &id)
{
    if (id.isEmpty()) return;
    QJsonObject selected;
    for (const auto &value : mMilestones)
        if (value.toObject().value("id").toString() == id) selected = value.toObject();
    if (selected.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Milestone details"));
    dialog.setMinimumWidth(600);
    auto layout = new QVBoxLayout(&dialog);
    auto form = new QFormLayout;
    auto title = new QLineEdit(selected.value("title").toString(), &dialog);
    title->setReadOnly(!mOwner);
    form->addRow(tr("Title"), title);
    auto description = new QTextEdit(&dialog);
    description->setPlainText(selected.value("description").toString());
    description->setReadOnly(!mOwner);
    description->setMaximumHeight(100);
    form->addRow(tr("Description"), description);
    auto dateRow = new QHBoxLayout;
    auto hasDate = new QCheckBox(tr("Due date"), &dialog);
    auto dueDate = new QDateEdit(QDate::currentDate(), &dialog);
    dueDate->setCalendarPopup(true);
    dueDate->setDisplayFormat("yyyy-MM-dd");
    const auto parsedDate = QDate::fromString(selected.value("dueDate").toString(), Qt::ISODate);
    hasDate->setChecked(parsedDate.isValid());
    dueDate->setDate(parsedDate.isValid() ? parsedDate : QDate::currentDate());
    dueDate->setEnabled(mOwner && hasDate->isChecked());
    hasDate->setEnabled(mOwner);
    dateRow->addWidget(hasDate);
    dateRow->addWidget(dueDate);
    auto closed = new QCheckBox(tr("Closed milestone"), &dialog);
    closed->setChecked(selected.value("closed").toBool());
    closed->setEnabled(mOwner);
    dateRow->addWidget(closed);
    dateRow->addStretch();
    form->addRow(dateRow);
    layout->addLayout(form);
    connect(hasDate, &QCheckBox::toggled, dueDate, &QDateEdit::setEnabled);

    layout->addWidget(new QLabel(tr("Pull requests"), &dialog));
    auto requests = new QTreeWidget(&dialog);
    requests->setRootIsDecorated(false);
    requests->setHeaderLabels({tr("Pull request"), tr("Status")});
    requests->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    const auto assigned = selected.value("pullRequests").toArray();
    for (const auto &pr : mPullRequests) {
        const auto prId = QString::fromStdString(pr.mMeta.mMsgId.toStdString());
        auto item = new QTreeWidgetItem(requests, {QString::fromStdString(pr.mTitle), pr.mStatus == 0 ? tr("Open") : (pr.mStatus == 1 ? tr("Closed") : tr("Merged"))});
        item->setData(0, Qt::UserRole, prId);
        item->setCheckState(0, assigned.contains(prId) ? Qt::Checked : Qt::Unchecked);
        if (!mOwner) item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    }
    layout->addWidget(requests);
    auto buttons = new QDialogButtonBox(mOwner ? (QDialogButtonBox::Save | QDialogButtonBox::Cancel) : QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || !mOwner) return;

    QJsonArray newAssignments;
    for (int i = 0; i < requests->topLevelItemCount(); ++i)
        if (requests->topLevelItem(i)->checkState(0) == Qt::Checked)
            newAssignments.append(requests->topLevelItem(i)->data(0, Qt::UserRole).toString());
    QJsonArray updated;
    for (const auto &value : mMilestones) {
        auto milestone = value.toObject();
        const auto milestoneId = milestone.value("id").toString();
        if (milestoneId == id) {
            milestone["title"] = title->text().trimmed();
            milestone["description"] = description->toPlainText();
            milestone["dueDate"] = hasDate->isChecked() ? dueDate->date().toString(Qt::ISODate) : QString();
            milestone["closed"] = closed->isChecked();
            milestone["pullRequests"] = newAssignments;
        } else {
            QJsonArray remaining;
            for (const auto &pr : milestone.value("pullRequests").toArray()) if (!newAssignments.contains(pr)) remaining.append(pr);
            milestone["pullRequests"] = remaining;
        }
        updated.append(milestone);
    }
    publish(updated);
}

void MilestonesWidget::filterRequests()
{
    for (int i = 0; i < mRequests->topLevelItemCount(); ++i) {
        auto item = mRequests->topLevelItem(i);
        const bool closed = item->data(1, Qt::UserRole).toBool();
        item->setHidden((mPRFilter->currentIndex() == 1 && closed) ||
                        (mPRFilter->currentIndex() == 2 && !closed));
    }
}

void MilestonesWidget::createMilestoneDialog()
{
    if (!mOwner) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("New milestone"));
    dialog.setMinimumWidth(560);
    auto layout = new QVBoxLayout(&dialog);
    auto form = new QFormLayout;
    auto title = new QLineEdit(&dialog);
    title->setMaxLength(200);
    form->addRow(tr("Title"), title);
    auto description = new QTextEdit(&dialog);
    description->setAcceptRichText(false);
    description->setMaximumHeight(100);
    form->addRow(tr("Description"), description);
    auto dateRow = new QHBoxLayout;
    auto hasDueDate = new QCheckBox(tr("Due date"), &dialog);
    auto dueDate = new QDateEdit(QDate::currentDate(), &dialog);
    dueDate->setCalendarPopup(true);
    dueDate->setDisplayFormat("yyyy-MM-dd");
    dueDate->setEnabled(false);
    dateRow->addWidget(hasDueDate);
    dateRow->addWidget(dueDate);
    dateRow->addStretch();
    form->addRow(dateRow);
    layout->addLayout(form);
    connect(hasDueDate, &QCheckBox::toggled, dueDate, &QDateEdit::setEnabled);

    layout->addWidget(new QLabel(tr("Assign pull requests (optional)"), &dialog));
    auto requests = new QTreeWidget(&dialog);
    requests->setRootIsDecorated(false);
    requests->setHeaderLabels({tr("Pull request"), tr("Status")});
    requests->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (const auto &pr : mPullRequests) {
        auto item = new QTreeWidgetItem(requests, {QString::fromStdString(pr.mTitle),
            pr.mStatus == 0 ? tr("Open") : (pr.mStatus == 1 ? tr("Closed") : tr("Merged"))});
        item->setData(0, Qt::UserRole, QString::fromStdString(pr.mMeta.mMsgId.toStdString()));
        item->setCheckState(0, Qt::Unchecked);
    }
    layout->addWidget(requests);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create milestone"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("buttonRole", "primary");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return;
    const QString milestoneTitle = title->text().trimmed();
    if (milestoneTitle.isEmpty() || description->toPlainText().size() > 16000) {
        QMessageBox::warning(this, tr("New milestone"),
            tr("Enter a title and keep the description within 16,000 characters."));
        return;
    }
    QJsonArray assigned;
    for (int i = 0; i < requests->topLevelItemCount(); ++i) {
        const auto item = requests->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) assigned.append(item->data(0, Qt::UserRole).toString());
    }
    const auto milestone = QJsonObject{
        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"title", milestoneTitle},
        {"description", description->toPlainText()},
        {"dueDate", hasDueDate->isChecked() ? dueDate->date().toString(Qt::ISODate) : QString()},
        {"closed", false},
        {"pullRequests", assigned}};
    auto updated = mMilestones;
    updated.append(milestone);
    publish(updated);
}

bool MilestonesWidget::publish(const QJsonArray &milestones)
{
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"milestones", milestones}}).toJson(QJsonDocument::Compact);
    if (data.size() > 64000) {
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

void MilestonesWidget::save()
{
    if (!mOwner) return;
    const QString title = mTitle->text().trimmed();
    if (title.isEmpty() || mDescription->toPlainText().size() > 16000) {
        QMessageBox::warning(this, tr("Milestones"), tr("Enter a title and keep the description within 16,000 characters."));
        return;
    }
    QString id = selectedId();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonArray assigned;
    // Keep assignments whose PR messages have not arrived on this peer yet.
    for (const auto &value : mMilestones) {
        const auto milestone = value.toObject();
        if (milestone.value("id").toString() != id) continue;
        for (const auto &previous : milestone.value("pullRequests").toArray()) {
            bool loaded = false;
            for (const auto &pr : mPullRequests)
                if (QString::fromStdString(pr.mMeta.mMsgId.toStdString()) == previous.toString()) loaded = true;
            if (!loaded) assigned.append(previous);
        }
    }
    for (int i = 0; i < mRequests->topLevelItemCount(); ++i) {
        const auto item = mRequests->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) assigned.append(item->data(0, Qt::UserRole).toString());
    }
    QJsonArray updated;
    for (const auto &value : mMilestones) {
        auto milestone = value.toObject();
        if (milestone.value("id").toString() == id) continue;
        QJsonArray remaining;
        for (const auto &pr : milestone.value("pullRequests").toArray()) if (!assigned.contains(pr)) remaining.append(pr);
        milestone["pullRequests"] = remaining;
        updated.append(milestone);
    }
    updated.append(QJsonObject{{"id", id}, {"title", title}, {"description", mDescription->toPlainText()},
        {"dueDate", mHasDueDate->isChecked() ? mDueDate->date().toString(Qt::ISODate) : QString()},
        {"closed", mClosed->isChecked()}, {"pullRequests", assigned}});
    publish(updated);
}

void MilestonesWidget::remove()
{
    const auto id = selectedId();
    if (!mOwner || id.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete milestone"), tr("Delete this milestone and its assignments? Pull requests will be kept.")) != QMessageBox::Yes) return;
    QJsonArray updated;
    for (const auto &value : mMilestones) if (value.toObject().value("id").toString() != id) updated.append(value);
    publish(updated);
}
