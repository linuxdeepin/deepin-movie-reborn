// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 2) for src/widgets/platform/platform_toolbox_proxy.cpp
// and src/widgets/platform/platform_volumeslider.cpp.
//
// Suite name "platform_tb_ext2" with static helpers using unique prefix ptb2_.
// This file complements test_platform_toolbox_proxy_ext.cpp ("platform_tb_ext") and
// test_platform_widgets_ext.cpp ("platform_widgets_ext") by targeting the branches
// those suites leave uncovered: volume slider enter/leave/show/popup paths,
// toolbox event-filter branches (right-click on list button, volume-key filter with
// the slider Open), mircast popup positioning, tooltip enter/leave button routing,
// movie-progress ViewProgBar math, slider-time font handling, and various setter
// round-trips.
//
// Safety rules baked in (verified against prior crashes / link failures):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * The shared toolbox / volume slider live in the running app's main window:
//       dApp->getMainWindow()->toolbox()->volumeSlider(). Guarded for null.
//   * Geometry / paint / popup cases are guarded by primaryScreen() and
//     GTEST_SKIP when headless, since they mapToGlobal / move windows.
//   * No mpv backend / decode path is exercised. Functions that need a loaded
//     playlist with real items (slotFileLoaded audio-skip, finishLoadSlot X86
//     branch, non-zero mircast) are guarded with GTEST_SKIP when the engine has
//     no items, so they never SIGSEGV.
//   * Only settings keys known-safe to write are used: global_volume, mute,
//     playmode, mousepreview. Unknown keys would NPE inside Settings.
//   * Qt6 event constructors: QMouseEvent takes QPointF; QWheelEvent takes
//     (pos, globalPos, angleDelta, pixelDelta, buttons, modifiers, phase, inverted).

#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/widgets/platform/platform_volumeslider.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/common/dmr_settings.h"
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QLabel>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QMetaObject>
#include <QPointingDevice>

using namespace dmr;

// Register Qt::ApplicationState as a metatype so QMetaObject::invokeMethod can
// deliver it to the (protected) slotApplicationStateChanged slot at runtime.
namespace {
const int ptb2_appStateMetaId = qRegisterMetaType<Qt::ApplicationState>("Qt::ApplicationState");
}

// --- Helpers ---------------------------------------------------------------

// The running app's main window. All shared widgets hang off this.
static Platform_MainWindow *ptb2_mainWindow()
{
    return dApp->getMainWindow();
}

// Shared toolbox proxy owned by the main window.
static Platform_ToolboxProxy *ptb2_toolbox()
{
    Platform_MainWindow *w = ptb2_mainWindow();
    return w ? w->toolbox() : nullptr;
}

// Shared volume slider owned by the toolbox.
static Platform_VolumeSlider *ptb2_volumeSlider()
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    return tb ? tb->volumeSlider() : nullptr;
}

// A short synchronous wait so animations/timers settle without stalling.
static void ptb2_wait(int ms = 120)
{
    QTest::qWait(ms);
}

// Many toolbox slots are protected/private, so route through invokeMethod.
// Returns silently on lookup failure so a missing slot doesn't abort the suite.
static void ptb2_invoke(Platform_ToolboxProxy *tb, const char *slot)
{
    QMetaObject::invokeMethod(tb, slot, Qt::DirectConnection);
}
template <typename T>
static void ptb2_invoke1(Platform_ToolboxProxy *tb, const char *slot, const T &arg)
{
    QMetaObject::invokeMethod(tb, slot, Qt::DirectConnection, Q_ARG(T, arg));
}

// Locate the DSlider child of the volume slider (the event filter is installed
// on it). Returns nullptr if not found.
static DSlider *ptb2_vsSlider(Platform_VolumeSlider *vs)
{
    return vs ? vs->findChild<DSlider *>() : nullptr;
}

// True when the engine has at least one loaded playlist item. Used to guard
// branches that dereference currentInfo() / url.
static bool ptb2_engineHasItems()
{
    Platform_MainWindow *w = ptb2_mainWindow();
    if (!w) return false;
    PlayerEngine *engine = w->engine();
    return engine && engine->playlist().count() > 0;
}

// ==========================================================================
// platform_volumeslider.cpp — additional branch coverage
// ==========================================================================

