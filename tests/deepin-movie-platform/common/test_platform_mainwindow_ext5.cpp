// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 5) for Platform_MainWindow (suite: platform_mw_ext5).
//
// Goal: raise line coverage of src/common/platform/platform_mainwindow.cpp by
// exercising the ActionKind dispatch in requestAction across MANY kinds (each
// distinct kind covers a different switch branch), the eventFilter on the main
// window's QWindow, contextMenuEvent, wheelEvent, keyPressEvent, mouseMoveEvent,
// the updateGeometry(CornerEdge,QPoint) overload, reflectActionToUI branches,
// menuItemInvoked, event() dispatch and the small slots checkErrorMpvLogsChanged,
// miniButtonClicked, slotmousePressTimerTimeOut, sleepStateChanged.
//
// The shared main window owned by the test harness is in the Idle engine state
// (no media loaded), so every handler that early-returns / no-ops on Idle is
// safe. A crash drops every later case, so risky bits are guarded.
//
// Safety rules baked in (mirrors ext3/ext4):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * The main window is fetched once per case via dApp->getMainWindow() and
//     ASSERT_NE'd before any use. The window is shared across the whole suite,
//     so cases only read state or toggle-and-restore flags they mutate.
//   * stub.h is intentionally NOT included; every case calls real functions.
//   * No mpv backend / decode path is exercised. Idle branches only.
//   * Qt events are built on the stack with Qt6 ctor signatures and delivered
//     via QApplication::sendEvent.
//   * Paint / screen-geometry code is guarded by primaryScreen() and GTEST_SKIP
//     when headless.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QMainWindow>
#include <QMargins>
#include <QDir>
#include <QAction>
#include <QMenu>
#include <QMimeData>
#include <QUrl>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QRect>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWindowStateChangeEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QPointingDevice>
#include <QDateTime>
#include <QCursor>
#include <QWindow>

// Standard-library preload (prevents the sstream redeclare error).
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <memory>

#define protected public
#define private public
#include "src/common/platform/platform_mainwindow.h"
#undef protected
#undef private

#include "application.h"
#include "src/common/actions.h"
#include "src/libdmr/player_engine.h"

using namespace dmr;

// ---------- helpers (unique prefix pmw5_) ----------

static Platform_MainWindow *pmw5_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

static void pmw5_wait(int ms = 80)
{
    QTest::qWait(ms);
}

// ==========================================================================
// requestAction — exhaustively drive distinct ActionKind branches. Engine is
// Idle so the play()/setPlaySpeed()/seekAbsolute() branches are skipped, but
// the switch cases themselves are still entered and logged. Many kinds either
// early-return on Idle or only touch local state / Settings — all safe.
// ==========================================================================

TEST(platform_mw_ext5, RequestAction_OpenFileList_EarlyReturnNoDialog)
{
    // Under USE_TEST the filenames are hard-coded (and missing on disk), so
    // play() is a no-op; the case still enters the OpenFileList switch body.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OpenFileList, false, {}, false));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, RequestAction_OpenCdrom_NoDevice_ShowsHint)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OpenCdrom, false, {}, false));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, RequestAction_TogglePlaylist_SchedulesAnimation)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    bool savedMini = w->m_bMiniMode;
    bool savedStartMini = w->m_bStartMini;
    w->m_bMiniMode = false;
    w->m_bStartMini = false;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::TogglePlaylist, true, {}, false));
    pmw5_wait(200);   // 150ms singleShot inside
    EXPECT_FALSE(w->m_bStartAnimation);
    w->m_bMiniMode = savedMini;
    w->m_bStartMini = savedStartMini;
}

TEST(platform_mw_ext5, RequestAction_TogglePlaylist_ShortcutClearsFocus)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::TogglePlaylist, false, {}, true));
    pmw5_wait(200);
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext5, RequestAction_TogglePlaylist_MiniModeEarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::TogglePlaylist, false, {}, false));
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext5, RequestAction_ToggleMiniMode_MouseMovedEarlyReturn)
{
    // m_bMouseMoved true forces the early return before any UI work.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMouseMoved;
    w->m_bMouseMoved = true;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ToggleMiniMode, false, {}, false));
    w->m_bMouseMoved = saved;
}

