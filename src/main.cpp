/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
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
#include "dbus_adpator.h"
#include "compositing_manager.h"
#include "utils.h"
#include "movie_configuration.h"
#include "vendor/movieapp.h"
#include "vendor/presenter.h"
#include <QSettings>


#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <va/va_x11.h>

#include "accessibility/acobjectlist.h"
DWIDGET_USE_NAMESPACE

bool runSingleInstance()
{
    QString userName = QDir::homePath().section("/", -1, -1);
    std::string path = ("/home/" + userName + "/.cache/deepin/deepin-movie/").toStdString();
    QDir tdir(path.c_str());
    if (!tdir.exists()) {
        tdir.mkpath(path.c_str());
    }

    path += "single";
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
#ifdef __aarch64__
    if (dmr::utils::first_check_wayland_env()) {
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "kwayland-shell");
        //qputenv("_d_disableDBusFileDialog", "true");
        setenv("PULSE_PROP_media.role", "video", 1);
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setDefaultFormat(format);
    }
#endif
#ifdef __mips__
    if (CompositingManager::get().composited()) {
        CompositingManager::detectOpenGLEarly();
        CompositingManager::detectPciID();
    }
#endif
#if defined(STATIC_LIB)
    DWIDGET_INIT_RESOURCE();
#endif
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
    app->setProductIcon(utils::LoadHiDPIPixmap(":/resources/icons/logo-big.svg"));
    app->setWindowIcon(QIcon(":/resources/icons/logo-big.svg"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app->setApplicationAcknowledgementPage(acknowledgementLink);

    //save theme
    DApplicationSettings saveTheme;

    qInfo() << "log path: " << Dtk::Core::DLogManager::getlogFilePath();
    auto &clm = dmr::CommandLineManager::get();
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

    if (singleton && !runSingleInstance()) {
        qInfo() << "another deepin movie instance has started";
        if (!toOpenFiles.isEmpty()) {
            QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
            if (toOpenFiles.size() == 1) {
                if (!toOpenFiles[0].contains("QProcess"))
                    iface.asyncCall("openFile", toOpenFiles[0]);
            } else {
                iface.asyncCall("openFiles", toOpenFiles);
            }
        }

        QDBusInterface iface("com.deepin.movie", "/", "com.deepin.movie");
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
    app->setApplicationVersion(DApplication::buildVersion(VERSION));
    MovieConfiguration::get().init();

    QRegExp url_re("\\w+://");

    dmr::MainWindow mw;
    Presenter *presenter = new Presenter(&mw);
//    mw.setMinimumSize(QSize(1070, 680));
    mw.setPresenter(presenter);
    if (CompositingManager::isPadSystem()) {
        ///平板模式下全屏显示
        mw.showMaximized();
    } else {
        mw.resize(850, 600);
        utils::MoveToCenter(&mw);
        mw.show();
    }

    mw.setOpenFiles(toOpenFiles);

    if (!QDBusConnection::sessionBus().isConnected()) {
        qWarning() << "dbus disconnected";
    }

    ApplicationAdaptor adaptor(&mw);
    QDBusConnection::sessionBus().registerService("com.deepin.movie");
    QDBusConnection::sessionBus().registerObject("/", &mw);

//    if (!toOpenFiles.isEmpty()) {
//        if (toOpenFiles.size() == 1) {
//            mw.play(toOpenFiles[0]);
//        } else {
//            mw.playList(toOpenFiles);
//        }
//    }
    return app->exec();

}

