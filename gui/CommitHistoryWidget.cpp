/*******************************************************************************
 * gui/CommitHistoryWidget.cpp                                                 *
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

#include "CommitHistoryWidget.h"
#include "ui_CommitHistoryWidget.h"
#include "MainWidget.h"
#include "services/GitManager.h"

#include "util/qtthreadsutils.h"
#include <util/rsthreads.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QHelpEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <set>
#include <vector>

namespace {
const size_t COMMITS_PER_PAGE = 35; // like GitHub
const int TIMELINE_WIDTH = 28;      // left space for the timeline
}

/* ------------------------------------------------------------------------- */
/*                              CommitItemDelegate                           */
/* ------------------------------------------------------------------------- */

CommitItemDelegate::CommitItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString CommitItemDelegate::relativeTime(qint64 secs)
{
    if (secs <= 0) return QString();
    const QDateTime when = QDateTime::fromSecsSinceEpoch(secs);
    const qint64 diff = when.secsTo(QDateTime::currentDateTime());
    if (diff < 60) return tr("just now");
    if (diff < 3600) { const int n = int(diff / 60); return n == 1 ? tr("1 minute ago") : tr("%1 minutes ago").arg(n); }
    if (diff < 86400) { const int n = int(diff / 3600); return n == 1 ? tr("1 hour ago") : tr("%1 hours ago").arg(n); }
    if (diff < 30 * 86400) { const int n = int(diff / 86400); return n == 1 ? tr("yesterday") : tr("%1 days ago").arg(n); }
    return tr("on %1").arg(QLocale().toString(when.date(), "MMM d, yyyy"));
}

QRect CommitItemDelegate::cardRect(const QRect &r) const
{
    return r.adjusted(TIMELINE_WIDTH, 0, -4, 0);
}

QRect CommitItemDelegate::diffButtonRect(const QRect &card, int fh) const
{
    const int s = int(fh * 1.6);
    return QRect(card.right() - s - fh / 2, card.center().y() - s / 2, s, s);
}

QRect CommitItemDelegate::copyButtonRect(const QRect &card, int fh) const
{
    return diffButtonRect(card, fh).translated(-int(fh * 2.0), 0);
}

QSize CommitItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const int fh = option.fontMetrics.height();
    switch (index.data(TypeRole).toInt()) {
    case HeaderType: return QSize(option.rect.width(), int(fh * 2.4));
    case CommitType: return QSize(option.rect.width(), int(fh * 3.9));
    default:         return QSize(option.rect.width(), int(fh * 2.4));
    }
}

static QColor colorForName(const QString &name)
{
    // Stable pastel color per author for the initial "avatar"
    return QColor::fromHsv(int(qHash(name) % 360), 90, 200);
}

void CommitItemDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const int type = index.data(TypeRole).toInt();
    const QPalette &pal = option.palette;
    const int fh = option.fontMetrics.height();
    const QColor muted = pal.color(QPalette::Disabled, QPalette::Text);
    const QColor border = pal.color(QPalette::Mid);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    // Timeline: vertical line through the whole list, a commit symbol on headers
    const int lineX = option.rect.left() + TIMELINE_WIDTH / 2;
    p->setPen(QPen(border, 1));
    if (type == HeaderType) {
        const int cy = option.rect.center().y();
        const int rad = fh / 3;
        p->drawLine(lineX, cy + rad, lineX, option.rect.bottom());
        if (index.row() > 0) p->drawLine(lineX, option.rect.top(), lineX, cy - rad);
        p->drawEllipse(QPoint(lineX, cy), rad, rad);
        p->drawLine(lineX - rad * 2, cy, lineX - rad, cy);
        p->drawLine(lineX + rad, cy, lineX + rad * 2, cy);

        p->setPen(muted);
        p->drawText(option.rect.adjusted(TIMELINE_WIDTH + 4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    index.data(Qt::DisplayRole).toString());
        p->restore();
        return;
    }
    if (type == InfoType) {
        p->setPen(muted);
        p->drawText(option.rect, Qt::AlignCenter, index.data(Qt::DisplayRole).toString());
        p->restore();
        return;
    }

    p->drawLine(lineX, option.rect.top(), lineX, option.rect.bottom());

    // Card: shared borders between rows, rounded group via the outer edges
    const QRect card = cardRect(option.rect);
    const bool last = index.row() == index.model()->rowCount(index.parent()) - 1;
    QColor bg = pal.color(QPalette::Base);
    if (option.state & QStyle::State_Selected)
        bg = pal.color(QPalette::Highlight).lighter(185);
    else if (option.state & QStyle::State_MouseOver)
        bg = pal.color(QPalette::AlternateBase);
    p->fillRect(card, bg);
    p->setPen(QPen(border, 1));
    p->drawLine(card.topLeft(), card.bottomLeft());
    p->drawLine(card.topRight(), card.bottomRight());
    p->drawLine(card.topLeft(), card.topRight());
    if (last) p->drawLine(card.bottomLeft(), card.bottomRight());

    const QRect copyRect = copyButtonRect(card, fh);
    const QRect diffRect = diffButtonRect(card, fh);

    // Short hash, right aligned before the buttons
    QFont mono("DejaVu Sans Mono");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(option.font.pointSizeF() > 0 ? option.font.pointSizeF() : 9);
    const QString shortHash = index.data(ShortHashRole).toString();
    const int hashWidth = QFontMetrics(mono).horizontalAdvance(shortHash);
    const QRect hashRect(copyRect.left() - fh - hashWidth, card.top(), hashWidth, card.height());
    p->setFont(mono);
    p->setPen(pal.color(QPalette::Text));
    p->drawText(hashRect, Qt::AlignVCenter | Qt::AlignRight, shortHash);

    // Buttons (hover feedback when the mouse is over them)
    // option.rect is in viewport coordinates
    const QAbstractItemView *view = qobject_cast<const QAbstractItemView *>(option.widget);
    const QPoint mouse = view ? view->viewport()->mapFromGlobal(QCursor::pos()) : QPoint(-1, -1);
    for (const QRect &b : {copyRect, diffRect}) {
        if ((option.state & QStyle::State_MouseOver) && b.contains(mouse)) {
            p->setPen(Qt::NoPen);
            p->setBrush(pal.color(QPalette::Button));
            p->drawRoundedRect(b, 4, 4);
        }
    }
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(pal.color(QPalette::Text), 1.3));
    {   // copy icon: two overlapping rounded squares
        const int s = copyRect.width() * 2 / 5;
        const QPoint c = copyRect.center();
        p->drawRoundedRect(QRect(c.x() - s / 2 - 2, c.y() - s / 2 + 2, s, s), 2, 2);
        p->fillRect(QRect(c.x() - s / 2 + 2, c.y() - s / 2 - 2, s, s), bg);
        p->drawRoundedRect(QRect(c.x() - s / 2 + 2, c.y() - s / 2 - 2, s, s), 2, 2);
    }
    {   // diff icon: "<>"
        const int s = diffRect.width() / 5;
        const QPoint c = diffRect.center();
        p->drawLine(c.x() - s / 2, c.y() - s, c.x() - s * 3 / 2, c.y());
        p->drawLine(c.x() - s * 3 / 2, c.y(), c.x() - s / 2, c.y() + s);
        p->drawLine(c.x() + s / 2, c.y() - s, c.x() + s * 3 / 2, c.y());
        p->drawLine(c.x() + s * 3 / 2, c.y(), c.x() + s / 2, c.y() + s);
    }

    // Text area
    const int textLeft = card.left() + fh;
    const int textRight = hashRect.left() - fh;
    const int textWidth = qMax(0, textRight - textLeft);

    QFont bold = option.font;
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() > 0 ? bold.pointSizeF() + 1 : bold.pointSizeF());
    const QFontMetrics bfm(bold);
    const QString message = bfm.elidedText(index.data(MessageRole).toString(), Qt::ElideRight, textWidth);
    const int line1 = card.top() + int(fh * 0.55);
    p->setFont(bold);
    p->setPen(pal.color(QPalette::Text));
    p->drawText(QRect(textLeft, line1, textWidth, bfm.height()), Qt::AlignLeft | Qt::AlignVCenter, message);

    // Author "avatar" (initial in a colored circle) + "author committed X ago"
    const QString author = index.data(AuthorRole).toString();
    const int line2 = line1 + bfm.height() + fh / 4;
    const int av = fh;
    const QRect avatarRect(textLeft, line2 + (fh - av) / 2, av, av);
    p->setPen(Qt::NoPen);
    p->setBrush(colorForName(author));
    p->drawEllipse(avatarRect);
    QFont small = option.font;
    small.setBold(true);
    small.setPointSizeF(small.pointSizeF() > 0 ? small.pointSizeF() * 0.75 : small.pointSizeF());
    p->setFont(small);
    p->setPen(Qt::white);
    p->drawText(avatarRect, Qt::AlignCenter, author.isEmpty() ? QString("?") : author.left(1).toUpper());

    p->setFont(option.font);
    p->setPen(muted);
    const QString info = tr("%1 committed %2").arg(author, relativeTime(index.data(TimeRole).toLongLong()));
    const int infoLeft = avatarRect.right() + fh / 2;
    p->drawText(QRect(infoLeft, line2, qMax(0, textRight - infoLeft), fh), Qt::AlignLeft | Qt::AlignVCenter,
                option.fontMetrics.elidedText(info, Qt::ElideRight, qMax(0, textRight - infoLeft)));

    p->restore();
}

