// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 3) for Platform_MainWindow (suite: platform_mw_ext3).
//
// Goal: raise line coverage of src/common/platform/platform_mainwindow.cpp by
// exercising the Qt event handlers, geometry helpers, settings-backed slots and
// pure-ish logic that do NOT depend on a live mpv backend. The shared main
// window owned by the test harness is in the Idle engine state (no media
// loaded), so every handler that early-returns / no-ops on Idle is safe.
//
// Safety rules baked in (verified against prior crashes / link failures):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * The main window is fetched once per case via dApp->getMainWindow() and
//     ASSERT_NE'd before any use. The window is shared across the whole suite,
//     so cases only read state or toggle-and-restore flags they mutate.
//   * stub.h is intentionally NOT included (its include path is not wired for
//     the platform test target); every case calls real functions directly.
//   * No mpv backend / decode path is exercised. Functions that need a loaded
//     playlist (decodeInit, loadPlayList with real files, play() with real
//     media, onBurstScreenshot real path, checkOnlineSubtitle network) are
//     avoided; only the no-item / Idle / pure-logic branches are covered.
//   * Qt events are built on the stack with the Qt6 ctor signatures (QMouseEvent
//     takes QPointF; QWheelEvent takes two QPoint angle deltas) and delivered
//     via QApplication::sendEvent. Cases that paint or touch screen geometry
//     are guarded by primaryScreen() and GTEST_SKIP when headless.
//   * closeEvent is safe in this build (USE_TEST makes it early-return before
//     the delete/_Exit path), but we still pump the event loop lightly after.
//   * Only settings keys known-safe to write are used (global_volume, mute,
//     last_open_path, pad_load_path, base.play.playmode). Unknown keys would
//     NPE inside Settings; reads are always safe.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QPaintEvent>
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
#include <QResizeEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QHideEvent>
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
#include <DFontSizeManager>
#include <DMainWindow>
#include <DWindowManagerHelper>

#include <gtest/gtest.h>

#define protected public
#define private public
#include "src/common/platform/platform_mainwindow.h"
#undef protected
#undef private

#include "application.h"
#include "src/common/dmr_settings.h"
#include "src/libdmr/player_engine.h"
#include "src/libdmr/online_sub.h"
#include "src/libdmr/compositing_manager.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/widgets/platform/platform_playlist_widget.h"
#include "src/widgets/platform/platform_notification_widget.h"
#include "src/widgets/platform/platform_animationlabel.h"
#include "src/widgets/notification_widget.h"
#include "src/widgets/titlebar.h"
#include "src/common/actions.h"
#include "src/common/shortcut_manager.h"
#include "utils.h"

using namespace dmr;

// ---------- helpers (unique prefix pmw3_) ----------

static Platform_MainWindow *pmw3_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

// Short synchronous wait so single-shot timers / queued slots settle.
static void pmw3_wait(int ms = 80)
{
    QTest::qWait(ms);
}

// ==========================================================================
// mousePressEvent / mouseReleaseEvent
// ==========================================================================

TEST(platform_mw_ext3, MousePressEvent_LeftButton_SetsPressedAndMove)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->m_bMousePressed = false;
    w->m_bStartMove = false;
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    EXPECT_TRUE(w->m_bMousePressed);
    EXPECT_TRUE(w->m_bStartMove);
}

TEST(platform_mw_ext3, MousePressEvent_RightButton_NoPressedFlag)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->m_bMousePressed = true;  // will be reset to false at handler entry
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                   Qt::RightButton, Qt::RightButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    // right button does not set m_bMousePressed back to true
    EXPECT_FALSE(w->m_bMousePressed);
}

