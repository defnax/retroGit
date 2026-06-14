/*******************************************************************************
 * gui/GitBranchDialog.cpp                                                     *
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

#include "GitBranchDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>

GitBranchDialog::GitBranchDialog(const QStringList& branches, const QString& currentBranch, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Create a branch"));
    resize(400, 220);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Title label
    QLabel *titleLabel = new QLabel(tr("Create a branch"), this);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #24292f;");
    mainLayout->addWidget(titleLabel);

    // Divider line
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet("color: #d0d7de;");
    mainLayout->addWidget(line);

    // Form layout
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(8);

    mBranchNameEdit = new QLineEdit(this);
    mBranchNameEdit->setPlaceholderText(tr("Enter new branch name..."));
    mBranchNameEdit->setStyleSheet("QLineEdit { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px; font-size: 13px; }"
                                  "QLineEdit:focus { border: 1px solid #0969da; }");
    
    QLabel *branchNameLabel = new QLabel(tr("New branch name"), this);
    branchNameLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(branchNameLabel, mBranchNameEdit);

    mSourceCombo = new QComboBox(this);
    mSourceCombo->setStyleSheet("QComboBox { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px 12px; font-size: 13px; background-color: #f6f8fa; }"
                                "QComboBox::drop-down { border: none; }");
    
    // Add branches with icons
    for (const QString& branch : branches) {
        mSourceCombo->addItem(QIcon(":/images/git-branch-24_.png"), branch);
    }
    
    // Select current branch as source
    int idx = mSourceCombo->findText(currentBranch);
    if (idx != -1) {
        mSourceCombo->setCurrentIndex(idx);
    }

    QLabel *sourceLabel = new QLabel(tr("Source"), this);
    sourceLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(sourceLabel, mSourceCombo);

    mainLayout->addLayout(formLayout);

    // Spacing
    mainLayout->addStretch();

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);

    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setText(tr("Create new branch"));
    okButton->setStyleSheet("QPushButton { background-color: #2da44e; color: white; border-radius: 6px; padding: 6px 12px; font-weight: bold; border: 1px solid rgba(27, 31, 36, 0.15); }"
                            "QPushButton:hover { background-color: #2c974b; }"
                            "QPushButton:pressed { background-color: #298e46; }");

    QPushButton *cancelButton = buttonBox->button(QDialogButtonBox::Cancel);
    cancelButton->setText(tr("Cancel"));
    cancelButton->setStyleSheet("QPushButton { background-color: #f6f8fa; color: #24292f; border-radius: 6px; padding: 6px 12px; border: 1px solid rgba(27, 31, 36, 0.15); }"
                                "QPushButton:hover { background-color: #f3f4f6; }"
                                "QPushButton:pressed { background-color: #ebecf0; }");

    mainLayout->addWidget(buttonBox);

    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));
}

QString GitBranchDialog::getBranchName() const
{
    return mBranchNameEdit->text().trimmed();
}

QString GitBranchDialog::getSourceBranch() const
{
    return mSourceCombo->currentText();
}
