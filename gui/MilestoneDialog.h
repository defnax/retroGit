/*******************************************************************************
 * gui/MilestoneDialog.h                                                       *
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

#ifndef RETROGIT_MILESTONE_DIALOG_H
#define RETROGIT_MILESTONE_DIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <vector>
#include "interface/rsGit.h"

namespace Ui {
class MilestoneDialog;
}

// Creates a milestone, or shows / edits an existing one. Read-only for peers
// that do not own the repository.
class MilestoneDialog : public QDialog
{
    Q_OBJECT

public:
    static constexpr int MaxDescriptionLength = 16000;

    MilestoneDialog(const QJsonObject &milestone, const QJsonArray &allMilestones,
                    const std::vector<RsGitPullRequest> &pullRequests,
                    bool editable, bool isNew, QWidget *parent = nullptr);
    ~MilestoneDialog() override;

    // The edited milestone. Assignments to pull requests that have not reached
    // this peer yet are carried over unchanged.
    QJsonObject milestone() const;

public slots:
    void accept() override;

private:
    void filterRequests();

    Ui::MilestoneDialog *ui;
    QJsonObject mMilestone;
    QJsonArray mUnloadedAssignments;
    bool mEditable;
};

#endif
