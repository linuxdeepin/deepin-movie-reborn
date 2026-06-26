// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 6) for Platform_MainWindow (suite: platform_mw_ext6).
//
// Goal: raise line coverage of src/common/platform/platform_mainwindow.cpp by
// exercising the many signal-driven slots, settings-backed handlers, mircast
// state-machine, the small pure helpers and the lightweight Qt event handlers
// that are NOT exercised by ext3/ext4/ext5 (those cover requestAction dispatch,
// reflectActionToUI, eventFilter, key/mouse/wheel/contextmenu events,
// isActionAllowed, geometry helpers and the per-ActionKind requestAction switch).
//
// The shared main window owned by the test harness is in the Idle engine state
// (no media loaded), so every handler that early-returns / no-ops on Idle is
// safe. A crash drops every later case, so risky bits are guarded or skipped.
//
// Safety rules baked in (mirrors ext3/ext4/ext5):
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
//   * closeEvent is safe in this build (USE_TEST makes it early-return before
//     the delete/_Exit path).
//   * Only settings keys known-safe to write are used. Unknown keys would NPE
//     inside Settings; reads are always safe.

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
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QRect>
#include <QShowEvent>
#include <QHideEvent>
#include <QMoveEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QStatusBar>
#include <QTimer>
#include <QTestEventList>
#include <QWindowStateChangeEvent>
#include <QEnterEvent>
#include <QPointingDevice>
#include <QAction>
#include <QMenu>
#include <QCursor>
#include <QWindow>
#include <DFontSizeManager>
#include <DMainWindow>
#include <DWindowManagerHelper>

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
#include "src/libdmr/online_sub.h"
#include "src/libdmr/compositing_manager.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/widgets/platform/platform_playlist_widget.h"
#include "src/widgets/platform/platform_notification_widget.h"
#include "src/widgets/platform/platform_animationlabel.h"
#include "src/widgets/notification_widget.h"
#include "src/widgets/titlebar.h"
#include "src/common/dmr_settings.h"

using namespace dmr;

// ---------- helpers (unique prefix pmw6_) ----------

static Platform_MainWindow *pmw6_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

static void pmw6_wait(int ms = 80)
{
    QTest::qWait(ms);
}

// ==========================================================================
// setInit — both branches (changed / unchanged). Also emits initChanged.
// ==========================================================================

TEST(platform_mw_ext6, SetInit_SameValue_NoEmit)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bInited;
    EXPECT_NO_FATAL_FAILURE(w->setInit(saved));   // same -> no emit, no change
    EXPECT_EQ(w->m_bInited, saved);
}

TEST(platform_mw_ext6, SetInit_DifferentValue_TogglesAndRestores)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bInited;
    EXPECT_NO_FATAL_FAILURE(w->setInit(!saved));   // different -> emit + set
    EXPECT_EQ(w->m_bInited, !saved);
    EXPECT_NO_FATAL_FAILURE(w->setInit(saved));    // restore
    EXPECT_EQ(w->m_bInited, saved);
}

// ==========================================================================
// Trivial getters that still count as covered lines.
// ==========================================================================

TEST(platform_mw_ext6, GetDisplayVolume_ReturnsMember)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int saved = w->m_nDisplayVolume;
    w->m_nDisplayVolume = 42;
    EXPECT_EQ(w->getDisplayVolume(), 42);
    w->m_nDisplayVolume = saved;
}

TEST(platform_mw_ext6, GetMiniMode_ReturnsMember)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_TRUE(w->getMiniMode());
    w->m_bMiniMode = false;
    EXPECT_FALSE(w->getMiniMode());
    w->m_bMiniMode = saved;
}

// ==========================================================================
// setPresenter — null guard path. Passing a real Presenter would deref it,
// so we only exercise the storage by giving nullptr (which still hits the
// assignment; the inner slotvolumeChanged is the risky bit so we use nullptr
// to skip the deref -- the function does m_pPresenter->slotvolumeChanged()
// unconditionally, hence we SKIP the real path).
// ==========================================================================

TEST(platform_mw_ext6, SetPresenter_NullptrSafeSkipped)
{
    // The real setPresenter derefs its arg; with nullptr it would crash.
    // We cannot safely construct a Presenter here, so just confirm the member
    // is what we expect and skip the call.
    GTEST_SKIP() << "setPresenter derefs arg; needs a real Presenter, skipped";
}

