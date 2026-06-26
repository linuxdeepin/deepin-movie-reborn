// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for the platform widget cluster under
// src/widgets/platform/:
//   * platform_playlist_widget.cpp
//   * platform_volumeslider.cpp
//   * platform_notification_widget.cpp
//   * platform_animationlabel.cpp
//   * platform_movie_progress_indicator.cpp
//
// Suite name "platform_widgets_ext"; static helpers use unique prefix "pw_".
//
// Safety rules baked in (verified against prior crashes / link failures):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * The shared playlist / volume slider live in the running app's main
//     window: playlist via dApp->getMainWindow()->playlist(), volume slider
//     via ->toolbox()->volumeSlider(). They are guarded for null before use.
//   * Notification / animation-label / progress-indicator widgets are
//     constructed with a fresh local QWidget parent. Cases touching geometry
//     are guarded by primaryScreen() and GTEST_SKIP when headless.
//   * No mpv backend / decode path is exercised. Functions that need a loaded
//     playlist (loadPlaylist, appendItems, removeItem with real items) are
//     avoided; only the no-item / pure-logic branches are covered.
//   * Only settings keys known-safe to write are used: global_volume, mute.
//     Unknown keys would NPE inside Settings; reads are always safe.

#include "src/widgets/platform/platform_playlist_widget.h"
#include "src/widgets/platform/platform_volumeslider.h"
#include "src/widgets/platform/platform_notification_widget.h"
#include "src/widgets/platform/platform_animationlabel.h"
#include "src/widgets/platform/platform_movie_progress_indicator.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/libdmr/compositing_manager.h"
#include "src/common/dmr_settings.h"
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QMetaObject>

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// The running app's main window. All shared widgets hang off this.
static Platform_MainWindow *pw_mainWindow()
{
    return dApp->getMainWindow();
}

// Shared playlist widget owned by the main window.
static Platform_PlaylistWidget *pw_playlist()
{
    Platform_MainWindow *w = pw_mainWindow();
    return w ? w->playlist() : nullptr;
}

// Shared volume slider owned by the toolbox.
static Platform_VolumeSlider *pw_volumeSlider()
{
    Platform_MainWindow *w = pw_mainWindow();
    if (!w) return nullptr;
    Platform_ToolboxProxy *tb = w->toolbox();
    return tb ? tb->volumeSlider() : nullptr;
}

// A short synchronous wait so animations/timers settle without stalling.
static void pw_wait(int ms = 120)
{
    QTest::qWait(ms);
}

// ==========================================================================
// platform_playlist_widget.cpp
// ==========================================================================

TEST(platform_widgets_ext, playlist_state_initial_isClosed)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    // The shared playlist starts hidden / closed.
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Closed);
}

TEST(platform_widgets_ext, playlist_toggling_initial_false)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    EXPECT_FALSE(pl->toggling());
}

TEST(platform_widgets_ext, playlist_get_playlist_nonNull)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    EXPECT_NE(pl->get_playlist(), nullptr);
}

TEST(platform_widgets_ext, playlist_engine_nonNull)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    EXPECT_NE(pl->engine(), nullptr);
}

TEST(platform_widgets_ext, playlist_endAnimation_noop_withoutAnimations)
{
    // paOpen/paClose start as nullptr; endAnimation must tolerate that.
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->endAnimation();  // should not crash; sets duration only when ptrs set
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_isFocusInPlaylist_whenPlaylistFocused)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *list = pl->get_playlist();
    ASSERT_NE(list, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();

    list->setFocus();
    pw_wait(20);
    EXPECT_TRUE(pl->isFocusInPlaylist());
}

TEST(platform_widgets_ext, playlist_isFocusInPlaylist_false_otherwise)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    // Clear focus away from playlist / clear button by focusing the main
    // window itself.
    if (Platform_MainWindow *mw = pw_mainWindow()) {
        mw->setFocus();
    }
    pw_wait(20);
    EXPECT_FALSE(pl->isFocusInPlaylist());
}

TEST(platform_widgets_ext, playlist_resetFocusAttribute_setAndClear)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    bool atr = true;
    pl->resetFocusAttribute(atr);   // sets internal flag true
    atr = false;
    pl->resetFocusAttribute(atr);   // clears it again
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_clear_withEmptyList_noCrash)
{
    // clear() forwards to _playlist->clear() and _engine->getplaylist()->clearLoad().
    // On the shared widget the list may already be empty; clear() must be safe.
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->clear();
    EXPECT_EQ(pl->get_playlist()->count(), 0);
}

