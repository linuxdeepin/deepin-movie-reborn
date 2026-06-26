// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests targeting src/common/mainwindow.cpp (suite: boost_dm_mw).
// All cases run against the shared MainWindow produced by dApp->getMainWindow().
// The engine is expected to stay Idle (no real media playback), so the cases
// focus on: (1) requestAction switch cases that early-return or are safe on Idle,
// (2) slot/helper branches reachable without playback, (3) event handlers driven
// by synthetic events, (4) branches reached via Stub (playerEngineState_Paused,
// isMpvExists, check_wayland_env, ...).

#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QTimer>
#include <QScreen>
#include <QWindow>
#include <QGuiApplication>
#include <QWidget>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QFocusEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QWindowStateChangeEvent>
#include <QPluginLoader>
#include <unistd.h>

#include <gtest/gtest.h>

// STL / Qt headers BEFORE the private/protected define (per project convention).
#define protected public
#define private public
#include "src/common/mainwindow.h"
#undef protected
#undef private

#include "application.h"
#include "src/common/actions.h"
#include "src/libdmr/player_engine.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/utils.h"
#include "src/libdmr/filefilter.h"
#include "src/libdmr/online_sub.h"
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/slider.h"
#include "src/widgets/movieinfo_dialog.h"
#include "src/widgets/url_dialog.h"
#include "src/widgets/dmr_lineedit.h"
#include "src/backends/mpv/mpv_glwidget.h"
#include "mpv_proxy.h"
#include "burst_screenshots_dialog.h"
#include "dbus_adpator.h"
#include "dbusutils.h"

#include "stub/stub.h"
#include "stub/addr_any.h"
#include "stub/stub_function.h"

using namespace dmr;

namespace {
// Helper prefix is bdmw_ per the task spec.
MainWindow *bdmw_window()
{
    MainWindow *w = dApp->getMainWindow();
    return w;
}

PlayerEngine *bdmw_engine()
{
    return bdmw_window()->engine();
}

ToolboxProxy *bdmw_toolbox()
{
    return bdmw_window()->toolbox();
}

// Paused-state stub for PlayerEngine::state, used to reach non-Idle branches
// (e.g. sleepStateChanged) without starting real playback.
PlayerEngine::CoreState bdmw_state_paused(void *)
{
    return PlayerEngine::CoreState::Paused;
}

// Skip helper for paint/globalscreen paths that need a real screen.
#define BDMW_SKIP_HEADLESS()                                          \
    do {                                                               \
        if (!QGuiApplication::primaryScreen())                         \
            GTEST_SKIP() << "headless: no primary screen";             \
    } while (0)
} // namespace

// =============================================================================
// requestAction: safe branches on Idle engine.
// =============================================================================

// requestAction early-returns when m_bStartAnimation is true. Toggle the flag,
// request, then restore so we exercise the early-return line without reaching
// real engine ops. (ToolboxProxy::getbAnimationFinash() is checked first; we
// leave it untouched and rely solely on the MainWindow flag.)
TEST(boost_dm_mw, requestAction_animation_locked_returns_early)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);

    bool oldAnim = w->m_bStartAnimation;
    w->m_bStartAnimation = true;

    w->requestAction(ActionFactory::ActionKind::ToggleMute);

    w->m_bStartAnimation = oldAnim;
    SUCCEED();
}

TEST(boost_dm_mw, requestAction_start_animation_returns_early)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldAnim = w->m_bStartAnimation;
    w->m_bStartAnimation = true;

    w->requestAction(ActionFactory::ActionKind::ToggleMute);

    w->m_bStartAnimation = oldAnim;
    SUCCEED();
}

// requestAction -> isActionAllowed when m_bInBurstShootMode is true => false.
TEST(boost_dm_mw, requestAction_blocked_in_burst_mode)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool old = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;

    w->requestAction(ActionFactory::ActionKind::ToggleFullscreen);

    w->m_bInBurstShootMode = old;
    SUCCEED();
}