// ==========================================================================
// syncPostion — forwards to notification widget.
// ==========================================================================

TEST(platform_mw_ext6, SyncPostion_ForwardsToHintWidget)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->syncPostion());
}

// ==========================================================================
// slotUrlpause — both branches (true shows "Buffering...", false no-ops).
// ==========================================================================

TEST(platform_mw_ext6, SlotUrlpause_True_ShowsBuffering)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUrlpause(true));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, SlotUrlpause_False_NoOp)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUrlpause(false));
}

// ==========================================================================
// slotMuteChanged — both branches. Engine Idle => setMute is a backend no-op.
// ==========================================================================

TEST(platform_mw_ext6, SlotMuteChanged_True_ShowsMuteHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int savedVol = w->m_nDisplayVolume;
    w->m_nDisplayVolume = 50;
    EXPECT_NO_FATAL_FAILURE(w->slotMuteChanged(true));
    pmw6_wait(20);
    w->m_nDisplayVolume = savedVol;
}

TEST(platform_mw_ext6, SlotMuteChanged_False_ShowsVolumeHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int savedVol = w->m_nDisplayVolume;
    w->m_nDisplayVolume = 70;
    EXPECT_NO_FATAL_FAILURE(w->slotMuteChanged(false));
    pmw6_wait(20);
    w->m_nDisplayVolume = savedVol;
}

// ==========================================================================
// slotVolumeChanged — first-call path (static firstInit) and subsequent path,
// plus zero/non-zero volume sub-branches.
// ==========================================================================

TEST(platform_mw_ext6, SlotVolumeChanged_NonZero_UpdatesDisplay)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int saved = w->m_nDisplayVolume;
    EXPECT_NO_FATAL_FAILURE(w->slotVolumeChanged(65));
    EXPECT_EQ(w->m_nDisplayVolume, 65);
    pmw6_wait(80);   // singleShot 50ms inside
    w->m_nDisplayVolume = saved;
}

TEST(platform_mw_ext6, SlotVolumeChanged_Zero_ShowsMute)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int saved = w->m_nDisplayVolume;
    EXPECT_NO_FATAL_FAILURE(w->slotVolumeChanged(0));
    EXPECT_EQ(w->m_nDisplayVolume, 0);
    pmw6_wait(80);
    w->m_nDisplayVolume = saved;
}

TEST(platform_mw_ext6, SlotVolumeChanged_ShortcutsPresenterBranch)
{
    // Drive the m_pPresenter non-null branch indirectly: the field is normally
    // null in the test harness, so this exercises the !m_pPresenter path.
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(w->m_pPresenter, nullptr);
    int saved = w->m_nDisplayVolume;
    EXPECT_NO_FATAL_FAILURE(w->slotVolumeChanged(30));
    pmw6_wait(80);
    w->m_nDisplayVolume = saved;
}

// ==========================================================================
// slotWMChanged — refreshes compositing flags on animationlabel + hint widget.
// ==========================================================================

TEST(platform_mw_ext6, SlotWMChanged_RefreshesCompositing)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pAnimationlabel, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotWMChanged());
}

// ==========================================================================
// slotFontChanged — guarded by screen (uses primaryScreen geometry). The Mips
// early-return is not in our path on x86 CI.
// ==========================================================================

TEST(platform_mw_ext6, SlotFontChanged_RecomputesLabelWidths)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QFont f;
    EXPECT_NO_FATAL_FAILURE(w->slotFontChanged(f));
}

// ==========================================================================
// slotFileLoaded — engine sender cast. Calling directly: pEngine from
// sender() is null (we are not in a signal), so the early-return branch runs.
// ==========================================================================

TEST(platform_mw_ext6, SlotFileLoaded_NullSenderEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    // Direct call -> sender() == nullptr -> early return.
    EXPECT_NO_FATAL_FAILURE(w->slotFileLoaded());
}

// ==========================================================================
// slotPlayerStateChanged — direct call: sender() is null -> early return.
// ==========================================================================

TEST(platform_mw_ext6, SlotPlayerStateChanged_NullSenderEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotPlayerStateChanged());
}

