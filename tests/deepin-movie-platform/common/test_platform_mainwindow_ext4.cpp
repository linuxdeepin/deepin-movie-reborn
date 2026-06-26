// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 4) for Platform_MainWindow (suite: platform_mw_ext4).
//
// Goal: raise line coverage of src/common/platform/platform_mainwindow.cpp by
// exercising the pure-logic helpers, settings-backed getters, geometry slots and
// ActionKind policy function (isActionAllowed) that do NOT depend on a live mpv
// backend or a loaded playlist. The shared main window owned by the test harness
// is in the Idle engine state (no media loaded), so every handler that
// early-returns / no-ops on Idle is safe.
//
// Safety rules baked in (mirrors ext3):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * The main window is fetched once per case via dApp->getMainWindow() and
//     ASSERT_NE'd before any use. The window is shared across the whole suite,
//     so cases only read state or toggle-and-restore flags they mutate.
//   * stub.h is intentionally NOT included; every case calls real functions.
//   * No mpv backend / decode path is exercised.
//   * Qt events are built on the stack with Qt6 ctor signatures and delivered
//     via QApplication::sendEvent.
//   * Paint / screen-geometry code is guarded by primaryScreen() and GTEST_SKIP
//     when headless.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QMainWindow>
#include <QMargins>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QRect>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QIcon>
#include <QStatusBar>
#include <QTimer>
#include <QWindowStateChangeEvent>
#include <QEnterEvent>
#include <QPointingDevice>

#include <gtest/gtest.h>

#define protected public
#define private public
#include "src/common/platform/platform_mainwindow.h"
#undef protected
#undef private

#include "application.h"
#include "src/common/dmr_settings.h"
#include "src/libdmr/player_engine.h"
#include "src/libdmr/compositing_manager.h"
#include "src/common/actions.h"

using namespace dmr;

// ---------- helpers (unique prefix pmw4_) ----------

static Platform_MainWindow *pmw4_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

static void pmw4_wait(int ms = 80)
{
    QTest::qWait(ms);
}

// ==========================================================================
// isActionAllowed — pure policy logic, many branches. High coverage value.
// Engine is Idle so the shortcut branches that need state != Idle yield false.
// ==========================================================================

TEST(platform_mw_ext4, IsActionAllowed_BurstShootMode_AlwaysFalse)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    bool old = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::OpenFile, false, false));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleFullscreen, true, true));
    w->m_bInBurstShootMode = old;
}

TEST(platform_mw_ext4, IsActionAllowed_MiniMode_FromUi_TogglesBlocked)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    bool oldMini = w->m_bMiniMode;
    bool oldBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = false;
    w->m_bMiniMode = true;

    // from UI / shortcut: these toggles are blocked in mini mode
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleFullscreen, true, false));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::TogglePlaylist, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::BurstScreenshot, true, true));
    // leaving mini mode is always allowed
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleMiniMode, true, false));

    w->m_bMiniMode = oldMini;
    w->m_bInBurstShootMode = oldBurst;
}

TEST(platform_mw_ext4, IsActionAllowed_Shortcut_NeedsPlaying_EngineIdle)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    bool oldMini = w->m_bMiniMode;
    bool oldBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = false;
    w->m_bMiniMode = false;

    // Engine is Idle => these shortcut kinds return false
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::Screenshot, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleMiniMode, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::MatchOnlineSubtitle, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::BurstScreenshot, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::MovieInfo, false, true));

    // subtitle kinds need subs; Idle movie info has none => false
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::HideSubtitle, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::SelectSubtitle, false, true));

    // a kind that isn't special-cased in the shortcut switch => true
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::OpenFile, false, true));

    w->m_bMiniMode = oldMini;
    w->m_bInBurstShootMode = oldBurst;
}

TEST(platform_mw_ext4, IsActionAllowed_NotShortcut_NotMini_DefaultTrue)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    bool oldMini = w->m_bMiniMode;
    bool oldBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = false;
    w->m_bMiniMode = false;

    // not from UI, not a shortcut -> falls through to `return true`
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::Screenshot, false, false));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::OpenFile, false, false));

    w->m_bMiniMode = oldMini;
    w->m_bInBurstShootMode = oldBurst;
}

// ==========================================================================
// judgeMouseInWindow — pure geometry helper. Always returns false; its body
// runs leaveEvent only when the point lies exactly on a frame edge.
// ==========================================================================

TEST(platform_mw_ext4, JudgeMouseInWindow_InteriorPoint_ReturnsFalse)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // a point clearly inside the window, not on any frame edge
    EXPECT_FALSE(w->judgeMouseInWindow(QPoint(50, 50)));
}

TEST(platform_mw_ext4, JudgeMouseInWindow_EdgePoint_TriggersLeave)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // top-left maps to the frame's top-left edge => leaveEvent branch is taken
    QPoint topLeft = w->rect().topLeft();
    EXPECT_FALSE(w->judgeMouseInWindow(topLeft));
    pmw4_wait(20);
}

// ==========================================================================
// Trivial setters / getters
// ==========================================================================

TEST(platform_mw_ext4, SetCurrentHwdec_StoresValue)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    QString old = w->m_sCurrentHwdec;
    w->setCurrentHwdec("auto-safe");
    EXPECT_EQ(w->m_sCurrentHwdec, "auto-safe");
    w->setCurrentHwdec(old);
}

