// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

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

TEST(PlayerEngine, playerEngine)
{
    Platform_MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();

    QString subCodepage = engine->subCodepage();
    engine->addSubSearchPath(QString("/test/"));
    engine->selectTrack(1);
    engine->volumeUp();
    engine->volumeDown();
    engine->toggleMute();
}
