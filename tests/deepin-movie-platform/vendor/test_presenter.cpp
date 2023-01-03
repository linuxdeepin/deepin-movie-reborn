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
#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include "application.h"
#include <unistd.h>
#include "src/vendor/presenter.h"
#include "movie_configuration.h"

using namespace dmr;

TEST(Presenter, slotplay)
{
//    MainWindow *w = dApp->getMainWindowWayland();
    MainWindow *w = new MainWindow;
    MovieConfiguration::get().init();
    Presenter *presenter = new Presenter(w);

    presenter->slotopenUrlRequested(QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3"));
    presenter->slotplay();
    presenter->slotpause();
    presenter->slotplaynext();
    presenter->slotplayprev();
    presenter->slotvolumeRequested(1.5);
    presenter->slotstateChanged();
    presenter->slotvolumeChanged();


    presenter->slotseek(qlonglong(200));

    presenter->slotloopStatusRequested(Mpris::LoopStatus::None);

    presenter->slotloopStatusRequested(Mpris::LoopStatus::Track);

    presenter->slotloopStatusRequested(Mpris::LoopStatus::Playlist);

    presenter->slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);

    presenter->slotplayModeChanged(PlaylistModel::PlayMode::OrderPlay);

    presenter->slotplayModeChanged(PlaylistModel::PlayMode::SingleLoop);

    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ListLoop);

    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ShufflePlay);

    presenter->slotstop();

    presenter->deleteLater();
    presenter = nullptr;

//    w->close();
//    w->deleteLater();
//    w = nullptr;
}

//TEST(Presenter, slotloopStatusRequested)
//{
////    Presenter *presenter = dApp->getPresenter();
//    MainWindow w;
////    auto &mc = MovieConfiguration::get();
//    MovieConfiguration::get().init();
////    PlayerEngine *engine =  w->engine();
//    Presenter *presenter = new Presenter(&w);

////    QTest::qWait(500);
//    presenter->slotloopStatusRequested(Mpris::LoopStatus::None);
////    QTest::qWait(500);
//    presenter->slotloopStatusRequested(Mpris::LoopStatus::Track);
////    QTest::qWait(500);
//    presenter->slotloopStatusRequested(Mpris::LoopStatus::Playlist);
////    QTest::qWait(500);
//    presenter->slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);
//}

//TEST(Presenter, slotplayModeChanged)
//{
////    Presenter *presenter = dApp->getPresenter();
//    MainWindow w;
////    auto &mc = MovieConfiguration::get();
//    MovieConfiguration::get().init();
////    PlayerEngine *engine =  w->engine();
//    Presenter *presenter = new Presenter(&w);

////    QTest::qWait(500);
//    presenter->slotplayModeChanged(PlaylistModel::PlayMode::OrderPlay);
////    QTest::qWait(500);
//    presenter->slotplayModeChanged(PlaylistModel::PlayMode::SingleLoop);
////    QTest::qWait(500);
//    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ListLoop);
////    QTest::qWait(500);
//    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ShufflePlay);
//}