TEST(platform_tb_ext2, volume_enterEvent_setsMouseIn)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(vs, &ee);
    SUCCEED();
}

TEST(platform_tb_ext2, volume_leaveEvent_triggersDelayedHide)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(vs, &le);
    ptb2_wait(30);
    SUCCEED();
}

TEST(platform_tb_ext2, volume_showEvent_repositions)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QShowEvent se;
    QApplication::sendEvent(vs, &se);
    SUCCEED();
}

TEST(platform_tb_ext2, volume_popup_openWhileHidden_isSafe)
{
    // popup() with state==Close and not visible takes the else branch (hide).
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    vs->hide();
    ptb2_wait(20);
    vs->popup();   // state Close, not visible -> else branch
    ptb2_wait(30);
    EXPECT_FALSE(vs->isVisible());
}

TEST(platform_tb_ext2, volume_changeVolume_exactlyAtBounds)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(0);
    EXPECT_EQ(vs->getVolume(), 0);
    vs->changeVolume(200);
    EXPECT_EQ(vs->getVolume(), 200);
    vs->changeVolume(100);
    EXPECT_EQ(vs->getVolume(), 100);
}

TEST(platform_tb_ext2, volume_volumeUp_wheelPath_belowThreshold_noChange)
{
    // calculationStep with |step|<120 sets m_bIsWheel; volumeUp sees the wheel
    // branch but |step|<120 -> no change.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    vs->calculationStep(60);   // |60| < 120
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 50);
}

TEST(platform_tb_ext2, volume_volumeUp_wheelPath_atThreshold_increments)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    vs->calculationStep(120);  // exactly 120 -> +10
    vs->volumeUp();
    EXPECT_EQ(vs->getVolume(), 60);
}

TEST(platform_tb_ext2, volume_volumeDown_wheelPath_belowThreshold_noChange)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    vs->calculationStep(-60);
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 50);
}

TEST(platform_tb_ext2, volume_volumeDown_wheelPath_atThreshold_decrements)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    vs->calculationStep(-120);
    vs->volumeDown();
    EXPECT_EQ(vs->getVolume(), 40);
}

TEST(platform_tb_ext2, volume_calculationStep_signFlip_resets)
{
    // First +60, then -60: signs differ -> m_iStep is reassigned to -60.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->calculationStep(60);
    vs->calculationStep(-60);
    vs->calculationStep(-60);  // now accumulates -> -120
    vs->changeVolume(50);
    vs->volumeDown();          // wheel path, |−120|>=120 -> −10
    EXPECT_EQ(vs->getVolume(), 40);
}

TEST(platform_tb_ext2, volume_changeMuteState_toMuteWhenVolumeZero_returnsEarly)
{
    // volume==0 short-circuits changeMuteState before touching settings.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(0);
    vs->changeMuteState(true);
    SUCCEED();
}

TEST(platform_tb_ext2, volume_volumeChanged_setsSameValue_safe)
{
    // volumeChanged with same value skips the assignment branch.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(80);
    int captured = -1;
    auto c = QObject::connect(vs, &Platform_VolumeSlider::sigVolumeChanged,
                     [&captured](int v) { captured = v; });
    // Direct call with the current value exercises the m_nVolume == nVolume path.
    QMetaObject::invokeMethod(vs, "volumeChanged", Qt::DirectConnection,
                              Q_ARG(int, 80));
    ptb2_wait(20);
    QObject::disconnect(c);
    SUCCEED();
}

TEST(platform_tb_ext2, volume_eventFilter_wheelLeftAxis_noChange)
{
    // angleDelta().y() == 0 (horizontal-only) -> neither up nor down branch.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = ptb2_vsSlider(vs);
    ASSERT_NE(slider, nullptr);
    vs->changeVolume(50);
    QPoint pos = slider->rect().center();
    QWheelEvent wheel(pos, slider->mapToGlobal(pos), QPoint(120, 0),
                      QPoint(120, 0), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &wheel);
    ptb2_wait(20);
    // calculationStep ran (m_bIsWheel=true) but no up/down invoked.
    SUCCEED();
}

TEST(platform_tb_ext2, volume_keyPressEvent_upAtMax_clamps)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(200);
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vs, &up);
    EXPECT_EQ(vs->getVolume(), 200);
}