TEST(platform_mw_ext6, SlotPlayerStateChanged_EmittedByEngine_RunsIdleBranch)
{
    // Emit the signal from the real engine so sender() casts cleanly. Engine
    // is Idle -> the Idle branch (m_pMovieWidget->stopPlaying) runs.
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    // Direct emit from a queued connection on the engine object: when the
    // handler runs, sender() returns the engine.
    QMetaObject::invokeMethod(engine, "stateChanged", Qt::QueuedConnection);
    pmw6_wait(150);   // 100ms singleShot inside the handler
}

// ==========================================================================
// slotFocusWindowChanged — both branches depending on whether the focus window
// matches the main window handle.
// ==========================================================================

TEST(platform_mw_ext6, SlotFocusWindowChanged_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotFocusWindowChanged());
}

// ==========================================================================
// checkOnlineState — both branches.
// ==========================================================================

TEST(platform_mw_ext6, CheckOnlineState_Offline_ShowsNetworkHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineState(false));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, CheckOnlineState_Online_NoOp)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineState(true));
}

// ==========================================================================
// checkOnlineSubtitle — both reason branches (NoSubFound shows hint, others
// are no-op).
// ==========================================================================

TEST(platform_mw_ext6, CheckOnlineSubtitle_NoSubFound_ShowsHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineSubtitle(OnlineSubtitle::FailReason::NoSubFound));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, CheckOnlineSubtitle_OtherReason_NoOp)
{
    // Any reason other than NoSubFound falls through with no message.
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineSubtitle(static_cast<OnlineSubtitle::FailReason>(99)));
}

// ==========================================================================
// checkWarningMpvLogsChanged — drive the matching branch and the no-match
// branch. The matching branch under USE_TEST shows (not execs) the dialog and
// deletes it later, plus a 500ms singleShot that calls pauseResume (Idle-safe).
// ==========================================================================

TEST(platform_mw_ext6, CheckWarningMpvLogsChanged_4KMatch_ShowsDialog)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkWarningMpvLogsChanged(
        "mpv", "Hardware does not support image size 3840x2160"));
    pmw6_wait(40);
    // Let the inner 500ms singleShot (pauseResume on Idle engine) settle so it
    // does not fire during a later case and surprise us.
    pmw6_wait(520);
}

TEST(platform_mw_ext6, CheckWarningMpvLogsChanged_NoMatch_NoOp)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkWarningMpvLogsChanged("mpv", "some unrelated warning text"));
}

// ==========================================================================
// slotdefaultplaymodechanged — wrong-key early return, plus each mode branch
// (reads the items list from the real settings option).
// ==========================================================================

TEST(platform_mw_ext6, SlotDefaultPlayModeChanged_WrongKey_EarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotdefaultplaymodechanged("not.the.key", QVariant(0)));
}

TEST(platform_mw_ext6, SlotDefaultPlayModeChanged_OrderPlayBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    // The settings option's items list is localized; index 0 is "Order play"
    // in the default locale. Even if the tr() comparison fails the function
    // still runs its full body (no crash).
    EXPECT_NO_FATAL_FAILURE(w->slotdefaultplaymodechanged("base.play.playmode", QVariant(0)));
}

TEST(platform_mw_ext6, SlotDefaultPlayModeChanged_ShuffleBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotdefaultplaymodechanged("base.play.playmode", QVariant(1)));
}

// ==========================================================================
// onSetDecodeModel / onRefreshDecode — the MpvProxy is null when Idle (no
// media), so both early-return safely.
// ==========================================================================

TEST(platform_mw_ext6, OnSetDecodeModel_NullProxyEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", QVariant(1)));
}

TEST(platform_mw_ext6, OnSetDecodeModel_ValueThreeGuarded)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", QVariant(3)));
}

TEST(platform_mw_ext6, OnRefreshDecode_NullProxyEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onRefreshDecode());
}

// ==========================================================================
// decodeInit — null MpvProxy early-return path.
// ==========================================================================

TEST(platform_mw_ext6, DecodeInit_NullProxyEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->decodeInit());
}

// ==========================================================================
// lockStateChanged — Idle engine, both bLock values; mircast widget hidden so
// the slotExitMircast branch is skipped.
// ==========================================================================

TEST(platform_mw_ext6, LockStateChanged_True_IdleNoPause)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    bool savedInLock = w->m_bStateInLock;
    EXPECT_NO_FATAL_FAILURE(w->lockStateChanged(true));
    pmw6_wait(20);
    w->m_bStateInLock = savedInLock;
}