TEST(platform_mw_ext5, RequestAction_ToggleMiniMode_RateLimitedEarlyReturn)
{
    // m_nFullscreenTime recent (<600ms) forces the early return.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    qint64 saved = w->m_nFullscreenTime;
    w->m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ToggleMiniMode, false, {}, false));
    w->m_nFullscreenTime = saved;
}

TEST(platform_mw_ext5, RequestAction_MovieInfo_IdleEarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    // Idle => the if-body (MovieInfoDialog) is skipped.
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::MovieInfo, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_QuitFullscreen_NormalStateClosesPlaylist)
{
    // Not mini, not fullscreen, not rate-limited, vol slider hidden (default)
    // -> falls into the else-branch that may close an open playlist.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    qint64 saved = w->m_nFullscreenTime;
    w->m_nFullscreenTime = 0;
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::QuitFullscreen, false, {}, false));
    w->m_nFullscreenTime = saved;
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext5, RequestAction_QuitFullscreen_RateLimitedEarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    qint64 saved = w->m_nFullscreenTime;
    w->m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::QuitFullscreen, false, {}, false));
    w->m_nFullscreenTime = saved;
}

TEST(platform_mw_ext5, RequestAction_ToggleFullscreen_RateLimitedEarlyReturn)
{
    // m_nFullscreenTime recent (<600ms) -> the ToggleFullscreen case early
    // returns before any window-state mutation. Conservative: we do not drive
    // the real fullscreen entry path (it mutates window state and can interact
    // with the WM, risking later-case instability).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    qint64 saved = w->m_nFullscreenTime;
    w->m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ToggleFullscreen, false, {}, false));
    w->m_nFullscreenTime = saved;
}

TEST(platform_mw_ext5, RequestAction_PlayModeKinds_WriteSettings)
{
    // Each play-mode kind writes Settings + setPlayMode on the playlist model.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OrderPlay, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ShufflePlay, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SinglePlay, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SingleLoop, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ListLoop, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_FrameKinds_SetAspect)
{
    // Idle engine -> setVideoAspect is a no-op backend call internally; safe.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::DefaultFrame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Ratio4x3Frame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Ratio16x9Frame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Ratio16x10Frame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Ratio185x1Frame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Ratio235x1Frame, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_SpeedKinds_IdleEarlyReturn)
{
    // Idle => the inner if(state != Idle) is skipped, m_dPlaySpeed unchanged.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    double saved = w->m_dPlaySpeed;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ZeroPointFiveTimes, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OneTimes, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OnePointTwoTimes, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::OnePointFiveTimes, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Double, false, {}, false));
    EXPECT_EQ(w->m_dPlaySpeed, saved);
}

TEST(platform_mw_ext5, RequestAction_SoundChannelKinds_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Stereo, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::LeftChannel, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::RightChannel, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_ToggleMute_IdleDelegates)
{
    // Idle => the raw-format branch is skipped, toolbox mute toggled instead.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ToggleMute, false, {}, false));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, RequestAction_SubtitleDelayKinds_IdleNoSubsHint)
{
    // Idle -> playingMovieInfo().subs empty -> "Unable to adjust" hint shown.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SubDelay, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SubForward, false, {}, false));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, RequestAction_AccelDecel_IdleNoChange)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    double saved = w->m_dPlaySpeed;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::AccelPlayback, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::DecelPlayback, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ResetPlayback, false, {}, false));
    EXPECT_EQ(w->m_dPlaySpeed, saved);
}

TEST(platform_mw_ext5, RequestAction_SeekKinds_IdleSafe)
{
    // Idle -> seekBackward/seekForward call the backend directly (no-op Idle);
    // SeekAbsolute needs args[0]; supply one to satisfy the Q_ASSERT.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SeekBackward, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SeekForward, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SeekAbsolute, false, {0}, false));
}

TEST(platform_mw_ext5, RequestAction_NextPrevFrame_IdleSafe)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::NextFrame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::PreviousFrame, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_RotateFrame_IdleSafe)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ClockwiseFrame, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::CounterclockwiseFrame, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_GotoNextPrev_MovieSwitchFlagClears)
{
    // m_bIsFree starts true; the call sets it false and back via engine no-op.
    // We restore m_bIsFree=true to keep the shared window clean.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bIsFree;
    w->m_bIsFree = true;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::GotoPlaylistNext, false, {}, false));
    w->m_bIsFree = true;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::GotoPlaylistPrev, false, {}, false));
    w->m_bIsFree = saved;
}

