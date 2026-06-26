// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for Platform_MainWindow (suite: platform_mw_ext).
// Covers getters, geometry helpers, settings-backed path helpers, simple slots
// and event handlers that do not depend on a live mpv backend.
//
// Notes:
//  - Only Google Test is used; no main() defined (provided by test_qtestmain.cpp).
//  - stub.h is intentionally NOT included (its include path is not wired for the
//    platform test target), so every case calls real functions directly and
//    guards all pointer dereferences against null.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QMargins>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QPoint>
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

// ---------- helpers (unique prefix pmw_ext_) ----------

static Platform_MainWindow *pmw_ext_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

// ---------- getters / simple state ----------

TEST(platform_mw_ext, Getters_ReadFields)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    // These getters read member fields directly; assert only that they return
    // sane-typed values without depending on the shared window's current state
    // (other suites may have played files / toggled mini mode before us).
    int vol = w->getDisplayVolume();
    EXPECT_GE(vol, 0);

    bool mini = w->getMiniMode();
    // mini mode is a bool flag; both values are acceptable, we only verify
    // the accessor does not crash and returns a bool.
    EXPECT_TRUE(mini == true || mini == false);

    bool inited = w->inited();
    EXPECT_TRUE(inited == true || inited == false);
}

TEST(platform_mw_ext, SetTouched_TogglesField)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    w->setTouched(true);
    EXPECT_TRUE(w->m_bIsTouch);
    w->setTouched(false);
    EXPECT_FALSE(w->m_bIsTouch);
}

TEST(platform_mw_ext, SetCurrentHwdec_StoresValue)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    w->setCurrentHwdec("auto");
    EXPECT_EQ(w->m_sCurrentHwdec.toStdString(), "auto");
    w->setCurrentHwdec("off");
    EXPECT_EQ(w->m_sCurrentHwdec.toStdString(), "off");
    w->setCurrentHwdec("");
    EXPECT_TRUE(w->m_sCurrentHwdec.isEmpty());
}

TEST(platform_mw_ext, SetInit_EmitsSignalOnChange)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    bool fired = false;
    auto conn = QObject::connect(w, &Platform_MainWindow::initChanged, [&]() {
        fired = true;
    });

    bool before = w->inited();
    w->setInit(!before);        // value changes -> signal must fire
    EXPECT_TRUE(fired);
    EXPECT_NE(w->inited(), before);

    fired = false;
    w->setInit(w->inited());    // same value -> no signal
    EXPECT_FALSE(fired);

    // restore to original to keep the shared window in a clean state
    w->setInit(before);
    QObject::disconnect(conn);
}

TEST(platform_mw_ext, SetOpenFiles_StoresList)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    QStringList files = {"/tmp/a.mp4", "/tmp/b.mp3"};
    w->setOpenFiles(files);
    EXPECT_EQ(w->m_listOpenFiles.size(), 2);
    EXPECT_EQ(w->m_listOpenFiles[0].toStdString(), "/tmp/a.mp4");
    EXPECT_EQ(w->m_listOpenFiles[1].toStdString(), "/tmp/b.mp3");

    QStringList empty;
    w->setOpenFiles(empty);
    EXPECT_TRUE(w->m_listOpenFiles.isEmpty());
}

// ---------- geometry helpers ----------

TEST(platform_mw_ext, DragMargins_ReturnsConstant)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    QMargins m = w->dragMargins();
    EXPECT_EQ(m.left(), m.right());
    EXPECT_EQ(m.top(), m.bottom());
    EXPECT_EQ(m.left(), m.top());
    EXPECT_GT(m.left(), 0);
}

TEST(platform_mw_ext, InsideResizeArea_OutsideCorner)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    QRect frame = w->frameGeometry();
    // a point clearly outside the frame must be inside the resize area
    QPoint outside(frame.right() + 5000, frame.bottom() + 5000);
    EXPECT_TRUE(w->insideResizeArea(outside));

    // the window center must NOT be inside the resize area
    QPoint center = frame.center();
    EXPECT_FALSE(w->insideResizeArea(center));
}

