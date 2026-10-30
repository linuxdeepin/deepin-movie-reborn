// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbusutils.h"
#include "ddlog.h"
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
    qCDebug(appMovie) << "Entering DBusUtils constructor.";
    // Constructor body is empty, no specific initialization to log
    qCDebug(appMovie) << "Exiting DBusUtils constructor.";
}

QVariant DBusUtils::readDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qCDebug(appMovie) << "Entering DBusUtils::readDBusProperty. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    mutex.lock();
    qCDebug(appMovie) << "Mutex locked in readDBusProperty.";

    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
//        qCInfo(appMovie) << qPrintable(QDBusConnection::sessionBus().lastError().message());
        //qCInfo(appMovie) << " QDBusInterface ainterface isValid" << path << propert;
        QVariant v(0) ;
        mutex.unlock();
        qCDebug(appMovie) << "Mutex unlocked. Exiting readDBusProperty with invalid interface.";
        return  v;
    }
    //调用远程的value方法
    qCDebug(appMovie) << "Attempting to get property:" << pPropert;
    QVariant v = ainterface.property(pPropert);
    mutex.unlock();
    qCDebug(appMovie) << "Mutex unlocked. Exiting DBusUtils::readDBusProperty. Returned value:" << v;
    return  v;
}
QVariant DBusUtils::readDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qCDebug(appMovie) << "Entering DBusUtils::readDBusMethod. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        //qCInfo(appMovie) <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
    //调用远程的value方法
    qCDebug(appMovie) << "Attempting to call method:" << pMethod;
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
        qCDebug(appMovie) << "Exiting DBusUtils::readDBusMethod with valid reply.";
        return reply.value().variant();
    } else {
        //qCInfo(appMovie) << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        return  v;
    }
}
