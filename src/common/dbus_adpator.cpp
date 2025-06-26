// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbus_adpator.h"
#include "utils.h"
#include "playlist_widget.h"
#include "player_engine.h"
#include "playlist_model.h"

ApplicationAdaptor::ApplicationAdaptor(MainWindow *pMainWid)
    : QDBusAbstractAdaptor(pMainWid)
{
    qDebug() << "Entering ApplicationAdaptor constructor.";
    initMember();

    m_pMainWindow = pMainWid;
    qDebug() << "Exiting ApplicationAdaptor constructor.";
}

//cppcheck 单元测试 wayland下再用
void ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    qDebug() << "Entering ApplicationAdaptor::openFiles. Files count:" << listFiles.count();
    m_pMainWindow->play(listFiles);
    qDebug() << "Exiting ApplicationAdaptor::openFiles.";
}

//cppcheck 单元测试在用
void ApplicationAdaptor::openFile(const QString &sFile)
{
    qDebug() << "Entering ApplicationAdaptor::openFile. File:" << sFile;
    if(sFile.startsWith("UOS_AI")) {
        QString uosAiStr = sFile.mid(6);
        qInfo() << "sFile: " << sFile << " midd: " << uosAiStr;
        funOpenFile(uosAiStr);
        qDebug() << "Exiting ApplicationAdaptor::openFile after UOS_AI processing.";
        return;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp url_re("\\w+://");
    QUrl url;
    if (url_re.indexIn(sFile) == 0) {
        qDebug() << "File is a URL. Converting to QUrl.";
        url = QUrl(sFile);
    } else {
        qDebug() << "File is a local path. Converting to QUrl from local file.";
        url = QUrl::fromLocalFile(sFile);
    }
#else
    QRegularExpression url_re("\\w+://");
    QUrl url;
    if (url_re.match(sFile).capturedStart() == 0) {
        qDebug() << "File is a URL. Converting to QUrl.";
        url = QUrl(sFile);
    } else {
        qDebug() << "File is a local path. Converting to QUrl from local file.";
        url = QUrl::fromLocalFile(sFile);
    }
#endif
    m_pMainWindow->play({url.toString()});
    qDebug() << "Exiting ApplicationAdaptor::openFile.";
}

void ApplicationAdaptor::funOpenFile(const QString &sFile)
{
    qDebug() << "Entering ApplicationAdaptor::funOpenFile. File:" << sFile;
    if(m_pMainWindow) {
        qDebug() << "MainWindow pointer is valid.";
        QList<PlayItemInfo> lstItem = m_pMainWindow->playlist()->engine()->playlist().items();
        for (PlayItemInfo info: lstItem) {
            if(QFileInfo(info.mi.filePath).fileName().toLower().contains(sFile.toLower())) {
                qInfo() << "funOpenFile: " << info.mi.filePath;
                m_pMainWindow->play({QUrl::fromLocalFile(info.mi.filePath).toString()});
                break;
            }
        }
    } else {
        qDebug() << "MainWindow pointer is null. Cannot open file.";
    }
    qDebug() << "Exiting ApplicationAdaptor::funOpenFile.";
}

void ApplicationAdaptor::Raise()
{
    qInfo() << "raise window from dbus";
    m_pMainWindow->showNormal();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
    qDebug() << "Exiting ApplicationAdaptor::Raise.";
}

void ApplicationAdaptor::initMember()
{
    qDebug() << "Entering ApplicationAdaptor::initMember.";
    m_pMainWindow = nullptr;
    qDebug() << "Exiting ApplicationAdaptor::initMember.";
}

QVariant ApplicationAdaptor::redDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qDebug() << "Entering ApplicationAdaptor::redDBusProperty.";
    qDebug() << "Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qDebug() << "Exiting ApplicationAdaptor::redDBusProperty with invalid interface.";
        return  v;
    }
    // 调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    QVariant v = ainterface.property(pPropert);
    qDebug() << "Exiting ApplicationAdaptor::redDBusProperty. Property value:" << v;
    return  v;
}

//cppcheck 单元测试在使用
QVariant ApplicationAdaptor::redDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qDebug() << "Entering ApplicationAdaptor::redDBusMethod.";
    qDebug() << "Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qDebug() << "Exiting ApplicationAdaptor::redDBusMethod with invalid interface.";
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
        qDebug() << "Exiting ApplicationAdaptor::redDBusMethod with failed method call.";
        return  v;
    }
}