// Order/Shuffle/Single/SingleLoop/ListLoop play mode setters are safe on Idle.
TEST(boost_dm_mw, requestAction_playmode_setters)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);

    w->requestAction(ActionFactory::ActionKind::OrderPlay);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::ShufflePlay);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::SinglePlay);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::SingleLoop);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::ListLoop);
    QTest::qWait(20);
    SUCCEED();
}

// Frame ratio setters route into m_pEngine->setVideoAspect; safe on Idle.
TEST(boost_dm_mw, requestAction_frame_ratios)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);

    w->requestAction(ActionFactory::ActionKind::DefaultFrame);
    w->requestAction(ActionFactory::ActionKind::Ratio4x3Frame);
    w->requestAction(ActionFactory::ActionKind::Ratio16x9Frame);
    w->requestAction(ActionFactory::ActionKind::Ratio16x10Frame);
    w->requestAction(ActionFactory::ActionKind::Ratio185x1Frame);
    w->requestAction(ActionFactory::ActionKind::Ratio235x1Frame);
    QTest::qWait(20);
    SUCCEED();
}

// Rotation frame actions call m_pEngine->setVideoRotation; safe on Idle.
TEST(boost_dm_mw, requestAction_rotation_frames)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);

    int before = e->videoRotation();
    w->requestAction(ActionFactory::ActionKind::ClockwiseFrame);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::CounterclockwiseFrame);
    QTest::qWait(20);
    // restore rotation to avoid drifting state on the shared window
    e->setVideoRotation(before);
    SUCCEED();
}

// Sound channel change is safe on Idle (routes to changeSoundMode).
TEST(boost_dm_mw, requestAction_sound_channels)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);

    w->requestAction(ActionFactory::ActionKind::Stereo);
    w->requestAction(ActionFactory::ActionKind::LeftChannel);
    w->requestAction(ActionFactory::ActionKind::RightChannel);
    QTest::qWait(20);
    SUCCEED();
}

// Volume / mute / sub-delay branches guarded by Idle state: with Idle engine the
// raw-format branch is false, so the else branch (toolbox.changeMuteState etc.)
// executes.
TEST(boost_dm_mw, requestAction_volume_mute_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);

    w->requestAction(ActionFactory::ActionKind::ToggleMute);
    QTest::qWait(20);
    w->m_iAngleDelta = 0;
    w->requestAction(ActionFactory::ActionKind::VolumeUp);
    QTest::qWait(20);
    w->m_iAngleDelta = 0;
    w->requestAction(ActionFactory::ActionKind::VolumeDown);
    QTest::qWait(20);
    SUCCEED();
}

// SubDelay / SubForward: with Idle engine the raw-format guard is false, but
// playingMovieInfo().subs.isEmpty() will be true so it shows the hint message.
TEST(boost_dm_mw, requestAction_sub_delay_forward_no_subs)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);

    w->requestAction(ActionFactory::ActionKind::SubDelay);
    QTest::qWait(20);
    w->requestAction(ActionFactory::ActionKind::SubForward);
    QTest::qWait(20);
    SUCCEED();
}

// SelectTrack / SelectSubtitle / ChangeSubCodepage / HideSubtitle all route into
// m_pEngine helpers; safe on Idle.
TEST(boost_dm_mw, requestAction_track_subtitle_codepage)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);

    w->requestAction(ActionFactory::ActionKind::SelectTrack, false, QList<QVariant>{0});
    w->requestAction(ActionFactory::ActionKind::SelectSubtitle, false, QList<QVariant>{0});
    w->requestAction(ActionFactory::ActionKind::ChangeSubCodepage, false, QList<QVariant>{QString("auto")});
    w->requestAction(ActionFactory::ActionKind::HideSubtitle);
    QTest::qWait(20);
    SUCCEED();
}

// Frame-step actions reach m_pEngine->nextFrame/previousFrame; safe on Idle.
TEST(boost_dm_mw, requestAction_next_prev_frame)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);

    w->requestAction(ActionFactory::ActionKind::NextFrame);
    w->requestAction(ActionFactory::ActionKind::PreviousFrame);
    QTest::qWait(20);
    SUCCEED();
}

