/*******************************************************************************
 * gui/MilestoneDialog.cpp                                                     *
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

#include "MilestoneDialog.h"
#include "ui_MilestoneDialog.h"
#include "PullRequestNumbers.h"
#include <algorithm>
#include <QDate>
#include <QHeaderView>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>

MilestoneDialog::MilestoneDialog(const QJsonObject &milestone, const QJsonArray &allMilestones,
                                 const std::vector<RsGitPullRequest> &pullRequests,
                                 bool editable, bool isNew, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MilestoneDialog)
    , mMilestone(milestone)
    , mEditable(editable)
{
    ui->setupUi(this);
    setWindowTitle(isNew ? tr("New milestone") : tr("Milestone details"));
    ui->mRequests->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    ui->mTitle->setText(milestone.value("title").toString());
    ui->mDescription->setPlainText(milestone.value("description").toString());
    const QDate dueDate = QDate::fromString(milestone.value("dueDate").toString(), Qt::ISODate);
    ui->mHasDueDate->setChecked(dueDate.isValid());
    ui->mDueDate->setDate(dueDate.isValid() ? dueDate : QDate::currentDate());
    ui->mDueDate->setEnabled(editable && dueDate.isValid());
    ui->mClosed->setChecked(milestone.value("closed").toBool());
    ui->mClosed->setVisible(!isNew);

    ui->mTitle->setReadOnly(!editable);
    ui->mDescription->setReadOnly(!editable);
    ui->mHasDueDate->setEnabled(editable);
    ui->mClosed->setEnabled(editable);
    if (editable)
        connect(ui->mHasDueDate, &QCheckBox::toggled, ui->mDueDate, &QDateEdit::setEnabled);

    // Where every other pull request currently lives: checking one here moves it.
    const QString milestoneId = milestone.value("id").toString();
    QMap<QString, QString> currentMilestone;
    for (const auto &value : allMilestones) {
        const auto other = value.toObject();
        if (other.value("id").toString() == milestoneId) continue;
        for (const auto &id : other.value("pullRequests").toArray())
            currentMilestone[id.toString()] = other.value("title").toString();
    }

    const auto assigned = milestone.value("pullRequests").toArray();
    const auto numbers = pullRequestNumbers(pullRequests);
    std::vector<const RsGitPullRequest *> ordered;
    for (const auto &pr : pullRequests) ordered.push_back(&pr);
    std::sort(ordered.begin(), ordered.end(), [&numbers](const RsGitPullRequest *a, const RsGitPullRequest *b) {
        return numbers.at(a->mMeta.mMsgId) < numbers.at(b->mMeta.mMsgId);
    });

    QSet<QString> loaded;
    for (const auto *pr : ordered) {
        const QString prId = QString::fromStdString(pr->mMeta.mMsgId.toStdString());
        if (loaded.contains(prId)) continue;
        loaded.insert(prId);
        const bool isAssigned = assigned.contains(prId);
        if (!editable && !isAssigned) continue;
        const QString status = pr->mStatus == 0 ? tr("Open") : (pr->mStatus == 1 ? tr("Closed") : tr("Merged"));
        auto item = new QTreeWidgetItem(ui->mRequests, {
            tr("#%1 %2").arg(numbers.at(pr->mMeta.mMsgId)).arg(QString::fromStdString(pr->mTitle)),
            status, isAssigned ? QString() : currentMilestone.value(prId)});
        item->setData(0, Qt::UserRole, prId);
        item->setData(1, Qt::UserRole, pr->mStatus != 0);
        if (editable) item->setCheckState(0, isAssigned ? Qt::Checked : Qt::Unchecked);
    }
    for (const auto &id : assigned)
        if (!loaded.contains(id.toString())) mUnloadedAssignments.append(id);

    if (editable) {
        if (isNew) {
            ui->mButtons->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            ui->mButtons->button(QDialogButtonBox::Ok)->setText(tr("Create milestone"));
            ui->mButtons->button(QDialogButtonBox::Ok)->setProperty("buttonRole", "primary");
        }
    } else {
        ui->mButtons->setStandardButtons(QDialogButtonBox::Close);
    }
    connect(ui->mButtons, &QDialogButtonBox::accepted, this, &MilestoneDialog::accept);
    connect(ui->mButtons, &QDialogButtonBox::rejected, this, &MilestoneDialog::reject);
    connect(ui->mRequestFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { filterRequests(); });
}

MilestoneDialog::~MilestoneDialog()
{
    delete ui;
}

void MilestoneDialog::filterRequests()
{
    const int filter = ui->mRequestFilter->currentIndex();
    for (int i = 0; i < ui->mRequests->topLevelItemCount(); ++i) {
        auto item = ui->mRequests->topLevelItem(i);
        const bool closed = item->data(1, Qt::UserRole).toBool();
        item->setHidden((filter == 1 && closed) || (filter == 2 && !closed));
    }
}

void MilestoneDialog::accept()
{
    if (mEditable) {
        if (ui->mTitle->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a title for the milestone."));
            ui->mTitle->setFocus();
            return;
        }
        if (ui->mDescription->toPlainText().size() > MaxDescriptionLength) {
            QMessageBox::warning(this, windowTitle(),
                tr("Keep the description within %1 characters.").arg(int(MaxDescriptionLength)));
            ui->mDescription->setFocus();
            return;
        }
    }
    QDialog::accept();
}

QJsonObject MilestoneDialog::milestone() const
{
    QJsonObject result = mMilestone;
    result["title"] = ui->mTitle->text().trimmed();
    result["description"] = ui->mDescription->toPlainText();
    result["dueDate"] = ui->mHasDueDate->isChecked() ? ui->mDueDate->date().toString(Qt::ISODate) : QString();
    result["closed"] = ui->mClosed->isChecked();
    QJsonArray assigned = mUnloadedAssignments;
    for (int i = 0; i < ui->mRequests->topLevelItemCount(); ++i) {
        const auto item = ui->mRequests->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked) assigned.append(item->data(0, Qt::UserRole).toString());
    }
    result["pullRequests"] = assigned;
    return result;
}