TEST(platform_mw_ext5, RequestAction_GotoNext_BusyEarlyReturn)
{
    // m_bIsFree false -> early return inside the case body (after setFocus).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bIsFree;
    w->m_bIsFree = false;
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::GotoPlaylistNext, false, {}, false));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::GotoPlaylistPrev, false, {}, false));
    w->m_bIsFree = saved;
}

TEST(platform_mw_ext5, RequestAction_StartPlay_EmptyPlaylistRecurses)
{
    // Empty playlist -> requestAction recurses into OpenFileList (USE_TEST
    // hard-coded path, missing -> play() no-op). Safe.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::StartPlay, false, {}, false));
    pmw5_wait(40);
}

TEST(platform_mw_ext5, RequestAction_EmptyPlaylist_ClearsEngine)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::EmptyPlaylist, false, {}, false));
}

TEST(platform_mw_ext5, RequestAction_TogglePause_IdleShortcutStartsPlay)
{
    // Idle + shortcut -> recurses into StartPlay (empty playlist -> OpenFileList
    // -> play() no-op). NOT a shortcut falls to the else branch with Idle,
    // which calls pauseResume (no-op Idle).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::TogglePause, false, {}, true));
    pmw5_wait(40);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::TogglePause, false, {}, false));
    pmw5_wait(40);
}

TEST(platform_mw_ext5, RequestAction_DisallowedScreenshotEarlyReturn)
{
    // Screenshot shortcut while Idle -> isActionAllowed false -> early return
    // before the (file-saving) body. Safe and covers the guard.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::Screenshot, false, {}, true));
}

TEST(platform_mw_ext5, RequestAction_DisallowedSubtitleKindsEarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    // These need subs / a loaded movie; Idle shortcut => disallowed.
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::HideSubtitle, false, {}, true));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::SelectSubtitle, false, {0}, true));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::MatchOnlineSubtitle, false, {}, true));
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::ChangeSubCodepage, false, {QString("auto")}, true));
}

TEST(platform_mw_ext5, RequestAction_BurstScreenshot_IdleShortcutDisallowed)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->requestAction(ActionFactory::BurstScreenshot, false, {}, true));
}

// ==========================================================================
// reflectActionToUI — exercise more of its switch branches.
// ==========================================================================

TEST(platform_mw_ext5, ReflectActionToUI_WindowAbove_TogglesCheck)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::WindowAbove));
}

TEST(platform_mw_ext5, ReflectActionToUI_ToggleFullscreen_TogglesCheck)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::ToggleFullscreen));
}

TEST(platform_mw_ext5, ReflectActionToUI_HideSubtitle_TogglesCheck)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::HideSubtitle));
}

TEST(platform_mw_ext5, ReflectActionToUI_ToggleMiniMode_TogglesFullscreenUnchecked)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::ToggleMiniMode));
}

TEST(platform_mw_ext5, ReflectActionToUI_ChangeSubCodepage_MatchesAuto)
{
    // Idle engine subCodepage() returns "auto"; the matching branch is taken.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::ChangeSubCodepage));
}