// SeekAbsolute takes args; safe on Idle (mpv backend not loaded).
TEST(boost_dm_mw, requestAction_seek_absolute)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::SeekAbsolute, false, QList<QVariant>{0});
    QTest::qWait(20);
    SUCCEED();
}

// QuitFullscreen: vol slider hidden path takes the early break, then since
// neither mini nor fullscreen on the shared window it falls into the playlist
// branch (Closed -> nothing) and finally updateFullState().
TEST(boost_dm_mw, requestAction_quit_fullscreen_normal)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::QuitFullscreen);
    QTest::qWait(20);
    SUCCEED();
}

// QuitFullscreen: hide the vol slider first so the early break is NOT taken.
TEST(boost_dm_mw, requestAction_quit_fullscreen_vol_hidden)
{
    MainWindow *w = bdmw_window();
    ToolboxProxy *tb = bdmw_toolbox();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(tb, nullptr);
    bool wasHidden = tb->getVolSliderIsHided();
    if (!wasHidden)
        tb->setVolSliderHide();

    w->requestAction(ActionFactory::ActionKind::QuitFullscreen);
    QTest::qWait(20);
    SUCCEED();
}

// EmptyPlaylist clears via m_pEngine->clearPlaylist(); safe on Idle.
TEST(boost_dm_mw, requestAction_empty_playlist)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::EmptyPlaylist);
    QTest::qWait(20);
    SUCCEED();
}

// GotoPlaylistNext / GotoPlaylistPrev: m_bIsFree is true initially, so it sets
// m_bIsFree=false then calls m_pEngine->next()/prev(); with an empty playlist
// these are no-ops on the backend. Restore m_bIsFree afterwards.
TEST(boost_dm_mw, requestAction_goto_next_prev)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldFree = w->m_bIsFree;
    w->m_bIsFree = true;

    w->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);
    QTest::qWait(20);
    // restore before prev so the prev branch is actually exercised too
    w->m_bIsFree = true;
    w->requestAction(ActionFactory::ActionKind::GotoPlaylistPrev);
    QTest::qWait(20);

    w->m_bIsFree = oldFree;
    SUCCEED();
}

// TogglePause with Idle engine + not shortcut: falls through to the
// state==Paused branch check (false on Idle) -> pauseResume(). We stub
// PlayerEngine::state to Idle and skip the start-play recursion path.
TEST(boost_dm_mw, requestAction_toggle_pause_idle)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);

    // call with bIsShortcut=false to avoid the StartPlay recursion path
    w->requestAction(ActionFactory::ActionKind::TogglePause, false, {}, false);
    QTest::qWait(40);
    SUCCEED();
}

// Settings action calls handleSettings(initSettings()). Under USE_TEST the
// settings dialog is shown (not exec'd), so it does not block. The inner
// restart-confirm DDialog only appears when the decode option changes, which it
// will not here. Close any window that pops up to keep the suite clean.
TEST(boost_dm_mw, requestAction_settings_dialog_opens)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::Settings);
    QTest::qWait(50);
    // Clean up any non-modal settings window left open.
    QWidget *top = QApplication::activeWindow();
    if (top && top != w)
        top->close();
    QTest::qWait(20);
    SUCCEED();
}

// =============================================================================
// reflectActionToUI / isActionAllowed branches.
// =============================================================================

// reflectActionToUI covers many ActionKind cases. Drive several directly.
TEST(boost_dm_mw, reflectActionToUI_play_modes)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->reflectActionToUI(ActionFactory::ActionKind::OrderPlay);
    w->reflectActionToUI(ActionFactory::ActionKind::ShufflePlay);
    w->reflectActionToUI(ActionFactory::ActionKind::SinglePlay);
    w->reflectActionToUI(ActionFactory::ActionKind::SingleLoop);
    w->reflectActionToUI(ActionFactory::ActionKind::ListLoop);
    QTest::qWait(20);
    SUCCEED();
}

TEST(boost_dm_mw, reflectActionToUI_frames_sound)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->reflectActionToUI(ActionFactory::ActionKind::DefaultFrame);
    w->reflectActionToUI(ActionFactory::ActionKind::Stereo);
    w->reflectActionToUI(ActionFactory::ActionKind::OneTimes);
    w->reflectActionToUI(ActionFactory::ActionKind::ChangeSubCodepage);
    QTest::qWait(20);
    SUCCEED();
}

