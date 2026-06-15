/*******************************************************************************
 * gui/GitCommitDialog.cpp                                                     *
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

#include "GitCommitDialog.h"
#include "gui/gxs/GxsIdChooser.h"
#include <retroshare/rsidentity.h>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>

extern RsIdentity *rsIdentity;

GitCommitDialog::GitCommitDialog(const QString& defaultAuthor, const QString& defaultEmail,
                                 const RsGxsId& defaultOwnId,
                                 const QStringList& branches, const QString& currentBranch,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Commit Local Changes"));
    resize(450, 350);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Form layout for git info
    QFormLayout *formLayout = new QFormLayout();
    
    mIdChooser = new GxsIdChooser(this);
    mIdChooser->loadIds(IDCHOOSER_ID_REQUIRED | IDCHOOSER_NON_ANONYMOUS, defaultOwnId);
    formLayout->addRow(tr("Identity:"), mIdChooser);
    
    mAuthorEdit = new QLineEdit(defaultAuthor, this);
    mAuthorEdit->setReadOnly(true);
    formLayout->addRow(tr("Author Name:"), mAuthorEdit);

    mEmailEdit = new QLineEdit(defaultEmail, this);
    mEmailEdit->setReadOnly(true);
    formLayout->addRow(tr("Author Email:"), mEmailEdit);

    mDateLabel = new QLabel(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"), this);
    formLayout->addRow(tr("Commit Date:"), mDateLabel);

    // Branch selection
    mBranchCombo = new QComboBox(this);
    mBranchCombo->addItems(branches);
    int currIdx = branches.indexOf(currentBranch);
    if (currIdx >= 0) {
        mBranchCombo->setCurrentIndex(currIdx);
    }
    formLayout->addRow(tr("Target Branch:"), mBranchCombo);

    mainLayout->addLayout(formLayout);

    // Commit message
    mainLayout->addWidget(new QLabel(tr("Commit Message:"), this));
    mMsgEdit = new QTextEdit(this);
    mMsgEdit->setPlaceholderText(tr("Enter your commit message here..."));
    mainLayout->addWidget(mMsgEdit);

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal, this);
    
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Commit"));
    
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));

    connect(mIdChooser, SIGNAL(idsLoaded()), this, SLOT(onIdentityChanged()));
    connect(mIdChooser, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitCommitDialog::onIdentityChanged);
}

void GitCommitDialog::onIdentityChanged(int index)
{
    Q_UNUSED(index);
    RsGxsId chosenId;
    if (mIdChooser) {
        GxsIdChooser::ChosenId_Ret cid = mIdChooser->getChosenId(chosenId);
        if (cid == GxsIdChooser::KnowId || cid == GxsIdChooser::UnKnowId) {
            RsIdentityDetails details;
            if (rsIdentity && rsIdentity->getIdDetails(chosenId, details)) {
                QString nickname = QString::fromStdString(details.mNickname);
                QString gxsIdStr = QString::fromStdString(chosenId.toStdString());
                mAuthorEdit->setText(nickname);
                mEmailEdit->setText(QString("%1@%2").arg(nickname).arg(gxsIdStr));
            }
        }
    }
}

QString GitCommitDialog::getTargetBranch() const
{
    if (mBranchCombo) {
        return mBranchCombo->currentText();
    }
    return "";
}

QString GitCommitDialog::getCommitMessage() const
{
    return mMsgEdit->toPlainText();
}

QString GitCommitDialog::getAuthorName() const
{
    return mAuthorEdit->text();
}

QString GitCommitDialog::getAuthorEmail() const
{
    return mEmailEdit->text();
}
