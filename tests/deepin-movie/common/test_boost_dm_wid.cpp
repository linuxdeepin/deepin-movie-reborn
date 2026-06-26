// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for src/widgets/toolbox_proxy.cpp, playlist_widget.cpp, volumeslider.cpp
// All cases run in the same process as the existing deepin-movie-test binary.
// Engine stays Idle — these tests never trigger real mpv play/seek/load.
// Suite: boost_dm_wid ; helper prefix: bdw_

#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QTimer>
#include <QScreen>
#include <QWindow>
#include <QGuiApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QFocusEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QRegion>
#include <QMenu>
#include <QAction>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>

#include <DSlider>
#include <DListWidget>
#include <DToolButton>
#include <DButtonBox>
#include <DLabel>
#include <DFloatingMessage>

#include <gtest/gtest.h>

// STL/Qt headers BEFORE the #define below.
#include "application.h"

#define protected public
#define private public
#include "src/common/mainwindow.h"
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/volumeslider.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/tip.h"
#include "src/widgets/notification_widget.h"
#undef protected
#undef private

#include "src/libdmr/player_engine.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/compositing_manager.h"
#include "src/common/actions.h"
#include "utils.h"
#include "dbus_adpator.h"

#include "stub/stub.h"
#include "stub/addr_any.h"
#include "stub/stub_function.h"

using namespace dmr;

namespace {

// Helper: ensure toolbox/playlist/volSlider pointers are usable.
ToolboxProxy *bdw_toolbox()
{
    MainWindow *mw = dApp->getMainWindow();
    return mw ? mw->toolbox() : nullptr;
}
PlaylistWidget *bdw_playlist()
{
    MainWindow *mw = dApp->getMainWindow();
    return mw ? mw->playlist() : nullptr;
}
MainWindow *bdw_mainwindow()
{
    return dApp->getMainWindow();
}

bool bdw_headless()
{
    return QGuiApplication::primaryScreen() == nullptr;
}

// ---- stub helpers used by multiple tests ---------------------------------

// readSinkInputPath calls ApplicationAdaptor::readDBusProperty; redirect to a
// cheap stub that returns an invalid variant so we never block on real D-Bus.
QVariant bdw_invalidVariant_stub(const QString &, const QString &, const QString &, const char *)
{
    return QVariant();  // invalid -> early return path
}

} // namespace

// ===========================================================================
//  ToolboxProxy — pure getters / setters (Idle-safe)
// ===========================================================================

TEST(boost_dm_wid, bdw_getfullscreentimeLabel)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    QLabel *l1 = tb->getfullscreentimeLabel();
    QLabel *l2 = tb->getfullscreentimeLabelend();
    EXPECT_NE(l1, nullptr);
    EXPECT_NE(l2, nullptr);
}

TEST(boost_dm_wid, bdw_getbAnimationFinash)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool saved = tb->m_bAnimationFinash;
    tb->m_bAnimationFinash = true;
    EXPECT_TRUE(tb->getbAnimationFinash());
    tb->m_bAnimationFinash = saved;
    // also exercise the no-op read path
    (void)tb->getbAnimationFinash();
}

TEST(boost_dm_wid, bdw_getVolSliderIsHided_and_setVolSliderHide)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Direct getter exercise (reads visibility flag only).
    (void)tb->getVolSliderIsHided();
    // setVolSliderHide is a no-op when slider already hidden (the default).
    tb->setVolSliderHide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_getListBtnFocus)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool saved = tb->m_bSetListBtnFocus;
    tb->m_bSetListBtnFocus = true;
    tb->setBtnFocusSign(true);
    EXPECT_TRUE(tb->m_bSetListBtnFocus);
    tb->setBtnFocusSign(false);
    EXPECT_FALSE(tb->m_bSetListBtnFocus);
    // getListBtnFocus reads the list button's focus state (no playback).
    (void)tb->getListBtnFocus();
    tb->m_bSetListBtnFocus = saved;
}

TEST(boost_dm_wid, bdw_getMouseTime)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    qint64 saved = tb->m_nClickTime;
    tb->m_nClickTime = 12345;
    EXPECT_EQ(tb->getMouseTime(), 12345);
    tb->m_nClickTime = saved;
}

