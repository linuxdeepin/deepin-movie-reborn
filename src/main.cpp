// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <locale.h>

#include <QtWidgets>
#include <QtDBus>

#include <DLog>
#include <DMainWindow>
#include <DApplication>
#include <DWidgetUtil>
#include <DApplicationSettings>

#include "config.h"

#include "options.h"
#include "dmr_settings.h"
#include "mainwindow.h"
#include "platform/platform_mainwindow.h"
#include "platform/platform_dbus_adpator.h"
#include "compositing_manager.h"
#include "dbus_adpator.h"
#include "utils.h"
#include "movie_configuration.h"
#include "vendor/movieapp.h"
#include "vendor/presenter.h"
#include <QSettings>

#include <DStandardPaths>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <va/va_x11.h>

#include "accessibility/acobjectlist.h"
DWIDGET_USE_NAMESPACE

bool runSingleInstance()
{
    std::string path;
    QString userName = QDir::homePath().section("/", -1, -1);
    if (userName == "root") {
        path = "/tmp/deepin-movie";
    } else {
        path = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString();
    }
    QDir tdir(path.c_str());
    if (!tdir.exists()) {
        tdir.mkpath(path.c_str());
    }

    path += "/single";
    int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
    int flock = lockf(fd, F_TLOCK, 0);

    if (fd == -1) {
        qInfo() << strerror(errno);
        return false;
    }
    if (flock == -1) {
        qInfo() << strerror(errno);
        return false;
    }
    return true;
}