bool CommitItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (index.data(TypeRole).toInt() == CommitType && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const QRect card = cardRect(option.rect);
            const int fh = option.fontMetrics.height();
            const QString hash = index.data(FullHashRole).toString();
            if (copyButtonRect(card, fh).contains(me->pos())) {
                emit copyHashRequested(hash);
                return true;
            }
            if (diffButtonRect(card, fh).contains(me->pos())) {
                emit showDiffRequested(hash);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool CommitItemDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event && index.data(TypeRole).toInt() == CommitType) {
        const QRect card = cardRect(option.rect);
        const int fh = option.fontMetrics.height();
        if (copyButtonRect(card, fh).contains(event->pos())) {
            QToolTip::showText(event->globalPos(), tr("Copy full SHA"), view);
            return true;
        }
        if (diffButtonRect(card, fh).contains(event->pos())) {
            QToolTip::showText(event->globalPos(), tr("View changes of this commit"), view);
            return true;
        }
        QToolTip::showText(event->globalPos(),
                           QString("<b>%1</b><br/>%2<br/>%3")
                               .arg(index.data(MessageRole).toString().toHtmlEscaped(),
                                    index.data(Qt::ToolTipRole).toString().toHtmlEscaped(),
                                    index.data(FullHashRole).toString()),
                           view);
        return true;
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

/* ------------------------------------------------------------------------- */
/*                             CommitHistoryWidget                           */
/* ------------------------------------------------------------------------- */

CommitHistoryWidget::CommitHistoryWidget(MainWidget *mainWidget, const QString &groupId,
                                         const QString &branchOrTag, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CommitHistoryWidget)
    , mMainWidget(mainWidget)
    , mGroupId(groupId)
    , mBranchOrTag(branchOrTag)
{
    ui->setupUi(this);

    CommitItemDelegate *delegate = new CommitItemDelegate(ui->mCommitTree);
    ui->mCommitTree->setItemDelegate(delegate);
    ui->mCommitTree->setMouseTracking(true);
    ui->mCommitTree->viewport()->setAttribute(Qt::WA_Hover, true);

    connect(delegate, &CommitItemDelegate::copyHashRequested, this, &CommitHistoryWidget::onCopyHash);
    connect(delegate, &CommitItemDelegate::showDiffRequested, this, &CommitHistoryWidget::onShowDiff);
    connect(ui->mCommitTree, &QTreeWidget::itemClicked, this, &CommitHistoryWidget::onItemClicked);
    connect(ui->mCommitTree, &QTreeWidget::itemDoubleClicked, this, &CommitHistoryWidget::onItemDoubleClicked);
    connect(ui->mBtnRefresh, &QToolButton::clicked, this, &CommitHistoryWidget::reload);
    connect(ui->mBtnNewer, &QPushButton::clicked, this, &CommitHistoryWidget::onNewerClicked);
    connect(ui->mBtnOlder, &QPushButton::clicked, this, &CommitHistoryWidget::onOlderClicked);

    ui->mAuthorCombo->addItem(tr("All users"), QString());
    connect(ui->mAuthorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CommitHistoryWidget::onAuthorChanged);

    loadBranches(mBranchOrTag);
    connect(ui->mBranchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CommitHistoryWidget::onBranchChanged);

    loadPage(true);
}

CommitHistoryWidget::~CommitHistoryWidget()
{
    delete ui;
}

std::string CommitHistoryWidget::repoPath() const
{
    std::string path = GitManager::getBareRepoPath(mGroupId.toStdString());
    const QString workingPath = mMainWidget ? mMainWidget->getLocalPath() : QString();
    if (!workingPath.isEmpty() && GitManager::isValidRepository(workingPath.toStdString()))
        path = workingPath.toStdString();
    return path;
}

void CommitHistoryWidget::loadBranches(const QString &select)
{
    ui->mBranchCombo->blockSignals(true);
    ui->mBranchCombo->clear();

    const std::string path = repoPath();
    std::vector<std::string> branches, tags;
    std::string currentBranch;
    GitManager::getBranches(path, branches, currentBranch);
    GitManager::getTags(path, tags);

    const QIcon branchIcon(":/images/git-branch-2.png");
    const QIcon tagIcon(":/images/tag.png");
    for (const std::string &b : branches)
        ui->mBranchCombo->addItem(branchIcon, QString::fromStdString(b));
    for (const std::string &t : tags)
        ui->mBranchCombo->addItem(tagIcon, QString::fromStdString(t));

    int index = ui->mBranchCombo->findText(select);
    if (index < 0) index = ui->mBranchCombo->findText(QString::fromStdString(currentBranch));
    if (index < 0 && ui->mBranchCombo->count() > 0) index = 0;
    ui->mBranchCombo->setCurrentIndex(index);
    mBranchOrTag = ui->mBranchCombo->currentText();
    ui->mBranchCombo->blockSignals(false);
}

void CommitHistoryWidget::setBranchOrTag(const QString &branchOrTag)
{
    const int index = ui->mBranchCombo->findText(branchOrTag);
    if (index >= 0 && index != ui->mBranchCombo->currentIndex())
        ui->mBranchCombo->setCurrentIndex(index); // triggers onBranchChanged
}

void CommitHistoryWidget::reload()
{
    loadBranches(mBranchOrTag);
    loadPage(true);
}

void CommitHistoryWidget::onBranchChanged(int)
{
    mBranchOrTag = ui->mBranchCombo->currentText();
    mPage = 0;
    // A new branch has other authors: back to "All users"
    ui->mAuthorCombo->blockSignals(true);
    ui->mAuthorCombo->setCurrentIndex(0);
    ui->mAuthorCombo->blockSignals(false);
    mAuthor.clear();
    loadPage(true);
}

void CommitHistoryWidget::onAuthorChanged(int index)
{
    mAuthor = ui->mAuthorCombo->itemData(index).toString();
    mPage = 0;
    loadPage(false);
}

void CommitHistoryWidget::onNewerClicked()
{
    if (mPage == 0) return;
    --mPage;
    loadPage(false);
}

void CommitHistoryWidget::onOlderClicked()
{
    if ((mPage + 1) * COMMITS_PER_PAGE >= mTotal) return;
    ++mPage;
    loadPage(false);
}

void CommitHistoryWidget::showInfo(const QString &text)
{
    ui->mCommitTree->clear();
    QTreeWidgetItem *item = new QTreeWidgetItem(ui->mCommitTree);
    item->setText(0, text);
    item->setData(0, CommitItemDelegate::TypeRole, CommitItemDelegate::InfoType);
    item->setFlags(Qt::ItemIsEnabled);
}

void CommitHistoryWidget::loadPage(bool reloadAuthors)
{
    const quint64 requestId = ++mRequestId;
    const std::string path = repoPath();
    const std::string branch = mBranchOrTag.toStdString();
    const std::string author = mAuthor.toStdString();
    const size_t skip = mPage * COMMITS_PER_PAGE;

    ui->mBtnNewer->setEnabled(false);
    ui->mBtnOlder->setEnabled(false);
    ui->mBtnRefresh->setEnabled(false);
    if (ui->mCommitTree->topLevelItemCount() == 0)
        showInfo(tr("Loading commits..."));

    // Walking a large history is slow: do it in the background.
    QPointer<CommitHistoryWidget> self(this);
    RsThread::async([self, requestId, path, branch, author, skip, reloadAuthors]() {
        std::vector<GitCommitInfo> commits;
        size_t total = 0;
        std::set<std::string> authors;
        const bool ok = GitManager::getCommitHistory(path, commits, branch, skip, COMMITS_PER_PAGE,
                                                     author, &total, reloadAuthors ? &authors : nullptr);
        RsQThreadUtils::postToObject([self, requestId, ok, commits, total, authors, reloadAuthors]() {
            if (!self || requestId != self->mRequestId) return; // a newer request is running
            self->ui->mBtnRefresh->setEnabled(true);

            if (reloadAuthors) {
                QComboBox *combo = self->ui->mAuthorCombo;
                combo->blockSignals(true);
                const QString keep = self->mAuthor;
                while (combo->count() > 1) combo->removeItem(1);
                for (const std::string &a : authors)
                    combo->addItem(QString::fromStdString(a), QString::fromStdString(a));
                const int idx = combo->findData(keep);
                combo->setCurrentIndex(idx < 0 ? 0 : idx);
                combo->blockSignals(false);
            }

            self->mTotal = total;
            if (!ok) {
                self->showInfo(tr("Could not read the commit history."));
                self->ui->mPageLabel->clear();
                return;
            }
            if (commits.empty()) {
                self->showInfo(tr("No commits found."));
                self->ui->mPageLabel->clear();
                return;
            }
            self->ui->mCommitTree->clear();

            // Group by day, like "Commits on Nov 21, 2026"
            QTreeWidgetItem *header = nullptr;
            QDate headerDate;
            for (const GitCommitInfo &c : commits) {
                const QDateTime when = QDateTime::fromSecsSinceEpoch(c.time);
                if (!header || when.date() != headerDate) {
                    headerDate = when.date();
                    header = new QTreeWidgetItem(self->ui->mCommitTree);
                    header->setText(0, tr("Commits on %1").arg(QLocale().toString(headerDate, "MMM d, yyyy")));
                    header->setData(0, CommitItemDelegate::TypeRole, CommitItemDelegate::HeaderType);
                    header->setFlags(Qt::ItemIsEnabled);
                }
                QTreeWidgetItem *item = new QTreeWidgetItem(header);
                item->setData(0, CommitItemDelegate::TypeRole, CommitItemDelegate::CommitType);
                item->setData(0, CommitItemDelegate::FullHashRole, QString::fromStdString(c.full_hash));
                item->setData(0, CommitItemDelegate::ShortHashRole, QString::fromStdString(c.full_hash.substr(0, 7)));
                item->setData(0, CommitItemDelegate::MessageRole, QString::fromStdString(c.message));
                item->setData(0, CommitItemDelegate::AuthorRole, QString::fromStdString(c.author));
                item->setData(0, CommitItemDelegate::TimeRole, static_cast<qlonglong>(c.time));
                QString who = QString::fromStdString(c.author);
                if (!c.email.empty()) who += " <" + QString::fromStdString(c.email) + ">";
                item->setData(0, Qt::ToolTipRole, who + "  " + QString::fromStdString(c.date));
            }
            self->ui->mCommitTree->expandAll();
            self->ui->mCommitTree->scrollToTop();

            const size_t first = self->mPage * COMMITS_PER_PAGE + 1;
            const size_t last = first + commits.size() - 1;
            self->ui->mPageLabel->setText(tr("%1 - %2 of %3 commits")
                                              .arg(QLocale().toString(qulonglong(first)),
                                                   QLocale().toString(qulonglong(last)),
                                                   QLocale().toString(qulonglong(total))));
            self->ui->mBtnNewer->setEnabled(self->mPage > 0);
            self->ui->mBtnOlder->setEnabled(last < total);
        }, qApp);
    });
}

void CommitHistoryWidget::onItemClicked(QTreeWidgetItem *item, int)
{
    if (!item || !mMainWidget) return;
    if (item->data(0, CommitItemDelegate::TypeRole).toInt() != CommitItemDelegate::CommitType) return;
    mMainWidget->showCommitDetails(item->data(0, CommitItemDelegate::FullHashRole).toString(),
                                   QString::fromStdString(repoPath()));
}

void CommitHistoryWidget::onItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;
    if (item->data(0, CommitItemDelegate::TypeRole).toInt() != CommitItemDelegate::CommitType) return;
    onShowDiff(item->data(0, CommitItemDelegate::FullHashRole).toString());
}

void CommitHistoryWidget::onCopyHash(const QString &fullHash)
{
    QApplication::clipboard()->setText(fullHash);
    QToolTip::showText(QCursor::pos(), tr("Copied!"), this);
}

void CommitHistoryWidget::onShowDiff(const QString &fullHash)
{
    if (mMainWidget) mMainWidget->showDiffForCommit(fullHash);
}