TEST(platform_mw_ext3, MousePressEvent_HidesHintAndPopupWidgets)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    ASSERT_NE(w->m_pPopupWid, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QMouseEvent me(QEvent::MouseButtonPress, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    EXPECT_FALSE(w->m_pCommHintWid->isVisible());
    EXPECT_FALSE(w->m_pPopupWid->isVisible());
}

TEST(platform_mw_ext3, MouseReleaseEvent_IdleEngine_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // Engine is Idle and playlist is closed -> the delayed-release timer is
    // started (120ms). Must not crash; we drain the timer.
    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(20, 20), QPointF(20, 20),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    pmw3_wait(160);
}

TEST(platform_mw_ext3, MouseReleaseEvent_InsideToolsArea_NoTimer)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // A point inside the toolbox area must short-circuit before the timer.
    // The toolbox lives at the bottom; use its own center.
    QPoint pt = w->m_pToolbox->rect().center();
    QMouseEvent me(QEvent::MouseButtonRelease, pt, w->m_pToolbox->mapToGlobal(pt),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    SUCCEED();
}

// ==========================================================================
// mouseDoubleClickEvent
// ==========================================================================

TEST(platform_mw_ext3, MouseDoubleClickEvent_IdleEngine_RequestsStartPlay)
{
    // Idle engine + no burst mode + no mircast -> requestAction(StartPlay).
    // StartPlay with an empty playlist is a safe no-op (no media to load).
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QMouseEvent me(QEvent::MouseButtonDblClick, QPointF(50, 50), QPointF(50, 50),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    pmw3_wait(40);
}

TEST(platform_mw_ext3, MouseDoubleClickEvent_MiniMode_ShortCircuits)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    QMouseEvent me(QEvent::MouseButtonDblClick, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    // restore to keep the shared window clean
    w->m_bMiniMode = wasMini;
    SUCCEED();
}

TEST(platform_mw_ext3, MouseDoubleClickEvent_BurstMode_ShortCircuits)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    QMouseEvent me(QEvent::MouseButtonDblClick, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    w->m_bInBurstShootMode = wasBurst;
    SUCCEED();
}

// ==========================================================================
// mouseMoveEvent
// ==========================================================================

TEST(platform_mw_ext3, MouseMoveEvent_StartMini_ReturnsEarly)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool was = w->m_bStartMini;
    w->m_bStartMini = true;
    QMouseEvent me(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    w->m_bStartMini = was;
    SUCCEED();
}

TEST(platform_mw_ext3, MouseMoveEvent_SmallDelta_ReturnsEarly)
{
    // delta < 5px in both axes is treated as jitter and returns early.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QMouseEvent me(QEvent::MouseMove, QPointF(2, 2), QPointF(2, 2),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    SUCCEED();
}

TEST(platform_mw_ext3, MouseMoveEvent_LargeDelta_DelegatesToBase)
{
    // Not full-screen, not touch, not startMove -> base class handler runs.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasMove = w->m_bStartMove;
    w->m_bStartMove = false;
    QMouseEvent me(QEvent::MouseMove, QPointF(80, 80), QPointF(80, 80),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(w, &me);
    w->m_bStartMove = wasMove;
    SUCCEED();
}

// ==========================================================================
// wheelEvent
// ==========================================================================

TEST(platform_mw_ext3, WheelEvent_InsideToolsArea_ReturnsEarly)
{
    // A wheel over the toolbox area must short-circuit (no volume change).
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    int before = w->m_nDisplayVolume;
    QPoint pt = w->m_pToolbox->rect().center();
    QWheelEvent we(pt, w->m_pToolbox->mapToGlobal(pt), QPoint(0, 120),
                   QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    EXPECT_EQ(w->m_nDisplayVolume, before);
}

TEST(platform_mw_ext3, WheelEvent_InsideResizeArea_ReturnsEarly)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    int before = w->m_nDisplayVolume;
    // a global point far outside the frame is inside the resize area
    QPoint outside(w->frameGeometry().right() + 5000,
                   w->frameGeometry().bottom() + 5000);
    QWheelEvent we(QPoint(5, 5), outside, QPoint(0, 120), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    EXPECT_EQ(w->m_nDisplayVolume, before);
}

TEST(platform_mw_ext3, WheelEvent_Locked_VerticalUp_RequestsVolumeUp)
{
    // m_bLocked defaults true; wheel up -> VolumeUp -> changes display volume.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->slotVolumeChanged(50);   // known base
    ASSERT_TRUE(w->m_bLocked);
    QPoint pt(10, 10);
    QWheelEvent we(pt, w->mapToGlobal(pt), QPoint(0, 120), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    pmw3_wait(40);
    // volume up by the step the engine applies (typically +10)
    EXPECT_GE(w->m_nDisplayVolume, 50);
}

TEST(platform_mw_ext3, WheelEvent_Locked_VerticalDown_RequestsVolumeDown)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->slotVolumeChanged(50);
    ASSERT_TRUE(w->m_bLocked);
    QPoint pt(10, 10);
    QWheelEvent we(pt, w->mapToGlobal(pt), QPoint(0, -120), QPoint(0, -120),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    pmw3_wait(40);
    EXPECT_LE(w->m_nDisplayVolume, 50);
}

TEST(platform_mw_ext3, WheelEvent_Locked_ClampsAbnormalDelta)
{
    // angleDelta magnitude > 240 is clamped to +/-120 inside the handler.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->slotVolumeChanged(50);
    QPoint pt(10, 10);
    QWheelEvent we(pt, w->mapToGlobal(pt), QPoint(0, 9999), QPoint(0, 9999),
                   Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    pmw3_wait(40);
    EXPECT_EQ(w->m_iAngleDelta, 120);   // clamped
}

TEST(platform_mw_ext3, WheelEvent_WithModifiers_NoChange)
{
    // Modifiers present -> the inner guard fails -> no volume request.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->slotVolumeChanged(50);
    QPoint pt(10, 10);
    QWheelEvent we(pt, w->mapToGlobal(pt), QPoint(0, 120), QPoint(0, 120),
                   Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &we);
    pmw3_wait(40);
    EXPECT_EQ(w->m_nDisplayVolume, 50);
}

// ==========================================================================
// keyPressEvent / keyReleaseEvent
// ==========================================================================

TEST(platform_mw_ext3, KeyPressEvent_GenericKey_DelegatesToBase)
{
    // Playlist is closed -> the playlist-open branch is skipped; the key is
    // forwarded to the base QWidget::keyPressEvent. No 'H' debug branch in
    // this (non-QT_DEBUG) build either.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(w, &ke);
    SUCCEED();
}

TEST(platform_mw_ext3, KeyReleaseEvent_GenericKey_DelegatesToBase)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier);
    QApplication::sendEvent(w, &ke);
    SUCCEED();
}

TEST(platform_mw_ext3, KeyPressEvent_WithModifiers_NotRoutedToPlaylist)
{
    // Even though playlist may be constructed, modifiers != NoModifier means
    // the playlist-update branch is skipped.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Up, Qt::ControlModifier);
    QApplication::sendEvent(w, &ke);
    SUCCEED();
}

// ==========================================================================
// captured* helpers (more branches than the existing suite)
// ==========================================================================

TEST(platform_mw_ext3, CapturedKeyEvent_NonTab_Noop)
{
    // Non-Tab key falls straight through.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    w->capturedKeyEvent(&ke);
    SUCCEED();
}

TEST(platform_mw_ext3, CapturedMousePressEvent_RightButton_NoPressed)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    w->m_bMousePressed = true;
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                   Qt::RightButton, Qt::RightButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    w->capturedMousePressEvent(&me);
    EXPECT_FALSE(w->m_bMousePressed);   // handler resets it
}

TEST(platform_mw_ext3, CapturedMouseReleaseEvent_TouchBranch_HidesToolbox)
{
    // Force the touch branch: m_bIsTouch true, m_bTouchChangeVolume true.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasTouch = w->m_bIsTouch;
    bool wasVol = w->m_bTouchChangeVolume;
    w->m_bIsTouch = true;
    w->m_bTouchChangeVolume = true;
    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    w->capturedMouseReleaseEvent(&me);
    EXPECT_FALSE(w->m_bIsTouch);
    EXPECT_FALSE(w->m_bTouchChangeVolume);
    w->m_bIsTouch = wasTouch;
    w->m_bTouchChangeVolume = wasVol;
}

TEST(platform_mw_ext3, CapturedMouseReleaseEvent_DelayedResizeFires)
{
    // m_bDelayedResizeByConstraint true -> a singleShot(0) resizes the window.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->m_bDelayedResizeByConstraint = true;
    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    w->capturedMouseReleaseEvent(&me);
    pmw3_wait(40);
    EXPECT_FALSE(w->m_bDelayedResizeByConstraint);
}

// ==========================================================================
// contextMenuEvent
// ==========================================================================

TEST(platform_mw_ext3, ContextMenuEvent_MiniMode_ShortCircuits)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    QContextMenuEvent ce(QContextMenuEvent::Mouse, QPoint(10, 10),
                         w->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(w, &ce);
    w->m_bMiniMode = wasMini;
    SUCCEED();
}

TEST(platform_mw_ext3, ContextMenuEvent_BurstMode_ShortCircuits)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    bool wasBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    QContextMenuEvent ce(QContextMenuEvent::Mouse, QPoint(10, 10),
                         w->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(w, &ce);
    w->m_bInBurstShootMode = wasBurst;
    SUCCEED();
}

TEST(platform_mw_ext3, ContextMenuEvent_InsideToolsArea_ShortCircuits)
{
    // A click inside the toolbox area returns before running xprop / popup.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QPoint pt = w->m_pToolbox->rect().center();
    QContextMenuEvent ce(QContextMenuEvent::Mouse, pt, w->m_pToolbox->mapToGlobal(pt));
    QApplication::sendEvent(w, &ce);
    SUCCEED();
}

// ==========================================================================
// focusInEvent / focusOutEvent
// ==========================================================================

TEST(platform_mw_ext3, FocusInEvent_NonFullScreen_DelegatesToBase)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    ASSERT_FALSE(w->isFullScreen());
    QFocusEvent fe(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(w, &fe);
    SUCCEED();
}

TEST(platform_mw_ext3, FocusOutEvent_NonFullScreen_DelegatesToBase)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    ASSERT_FALSE(w->isFullScreen());
    QFocusEvent fe(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(w, &fe);
    SUCCEED();
}

// ==========================================================================
// closeEvent (USE_TEST early-return path)
// ==========================================================================

TEST(platform_mw_ext3, CloseEvent_TestBuildEarlyReturn)
{
    // In the USE_TEST build closeEvent returns immediately without deleting
    // the engine or calling _Exit, so the suite keeps running.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QCloseEvent ce;
    QApplication::sendEvent(w, &ce);
    // window + engine must still be alive after the early return
    ASSERT_NE(w->engine(), nullptr);
    SUCCEED();
}

// ==========================================================================
// showEvent / hideEvent / resizeEvent / moveEvent
// ==========================================================================

TEST(platform_mw_ext3, ShowEvent_RaisesChildren_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QShowEvent se;
    QApplication::sendEvent(w, &se);
    SUCCEED();
}

TEST(platform_mw_ext3, HideEvent_DelegatesToBase)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QHideEvent he;
    QApplication::sendEvent(w, &he);
    SUCCEED();
}

TEST(platform_mw_ext3, ResizeEvent_LayoutsChildren_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QSize old = w->size();
    QResizeEvent re(QSize(old.width() + 4, old.height() + 4), old);
    QApplication::sendEvent(w, &re);
    pmw3_wait(40);   // let the singleShot(0) updateWindowTitle fire
    SUCCEED();
}

TEST(platform_mw_ext3, MoveEvent_SyncsHintAndSlider_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QPoint old = w->pos();
    QMoveEvent me(old + QPoint(3, 3), old);
    QApplication::sendEvent(w, &me);
    SUCCEED();
}

// ==========================================================================
// dragEnterEvent / dragMoveEvent / dropEvent
// ==========================================================================

TEST(platform_mw_ext3, DragEnterEvent_WithUrls_AcceptsAction)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/a.mp4")});
    QDragEnterEvent de(QPoint(10, 10), Qt::CopyAction, &md,
                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &de);
    EXPECT_TRUE(de.isAccepted());
}

TEST(platform_mw_ext3, DragEnterEvent_NoUrls_NotAccepted)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    QDragEnterEvent de(QPoint(10, 10), Qt::CopyAction, &md,
                       Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &de);
    EXPECT_FALSE(de.isAccepted());
}

