// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_dbus_adpator.h"
#include "utils.h"
#include "platform/platform_playlist_widget.h"
#include "player_engine.h"
#include "playlist_model.h"

Platform_ApplicationAdaptor::Platform_ApplicationAdaptor(Platform_MainWindow *pMainWid)
    : QDBusAbstractAdaptor(pMainWid)
{
    qDebug() << "Entering Platform_ApplicationAdaptor constructor.";
    initMember();

    m_pMainWindow = pMainWid;
    qDebug() << "Exiting Platform_ApplicationAdaptor constructor.";
}

//cppcheck 单元测试 wayland下再用
void Platform_ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    qDebug() << "Entering Platform_ApplicationAdaptor::openFiles. Files count:" << listFiles.count();
    m_pMainWindow->play(listFiles);
    qDebug() << "Exiting Platform_ApplicationAdaptor::openFiles.";
}

//cppcheck 单元测试在用
void Platform_ApplicationAdaptor::openFile(const QString &sFile)
{
    qDebug() << "Entering Platform_ApplicationAdaptor::openFile. File:" << sFile;
    if(sFile.startsWith("UOS_AI")) {
        QString uosAiStr = sFile.mid(6);
        qInfo() << "sFile: " << sFile << " midd: " << uosAiStr;
        funOpenFile(uosAiStr);
        qDebug() << "File is UOS_AI string. Called funOpenFile and returning.";
        return;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp url_re("\\w+://");
    bool isMatch = (url_re.indexIn(sFile) == 0);
    qDebug() << "QT_VERSION < 6.0.0. Using QRegExp. isMatch:" << isMatch;
#else
    QRegularExpression url_re("\\w+://");
    bool isMatch = url_re.match(sFile).capturedStart() == 0;
    qDebug() << "QT_VERSION >= 6.0.0. Using QRegularExpression. isMatch:" << isMatch;
#endif

    QUrl url;
    if (isMatch) {
        url = QUrl(sFile);
        qDebug() << "File matches URL pattern. Created QUrl from file:" << url.toString();
    } else {
        url = QUrl::fromLocalFile(sFile);
        qDebug() << "File does not match URL pattern. Created QUrl from local file:" << url.toString();
    }
    m_pMainWindow->play({url.toString()});
    qDebug() << "Exiting Platform_ApplicationAdaptor::openFile. Playback initiated.";
}

void Platform_ApplicationAdaptor::funOpenFile(const QString &sFile)
{
    qDebug() << "Entering Platform_ApplicationAdaptor::funOpenFile. File to search:" << sFile;
    if(m_pMainWindow) {
        qDebug() << "MainWindow is valid. Retrieving playlist items.";
        QList<PlayItemInfo> lstItem = m_pMainWindow->playlist()->engine()->playlist().items();
        qDebug() << "Playlist items count:" << lstItem.count();
        for (PlayItemInfo info: lstItem) {
            if(QFileInfo(info.mi.filePath).fileName().toLower().contains(sFile.toLower())) {
                qInfo() << "Platform_funOpenFile: " << info.mi.filePath;
                m_pMainWindow->play({QUrl::fromLocalFile(info.mi.filePath).toString()});
                qDebug() << "Playing matched file and breaking loop.";
                break;
            } else {
                qDebug() << "Playlist item file name does not contain search string (case-insensitive).";
            }
        }
    } else {
        qWarning() << "MainWindow is null in Platform_ApplicationAdaptor::funOpenFile. Cannot process file.";
    }
    qDebug() << "Exiting Platform_ApplicationAdaptor::funOpenFile.";
}

void Platform_ApplicationAdaptor::Raise()
{
    qDebug() << "Entering Platform_ApplicationAdaptor::Raise().";
    qInfo() << "raise window from dbus";
    m_pMainWindow->showNormal();
    m_pMainWindow->raise();
    m_pMainWindow->activateWindow();
    qDebug() << "Exiting Platform_ApplicationAdaptor::Raise(). Window raised and activated.";
}

void Platform_ApplicationAdaptor::initMember()
{
    qDebug() << "Entering Platform_ApplicationAdaptor::initMember().";
    m_pMainWindow = nullptr;
    qDebug() << "Exiting Platform_ApplicationAdaptor::initMember(). m_pMainWindow set to nullptr.";
}

QVariant Platform_ApplicationAdaptor::readDBusProperty(const QString &sService, const QString &sPath, const QString &sInterface, const char *pPropert)
{
    qDebug() << "Entering Platform_ApplicationAdaptor::redDBusProperty. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Property:" << pPropert;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qWarning() << "QDBusInterface is not valid. Returning default QVariant(0).";
        return  v;
    } else {
        qDebug() << "QDBusInterface created successfully.";
    }
    // 调用远程的value方法
    QList<QByteArray> q = ainterface.dynamicPropertyNames();
    qDebug() << "Dynamic property names retrieved. Count:" << q.count();
    QVariant v = ainterface.property(pPropert);
    qDebug() << "Property value retrieved. Returning:" << v;
    qDebug() << "Exiting Platform_ApplicationAdaptor::redDBusProperty.";
    return  v;
}

//cppcheck 单元测试在使用
QVariant Platform_ApplicationAdaptor::readDBusMethod(const QString &sService, const QString &sPath, const QString &sInterface, const char *pMethod)
{
    qDebug() << "Entering Platform_ApplicationAdaptor::redDBusMethod. Service:" << sService << ", Path:" << sPath << ", Interface:" << sInterface << ", Method:" << pMethod;
    // 创建QDBusInterface接口
    QDBusInterface ainterface(sService, sPath,
                              sInterface,
                              QDBusConnection::sessionBus());
    if (!ainterface.isValid()) {
        qInfo() <<  "error:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qWarning() << "QDBusInterface is not valid. Returning default QVariant(0).";
        return  v;
    } else {
        qDebug() << "QDBusInterface created successfully.";
    }
    // 调用远程的value方法
    QDBusReply<QDBusVariant> reply = ainterface.call(pMethod);
    if (reply.isValid()) {
        QVariant v(0) ;
        qDebug() << "QDBusReply is valid. Returning default QVariant(0).";
        return  v;
    } else {
        qInfo() << "error1:" << qPrintable(QDBusConnection::sessionBus().lastError().message());
        QVariant v(0) ;
        qWarning() << "QDBusReply is not valid. Returning default QVariant(0).";
        return  v;
    }
    qDebug() << "Exiting Platform_ApplicationAdaptor::redDBusMethod.";
}