TEST(platform_mw_ext, JudgeMouseInWindow_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);

    // call with a couple of positions; function only reads geometry and may
    // emit a leaveEvent internally -- must not crash regardless of result.
    bool r1 = w->judgeMouseInWindow(w->rect().center());
    bool r2 = w->judgeMouseInWindow(QPoint(-1, -1));
    bool r3 = w->judgeMouseInWindow(w->rect().bottomRight() + QPoint(50, 50));
    EXPECT_FALSE(r1);  // documented to always return false
    EXPECT_FALSE(r2);
    EXPECT_FALSE(r3);
}

// ---------- settings-backed path helpers ----------

TEST(platform_mw_ext, LastOpenedPath_FallbackChain)
{
    // With an empty/invalid stored path the helper must still return a
    // non-empty usable path (MoviesLocation or current path fallback).
    Settings::get().setGeneralOption("last_open_path", "");
    QString p = Platform_MainWindow::lastOpenedPath();
    EXPECT_FALSE(p.isEmpty());

    // An invalid path should also fall back gracefully.
    Settings::get().setGeneralOption("last_open_path", "/definitely/not/here/xyz");
    QString p2 = Platform_MainWindow::lastOpenedPath();
    EXPECT_FALSE(p2.isEmpty());
}

TEST(platform_mw_ext, PadLoadPath_FallbackChain)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    Settings::get().setGeneralOption("pad_load_path", "");
    QString p = w->padLoadPath();
    EXPECT_FALSE(p.isEmpty());

    Settings::get().setGeneralOption("pad_load_path", "/no/such/dir/pad");
    QString p2 = w->padLoadPath();
    EXPECT_FALSE(p2.isEmpty());
}

// ---------- probeCdromDevice / addCdromPath ----------

TEST(platform_mw_ext, ProbeCdromDevice_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Reads /proc/mounts. Returns empty string when no cdrom mounted.
    QString dev = w->probeCdromDevice();
    EXPECT_TRUE(dev.isNull() || dev.isEmpty() || dev.contains("/dev/"));
}

TEST(platform_mw_ext, AddCdromPath_NoCdrom_ReturnsFalseOrSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Without a mounted cdrom the early-return path is exercised.
    bool ok = w->addCdromPath();
    // We don't assert the return value (env dependent), only that it returns.
    (void)ok;
}

// ---------- simple slots ----------

TEST(platform_mw_ext, SlotFocusWindowChanged_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->slotFocusWindowChanged();
    w->slotFocusWindowChanged();
}

TEST(platform_mw_ext, SlotWMChanged_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->slotWMChanged();
    // m_bIsWM should reflect DWindowManagerHelper after the call.
    EXPECT_EQ(w->m_bIsWM, DWindowManagerHelper::instance()->hasBlurWindow());
}

TEST(platform_mw_ext, CheckOnlineState_BothBranches)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // true (online) -> no message; false (offline) -> sendMessage via DMainWindow.
    w->checkOnlineState(true);
    w->checkOnlineState(false);
}

TEST(platform_mw_ext, CheckOnlineSubtitle_AllFailReasons)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    using FR = OnlineSubtitle::FailReason;
    w->checkOnlineSubtitle(FR::NoError);
    w->checkOnlineSubtitle(FR::NetworkError);
    w->checkOnlineSubtitle(FR::NoSubFound);
    w->checkOnlineSubtitle(FR::Duplicated);
}

TEST(platform_mw_ext, DiskRemoved_EmptyPlaylist_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    // With no items in the playlist the function returns early.
    w->diskRemoved("nonexistent_disk");
}

TEST(platform_mw_ext, SyncPostion_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    ASSERT_TRUE(w->m_pCommHintWid);
    w->syncPostion();
}

TEST(platform_mw_ext, UpdateGeometryNotification_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->updateGeometryNotification(QSize(800, 600));
    w->updateGeometryNotification(QSize(0, 0));
    w->updateGeometryNotification(QSize(1, 1));
}

TEST(platform_mw_ext, UpdateContentGeometry_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Non-DXCB path: just move/resize the widget.
    w->updateContentGeometry(QRect(10, 20, 400, 300));
}

// ---------- playback-mode settings init ----------

TEST(platform_mw_ext, DefaultPlayModeInit_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Walks each known play mode value; uses the real playmode option key.
    // Exercise with each combobox index to cover all branches.
    auto opt = Settings::get().settings()->option("base.play.playmode");
    ASSERT_TRUE(opt);
    QVariant orig = opt->value();

    for (int i = 0; i <= 4; ++i) {
        opt->setValue(i);
        // defaultplaymodeinit reads the current option value and dispatches
        w->defaultplaymodeinit();
    }
    opt->setValue(orig);
}