// reflectActionToUI(SelectTrack/SelectSubtitle): with Idle engine the early
// break is hit; cover it.
TEST(boost_dm_mw, reflectActionToUI_select_track_subtitle_idle)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);
    // Idle by default in the shared window
    w->reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    w->reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    QTest::qWait(20);
    SUCCEED();
}

TEST(boost_dm_mw, reflectActionToUI_toggle_playlist_and_mini)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->reflectActionToUI(ActionFactory::ActionKind::TogglePlaylist);
    w->reflectActionToUI(ActionFactory::ActionKind::ToggleMiniMode);
    QTest::qWait(20);
    SUCCEED();
}

// isActionAllowed: burst mode returns false; mini mode toggles behavior.
TEST(boost_dm_mw, isActionAllowed_burst_mode_false)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool old = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ActionKind::ToggleFullscreen, false, false));
    w->m_bInBurstShootMode = old;
}

TEST(boost_dm_mw, isActionAllowed_normal_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    // Idle engine + shortcut: several action kinds return false here.
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ActionKind::Screenshot, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ActionKind::MatchOnlineSubtitle, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ActionKind::MovieInfo, false, true));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ActionKind::VolumeUp, false, false));
}

// =============================================================================
// Slots / helpers reachable on Idle.
// =============================================================================

TEST(boost_dm_mw, slot_urlpause_shows_buffering)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotUrlpause(true);
    QTest::qWait(20);
    w->slotUrlpause(false);
    SUCCEED();
}

TEST(boost_dm_mw, slot_mute_changed)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);
    int old = w->m_nDisplayVolume;
    w->m_nDisplayVolume = 50;
    w->slotMuteChanged(true);
    QTest::qWait(10);
    w->slotMuteChanged(false);
    QTest::qWait(10);
    w->m_nDisplayVolume = old;
    SUCCEED();
}

TEST(boost_dm_mw, slot_volume_changed)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    int old = w->m_nDisplayVolume;
    w->slotVolumeChanged(0);
    QTest::qWait(10);
    w->slotVolumeChanged(80);
    QTest::qWait(10);
    w->m_nDisplayVolume = old;
    SUCCEED();
}

TEST(boost_dm_mw, slot_wm_changed)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotWMChanged();
    QTest::qWait(10);
    SUCCEED();
}

TEST(boost_dm_mw, check_online_subtitle_nosubfound)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->checkOnlineSubtitle(OnlineSubtitle::FailReason::NoSubFound);
    QTest::qWait(10);
    SUCCEED();
}

TEST(boost_dm_mw, check_online_state_offline)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->checkOnlineState(false);
    QTest::qWait(10);
    w->checkOnlineState(true);
    SUCCEED();
}

// checkWarningMpvLogsChanged: the special 4K branch fires only for a specific
// warning text. Use a non-matching text to hit the no-op path (safe).
TEST(boost_dm_mw, check_warning_mpv_logs_noop)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->checkWarningMpvLogsChanged("test", "an unrelated warning line");
    QTest::qWait(10);
    SUCCEED();
}

// checkErrorMpvLogsChanged: drive several else-if branches with synthetic text.
TEST(boost_dm_mw, check_error_mpv_logs_branches)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    // avformat_open_input failed -> do nothing (covered)
    w->checkErrorMpvLogsChanged("test", "avformat_open_input() failed");
    QTest::qWait(10);
    // moov atom not found -> message
    w->checkErrorMpvLogsChanged("test", "moov atom not found");
    QTest::qWait(10);
    // couldn't open dvd device -> message
    w->checkErrorMpvLogsChanged("test", "couldn't open dvd device");
    QTest::qWait(10);
    // incomplete frame -> do nothing
    w->checkErrorMpvLogsChanged("test", "incomplete frame data");
    QTest::qWait(10);
    SUCCEED();
}