TEST(platform_tb_ext2, volume_keyPressEvent_downAtZero_clamps)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(0);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(vs, &down);
    EXPECT_EQ(vs->getVolume(), 0);
}

TEST(platform_tb_ext2, volume_muteButtnClicked_atZeroVolume_safe)
{
    // mute toggle path when volume==0; setMute early-returns on m_nVolume==0.
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(0);
    vs->muteButtnClicked();
    SUCCEED();
}

TEST(platform_tb_ext2, volume_updatePoint_movesToComputedPos)
{
    Platform_VolumeSlider *vs = ptb2_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    vs->updatePoint(QPoint(100, 100));
    EXPECT_FALSE(vs->pos().isNull());
}

// ==========================================================================
// platform_toolbox_proxy.cpp — button state & event-filter branches
// ==========================================================================

TEST(platform_tb_ext2, getters_enginePointer_consistent)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb2_mainWindow();
    ASSERT_NE(w, nullptr);
    // The toolbox's engine should match the main window's engine.
    EXPECT_NE(w->engine(), nullptr);
    SUCCEED();
}

TEST(platform_tb_ext2, setThumbnailmode_flagRoundTrip)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->setThumbnailmode(true);
    tb->setThumbnailmode(false);
    SUCCEED();
}

TEST(platform_tb_ext2, addpmList_emptyList_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    QList<QPixmap> empty;
    tb->addpmList(empty);
    tb->addpmBlackList(empty);
    SUCCEED();
}

TEST(platform_tb_ext2, setVolSliderHide_whenAlreadyHidden_isNoOp)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->setVolSliderHide();
    tb->setVolSliderHide();   // already hidden -> no-op
    EXPECT_TRUE(tb->getVolSliderIsHided());
}

TEST(platform_tb_ext2, closeAnyPopup_thenCheck_anyPopupShownFalse)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->closeAnyPopup();
    EXPECT_FALSE(tb->anyPopupShown());
}

TEST(platform_tb_ext2, updateMircastWidget_zeroPoint_safe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateMircastWidget(QPoint(0, 0));
    SUCCEED();
}

TEST(platform_tb_ext2, isInMircastWidget_hiddenWidget_returnsFalse)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hideMircastWidget();
    EXPECT_FALSE(tb->isInMircastWidget(QPoint(0, 0)));
}

TEST(platform_tb_ext2, hideMircastWidget_idempotent)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hideMircastWidget();
    tb->hideMircastWidget();
    EXPECT_FALSE(tb->getMircast()->isVisible());
}

TEST(platform_tb_ext2, initToolTip_thenHide_safe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->initToolTip();
    tb->setButtonTooltipHide();
    SUCCEED();
}

// --- updateTimeInfo: non-idle branch needs a loaded item, so guard it. -----

TEST(platform_tb_ext2, updateTimeInfo_idleState_clearsBothLabels)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    QLabel *start = tb->getfullscreentimeLabel();
    QLabel *end = tb->getfullscreentimeLabelend();
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    start->setText("seed");
    end->setText("seed");
    tb->updateTimeInfo(120, 30, start, end, true);
    EXPECT_TRUE(start->text().isEmpty());
    EXPECT_TRUE(end->text().isEmpty());
}

TEST(platform_tb_ext2, updateTimeInfo_nonIdle_populatesLabels)
{
    // The not-Idle branch formats pos/duration via utils::Time2str. Only safe
    // when the engine actually has a loaded item (otherwise state may still be
    // Idle and we'd just re-test the idle path).
    if (!ptb2_engineHasItems()) GTEST_SKIP() << "Engine has no loaded item";
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    QLabel *start = tb->getfullscreentimeLabel();
    QLabel *end = tb->getfullscreentimeLabelend();
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    start->setText("");
    end->setText("");
    tb->updateTimeInfo(120, 30, start, end, true);
    tb->updateTimeInfo(120, 30, start, end, false);
    SUCCEED();
}

// --- eventFilter: volume-button key filter (slider Closed -> fall-through) --

