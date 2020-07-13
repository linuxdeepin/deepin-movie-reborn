/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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

DWIDGET_USE_NAMESPACE


int main(int argc, char *argv[])
{
    qputenv("LD_LIBRARY_PATH", "/usr/local/lib:/usr/local/lib/aarch64-linux-gnu/");
//    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/local/plugins/platforms:/usr/local/lib/aarch64-linux-gnu/plugins/platforms");
//    qputenv("QT_PLUGIN_PATH", "/usr/local/plugins:/usr/local/lib/aarch64-linux-gnu/plugins/");
//    qputenv("QML2_IMPORT_PATH", "/usr/local/qml:/usr/local/lib/aarch64-linux-gnu/qml:/usr/lib/aarch64-linux-gnu/qt5/qml");
//    qputenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
//    qputenv("XDG_CONFIG_DIRS", "/usr/local/etc/xdg:/etc/xdg");
//    qputenv("QT_QPA_PLATFORM", "wayland");
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell-v6");
    qputenv("_d_disableDBusFileDialog", "true");

//    CompositingManager::detectOpenGLEarly();
//    CompositingManager::detectPciID();

#if defined(STATIC_LIB)
    DWIDGET_INIT_RESOURCE();
#endif

    DApplication app(argc, argv);

    //DApplication::loadDXcbPlugin();

    // required by mpv
    setlocale(LC_NUMERIC, "C");

    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
    // overwrite DApplication default value
    app.setAttribute(Qt::AA_ForceRasterWidgets, false);
    app.setOrganizationName("deepin");
    app.setApplicationName("deepin-movie");
    app.setApplicationVersion(DMR_VERSION);
    app.setProductIcon(utils::LoadHiDPIPixmap(":/resources/icons/logo-big.svg"));
    app.setWindowIcon(QIcon(":/resources/icons/logo-big.svg"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app.setApplicationAcknowledgementPage(acknowledgementLink);

    //save theme
    DApplicationSettings saveTheme;
    auto &clm = dmr::CommandLineManager::get();
    clm.process(app);

    QStringList toOpenFiles;
    if (clm.positionalArguments().length() > 0) {
        toOpenFiles = clm.positionalArguments();
    }

    app.loadTranslator();
    app.setApplicationDisplayName(QObject::tr("Movie"));
    app.setApplicationDescription(QObject::tr(
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
    qDebug() << "log path: " << Dtk::Core::DLogManager::getlogFilePath();

    bool singleton = !dmr::Settings::get().isSet(dmr::Settings::MultipleInstance);

    if (QString(argv[argc - 1]) != "QProcess") {
        QString t_argv = QString(argv[0]) + " ";
        if (argc > 1) {
            for (int i = 1; i < argc; i++) {
                t_argv += argv[i];
                t_argv += " ";
            }
        }

        t_argv += "QProcess";

        QProcess *process = new QProcess(0);
        /*QStringList env = QProcess::systemEnvironment();
        env<<"LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/aarch64-linux-gnu/"
        <<"QT_QPA_PLATFORM_PLUGIN_PATH=/usr/local/plugins/platforms:/usr/local/lib/aarch64-linux-gnu/plugins/platforms"
        <<"QT_PLUGIN_PATH=/usr/local/plugins:/usr/local/lib/aarch64-linux-gnu/plugins/"
        <<"QML2_IMPORT_PATH=/usr/local/qml:/usr/local/lib/aarch64-linux-gnu/qml:/usr/lib/aarch64-linux-gnu/qt5/qml"
        <<"XDG_DATA_DIRS=/usr/local/share:/usr/share"
        <<"XDG_CONFIG_DIRS=/usr/local/etc/xdg:/etc/xdg"
        <<"QT_QPA_PLATFORM=wayland"
        <<"QT_WAYLAND_SHELL_INTEGRATION=xdg-shell-v6";*/
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("LD_LIBRARY_PATH", "/usr/local/lib:/usr/local/lib/aarch64-linux-gnu/");
        env.insert("QT_QPA_PLATFORM_PLUGIN_PATH", "/usr/local/plugins/platforms:/usr/local/lib/aarch64-linux-gnu/plugins/platforms");
        env.insert("QT_PLUGIN_PATH", "/usr/local/plugins:/usr/local/lib/aarch64-linux-gnu/plugins/");
        env.insert("QML2_IMPORT_PATH", "/usr/local/qml:/usr/local/lib/aarch64-linux-gnu/qml:/usr/lib/aarch64-linux-gnu/qt5/qml");
        env.insert("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
        env.insert("XDG_CONFIG_DIRS", "/usr/local/etc/xdg:/etc/xdg");
        env.insert("QT_QPA_PLATFORM", "wayland");
        env.insert("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell-v6");
        //process->setEnvironment(env);
        //process->setNativeArguments("");
        process->setProcessEnvironment(env);
        process->startDetached(t_argv);
        process->deleteLater();

        qDebug() << t_argv;
        return 0;
    }

    QSharedMemory shared_memory("deepinmovie");

    if (shared_memory.attach()) {
        shared_memory.detach();
    }

    if (singleton && !shared_memory.create(1)) {
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


//    app.setWindowIcon(QIcon(":/resources/icons/logo.svg"));
    app.setApplicationDisplayName(QObject::tr("Movie"));
    app.setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);

//    app.setApplicationVersion(DApplication::buildVersion("20190830"));
    app.setApplicationVersion(DApplication::buildVersion(VERSION));
    MovieConfiguration::get().init();

    QRegExp url_re("\\w+://");

    dmr::MainWindow mw;
    Presenter *presenter = new Presenter(&mw);
//    mw.setMinimumSize(QSize(1070, 680));
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
        if (toOpenFiles.size() == 1) {
            mw.play(toOpenFiles[0]);
        } else {
            mw.playList(toOpenFiles);
        }
    }
    return app.exec();

}