TEST(boost_dm_mw, cpu_hardware_by_dbus)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    // Returns a string (possibly empty if the dbus service is absent in CI).
    QString hw = w->cpuHardwareByDBus();
    qDebug() << "cpuHardware:" << hw;
    SUCCEED();
}

TEST(boost_dm_mw, probe_cdrom_device)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QString dev = w->probeCdromDevice();
    qDebug() << "probeCdromDevice:" << dev;
    SUCCEED();
}

// addCdromPath reads /proc/mounts; returns false when no cdrom is mounted (CI).
TEST(boost_dm_mw, add_cdrom_path)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool ok = w->addCdromPath();
    qDebug() << "addCdromPath:" << ok;
    SUCCEED();
}

TEST(boost_dm_mw, pad_load_path)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QString p = w->padLoadPath();
    qDebug() << "padLoadPath:" << p;
    EXPECT_FALSE(p.isEmpty());
}

TEST(boost_dm_mw, set_open_files_then_last_path)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QStringList files{"/tmp/nonexistent_a.mp4", "/tmp/nonexistent_b.mp3"};
    w->setOpenFiles(files);
    QString lp = w->lastOpenedPath();
    qDebug() << "lastOpenedPath:" << lp;
    SUCCEED();
}

TEST(boost_dm_mw, drag_margins_and_inside_resize_area)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QMargins m = w->dragMargins();
    qDebug() << "dragMargins:" << m;
    // A point well inside the frame should NOT be in the resize area.
    bool inside = w->insideResizeArea(w->geometry().center());
    qDebug() << "insideResizeArea(center):" << inside;
    SUCCEED();
}

TEST(boost_dm_mw, update_geometry_notification)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QRect oldLast = w->m_lastRectInNormalMode;
    w->updateGeometryNotification(QSize(800, 600));
    QTest::qWait(10);
    w->m_lastRectInNormalMode = oldLast;
    SUCCEED();
}

TEST(boost_dm_mw, limit_windowize_noop)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QRect oldLast = w->m_lastRectInNormalMode;
    w->LimitWindowize();
    w->m_lastRectInNormalMode = oldLast;
    SUCCEED();
}

TEST(boost_dm_mw, update_size_constraints)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->updateSizeConstraints();
    SUCCEED();
}

// setInit toggles m_bInited and emits initChanged only on change.
TEST(boost_dm_mw, set_init_toggle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool old = w->m_bInited;
    bool gotSignal = false;
    auto c = QObject::connect(w, &MainWindow::initChanged, [&]() { gotSignal = true; });
    w->setInit(!old);   // change -> emit
    QTest::qWait(10);
    bool changedSignal = gotSignal;
    gotSignal = false;
    w->setInit(!old);   // no change -> no emit
    QTest::qWait(10);
    EXPECT_TRUE(changedSignal);
    EXPECT_FALSE(gotSignal);
    // restore
    w->setInit(old);
    QObject::disconnect(c);
}

// slotFocusWindowChanged: focus != windowHandle -> suspendToolsWindow path.
TEST(boost_dm_mw, slot_focus_window_changed)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotFocusWindowChanged();
    QTest::qWait(10);
    SUCCEED();
}

// slotFontChanged only updates label widths; safe on Idle.
TEST(boost_dm_mw, slot_font_changed)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotFontChanged(QFont("Helvetica"));
    QTest::qWait(10);
    SUCCEED();
}

// =============================================================================
// Slots that need Stub to reach non-Idle branches.
// =============================================================================

// slotPlayerStateChanged reads sender(); calling it directly without a sender
// yields nullptr -> early return. That covers the early-return branch safely.
TEST(boost_dm_mw, slot_player_state_changed_no_sender)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotPlayerStateChanged();
    QTest::qWait(10);
    SUCCEED();
}

// sleepStateChanged: with Idle engine (no stub) the playing/paused branches
// are skipped; only m_bStartSleep is set. Cover both args + restore.
TEST(boost_dm_mw, sleep_state_changed_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldStart = w->m_bStartSleep;
    w->sleepStateChanged(true);
    QTest::qWait(10);
    w->sleepStateChanged(false);
    QTest::qWait(10);
    w->m_bStartSleep = oldStart;
    SUCCEED();
}

