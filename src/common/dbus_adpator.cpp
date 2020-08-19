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
#include "dbus_adpator.h"

ApplicationAdaptor::ApplicationAdaptor(MainWindow* mw)
    :QDBusAbstractAdaptor(mw), _mw(mw) 
{
}

void ApplicationAdaptor::openFiles(const QStringList& list)
{
    _mw->playList(list);
}

void ApplicationAdaptor::openFile(const QString& file) 
{
    QRegExp url_re("\\w+://");

    QUrl url;
    if (url_re.indexIn(file) == 0) {
        url = QUrl(file);
    } else {
        url = QUrl::fromLocalFile(file);
    }
    _mw->play(url);
}

void ApplicationAdaptor::Raise(){
   //临时使用
    _mw->hide();
    _mw->show();
}

QVariant ApplicationAdaptor::redDBusProperty(const QString &service, const QString &path, const QString &interface, const char *propert)
{
    // 创建QDBusInterface接口
    QDBusInterface ainterface(service, path,
                              interface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qDebug() << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    //调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    QVariant v = ainterface.property(propert);
    return  v;
}
QVariant ApplicationAdaptor::redDBusMethod(const QString &service, const QString &path, const QString &interface, const char *method)
{
    // 创建QDBusInterface接口
    QDBusInterface ainterface(service, path,
                              interface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qDebug() <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    //调用远程的value方法
    QDBusReply<QDBusVariant> reply = ainterface.call(method);
    if (reply.isValid()) {
//        return reply.value();
        QVariant v(0) ;
        return  v;
    } else {
        qDebug() << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
}


