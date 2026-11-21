/*******************************************************************************
 * gui/CommitHistoryWidget.h                                                   *
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

#ifndef COMMITHISTORYWIDGET_H
#define COMMITHISTORYWIDGET_H

#include <QWidget>
#include <QString>
#include <QStyledItemDelegate>
#include <string>

namespace Ui {
class CommitHistoryWidget;
}

class MainWidget;
class QTreeWidgetItem;

/** Draws the GitHub-like rows: date headers and commit cards with copy/diff buttons. */
class CommitItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Roles {
        TypeRole = Qt::UserRole + 1, // 0 = date header, 1 = commit, 2 = info text
        FullHashRole,
        MessageRole,
        AuthorRole,
        TimeRole,
        ShortHashRole
    };
    enum Type { HeaderType = 0, CommitType = 1, InfoType = 2 };

    explicit CommitItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override;

    static QString relativeTime(qint64 secsSinceEpoch);

signals:
    void copyHashRequested(const QString &fullHash);
    void showDiffRequested(const QString &fullHash);

private:
    QRect cardRect(const QRect &itemRect) const;
    QRect copyButtonRect(const QRect &card, int fontHeight) const;
    QRect diffButtonRect(const QRect &card, int fontHeight) const;
};

class CommitHistoryWidget : public QWidget
{
    Q_OBJECT

public:
    CommitHistoryWidget(MainWidget *mainWidget, const QString &groupId, const QString &branchOrTag, QWidget *parent = nullptr);
    ~CommitHistoryWidget();

    QString groupId() const { return mGroupId; }
    /** Show another branch/tag (used when the tab is opened again). */
    void setBranchOrTag(const QString &branchOrTag);

public slots:
    void reload();

private slots:
    void onBranchChanged(int index);
    void onAuthorChanged(int index);
    void onNewerClicked();
    void onOlderClicked();
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onCopyHash(const QString &fullHash);
    void onShowDiff(const QString &fullHash);

private:
    std::string repoPath() const;
    void loadBranches(const QString &select);
    void loadPage(bool reloadAuthors);
    void showInfo(const QString &text);

    Ui::CommitHistoryWidget *ui;
    MainWidget *mMainWidget;
    QString mGroupId;
    QString mBranchOrTag;
    QString mAuthor;          // empty = all users
    size_t mPage = 0;
    size_t mTotal = 0;
    quint64 mRequestId = 0;   // newer requests win
};

#endif // COMMITHISTORYWIDGET_H