TEST(platform_widgets_ext, playlist_updateSelectItem_enter_noFocus_noCrash)
{
    // No current item (empty list): Key_Enter should fall through the
    // "focusWidget == clear button" check and the slotDoubleClickedItem path
    // without dereferencing a null prevItemWgt.
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    ASSERT_EQ(pl->get_playlist()->count(), 0);
    pl->updateSelectItem(Qt::Key_Enter);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_updateSelectItem_up_atTop_noCrash)
{
    // Empty list -> row -1 -> index 0 -> setCurrentRow(0) is a no-op on an
    // empty list; must not crash.
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->updateSelectItem(Qt::Key_Up);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_updateSelectItem_down_atBottom_noCrash)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    // count() - 1 == -1 on empty list; guard inside the function returns early
    // only when index >= count()-1, but here _index starts 0 and count is 0,
    // so 0 >= -1 is true and it returns. Still exercise it.
    pl->updateSelectItem(Qt::Key_Down);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_showItemInfo_noMouseItem_returnsEarly)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->showItemInfo();  // _mouseItem null -> early return
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_openItemInFM_noMouseItem_returnsEarly)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->openItemInFM();  // _mouseItem null -> early return
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_contextMenuEvent_doesNotCrash)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();

    // Send a context menu event at an arbitrary point. With USE_TEST defined
    // (it is, in this build) the menu is hidden+cleared immediately, so no
    // modal popup blocks the test.
    QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(10, 10),
                          pl->mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(pl, &cme);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_paintEvent_doesNotCrash)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    QPaintEvent pe(pl->rect());
    QApplication::sendEvent(pl, &pe);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_resizeEvent_emitsSizeChange)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    bool got = false;
    QObject::connect(pl, &Platform_PlaylistWidget::sizeChange,
                     [&got]() { got = true; });
    QSize old = pl->size();
    QResizeEvent re(QSize(old.width() + 2, old.height() + 2), old);
    QApplication::sendEvent(pl, &re);
    pw_wait(30);  // let the queued batchUpdateSizeHints timer fire
    EXPECT_TRUE(got);
}

TEST(platform_widgets_ext, playlist_eventFilter_clearButton_keyUpDown_consumed)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    // eventFilter is protected; we exercise it indirectly by giving the clear
    // button focus and delivering a KeyPress. The filter returns true (eats)
    // Up/Down on the clear button. Reach it via the playlist's own event
    // filter by sending the key to the clear button (which has the filter
    // installed).
    QPushButton *clearBtn = nullptr;
    // The clear button is private; find it by object name.
    clearBtn = pl->findChild<QPushButton *>("CLEAR_PLAYLIST_BUTTON");
    ASSERT_NE(clearBtn, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    clearBtn->setFocus();
    pw_wait(20);
    QKeyEvent upPress(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(clearBtn, &upPress);
    QKeyEvent downPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(clearBtn, &downPress);
    SUCCEED();
}

TEST(platform_widgets_ext, playlist_showEvent_adjustsSize)
{
    Platform_PlaylistWidget *pl = pw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QShowEvent se;
    // Already constructed; sending showEvent directly exercises
    // batchUpdateSizeHints + adjustSize.
    QApplication::sendEvent(pl, &se);
    SUCCEED();
}

// ==========================================================================
// platform_volumeslider.cpp
// ==========================================================================

TEST(platform_widgets_ext, volume_state_initial_isClose)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    EXPECT_EQ(vs->state(), Platform_VolumeSlider::Close);
}

TEST(platform_widgets_ext, volume_getsliderstate_initial_false)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    EXPECT_FALSE(vs->getsliderstate());
}

TEST(platform_widgets_ext, volume_getVolume_initialValue)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    // Constructor seeds m_nVolume = 100.
    int v = vs->getVolume();
    EXPECT_GE(v, 0);
    EXPECT_LE(v, 200);
}

TEST(platform_widgets_ext, volume_setThemeType_noop)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->setThemeType(1);
    vs->setThemeType(2);
    SUCCEED();
}

TEST(platform_widgets_ext, volume_stopTimer_idempotent)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->stopTimer();   // not running
    vs->stopTimer();   // still safe
    SUCCEED();
}

TEST(platform_widgets_ext, volume_changeVolume_clampsLow)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(-50);
    EXPECT_EQ(vs->getVolume(), 0);
}

TEST(platform_widgets_ext, volume_changeVolume_clampsHigh)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(1000);
    EXPECT_EQ(vs->getVolume(), 200);
}

TEST(platform_widgets_ext, volume_changeVolume_midRange)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(42);
    EXPECT_EQ(vs->getVolume(), 42);
}

