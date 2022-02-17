/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiepengfei <xiepengfei@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#include "platform_dbus_adpator.h"
#include "utils.h"

Platform_ApplicationAdaptor::Platform_ApplicationAdaptor(Platform_MainWindow *pMainWid)
    : QDBusAbstractAdaptor(pMainWid)
{
    initMember();

    m_pMainWindow = pMainWid;

    m_oldTime = QTime::currentTime();
}

//cppcheck 单元测试 wayland下再用
void Platform_ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    // 快速点击，播放不正常问题
    QTime current = QTime::currentTime();
    if (abs(m_oldTime.msecsTo(current)) > 800) {
        m_oldTime = current;
        m_pMainWindow->play(listFiles);
    }
}

//cppcheck 单元测试在用
void Platform_ApplicationAdaptor::openFile(const QString &sFile)
{
    QRegExp url_re("\\w+://");

    QUrl url;
    if (url_re.indexIn(sFile) == 0) {
        url = QUrl(sFile);
    } else {
        url = QUrl::fromLocalFile(sFile);
    }

    QTime current = QTime::currentTime();
    if (abs(m_oldTime.msecsTo(current)) > 800) {
        m_oldTime = current;
        m_pMainWindow->play({url.toString()});
    }
}

void Platform_ApplicationAdaptor::Raise()
{
    qInfo() << "raise window from dbus";
    m_pMainWindow->showNormal();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
}

void Platform_ApplicationAdaptor::initMember()
{
    m_pMainWindow = nullptr;
}

QVariant Platform_ApplicationAdaptor::redDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    // 调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    QVariant v = ainterface.property(pPropert);
    return  v;
}

//cppcheck 单元测试在使用
QVariant Platform_ApplicationAdaptor::redDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    // 调用远程的value方法
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
        QVariant v(0) ;
        return  v;
    } else {
        qInfo() << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
}
