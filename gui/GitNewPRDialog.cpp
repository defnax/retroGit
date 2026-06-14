/*******************************************************************************
 * gui/GitNewPRDialog.cpp                                                      *
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

#include "GitNewPRDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QFrame>

GitNewPRDialog::GitNewPRDialog(const QStringList& branches, const QString& currentBranch, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Open a pull request"));
    resize(480, 320);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Title label
    QLabel *titleLabel = new QLabel(tr("Open a pull request"), this);
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

    mTitleEdit = new QLineEdit(this);
    mTitleEdit->setPlaceholderText(tr("Title"));
    mTitleEdit->setStyleSheet("QLineEdit { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px; font-size: 13px; }"
                             "QLineEdit:focus { border: 1px solid #0969da; }");
    
    QLabel *titleTagLabel = new QLabel(tr("Title"), this);
    titleTagLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(titleTagLabel, mTitleEdit);

    mDescEdit = new QTextEdit(this);
    mDescEdit->setPlaceholderText(tr("Write a description for this pull request..."));
    mDescEdit->setStyleSheet("QTextEdit { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px; font-size: 13px; }"
                            "QTextEdit:focus { border: 1px solid #0969da; }");
    
    QLabel *descTagLabel = new QLabel(tr("Description"), this);
    descTagLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(descTagLabel, mDescEdit);

    // Source / Target dropdowns
    mSourceCombo = new QComboBox(this);
    mSourceCombo->setStyleSheet("QComboBox { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px 12px; font-size: 13px; background-color: #f6f8fa; }"
                                "QComboBox::drop-down { border: none; }");
    
    mTargetCombo = new QComboBox(this);
    mTargetCombo->setStyleSheet("QComboBox { border: 1px solid #d0d7de; border-radius: 6px; padding: 6px 12px; font-size: 13px; background-color: #f6f8fa; }"
                                "QComboBox::drop-down { border: none; }");

    for (const QString& branch : branches) {
        mSourceCombo->addItem(QIcon(":/images/git-branch.png"), branch);
        mTargetCombo->addItem(QIcon(":/images/git-branch.png"), branch);
    }

    // Set defaults
    int idxSrc = mSourceCombo->findText(currentBranch);
    if (idxSrc != -1) {
        mSourceCombo->setCurrentIndex(idxSrc);
    }
    
    // Choose master/main for target if available
    int idxTgt = mTargetCombo->findText("master");
    if (idxTgt == -1) idxTgt = mTargetCombo->findText("main");
    if (idxTgt != -1) {
        mTargetCombo->setCurrentIndex(idxTgt);
    }

    QLabel *sourceLabel = new QLabel(tr("Compare (Source)"), this);
    sourceLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(sourceLabel, mSourceCombo);

    QLabel *targetLabel = new QLabel(tr("Base (Target)"), this);
    targetLabel->setStyleSheet("font-weight: bold; color: #24292f;");
    formLayout->addRow(targetLabel, mTargetCombo);

    mainLayout->addLayout(formLayout);

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);

    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setText(tr("Create pull request"));
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

QString GitNewPRDialog::getTitle() const
{
    return mTitleEdit->text().trimmed();
}

QString GitNewPRDialog::getDescription() const
{
    return mDescEdit->toPlainText().trimmed();
}

QString GitNewPRDialog::getSourceBranch() const
{
    return mSourceCombo->currentText();
}

QString GitNewPRDialog::getTargetBranch() const
{
    return mTargetCombo->currentText();
}