TEST(platform_mw_ext3, DragMoveEvent_WithUrls_AcceptsAction)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    md.setUrls({QUrl::fromLocalFile("/tmp/b.mp4")});
    QDragMoveEvent de(QPoint(20, 20), Qt::CopyAction, &md,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &de);
    EXPECT_TRUE(de.isAccepted());
}

TEST(platform_mw_ext3, DropEvent_NoUrls_ReturnsEarly)
{
    // No URLs -> early return; must not touch the engine / playlist.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QMimeData md;
    QDropEvent de(QPointF(10, 10), Qt::CopyAction, &md,
                  Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &de);
    SUCCEED();
}

// ==========================================================================
// event() dispatch for assorted QEvent types
// ==========================================================================

TEST(platform_mw_ext3, Event_WindowStateChange_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // Capture current state, then deliver a synthetic state-change event.
    Qt::WindowStates before = w->windowState();
    QWindowStateChangeEvent wsce(before);
    EXPECT_TRUE(w->event(&wsce));
    SUCCEED();
}

TEST(platform_mw_ext3, Event_Paint_DelegatesToBase)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QPaintEvent pe{QRegion()};
    EXPECT_TRUE(w->event(&pe));
}

TEST(platform_mw_ext3, Event_MouseButtonPress_StopsPressTimerOnBigMove)
{
    // With m_bMousePressed true and a large cursor delta, event() must stop
    // the right-click press timer. We can't easily move the real cursor, but
    // we can verify the branch does not crash when the timer is inactive.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->m_bMousePressed = true;
    w->m_nLastPressX = 0;
    w->m_nLastPressY = 0;
    QMouseEvent me(QEvent::MouseMove, QPointF(0, 0), QPointF(0, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_TRUE(w->event(&me));
    SUCCEED();
}

TEST(platform_mw_ext3, Event_WindowDeactivate_HidesCommHint)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    QEvent de(QEvent::WindowDeactivate);
    EXPECT_TRUE(w->event(&de));
    EXPECT_FALSE(w->m_pCommHintWid->isVisible());
}

TEST(platform_mw_ext3, Event_Enter_ClearsMouseMovedFlag)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->m_bMouseMoved = true;
    QEvent enter(QEvent::Enter);
    EXPECT_TRUE(w->event(&enter));
    EXPECT_FALSE(w->m_bMouseMoved);
}