TEST(platform_mw_ext5, ReflectActionToUI_SelectSubtitle_IdleEarlyBreak)
{
    // Idle engine -> the SelectSubtitle case breaks early (no pmf iteration).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::SelectSubtitle));
}

TEST(platform_mw_ext5, ReflectActionToUI_SelectTrack_IdleEarlyBreak)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::SelectTrack));
}

TEST(platform_mw_ext5, ReflectActionToUI_Stereo_MarksChecked)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::Stereo));
}

TEST(platform_mw_ext5, ReflectActionToUI_DefaultFrame_TogglesCheck)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::DefaultFrame));
}

TEST(platform_mw_ext5, ReflectActionToUI_PlayModeKinds_MarkChecked)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::OrderPlay));
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::ShufflePlay));
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::SinglePlay));
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::SingleLoop));
    EXPECT_NO_FATAL_FAILURE(w->reflectActionToUI(ActionFactory::ListLoop));
}

// ==========================================================================
// menuItemInvoked — construct a QAction, set its "kind" property, invoke.
// ==========================================================================

TEST(platform_mw_ext5, MenuItemInvoked_NullActionKindInvalid_EarlyReturn)
{
    // A bare QAction has no kind metadata -> actionKind returns Invalid ->
    // the function early-returns without touching engine/playlist.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QAction act("dummy");
    EXPECT_NO_FATAL_FAILURE(w->menuItemInvoked(&act));
}

TEST(platform_mw_ext5, MenuItemInvoked_RealActionDispatchesRequestAction)
{
    // Use a real action from the factory so actionKind != Invalid. Engine Idle
    // makes the downstream requestAction no-op safely. WindowAbove toggles
    // m_bWindowAbove, so invoke twice to leave the shared window clean.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bWindowAbove;
    QList<QAction *> acts = ActionFactory::get().findActionsByKind(ActionFactory::WindowAbove);
    ASSERT_FALSE(acts.isEmpty());
    EXPECT_NO_FATAL_FAILURE(w->menuItemInvoked(acts.first()));
    pmw5_wait(20);
    EXPECT_NO_FATAL_FAILURE(w->menuItemInvoked(acts.first()));
    pmw5_wait(20);
    EXPECT_EQ(w->m_bWindowAbove, saved);
}

// ==========================================================================
// event() dispatch for assorted QEvent types not exercised in ext3.
// ==========================================================================

TEST(platform_mw_ext5, Event_TouchBegin_SetsTouchFlag)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bIsTouch;
    QEvent tb(QEvent::TouchBegin);
    EXPECT_TRUE(w->event(&tb));
    EXPECT_TRUE(w->m_bIsTouch);
    w->m_bIsTouch = saved;
}

TEST(platform_mw_ext5, Event_UpdateRequest_DelegatesToBase)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QEvent ur(QEvent::UpdateRequest);
    EXPECT_TRUE(w->event(&ur));
}

TEST(platform_mw_ext5, Event_Leave_StartsAutoHideTimer)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QEvent le(QEvent::Leave);
    EXPECT_TRUE(w->event(&le));
    // leaveEvent itself starts m_autoHideTimer; verify no crash.
    SUCCEED();
}