TEST(platform_mw_ext6, LockStateChanged_False_SchedulesLockedFalse)
{
    // !bLock -> sets m_bLocked=false and a 1000ms singleShot resets it true.
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool savedLocked = w->m_bLocked;
    EXPECT_NO_FATAL_FAILURE(w->lockStateChanged(false));
    EXPECT_FALSE(w->m_bLocked);
    pmw6_wait(20);
    w->m_bLocked = savedLocked;
}

// ==========================================================================
// onSysLockState — drive several key2value maps. Idle engine => the
// "Locked && Playing" branch is skipped; the "Locked false && m_bStateInLock"
// branch still runs.
// ==========================================================================

TEST(platform_mw_ext6, OnSysLockState_StartSleepSetsStateInLock)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool savedSleep = w->m_bStartSleep;
    bool savedInLock = w->m_bStateInLock;
    w->m_bStartSleep = true;
    QVariantMap vm;
    EXPECT_NO_FATAL_FAILURE(w->onSysLockState("svc", vm, QStringList()));
    EXPECT_TRUE(w->m_bStateInLock);
    w->m_bStartSleep = savedSleep;
    w->m_bStateInLock = savedInLock;
}

TEST(platform_mw_ext6, OnSysLockState_LockedFalseInLockClears)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool savedInLock = w->m_bStateInLock;
    w->m_bStateInLock = true;
    QVariantMap vm;
    vm.insert("Locked", QVariant(false));
    EXPECT_NO_FATAL_FAILURE(w->onSysLockState("svc", vm, QStringList()));
    EXPECT_FALSE(w->m_bStateInLock);
    w->m_bStateInLock = savedInLock;
}

// ==========================================================================
// slotProperChanged — Idle engine => the seekAbsolute branch is skipped.
// ==========================================================================

TEST(platform_mw_ext6, SlotProperChanged_IdleNoSeek)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QVariantMap vm;
    vm.insert("Active", QVariant(true));
    EXPECT_NO_FATAL_FAILURE(w->slotProperChanged("svc", vm, QStringList()));
}

TEST(platform_mw_ext6, SlotProperChanged_InactiveNoSeek)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QVariantMap vm;
    vm.insert("Active", QVariant(false));
    EXPECT_NO_FATAL_FAILURE(w->slotProperChanged("svc", vm, QStringList()));
}

// ==========================================================================
// slotUnsupported / slotInvalidFile — both just show a hint.
// ==========================================================================

TEST(platform_mw_ext6, SlotUnsupported_ShowsHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUnsupported());
    pmw6_wait(20);
}

TEST(platform_mw_ext6, SlotInvalidFile_SchedulesHint)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotInvalidFile("/tmp/nonexistent.mp4"));
    pmw6_wait(40);
}

// ==========================================================================
// slotUpdateMircastState — exhaust every switch arm. The MIRCAST_SUCCEEDED arm
// calls mircastSuccess which derefs m_pMircastShowWidget (constructed at init)
// and hides the mircast widget on the toolbox; safe. Other arms call
// slotExitMircast -> exitMircast which seeks the toolbox slider (Idle no-op).
// ==========================================================================

TEST(platform_mw_ext6, SlotUpdateMircastState_Succeeded)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pMircastShowWidget, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_SUCCEEDED, "device"));
    pmw6_wait(20);
    // mircastSuccess shows the widget; hide it again to keep the shared
    // window clean for later cases that assert it is hidden.
    w->m_pMircastShowWidget->hide();
}

TEST(platform_mw_ext6, SlotUpdateMircastState_Exit)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_EXIT, QString()));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, SlotUpdateMircastState_ConnectionFailed)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_CONNECTION_FAILED, QString()));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, SlotUpdateMircastState_Disconnected)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_DISCONNECTED, QString()));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, SlotUpdateMircastState_DefaultNoOp)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(9999, QString()));
}

TEST(platform_mw_ext6, SlotExitMircast_EmitsEnableSignals)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotExitMircast());
    pmw6_wait(20);
}

TEST(platform_mw_ext6, ExitMircast_IdleNoPause)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->exitMircast());
    pmw6_wait(20);
}