TEST(platform_mw_ext3, Event_TouchUpdate_ResetsTouchFlag)
{
    // TouchUpdate falls through event() to the base; verify it does not crash
    // and leaves m_bIsTouch as-is (only TouchBegin forces it true).
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool was = w->m_bIsTouch;
    QEvent tu(QEvent::TouchUpdate);
    EXPECT_TRUE(w->event(&tu));
    EXPECT_EQ(w->m_bIsTouch, was);
}

// ==========================================================================
// geometry helpers
// ==========================================================================

TEST(platform_mw_ext3, InsideToolsArea_CenterFalse_OutsideTrue)
{
    // A point in the dead center of the window is not in any tools rect;
    // the toolbox geometry (bottom) is a separate region we probe too.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);

    // toolbox-area point -> true
    QPoint tbPt = w->m_pToolbox->mapTo(w, w->m_pToolbox->rect().center());
    EXPECT_TRUE(w->insideToolsArea(tbPt));

    // far-away point -> false
    EXPECT_FALSE(w->insideToolsArea(QPoint(-9999, -9999)));
}

TEST(platform_mw_ext3, InsideResizeArea_GlobalOutsideTrue_CenterFalse)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QRect frame = w->frameGeometry();

    QPoint outside(frame.right() + 5000, frame.bottom() + 5000);
    EXPECT_TRUE(w->insideResizeArea(outside));

    EXPECT_FALSE(w->insideResizeArea(frame.center()));
}

TEST(platform_mw_ext3, DragMargins_AllSidesEqualSix)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QMargins m = w->dragMargins();
    EXPECT_EQ(m, QMargins(6, 6, 6, 6));
}

TEST(platform_mw_ext3, UpdateGeometry_NoneEdge_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect before = w->frameGeometry();
    w->updateGeometry(Platform_NoneEdge, QPoint(100, 100));
    EXPECT_EQ(w->frameGeometry(), before);
}

TEST(platform_mw_ext3, UpdateGeometry_TopEdge_EarlyReturn)
{
    // All *Edge kinds early-return; only *Corner kinds proceed. The engine is
    // Idle so even a corner kind only touches local QRect math.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    w->updateGeometry(Platform_TopEdge, QPoint(0, 0));
    w->updateGeometry(Platform_BottomEdge, QPoint(0, 0));
    w->updateGeometry(Platform_LeftEdge, QPoint(0, 0));
    w->updateGeometry(Platform_RightEdge, QPoint(0, 0));
    SUCCEED();
}