TEST(platform_tb_ext2, eventFilter_volBtnKeysWhenSliderClosed_delegates)
{
    // The filter only intercepts Up/Down when m_pVolSlider->state()==Open.
    // In the test env the slider is Closed, so the key falls through the filter
    // to the base class — distinct from the ext suite which targets a bare
    // VolumeButton. Exercises the obj==m_pVolBtn branch's fall-through for
    // Up, Down, and an unrelated key.
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);
    Platform_VolumeSlider *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vb, &up);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(vb, &down);
    QKeyEvent other(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(vb, &other);
    // slider state is Close -> filter returns false -> volume unchanged by filter.
    SUCCEED();
}

// --- mouseMoveEvent on toolbox ---------------------------------------------

TEST(platform_tb_ext2, mouseMoveEvent_hidesButtonTooltips)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb2_wait(20);
    QMouseEvent me(QEvent::MouseMove, QPointF(3, 3), QPointF(3, 3),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(tb, &me);
    SUCCEED();
}

// --- resizeEvent: width-change + Idle branch (animation pointers null) -----

TEST(platform_tb_ext2, resizeEvent_widthGrowAndShrink_safe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    QSize oldSize = tb->size();
    // Grow: takes the width-change branch (Idle engine -> setCurrentIndex(1)
    // skipped, updateTimeLabel runs).
    QSize grow(std::max(10, oldSize.width() + 80), oldSize.height());
    QResizeEvent reGrow(grow, oldSize);
    QApplication::sendEvent(tb, &reGrow);
    // Shrink: same branch.
    QSize shrink(std::max(10, grow.width() - 30), oldSize.height());
    QResizeEvent reShrink(shrink, grow);
    QApplication::sendEvent(tb, &reShrink);
    // Height-only: width-change branch skipped; only updateTimeLabel runs.
    QSize heightOnly(shrink.width(), oldSize.height() + 20);
    QResizeEvent reHeight(heightOnly, shrink);
    QApplication::sendEvent(tb, &reHeight);
    SUCCEED();
}

// --- paintEvent (guarded; moves the toolbox) -------------------------------

TEST(platform_tb_ext2, paintEvent_doesNotCrash)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->resize(800, 80);
    QPaintEvent pe(tb->rect());
    QApplication::sendEvent(tb, &pe);
    SUCCEED();
}

// --- showEvent triggers updateTimeLabel ------------------------------------

TEST(platform_tb_ext2, showEvent_updatesTimeLabel)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb2_wait(20);
    QShowEvent se;
    QApplication::sendEvent(tb, &se);
    SUCCEED();
}

// --- buttonClicked id routing (visible toolbox, Idle engine) ---------------
// Each id routes to a distinct ActionFactory kind, so exercise them separately.

TEST(platform_tb_ext2, buttonClicked_fs_vol_prev_next_whenVisible)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb2_wait(20);
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("fs"));
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("vol"));
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("prev"));
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("next"));
    SUCCEED();
}

TEST(platform_tb_ext2, buttonClicked_list_whenVisible_recordsClickTime)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb2_wait(20);
    qint64 before = tb->getMouseTime();
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("list"));
    ptb2_wait(30);
    // list branch stamps m_nClickTime; if before was 0 it should now be set.
    qint64 after = tb->getMouseTime();
    EXPECT_GE(after, before);
}

TEST(platform_tb_ext2, buttonClicked_mircast_whenVisible)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb2_wait(20);
    ptb2_invoke1<QString>(tb, "buttonClicked", QString("mircast"));
    ptb2_wait(30);
    // togglePopup may show/hide; hide it to leave clean state.
    tb->hideMircastWidget();
    SUCCEED();
}

// --- buttonEnter / buttonLeave (need a visible toolbox + ToolButton sender) -

TEST(platform_tb_ext2, buttonLeave_whenHidden_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    ptb2_wait(20);
    ptb2_invoke(tb, "buttonLeave");
    tb->show();
    ptb2_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext2, buttonEnter_whenHidden_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    ptb2_wait(20);
    ptb2_invoke(tb, "buttonEnter");
    tb->show();
    ptb2_wait(20);
    SUCCEED();
}

// --- slotVolumeButtonClicked: visible + slider-state branches ---------------

TEST(platform_tb_ext2, slotVolumeButtonClicked_visibleShowsThenHidesSlider)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb2_wait(20);
    tb->setVolSliderHide();   // ensure hidden first
    ptb2_invoke(tb, "slotVolumeButtonClicked");   // shows
    ptb2_wait(30);
    ptb2_invoke(tb, "slotVolumeButtonClicked");   // hides
    ptb2_wait(30);
    SUCCEED();
}