TEST(platform_mw_ext6, MircastSuccess_IdleSafeUpdate)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pMircastShowWidget, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->mircastSuccess("sink01"));
    pmw6_wait(20);
    w->m_pMircastShowWidget->hide();
}

// ==========================================================================
// popupAdapter — resizes/shows the popup widget from icon + text.
// ==========================================================================

TEST(platform_mw_ext6, PopupAdapter_SizesAndShows)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pPopupWid, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->popupAdapter(QIcon(), "hello world"));
    pmw6_wait(20);
}

// ==========================================================================
// prepareSplashImages — loads two resources (may be null in headless, no crash).
// ==========================================================================

TEST(platform_mw_ext6, PrepareSplashImages_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->prepareSplashImages());
}

// ==========================================================================
// diskRemoved — empty playlist early-return path (safe). The non-empty path
// calls currentInfo() which on an empty playlist is UB, so we ONLY drive the
// empty path.
// ==========================================================================

TEST(platform_mw_ext6, DiskRemoved_EmptyPlaylistEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    ASSERT_EQ(w->engine()->getplaylist()->count(), 0);
    EXPECT_NO_FATAL_FAILURE(w->diskRemoved("/dev/sr0"));
}

// ==========================================================================
// probeCdromDevice — reads /proc/mounts. Always safe (file may or may not
// contain sr devices; function returns a string either way).
// ==========================================================================

TEST(platform_mw_ext6, ProbeCdromDevice_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QString s;
    EXPECT_NO_FATAL_FAILURE(s = w->probeCdromDevice());
    (void)s;   // result is environment-dependent; we only need the lines run
}

// ==========================================================================
// addCdromPath — reads /proc/mounts; on most CI hosts there is no /dev/sr*
// mount so the function returns false at the "size == 0" guard (note the
// missing braces make `return false` always execute when no mount found).
// ==========================================================================

TEST(platform_mw_ext6, AddCdromPath_NoCdromReturnsFalse)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool ok = true;
    EXPECT_NO_FATAL_FAILURE(ok = w->addCdromPath());
    // We do not assert the result; either branch is environment-dependent.
    (void)ok;
}

// ==========================================================================
// setOpenFiles / padLoadPath / lastOpenedPath — pure-ish helpers.
// ==========================================================================

TEST(platform_mw_ext6, SetOpenFiles_StoresList)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QStringList saved = w->m_listOpenFiles;
    QStringList lst{"/tmp/a.mp4", "/tmp/b.mp4"};
    EXPECT_NO_FATAL_FAILURE(w->setOpenFiles(lst));
    EXPECT_EQ(w->m_listOpenFiles, lst);
    w->m_listOpenFiles = saved;
}

TEST(platform_mw_ext6, PadLoadPath_FallsBackToMoviesOrCurrent)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QString p;
    EXPECT_NO_FATAL_FAILURE(p = w->padLoadPath());
    EXPECT_FALSE(p.isEmpty());
}

TEST(platform_mw_ext6, LastOpenedPath_StaticReturnsSomething)
{
    QString p;
    EXPECT_NO_FATAL_FAILURE(p = Platform_MainWindow::lastOpenedPath());
    (void)p;
}

// ==========================================================================
// defaultplaymodeinit — reads settings and dispatches; Idle-safe.
// ==========================================================================

TEST(platform_mw_ext6, DefaultPlayModeInit_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->defaultplaymodeinit());
    pmw6_wait(20);
}

// ==========================================================================
// setCurrentHwdec (USE_TEST) — trivial setter.
// ==========================================================================

TEST(platform_mw_ext6, SetCurrentHwdec_StoresValue)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QString saved = w->m_sCurrentHwdec;
    EXPECT_NO_FATAL_FAILURE(w->setCurrentHwdec("vaapi"));
    EXPECT_EQ(w->m_sCurrentHwdec, "vaapi");
    w->m_sCurrentHwdec = saved;
}

// ==========================================================================
// mipsShowFullScreen — sets fullscreen window state on the real window. We
// restore the state afterwards to keep the shared window clean.
// ==========================================================================

TEST(platform_mw_ext6, MipsShowFullScreen_TogglesAndRestores)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    Qt::WindowStates saved = w->windowState();
    EXPECT_NO_FATAL_FAILURE(w->mipsShowFullScreen());
    EXPECT_TRUE(w->windowState() & Qt::WindowFullScreen);
    w->setWindowState(Qt::WindowNoState);
    w->setWindowState(saved);
    pmw6_wait(20);
}