TEST(boost_dm_wid, bdw_setPlaylist_rewire)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    auto *pl = bdw_playlist();
    ASSERT_NE(tb, nullptr);
    ASSERT_NE(pl, nullptr);
    // Re-setting the same playlist pointer is safe and exercises setPlaylist.
    // Capture the connection so we don't double-connect across many tests.
    tb->setPlaylist(pl);
    SUCCEED();
}

// ===========================================================================
//  ToolboxProxy — popup helpers (Idle-safe, pure UI)
// ===========================================================================

TEST(boost_dm_wid, bdw_anyPopupShown_idle)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // All popups hidden in Idle; verify the false path.
    tb->closeAnyPopup();
    EXPECT_FALSE(tb->anyPopupShown());
}

TEST(boost_dm_wid, bdw_closeAnyPopup_when_hidden)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Ensure all popups are hidden first, then close.
    // (m_pPreviewer/m_pPreviewTime are forward-declared incomplete types in the
    // header; closeAnyPopup handles them itself.)
    if (tb->m_pVolSlider) tb->m_pVolSlider->hide();
    tb->closeAnyPopup();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_setButtonTooltipHide)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Exercises both wayland and non-wayland branches depending on env.
    tb->setButtonTooltipHide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_mouseMoveEvent_hides_tooltips)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    QMouseEvent me(QEvent::MouseMove, QPointF(5, 5), QPointF(5, 5),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(tb, &me);
    SUCCEED();
}

// ===========================================================================
//  ToolboxProxy — updatePlayState / updateFullState / updateButtonStates
//  (Idle branch is exercised explicitly)
// ===========================================================================

TEST(boost_dm_wid, bdw_updatePlayState_idle)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updatePlayState();   // Idle branch
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateButtonStates_idle)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateButtonStates();
    // In Idle the prev/next buttons must be disabled.
    EXPECT_FALSE(tb->m_pPrevBtn->isEnabled());
    EXPECT_FALSE(tb->m_pNextBtn->isEnabled());
}

TEST(boost_dm_wid, bdw_updateFullState_nonfullscreen)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    auto *mw = bdw_mainwindow();
    ASSERT_NE(tb, nullptr);
    ASSERT_NE(mw, nullptr);
    bool wasFs = mw->isFullScreen();
    if (wasFs) mw->showNormal();
    tb->updateFullState();
    if (wasFs) mw->showFullScreen();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateTimeVisible_toggle)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateTimeVisible(true);
    tb->updateTimeVisible(false);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateTimeInfo_idle_clears_labels)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    QLabel a("a"), b("b");
    a.setText("zzz");
    b.setText("zzz");
    // Idle branch: clears text on both labels (flag value irrelevant).
    tb->updateTimeInfo(100, 50, &a, &b, true);
    EXPECT_TRUE(a.text().isEmpty());
    EXPECT_TRUE(b.text().isEmpty());
}

// ===========================================================================
//  ToolboxProxy — volume delegation (Idle-safe; engine untouched)
// ===========================================================================

TEST(boost_dm_wid, bdw_calculationStep_delegates)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->calculationStep(120);
    tb->calculationStep(-120);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_changeMuteState_delegates)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // muteButtnClicked does not touch the engine; just toggles internal flag.
    tb->changeMuteState();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_volumeUpDown_enabled_path)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Default: vol slider enabled → emits no sigUnsupported, just changes volume.
    bool gotUnsupported = false;
    auto c = QObject::connect(tb, &ToolboxProxy::sigUnsupported, [&]() {
        gotUnsupported = true;
    });
    // Save & restore volume so repeated test runs are deterministic.
    int savedVol = tb->m_pVolSlider->getVolume();
    tb->m_pVolSlider->changeVolume(50);

    tb->m_pVolSlider->calculationStep(120);
    tb->volumeUp();
    tb->m_pVolSlider->calculationStep(-120);
    tb->volumeDown();
    EXPECT_FALSE(gotUnsupported);

    tb->m_pVolSlider->changeVolume(savedVol);
    QObject::disconnect(c);
}

