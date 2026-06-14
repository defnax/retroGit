/*******************************************************************************
 * gui/GitNewPRDialog.h                                                        *
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

#ifndef GITNEWPRDIALOG_H
#define GITNEWPRDIALOG_H

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QTextEdit;
class QComboBox;

class GitNewPRDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GitNewPRDialog(const QStringList& branches, const QString& currentBranch, QWidget *parent = nullptr);

    QString getTitle() const;
    QString getDescription() const;
    QString getSourceBranch() const;
    QString getTargetBranch() const;

private:
    QLineEdit *mTitleEdit;
    QTextEdit *mDescEdit;
    QComboBox *mSourceCombo;
    QComboBox *mTargetCombo;
};

#endif // GITNEWPRDIALOG_H