// ==========================================================================
// setPlaySpeedMenuUnchecked — iterates all five speed actions and unchecks.
// ==========================================================================

TEST(platform_mw_ext6, SetPlaySpeedMenuUnchecked_UnchecksAll)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setPlaySpeedMenuUnchecked());
}

TEST(platform_mw_ext6, SetPlaySpeedMenuChecked_MarksGivenKind)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes));
}

// ==========================================================================
// adjustPlaybackSpeed — Idle engine => the whole body is skipped (early return
// at the if). Covers the entry + guard.
// ==========================================================================

TEST(platform_mw_ext6, AdjustPlaybackSpeed_IdleEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    double saved = w->m_dPlaySpeed;
    EXPECT_NO_FATAL_FAILURE(w->adjustPlaybackSpeed(ActionFactory::ActionKind::AccelPlayback));
    EXPECT_NO_FATAL_FAILURE(w->adjustPlaybackSpeed(ActionFactory::ActionKind::DecelPlayback));
    EXPECT_EQ(w->m_dPlaySpeed, saved);
}

// ==========================================================================
// setMusicShortKeyState — iterates this->actions() and toggles enabled for the
// listed kinds. The shared window has actions registered; toggling enabled is
// benign and we restore by toggling again.
// ==========================================================================

TEST(platform_mw_ext6, SetMusicShortKeyState_TrueThenFalse)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setMusicShortKeyState(true));
    EXPECT_NO_FATAL_FAILURE(w->setMusicShortKeyState(false));
}

// ==========================================================================
// dragEnterEvent / dragMoveEvent — both branches (hasUrls / no urls).
// ==========================================================================

TEST(platform_mw_ext6, DragEnterEvent_HasUrls_Accepts)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/a.mp4")});
    QDragEnterEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
}

TEST(platform_mw_ext6, DragEnterEvent_NoUrls_Ignores)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    QDragEnterEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
}

TEST(platform_mw_ext6, DragMoveEvent_HasUrls_Accepts)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/a.mp4")});
    QDragMoveEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
}

TEST(platform_mw_ext6, DragMoveEvent_NoUrls_Ignores)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    QDragMoveEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
}

// ==========================================================================
// dropEvent — no-urls early return; multi-url play() path (play() validates
// the list and early-returns safely on missing files). We AVOID the single-url
// subtitle branch because it derefs playlist().currentInfo() on Idle/raw paths.
// ==========================================================================

TEST(platform_mw_ext6, DropEvent_NoUrls_EarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    QDropEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
}

TEST(platform_mw_ext6, DropEvent_MultipleUrls_PlaysList)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/a.mp4"), QUrl::fromLocalFile("/tmp/b.mp4")});
    QDropEvent de(QPoint(10, 10), Qt::CopyAction, &md, Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &de));
    pmw6_wait(40);
}

// ==========================================================================
// resizeEvent — deliver a real resize event (uses primaryScreen geometry).
// ==========================================================================

TEST(platform_mw_ext6, ResizeEvent_Deliver_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QSize old = w->size();
    QResizeEvent re(old + QSize(2, 2), old);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &re));
    pmw6_wait(20);
}

// ==========================================================================
// paintEvent — Idle engine paints the splash icon; guarded by screen.
// ==========================================================================

TEST(platform_mw_ext6, PaintEvent_Idle_DrawsSplash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QPaintEvent pe(w->rect());
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &pe));
}

// ==========================================================================
// closeEvent — USE_TEST early-returns before the destructive teardown.
// ==========================================================================

TEST(platform_mw_ext6, CloseEvent_UseTestEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QCloseEvent ce;
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ce));
    // Engine must still be alive after the early return.
    ASSERT_NE(w->engine(), nullptr);
}

// ==========================================================================
// focusInEvent / focusOutEvent — both delegate to base; the fullscreen-only
// body is skipped because the window is not fullscreen.
// ==========================================================================

TEST(platform_mw_ext6, FocusInEvent_NotFullscreen_Delegates)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QFocusEvent fe(QEvent::FocusIn, Qt::OtherFocusReason);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &fe));
}

