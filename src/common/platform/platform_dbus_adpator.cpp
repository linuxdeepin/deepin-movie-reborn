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
    initMember();

    m_pMainWindow = pMainWid;
}

//cppcheck 单元测试 wayland下再用
void Platform_ApplicationAdaptor::openFiles(const QStringList &listFiles)
{
    m_pMainWindow->play(listFiles);
}

//cppcheck 单元测试在用
void Platform_ApplicationAdaptor::openFile(const QString &sFile)
{
    if(sFile.startsWith("UOS_AI")) {
        QString uosAiStr = sFile.mid(6);
        qInfo() << "sFile: " << sFile << " midd: " << uosAiStr;
        funOpenFile(uosAiStr);
        return;
    }
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp url_re("\\w+://");
    bool isMatch = (url_re.indexIn(sFile) == 0);
#else
    QRegularExpression url_re("\\w+://");
    bool isMatch = url_re.match(sFile).capturedStart() == 0;
#endif

    QUrl url;
    if (isMatch) {
        url = QUrl(sFile);
    } else {
        url = QUrl::fromLocalFile(sFile);
    }
    m_pMainWindow->play({url.toString()});
}

void Platform_ApplicationAdaptor::funOpenFile(const QString &sFile)
{
    if(m_pMainWindow) {
        QList<PlayItemInfo> lstItem = m_pMainWindow->playlist()->engine()->playlist().items();
        for (PlayItemInfo info: lstItem) {
            if(QFileInfo(info.mi.filePath).fileName().toLower().contains(sFile.toLower())) {
                qInfo() << "Platform_funOpenFile: " << info.mi.filePath;
                m_pMainWindow->play({QUrl::fromLocalFile(info.mi.filePath).toString()});
                break;
            }
        }
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