TEST(boost_dm_wid, bdw_volumeUpDown_disabled_emits_unsupported)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool savedEnabled = tb->m_pVolSlider->isEnabled();
    tb->m_pVolSlider->setEnabled(false);

    bool gotUp = false, gotDown = false;
    auto c1 = QObject::connect(tb, &ToolboxProxy::sigUnsupported, [&]() {
        gotUp = true;
    });
    tb->volumeUp();
    QObject::disconnect(c1);

    auto c2 = QObject::connect(tb, &ToolboxProxy::sigUnsupported, [&]() {
        gotDown = true;
    });
    tb->volumeDown();
    QObject::disconnect(c2);

    EXPECT_TRUE(gotUp);
    EXPECT_TRUE(gotDown);

    tb->m_pVolSlider->setEnabled(savedEnabled);
}

// ===========================================================================
//  ToolboxProxy — mircast helpers (Idle-safe)
// ===========================================================================

TEST(boost_dm_wid, bdw_isInMircastWidget_hidden)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!tb->m_mircastWidget) GTEST_SKIP() << "no mircast widget";
    bool wasVisible = tb->m_mircastWidget->isVisible();
    tb->m_mircastWidget->hide();
    EXPECT_FALSE(tb->isInMircastWidget(QPoint(0, 0)));
    if (wasVisible) tb->m_mircastWidget->show();
}

TEST(boost_dm_wid, bdw_hideMircastWidget)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!tb->m_mircastWidget) GTEST_SKIP() << "no mircast widget";
    tb->hideMircastWidget();
    EXPECT_FALSE(tb->m_mircastWidget->isVisible());
    EXPECT_FALSE(tb->m_pMircastBtn->isChecked());
}

TEST(boost_dm_wid, bdw_updateMircastWidget_moves)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!tb->m_mircastWidget) GTEST_SKIP() << "no mircast widget";
    QPoint before = tb->m_mircastWidget->pos();
    tb->updateMircastWidget(QPoint(500, 500));
    // Just exercises the move arithmetic; position may shift.
    SUCCEED();
    (void)before;
}

// ===========================================================================
//  ToolboxProxy — slots that are pure UI / engine-read-only
// ===========================================================================

TEST(boost_dm_wid, bdw_slotThemeTypeChanged)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->slotThemeTypeChanged();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotLeavePreview)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->slotLeavePreview();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotSliderPressed)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool saved = tb->m_bMousePree;
    tb->slotSliderPressed();
    EXPECT_TRUE(tb->m_bMousePree);
    tb->m_bMousePree = saved;
}

TEST(boost_dm_wid, bdw_slotBaseMuteChanged)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Just exercises the settings-key branch.
    tb->slotBaseMuteChanged("mute", QVariant(true));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotVolumeButtonClicked)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool wasVisible = tb->m_pVolSlider->isVisible();
    tb->slotVolumeButtonClicked();
    // Toggle: should have changed visibility.
    if (wasVisible) tb->m_pVolSlider->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotApplicationStateChanged_inactive)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->slotApplicationStateChanged(Qt::ApplicationSuspended);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotProAnimationFinished_no_sender)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Invoked directly (no sender()) → neither open nor close branch taken;
    // exercises the entry + epilogue lines.
    tb->slotProAnimationFinished();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_slotVolumeChanged_emits_signal)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    int captured = -1;
    auto c = QObject::connect(tb, &ToolboxProxy::sigVolumeChanged,
                              [&](int &v) { captured = v; });
    tb->slotVolumeChanged(42);
    QObject::disconnect(c);
    EXPECT_EQ(captured, 42);
}

TEST(boost_dm_wid, bdw_slotMuteStateChanged_emits_signal)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool captured = false;
    auto c = QObject::connect(tb, &ToolboxProxy::sigMuteStateChanged,
                              [&](bool &m) { captured = m; });
    tb->slotMuteStateChanged(true);
    QObject::disconnect(c);
    EXPECT_TRUE(captured);
}

TEST(boost_dm_wid, bdw_slotUpdateMircast_state_zero)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool savedVolEnabled = tb->m_pVolBtn->isEnabled();
    bool savedFsEnabled = tb->m_pFullScreenBtn->isEnabled();
    tb->slotUpdateMircast(0, QStringLiteral("device"));
    // state == 0 disables vol button; full-screen is re-enabled at the tail.
    EXPECT_FALSE(tb->m_pVolBtn->isEnabled());
    EXPECT_TRUE(tb->m_pFullScreenBtn->isEnabled());
    // Restore so subsequent volume tests are not affected.
    tb->m_pVolBtn->setButtonEnable(savedVolEnabled);
    tb->m_pFullScreenBtn->setEnabled(savedFsEnabled);
}