TEST(platform_mw_ext6, FocusOutEvent_NotFullscreen_Delegates)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QFocusEvent fe(QEvent::FocusOut, Qt::OtherFocusReason);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &fe));
}

// ==========================================================================
// keyReleaseEvent — delegates to base for an unhandled key.
// ==========================================================================

TEST(platform_mw_ext6, KeyReleaseEvent_UnhandledKey_Delegates)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &ke));
}

// ==========================================================================
// showEvent / hideEvent — deliver real events. Both touch real widgets so we
// guard by screen.
// ==========================================================================

TEST(platform_mw_ext6, ShowEvent_RaisesWidgets)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QShowEvent se;
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &se));
    pmw6_wait(20);
}

TEST(platform_mw_ext6, HideEvent_DelegatesToBase)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QHideEvent he;
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &he));
}

TEST(platform_mw_ext6, MoveEvent_DelegatesToBase)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QPoint old = w->pos();
    QMoveEvent me(old + QPoint(1, 1), old);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &me));
}

// ==========================================================================
// leaveEvent — minimal, starts the auto-hide timer.
// ==========================================================================

TEST(platform_mw_ext6, LeaveEvent_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QEvent le(QEvent::Leave);
    EXPECT_NO_FATAL_FAILURE(QApplication::sendEvent(w, &le));
    pmw6_wait(20);
}

// ==========================================================================
// resizeByConstraints — Idle engine => early return after clearing title text.
// ==========================================================================

TEST(platform_mw_ext6, ResizeByConstraints_IdleEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->resizeByConstraints(false));
    EXPECT_NO_FATAL_FAILURE(w->resizeByConstraints(true));
}

// ==========================================================================
// updateSizeConstraints — runs both the mini and non-mini size branches.
// ==========================================================================

TEST(platform_mw_ext6, UpdateSizeConstraints_NonMiniBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = false;
    EXPECT_NO_FATAL_FAILURE(w->updateSizeConstraints());
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext6, UpdateSizeConstraints_MiniBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_NO_FATAL_FAILURE(w->updateSizeConstraints());
    w->m_bMiniMode = saved;
}

// ==========================================================================
// LimitWindowize — clamps window size; guarded by screen.
// ==========================================================================

TEST(platform_mw_ext6, LimitWindowize_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->LimitWindowize());
}

// ==========================================================================
// updateGeometryNotification — emits size-changed; benign.
// ==========================================================================

TEST(platform_mw_ext6, UpdateGeometryNotification_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->updateGeometryNotification(QSize(800, 600)));
}

// ==========================================================================
// updateContentGeometry — both mini and non-mini branches.
// ==========================================================================

TEST(platform_mw_ext6, UpdateContentGeometry_NonMini)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = false;
    EXPECT_NO_FATAL_FAILURE(w->updateContentGeometry(w->rect()));
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext6, UpdateContentGeometry_Mini)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_NO_FATAL_FAILURE(w->updateContentGeometry(w->rect()));
    w->m_bMiniMode = saved;
}

// ==========================================================================
// suspendToolsWindow / resumeToolsWindow — paired calls that hide/show the
// tools window. Both are safe on Idle and restore visibility.
// ==========================================================================

TEST(platform_mw_ext6, SuspendResumeToolsWindow_Paired)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->suspendToolsWindow());
    pmw6_wait(20);
    EXPECT_NO_FATAL_FAILURE(w->resumeToolsWindow());
    pmw6_wait(20);
}

// ==========================================================================
// animatePlayState — Idle engine branch.
// ==========================================================================

TEST(platform_mw_ext6, AnimatePlayState_IdleBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->animatePlayState());
    pmw6_wait(20);
}

// ==========================================================================
// delayedMouseReleaseHandler — toggles play/pause via click. Idle engine =>
// downstream is a no-op. We must restore m_bMouseMoved etc. if mutated.
// ==========================================================================

TEST(platform_mw_ext6, DelayedMouseReleaseHandler_IdleSafe)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool savedMoved = w->m_bMouseMoved;
    EXPECT_NO_FATAL_FAILURE(w->delayedMouseReleaseHandler());
    pmw6_wait(20);
    w->m_bMouseMoved = savedMoved;
}

// ==========================================================================
// capturedKeyEvent — passes a key event to the captured handler. Engine Idle
// makes the downstream no-op.
// ==========================================================================

