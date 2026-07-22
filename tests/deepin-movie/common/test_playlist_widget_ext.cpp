// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <DGuiApplicationHelper>
#include <gtest/gtest.h>

#include "application.h"
#include "src/common/mainwindow.h"
#include "src/widgets/playlist_widget.h"
#include "src/libdmr/player_engine.h"

using namespace dmr;

// PlaylistWidget public-method coverage (playlist_widget.cpp). Uses only public
// API + the helper sizeModeChanged signal; addPlayFiles keeps the shared playlist
// at >=2 items so later ToolBox tests stay safe.
TEST(PlaylistWidgetExt, publicMethodsAndSizeMode)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_TRUE(w);
    PlaylistWidget *pl = w->playlist();
    ASSERT_TRUE(pl);
    PlayerEngine *engine = w->engine();

    // keep >=2 items in the shared playlist
    engine->addPlayFiles({QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4"),
                          QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3")});

    // trivial getters / safe calls (paOpen/paClose are ctor-init'd -> non-null)
    (void)pl->state();
    (void)pl->isFocusInPlaylist();
    pl->endAnimation();   // covers endAnimation body (~1509-1518)

    // size-mode lambdas (PlayItemWidget / ListWidget resize) for live widgets.
    // Pure UI resize, helper mode not changed by emit.
    emit DGuiApplicationHelper::instance()->sizeModeChanged(DGuiApplicationHelper::NormalMode);
    emit DGuiApplicationHelper::instance()->sizeModeChanged(DGuiApplicationHelper::CompactMode);
    QTest::qWait(50);
}