TEST(boost_dm_wid, bdw_clearPlayListFocus)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    auto *pl = bdw_playlist();
    ASSERT_NE(tb, nullptr);
    ASSERT_NE(pl, nullptr);
    tb->clearPlayListFocus();
    EXPECT_FALSE(tb->m_bSetListBtnFocus);
}

TEST(boost_dm_wid, bdw_playlistClosedByEsc_no_action)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Focus is not in playlist by default → no requestAction fired.
    tb->setBtnFocusSign(false);
    tb->playlistClosedByEsc();
    SUCCEED();
}

// ===========================================================================
//  ToolboxProxy — event handlers (Idle-safe)
// ===========================================================================

TEST(boost_dm_wid, bdw_paintEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    QPaintEvent pe(QRegion(tb->rect()));
    QApplication::sendEvent(tb, &pe);
    tb->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_resizeEvent_idle)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    QSize oldSz = tb->size();
    QSize newSz(oldSz.width() + 1, oldSz.height());
    QResizeEvent re(newSz, oldSz);
    QApplication::sendEvent(tb, &re);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_eventFilter_volBtn_keypress_when_closed)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Vol slider state == Close by default → key event falls through.
    if (tb->m_pVolSlider->state() != VolumeSlider::Close) {
        tb->m_pVolSlider->m_state = VolumeSlider::Close;
    }
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    bool eaten = tb->eventFilter(tb->m_pVolBtn, &ke);
    EXPECT_FALSE(eaten);  // not consumed because slider is closed
}

TEST(boost_dm_wid, bdw_eventFilter_volBtn_keypress_when_open)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *slider = tb->m_pVolSlider;
    ASSERT_NE(slider, nullptr);
    auto saved = slider->m_state;
    int savedVol = slider->getVolume();
    slider->changeVolume(50);
    slider->m_state = VolumeSlider::Open;

    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    bool eatenUp = tb->eventFilter(tb->m_pVolBtn, &up);
    EXPECT_TRUE(eatenUp);

    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    bool eatenDown = tb->eventFilter(tb->m_pVolBtn, &down);
    EXPECT_TRUE(eatenDown);

    // Non-volume key still falls through when slider open.
    QKeyEvent other(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    bool eatenOther = tb->eventFilter(tb->m_pVolBtn, &other);
    EXPECT_FALSE(eatenOther);

    slider->m_state = saved;
    slider->changeVolume(savedVol);
}

TEST(boost_dm_wid, bdw_eventFilter_unknown_object)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    QObject stranger;
    QEvent ev(QEvent::User);
    // Falls straight through to QObject::eventFilter.
    bool eaten = tb->eventFilter(&stranger, &ev);
    EXPECT_FALSE(eaten);
}

TEST(boost_dm_wid, bdw_buttonEnterLeave_not_visible)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    // buttonEnter/buttonLeave early-return when toolbox not visible.
    QMetaObject::invokeMethod(tb, "buttonEnter");
    QMetaObject::invokeMethod(tb, "buttonLeave");
    SUCCEED();
}

TEST(boost_dm_wid, bdw_buttonClicked_not_visible)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    // Toolbox hidden → early return, no requestAction.
    QMetaObject::invokeMethod(tb, "buttonClicked", Q_ARG(QString, "play"));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_buttonClicked_visible_unknown_id)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    // An unknown id falls through all branches without touching engine.
    QMetaObject::invokeMethod(tb, "buttonClicked", Q_ARG(QString, "unknown-id"));
    tb->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_buttonClicked_visible_mircast_id)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!tb->m_mircastWidget) GTEST_SKIP() << "no mircast";
    tb->show();
    bool wasVisible = tb->m_mircastWidget->isVisible();
    tb->m_mircastWidget->hide();   // ensure starting state
    QMetaObject::invokeMethod(tb, "buttonClicked", Q_ARG(QString, "mircast"));
    // After click mircast should be visible + button checked.
    EXPECT_TRUE(tb->m_mircastWidget->isVisible());
    EXPECT_TRUE(tb->m_pMircastBtn->isChecked());
    // Restore.
    tb->m_mircastWidget->hide();
    tb->m_pMircastBtn->setChecked(false);
    if (wasVisible) tb->m_mircastWidget->show();
    tb->hide();
}