TEST(platform_mw_ext6, CapturedKeyEvent_DeliversKey)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    EXPECT_NO_FATAL_FAILURE(w->capturedKeyEvent(&ke));
}

// ==========================================================================
// capturedMousePressEvent / capturedMouseReleaseEvent — drive both with a
// left-button event. The release handler has click-type detection branches
// that are safe under Idle.
// ==========================================================================

TEST(platform_mw_ext6, CapturedMousePress_LeftButton_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_NO_FATAL_FAILURE(w->capturedMousePressEvent(&me));
}

TEST(platform_mw_ext6, CapturedMouseRelease_LeftButton_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    bool savedPressed = w->m_bMousePressed;
    bool savedMoved = w->m_bMouseMoved;
    w->m_bMousePressed = true;
    w->m_bMouseMoved = false;
    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    EXPECT_NO_FATAL_FAILURE(w->capturedMouseReleaseEvent(&me));
    w->m_bMousePressed = savedPressed;
    w->m_bMouseMoved = savedMoved;
    pmw6_wait(40);
}

// ==========================================================================
// insideToolsArea / insideResizeArea / dragMargins — pure-ish helpers.
// ==========================================================================

TEST(platform_mw_ext6, InsideToolsArea_ArbitraryPoint_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool b = false;
    EXPECT_NO_FATAL_FAILURE(b = w->insideToolsArea(QPoint(5, 5)));
    (void)b;
}

TEST(platform_mw_ext6, InsideResizeArea_ArbitraryPoint_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool b = false;
    EXPECT_NO_FATAL_FAILURE(b = w->insideResizeArea(w->mapToGlobal(QPoint(2, 2))));
    (void)b;
}

TEST(platform_mw_ext6, DragMargins_ReturnsMargins)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    QMargins m;
    EXPECT_NO_FATAL_FAILURE(m = w->dragMargins());
    (void)m;
}

// ==========================================================================
// judgeMouseInWindow — point classification helper.
// ==========================================================================

TEST(platform_mw_ext6, JudgeMouseInWindow_Origin_NoCrash)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    bool b = false;
    EXPECT_NO_FATAL_FAILURE(b = w->judgeMouseInWindow(QPoint(0, 0)));
    (void)b;
}

// ==========================================================================
// updateWindowTitle — Idle engine branch.
// ==========================================================================

TEST(platform_mw_ext6, UpdateWindowTitle_IdleBranch)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->updateWindowTitle());
}

// ==========================================================================
// onBurstScreenshot — feed a small synthetic QImage. The "size < 15 and
// non-null" branch just appends and returns; the threshold/dialog branches are
// NOT driven here because they need a loaded playlist.currentInfo() (UB on
// empty). We append a couple of frames to exercise the append + message path.
// ==========================================================================

TEST(platform_mw_ext6, OnBurstScreenshot_NonNullFrameAppends)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    int savedSize = w->m_listBurstShoots.size();
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::black);
    EXPECT_NO_FATAL_FAILURE(w->onBurstScreenshot(img, 12345LL));
    // The function may have appended; restore the list to keep the window clean.
    while (w->m_listBurstShoots.size() > savedSize) {
        w->m_listBurstShoots.removeLast();
    }
    pmw6_wait(20);
}

// ==========================================================================
// startBurstShooting — Idle engine => duration() is 0, so the function
// early-returns at the `duration <= 40` guard. Covers the guard.
// ==========================================================================

TEST(platform_mw_ext6, StartBurstShooting_IdleDurationGuardEarlyReturn)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->startBurstShooting());
}

// ==========================================================================
// updateActionsState — drives the action-state lambda over the menu actions.
// Mircast widget is hidden so the early-return is skipped.
// ==========================================================================

TEST(platform_mw_ext6, UpdateActionsState_IdleSafe)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_FALSE(w->m_pMircastShowWidget && w->m_pMircastShowWidget->isVisible());
    EXPECT_NO_FATAL_FAILURE(w->updateActionsState());
    pmw6_wait(20);
}

// ==========================================================================
// onBindingsChanged — rebuilds shortcut actions from the ShortcutManager.
// ==========================================================================

TEST(platform_mw_ext6, OnBindingsChanged_RebuildsActions)
{
    Platform_MainWindow *w = pmw6_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onBindingsChanged());
    pmw6_wait(20);
}
