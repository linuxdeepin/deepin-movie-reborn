/**
 * Copyright (C) 2016 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#include <locale.h>

#include <QtWidgets>
#include <QtDBus>

#include <DLog>
#include <DMainWindow>
#include <DApplication>
#include <DWidgetUtil>

#include "config.h"

#include "options.h"
#include "dmr_settings.h"
#include "mainwindow.h"
#include "dbus_adpator.h"
#include "compositing_manager.h"
#include "utils.h"
#ifdef ENABLE_VPU_PLATFORM
#include "vpu_proxy.h"
#endif
#include "movie_configuration.h"

DWIDGET_USE_NAMESPACE


int main(int argc, char *argv[])
{
    CompositingManager::detectOpenGLEarly();

    DApplication::loadDXcbPlugin();

#if defined(STATIC_LIB)
    DWIDGET_INIT_RESOURCE();
#endif

    DApplication app(argc, argv);

    // required by mpv
    setlocale(LC_NUMERIC, "C");

    app.setOrganizationName("deepin");
    app.setApplicationName("deepin-movie");
    app.setApplicationVersion(DMR_VERSION);
    app.setProductIcon(QPixmap(":/resources/icons/logo-big.svg"));
    app.setWindowIcon(QIcon(":/resources/icons/logo-big.svg"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app.setApplicationAcknowledgementPage(acknowledgementLink);

    auto& clm = dmr::CommandLineManager::get();
    clm.process(app);

    QStringList toOpenFiles;
    if (clm.positionalArguments().length() > 0) {
        toOpenFiles = clm.positionalArguments();
    }

    app.loadTranslator();
    app.setApplicationDisplayName(QObject::tr("Deepin Movie"));
    app.setApplicationDescription(QObject::tr(
                "Deepin Movie is a well-designed and full-featured"
                " video player with simple borderless design. It supports local and"
                " streaming media play with multiple video formats."));

    auto light_theme = dmr::Settings::get().internalOption("light_theme").toBool();
    app.setTheme(light_theme ? "light": "dark");

    Dtk::Core::DLogManager::registerConsoleAppender();
    Dtk::Core::DLogManager::registerFileAppender();
    qDebug() << "log path: " << Dtk::Core::DLogManager::getlogFilePath();

    bool singleton = !dmr::Settings::get().isSet(dmr::Settings::MultipleInstance);
    if (singleton && !app.setSingleInstance("deepinmovie")) {
        qDebug() << "another deepin movie instance has started";
        if (!toOpenFiles.isEmpty()) {
            QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
            if (toOpenFiles.size() == 1) {
                iface.asyncCall("openFile", toOpenFiles[0]);
            } else {
                iface.asyncCall("openFiles", toOpenFiles);
            }
        }
        
        exit(0);
    }


    app.setWindowIcon(QIcon(":/resources/icons/logo.svg"));
    app.setApplicationDisplayName(QObject::tr("Deepin Movie"));
    app.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);

    MovieConfiguration::get().init();

    QRegExp url_re("\\w+://");

#ifdef ENABLE_VPU_PLATFORM
    if (dmr::CommandLineManager::get().vpuDemoMode()) {
        dmr::VpuProxy mw;
        mw.setMinimumSize(QSize(528, 400));
        mw.resize(850, 600);
        utils::MoveToCenter(&mw);
        mw.show();
        auto fi = QFileInfo(toOpenFiles[0]);
        if (fi.exists()) {
            mw.setPlayFile(fi);
            mw.play();
        }

        return app.exec();
    }
#endif

    dmr::MainWindow mw;
    mw.setMinimumSize(QSize(528, 400));
    mw.resize(850, 600);
    utils::MoveToCenter(&mw);
    mw.show();

    if (!QDBusConnection::sessionBus().isConnected()) {
        qWarning() << "dbus disconnected";
    }

    ApplicationAdaptor adaptor(&mw);
    QDBusConnection::sessionBus().registerService("com.deepin.movie");
    QDBusConnection::sessionBus().registerObject("/", &mw);

    if (!toOpenFiles.isEmpty()) {
        mw.playList(toOpenFiles);
    }
    return app.exec();
}

