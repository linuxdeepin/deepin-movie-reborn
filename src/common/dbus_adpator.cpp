// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbus_adpator.h"
#include "ddlog.h"
#include "utils.h"
#include "playlist_widget.h"
#include "player_engine.h"
#include "playlist_model.h"

ApplicationAdaptor::ApplicationAdaptor(MainWindow *pMainWid)
    : QDBusAbstractAdaptor(pMainWid)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor constructor.";
    initMember();

    m_pMainWindow = pMainWid;
    qCDebug(appMovie) << "Exiting ApplicationAdaptor constructor.";
}

//cppcheck 单元测试 wayland下再用
void ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::openFiles. Files count:" << listFiles.count();
    m_pMainWindow->play(listFiles);
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::openFiles.";
}

//cppcheck 单元测试在用
void ApplicationAdaptor::openFile(const QString &sFile)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::openFile. File:" << sFile;
    if(sFile.startsWith("UOS_AI")) {
        QString uosAiStr = sFile.mid(6);
        qCInfo(appMovie) << "sFile: " << sFile << " midd: " << uosAiStr;
        funOpenFile(uosAiStr);
        qCDebug(appMovie) << "Exiting ApplicationAdaptor::openFile after UOS_AI processing.";
        return;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp url_re("\\w+://");
    QUrl url;
    if (url_re.indexIn(sFile) == 0) {
        qCDebug(appMovie) << "File is a URL. Converting to QUrl.";
        url = QUrl(sFile);
    } else {
        qCDebug(appMovie) << "File is a local path. Converting to QUrl from local file.";
        url = QUrl::fromLocalFile(sFile);
    }
#else
    QRegularExpression url_re("\\w+://");
    QUrl url;
    if (url_re.match(sFile).capturedStart() == 0) {
        qCDebug(appMovie) << "File is a URL. Converting to QUrl.";
        url = QUrl(sFile);
    } else {
        qCDebug(appMovie) << "File is a local path. Converting to QUrl from local file.";
        url = QUrl::fromLocalFile(sFile);
    }
#endif
    m_pMainWindow->play({url.toString()});
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::openFile.";
}

void ApplicationAdaptor::funOpenFile(const QString &sFile)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::funOpenFile. File:" << sFile;
    if(m_pMainWindow) {
        qCDebug(appMovie) << "MainWindow pointer is valid.";
        QList<PlayItemInfo> lstItem = m_pMainWindow->playlist()->engine()->playlist().items();
        for (PlayItemInfo info: lstItem) {
            if(QFileInfo(info.mi.filePath).fileName().toLower().contains(sFile.toLower())) {
                qCInfo(appMovie) << "funOpenFile: " << info.mi.filePath;
                m_pMainWindow->play({QUrl::fromLocalFile(info.mi.filePath).toString()});
                break;
            }
        }
    } else {
        qCDebug(appMovie) << "MainWindow pointer is null. Cannot open file.";
    }
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::funOpenFile.";
}

void ApplicationAdaptor::Raise()
{
    qCInfo(appMovie) << "raise window from dbus";
    m_pMainWindow->showNormal();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::Raise.";
}

void ApplicationAdaptor::initMember()
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::initMember.";
    m_pMainWindow = nullptr;
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::initMember.";
}

QVariant ApplicationAdaptor::readDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::readDBusProperty.";
    qCDebug(appMovie) << "Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qCInfo(appMovie) << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCDebug(appMovie) << "Exiting ApplicationAdaptor::readDBusProperty with invalid interface.";
        return  v;
    }
    // 调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    QVariant v = ainterface.property(pPropert);
    qCDebug(appMovie) << "Exiting ApplicationAdaptor::readDBusProperty. Property value:" << v;
    return  v;
}

//cppcheck 单元测试在使用
QVariant ApplicationAdaptor::readDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qCDebug(appMovie) << "Entering ApplicationAdaptor::readDBusMethod.";
    qCDebug(appMovie) << "Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qCInfo(appMovie) <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCDebug(appMovie) << "Exiting ApplicationAdaptor::readDBusMethod with invalid interface.";
        return  v;
    }
    // 调用远程的value方法
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
        QVariant v(0) ;
        return  v;
    } else {
        qCInfo(appMovie) << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCDebug(appMovie) << "Exiting ApplicationAdaptor::readDBusMethod with failed method call.";
        return  v;
    }
}