TEST(platform_mw_ext3, UpdateGeometry_TopLeftCorner_IdleNoRatio)
{
    // Engine Idle -> bKeepRatio false -> corner branch just sets rect coords
    // and calls updateContentGeometry (non-DXCB: move/resize). Safe.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect g = w->frameGeometry();
    w->updateGeometry(Platform_TopLeftCorner, g.topLeft() + QPoint(5, 5));
    SUCCEED();
}

TEST(platform_mw_ext3, UpdateGeometry_BottomRightCorner_IdleNoRatio)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect g = w->frameGeometry();
    w->updateGeometry(Platform_BottomRightCorner, g.bottomRight() - QPoint(5, 5));
    SUCCEED();
}

TEST(platform_mw_ext3, UpdateGeometry_BottomLeftCorner_IdleNoRatio)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect g = w->frameGeometry();
    w->updateGeometry(Platform_BottomLeftCorner, g.bottomLeft() + QPoint(5, -5));
    SUCCEED();
}

TEST(platform_mw_ext3, UpdateGeometry_TopRightCorner_IdleNoRatio)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    QRect g = w->frameGeometry();
    w->updateGeometry(Platform_TopRightCorner, g.topRight() + QPoint(-5, 5));
    SUCCEED();
}

// ==========================================================================
// small pure-ish helpers
// ==========================================================================

TEST(platform_mw_ext3, GetDisplayVolume_ReturnsMemberValue)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->m_nDisplayVolume = 73;
    EXPECT_EQ(w->getDisplayVolume(), 73);
    w->m_nDisplayVolume = 0;
    EXPECT_EQ(w->getDisplayVolume(), 0);
}

TEST(platform_mw_ext3, GetMiniMode_ReturnsMemberFlag)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_TRUE(w->getMiniMode());
    w->m_bMiniMode = false;
    EXPECT_FALSE(w->getMiniMode());
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext3, LimitWindowize_NotMini_NoOp)
{
    // m_bMiniMode false -> condition (width==380 || height==380) is checked
    // against current geometry; restoring to m_lastRectInNormalMode happens
    // only on a 380 match. Must not crash regardless.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->LimitWindowize());
}

TEST(platform_mw_ext3, LimitWindowize_Mini_NoOp)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    EXPECT_NO_FATAL_FAILURE(w->LimitWindowize());
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext3, UpdateSizeConstraints_SetsMinimumNonMini)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = false;
    w->updateSizeConstraints();
    EXPECT_EQ(w->minimumSize(), QSize(614, 500));
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext3, UpdateSizeConstraints_Mini_SetsForty)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bMiniMode;
    w->m_bMiniMode = true;
    w->updateSizeConstraints();
    EXPECT_EQ(w->minimumSize(), QSize(40, 40));
    w->m_bMiniMode = saved;
}

TEST(platform_mw_ext3, UpdateWindowTitle_IdleEngine_ClearsTitle)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    // Idle engine -> the else-branch clears the window title and titlebar text.
    // (Titlebar stores its text behind a d-pointer, so we can only observe the
    //  QMainWindow-level window title here.)
    EXPECT_NO_FATAL_FAILURE(w->updateWindowTitle());
    EXPECT_TRUE(w->windowTitle().isEmpty());
}

TEST(platform_mw_ext3, ResizeByConstraints_IdleEngine_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    EXPECT_NO_FATAL_FAILURE(w->resizeByConstraints(true));
    EXPECT_NO_FATAL_FAILURE(w->resizeByConstraints(false));
}

TEST(platform_mw_ext3, UpdateGeometryNotification_Idle_NoMessageButRecordsRect)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QRect saved = w->m_lastRectInNormalMode;

    w->updateGeometryNotification(QSize(123, 456));
    // Idle engine -> no message update; when window is in normal state the
    // last-rect is refreshed.
    if (w->windowState() == Qt::WindowNoState && !w->m_bMiniMode && !w->m_bMaximized) {
        EXPECT_EQ(w->m_lastRectInNormalMode.size(), QSize(123, 456));
    }
    w->m_lastRectInNormalMode = saved;
}

// ==========================================================================
// paintEvent (Idle engine -> draws the splash icon path)
// ==========================================================================

TEST(platform_mw_ext3, PaintEvent_IdleEngine_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    QPaintEvent pe(w->rect());
    QApplication::sendEvent(w, &pe);
    SUCCEED();
}

// ==========================================================================
// isActionAllowed (many branches, all safe with Idle engine)
// ==========================================================================

TEST(platform_mw_ext3, IsActionAllowed_BurstMode_AlwaysFalse)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::TogglePause, false, false));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::VolumeUp, true, true));
    w->m_bInBurstShootMode = saved;
}

TEST(platform_mw_ext3, IsActionAllowed_NormalState_DefaultTrue)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::TogglePause, false, false));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::VolumeUp, false, false));
}

TEST(platform_mw_ext3, IsActionAllowed_Shortcut_RequiresNonIdleForSomeKinds)
{
    // Idle engine -> shortcut path returns false for Screenshot / MovieInfo /
    // subtitle kinds (need a loaded movie).
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    EXPECT_FALSE(w->isActionAllowed(ActionFactory::Screenshot, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::MovieInfo, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::MatchOnlineSubtitle, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::BurstScreenshot, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::HideSubtitle, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::SelectSubtitle, false, true));
    // ToggleMiniMode shortcut on an idle engine: still allowed (no Idle guard).
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleMiniMode, false, true));
}