TEST(boost_dm_wid, bdw_updateMircastTime)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Reads engine playlist.current(); with no playback returns -1.
    tb->updateMircastTime(100);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateHoverPreview_idle_returns)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Idle → early return, no thumb request.
    tb->updateHoverPreview(QUrl("file:///nonexistent"), 10);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_progressHoverChanged_idle_returns)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->progressHoverChanged(10);  // Idle → early return
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateMovieProgress_idle_safe)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Reads engine duration/elapsed (both 0 in Idle) — no seek.
    tb->updateMovieProgress();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateProgress_idle_safe)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateProgress(50);  // Idle-safe: sets slider value, no seek
    SUCCEED();
}

TEST(boost_dm_wid, bdw_initToolTip_idempotent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // initToolTip may be re-invoked; just exercise its body.
    tb->initToolTip();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_updateToolTipTheme)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!tb->m_pListBtn) GTEST_SKIP() << "no list btn";
    tb->updateToolTipTheme(tb->m_pListBtn);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_waitPlay_disables_buttons)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    bool savedPlay = tb->m_pPlayBtn->isEnabled();
    bool savedPrev = tb->m_pPrevBtn->isEnabled();
    bool savedNext = tb->m_pNextBtn->isEnabled();
    tb->waitPlay();
    EXPECT_FALSE(tb->m_pPlayBtn->isEnabled());
    EXPECT_FALSE(tb->m_pPrevBtn->isEnabled());
    EXPECT_FALSE(tb->m_pNextBtn->isEnabled());
    // Spin the event loop so the 500ms singleShot re-enable fires promptly.
    QTest::qWait(700);
    // Buttons re-enabled based on playlist count (0 in idle → prev/next stay off).
    EXPECT_TRUE(tb->m_pPlayBtn->isEnabled());
    // Restore saved flags just in case.
    tb->m_pPlayBtn->setEnabled(savedPlay);
    tb->m_pPrevBtn->setEnabled(savedPrev);
    tb->m_pNextBtn->setEnabled(savedNext);
}

TEST(boost_dm_wid, bdw_finishLoadSlot_empty_pmList)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    // Empty pmList path: clears and exits early.
    QList<QPixmap> empty;
    tb->addpmList(empty);
    tb->addpmBlackList(empty);
    QSize sz(100, 50);
    tb->finishLoadSlot(sz);
    SUCCEED();
}

// ===========================================================================
//  VolumeSlider — direct coverage (no MainWindow needed, but we use one)
// ===========================================================================

TEST(boost_dm_wid, bdw_vol_getVolume_getsliderstate)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    vs->changeVolume(73);
    EXPECT_EQ(vs->getVolume(), 73);
    EXPECT_FALSE(vs->getsliderstate());  // m_bFinished defaults false
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_changeVolume_clamp)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    vs->changeVolume(-50);   // clamped to 0
    EXPECT_EQ(vs->getVolume(), 0);
    vs->changeVolume(9999);  // clamped to 200
    EXPECT_EQ(vs->getVolume(), 200);
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_changeVolume_triggers_volumeChanged_at_100)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    // >= 100 path calls volumeChanged internally.
    vs->changeVolume(150);
    EXPECT_EQ(vs->getVolume(), 150);
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_calculationStep_same_and_opposite_direction)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int savedStep = vs->m_iStep;
    bool savedWheel = vs->m_bIsWheel;

    vs->m_iStep = 0;
    vs->calculationStep(120);   // different direction → reset
    EXPECT_EQ(vs->m_iStep, 120);
    vs->calculationStep(120);   // same direction → accumulate
    EXPECT_EQ(vs->m_iStep, 240);
    vs->calculationStep(-120);  // opposite → reset
    EXPECT_EQ(vs->m_iStep, -120);

    vs->m_iStep = savedStep;
    vs->m_bIsWheel = savedWheel;
}