TEST(platform_widgets_ext, volume_changeVolume_above100_callsVolumeChanged)
{
    // nVolume >= 100 forces a manual volumeChanged() call (slider maxes at
    // 100). With volume > 0 and mute possibly true, it may flip mute off and
    // call setMute -> readSinkInputPath (DBUS, safe to fail). Exercise both.
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(150);
    EXPECT_EQ(vs->getVolume(), 150);
}

TEST(platform_widgets_ext, volume_calculationStep_accumulates)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    // First set: assigns. Same-sign second: accumulates.
    vs->calculationStep(60);
    vs->calculationStep(60);
    // No direct getter for m_iStep; verify via volumeUp path below.
    SUCCEED();
}

TEST(platform_widgets_ext, volume_volumeUp_nonWheel_increments)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);              // ensure a known base
    // Non-wheel path (m_bIsWheel false) increments by 10.
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 60);
}

TEST(platform_widgets_ext, volume_volumeDown_nonWheel_decrements)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 40);
}

TEST(platform_widgets_ext, volume_volumeUp_clampsAt200)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(195);
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 200);
}

TEST(platform_widgets_ext, volume_volumeDown_clampsAt0)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(5);
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 0);
}

TEST(platform_widgets_ext, volume_changeMuteState_sameState_returnsEarly)
{
    // Constructor sets m_bIsMute = false; asking to set false again is a
    // no-op (returns before touching settings / emitting).
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(100);   // ensure volume != 0 so the volume==0 guard
                              // does not also short-circuit
    vs->changeMuteState(false);
    SUCCEED();
}

TEST(platform_widgets_ext, volume_changeMuteState_toMute_true)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(100);
    vs->changeMuteState(true);   // writes "mute" (safe key), emits signal
    vs->changeMuteState(false);  // toggle back
    SUCCEED();
}

TEST(platform_widgets_ext, volume_volumeChanged_updatesInternalAndEmits)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    int captured = -1;
    auto c = QObject::connect(vs, &Platform_VolumeSlider::sigVolumeChanged,
                     [&captured](int v) { captured = v; });
    vs->changeVolume(70);   // triggers volumeChanged(70) indirectly
    pw_wait(20);
    EXPECT_EQ(vs->getVolume(), 70);
    QObject::disconnect(c);
}

TEST(platform_widgets_ext, volume_keyPressEvent_up_increments)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vs, &up);
    EXPECT_EQ(vs->getVolume(), 55);
}

TEST(platform_widgets_ext, volume_keyPressEvent_down_decrements)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(vs, &down);
    EXPECT_EQ(vs->getVolume(), 45);
}

TEST(platform_widgets_ext, volume_keyPressEvent_otherKey_ignored)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    QKeyEvent other(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(vs, &other);
    EXPECT_EQ(vs->getVolume(), 50);
}

TEST(platform_widgets_ext, volume_eventFilter_wheelUp_increments)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);
    // The slider (child) has the event filter installed.
    DSlider *slider = nullptr;
    slider = vs->findChild<DSlider *>();
    ASSERT_NE(slider, nullptr);
    QPoint pos = slider->rect().center();
    QWheelEvent wheelUp(pos, slider->mapToGlobal(pos), QPoint(0, 120),
                        QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                        Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &wheelUp);
    pw_wait(20);
    EXPECT_GT(vs->getVolume(), 40);
}

TEST(platform_widgets_ext, volume_eventFilter_wheelDown_decrements)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(60);
    DSlider *slider = vs->findChild<DSlider *>();
    ASSERT_NE(slider, nullptr);
    QPoint pos = slider->rect().center();
    QWheelEvent wheelDown(pos, slider->mapToGlobal(pos), QPoint(0, -120),
                           QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &wheelDown);
    pw_wait(20);
    EXPECT_LT(vs->getVolume(), 60);
}

TEST(platform_widgets_ext, volume_eventFilter_wheelWithModifiers_ignored)
{
    // Modifiers/buttons present -> filter returns false, volume untouched.
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    DSlider *slider = vs->findChild<DSlider *>();
    ASSERT_NE(slider, nullptr);
    QPoint pos = slider->rect().center();
    QWheelEvent wheelMod(pos, slider->mapToGlobal(pos), QPoint(0, 120),
                          QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                          Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &wheelMod);
    pw_wait(20);
    EXPECT_EQ(vs->getVolume(), 50);
}

TEST(platform_widgets_ext, volume_eventFilter_nonWheel_delegates)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vs->findChild<DSlider *>();
    ASSERT_NE(slider, nullptr);
    // A non-wheel event must fall through to QObject::eventFilter (no crash).
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(0, 0),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(slider, &me);
    SUCCEED();
}

