// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
/**
 * @file
 * 此文件为影院播放截图相关。
 */
#ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H
#define _DMR_BURST_SCREENSHOTS_DIALOG_H

#include <QtWidgets>
#include <DDialog>
#include <DTitlebar>

DWIDGET_USE_NAMESPACE

namespace dmr {
class PlayItemInfo;

/**
 * @brief The ThumbnailFrame class
 * 单个截图图像窗口
 */
class ThumbnailFrame: public QLabel
{
    Q_OBJECT
public:
    /**
     * @brief ThumbnailFrame 构造函数
     * @param parent 父窗口
     */
    explicit ThumbnailFrame(QWidget *parent) : QLabel(parent)
    {
        //参考设计图
        setFixedSize(178, 100);
        QGraphicsDropShadowEffect *pGraphicsShadow = new QGraphicsDropShadowEffect(this);
        pGraphicsShadow->setColor(QColor(0, 0, 0, 255 * 2 / 10));
        //参考设计图
        pGraphicsShadow->setOffset(0, 2);
        //参考设计图
        pGraphicsShadow->setBlurRadius(4);
        setGraphicsEffect(pGraphicsShadow);
    }
};

/**
 * @brief The BurstScreenshotsDialog class
 * 截图窗口
 */
class BurstScreenshotsDialog: public DAbstractDialog
{
    Q_OBJECT
public:
    /**
     * @brief BurstScreenshotsDialog 构造函数
     * @param strPlayItemInfo 播放项信息
     */
    explicit BurstScreenshotsDialog(const PlayItemInfo &strPlayItemInfo);
    /**
     * @brief updateWithFrames 更新截图图像
     * @param frames 截图图像
     */
    void updateWithFrames(const QList<QPair<QImage, qint64>> &frames);
    /**
     * @brief savedPosterPath 保存截图路径
     * @return 返回设置的截图保存路径
     */
    QString savedPosterPath();

public slots:
    /**
     * @brief exec 返回执行函数的标识符
     * @return 执行函数的标识符
     */
    int exec() override;
    /**
     * @brief savePoster 保存截图
     */
    void savePoster();

private:
    QGridLayout             *m_pGrid;       ///截图窗口布局
    QPushButton             *m_pSaveBtn;    ///截图保存按键
    QString                  m_sPosterPath; ///截图保存路径
    DTitlebar               *m_pTitlebar;   ///截图窗口标题栏
};
}

#endif /* ifndef _DMR_BURST_SCREENSHOTS_DIALOG_H */
