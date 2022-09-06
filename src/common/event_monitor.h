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
#ifndef EVENTMONITOR_H
#define EVENTMONITOR_H

#include <QThread>

namespace dmr {

// Virtual button codes that are not defined by X11.
#define Button1         1
#define Button2         2
#define Button3         3
#define WheelUp         4
#define WheelDown       5
#define WheelLeft       6
#define WheelRight      7
#define XButton1        8
#define XButton2        9
/**
 * @file x11事件过滤器线程
 */
class EventMonitor : public QThread
{
    Q_OBJECT

public:
    explicit EventMonitor(QObject *parent = nullptr);
    /**
     * @brief 事件处理函数，处理点击事件
     */
    void handleRecordEvent(void *);
    /**
     * @brief 应用进入激活状态
     */
    void resumeRecording();
    /**
     * @brief 应用失去选中状态
     */
    void suspendRecording();

signals:
    /**
     * @brief 鼠标按下信号
     * @param 按下的横坐标
     * @param 纵坐标
     */
    void buttonedPress(int nPosX, int nPosY);
    /**
     * @brief 鼠标拖动信号
     * @param 拖动的横坐标
     * @param 纵坐标
     */
    void buttonedDrag(int nPosX, int nPosY);
    /**
     * @brief 鼠标释放信号
     * @param 按下的横坐标
     * @param 纵坐标
     */
    void buttonedRelease(int nPosX, int nPosY);

//cppcheck 误报
protected:
    void run();

private:
    bool m_bIsPress;            ///记录鼠标是否按下标志位
    QAtomicInt m_recording {1}; ///记录应用是否是激活状态
};

}
#endif