#ifdef __x86_64__
//进程检测是否支持硬解
void checkIsCanHwdec(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Display *x11=QX11Info::display();
    VADisplay *display = (VADisplay *)vaGetDisplay(x11);
    int major, minor;
    int status = 0;
    try {
        status = vaInitialize(display, &major, &minor);
    }
    catch (...) {
        status = -1;
    }
    exit(status);
}
#endif
//UOS_AI调用函数
QString getFunctionMovieName()
{
    QString movieName = "";
    qInfo() << "single uos ai function call";
    bool isCopilotConnected = false;
    QDBusInterface aiDbus("com.deepin.copilot", "/com/deepin/copilot", "com.deepin.copilot", QDBusConnection::sessionBus());
    if (aiDbus.isValid())
        isCopilotConnected = true;
    if(isCopilotConnected) {
        QDBusReply<QString> functions = aiDbus.call("cachedFunctions");
        QJsonDocument jsonDocu = QJsonDocument::fromJson(functions.value().toUtf8());
        qInfo() << "UOS_AI jsonDocu is: " << jsonDocu;
        if (jsonDocu.isObject()) {
            QJsonObject objRoot = jsonDocu.object();

            for (QString key : objRoot.keys()) {
                QJsonValue valueRoot = objRoot.value(key);

                if (valueRoot.isArray() && key == "functions") {
                    QJsonArray array = valueRoot.toArray();

                    for (int i = 0; i < array.count(); ++i) {
                        if (array[i].isObject()) {
                            //解析每个function的名称和参数
                            QJsonObject funcObj = array[i].toObject();
                            QString functionName = nullptr;
                            QMap<QString, QString>functionArguments;

                            for (QString funcKey : funcObj.keys()) {
                                if (funcKey == "arguments") {
                                    QByteArray arr = funcObj[funcKey].toString().toUtf8();
                                    QJsonDocument argDoc = QJsonDocument::fromJson(arr);
                                    if (argDoc.isObject()) {
                                        QJsonObject argObj= argDoc.object();
                                        for (QString argKey : argObj.keys()) {
                                            functionArguments[argKey] = argObj[argKey].toString();
                                            if(argKey == "name") {
                                                qInfo() << "UOS_AI function argument:  " << argKey << ": " << functionArguments[argKey];
                                                movieName = "UOS_AI"+functionArguments[argKey];
                                                return movieName;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        qInfo() << "isCopilotConnected is false!";
    }
    return movieName;
}

int main(int argc, char *argv[])
{
    //for qt5platform-plugins load DPlatformIntegration or DPlatformIntegrationParent
    if (!QString(qgetenv("XDG_CURRENT_DESKTOP")).toLower().startsWith("deepin")){
        setenv("XDG_CURRENT_DESKTOP", "Deepin", 1);
    }

#ifdef __x86_64__
    if(argc==2 && strcmp(argv[1],"hwdec") == 0) {
        checkIsCanHwdec(argc, argv);
    }
#endif
//#ifdef __aarch64__ //wayland平台支持影院播放
    if (dmr::utils::first_check_wayland_env()) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kwayland-shell");
        //qputenv("_d_disableDBusFileDialog", "true");
        setenv("PULSE_PROP_media.role", "video", 1);
#ifndef __x86_64__
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setDefaultFormat(format);
#endif
    }
//#endif
#ifdef __mips__
    if (CompositingManager::get().composited()) {
        CompositingManager::detectOpenGLEarly();
        CompositingManager::detectPciID();
    }
#endif
#if defined(STATIC_LIB)
    DWIDGET_INIT_RESOURCE();
#endif
    QFileInfo jmfi("/dev/jmgpu");
    if (jmfi.exists()) {
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    }
    /**
      *This function dtk is obsolete and has no
      * impact after testing on x86 platform.
      * If there is a problem with later adaptation,
      * please give priority to whether there
      * is any impact here.
      */
//    DApplication::loadDXcbPlugin();
    DApplication *app = nullptr;
#if (DTK_VERSION < DTK_VERSION_CHECK(5, 4, 0, 0))
    app = new DApplication(argc, argv);
#else
    app = DApplication::globalApplication(argc, argv);
#endif

    QAccessible::installFactory(accessibleFactory);
    // required by mpv
    setlocale(LC_NUMERIC, "C");

#ifdef __mips__
    if (CompositingManager::get().composited()) {
        app->setAttribute(Qt::AA_UseHighDpiPixmaps);
        // overwrite DApplication default value
        app->setAttribute(Qt::AA_ForceRasterWidgets, false);
    }
#else
    app->setAttribute(Qt::AA_UseHighDpiPixmaps);
    // overwrite DApplication default value
    app->setAttribute(Qt::AA_ForceRasterWidgets, false);
#endif

    app->setOrganizationName("deepin");
    app->setApplicationName("deepin-movie");
    app->setApplicationVersion(DMR_VERSION);
    app->setProductIcon(QIcon::fromTheme("deepin-movie"));
    app->setWindowIcon(QIcon::fromTheme("deepin-movie"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app->setApplicationAcknowledgementPage(acknowledgementLink);
    app->setApplicationVersion(DApplication::buildVersion(VERSION));

    //save theme
    DApplicationSettings saveTheme;

    qInfo() << "log path: " << Dtk::Core::DLogManager::getlogFilePath();
    auto &clm = dmr::CommandLineManager::get();
    QCommandLineOption functionCallOption("functioncall", "AI function call.");
    clm.addOption(functionCallOption);
    clm.process(*app);

    QStringList toOpenFiles;
    if (clm.positionalArguments().length() > 0) {
        toOpenFiles = clm.positionalArguments();
    }

    app->loadTranslator();
    app->setApplicationDisplayName(QObject::tr("Movie"));
    app->setApplicationDescription(QObject::tr(
                                       "Movie is a full-featured video player, supporting playing local and streaming media in multiple video formats."
                                   ));
//    "Deepin Movie is a well-designed and full-featured"
//    " video player with simple borderless design. It supports local and"
//    " streaming media play with multiple video formats."
//    auto light_theme = dmr::Settings::get().internalOption("light_theme").toBool();
//    app.setTheme(light_theme ? "light": "dark");

    if (clm.debug()) {
        Dtk::Core::DLogManager::registerConsoleAppender();
    }
    Dtk::Core::DLogManager::registerFileAppender();

    bool singleton = !dmr::Settings::get().isSet(dmr::Settings::MultipleInstance);
    QString movieName = "";
    if (clm.isSet("functioncall")) {
        movieName = getFunctionMovieName();
    }

    if (singleton && !runSingleInstance()) {
        QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
        if (clm.isSet("functioncall")) {
            if(!movieName.isEmpty()) {
                iface.asyncCall("openFile", movieName);
            }
        }
        qInfo() << "another deepin movie instance has started";
        if (!toOpenFiles.isEmpty()) {
            // QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
            if (toOpenFiles.size() == 1) {
                if (!toOpenFiles[0].contains("QProcess"))
                    iface.asyncCall("openFile", toOpenFiles[0]);
            } else {
                iface.asyncCall("openFiles", toOpenFiles);
            }
        }

        // QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
        if (iface.isValid()) {
            qWarning() << "deepin-movie raise";
            iface.asyncCall("Raise");
        }
        exit(0);
    }

//    app.setWindowIcon(QIcon(":/resources/icons/logo.svg"));
    app->setApplicationDisplayName(QObject::tr("Movie"));
    app->setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);

//    app.setApplicationVersion(DApplication::buildVersion("20190830"));
    MovieConfiguration::get().init();

    QRegExp url_re("\\w+://");

    if (CompositingManager::get().composited()) {
        dmr::MainWindow mw;
        Presenter *presenter = new Presenter(&mw);
    //    mw.setMinimumSize(QSize(1070, 680));
        mw.setPresenter(presenter);
        if (CompositingManager::isPadSystem()) {
            ///平板模式下全屏显示
            mw.showMaximized();
        } else {
            mw.resize(850, 600);
            Dtk::Widget::moveToCenter(&mw);
            mw.show();
        }

        mw.setOpenFiles(toOpenFiles);

        if (!QDBusConnection::sessionBus().isConnected()) {
            qWarning() << "dbus disconnected";
        }

        ApplicationAdaptor adaptor(&mw);
        QDBusConnection::sessionBus().registerService("com.deepin.movie");
        QDBusConnection::sessionBus().registerObject("/", &mw);

        return app->exec();
    } else {
        dmr::Platform_MainWindow platform_mw;
        Presenter *presenter = new Presenter(&platform_mw);
    //    mw.setMinimumSize(QSize(1070, 680));
        platform_mw.setPresenter(presenter);
        if (CompositingManager::isPadSystem()) {
            ///平板模式下全屏显示
            platform_mw.showMaximized();
        } else {
            platform_mw.resize(850, 600);
            Dtk::Widget::moveToCenter(&platform_mw);
            platform_mw.show();
        }

        platform_mw.setOpenFiles(toOpenFiles);

        if (!QDBusConnection::sessionBus().isConnected()) {
            qWarning() << "dbus disconnected";
        }

        Platform_ApplicationAdaptor adaptor(&platform_mw);
        QDBusConnection::sessionBus().registerService("com.deepin.movie");
        QDBusConnection::sessionBus().registerObject("/", &platform_mw);

        return app->exec();
    }

//    if (!toOpenFiles.isEmpty()) {
//        if (toOpenFiles.size() == 1) {
//            mw.play(toOpenFiles[0]);
//        } else {
//            mw.playList(toOpenFiles);
//        }
//    }
}

