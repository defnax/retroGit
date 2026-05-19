/*******************************************************************************
 * gui/GitCommitDialog.h                                                       *
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

#ifndef GITCOMMITDIALOG_H
#define GITCOMMITDIALOG_H

#include <QDialog>

class QLineEdit;
class QTextEdit;
class QLabel;

class GitCommitDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitCommitDialog(const QString& defaultAuthor, const QString& defaultEmail, QWidget *parent = nullptr);

    QString getCommitMessage() const;
    QString getAuthorName() const;
    QString getAuthorEmail() const;

private:
    QLineEdit *mAuthorEdit;
    QLineEdit *mEmailEdit;
    QLabel *mDateLabel;
    QTextEdit *mMsgEdit;
};

#endif // GITCOMMITDIALOG_H