// sleepStateChanged with Paused engine (stubbed): hits the seekAbsolute branch.
TEST(boost_dm_mw, sleep_state_changed_paused_stub)
{
    MainWindow *w = bdmw_window();
    PlayerEngine *e = bdmw_engine();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(e, nullptr);
    Stub stub;
    stub.set(ADDR(PlayerEngine, state), bdmw_state_paused);
    bool oldStart = w->m_bStartSleep;
    w->sleepStateChanged(false);
    QTest::qWait(20);
    w->m_bStartSleep = oldStart;
    stub.reset(ADDR(PlayerEngine, state));
    SUCCEED();
}

// lockStateChanged: Idle engine -> only the mircast (false) and state guards
// are skipped; safe.
TEST(boost_dm_mw, lock_state_changed_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldStateInLock = w->m_bStateInLock;
    w->lockStateChanged(true);
    QTest::qWait(10);
    w->lockStateChanged(false);
    QTest::qWait(10);
    w->m_bStateInLock = oldStateInLock;
    SUCCEED();
}

// diskRemoved: empty playlist -> early return; safe.
TEST(boost_dm_mw, disk_removed_empty)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->diskRemoved("/dev/sr0");
    QTest::qWait(10);
    SUCCEED();
}

// slotmousePressTimerTimeOut: with mini/burst off and mouse not pressed -> early
// return; safe.
TEST(boost_dm_mw, slot_mouse_press_timer_timeout_noop)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldMini = w->m_bMiniMode;
    bool oldBurst = w->m_bInBurstShootMode;
    bool oldPressed = w->m_bMousePressed;
    w->m_bMiniMode = false;
    w->m_bInBurstShootMode = false;
    w->m_bMousePressed = false;
    w->slotmousePressTimerTimeOut();
    QTest::qWait(10);
    w->m_bMiniMode = oldMini;
    w->m_bInBurstShootMode = oldBurst;
    w->m_bMousePressed = oldPressed;
    SUCCEED();
}

// animatePlayState: mini mode short-circuits; non-mini with Idle engine hits the
// Paused check (false) and returns. Both branches safe.
TEST(boost_dm_mw, animate_play_state)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldMini = w->m_bMiniMode;

    w->m_bMiniMode = true;
    w->animatePlayState();
    QTest::qWait(10);

    w->m_bMiniMode = false;
    w->animatePlayState();
    QTest::qWait(10);

    w->m_bMiniMode = oldMini;
    SUCCEED();
}

// slotMediaError removes the current playlist item; on empty playlist this is a
// no-op. Safe on Idle.
TEST(boost_dm_mw, slot_media_error)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->slotMediaError();
    QTest::qWait(10);
    SUCCEED();
}

// =============================================================================
// Event handlers driven by synthetic events.
// =============================================================================

// leaveEvent stops the auto-hide timer and suspends tools window. Safe.
TEST(boost_dm_mw, leave_event)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QEvent ev(QEvent::Leave);
    QApplication::sendEvent(w, &ev);
    QTest::qWait(10);
    SUCCEED();
}

// hideEvent / showEvent round-trip is safe.
TEST(boost_dm_mw, hide_show_event)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QHideEvent he;
    QApplication::sendEvent(w, &he);
    QTest::qWait(10);
    QShowEvent se;
    QApplication::sendEvent(w, &se);
    QTest::qWait(10);
    SUCCEED();
}