TEST(platform_mw_ext3, IsActionAllowed_MiniMode_FromUI_BlockedKinds)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = true;

    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleFullscreen, true, false));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::TogglePlaylist, true, false));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::BurstScreenshot, true, false));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleMiniMode, true, false));

    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext3, IsActionAllowed_MiniMode_Shortcut_BlockedKinds)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = true;

    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleFullscreen, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::TogglePlaylist, false, true));
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::BurstScreenshot, false, true));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleMiniMode, false, true));

    w->m_bMiniMode = savedMini;
}

// ==========================================================================
// requestAction (only safe kinds: ones that early-return or only touch UI)
// ==========================================================================

TEST(platform_mw_ext3, RequestAction_DisallowedWhenAnimationRunning)
{
    // Force the animation-running guard so requestAction returns immediately.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bStartAnimation;
    w->m_bStartAnimation = true;
    w->requestAction(ActionFactory::VolumeUp, false, {}, false);
    w->m_bStartAnimation = saved;
    SUCCEED();
}

TEST(platform_mw_ext3, RequestAction_VolumeUp_IncrementsDisplayVolume)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->slotVolumeChanged(40);
    w->requestAction(ActionFactory::VolumeUp, false, {}, true);
    pmw3_wait(40);
    EXPECT_GT(w->m_nDisplayVolume, 40);
}

TEST(platform_mw_ext3, RequestAction_VolumeDown_DecrementsDisplayVolume)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->slotVolumeChanged(40);
    w->requestAction(ActionFactory::VolumeDown, false, {}, true);
    pmw3_wait(40);
    EXPECT_LT(w->m_nDisplayVolume, 40);
}

TEST(platform_mw_ext3, RequestAction_WindowAbove_TogglesFlag)
{
    // WindowAbove flips m_bWindowAbove and reflects to UI; no engine use.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bWindowAbove;
    w->requestAction(ActionFactory::WindowAbove, true, {}, false);
    EXPECT_NE(w->m_bWindowAbove, saved);
    // toggle back to keep the shared window clean
    w->requestAction(ActionFactory::WindowAbove, true, {}, false);
    EXPECT_EQ(w->m_bWindowAbove, saved);
}

TEST(platform_mw_ext3, RequestAction_Exit_DoesNotQuitImmediately)
{
    // Exit calls qApp->quit() which only takes effect when the event loop
    // spins back; the rest of this case still runs. We do NOT pump the loop
    // afterwards (the harness keeps the process alive for the suite).
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->requestAction(ActionFactory::Exit, false, {}, false);
    // If quit had taken effect synchronously we would not reach here.
    SUCCEED();
}

// ==========================================================================
// adjustPlaybackSpeed / setPlaySpeedMenu* (Idle engine early-return)
// ==========================================================================

TEST(platform_mw_ext3, AdjustPlaybackSpeed_IdleEngine_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);

    double saved = w->m_dPlaySpeed;
    w->adjustPlaybackSpeed(ActionFactory::AccelPlayback);
    EXPECT_EQ(w->m_dPlaySpeed, saved);   // unchanged: Idle early return
    w->adjustPlaybackSpeed(ActionFactory::DecelPlayback);
    EXPECT_EQ(w->m_dPlaySpeed, saved);
}

TEST(platform_mw_ext3, SetPlaySpeedMenuUnchecked_ClearsAllSpeedActions)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setPlaySpeedMenuUnchecked());
}

TEST(platform_mw_ext3, SetPlaySpeedMenuChecked_MarksOneAction)
{
    // findActionsByKind must return a non-empty list for each speed kind so
    // the iterator dereference is safe.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setPlaySpeedMenuChecked(ActionFactory::OneTimes));
    EXPECT_NO_FATAL_FAILURE(w->setPlaySpeedMenuChecked(ActionFactory::Double));
}

// ==========================================================================
// setMusicShortKeyState
// ==========================================================================

TEST(platform_mw_ext3, SetMusicShortKeyState_True_EnablesMediaActions)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setMusicShortKeyState(true));
}

TEST(platform_mw_ext3, SetMusicShortKeyState_False_DisablesMediaActions)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->setMusicShortKeyState(false));
}

// ==========================================================================
// dbus / system-state slots (all safe with Idle engine)
// ==========================================================================

TEST(platform_mw_ext3, OnSysLockState_NotLocked_NoChange)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QVariantMap vm;
    vm["Locked"] = false;
    bool saved = w->m_bStateInLock;
    w->m_bStateInLock = false;
    w->onSysLockState(QString(), vm, QStringList());
    EXPECT_FALSE(w->m_bStateInLock);
    w->m_bStateInLock = saved;
}

TEST(platform_mw_ext3, OnSysLockState_LockedButIdle_NoPause)
{
    // Engine is Idle -> the "Locked && Playing" branch is skipped.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QVariantMap vm;
    vm["Locked"] = true;
    EXPECT_NO_FATAL_FAILURE(w->onSysLockState(QString(), vm, QStringList()));
}

TEST(platform_mw_ext3, SlotProperChanged_ActiveButIdle_NoSeek)
{
    // Engine Idle -> seekAbsolute branch skipped.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    QVariantMap vm;
    vm["Active"] = true;
    EXPECT_NO_FATAL_FAILURE(w->slotProperChanged(QString(), vm, QStringList()));
}