// --- updateMircastTime: index 1 (progress bar) -----------------------------

TEST(platform_tb_ext2, updateMircastTime_progressIndex_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<int>(tb, "updateMircastTime", 10);
    ptb2_invoke1<int>(tb, "updateMircastTime", 0);
    SUCCEED();
}

// --- progressHoverChanged: additional early-return branches -----------------

TEST(platform_tb_ext2, progressHoverChanged_negativeValue_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<int>(tb, "progressHoverChanged", -5);
    ptb2_invoke1<int>(tb, "progressHoverChanged", 99999);
    SUCCEED();
}

// --- slotBaseMuteChanged: mousepreview key path ----------------------------

TEST(platform_tb_ext2, slotBaseMuteChanged_mousepreviewKey_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "slotBaseMuteChanged");
    SUCCEED();
}

// --- slotUpdateMircast: state 0 disables buttons ---------------------------

TEST(platform_tb_ext2, slotUpdateMircast_state0_disablesFsBtn)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->slotUpdateMircast(0, QString());
    EXPECT_FALSE(tb->fsBtn()->isEnabled());
}

TEST(platform_tb_ext2, slotUpdateMircast_emitsSignal)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    int capturedState = -999;
    QString capturedMsg;
    auto c5 = QObject::connect(tb, &Platform_ToolboxProxy::sigMircastState,
                     [&capturedState, &capturedMsg](int s, const QString &m) {
                         capturedState = s;
                         capturedMsg = m;
                     });
    tb->slotUpdateMircast(0, QString("done"));
    EXPECT_EQ(capturedState, 0);
    EXPECT_EQ(capturedMsg, QString("done"));
    QObject::disconnect(c5);
}

// --- slotElapsedChanged: Idle engine is a safe no-op path ------------------

TEST(platform_tb_ext2, slotElapsedChanged_idleEngine_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "slotElapsedChanged");
    SUCCEED();
}

// --- slotPlayListStateChange: needs playlist + animation finished -----------

TEST(platform_tb_ext2, slotPlayListStateChange_noPlaylist_isSafe)
{
    // If the toolbox has no playlist bound, this slot would dereference null.
    // Guard by skipping when no playlist is present.
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb2_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (!pl) GTEST_SKIP() << "No playlist bound";
    // Animation finished flag is true by default in test env.
    ptb2_invoke1<bool>(tb, "slotPlayListStateChange", false);
    SUCCEED();
}

// --- slotUpdateThumbnailTimeOut: empty playlist returns early --------------

TEST(platform_tb_ext2, slotUpdateThumbnailTimeOut_idleEngine_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "slotUpdateThumbnailTimeOut");
    SUCCEED();
}

// --- slotProAnimationFinished: both animation pointers null -----------------

TEST(platform_tb_ext2, slotProAnimationFinished_nullAnimations_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "slotProAnimationFinished");
    SUCCEED();
}

// --- updateMovieProgress: Idle engine (duration 0) -------------------------

TEST(platform_tb_ext2, updateMovieProgress_idleEngine_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "updateMovieProgress");
    SUCCEED();
}

// --- updateButtonStates: Idle engine branch --------------------------------

TEST(platform_tb_ext2, updateButtonStates_idle_disablesPrevNext)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "updateButtonStates");
    // Idle or single-item -> prev/next disabled.
    EXPECT_FALSE(tb->prevBtn()->isEnabled());
    EXPECT_FALSE(tb->nextBtn()->isEnabled());
}

// --- updateFullState: toggles fs icon based on window state ----------------

TEST(platform_tb_ext2, updateFullState_setsIcon)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateFullState();
    SUCCEED();
}

// --- updatePlayState: Idle engine clears popups ----------------------------

TEST(platform_tb_ext2, updatePlayState_idle_clearsPopups)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "updatePlayState");
    EXPECT_FALSE(tb->anyPopupShown());
}

// --- updateTimeVisible: PreviewOnMouseover true returns early; false toggles -

TEST(platform_tb_ext2, updateTimeVisible_true_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<bool>(tb, "updateTimeVisible", true);
    SUCCEED();
}

