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
  *@file 这个文件是负责实现全屏展示进度控件相关
  */
#ifndef _DMR_Platform_MOVIE_PROGRESS_INDICATOR_H
#define _DMR_Platform_MOVIE_PROGRESS_INDICATOR_H

#include <QtWidgets>

namespace dmr {
/**
 * @brief The MovieProgressIndicator class
 * 这个类是全屏时右上角电影进度的展示控件
 */
class Platform_MovieProgressIndicator: public QFrame {
    Q_OBJECT
public:
    /**
     * @brief MovieProgressIndicator 构造函数
     * @param parent 父窗口
     */
    explicit Platform_MovieProgressIndicator(QWidget* parent);

public slots:
    /**
     * @brief updateMovieProgress 更新电影进度控件
     * @param duration 总时长
     * @param pos 当前时长
     */
    void updateMovieProgress(qint64 nDuration, qint64 nPos);

protected:
    /**
     * @brief paintEvent 重载绘制事件函数
     * @param pPaintEvent
     */
    void paintEvent(QPaintEvent* pPaintEvent) override;

private:
    /**
     * @brief initMember 初始化成员变量
     */
    void initMember();

private:
    qint64 m_nElapsed;   ///当前播放的进度
    qreal m_pert;        ///当前播放百分比
    QSize m_fixedSize;   ///窗口大小
};

}

#endif /* ifndef _DMR_Platform_MOVIE_PROGRESS_INDICATOR_H */