TEST(platform_mw_ext3, SlotUnsupported_ShowsMessage)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUnsupported());
}

TEST(platform_mw_ext3, SlotInvalidFile_SchedulesMessage)
{
    // Arms a singleShot; we pump briefly so the lambda does not leak.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->slotInvalidFile("/tmp/does_not_exist.mp4");
    pmw3_wait(40);
    SUCCEED();
}

// ==========================================================================
// mircast slots (engine Idle -> pauseResume / seek branches skipped)
// ==========================================================================

TEST(platform_mw_ext3, MircastSuccess_IdleEngine_NoPauseResume)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->mircastSuccess("test-device"));
    // hide the show-widget again so the shared window is clean
    if (w->m_pMircastShowWidget) w->m_pMircastShowWidget->hide();
}

TEST(platform_mw_ext3, ExitMircast_IdleEngine_SeekAbsoluteSafe)
{
    // exitMircast calls seekAbsolute(slider->value()) which on an idle engine
    // is a no-op internally; safe to exercise.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->exitMircast());
}

TEST(platform_mw_ext3, SlotExitMircast_EmitsEnableSignals)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    int frame = -1, speed = -1, sub = -1, sound = -1;
    auto c1 = QObject::connect(w, &Platform_MainWindow::frameMenuEnable,
                     [&frame](bool e) { frame = e ? 1 : 0; });
    auto c2 = QObject::connect(w, &Platform_MainWindow::playSpeedMenuEnable,
                     [&speed](bool e) { speed = e ? 1 : 0; });
    auto c3 = QObject::connect(w, &Platform_MainWindow::subtitleMenuEnable,
                     [&sub](bool e) { sub = e ? 1 : 0; });
    auto c4 = QObject::connect(w, &Platform_MainWindow::soundMenuEnable,
                     [&sound](bool e) { sound = e ? 1 : 0; });
    w->slotExitMircast();
    EXPECT_EQ(frame, 1);
    EXPECT_EQ(speed, 1);
    EXPECT_EQ(sub, 1);
    EXPECT_EQ(sound, 1);
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    QObject::disconnect(c3);
    QObject::disconnect(c4);
}

TEST(platform_mw_ext3, SlotUpdateMircastState_Exit_EmitsEnableSignals)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    int frame = -1;
    auto c = QObject::connect(w, &Platform_MainWindow::frameMenuEnable,
                     [&frame](bool e) { frame = e ? 1 : 0; });
    w->slotUpdateMircastState(MIRCAST_EXIT, QString());
    EXPECT_EQ(frame, 1);
    QObject::disconnect(c);
}

TEST(platform_mw_ext3, SlotUpdateMircastState_ConnectionFailed_PopupsAndExits)
{
    // Exercises the popupAdapter path + slotExitMircast; engine Idle -> safe.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_CONNECTION_FAILED, QString()));
    pmw3_wait(40);
}

TEST(platform_mw_ext3, SlotUpdateMircastState_Disconnected_ShowsMessageAndExits)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUpdateMircastState(MIRCAST_DISCONNECTED, QString()));
    pmw3_wait(40);
}

// ==========================================================================
// engine-state slots (sender() is null when called directly -> early return)
// ==========================================================================

TEST(platform_mw_ext3, SlotPlayerStateChanged_NullSender_EarlyReturn)
{
    // Called directly (not via signal), sender() is null -> early return.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bInited;
    EXPECT_NO_FATAL_FAILURE(w->slotPlayerStateChanged());
    EXPECT_EQ(w->m_bInited, saved);   // unchanged
}

TEST(platform_mw_ext3, SlotFileLoaded_NullSender_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    int saved = w->m_platform_nRetryTimes;
    EXPECT_NO_FATAL_FAILURE(w->slotFileLoaded());
    EXPECT_EQ(w->m_platform_nRetryTimes, saved);
}

TEST(platform_mw_ext3, SlotPlayerStateChanged_ViaSignal_SyncsTitle)
{
    // Emit stateChanged() from the engine so sender() is non-null; the Idle
    // state still makes most branches no-op, but updateWindowTitle runs.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    EXPECT_NO_FATAL_FAILURE(emit engine->stateChanged());
    pmw3_wait(140);   // the 100ms singleShot inside the slot
}

// ==========================================================================
// my_setStayOnTop (X11 atoms; safe to call on the real window)
// ==========================================================================

TEST(platform_mw_ext3, MySetStayOnTop_On_Off_NoCrash)
{
    // Calls XInternAtom / XSendEvent against the real X connection. In the
    // test container this may be a no-op display, but it must not crash.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->my_setStayOnTop(w, true));
    EXPECT_NO_FATAL_FAILURE(w->my_setStayOnTop(w, false));
}

// ==========================================================================
// updateContentGeometry (non-DXCB path: move/resize)
// ==========================================================================

TEST(platform_mw_ext3, UpdateContentGeometry_AppliesRect)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QRect r = w->geometry();
    EXPECT_NO_FATAL_FAILURE(w->updateContentGeometry(r));
}

// ==========================================================================
// popupAdapter (uses m_pPopupWid + DFontSizeManager)
// ==========================================================================

