/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiangxiaojun <xiangxiaoju@uniontech.com>
 *
 * Maintainer: liuzheng <liuzheng@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "player_engine.h"
#include "burst_screenshots_dialog.h"
#include "dmr_settings.h"
#include "utils.h"

#include <DThemeManager>

DWIDGET_USE_NAMESPACE

namespace dmr {
/**
* @brief BurstScreenshotsDialog 构造函数
* @param strPlayItemInfo 播放项信息
*/
BurstScreenshotsDialog::BurstScreenshotsDialog(const PlayItemInfo &PlayItemInfo)
    : DAbstractDialog(nullptr)
{
    MovieInfo strMovieInfo = PlayItemInfo.mi;

    //title
    m_pTitlebar = new DTitlebar(this);
    m_pTitlebar->setFixedHeight(50);
    m_pTitlebar->layout()->setContentsMargins(0, 0, 0, 0);
    m_pTitlebar->setMenuVisible(false);
    m_pTitlebar->setIcon(QIcon::fromTheme(":/resources/icons/logo-big.svg"));
    //参考设计图
    m_pTitlebar->setFixedWidth(590);
    m_pTitlebar->setTitle(strMovieInfo.title);
    m_pTitlebar->setBackgroundTransparent(true);
    //参考设计图
    setFixedSize(600, 700);
    const int nStretch = 1;
    //title bottom
    QHBoxLayout *pTitleLayout = new QHBoxLayout;
    pTitleLayout->setContentsMargins(0, 0, 0, 0);
    pTitleLayout->setSpacing(0);
    pTitleLayout->addStretch(nStretch);
    {
        QLabel *pDurationLabel = new QLabel(tr("Duration: %1").arg(strMovieInfo.durationStr()), this);
        pTitleLayout->addWidget(pDurationLabel);
        pTitleLayout->addStretch(nStretch);
    }
    {
        QLabel *pResolutionLabel = new QLabel(tr("Resolution: %1").arg(strMovieInfo.resolution), this);
        pTitleLayout->addWidget(pResolutionLabel);
        pTitleLayout->addStretch(nStretch);
    }
    {
        QLabel *pSizeLabel = new QLabel(tr("Size: %1").arg(strMovieInfo.sizeStr()), this);
        pTitleLayout->addWidget(pSizeLabel);
        pTitleLayout->addStretch(nStretch);
    }

    // main content
    QVBoxLayout *pMainContent = new QVBoxLayout;
    pMainContent->setContentsMargins(0, 10, 0, 0);

    m_pGrid = new QGridLayout();
    //参考设计图
    m_pGrid->setHorizontalSpacing(12);
    m_pGrid->setVerticalSpacing(15);
    m_pGrid->setContentsMargins(0, 0, 0, 0);
    pMainContent->addLayout(m_pGrid);
    m_pGrid->setColumnMinimumWidth(0, 160);
    m_pGrid->setColumnMinimumWidth(1, 160);
    m_pGrid->setColumnMinimumWidth(2, 160);

    QHBoxLayout *pButtonContent = new QHBoxLayout;
    pButtonContent->setContentsMargins(0, 13, 0, 0);
    pButtonContent->addStretch(1);

    m_pSaveBtn = new QPushButton(tr("Save"));
    m_pSaveBtn->setObjectName("SaveBtn");
    connect(m_pSaveBtn, &QPushButton::clicked, this, &BurstScreenshotsDialog::savePoster);
    m_pSaveBtn->setFixedSize(70, 30);
    m_pSaveBtn->setDefault(true);
    pButtonContent->addWidget(m_pSaveBtn);
    pMainContent->addLayout(pButtonContent);
    QVBoxLayout *pMainlayout = new QVBoxLayout;
    pMainlayout->setContentsMargins(10, 0, 10, 15);
    pMainlayout->setSpacing(0);
    pMainlayout->addWidget(m_pTitlebar);
    pMainlayout->addLayout(pTitleLayout);
    pMainlayout->addLayout(pMainContent);

    setLayout(pMainlayout);
}

/**
 * @brief updateWithFrames 更新截图图像
 * @param frames 截图图像
 */
void BurstScreenshotsDialog::updateWithFrames(const QList<QPair<QImage, qint64>> &frames)
{
    qreal devicePixelRatio = qApp->devicePixelRatio();
    //参考设计图
    QSize size(static_cast<int>(178 * devicePixelRatio), static_cast<int>(100 * devicePixelRatio));

    int nCount = 0;
    QImage scaled;
    for (auto frame : frames) {
        QImage image = frame.first;
        ThumbnailFrame *pThumbnailFrame = new ThumbnailFrame(this);

        int nRowCount = nCount / 3;
        int nColumn = nCount % 3;

        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap = pixmap.scaled(size.width() - 2, size.height() - 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(devicePixelRatio);
        pixmap = utils::MakeRoundedPixmap(size, pixmap, 2, 2, frame.second);
        pThumbnailFrame->setAlignment(Qt::AlignCenter);
        pThumbnailFrame->setPixmap(pixmap);
        m_pGrid->addWidget(pThumbnailFrame, nRowCount, nColumn);
        nCount++;
    }
}

/**
 * @brief exec 返回执行函数的标识符
 * @return 执行函数的标识符
 */
int BurstScreenshotsDialog::exec()
{
    return DAbstractDialog::exec();
}

/**
 * @brief savePoster 保存截图
 */
void BurstScreenshotsDialog::savePoster()
{
    //参考设计图
    m_pTitlebar->setFixedWidth(610);
    QPixmap img = this->grab(rect().marginsRemoved(QMargins(10, 0, 10, 45)));
    m_sPosterPath = Settings::get().screenshotNameTemplate();
    img.save(m_sPosterPath);
    DAbstractDialog::accept();
}

/**
 * @brief savedPosterPath 保存截图路径
 * @return 返回设置的截图保存路径
 */
QString BurstScreenshotsDialog::savedPosterPath()
{
    return m_sPosterPath;
}

}