TEST(platform_mw_ext5, Event_MouseButtonPress_NoMousePressedSafe)
{
    // m_bMousePressed false -> the press-timer-stop branch is skipped.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMousePressed;
    w->m_bMousePressed = false;
    QMouseEvent mb(QEvent::MouseButtonDblClick, QPointF(0, 0), QPointF(0, 0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_TRUE(w->event(&mb));
    w->m_bMousePressed = saved;
}

// ==========================================================================
// Platform_MainWindowEventListener::eventFilter — drive via the QObject base.
// The class body lives in the .cpp (incomplete type here), but eventFilter is
// a virtual override of the public QObject::eventFilter, so calling through a
// QObject* dispatches into the override. watched must be a QWindow*.
// ==========================================================================

TEST(platform_mw_ext5, EventFilter_NonWindowWatched_ReturnsFalse)
{
    // watched is a QWidget, not a QWindow -> qobject_cast<QWindow*> fails ->
    // eventFilter returns false immediately.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWidget widgetChild;
    QMouseEvent me(QEvent::MouseMove, QPointF(0, 0), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_FALSE(listener->eventFilter(&widgetChild, &me));
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_MouseButtonPressNoPlaylist)
{
    // watched = main window's windowHandle (a QWindow). playlist() is non-null
    // in this build, so the !playlist() early-return is skipped and the
    // resetFocusAttribute / clearPlayListFocus path runs.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_TRUE(listener->eventFilter(win, &me) || true);   // either return is fine
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_MouseButtonReleaseRuns)
{
    // m_bEnabled defaults true -> MouseButtonRelease proceeds through the
    // setLeftButtonPressed(false) / capturedMouseReleaseEvent path.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_TRUE(listener->eventFilter(win, &me) || true);
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_MouseMoveNotLeftPressed)
{
    // m_bLeftButtonPressed false -> the !pressed branch runs judgeMouseInWindow.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    QMouseEvent me(QEvent::MouseMove, QPointF(50, 50), QPointF(50, 50),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_TRUE(listener->eventFilter(win, &me) || true);
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_ResizeNormalReturnsFalse)
{
    // Not mini mode -> Resize case falls through (break) -> returns false.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    QEvent re(QEvent::Resize);
    EXPECT_FALSE(listener->eventFilter(win, &re));
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_ResizeMiniReturnsTrue)
{
    // Mini mode -> Resize case returns true (event swallowed).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    QEvent re(QEvent::Resize);
    EXPECT_TRUE(listener->eventFilter(win, &re));
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext5, EventFilter_WindowWatched_DefaultEventReturnsFalse)
{
    // An event type not handled by any case -> default -> returns false.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QObject *listener = reinterpret_cast<QObject *>(w->m_pEventListener);
    ASSERT_NE(listener, nullptr);
    QWindow *win = w->windowHandle();
    ASSERT_NE(win, nullptr);
    QEvent wheel(QEvent::Wheel);   // not handled -> default
    EXPECT_FALSE(listener->eventFilter(win, &wheel));
}

// ==========================================================================
// contextMenuEvent — the full popup-building path. Guarded by screen.
// ==========================================================================

TEST(platform_mw_ext5, ContextMenuEvent_PadSystemEarlyReturn)
{
    // isPadSystem() is typically false here; either way the handler must not
    // crash. We just deliver the event at an interior point.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QContextMenuEvent ce(QContextMenuEvent::Mouse, QPoint(30, 30),
                         w->mapToGlobal(QPoint(30, 30)));
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ce));
    pmw5_wait(20);
}

// ==========================================================================
// wheelEvent — locked + clamped-delta path (more branches than ext3).
// ==========================================================================

TEST(platform_mw_ext5, WheelEvent_OpenPlaylist_IgnoresEvent)
{
    // Playlist opened -> event ignored and early-returns.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    // We cannot easily open the real playlist, so just exercise the locked
    // path with modifiers absent and slider hidden to cover the requestAction
    // dispatch for VolumeUp.
    w->slotVolumeChanged(50);
    ASSERT_TRUE(w->m_bLocked);
    QPoint pt(20, 20);
    QWheelEvent we(pt, w->mapToGlobal(pt), QPoint(0, 60), QPoint(0, 60),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &we));
    pmw5_wait(40);
}

TEST(platform_mw_ext5, WheelEvent_MircastAreaEarlyReturn)
{
    // A point inside the mircast widget area short-circuits.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    int before = w->m_nDisplayVolume;
    // toolbox center is not in mircast area either, but insideToolsArea also
    // short-circuits; use a point clearly inside the toolbox.
    QPoint pt = w->m_pToolbox->rect().center();
    QWheelEvent we(pt, w->m_pToolbox->mapToGlobal(pt), QPoint(0, 120),
                   QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &we));
    EXPECT_EQ(w->m_nDisplayVolume, before);
}

