// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "player_engine.h"
#include "burst_screenshots_dialog.h"
#include "dmr_settings.h"
#include "utils.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DThemeManager>
#endif

DWIDGET_USE_NAMESPACE

namespace dmr {
/**
* @brief BurstScreenshotsDialog 构造函数
* @param strPlayItemInfo 播放项信息
*/
BurstScreenshotsDialog::BurstScreenshotsDialog(const PlayItemInfo &PlayItemInfo)
    : DAbstractDialog(nullptr)
{
    qDebug() << "Initializing BurstScreenshotsDialog";
    MovieInfo strMovieInfo = PlayItemInfo.mi;
    qDebug() << "MovieInfo title:" << strMovieInfo.title;

    //title
    m_pTitlebar = new DTitlebar(this);
    m_pTitlebar->setFixedHeight(50);
    m_pTitlebar->layout()->setContentsMargins(0, 0, 0, 0);
    m_pTitlebar->setMenuVisible(false);
    m_pTitlebar->setIcon(QIcon::fromTheme("deepin-movie"));
    //参考设计图
    m_pTitlebar->setFixedWidth(590);
    m_pTitlebar->setTitle(strMovieInfo.title);
    m_pTitlebar->setBackgroundTransparent(true);
    qDebug() << "Titlebar initialized with title:" << strMovieInfo.title;
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
        qDebug() << "Duration label added:" << strMovieInfo.durationStr();
    }
    {
        QLabel *pResolutionLabel = new QLabel(tr("Resolution: %1").arg(strMovieInfo.resolution), this);
        pTitleLayout->addWidget(pResolutionLabel);
        pTitleLayout->addStretch(nStretch);
        qDebug() << "Resolution label added:" << strMovieInfo.resolution;
    }
    {
        QLabel *pSizeLabel = new QLabel(tr("Size: %1").arg(strMovieInfo.sizeStr()), this);
        pTitleLayout->addWidget(pSizeLabel);
        pTitleLayout->addStretch(nStretch);
        qDebug() << "Size label added:" << strMovieInfo.sizeStr();
    }

    // main content
    QVBoxLayout *pMainContent = new QVBoxLayout;
    pMainContent->setContentsMargins(0, 10, 0, 0);
    qDebug() << "Main content layout created.";

    m_pGrid = new QGridLayout();
    //参考设计图
    m_pGrid->setHorizontalSpacing(12);
    m_pGrid->setVerticalSpacing(15);
    m_pGrid->setContentsMargins(0, 0, 0, 0);
    pMainContent->addLayout(m_pGrid);
    m_pGrid->setColumnMinimumWidth(0, 160);
    m_pGrid->setColumnMinimumWidth(1, 160);
    m_pGrid->setColumnMinimumWidth(2, 160);
    qDebug() << "Grid layout initialized.";

    QHBoxLayout *pButtonContent = new QHBoxLayout;
    pButtonContent->setContentsMargins(0, 13, 0, 0);
    pButtonContent->addStretch(1);
    qDebug() << "Button content layout created.";

    m_pSaveBtn = new QPushButton(tr("Save"));
    m_pSaveBtn->setObjectName("SaveBtn");
    connect(m_pSaveBtn, &QPushButton::clicked, this, &BurstScreenshotsDialog::savePoster);
    m_pSaveBtn->setFixedSize(70, 30);
    m_pSaveBtn->setDefault(true);
    pButtonContent->addWidget(m_pSaveBtn);
    pMainContent->addLayout(pButtonContent);
    qDebug() << "Save button initialized and added.";
    QVBoxLayout *pMainlayout = new QVBoxLayout;
    pMainlayout->setContentsMargins(10, 0, 10, 15);
    pMainlayout->setSpacing(0);
    pMainlayout->addWidget(m_pTitlebar);
    pMainlayout->addLayout(pTitleLayout);
    pMainlayout->addLayout(pMainContent);
    qDebug() << "Main layout created and populated.";

    setLayout(pMainlayout);
    qDebug() << "Layout set.";

#ifdef DTKWIDGET_CLASS_DSizeMode
    qDebug() << "DTKWIDGET_CLASS_DSizeMode defined.";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        qDebug() << "CompactMode detected. Adjusting sizes.";
        m_pTitlebar->setFixedSize(389, 33);
        setFixedSize(396, 462);
        m_pGrid->setHorizontalSpacing(8);
        m_pGrid->setVerticalSpacing(10);
        m_pSaveBtn->setFixedSize(46, 20);
        pMainlayout->setContentsMargins(7, 0, 7, 10);
    } else {
        qDebug() << "NormalMode detected. Using default sizes.";
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        qDebug() << "Size mode changed signal received. New sizeMode:" << sizeMode;
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            qDebug() << "Switching to NormalMode. Adjusting sizes.";
            m_pTitlebar->setFixedSize(590, 50);
            setFixedSize(600, 700);
            m_pGrid->setHorizontalSpacing(12);
            m_pGrid->setVerticalSpacing(15);
            m_pSaveBtn->setFixedSize(70, 30);
            layout()->setContentsMargins(10, 0, 10, 15);
        } else {
            qDebug() << "Switching to CompactMode. Adjusting sizes.";
            m_pTitlebar->setFixedSize(389, 33);
            setFixedSize(396, 462);
            m_pGrid->setHorizontalSpacing(8);
            m_pGrid->setVerticalSpacing(10);
            m_pSaveBtn->setFixedSize(46, 20);
            layout()->setContentsMargins(7, 0, 7, 10);
        }
        this->moveToCenter();
        qDebug() << "Dialog moved to center.";
    });
