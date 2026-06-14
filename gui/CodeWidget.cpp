/*******************************************************************************
 * gui/CodeWidget.cpp                                                          *
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

#include "CodeWidget.h"
#include "ui_CodeWidget.h"
#include "MainWidget.h"
#include "services/GitManager.h"
#include <QDir>
#include <QPushButton>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <retroshare/rsinit.h>

CodeWidget::CodeWidget(MainWidget *mainWidget, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::CodeWidget)
    , mMainWidget(mainWidget)
{
    ui->setupUi(this);

    ui->mRepoBrowserList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->mRepoBrowserList, &QListWidget::customContextMenuRequested, this, &CodeWidget::onRepoBrowserContextMenu);
    connect(ui->mRepoBrowserList, &QListWidget::itemDoubleClicked, this, &CodeWidget::openSelectedFile);
    connect(ui->mBtnOpenFolder, &QPushButton::clicked, this, &CodeWidget::onOpenFolderClicked);

    clear();
}

CodeWidget::~CodeWidget()
{
    delete ui;
}

void CodeWidget::clear()
{
    ui->mRepoBrowserList->clear();
    ui->mBtnOpenFolder->setEnabled(false);
    mGroupId.clear();
}

void CodeWidget::setGroupId(const QString &groupId)
{
    mGroupId = groupId;
    if (mGroupId.isEmpty()) {
        clear();
        return;
    }

    refresh();
}

void CodeWidget::refresh()
{
    if (mGroupId.isEmpty()) return;

    QString rawPath = mMainWidget->getLocalPath();
    ui->mBtnOpenFolder->setEnabled(!rawPath.isEmpty() && QDir(rawPath).exists());

    populateRepoBrowser();
}

void CodeWidget::handleGitEvent(const RsGitEvent *e)
{
    if (mGroupId.isEmpty() || e->mGitGroupId != RsGxsGroupId(mGroupId.toStdString()))
        return;

    if (e->mGitEventCode == RsGitEventCode::NEW_POST || e->mGitEventCode == RsGitEventCode::READ_STATUS_CHANGED) {
        refresh();
    }
}

void CodeWidget::onOpenFolderClicked()
{
    QString localPath = mMainWidget->getLocalPath();
    if (!localPath.isEmpty() && QDir(localPath).exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    } else {
        QMessageBox::warning(this, tr("Warning"), tr("Local working directory does not exist or is not set."));
    }
}

void CodeWidget::onRepoBrowserContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = ui->mRepoBrowserList->itemAt(pos);
    if (!item) return;

    QMenu contextMnu(this);
    contextMnu.addAction(QIcon(":/images/open.png"), tr("Open File"), this, &CodeWidget::openSelectedFile);
    contextMnu.exec(ui->mRepoBrowserList->mapToGlobal(pos));
}

void CodeWidget::openSelectedFile()
{
    QListWidgetItem *item = ui->mRepoBrowserList->currentItem();
    if (!item) return;

    QString selectedFile = item->text();
    QString rawPath = mMainWidget->getLocalPath();
    QString filePath;
    bool opened = false;

    // 1. Try to open from working directory if it exists
    if (!rawPath.isEmpty()) {
        QString localPath = QDir::cleanPath(rawPath);
        if (QDir(localPath).exists()) {
            filePath = QDir(localPath).filePath(selectedFile);
            if (QFile::exists(filePath)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                opened = true;
            }
        }
    }

    // 2. If not opened, extract from bare repository to temp folder and open
    if (!opened) {
        std::string barePath = GitManager::getBareRepoPath(mGroupId.toStdString());
        QString tempDir = QDir::cleanPath(QString::fromStdString(RsAccounts::AccountDirectory()) + "/retrogit_temp/previews/" + mGroupId);
        QDir().mkpath(tempDir);

        QFileInfo fileInfo(selectedFile);
        if (!fileInfo.path().isEmpty() && fileInfo.path() != ".") {
            QDir().mkpath(tempDir + "/" + fileInfo.path());
        }

        filePath = tempDir + "/" + selectedFile;

        if (GitManager::extractFile(barePath, selectedFile.toStdString(), filePath.toStdString())) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        } else {
            QMessageBox::critical(this, tr("Error"), tr("Failed to extract and open file from repository."));
        }
    }
}

void CodeWidget::populateRepoBrowser()
{
    ui->mRepoBrowserList->clear();
    if (mGroupId.isEmpty()) return;

    std::string bareRepoPath = GitManager::getBareRepoPath(mGroupId.toStdString());
    std::vector<std::string> files;

    if (GitManager::getRepoFiles(bareRepoPath, files)) {
        for (const std::string& file : files) {
            ui->mRepoBrowserList->addItem(QString::fromStdString(file));
        }
    }
}