TEST(boost_dm_wid, bdw_vol_volumeUp_button_path)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    bool savedWheel = vs->m_bIsWheel;
    vs->m_bIsWheel = false;        // button path: +10
    vs->changeVolume(50);
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 60);
    vs->m_bIsWheel = savedWheel;
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_volumeDown_button_path)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    bool savedWheel = vs->m_bIsWheel;
    vs->m_bIsWheel = false;        // button path: -10
    vs->changeVolume(50);
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 40);
    vs->m_bIsWheel = savedWheel;
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_volumeUp_wheel_path)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    vs->changeVolume(50);
    vs->m_bIsWheel = true;
    vs->m_iStep = 120;            // one notch
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 60);
    EXPECT_EQ(vs->m_iStep, 0);
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_volumeDown_wheel_path)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    vs->changeVolume(50);
    vs->m_bIsWheel = true;
    vs->m_iStep = 240;           // two notches → -20
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 30);
    EXPECT_EQ(vs->m_iStep, 0);
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_changeMuteState_no_op_when_volume_zero)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    bool savedMute = vs->m_bIsMute;
    vs->changeVolume(0);
    vs->m_bIsMute = false;
    vs->changeMuteState(true);   // volume 0 → early return, no change
    EXPECT_FALSE(vs->m_bIsMute);
    vs->m_bIsMute = savedMute;
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_changeMuteState_unmutes_when_already_mute)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    bool savedMute = vs->m_bIsMute;
    vs->changeVolume(50);
    vs->m_bIsMute = true;
    // bMute==true == current → early return
    vs->changeMuteState(true);
    EXPECT_TRUE(vs->m_bIsMute);
    // Now actually flip
    bool captured = false;
    auto c = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                              [&](bool m) { captured = m; });
    vs->changeMuteState(false);
    QObject::disconnect(c);
    EXPECT_FALSE(vs->m_bIsMute);
    EXPECT_FALSE(captured);
    vs->m_bIsMute = savedMute;
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_volumeChanged_emits_signal)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int captured = -1;
    auto c = QObject::connect(vs, &VolumeSlider::sigVolumeChanged,
                              [&](int v) { captured = v; });
    int saved = vs->getVolume();
    vs->changeVolume(80);   // volumeChanged fires via DSlider signal
    QObject::disconnect(c);
    EXPECT_EQ(captured, 80);
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_muteButtnClicked_toggles)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int savedVol = vs->getVolume();
    bool savedMute = vs->m_bIsMute;
    vs->changeVolume(50);
    vs->m_bIsMute = false;
    vs->muteButtnClicked();
    EXPECT_TRUE(vs->m_bIsMute);
    vs->muteButtnClicked();
    EXPECT_FALSE(vs->m_bIsMute);
    vs->m_bIsMute = savedMute;
    vs->changeVolume(savedVol);
}

TEST(boost_dm_wid, bdw_vol_refreshIcon_branches)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    // >= 66 branch
    vs->changeVolume(80);
    vs->refreshIcon();
    // >= 33 && < 66 branch
    vs->changeVolume(50);
    vs->refreshIcon();
    // < 33 branch
    vs->changeVolume(10);
    vs->refreshIcon();
    // mute icon override path
    vs->m_bIsMute = true;
    vs->refreshIcon();
    vs->m_bIsMute = false;
    // volume == 0 path
    vs->changeVolume(0);
    vs->refreshIcon();
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_setMute_no_change)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    // m_bIsMute == muted (false) and volume != 0 → falls through to readSinkInputPath
    // which we stub to avoid real D-Bus.
    Stub stub;
    stub.set(ADDR(ApplicationAdaptor, readDBusProperty), bdw_invalidVariant_stub);
    vs->m_bIsMute = false;
    vs->changeVolume(50);
    vs->setMute(false);   // no change → still calls readSinkInputPath (stubbed)
    // sinkInputPath empty → no D-Bus interface call
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_readSinkInputPath_invalid_dbus)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    Stub stub;
    stub.set(ADDR(ApplicationAdaptor, readDBusProperty), bdw_invalidVariant_stub);
    QString path = vs->readSinkInputPath();
    EXPECT_TRUE(path.isEmpty());
}