#else
    qDebug() << "DTKWIDGET_CLASS_DSizeMode not defined.";
#endif
    qDebug() << "Exiting BurstScreenshotsDialog constructor.";
}

/**
 * @brief updateWithFrames 更新截图图像
 * @param frames 截图图像
 */
void BurstScreenshotsDialog::updateWithFrames(const QList<QPair<QImage, qint64>> &frames)
{
    qDebug() << "Updating frames - Total frames:" << frames.size();
    qreal devicePixelRatio = qApp->devicePixelRatio();
    qDebug() << "Device pixel ratio:" << devicePixelRatio;
    //参考设计图
    QSize size(static_cast<int>(178 * devicePixelRatio), static_cast<int>(100 * devicePixelRatio));
    qDebug() << "Target thumbnail size:" << size;

    int nCount = 0;
    QImage scaled;
    for (auto frame : frames) {
        qDebug() << "Processing frame" << nCount;
        QImage image = frame.first;
        ThumbnailFrame *pThumbnailFrame = new ThumbnailFrame(this);

        int nRowCount = nCount / 3;
        int nColumn = nCount % 3;
        qDebug() << "Processing frame" << nCount << "at position" << nRowCount << "," << nColumn;

        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap = pixmap.scaled(size.width() - 2, size.height() - 2, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        qDebug() << "Pixmap scaled to:" << pixmap.size();
#ifdef DTKWIDGET_CLASS_DSizeMode
    qDebug() << "DTKWIDGET_CLASS_DSizeMode defined for frame processing.";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        qDebug() << "CompactMode detected for frame. Adjusting thumbnail size and pixmap scale.";
        pThumbnailFrame->setFixedSize(118, 67);
        pixmap = pixmap.scaled(pixmap.width() * 0.66, pixmap.height() * 0.66, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap = utils::MakeRoundedPixmap(size*0.66, pixmap, 2, 2, frame.second);
    } else {
        qDebug() << "NormalMode detected for frame. Using default thumbnail size and pixmap scale.";
        pixmap = utils::MakeRoundedPixmap(size, pixmap, 2, 2, frame.second);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, pThumbnailFrame, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        qDebug() << "Size mode changed for ThumbnailFrame. New sizeMode:" << sizeMode;
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            qDebug() << "Switching ThumbnailFrame to NormalMode.";
            pThumbnailFrame->setFixedSize(178, 100);
            auto pixmap = pThumbnailFrame->pixmap();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            qDebug() << "Qt version < 6.0.0 for ThumbnailFrame pixmap scaling.";
            auto new_pix = pixmap->scaled(178, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#else
            auto new_pix = pixmap.scaled(178, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#endif
//            new_pix = utils::MakeRoundedPixmap(QSize(180, 102), new_pix, 2, 2, frame.second);
            pThumbnailFrame->setPixmap(new_pix);
        } else {
            qDebug() << "Switching ThumbnailFrame to CompactMode.";
            pThumbnailFrame->setFixedSize(118, 67);
            auto pixmap = pThumbnailFrame->pixmap();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            qDebug() << "Qt version < 6.0.0 for ThumbnailFrame pixmap scaling (compact).";
            auto new_pix = pixmap->scaled(118, 67, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#else
            auto new_pix = pixmap.scaled(118, 67, Qt::KeepAspectRatio, Qt::SmoothTransformation);
#endif
//            new_pix = utils::MakeRoundedPixmap(QSize(120, 69), new_pix, 2, 2, frame.second);
            pThumbnailFrame->setPixmap(new_pix);
        }
    });
#else
    qDebug() << "DTKWIDGET_CLASS_DSizeMode not defined for frame processing.";
        pixmap = utils::MakeRoundedPixmap(size, pixmap, 2, 2, frame.second);
#endif
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pThumbnailFrame->setAlignment(Qt::AlignCenter);
    pThumbnailFrame->setPixmap(pixmap);
    m_pGrid->addWidget(pThumbnailFrame, nRowCount, nColumn);
    nCount++;
    qDebug() << "Thumbnail frame added to grid.";
    }
    qDebug() << "Exiting updateWithFrames().";
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
    qDebug() << "Entering BurstScreenshotsDialog::savePoster().";
    qDebug() << "Saving poster";
    //参考设计图
    m_pTitlebar->setFixedWidth(610);
    qDebug() << "Titlebar width set to 610.";
    QPixmap img = this->grab(rect().marginsRemoved(QMargins(10, 0, 10, 45)));
    m_sPosterPath = Settings::get().screenshotNameTemplate();
    qDebug() << "Poster image grabbed. Path:" << m_sPosterPath;
    qInfo() << "Saving poster to:" << m_sPosterPath;
    img.save(m_sPosterPath);
    DAbstractDialog::accept();
    qDebug() << "Dialog accepted.";
    qDebug() << "Exiting BurstScreenshotsDialog::savePoster().";
}

/**
 * @brief savedPosterPath 保存截图路径
 * @return 返回设置的截图保存路径
 */
QString BurstScreenshotsDialog::savedPosterPath()
{
    qDebug() << "Getting saved poster path:" << m_sPosterPath;
    return m_sPosterPath;
}

}

