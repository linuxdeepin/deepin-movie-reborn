// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbusutils.h"
#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusInterface>
#include <QDebug>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMutex>

static QMutex mutex;

DBusUtils::DBusUtils()
{
    qDebug() << "Entering DBusUtils constructor.";
    // Constructor body is empty, no specific initialization to log
    qDebug() << "Exiting DBusUtils constructor.";
}

QVariant DBusUtils::readDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qDebug() << "Entering DBusUtils::redDBusProperty. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    mutex.lock();
    qDebug() << "Mutex locked in redDBusProperty.";

    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
//        qInfo() << qPrintable(QDBusConnection::sessionBus().lastError().message());
        //qInfo() << " QDBusInterface ainterface isValid" << path << propert;
        QVariant v(0) ;
        mutex.unlock();
        qDebug() << "Mutex unlocked. Exiting redDBusProperty with invalid interface.";
        return  v;
    }
    //调用远程的value方法
    qDebug() << "Attempting to get property:" << pPropert;
    QVariant v = ainterface.property(pPropert);
    mutex.unlock();
    qDebug() << "Mutex unlocked. Exiting DBusUtils::redDBusProperty. Returned value:" << v;
    return  v;
}
QVariant DBusUtils::readDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qDebug() << "Entering DBusUtils::redDBusMethod. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        //qInfo() <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    //调用远程的value方法
    qDebug() << "Attempting to call method:" << pMethod;
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
//        return reply.value();
        QVariant v(0) ;
        return  v;
    } else {
        //qInfo() << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
}