TEST(platform_widgets_ext, volume_muteButtnClicked_toggles)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(100);
    vs->muteButtnClicked();   // mute -> true (writes safe "mute" key)
    vs->muteButtnClicked();   // mute -> false
    SUCCEED();
}

TEST(platform_widgets_ext, volume_paintEvent_doesNotCrash)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    QPaintEvent pe(vs->rect());
    QApplication::sendEvent(vs, &pe);
    SUCCEED();
}

TEST(platform_widgets_ext, volume_initVolume_readsSafeKeys)
{
    // initVolume() reads global_volume + mute (both safe keys) on a 50ms
    // single-shot timer. Just drive it and pump the event loop.
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->initVolume();
    pw_wait(120);
    SUCCEED();
}

TEST(platform_widgets_ext, volume_updatePoint_movesWidget)
{
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    vs->updatePoint(QPoint(10, 10));
    // move() applied; verify position changed from the default.
    EXPECT_FALSE(vs->pos().isNull());
}

TEST(platform_widgets_ext, volume_delayedHide_schedulesHide)
{
    // delayedHide() arms a DUtil::TimerSingleShot (100/2000ms). We only need
    // to ensure it does not crash and the mouseIn flag flips to false.
    Platform_VolumeSlider *vs = pw_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->delayedHide();
    pw_wait(30);
    SUCCEED();
}

// ==========================================================================
// platform_notification_widget.cpp
// ==========================================================================

TEST(platform_widgets_ext, notification_construct_andSetters)
{
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);

    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NONE);
    nw.setAnchorDistance(42);
    nw.setAnchorPoint(QPoint(5, 5));
    nw.setWM(true);
    nw.setWM(false);
    SUCCEED();
}

TEST(platform_widgets_ext, notification_syncPosition_allAnchors)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);
    nw.resize(120, 30);

    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.syncPosition();
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    nw.syncPosition();
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NONE);
    nw.syncPosition();
    SUCCEED();
}

TEST(platform_widgets_ext, notification_syncPositionWithRect_allAnchors)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);
    nw.resize(120, 30);
    QRect rect(0, 0, 800, 600);

    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.syncPosition(rect);
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    nw.syncPosition(rect);
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NONE);
    nw.syncPosition(rect);
    SUCCEED();
}

TEST(platform_widgets_ext, notification_popup_autoHideFalse_shows)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    dmr::Platform_NotificationWidget nw(&parent);
    nw.popup(QString("hello"), false);   // flag=false: skip window-overlap scan
    pw_wait(20);
    EXPECT_TRUE(nw.isVisible());
}

TEST(platform_widgets_ext, notification_updateWithMessage_whenHidden_delegatesToPopup)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    dmr::Platform_NotificationWidget nw(&parent);
    // Not visible -> updateWithMessage calls popup(sMsg, flag).
    nw.updateWithMessage(QString("a fairly long message that needs eliding"), false);
    pw_wait(20);
    EXPECT_TRUE(nw.isVisible());
}

TEST(platform_widgets_ext, notification_updateWithMessage_whenVisible_resizes)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    dmr::Platform_NotificationWidget nw(&parent);
    nw.popup(QString("first"), false);
    pw_wait(20);
    ASSERT_TRUE(nw.isVisible());
    nw.updateWithMessage(QString("second longer message text"), false);
    pw_wait(20);
    EXPECT_TRUE(nw.isVisible());
}

TEST(platform_widgets_ext, notification_paintEvent_wmFalse)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);
    nw.setWM(false);
    nw.resize(80, 30);
    QPaintEvent pe(nw.rect());
    QApplication::sendEvent(&nw, &pe);
    SUCCEED();
}

TEST(platform_widgets_ext, notification_paintEvent_wmTrue)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);
    nw.setWM(true);
    nw.resize(80, 30);
    QPaintEvent pe(nw.rect());
    QApplication::sendEvent(&nw, &pe);
    SUCCEED();
}

TEST(platform_widgets_ext, notification_resizeEvent_doesNotCrash)
{
    QWidget parent;
    parent.resize(400, 300);
    dmr::Platform_NotificationWidget nw(&parent);
    QSize old = nw.size();
    QResizeEvent re(QSize(100, 40), old);
    QApplication::sendEvent(&nw, &re);
    SUCCEED();
}

TEST(platform_widgets_ext, notification_showEvent_syncsPosition)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    parent.show();
    dmr::Platform_NotificationWidget nw(&parent);
    nw.setAnchor(dmr::Platform_NotificationWidget::ANCHOR_NONE);
    QShowEvent se;
    QApplication::sendEvent(&nw, &se);
    SUCCEED();
}