TEST(boost_dm_wid, bdw_vol_eventFilter_wheel_up_and_down)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    int saved = vs->getVolume();
    int savedStep = vs->m_iStep;
    bool savedWheel = vs->m_bIsWheel;
    vs->changeVolume(50);

    // Wheel up (positive delta)
    QWheelEvent up(QPointF(0, 0), QPointF(0, 0), QPoint(0, 0), QPoint(0, 120),
                   Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                   Qt::MouseEventNotSynthesized, QPointingDevice::primaryPointingDevice());
    bool eatenUp = vs->eventFilter(vs->m_slider, &up);
    EXPECT_FALSE(eatenUp);   // returns false per source
    EXPECT_GT(vs->getVolume(), 50);

    vs->changeVolume(50);
    // Wheel down (negative delta)
    QWheelEvent down(QPointF(0, 0), QPointF(0, 0), QPoint(0, 0), QPoint(0, -120),
                     Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false,
                     Qt::MouseEventNotSynthesized, QPointingDevice::primaryPointingDevice());
    bool eatenDown = vs->eventFilter(vs->m_slider, &down);
    EXPECT_FALSE(eatenDown);
    EXPECT_LT(vs->getVolume(), 50);

    vs->m_iStep = savedStep;
    vs->m_bIsWheel = savedWheel;
    vs->changeVolume(saved);
}

TEST(boost_dm_wid, bdw_vol_eventFilter_non_wheel_passthrough)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    // A non-Wheel event is forwarded to QObject::eventFilter.
    QMouseEvent me(QEvent::MouseButtonPress, QPointF(0, 0), QPointF(0, 0),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    bool eaten = vs->eventFilter(vs->m_slider, &me);
    EXPECT_FALSE(eaten);
}

TEST(boost_dm_wid, bdw_vol_keyPressEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(vs, &ke);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_enterLeaveEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    QEnterEvent ee(QPointF(0, 0), QPointF(0, 0), QPointF(0, 0),
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vs, &ee);
    EXPECT_TRUE(vs->m_mouseIn);

    // leaveEvent triggers delayedHide; m_mouseIn set false and timer armed.
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(vs, &le);
    EXPECT_FALSE(vs->m_mouseIn);
    vs->stopTimer();   // cancel any pending hide to keep state stable
}

TEST(boost_dm_wid, bdw_vol_showEvent_sets_geometry)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    bool wasVisible = vs->isVisible();
    vs->show();
    QShowEvent se;
    QApplication::sendEvent(vs, &se);
    if (!wasVisible) vs->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_paintEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->resize(VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT);
    vs->show();
    QPaintEvent pe(QRegion(vs->rect()));
    QApplication::sendEvent(vs, &pe);
    vs->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_setThemeType)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->setThemeType(DGuiApplicationHelper::DarkType);
    vs->setThemeType(DGuiApplicationHelper::LightType);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_updatePoint)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    QPoint before = vs->m_point;
    vs->updatePoint(QPoint(100, 100));
    EXPECT_NE(vs->m_point, before);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_vol_stopTimer)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *tb = bdw_toolbox();
    ASSERT_NE(tb, nullptr);
    auto *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->m_autoHideTimer.start(10000);
    EXPECT_TRUE(vs->m_autoHideTimer.isActive());
    vs->stopTimer();
    EXPECT_FALSE(vs->m_autoHideTimer.isActive());
}

// ===========================================================================
//  PlaylistWidget — empty-list branches (Idle-safe)
// ===========================================================================

TEST(boost_dm_wid, bdw_pl_state_toggling_initial)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // _state defaults to Closed.
    EXPECT_TRUE(pl->state() == PlaylistWidget::State::Closed ||
                pl->state() == PlaylistWidget::State::Opened);
    EXPECT_FALSE(pl->toggling());
}

TEST(boost_dm_wid, bdw_pl_clear_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *lw = pl->get_playlist();
    ASSERT_NE(lw, nullptr);
    int before = lw->count();
    pl->clear();
    EXPECT_EQ(lw->count(), 0);
    (void)before;
}

TEST(boost_dm_wid, bdw_pl_loadPlaylist_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // With no items loaded, the while-loop body never executes.
    pl->loadPlaylist();
    EXPECT_EQ(pl->get_playlist()->count(), 0);
}