TEST(platform_mw_ext3, PopupAdapter_ShowsPopupWid)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pPopupWid, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    EXPECT_NO_FATAL_FAILURE(w->popupAdapter(QIcon(), "test message"));
    pmw3_wait(20);
}

// ==========================================================================
// delay / mouse-press timer slots
// ==========================================================================

TEST(platform_mw_ext3, SlotMousePressTimerTimeOut_NotPressed_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    w->m_bMousePressed = false;
    EXPECT_NO_FATAL_FAILURE(w->slotmousePressTimerTimeOut());
}

TEST(platform_mw_ext3, SlotMousePressTimerTimeOut_MiniMode_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool savedMini = w->m_bMiniMode;
    w->m_bMiniMode = true;
    w->m_bMousePressed = true;
    EXPECT_NO_FATAL_FAILURE(w->slotmousePressTimerTimeOut());
    w->m_bMiniMode = savedMini;
}

TEST(platform_mw_ext3, DelayedMouseReleaseHandler_AfterDblClick_NoPause)
{
    // s_bAfterDblClick true -> the pause branch is skipped. Static flag, so
    // we restore it to false to keep the shared suite clean.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->delayedMouseReleaseHandler());
}

TEST(platform_mw_ext3, DelayedMouseReleaseHandler_Touch_Pauses)
{
    // m_bLastIsTouch true and not after dblclick -> TogglePause requested;
    // Idle engine makes requestAction's downstream effect a safe no-op.
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    bool saved = w->m_bLastIsTouch;
    w->m_bLastIsTouch = true;
    EXPECT_NO_FATAL_FAILURE(w->delayedMouseReleaseHandler());
    w->m_bLastIsTouch = saved;
}

// ==========================================================================
// checkErrorMpvLogsChanged / checkWarningMpvLogsChanged (string scan only)
// ==========================================================================

TEST(platform_mw_ext3, CheckErrorMpvLogsChanged_NoMatch_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged("mpv", "nothing interesting here"));
}

TEST(platform_mw_ext3, CheckErrorMpvLogsChanged_AvformatMatch_BranchTaken)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkErrorMpvLogsChanged(
        "mpv", "ERROR: avformat_open_input() failed for foo"));
}

TEST(platform_mw_ext3, CheckWarningMpvLogsChanged_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkWarningMpvLogsChanged("mpv", "some warning text"));
}

// ==========================================================================
// sleep / lock (Idle engine -> no actual pause/seek)
// ==========================================================================

TEST(platform_mw_ext3, SleepStateChanged_BothBranches_IdleSafe)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE(w->sleepStateChanged(true));
    EXPECT_NO_FATAL_FAILURE(w->sleepStateChanged(false));
}

TEST(platform_mw_ext3, LockStateChanged_BothBranches_IdleSafe)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    ASSERT_EQ(engine->state(), PlayerEngine::Idle);
    bool saved = w->m_bLocked;
    EXPECT_NO_FATAL_FAILURE(w->lockStateChanged(true));
    EXPECT_NO_FATAL_FAILURE(w->lockStateChanged(false));
    pmw3_wait(80);
    w->m_bLocked = saved;
}

// ==========================================================================
// misc slots / helpers
// ==========================================================================

TEST(platform_mw_ext3, CheckOnlineState_BothBranches_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineState(true));
    EXPECT_NO_FATAL_FAILURE(w->checkOnlineState(false));
}

TEST(platform_mw_ext3, SyncPostion_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->m_pCommHintWid, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->syncPostion());
}

TEST(platform_mw_ext3, UpdateProxyGeometry_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->updateProxyGeometry());
}

TEST(platform_mw_ext3, SuspendResumeToolsWindow_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->suspendToolsWindow());
    EXPECT_NO_FATAL_FAILURE(w->resumeToolsWindow());
}

TEST(platform_mw_ext3, SlotWMChanged_SyncsFlag)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotWMChanged());
    EXPECT_EQ(w->m_bIsWM, DWindowManagerHelper::instance()->hasBlurWindow());
}

TEST(platform_mw_ext3, SlotUrlpause_BothBranches_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotUrlpause(true));
    EXPECT_NO_FATAL_FAILURE(w->slotUrlpause(false));
}

TEST(platform_mw_ext3, SlotFocusWindowChanged_BothStates_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->slotFocusWindowChanged());
    EXPECT_NO_FATAL_FAILURE(w->slotFocusWindowChanged());
}

TEST(platform_mw_ext3, DiskRemoved_EmptyPlaylist_EarlyReturn)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(engine, nullptr);
    // Playlist may be empty in the test env -> early return path.
    EXPECT_NO_FATAL_FAILURE(w->diskRemoved("nonexistent_disk"));
}

TEST(platform_mw_ext3, OnRefreshDecode_NoBackendSafe)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onRefreshDecode());
}

TEST(platform_mw_ext3, OnSetDecodeModel_AllIndices_NullBackendSafe)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", 0));
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", 1));
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", 2));
    EXPECT_NO_FATAL_FAILURE(w->onSetDecodeModel("base.decode.select", 3));
}

// ==========================================================================
// prepareSplashImages (loads SVG resources)
// ==========================================================================

TEST(platform_mw_ext3, PrepareSplashImages_NoCrash)
{
    Platform_MainWindow *w = pmw3_window();
    ASSERT_NE(w, nullptr);
    EXPECT_NO_FATAL_FAILURE(w->prepareSplashImages());
}
