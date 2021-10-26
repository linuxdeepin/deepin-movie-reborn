/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
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
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>
#include <QDir>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "dmr_settings.h"
#include "utils.h"
#include "movie_configuration.h"
#include "dbus_adpator.h"

using namespace dmr;
using namespace utils;

TEST(Wayland, wayland)
{
//    QTest::qWait(500);

    QString homePath = QDir::homePath();
    QStringList command;
    command << "-rf" << QString("%1/.config/deepin-movie-test").arg(homePath);
    QProcess::execute("rm", command);
    MainWindow *w_wayland = dApp->getMainWindowWayland();
    auto &mc = MovieConfiguration::get();
    MovieConfiguration::get().init();
    PlayerEngine *engine =  w_wayland->engine();
    ToolboxProxy *toolboxProxy = w_wayland->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();
    ToolButton *fsBtn = toolboxProxy->fsBtn();
    ButtonBoxButton* playBtn = static_cast<ButtonBoxButton *>(toolboxProxy->playBtn());
    ButtonBoxButton* nextBtn = static_cast<ButtonBoxButton *>(toolboxProxy->nextBtn());
    ButtonBoxButton* prevBtn = static_cast<ButtonBoxButton *>(toolboxProxy->prevBtn());
    QList<QUrl> listPlayFiles;

//    QTest::qWait(200);
    w_wayland->resize(850, 600);
    w_wayland->show();

//    QTest::qWait(800);

    listPlayFiles << QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4")\
                  << QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3");
    const auto &valids = engine->addPlayFiles(listPlayFiles);


#if !defined (__mips__ ) && !defined(__aarch64__)
    Settings::get().settings()->setOption("base.play.showInthumbnailmode", true);
    qDebug() << __func__ << Settings::get().settings()->option("base.play.showInthumbnailmode");
#endif

    Settings::get().settings()->setOption("base.play.resumelast", false);
    qDebug() << Settings::get().settings()->option("base.play.resumelast");

    if(!valids.empty()){
//        QTest::mouseMove(playBtn, QPoint(), 100);
//        QTest::mouseClick(playBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play

//        QTest::qWait(200);
        const auto &valids = engine->addPlayFiles(listPlayFiles);
        engine->playByName(valids[0]);

        w_wayland->requestAction(ActionFactory::ActionKind::ToggleMute);
//        QTest::qWait(200);
        QTest::mouseMove(fsBtn, QPoint(), 200);
        QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
        QTest::mouseMove(listBtn, QPoint(), 200);
        QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier,QPoint(), 500);
        fsBtn->showToolTip();

        QTest::mouseMove(nextBtn, QPoint(), 200);
        QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play next
//        QTest::qWait(200);
//    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::DarkType);
//    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::DarkType);
        QTest::mouseMove(prevBtn, QPoint(), 200);
        QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play prev

        QTest::mouseMove(listBtn, QPoint(), 200);
        QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
        QTest::mouseMove(fsBtn, QPoint(), 200);
        QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500);

        Settings::get().settings()->setOption("base.play.emptylist", false);
    }
    ApplicationAdaptor *appAdaptor = new ApplicationAdaptor(w_wayland);
    appAdaptor->Raise();
    appAdaptor->openFile("/data/source/deepin-movie-reborn/movie/demo.mp4");
    QStringList fileList;
    fileList << "/data/source/deepin-movie-reborn/movie/demo.mp4"\
             <<"/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3";
    appAdaptor->openFiles(fileList);


    QEvent enterEvent(QEvent::Enter);
    QEvent leaveEvent(QEvent::Leave);
    QApplication::sendEvent(playBtn, &enterEvent);
    QApplication::sendEvent(playBtn, &leaveEvent);
    QApplication::sendEvent(fsBtn, &enterEvent);
    QApplication::sendEvent(fsBtn, &leaveEvent);


#if !defined (__mips__ ) && !defined(__aarch64__)
    Settings::get().settings()->setOption("base.play.showInthumbnailmode", false);
#endif
    Settings::get().settings()->setOption("base.play.resumelast", true);
//    w_wayland->close();
    QTest::qWait(100);
    delete w_wayland;
    w_wayland = nullptr;
}