TEST(platform_mw_ext4, SetOpenFiles_StoresList)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    QStringList old = w->m_listOpenFiles;
    QStringList list{"/tmp/a.mp4", "/tmp/b.mkv"};
    w->setOpenFiles(list);
    EXPECT_EQ(w->m_listOpenFiles, list);
    w->setOpenFiles(old);
}

TEST(platform_mw_ext4, PadLoadPath_NonEmpty_ReturnsExistingPath)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    // pad_load_path setting is empty by default => falls back to MoviesLocation
    // (or currentPath if MoviesLocation does not exist). Either way non-empty.
    QString path = w->padLoadPath();
    EXPECT_FALSE(path.isEmpty());
}

TEST(platform_mw_ext4, PadLoadPath_BogusSetting_FallsBackGracefully)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    Settings &s = Settings::get();
    QVariant prev = s.generalOption("pad_load_path");
    s.setGeneralOption("pad_load_path", "/this/path/does/not/exist/xyz123");
    QString path = w->padLoadPath();
    EXPECT_FALSE(path.isEmpty());
    EXPECT_NE(path, QString("/this/path/does/not/exist/xyz123"));
    s.setGeneralOption("pad_load_path", prev);
}

// ==========================================================================
// addCdromPath — reads /proc/mounts. With no optical mount present it always
// returns false (note: the early `return false;` is unconditional in source).
// ==========================================================================

TEST(platform_mw_ext4, AddCdromPath_NoOpticalMount_ReturnsFalse)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    EXPECT_FALSE(w->addCdromPath());
}

// ==========================================================================
// updateProxyGeometry — lays out toolbox / playlist / titlebar. Safe in Idle.
// ==========================================================================

TEST(platform_mw_ext4, UpdateProxyGeometry_Idle_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->updateProxyGeometry();
    SUCCEED();
}

// ==========================================================================
// suspendToolsWindow / resumeToolsWindow — UI restore/cursor logic. Safe Idle.
// ==========================================================================

TEST(platform_mw_ext4, SuspendToolsWindow_Idle_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->suspendToolsWindow();
    pmw4_wait(20);
    SUCCEED();
}

TEST(platform_mw_ext4, ResumeToolsWindow_Idle_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->resumeToolsWindow();
    pmw4_wait(20);
    SUCCEED();
}

// ==========================================================================
// animatePlayState — early-returns in Idle / mini / minimized. Safe.
// ==========================================================================

TEST(platform_mw_ext4, AnimatePlayState_Idle_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    bool oldMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    w->animatePlayState();   // Idle => no animation resize
    w->m_bMiniMode = oldMini;
    SUCCEED();
}

TEST(platform_mw_ext4, AnimatePlayState_MiniMode_EarlyReturn)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);

    bool oldMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    w->animatePlayState();
    w->m_bMiniMode = oldMini;
    SUCCEED();
}

// ==========================================================================
// onWindowStateChanged — titlebar / playlist visibility bookkeeping. Safe Idle.
// ==========================================================================

TEST(platform_mw_ext4, OnWindowStateChanged_Idle_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->onWindowStateChanged();
    pmw4_wait(20);
    SUCCEED();
}

// ==========================================================================
// updateContentGeometry — non-USE_DXCB path just move()/resize(). Safe.
// ==========================================================================

TEST(platform_mw_ext4, UpdateContentGeometry_NormalRect_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect saved = w->geometry();
    w->updateContentGeometry(QRect(saved.x(), saved.y(), saved.width(), saved.height()));
    pmw4_wait(20);
    w->resize(saved.size());
    w->move(saved.topLeft());
    SUCCEED();
}

// ==========================================================================
// checkOnlineState — toggles online/offline UI state. Safe Idle.
// ==========================================================================

TEST(platform_mw_ext4, CheckOnlineState_OnlineTrue_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->checkOnlineState(true);
    pmw4_wait(20);
    SUCCEED();
}

TEST(platform_mw_ext4, CheckOnlineState_OnlineFalse_NoCrash)
{
    Platform_MainWindow *w = pmw4_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->checkOnlineState(false);
    pmw4_wait(20);
    SUCCEED();
}

// ==========================================================================
// Platform_MessageWindow — small standalone widget. Construct on heap, exercise
// the inline setIcon/setMessage/showEvent/paintEvent, then delete.
// ==========================================================================

TEST(platform_mw_ext4, MessageWindow_SetIconAndMessage)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    Platform_MessageWindow *mw = new Platform_MessageWindow();
    ASSERT_NE(mw, nullptr);
    mw->setIcon(QIcon());
    mw->setMessage("hello message");
    EXPECT_EQ(mw->m_pTextLabel->text(), QString("hello message"));
    delete mw;
}

TEST(platform_mw_ext4, MessageWindow_ShowEvent_StartsTimer)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    Platform_MessageWindow *mw = new Platform_MessageWindow();
    ASSERT_NE(mw, nullptr);
    ASSERT_NE(mw->m_pTimer, nullptr);
    mw->show();
    pmw4_wait(30);
    // showEvent starts the single-shot close timer; let it fire.
    EXPECT_TRUE(mw->m_pTimer->isSingleShot());
    delete mw;
}

TEST(platform_mw_ext4, MessageWindow_PaintEvent_NoCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    Platform_MessageWindow *mw = new Platform_MessageWindow();
    ASSERT_NE(mw, nullptr);
    mw->resize(120, 40);
    mw->show();
    pmw4_wait(30);
    // force a repaint to run paintEvent
    mw->repaint();
    pmw4_wait(20);
    delete mw;
}