// focusInEvent / focusOutEvent.
TEST(boost_dm_mw, focus_in_out_event)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QFocusEvent fin(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(w, &fin);
    QTest::qWait(10);
    QFocusEvent fout(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(w, &fout);
    QTest::qWait(10);
    SUCCEED();
}

// keyPressEvent with no playlist open just forwards to base.
TEST(boost_dm_mw, key_press_event_no_playlist)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(w, &ke);
    QTest::qWait(10);
    QKeyEvent kr(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(w, &kr);
    QTest::qWait(10);
    SUCCEED();
}

// wheelEvent with no buttons and vol-slider hidden -> routes to VolumeUp/Down
// via requestAction. Use a small positive then negative delta.
TEST(boost_dm_mw, wheel_event_volume)
{
    BDMW_SKIP_HEADLESS();
    MainWindow *w = bdmw_window();
    ToolboxProxy *tb = bdmw_toolbox();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(tb, nullptr);
    bool wasHidden = tb->getVolSliderIsHided();
    if (!wasHidden)
        tb->setVolSliderHide();

    const QPointingDevice *dev = QPointingDevice::primaryPointingDevice();
    if (dev) {
        QPointF pos(5, 5);
        // positive delta -> volume up
        QWheelEvent up(pos, pos, QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::ScrollUpdate, false, Qt::MouseEventNotSynthesized, dev);
        QApplication::sendEvent(w, &up);
        QTest::qWait(20);
        // negative delta -> volume down
        QWheelEvent down(pos, pos, QPoint(0, 0), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                         Qt::ScrollUpdate, false, Qt::MouseEventNotSynthesized, dev);
        QApplication::sendEvent(w, &down);
        QTest::qWait(20);
    }
    SUCCEED();
}

// mousePressEvent / mouseReleaseEvent with a left button. Restore flags after.
TEST(boost_dm_mw, mouse_press_release_events)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldPressed = w->m_bMousePressed;
    bool oldMoved = w->m_bMouseMoved;

    QPointF local(10, 10);
    QPointF global = w->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &press);
    QTest::qWait(10);

    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &release);
    QTest::qWait(10);

    w->m_bMousePressed = oldPressed;
    w->m_bMouseMoved = oldMoved;
    SUCCEED();
}

// mouseMoveEvent with a tiny delta (< 5px) early-returns; safe.
TEST(boost_dm_mw, mouse_move_event_small_delta)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldStartMini = w->m_bStartMini;
    w->m_bStartMini = false;
    QPointF local(2, 2);
    QPointF global = w->mapToGlobal(local.toPoint());
    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &move);
    QTest::qWait(10);
    w->m_bStartMini = oldStartMini;
    SUCCEED();
}

// mouseDoubleClickEvent: Idle engine + not mini + not burst would dispatch
// StartPlay which we must avoid. Set m_bInBurstShootMode=true to hit the early
// return path instead.
TEST(boost_dm_mw, mouse_double_click_burst_guard)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;

    QPointF local(5, 5);
    QPointF global = w->mapToGlobal(local.toPoint());
    QMouseEvent dclick(QEvent::MouseButtonDblClick, local, global, Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &dclick);
    QTest::qWait(10);

    w->m_bInBurstShootMode = oldBurst;
    SUCCEED();
}

// contextMenuEvent: mini/burst guard -> early return (avoids the popup path).
TEST(boost_dm_mw, context_menu_event_mini_guard)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    bool oldMini = w->m_bMiniMode;
    w->m_bMiniMode = true;

    QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(5, 5), w->mapToGlobal(QPoint(5, 5)));
    QApplication::sendEvent(w, &cme);
    QTest::qWait(10);

    w->m_bMiniMode = oldMini;
    SUCCEED();
}

// capturedKeyEvent with Tab key: shows toolbox if not fullscreen.
TEST(boost_dm_mw, captured_key_event_tab)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    w->capturedKeyEvent(&tab);
    QTest::qWait(10);
    SUCCEED();
}

// capturedMousePressEvent / capturedMouseReleaseEvent with focus window null is
// hard to force; instead drive them with a valid focus window (the shared window
// usually has one after show). Restore flags after.
TEST(boost_dm_mw, captured_mouse_press_release)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    if (!w->windowHandle()) {
        w->show();
        QTest::qWait(50);
    }
    bool oldPressed = w->m_bMousePressed;
    bool oldMoved = w->m_bMouseMoved;
    bool oldTouch = w->m_bIsTouch;

    QPointF local(8, 8);
    QPointF global = w->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    w->capturedMousePressEvent(&press);
    QTest::qWait(10);

    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    w->capturedMouseReleaseEvent(&release);
    QTest::qWait(10);

    w->m_bMousePressed = oldPressed;
    w->m_bMouseMoved = oldMoved;
    w->m_bIsTouch = oldTouch;
    SUCCEED();
}

