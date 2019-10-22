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
#include "environments.h"

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

    app.setAttribute(Qt::AA_UseHighDpiPixmaps);
    // overwrite DApplication default value
    app.setAttribute(Qt::AA_ForceRasterWidgets, false);
    app.setOrganizationName("deepin");
    app.setApplicationName("deepin-movie");
    app.setApplicationVersion(DMR_VERSION);
    app.setProductIcon(QPixmap(":/resources/icons/logo-big.svg"));
    app.setWindowIcon(QIcon(":/resources/icons/logo-big.svg"));
    QString acknowledgementLink = "https://www.deepin.org/acknowledgments/deepin-movie";
    app.setApplicationAcknowledgementPage(acknowledgementLink);

    //save theme
    DApplicationSettings saveTheme;
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

//    auto light_theme = dmr::Settings::get().internalOption("light_theme").toBool();
//    app.setTheme(light_theme ? "light": "dark");

    if (clm.debug()) {
        Dtk::Core::DLogManager::registerConsoleAppender();
    }
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
//    app.setApplicationVersion(DApplication::buildVersion("20190830"));
    app.setApplicationVersion(DApplication::buildVersion(VERSION));
    MovieConfiguration::get().init();

    QRegExp url_re("\\w+://");

    dmr::MainWindow mw;
    mw.setMinimumSize(QSize(1070, 680));
    mw.resize(1280, 720);
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

