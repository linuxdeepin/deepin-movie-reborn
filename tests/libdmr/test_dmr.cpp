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
#include <QTimer>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>
#include <QWidget>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "player_widget.h"
#include "player_engine.h"
#include "compositing_manager.h"
#include "movie_configuration.h"

TEST(libdmr, libdmrTest)
{
    using namespace dmr;
    PlayerWidget *player = new PlayerWidget();
    player->engine().changeVolume(120);
    player->play(QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4"));

    auto &mc = MovieConfiguration::get();
    mc.updateUrl(QUrl("movie1"), "sub-delay", -2.5);
    mc.updateUrl(QUrl("movie1"), "sub-delay", 1.5);
    mc.updateUrl(QUrl("movie2"), "sub-delay", 1.0);
    mc.updateUrl(QUrl("movie1"), "volume", 20);

    auto res = mc.queryByUrl(QUrl("movie1"));
    mc.removeUrl(QUrl("movie1"));
    mc.updateUrl(QUrl("movie1"), "volume", 30);
    mc.updateUrl(QUrl("movie2"), "volume", 40);
    res = mc.queryByUrl(QUrl("movie1"));
    mc.clear();

    QTest::qWait(100);
    delete player;
}

TEST(libdmr, utils)
{
    using namespace dmr;

    MainWindow* w = dApp->getMainWindow();

    bool ret = utils::CompareNames("/data/source/deepin-movie-reborn/movie/demo.mp4", "/data/source/deepin-movie-reborn/movie/demo.mp3");
    ret = utils::CompareNames("/data/source/deepin-movie-reborn/movie/demo.mp4", "/data/source/deepin-movie-reborn//movie/demo.mp4");
    //QCOMPARE(ret, true);
    utils::UnInhibitPower(20);
    utils::MoveToCenter(w);
    utils::Time2str(90000);
    utils::ValidateScreenshotPath(QString("/data/source/deepin-movie-reborn"));
    utils::ValidateScreenshotPath(QString("~/uos"));
    utils::MakeRoundedPixmap(QPixmap("/data/source/deepin-movie-reborn/test.jpg"), 10, 10);
    utils::MakeRoundedPixmap(QSize(20, 30), QPixmap("/data/source/deepin-movie-reborn/test.jpg"), 10, 10, 20);
    utils::ShowInFileManager("/data/source/deepin-movie-reborn/movie/demo.mp4");
    utils::ShowInFileManager("/data/source/deepin-movie-reborn/test");  //path is not exist
}

TEST(libdmr, playlistModel)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();
    engine->playlist().savePlaylist();
    engine->playlist().clearPlaylist();

    QUrl url = QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4");
    bool is = false;
    engine->playlist().getMovieInfo(url, &is);
    engine->playlist().getMovieCover(url);
}

TEST(libdmr, compositingManager)
{
    if (CompositingManager::get().composited()) {
        CompositingManager::detectOpenGLEarly();
        CompositingManager::detectPciID();
    }
    bool run = CompositingManager::get().runningOnNvidia();
    qDebug() << __func__ << "isRunningOnNvidia:  " << run ;
    run = CompositingManager::get().runningOnVmwgfx();
    qDebug() << __func__ << "isRunningOnVmwgfx:  " << run ;
    CompositingManager::get().setTestFlag(CompositingManager::get().isTestFlag());
}

TEST(libdmr, movieConfiguration)
{
    MovieConfiguration::get().removeFromListUrl(
                QUrl("/data/source/deepin-movie-reborn/Hachiko.A.Dog's.Story.ass"),
                ConfigKnownKey::ExternalSubs, QString());
}