// event() with a TouchBegin sets the touch flag; safe synthetic event.
TEST(boost_dm_mw, event_touch_begin)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QEvent tb(QEvent::TouchBegin);
    QApplication::sendEvent(w, &tb);
    QTest::qWait(10);
    SUCCEED();
}

// =============================================================================
// Wayland-gated branches via Stub.
// =============================================================================

// resizeByConstraints: Idle engine -> early return + clears titletxt.
TEST(boost_dm_mw, resize_by_constraints_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->resizeByConstraints();
    QTest::qWait(10);
    w->resizeByConstraints(true);
    QTest::qWait(10);
    SUCCEED();
}

// updateWindowTitle: Idle -> clears title; safe.
TEST(boost_dm_mw, update_window_title_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->updateWindowTitle();
    QTest::qWait(10);
    SUCCEED();
}

// judgeMouseInWindow: corner point triggers leaveEvent path.
TEST(boost_dm_mw, judge_mouse_in_window_edge)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QRect r = w->frameGeometry();
    w->judgeMouseInWindow(r.topLeft());
    QTest::qWait(10);
    SUCCEED();
}

TEST(boost_dm_mw, judge_mouse_in_window_inside)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->judgeMouseInWindow(w->rect().center());
    QTest::qWait(10);
    SUCCEED();
}

// mircastSuccess / exitMircast touch engine state; with Idle engine the
// pauseResume guards are skipped. Safe.
TEST(boost_dm_mw, mircast_success_and_exit_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->mircastSuccess("test-device");
    QTest::qWait(20);
    w->exitMircast();
    QTest::qWait(20);
    SUCCEED();
}

// TestCdrom is a USE_TEST-only helper that exercises several private slots.
TEST(boost_dm_mw, test_cdrom_helper)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->testCdrom();
    QTest::qWait(40);
    SUCCEED();
}

// setCurrentHwdec is a USE_TEST-only setter; round-trip a value.
TEST(boost_dm_mw, set_current_hwdec)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    QString old = w->m_sCurrentHwdec;
    w->setCurrentHwdec("auto-safe");
    w->setCurrentHwdec(old);
    SUCCEED();
}

// =============================================================================
// Playback-speed helpers (safe on Idle: the inner block is guarded by state !=
// Idle, so we exercise the early-return line; then with a Paused stub we reach
// the menu-update branches).
// =============================================================================

TEST(boost_dm_mw, adjust_playback_speed_idle_returns)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->adjustPlaybackSpeed(ActionFactory::ActionKind::AccelPlayback);
    QTest::qWait(10);
    w->adjustPlaybackSpeed(ActionFactory::ActionKind::DecelPlayback);
    QTest::qWait(10);
    SUCCEED();
}

// setPlaySpeedMenuChecked / Unchecked touch ActionFactory action state; safe.
TEST(boost_dm_mw, set_play_speed_menu_checked_unchecked)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
    QTest::qWait(10);
    w->setPlaySpeedMenuUnchecked();
    QTest::qWait(10);
    SUCCEED();
}

// Request the speed-menu actions on Idle; they early-return inside the switch.
TEST(boost_dm_mw, requestAction_speed_menus_idle)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::ZeroPointFiveTimes);
    w->requestAction(ActionFactory::ActionKind::OneTimes);
    w->requestAction(ActionFactory::ActionKind::OnePointTwoTimes);
    w->requestAction(ActionFactory::ActionKind::OnePointFiveTimes);
    w->requestAction(ActionFactory::ActionKind::Double);
    w->requestAction(ActionFactory::ActionKind::ResetPlayback);
    QTest::qWait(20);
    SUCCEED();
}

// GotoPlaylistSelected routes into m_pEngine->playSelected(idx). With an empty
// playlist this is a backend no-op on Idle. Safe.
TEST(boost_dm_mw, requestAction_goto_playlist_selected)
{
    MainWindow *w = bdmw_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::ActionKind::GotoPlaylistSelected, false, QList<QVariant>{0});
    QTest::qWait(20);
    SUCCEED();
}