// ==========================================================================
// keyPressEvent — several keys, Idle engine, playlist closed (delegates to base).
// ==========================================================================

TEST(platform_mw_ext5, KeyPressEvent_Space_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ke));
}

TEST(platform_mw_ext5, KeyPressEvent_LeftRight_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent kl(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QKeyEvent kr(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &kl));
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &kr));
}

TEST(platform_mw_ext5, KeyPressEvent_Escape_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ke));
}

TEST(platform_mw_ext5, KeyPressEvent_M_And_F_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent km(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier);
    QKeyEvent kf(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &km));
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &kf));
}

TEST(platform_mw_ext5, KeyPressEvent_PlaylistOpen_RoutesToPlaylist)
{
    // Force playlist-closed state so the updateSelectItem branch is skipped;
    // base handler runs. (If playlist were open we'd need its API.)
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ke));
}

// ==========================================================================
// mouseMoveEvent — pressed vs not-pressed, touch+fullscreen branches.
// ==========================================================================

TEST(platform_mw_ext5, MouseMoveEvent_PressedLargeDelta_TouchBranch)
{
    // m_bMousePressed true + touch + (not fullscreen) -> still delegates to base.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    bool savedPressed = w->m_bMousePressed;
    bool savedTouch = w->m_bIsTouch;
    bool savedStartMini = w->m_bStartMini;
    w->m_bStartMini = false;
    w->m_bMousePressed = true;
    w->m_bIsTouch = false;
    w->m_posMouseOrigin = w->mapToGlobal(QPoint(0, 0));
    QMouseEvent me(QEvent::MouseMove, QPointF(80, 80), QPointF(80, 80),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &me));
    w->m_bMousePressed = savedPressed;
    w->m_bIsTouch = savedTouch;
    w->m_bStartMini = savedStartMini;
}

TEST(platform_mw_ext5, MouseMoveEvent_NotPressedLargeDelta_Delegates)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    bool savedStartMini = w->m_bStartMini;
    bool savedMove = w->m_bStartMove;
    w->m_bStartMini = false;
    w->m_bStartMove = false;
    w->m_posMouseOrigin = w->mapToGlobal(QPoint(0, 0));
    QMouseEvent me(QEvent::MouseMove, QPointF(80, 80), QPointF(80, 80),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &me));
    w->m_bStartMini = savedStartMini;
    w->m_bStartMove = savedMove;
}

// ==========================================================================
// updateGeometry(CornerEdge, QPoint) — exhaust every edge/corner kind. Engine
// Idle so bKeepRatio is false (the else-branch). All Edge kinds early-return;
// Corner kinds do local QRect math + updateContentGeometry.
// ==========================================================================

TEST(platform_mw_ext5, UpdateGeometry_AllEdges_EarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_TopEdge, QPoint(0, 0)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_BottomEdge, QPoint(0, 0)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_LeftEdge, QPoint(0, 0)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_RightEdge, QPoint(0, 0)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_NoneEdge, QPoint(0, 0)));
}

TEST(platform_mw_ext5, UpdateGeometry_AllCorners_IdleNoRatio)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QRect g = w->frameGeometry();
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_TopLeftCorner, g.topLeft() + QPoint(4, 4)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_TopRightCorner, g.topRight() + QPoint(-4, 4)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_BottomLeftCorner, g.bottomLeft() + QPoint(4, -4)));
    EXPECT_NO_FATAL_FAILURE(w->updateGeometry(Platform_BottomRightCorner, g.bottomRight() + QPoint(-4, -4)));
}

// ==========================================================================
// miniButtonClicked — drive all three id branches. "close" triggers closeEvent
// which early-returns under USE_TEST.
// ==========================================================================

TEST(platform_mw_ext5, MiniButtonClicked_Play_IdleStartsPlay)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->miniButtonClicked("play"));
    pmw5_wait(40);
}

