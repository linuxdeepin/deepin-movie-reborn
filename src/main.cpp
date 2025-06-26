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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DApplicationSettings>
#endif

#include <qsettingbackend.h>

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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QX11Info>
#else
#include <QtGui/private/qtx11extras_p.h>
#include <QtGui/private/qtguiglobal_p.h>
#endif

DWIDGET_USE_NAMESPACE

bool runSingleInstance()
{
    qDebug() << "runSingleInstance";
    std::string path;
    QString userName = QDir::homePath().section("/", -1, -1);
    if (userName == "root") {
        path = "/tmp/deepin-movie";
        qDebug() << "user is root, use /tmp/deepin-movie";
    } else {
        path = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString();
        qDebug() << "user is not root, use " << path;
    }
    QDir tdir(path.c_str());
    if (!tdir.exists()) {
        qDebug() << "tdir.mkpath";
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
    qDebug() << "runSingleInstance end";
    return true;
}

#ifdef __x86_64__
//进程检测是否支持硬解
void checkIsCanHwdec(int argc, char *argv[])
{
    qDebug() << "checkIsCanHwdec";
    QApplication a(argc, argv);
    Display *x11=QX11Info::display();
    VADisplay *display = (VADisplay *)vaGetDisplay(x11);
    int major, minor;
    int status = 0;
    try {
        status = vaInitialize(display, &major, &minor);
    }
    catch (...) {
        qDebug() << "catch";
        status = -1;
    }
    qDebug() << "checkIsCanHwdec end";
    exit(status);
}
#endif
//UOS_AI调用函数
QString getFunctionMovieName()
{
    qDebug() << "getFunctionMovieName";
    QString movieName = "";
    qInfo() << "single uos ai function call";
    bool isCopilotConnected = false;
    QDBusInterface aiDbus("com.deepin.copilot", "/com/deepin/copilot", "com.deepin.copilot", QDBusConnection::sessionBus());
    if (aiDbus.isValid()) {
        qDebug() << "aiDbus.isValid()";
        isCopilotConnected = true;
    }
    if(isCopilotConnected) {
        qDebug() << "isCopilotConnected";
        QDBusReply<QString> functions = aiDbus.call("cachedFunctions");
        QJsonDocument jsonDocu = QJsonDocument::fromJson(functions.value().toUtf8());
        qInfo() << "UOS_AI jsonDocu is: " << jsonDocu;
        if (jsonDocu.isObject()) {
            QJsonObject objRoot = jsonDocu.object();
            qDebug() << "objRoot";
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
    qDebug() << "getFunctionMovieName end";
    return movieName;
}

void killOldMovie()
{
    qInfo() << "kill old movie process";
    QString processName = "deepin-movie";

    QProcess psProcess;
    psProcess.start("bash", QStringList() << "-c" << "ps -eo pid,lstart,cmd | grep deepin-movie");
    psProcess.waitForFinished();
    QString output = psProcess.readAllStandardOutput();

    QStringList lines = output.split("\n");
    QStringList earlierProcessPids;
    QDateTime earliestStartTime;

    for (const QString &line : lines) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
#else
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#endif
        if (parts.size() < 3) continue;
        if (!parts[6].startsWith("deepin-movie")) continue;

        int pid = parts[0].toInt();
        if(QCoreApplication::applicationPid() == pid) continue;
        QString time;
//        for (int i = 1; i < 6; i++) {
//            time += parts[i];
//            time += " ";
//        }
        time = parts[3] + " " + parts[4];
        QDateTime startTime = QDateTime::fromString(time, "dd HH:mm:ss");

        if (earlierProcessPids.isEmpty() || startTime < earliestStartTime) {
            earlierProcessPids.clear();
            earlierProcessPids << QString::number(pid);
            earliestStartTime = startTime;
        }
    }

    // 杀死较早启动的进程
    for (const QString &pid : earlierProcessPids) {
        QProcess killProcess;
        killProcess.start("kill", QStringList() << pid);
        killProcess.waitForFinished();
        qInfo() << "Killed process with PID:" << pid;
    }
    qInfo() << "Kill old movie process end";
}

int main(int argc, char *argv[])
{
    qInfo() << "Starting deepin-movie application";
    
    // Task 326583 不参与合成器崩溃重连
    unsetenv("QT_WAYLAND_RECONNECT");
    qDebug() << "Unset QT_WAYLAND_RECONNECT environment variable.";

    //for qt5platform-plugins load DPlatformIntegration or DPlatformIntegrationParent
    if (!QString(qgetenv("XDG_CURRENT_DESKTOP")).toLower().startsWith("deepin")){
        setenv("XDG_CURRENT_DESKTOP", "Deepin", 1);
        qDebug() << "XDG_CURRENT_DESKTOP not Deepin, setting to Deepin.";
    } else {
        qDebug() << "XDG_CURRENT_DESKTOP already starts with Deepin.";
    }

#ifdef __x86_64__
    qDebug() << "__x86_64__ defined.";
    if(argc==2 && strcmp(argv[1],"hwdec") == 0) {
        qDebug() << "Hardware decoding check requested with argc:" << argc << ", argv[1]:" << argv[1];
        checkIsCanHwdec(argc, argv);
        qDebug() << "checkIsCanHwdec() completed.";
    } else {
        qDebug() << "Hardware decoding check not requested or invalid arguments.";
    }
#else
    qDebug() << "__x86_64__ not defined.";
#endif
//#ifdef __aarch64__ //wayland平台支持影院播放
    if (dmr::utils::first_check_wayland_env()) {
        qInfo() << "Running in Wayland environment";
        qDebug() << "Wayland environment detected.";
        //qputenv("_d_disableDBusFileDialog", "true");
        setenv("PULSE_PROP_media.role", "video", 1);
        qDebug() << "Set PULSE_PROP_media.role to video.";
#ifndef __x86_64__
        qDebug() << "Non-__x86_64__ platform - setting OpenGLES renderable type.";
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setDefaultFormat(format);
        qDebug() << "Set OpenGLES as renderable type for non-x86_64 platform";
#else
        qDebug() << "__x86_64__ platform - OpenGLES renderable type not set.";
#endif
    } else {
        qDebug() << "Not running in Wayland environment.";
    }
//#endif
#ifdef __mips__
    qDebug() << "__mips__ defined.";
    if (CompositingManager::get().composited()) {
        qDebug() << "Composited manager detected on MIPS.";
        CompositingManager::detectOpenGLEarly();
        CompositingManager::detectPciID();
    } else {
        qDebug() << "Composited manager not detected on MIPS.";
    }
#else
    qDebug() << "__mips__ not defined.";
#endif
#if defined(STATIC_LIB)
    qDebug() << "STATIC_LIB defined - initializing DWidget resource.";
    DWIDGET_INIT_RESOURCE();
#else
    qDebug() << "STATIC_LIB not defined.";
#endif
    QFileInfo jmfi("/dev/jmgpu");
    if (jmfi.exists()) {
        qDebug() << "JM GPU device detected.";
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
        qDebug() << "Set QT_XCB_GL_INTEGRATION to xcb_egl for JM GPU";
    } else {
        qDebug() << "JM GPU device not detected.";
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
    qDebug() << "DTK version < 5.4.0.0 - creating new DApplication.";
    app = new DApplication(argc, argv);
#else
    qDebug() << "DTK version >= 5.4.0.0 - getting global DApplication.";
    app = DApplication::globalApplication(argc, argv);
#endif

    QAccessible::installFactory(accessibleFactory);
    qDebug() << "Accessible factory installed.";
    // required by mpv
    setlocale(LC_NUMERIC, "C");
    qDebug() << "Locale set to C.";

#ifdef __mips__
    qDebug() << "__mips__ defined - setting DApplication attributes.";
    if (CompositingManager::get().composited()) {
        qDebug() << "Composited manager detected on MIPS - setting attributes.";
        app->setAttribute(Qt::AA_UseHighDpiPixmaps);
        // overwrite DApplication default value
        app->setAttribute(Qt::AA_ForceRasterWidgets, false);
    } else {
        qDebug() << "Composited manager not detected on MIPS.";
    }
#else
    qDebug() << "__mips__ not defined - setting DApplication attributes.";
    app->setAttribute(Qt::AA_UseHighDpiPixmaps);
    // overwrite DApplication default value
    app->setAttribute(Qt::AA_ForceRasterWidgets, false);
#endif
    qDebug() << "DApplication attributes set.";

    app->setOrganizationName("deepin");
    app->setApplicationName("deepin-movie");
    app->setApplicationVersion(DMR_VERSION);
    qInfo() << "Application initialized with version:" << DMR_VERSION;
    app->setProductIcon(QIcon::fromTheme("deepin-movie"));
    app->setWindowIcon(QIcon::fromTheme("deepin-movie"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app->setApplicationAcknowledgementPage(acknowledgementLink);
    app->setApplicationVersion(DApplication::buildVersion(VERSION));
    qDebug() << "Application metadata set.";

    //save theme
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "Qt version < 6.0.0 - DApplicationSettings for theme saving.";
    // qt6中自动设置了，如果不想要记住主题的话， DGuiApplicationHelper：：setAttribute（DontSaveApplicationTheme）
    DApplicationSettings saveTheme;
#else
    qDebug() << "Qt version >= 6.0.0 - theme saving handled automatically.";
#endif

    qInfo() << "log path: " << Dtk::Core::DLogManager::getlogFilePath();
    auto &clm = dmr::CommandLineManager::get();
    QCommandLineOption functionCallOption("functioncall", "AI function call.");
    QCommandLineOption restartCallOption("restart", "deepin movie restart");
    clm.addOption(functionCallOption);
    clm.addOption(restartCallOption);
    clm.process(*app);
    qDebug() << "Command line options processed.";

    QStringList toOpenFiles;
    if (clm.positionalArguments().length() > 0) {
        toOpenFiles = clm.positionalArguments();
        qDebug() << "Positional arguments found:" << toOpenFiles;
    } else {
        qDebug() << "No positional arguments found.";
    }

    app->loadTranslator();
    app->setApplicationDisplayName(QObject::tr("Movie"));
    app->setApplicationDescription(QObject::tr(
                                       "Movie is a full-featured video player, supporting playing local and streaming media in multiple video formats."
                                   ));
    qDebug() << "Translator loaded and application display name/description set.";

//    "Deepin Movie is a well-designed and full-featured"
//    " video player with simple borderless design. It supports local and"
//    " streaming media play with multiple video formats."
//    auto light_theme = dmr::Settings::get().internalOption("light_theme").toBool();
//    app.setTheme(light_theme ? "light": "dark");

    if (clm.debug()) {
        Dtk::Core::DLogManager::registerConsoleAppender();
        qInfo() << "Debug mode enabled - console logging activated";
    }
    Dtk::Core::DLogManager::registerFileAppender();
    qInfo() << "File logging initialized at:" << Dtk::Core::DLogManager::getlogFilePath();

    bool singleton = !dmr::Settings::get().isSet(dmr::Settings::MultipleInstance);
    qDebug() << "Singleton mode:" << (singleton ? "enabled" : "disabled");
    QString movieName = "";
    if (clm.isSet("functioncall")) {
        qDebug() << "Function call option is set.";
        movieName = getFunctionMovieName();
    }

    if (singleton && !runSingleInstance()) {
        qDebug() << "Singleton mode enabled and another instance running.";
        if (clm.isSet("restart")) {
            qInfo() << "Restart requested - waiting for old instance to terminate";
            qDebug() << "Restart option is set.";
            sleep(2);
            qDebug() << "Waited for 2 seconds.";
            if (!runSingleInstance()) {
                qWarning() << "Failed to acquire single instance lock - killing old instance";
                qDebug() << "Failed to acquire single instance lock during restart, killing old movie.";
                killOldMovie();
            } else {
                qDebug() << "Successfully acquired single instance lock after restart wait.";
            }
        } else {
            qDebug() << "Restart option not set - forwarding request to existing instance.";
            QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
            if (clm.isSet("functioncall")) {
                qDebug() << "Function call option set for existing instance.";
                if(!movieName.isEmpty()) {
                    qInfo() << "Opening file from AI function call:" << movieName;
                    iface.asyncCall("openFile", movieName);
                    qDebug() << "Sent openFile D-Bus call for AI function call.";
                } else {
                    qDebug() << "Movie name is empty for AI function call.";
                }
            } else {
                qDebug() << "Function call option not set for existing instance.";
            }
            qInfo() << "Another instance detected - forwarding request";
            if (!toOpenFiles.isEmpty()) {
                qDebug() << "Files to open for existing instance:" << toOpenFiles;
                // QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
                if (toOpenFiles.size() == 1) {
                    qDebug() << "Single file to open for existing instance.";
                    if (!toOpenFiles[0].contains("QProcess")) {
                        iface.asyncCall("openFile", toOpenFiles[0]);
                        qDebug() << "Sent openFile D-Bus call for single file:" << toOpenFiles[0];
                    } else {
                        qDebug() << "Skipping QProcess related file:" << toOpenFiles[0];
                    }
                } else {
                    qDebug() << "Multiple files to open for existing instance.";
                    iface.asyncCall("openFiles", toOpenFiles);
                    qDebug() << "Sent openFiles D-Bus call for multiple files.";
                }
            } else {
                qDebug() << "No files to open for existing instance.";
            }

            // QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
            if (iface.isValid()) {
                qWarning() << "deepin-movie raise";
                iface.asyncCall("Raise");
                qDebug() << "Sent Raise D-Bus call.";
            }
            exit(0);
        }
    } else {
        qDebug() << "Singleton mode disabled or no other instance running.";
        if (clm.isSet("functioncall")) {
            qInfo() << "Scheduling AI function call file open:" << movieName;
            qDebug() << "Function call option set for new instance.";
            QTimer::singleShot(2000, [=]() {
                qDebug() << "Executing singleShot timer for AI function call file open.";
                QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
                if(!movieName.isEmpty()) {
                    iface.asyncCall("openFile", movieName);
                    qDebug() << "Sent openFile D-Bus call from singleShot for AI function call.";
                } else {
                    qDebug() << "Movie name is empty in singleShot for AI function call.";
                }
                qDebug() << "Finished singleShot timer for AI function call file open.";
            });
        } else {
            qDebug() << "Function call option not set for new instance.";
        }
    }

//    app.setWindowIcon(QIcon(":/resources/icons/logo.svg"));
    app->setApplicationDisplayName(QObject::tr("Movie"));
    app->setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);
    qDebug() << "Application display name and attributes set.";

//    app.setApplicationVersion(DApplication::buildVersion("20190830"));
    MovieConfiguration::get().init();
    qDebug() << "MovieConfiguration initialized.";

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "Qt version < 6.0.0 - using QRegExp for URL regex.";
    QRegExp url_re("\\w+://");
#else
    qDebug() << "Qt version >= 6.0.0 - using QRegularExpression for URL regex.";
    QRegularExpression url_re("\\w+://");
#endif
    qDebug() << "URL regex initialized.";

    if (CompositingManager::get().composited()) {
        qInfo() << "Using composited window manager";
        dmr::MainWindow mw;
        qDebug() << "MainWindow created.";
        Presenter *presenter = new Presenter(&mw);
        mw.setPresenter(presenter);
        qDebug() << "Presenter set for MainWindow.";
        if (CompositingManager::isPadSystem()) {
            qInfo() << "Running in tablet mode - maximizing window";
            mw.showMaximized();
        } else {
            qDebug() << "Setting default window size: 850x600";
            mw.resize(850, 600);
            Dtk::Widget::moveToCenter(&mw);
            mw.show();
        }

        mw.setOpenFiles(toOpenFiles);

        if (!QDBusConnection::sessionBus().isConnected()) {
            qCritical() << "Failed to connect to D-Bus session bus";
        } else {
            qInfo() << "D-Bus service registered successfully";
        }

        ApplicationAdaptor adaptor(&mw);
        QDBusConnection::sessionBus().registerService("com.deepin.movie");
        QDBusConnection::sessionBus().registerObject("/", &mw);

        int ret = app->exec();
        if (ret == 2)
            execv( app->applicationFilePath().toUtf8().data(), nullptr);

        qInfo() << "Application exit code:" << ret;
        return ret;
    } else {
        qInfo() << "Using non-composited window manager";
        dmr::Platform_MainWindow platform_mw;
        Presenter *presenter = new Presenter(&platform_mw);
        platform_mw.setPresenter(presenter);
        if (CompositingManager::isPadSystem()) {
            qInfo() << "Running in tablet mode - maximizing window";
            ///平板模式下全屏显示
            platform_mw.showMaximized();
        } else {
            qInfo() << "Running in desktop mode - setting default window size: 850x600";
            platform_mw.resize(850, 600);
            Dtk::Widget::moveToCenter(&platform_mw);
            platform_mw.show();
        }

        platform_mw.setOpenFiles(toOpenFiles);

        if (!QDBusConnection::sessionBus().isConnected()) {
            qCritical() << "Failed to connect to D-Bus session bus";
        } else {
            qInfo() << "D-Bus service registered successfully";
        }

        Platform_ApplicationAdaptor adaptor(&platform_mw);
        QDBusConnection::sessionBus().registerService("com.deepin.movie");
        QDBusConnection::sessionBus().registerObject("/", &platform_mw);
        int ret = app->exec();
        if (ret == 2)
            execv( app->applicationFilePath().toUtf8().data(), nullptr);

        qInfo() << "Application exit code:" << ret;
        return ret;
    }
}