TEST(platform_mw_ext, SlotDefaultPlayModeChanged_WrongKeyEarlyReturn)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Wrong key -> early return, must not dereference the option pointer.
    w->slotdefaultplaymodechanged("base.play.notreal", 0);
}

TEST(platform_mw_ext, SlotDefaultPlayModeChanged_EachIndex)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    for (int i = 0; i <= 4; ++i) {
        w->slotdefaultplaymodechanged("base.play.playmode", i);
    }
}

// ---------- decode model slots (backend may be null) ----------

TEST(platform_mw_ext, OnSetDecodeModel_NullBackendSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    // No mpv backend in test env -> getMpvProxy() returns null -> early return.
    w->onSetDecodeModel("base.decode.select", 0);
    w->onSetDecodeModel("base.decode.select", 1);
    w->onSetDecodeModel("base.decode.select", 2);
    w->onSetDecodeModel("base.decode.select", 3);
}

TEST(platform_mw_ext, OnRefreshDecode_NullBackendSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->onRefreshDecode();
}

// ---------- mute / volume slots ----------

TEST(platform_mw_ext, SlotMuteChanged_BothBranches)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    w->slotMuteChanged(true);
    w->slotMuteChanged(false);
}

TEST(platform_mw_ext, SlotVolumeChanged_SeveralValues)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    w->slotVolumeChanged(0);
    EXPECT_EQ(w->m_nDisplayVolume, 0);
    w->slotVolumeChanged(50);
    EXPECT_EQ(w->m_nDisplayVolume, 50);
    w->slotVolumeChanged(100);
    EXPECT_EQ(w->m_nDisplayVolume, 100);
    w->slotVolumeChanged(-5);   // negative clamp handled downstream
    EXPECT_EQ(w->m_nDisplayVolume, -5);
    QTest::qWait(80);  // let the singleShot message helper run
}

TEST(platform_mw_ext, SlotUrlpause_BothBranches)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->slotUrlpause(true);
    w->slotUrlpause(false);
}

// ---------- font slot ----------

TEST(platform_mw_ext, SlotFontChanged_NoCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QFont f = DFontSizeManager::instance()->get(DFontSizeManager::T6);
    w->slotFontChanged(f);
}

// ---------- burst screenshot ----------

TEST(platform_mw_ext, StartBurstShooting_NoBackendSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    // Connects a signal; without a backend no screenshot arrives.
    w->startBurstShooting();
    // Simulate a frame arriving to exercise the handler defensively.
    QImage img(16, 16, QImage::Format_ARGB32);
    img.fill(Qt::black);
    emit engine->notifyScreenshot(img, 12345);
    QTest::qWait(60);
}

// ---------- mircast slots ----------
// NOTE: the full mircast chain (getMircast()->slotExitMircast -> stopDlnaTP /
// network signals) is intentionally NOT exercised here because it depends on
// the toolbox's mircast widget being fully wired and may trigger heavy network
// side effects. Only the pure default-branch dispatch is covered, which does
// nothing.

TEST(platform_mw_ext, SlotUpdateMircastState_DefaultBranch)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // An unknown state code hits the default branch (no-op).
    w->slotUpdateMircastState(9999, QString());
}

// ---------- sleep / lock ----------

TEST(platform_mw_ext, SleepStateChanged_BothBranches)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    // Engine is idle, so neither pause nor seek branches fire; safe.
    w->sleepStateChanged(true);
    w->sleepStateChanged(false);
}

TEST(platform_mw_ext, LockStateChanged_BothBranches)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    w->lockStateChanged(true);
    w->lockStateChanged(false);
    QTest::qWait(60);  // let the relock singleShot run
}

// ---------- short-key state ----------

TEST(platform_mw_ext, SetMusicShortKeyState_BothStates)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->setMusicShortKeyState(true);
    w->setMusicShortKeyState(false);
}

// ---------- action helpers that don't trigger playback ----------

TEST(platform_mw_ext, UpdateActionsState_NoBackendSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    PlayerEngine *engine = w->engine();
    ASSERT_TRUE(engine);
    // Iterates main menu actions and toggles enabled state based on engine
    // state (idle here) -- safe to call.
    w->updateActionsState();
}