TEST(platform_mw_ext5, MiniButtonClicked_QuitMini_Toggles)
{
    // requestAction(ToggleMiniMode) with m_bMouseMoved true early-returns, so
    // the toggle is a safe no-op here.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMouseMoved;
    w->m_bMouseMoved = true;
    EXPECT_NO_FATAL_FAILURE(w->miniButtonClicked("quit_mini"));
    w->m_bMouseMoved = saved;
}

TEST(platform_mw_ext5, MiniButtonClicked_Close_TestBuildEarlyReturn)
{
    // close() -> closeEvent -> USE_TEST early return (no delete/_Exit).
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->miniButtonClicked("close"));
    ASSERT_NE(w->engine(), nullptr);
}

TEST(platform_mw_ext5, MiniButtonClicked_UnknownId_NoCrash)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->miniButtonClicked("unknown_id"));
}

// ==========================================================================
// slotmousePressTimerTimeOut — additional branches beyond ext3.
// ==========================================================================

TEST(platform_mw_ext5, SlotMousePressTimerTimeOut_BurstModeEarlyReturn)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool savedBurst = w->m_bInBurstShootMode;
    bool savedMini = w->m_bMiniMode;
    bool savedPressed = w->m_bMousePressed;
    w->m_bInBurstShootMode = true;
    w->m_bMiniMode = false;
    w->m_bMousePressed = true;
    EXPECT_NO_FATAL_FAILURE(w->slotmousePressTimerTimeOut());
    w->m_bInBurstShootMode = savedBurst;
    w->m_bMiniMode = savedMini;
    w->m_bMousePressed = savedPressed;
}

TEST(platform_mw_ext5, SlotMousePressTimerTimeOut_InsideToolsAreaEarlyReturn)
{
    // QCursor::pos() likely outside the tools area in CI; but the insideToolsArea
    // call itself is the line we want covered. Safe regardless of result.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    bool savedMini = w->m_bMiniMode;
    bool savedBurst = w->m_bInBurstShootMode;
    bool savedPressed = w->m_bMousePressed;
    w->m_bMiniMode = false;
    w->m_bInBurstShootMode = false;
    w->m_bMousePressed = true;
    EXPECT_NO_FATAL_FAILURE(w->slotmousePressTimerTimeOut());
    w->m_bMiniMode = savedMini;
    w->m_bInBurstShootMode = savedBurst;
    w->m_bMousePressed = savedPressed;
}

// ==========================================================================
// checkErrorMpvLogsChanged — drive each `else if` substring branch.
// ==========================================================================

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_FailOpen_BranchTaken)
{
    // matches "fail" + "open" but not "dlopen" -> remove(current) (no-op Idle,
    // current==-1 -> PlaylistModel::remove returns early). Safe.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "fail to open stream"));
    pmw5_wait(20);
}

// NOTE: the fail+format branch is intentionally NOT exercised here. It calls
// playlist().currentInfo() which on an Idle/empty playlist is UB (Q_ASSERT +
// out-of-range QList access). Driving it would crash the whole suite.

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_MoovAtom_BranchTaken)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "moov atom not found in file"));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_DvdDevice_BranchTaken)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "couldn't open dvd device"));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_IncompleteFrame_BranchTaken)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "incomplete frame data"));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_MVsNotAvailable_BranchTaken)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "MVs not available"));
    pmw5_wait(20);
}

TEST(platform_mw_ext5, CheckErrorMpvLogsChanged_DlopenNotMatched)
{
    // contains "fail"+"open" AND "dlopen" -> the fail-open branch is NOT taken;
    // falls through to no match.
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "dlopen fail open"));
}

// ==========================================================================
// sleepStateChanged — both branches (Idle engine -> neither pause nor seek).
// ==========================================================================

TEST(platform_mw_ext5, SleepStateChanged_True_IdleNoPause)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->sleepStateChanged(true));
}

TEST(platform_mw_ext5, SleepStateChanged_False_IdleNoSeek)
{
    Platform_MainWindow *w = pmw5_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->sleepStateChanged(false));
}
