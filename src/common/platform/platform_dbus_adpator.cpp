// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_dbus_adpator.h"
#include "ddlog.h"
#include "utils.h"
#include "platform/platform_playlist_widget.h"
#include "player_engine.h"
#include "playlist_model.h"

Platform_ApplicationAdaptor::Platform_ApplicationAdaptor(Platform_MainWindow *pMainWid)
    : QDBusAbstractAdaptor(pMainWid)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor constructor.";
    initMember();

    m_pMainWindow = pMainWid;
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor constructor.";
}

//cppcheck 单元测试 wayland下再用
void Platform_ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::openFiles. Files count:" << listFiles.count();
    m_pMainWindow->play(listFiles);
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::openFiles.";
}

//cppcheck 单元测试在用
void Platform_ApplicationAdaptor::openFile(const QString &sFile)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::openFile. File:" << sFile;
    if(sFile.startsWith("UOS_AI")) {
        QString uosAiStr = sFile.mid(6);
        qCInfo(appMovie) << "sFile: " << sFile << " midd: " << uosAiStr;
        funOpenFile(uosAiStr);
        qCDebug(appMovie) << "File is UOS_AI string. Called funOpenFile and returning.";
        return;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp url_re("\\w+://");
    bool isMatch = (url_re.indexIn(sFile) == 0);
    qCDebug(appMovie) << "QT_VERSION < 6.0.0. Using QRegExp. isMatch:" << isMatch;
#else
    QRegularExpression url_re("\\w+://");
    bool isMatch = url_re.match(sFile).capturedStart() == 0;
    qCDebug(appMovie) << "QT_VERSION >= 6.0.0. Using QRegularExpression. isMatch:" << isMatch;
#endif

    QUrl url;
    if (isMatch) {
        url = QUrl(sFile);
        qCDebug(appMovie) << "File matches URL pattern. Created QUrl from file:" << url.toString();
    } else {
        url = QUrl::fromLocalFile(sFile);
        qCDebug(appMovie) << "File does not match URL pattern. Created QUrl from local file:" << url.toString();
    }
    m_pMainWindow->play({url.toString()});
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::openFile. Playback initiated.";
}

void Platform_ApplicationAdaptor::funOpenFile(const QString &sFile)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::funOpenFile. File to search:" << sFile;
    if(m_pMainWindow) {
        qCDebug(appMovie) << "MainWindow is valid. Retrieving playlist items.";
        QList<PlayItemInfo> lstItem = m_pMainWindow->playlist()->engine()->playlist().items();
        qCDebug(appMovie) << "Playlist items count:" << lstItem.count();
        for (PlayItemInfo info: lstItem) {
            if(QFileInfo(info.mi.filePath).fileName().toLower().contains(sFile.toLower())) {
                qCInfo(appMovie) << "Platform_funOpenFile: " << info.mi.filePath;
                m_pMainWindow->play({QUrl::fromLocalFile(info.mi.filePath).toString()});
                qCDebug(appMovie) << "Playing matched file and breaking loop.";
                break;
            } else {
                qCDebug(appMovie) << "Playlist item file name does not contain search string (case-insensitive).";
            }
        }
    } else {
        qCWarning(appMovie) << "MainWindow is null in Platform_ApplicationAdaptor::funOpenFile. Cannot process file.";
    }
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::funOpenFile.";
}

void Platform_ApplicationAdaptor::Raise()
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::Raise().";
    qCInfo(appMovie) << "raise window from dbus";
    m_pMainWindow->showNormal();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::Raise(). Window raised and activated.";
}

void Platform_ApplicationAdaptor::initMember()
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::initMember().";
    m_pMainWindow = nullptr;
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::initMember(). m_pMainWindow set to nullptr.";
}

QVariant Platform_ApplicationAdaptor::readDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::readDBusProperty. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qCInfo(appMovie) << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCWarning(appMovie) << "QDBusInterface is not valid. Returning default QVariant(0).";
        return  v;
    } else {
        qCDebug(appMovie) << "QDBusInterface created successfully.";
    }
    // 调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    qCDebug(appMovie) << "Dynamic property names retrieved. Count:" << q.count();
    QVariant v = ainterface.property(pPropert);
    qCDebug(appMovie) << "Property value retrieved. Returning:" << v;
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::readDBusProperty.";
    return  v;
}

//cppcheck 单元测试在使用
QVariant Platform_ApplicationAdaptor::readDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qCDebug(appMovie) << "Entering Platform_ApplicationAdaptor::readDBusMethod. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qCInfo(appMovie) <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCWarning(appMovie) << "QDBusInterface is not valid. Returning default QVariant(0).";
        return  v;
    } else {
        qCDebug(appMovie) << "QDBusInterface created successfully.";
    }
    // 调用远程的value方法
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
        QVariant v(0) ;
        qCDebug(appMovie) << "QDBusReply is valid. Returning default QVariant(0).";
        return  v;
    } else {
        qCInfo(appMovie) << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qCWarning(appMovie) << "QDBusReply is not valid. Returning default QVariant(0).";
        return  v;
    }
    qCDebug(appMovie) << "Exiting Platform_ApplicationAdaptor::readDBusMethod.";
}