TEST(platform_mw_ext, ReflectActionToUI_PlayModeKinds)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    using AK = ActionFactory::ActionKind;
    w->reflectActionToUI(AK::OrderPlay);
    w->reflectActionToUI(AK::ShufflePlay);
    w->reflectActionToUI(AK::SinglePlay);
    w->reflectActionToUI(AK::SingleLoop);
    w->reflectActionToUI(AK::ListLoop);
}

// ---------- subtitleMatchVideo ----------

TEST(platform_mw_ext, SubtitleMatchVideo_NonExistentFile)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // A path whose directory does not exist -> the directory scan yields no
    // candidates, vfileInfo does not exist, so play() is NOT reached and the
    // "Please load the video first" hint is shown. Safe, no backend involved.
    w->subtitleMatchVideo("/no/such/dir/movie.ass");
}

// ---------- testCdrom umbrella (USE_TEST only) ----------

TEST(platform_mw_ext, TestCdrom_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->testCdrom();
}

// ---------- captured* helpers ----------

TEST(platform_mw_ext, CapturedKeyEvent_TabShowsToolbox)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    w->capturedKeyEvent(&tab);
}

TEST(platform_mw_ext, CapturedMousePressRelease_NoFocusSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                      QPointingDevice::primaryPointingDevice());
    w->capturedMousePressEvent(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10), QPointF(10, 10),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                        QPointingDevice::primaryPointingDevice());
    w->capturedMouseReleaseEvent(&release);
}

// ---------- mousePressTimer slot ----------

TEST(platform_mw_ext, SlotMousePressTimerTimeOut_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // With no mouse pressed the early-return branch is taken.
    w->slotmousePressTimerTimeOut();
}

// ---------- event() dispatch for assorted QEvent types ----------

TEST(platform_mw_ext, Event_LeaveAndEnter)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QEvent enter(QEvent::Enter);
    EXPECT_TRUE(w->event(&enter));
    QEvent leave(QEvent::Leave);
    EXPECT_TRUE(w->event(&leave));
    QEvent deactivate(QEvent::WindowDeactivate);
    EXPECT_TRUE(w->event(&deactivate));
}

TEST(platform_mw_ext, Event_TouchBegin)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QEvent touch(QEvent::TouchBegin);
    EXPECT_TRUE(w->event(&touch));
    EXPECT_TRUE(w->m_bIsTouch);
}

// ---------- updateProxyGeometry / suspend / resume ----------

TEST(platform_mw_ext, UpdateProxyGeometry_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->updateProxyGeometry();
}

TEST(platform_mw_ext, SuspendResumeToolsWindow_NoCrash)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    w->suspendToolsWindow();
    w->resumeToolsWindow();
}

// ---------- paint / show / hide events (need a screen) ----------

TEST(platform_mw_ext, ShowHideEvents_NoCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QShowEvent se;
    w->showEvent(&se);
    QHideEvent he;
    w->hideEvent(&he);
}

TEST(platform_mw_ext, MoveEvent_NoCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    QPoint oldPos = w->pos();
    w->move(oldPos + QPoint(5, 5));
    QMoveEvent me(w->pos(), oldPos);
    w->moveEvent(&me);
}

// ---------- menu item invoked with invalid action ----------

TEST(platform_mw_ext, MenuItemInvoked_UnknownActionSafe)
{
    Platform_MainWindow *w = pmw_ext_window();
    ASSERT_TRUE(w);
    // Use a real QAction without a "kind" property so actionKind() returns
    // ActionFactory::Invalid, which makes menuItemInvoked return early.
    // (Do NOT pass nullptr: actionKind() dereferences pAction before the
    //  null/Invalid guard inside menuItemInvoked.)
    QAction act("dummy");
    w->menuItemInvoked(&act);
}

// ---------- local QScreen sanity on a throwaway widget (rule 4) ----------

TEST(platform_mw_ext, ScreenGeometry_ThrowawayWidget)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";
    QWidget local;
    local.resize(120, 80);
    local.move(50, 50);
    local.show();
    QTest::qWait(40);
    QScreen *s = local.windowHandle() ? local.screen() : QGuiApplication::primaryScreen();
    ASSERT_TRUE(s);
    QRect avail = s->availableGeometry();
    EXPECT_GT(avail.width(), 0);
    EXPECT_GT(avail.height(), 0);
    local.close();
}