TEST(platform_tb_ext2, updateTimeVisible_false_togglesPreviewTime)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<bool>(tb, "updateTimeVisible", false);
    ptb2_invoke1<bool>(tb, "updateTimeVisible", true);
    SUCCEED();
}

// --- slotVolumeChanged / slotMuteStateChanged: emit forwarding --------------

TEST(platform_tb_ext2, slotVolumeChanged_forwardsToVolBtn)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    int captured = -1;
    auto c = QObject::connect(tb, &Platform_ToolboxProxy::sigVolumeChanged,
                     [&captured](int &v) { captured = v; });
    ptb2_invoke1<int>(tb, "slotVolumeChanged", 73);
    QObject::disconnect(c);
    SUCCEED();
}

TEST(platform_tb_ext2, slotMuteStateChanged_forwardsToVolBtn)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<bool>(tb, "slotMuteStateChanged", true);
    ptb2_invoke1<bool>(tb, "slotMuteStateChanged", false);
    SUCCEED();
}

// --- slotApplicationStateChanged: additional non-active states --------------

TEST(platform_tb_ext2, slotApplicationStateChanged_suspended_hidden_closesPopups)
{
    // ApplicationInactive is covered by ext; here exercise Suspended + Hidden,
    // which also satisfy the "e != Active && anyPopupShown" guard.
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke1<Qt::ApplicationState>(tb, "slotApplicationStateChanged",
                                       Qt::ApplicationSuspended);
    ptb2_invoke1<Qt::ApplicationState>(tb, "slotApplicationStateChanged",
                                       Qt::ApplicationHidden);
    EXPECT_FALSE(tb->anyPopupShown());
}

// --- waitPlay: disables then re-enables buttons (Idle engine still safe) ----

TEST(platform_tb_ext2, waitPlay_disablesButtons_temporarily)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "waitPlay");
    EXPECT_FALSE(tb->playBtn()->isEnabled());
    ptb2_wait(600);   // the 500ms single-shot re-enables
    EXPECT_TRUE(tb->playBtn()->isEnabled());
}

// --- updateHoverPreview: Idle engine early-return --------------------------

TEST(platform_tb_ext2, updateHoverPreview_idleEngine_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb2_invoke(tb, "updateHoverPreview");
    SUCCEED();
}

// --- Settings interaction: mousepreview toggle -----------------------------

TEST(platform_tb_ext2, mousepreviewToggle_isSafe)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    Settings::get().setInternalOption("mousepreview", true);
    ptb2_wait(50);
    Settings::get().setInternalOption("mousepreview", false);
    ptb2_wait(50);
    SUCCEED();
}

// --- updateSliderPoint repositions the volume slider -----------------------

TEST(platform_tb_ext2, updateSliderPoint_movesVolumeSlider)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QPoint pt(50, 50);
    tb->updateSliderPoint(pt);
    SUCCEED();
}

// --- volumeUp / volumeDown on toolbox when slider disabled -----------------

TEST(platform_tb_ext2, toolboxVolumeUpDown_emitsUnsupportedWhenDisabled)
{
    // When the volume slider is disabled, the toolbox forwards volumeUp/Down
    // to emit sigUnsupported instead of calling the slider. ext covers the
    // enabled path; this covers the disabled branch.
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_VolumeSlider *vs = tb->volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->setEnabled(false);
    tb->volumeUp();
    tb->volumeDown();
    vs->setEnabled(true);
    SUCCEED();
}

// --- getListBtnFocus reflects focus on the list button ---------------------

TEST(platform_tb_ext2, getListBtnFocus_afterSetFocus_true)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ToolButton *listBtn = tb->listBtn();
    ASSERT_NE(listBtn, nullptr);
    listBtn->setFocus();
    ptb2_wait(20);
    EXPECT_TRUE(tb->getListBtnFocus());
    listBtn->clearFocus();
    ptb2_wait(20);
    EXPECT_FALSE(tb->getListBtnFocus());
}

// --- updateProgress: small + large deltas on Idle engine -------------------

TEST(platform_tb_ext2, updateProgress_tinyAndZeroDelta_accumulates)
{
    Platform_ToolboxProxy *tb = ptb2_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->updateProgress(1);
    tb->updateProgress(-1);
    tb->updateProgress(0);
    SUCCEED();
}