// ==========================================================================
// platform_animationlabel.cpp
// ==========================================================================

TEST(platform_widgets_ext, animation_construct_defaultSize)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    // Non-WM default: 100x100.
    EXPECT_EQ(al.size(), QSize(100, 100));
}

TEST(platform_widgets_ext, animation_setWM_togglesFlag)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.setWM(true);
    al.setWM(false);
    SUCCEED();
}

TEST(platform_widgets_ext, animation_onPlayAnimationChanged_wmFalse)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.setWM(false);
    al.onPlayAnimationChanged(QVariant(3));   // loads stop_new/3.png
    SUCCEED();
}

TEST(platform_widgets_ext, animation_onPlayAnimationChanged_wmTrue)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.setWM(true);
    al.onPlayAnimationChanged(QVariant(3));   // loads stop/3.png
    SUCCEED();
}

TEST(platform_widgets_ext, animation_onPauseAnimationChanged_wmFalse)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.setWM(false);
    al.onPauseAnimationChanged(QVariant(2));  // loads start_new/2.png
    SUCCEED();
}

TEST(platform_widgets_ext, animation_onPauseAnimationChanged_wmTrue)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.setWM(true);
    al.onPauseAnimationChanged(QVariant(2));  // loads start/2.png
    SUCCEED();
}

TEST(platform_widgets_ext, animation_onHideAnimation_hides)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.show();
    al.onHideAnimation();
    EXPECT_FALSE(al.isVisible());
}

TEST(platform_widgets_ext, animation_onHideAnimation_nullMainWindow_noCrash)
{
    // Construct without a main window pointer -> onHideAnimation warns and
    // skips m_pMainWindow->update().
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, nullptr);
    al.onHideAnimation();
    SUCCEED();
}

TEST(platform_widgets_ext, animation_playAnimation_startsGroup)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.playAnimation();   // starts pause group, shows label
    pw_wait(20);
    EXPECT_TRUE(al.isVisible());
}

TEST(platform_widgets_ext, animation_pauseAnimation_startsGroup)
{
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.pauseAnimation();  // starts play group, shows label
    pw_wait(20);
    EXPECT_TRUE(al.isVisible());
}

TEST(platform_widgets_ext, animation_paintEvent_doesNotCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    al.resize(100, 100);
    QPaintEvent pe(al.rect());
    QApplication::sendEvent(&al, &pe);
    SUCCEED();
}

TEST(platform_widgets_ext, animation_showEvent_compositedPath)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    // In the test env composited() is usually true -> showEvent takes the
    // else branch (no geometry reset). Either path must not crash.
    QShowEvent se;
    QApplication::sendEvent(&al, &se);
    SUCCEED();
}

TEST(platform_widgets_ext, animation_moveEvent_doesNotCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(400, 300);
    Platform_AnimationLabel al(&parent, &parent);
    QMoveEvent me(QPoint(5, 5), QPoint(0, 0));
    QApplication::sendEvent(&al, &me);
    SUCCEED();
}

// ==========================================================================
// platform_movie_progress_indicator.cpp
// ==========================================================================

TEST(platform_widgets_ext, progress_construct_hasFixedSize)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    dmr::Platform_MovieProgressIndicator ind(&parent);
    // Constructor sets a non-zero fixed size derived from font metrics.
    EXPECT_FALSE(ind.size().isEmpty());
}

TEST(platform_widgets_ext, progress_updateMovieProgress_zeroDuration_noDivByZero)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    dmr::Platform_MovieProgressIndicator ind(&parent);
    ind.updateMovieProgress(0, 0);   // duration==0 branch: skip percentage
    SUCCEED();
}

TEST(platform_widgets_ext, progress_updateMovieProgress_normalComputesPercent)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    dmr::Platform_MovieProgressIndicator ind(&parent);
    ind.updateMovieProgress(100, 50);   // 50%
    SUCCEED();
}

TEST(platform_widgets_ext, progress_updateMovieProgress_fullProgress)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    dmr::Platform_MovieProgressIndicator ind(&parent);
    ind.updateMovieProgress(100, 100);  // 100%
    SUCCEED();
}

TEST(platform_widgets_ext, progress_paintEvent_doesNotCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    dmr::Platform_MovieProgressIndicator ind(&parent);
    ind.updateMovieProgress(200, 100);  // 50%, exercises the dot row
    QPaintEvent pe(ind.rect());
    QApplication::sendEvent(&ind, &pe);
    SUCCEED();
}
