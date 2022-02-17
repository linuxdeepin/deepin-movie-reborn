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
#include "src/vendor/movieapp.h"
#include "movie_configuration.h"

using namespace dmr;

#ifndef __mips__
TEST(MovieApp, testMprisapp)
{
    Platform_MainWindow* w = new Platform_MainWindow;
    auto &mc = MovieConfiguration::get();
    MovieConfiguration::get().init();
    PlayerEngine *engine =  w->engine();
    Platform_ToolboxProxy *toolboxProxy = w->toolbox();

    w->resize(850, 600);
    utils::MoveToCenter(w);
    w->show();

//    QTest::qWait(500);
    Platform_Settings::get().settings()->setOption("base.play.emptylist", true); //退出时清空播放列表

    MovieApp *movieapp = new MovieApp(w);
    movieapp->initMpris("movie");
    movieapp->show();

//    QTest::qWait(300);
    movieapp->quit();
    movieapp->deleteLater();
    movieapp = nullptr;

    w->deleteLater();
    w = nullptr;

//    QTimer::singleShot(1000,[=]{movieapp->show();});
//    QTest::qWait(300);
//    w->testMprisapp();
}
#endif
