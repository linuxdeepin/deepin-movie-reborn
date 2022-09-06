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
#ifndef _DMR_DBUS_ADAPTOR
#define _DMR_DBUS_ADAPTOR

#include <QtDBus>

#include "mainwindow.h"

using namespace dmr;
/**
 * @file d-bus适配器，开放影院d-bus接口
 */
class ApplicationAdaptor: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie")
public:
    explicit ApplicationAdaptor(MainWindow *pMainWid);
    /**
     * @brief 读取d-bus接口参数值
     * @param dbus服务名
     * @param dbus路径
     * @param dbus接口名
     * @param 获取的参数名
     * @return 获取参数的值
     */
    static QVariant redDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface = QString(), const char *pPropert = "");
    /**
     * @brief 调用d-bus方法
     * @param d-bus服务名
     * @param d-bus路径
     * @param d-bus接口名
     * @param d-bus的方法
     * @return 方法的返回值
     */
    static QVariant redDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod);

public slots:
    /**
     * @brief 通过d-bus服务播放视频
     * @param 视频路径
     */
    void openFile(const QString &sFile);
    /**
     * @brief 通过d-bus服务播放视频
     * @param 视频路径集合
     */
    void openFiles(const QStringList &listFiles);
    /**
     * @brief 调用mainwindow的raise方法
     */
    void Raise();

private:
    void initMember();

private:
    MainWindow *m_pMainWindow;    ///主窗口指针
    QTime m_oldTime;              ///记录上次播放时的时间
};


#endif /* ifndef _DMR_DBUS_ADAPTOR */