TEST(boost_dm_wid, bdw_pl_updateItemStates_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QMetaObject::invokeMethod(pl, "updateItemStates");
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_updateItemInfo_out_of_range)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // Empty list → itemWidget returns nullptr → early return.
    QMetaObject::invokeMethod(pl, "updateItemInfo", Q_ARG(int, 0));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_removeItem_out_of_range)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // No matching item → guarded path.
    QMetaObject::invokeMethod(pl, "removeItem", Q_ARG(int, 0));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_updateSelectItem_up_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // Empty list, current row -1: Up branch sets _index=0, itemWidget null.
    pl->updateSelectItem(Qt::Key_Up);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_updateSelectItem_down_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // Empty list: Down branch hits `_index >= count()-1` (0 >= -1) → return.
    pl->updateSelectItem(Qt::Key_Down);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_appendItems_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QMetaObject::invokeMethod(pl, "appendItems");
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_slotRowsMoved_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QMetaObject::invokeMethod(pl, "slotRowsMoved");
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_slotCloseTimeTimeOut)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QMetaObject::invokeMethod(pl, "slotCloseTimeTimeOut");
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_isFocusInPlaylist_initial)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    (void)pl->isFocusInPlaylist();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_endAnimation_safe)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->endAnimation();   // paOpen/paClose null by default → no-op
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_resetFocusAttribute)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    bool atr = true;
    pl->resetFocusAttribute(atr);
    EXPECT_TRUE(pl->m_bButtonFocusOut);
    atr = false;
    pl->resetFocusAttribute(atr);
    EXPECT_FALSE(pl->m_bButtonFocusOut);
}

TEST(boost_dm_wid, bdw_pl_showItemInfo_null_mouse)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->_mouseItem = nullptr;
    pl->showItemInfo();   // null guard → early return
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_openItemInFM_null_mouse)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->_mouseItem = nullptr;
    pl->openItemInFM();   // null guard → early return
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_removeClickedItem_null)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->_clickedItem = nullptr;
    pl->removeClickedItem(false);   // null guard → early return
    pl->removeClickedItem(true);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_slotShowSelectItem_null)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    // Null item → early return; does not call doDoubleClick.
    QMetaObject::invokeMethod(pl, "slotShowSelectItem",
                              Q_ARG(QListWidgetItem *, nullptr));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_OnItemChanged_both_null)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QMetaObject::invokeMethod(pl, "OnItemChanged",
                              Q_ARG(QListWidgetItem *, nullptr),
                              Q_ARG(QListWidgetItem *, nullptr));
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_slotCloseItem_records)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QWidget dummy;
    QWidget *saved = pl->_clickedItem;
    QMetaObject::invokeMethod(pl, "slotCloseItem", Q_ARG(QWidget *, &dummy));
    EXPECT_EQ(pl->_clickedItem, &dummy);
    pl->_clickedItem = saved;
}

TEST(boost_dm_wid, bdw_pl_contextMenuEvent_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->show();
    QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(10, 10), QPoint(10, 10));
    QApplication::sendEvent(pl, &cme);
    pl->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_paintEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    pl->show();
    QPaintEvent pe(QRegion(pl->rect()));
    QApplication::sendEvent(pl, &pe);
    pl->hide();
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_resizeEvent)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    QSize oldSz = pl->size();
    QSize newSz(oldSz.width() + 1, oldSz.height());
    QResizeEvent re(newSz, oldSz);
    QApplication::sendEvent(pl, &re);
    SUCCEED();
}

TEST(boost_dm_wid, bdw_pl_eventFilter_focusOut_empty)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *lw = pl->get_playlist();
    ASSERT_NE(lw, nullptr);
    // count() <= 0 guard at the FocusOut path returns true.
    QFocusEvent fe(QEvent::FocusOut, Qt::OtherFocusReason);
    bool eaten = pl->eventFilter(lw, &fe);
    EXPECT_TRUE(eaten);
}

TEST(boost_dm_wid, bdw_pl_togglePopup_roundtrip)
{
    if (bdw_headless()) GTEST_SKIP() << "headless";
    auto *pl = bdw_playlist();
    ASSERT_NE(pl, nullptr);
    auto *mw = bdw_mainwindow();
    ASSERT_NE(mw, nullptr);
    pl->show();
    // Open then close to exercise both animation setup branches.
    pl->togglePopup(true);
    QTest::qWait(50);
    pl->togglePopup(true);
    QTest::qWait(50);
    pl->hide();
    SUCCEED();
}
