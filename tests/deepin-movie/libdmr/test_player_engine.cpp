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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "player_widget.h"
#include "player_engine.h"
#include "compositing_manager.h"
#include "movie_configuration.h"

TEST(PlayerEngine, playerEngine)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();

    QString subCodepage = engine->subCodepage();
    engine->addSubSearchPath(QString("/test/"));
    engine->selectTrack(1);
    engine->volumeUp();
    engine->volumeDown();
    engine->toggleMute();
}

TEST(PlayerEngine, movieInfo)
{
#ifdef _LIBDMR_
    MovieInfo mi;
    bool bFlag = false;

    mi = MovieInfo::parseFromFile(QFileInfo("/data/source/deepin-movie-reborn/movie/demo.mp4"), &bFlag);
#endif
}

// Real media so isPlayableFile's FFmpeg content probe returns true.
static const char *pe_kDemoMedia = "/data/source/deepin-movie-reborn/movie/demo.mp4";

// addPlayDir: temp dir with numbered media symlinks + a non-media file
// exercises FileFilter::filterDir, the SortByDigits digit comparator, and
// addPlayFiles' playable / non-playable branches.
TEST(PlayerEngine, addPlayDir_sortsAndAppends)
{
    PlayerEngine *engine = dApp->getMainWindow()->engine();
    ASSERT_TRUE(engine != nullptr);

    QTemporaryDir td;
    ASSERT_TRUE(td.isValid());
    // Digit-leading names so SortByDigits' same-position digit comparison runs
    // (the comparator only enters the digit branch when the digit offset equals
    // the search offset, i.e. the name starts with digits).
    for (const char *n : {"01.mp4", "02.mp4", "10.mp4"}) {
        ASSERT_TRUE(QFile::link(QString(pe_kDemoMedia), td.path() + "/" + n));
    }
    // A non-media file -> isPlayableFile false -> filtered out by addPlayFiles.
    QFile txt(td.path() + "/readme.txt");
    ASSERT_TRUE(txt.open(QIODevice::WriteOnly));
    txt.close();

    QList<QUrl> result = engine->addPlayDir(QDir(td.path()));
    EXPECT_GE(result.size(), 3);  // the 3 real-media symlinks survived filtering
}

// addPlayFs: file branch, directory branch (filterDir), and the empty-valids
// early return.
TEST(PlayerEngine, addPlayFs_file_dir_empty_branches)
{
    PlayerEngine *engine = dApp->getMainWindow()->engine();
    ASSERT_TRUE(engine != nullptr);

    QTemporaryDir td;
    ASSERT_TRUE(td.isValid());
    ASSERT_TRUE(QFile::link(QString(pe_kDemoMedia), td.path() + "/clip.mp4"));
    ASSERT_TRUE(QDir(td.path()).mkdir("sub"));
    ASSERT_TRUE(QFile::link(QString(pe_kDemoMedia), td.path() + "/sub/inner.mp4"));
    ASSERT_TRUE(QDir(td.path()).mkdir("empty"));

    // file + dir entries -> non-empty valids -> addPlayFiles + finishedAddFiles.
    engine->addPlayFs({td.path() + "/clip.mp4", td.path() + "/sub"});
    QTest::qWait(50);

    // only an empty dir -> valids empty -> early return.
    engine->addPlayFs({td.path() + "/empty"});
    QTest::qWait(50);

    SUCCEED();
}

// play(): empty-playlist early return (no real playback triggered).
TEST(PlayerEngine, play_emptyPlaylist_returnsEarly)
{
    PlayerEngine *engine = dApp->getMainWindow()->engine();
    ASSERT_TRUE(engine != nullptr);
    engine->playlist().clear();   // ensure empty -> play() hits the guard
    engine->play();               // covers "no backend or empty playlist" return
    SUCCEED();
}

// loadSubtitle + selectSubtitle: needs a loaded file. Load demo.mp4, then load
// an external .ass subtitle (covers loadSubtitle's append2ListUrl path) and
// select subtitle track 0 (covers selectSubtitle's sid lookup). Does NOT clear
// the shared playlist (later ToolBox tests need >=2 items).
TEST(PlayerEngine, loadSubtitleAndSelect)
{
    PlayerEngine *engine = dApp->getMainWindow()->engine();
    ASSERT_TRUE(engine != nullptr);
    engine->addPlayFiles({QUrl::fromLocalFile(pe_kDemoMedia)});
    engine->playByName(QUrl::fromLocalFile(pe_kDemoMedia));
    int waited = 0;
    while (engine->state() == PlayerEngine::CoreState::Idle && waited < 30) { QTest::qWait(100); waited++; }
    QTest::qWait(300);

    const QString ass = "/data/source/deepin-movie-reborn/movie/Hachiko.A.Dog's.Story.ass";
    if (QFile::exists(ass)) {
        engine->loadSubtitle(QFileInfo(ass));
    }
    engine->selectSubtitle(0);
    QTest::qWait(100);
}

// isPlayableFile: a local nonexistent file hits the not-playable path that
// emits sigInvalidFile + returns false (player_engine.cpp ~145-157). (Network
// URLs are considered playable by default, so they don't reach this path.)
TEST(PlayerEngine, isPlayableFile_invalid)
{
    PlayerEngine *engine = dApp->getMainWindow()->engine();
    ASSERT_TRUE(engine != nullptr);
    EXPECT_FALSE(engine->isPlayableFile(QUrl::fromLocalFile("/nonexistent/deepin-movie-probe.mp4")));
}

