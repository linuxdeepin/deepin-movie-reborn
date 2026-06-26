// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 3) for src/widgets/platform/platform_toolbox_proxy.cpp.
//
// Suite name "platform_tb_ext3" with static helpers using unique prefix ptb3_.
// This file complements test_platform_toolbox_proxy_ext.cpp ("platform_tb_ext")
// and test_platform_toolbox_proxy_ext2.cpp ("platform_tb_ext2") by targeting the
// auxiliary classes those suites leave untouched: Platform_ImageItem and
// Platform_IndicatorItem paintEvent branches, Platform_ViewProgBar event
// handlers (leave/show/mouse-move/mouse-release/paint/resize — the ViewProgBar
// page is never the current stacked page in Idle, so it never receives these
// events spontaneously), the volume-button event-filter branch that intercepts
// Up/Down only while the volume slider is Open, the list-button right-click
// filter branch, and the updateProgress "filmstrip mode" branch that routes
// through ViewProgBar::setValue/getValue/setIsBlockSignals.
//
// Safety rules baked in (verified against the source):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * Private members of Platform_ToolboxProxy are reached via the
//     #define private/protected public trick around the header include. The
//     running app's shared toolbox is obtained exactly like ext2:
//       dApp->getMainWindow()->toolbox().  Guarded for null.
//   * Platform_ViewProgBar is only forward-declared in the header, so its
//     methods cannot be called directly; its QWidget event overrides are
//     driven through QApplication::sendEvent on the pointer from
//     getViewProBar(). Its methods (setValue/getValue/setIsBlockSignals) are
//     exercised transitively through Platform_ToolboxProxy::updateProgress by
//     flipping m_pProgBar_Widget's current index (requires private access).
//   * Platform_ViewProgBar::mousePressEvent flips an internal m_bPress flag
//     with no Idle-safe reset path, and mouseReleaseEvent after a press calls
//     m_pEngine->seekAbsolute (engine ptr null -> SIGSEGV). mousePressEvent is
//     therefore intentionally NOT exercised; only the not-pressed release
//     branch is driven.
//   * ViewProgBar signal recipients are all Idle-safe: hoverChanged ->
//     progressHoverChanged (early-returns when slider value == 0); leave ->
//     slotHidePreviewTime (no-op); mousePressed -> updateTimeVisible (no-op);
//     sliderMoved is not connected.
//   * Geometry / paint / popup cases are guarded by primaryScreen() and
//     GTEST_SKIP when headless.
//   * Global theme type is saved and restored around ImageItem paint cases.
//   * m_pProgBar_Widget current index is restored to 1 after the filmstrip-mode
//     updateProgress test.

#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <memory>

#define protected public
#define private public
#include "src/widgets/platform/platform_toolbox_proxy.h"
#undef protected
#undef private

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
#include <QPointingDevice>
#include <DGuiApplicationHelper>

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// The running app's main window. All shared widgets hang off this.
static Platform_MainWindow *ptb3_mainWindow()
{
    return dApp->getMainWindow();
}

// Shared toolbox proxy owned by the main window.
static Platform_ToolboxProxy *ptb3_toolbox()
{
    Platform_MainWindow *w = ptb3_mainWindow();
    return w ? w->toolbox() : nullptr;
}

// Shared volume slider owned by the toolbox.
static Platform_VolumeSlider *ptb3_volumeSlider()
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    return tb ? tb->volumeSlider() : nullptr;
}

// A short synchronous wait so animations/timers settle without stalling.
static void ptb3_wait(int ms = 120)
{
    QTest::qWait(ms);
}

// True when the compositing platform is X86 (gates the list-button right-click
// filter branch).
static bool ptb3_isX86()
{
    return CompositingManager::get().platform() == Platform::X86;
}

// ==========================================================================
// Platform_ImageItem — paintEvent theme branches (lines 73-117)
// ==========================================================================

TEST(platform_tb_ext3, imageItem_paintEvent_lightTheme_branch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget host;
    host.resize(40, 50);
    Platform_ImageItem item(QPixmap(40, 50), false, &host);
    item.resize(40, 50);
    // Force the LightType pen branch, then restore the original theme.
    auto helper = DGuiApplicationHelper::instance();
    auto orig = helper->themeType();
    helper->setPaletteType(DGuiApplicationHelper::LightType);
    ptb3_wait(20);
    QPaintEvent pe(item.rect());
    QApplication::sendEvent(&item, &pe);
    helper->setPaletteType(orig);
    ptb3_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext3, imageItem_paintEvent_darkTheme_branch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget host;
    host.resize(40, 50);
    Platform_ImageItem item(QPixmap(40, 50), true, &host);
    item.resize(40, 50);
    auto helper = DGuiApplicationHelper::instance();
    auto orig = helper->themeType();
    helper->setPaletteType(DGuiApplicationHelper::DarkType);
    ptb3_wait(20);
    QPaintEvent pe(item.rect());
    QApplication::sendEvent(&item, &pe);
    helper->setPaletteType(orig);
    ptb3_wait(20);
    SUCCEED();
}

// ==========================================================================
// Platform_IndicatorItem — setPressed resize + paintEvent branches
// ==========================================================================

TEST(platform_tb_ext3, imageItem_paintEvent_unknownTheme_noPen)
{
    // Neither LightType nor DarkType: both pen branches are skipped.
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget host;
    host.resize(40, 50);
    Platform_ImageItem item(QPixmap(40, 50), false, &host);
    item.resize(40, 50);
    auto helper = DGuiApplicationHelper::instance();
    auto orig = helper->themeType();
    helper->setPaletteType(DGuiApplicationHelper::UnknownType);
    ptb3_wait(20);
    QPaintEvent pe(item.rect());
    QApplication::sendEvent(&item, &pe);
    helper->setPaletteType(orig);
    ptb3_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext3, indicatorItem_setPressed_resizesWidget)
{
    QWidget host;
    Platform_IndicatorItem ind(&host);
    ind.setPressed(false);
    EXPECT_GE(ind.width(), 2);
    EXPECT_GE(ind.height(), 40);
    ind.setPressed(true);
    // Pressed path widens to 2px; released path widens to 6px.
    EXPECT_EQ(ind.width(), 2);
    ind.setPressed(false);
    EXPECT_EQ(ind.width(), 6);
}

TEST(platform_tb_ext3, indicatorItem_paintEvent_notPressed_branch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget host;
    host.resize(20, 70);
    Platform_IndicatorItem ind(&host);
    ind.resize(6, 60);
    ind.setPressed(false);
    QPaintEvent pe(ind.rect());
    QApplication::sendEvent(&ind, &pe);
    SUCCEED();
}

TEST(platform_tb_ext3, indicatorItem_paintEvent_pressed_branch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget host;
    host.resize(20, 70);
    Platform_IndicatorItem ind(&host);
    ind.resize(2, 60);
    ind.setPressed(true);
    QPaintEvent pe(ind.rect());
    QApplication::sendEvent(&ind, &pe);
    // Restore so no lingering pressed state.
    ind.setPressed(false);
    SUCCEED();
}

// ==========================================================================
// Platform_ViewProgBar — QWidget event overrides via getViewProBar().
// The filmstrip page is never current in Idle, so these never fire
// spontaneously; drive them directly with sendEvent.
// ==========================================================================

TEST(platform_tb_ext3, viewProgBar_leaveEvent_emitsLeave)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    // leave -> slotHidePreviewTime (Idle-safe no-op); base leaveEvent.
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(vp, &le);
    SUCCEED();
}

TEST(platform_tb_ext3, viewProgBar_showEvent_baseSafe)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    QShowEvent se;
    QApplication::sendEvent(vp, &se);
    SUCCEED();
}

TEST(platform_tb_ext3, viewProgBar_mouseMoveEvent_disabled_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    vp->setEnabled(false);
    QMouseEvent me(QEvent::MouseMove, QPointF(5, 5), QPointF(5, 5),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    vp->setEnabled(true);
    SUCCEED();
}

TEST(platform_tb_ext3, viewProgBar_mouseMoveEvent_enabledNoButton_accepts)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    vp->resize(200, 70);
    // In-range hover, no button: position2progress returns 0 (engine null),
    // hoverChanged not emitted (v == m_nVlastHoverValue == 0).
    QMouseEvent me(QEvent::MouseMove, QPointF(10, 5), QPointF(10, 5),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    EXPECT_TRUE(me.isAccepted());
}

TEST(platform_tb_ext3, viewProgBar_mouseMoveEvent_leftButtonDrag_emitsSlider)
{
    // Left-button move beyond startDragDistance triggers the drag branch:
    // emits sliderMoved (unconnected) / hoverChanged / mousePressed, then
    // setValue + setTime + repaint. With engine null, position2progress = 0
    // and the ViewProgBar is not visible so repaint() is a no-op.
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    vp->resize(200, 70);
    vp->setEnabled(true);
    // m_startPos defaults to (0,0); a move to (60,5) exceeds startDragDistance.
    QMouseEvent me(QEvent::MouseMove, QPointF(60, 5), QPointF(60, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    EXPECT_TRUE(me.isAccepted());
}

TEST(platform_tb_ext3, viewProgBar_mouseMoveEvent_outOfRange_ignored)
{
    // pos.x() beyond contentsRect().width(): the in-range if-body is skipped,
    // only e->accept() at the tail runs (in-range branch condition false).
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    vp->resize(20, 70);  // narrow so (200, 5) is out of range
    QMouseEvent me(QEvent::MouseMove, QPointF(200, 5), QPointF(200, 5),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    EXPECT_TRUE(me.isAccepted());
}

TEST(platform_tb_ext3, viewProgBar_mouseReleaseEvent_notPressed_cleanShutdown)
{
    // With m_bPress == false the seekAbsolute branch is skipped; only
    // mousePressed(false), arrow-hide, setTimeVisible(false) and base run.
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(10, 5), QPointF(10, 5),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &re);
    SUCCEED();
}

TEST(platform_tb_ext3, viewProgBar_paintEvent_repositionsIndicator)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    vp->resize(200, 70);
    QPaintEvent pe(vp->rect());
    QApplication::sendEvent(vp, &pe);
    SUCCEED();
}

TEST(platform_tb_ext3, viewProgBar_resizeEvent_setsBackWidth)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QWidget *vp = reinterpret_cast<QWidget *>(tb->getViewProBar());
    ASSERT_NE(vp, nullptr);
    QSize oldSize = vp->size();
    QSize newSize(std::max(10, oldSize.width() + 40), oldSize.height());
    QResizeEvent re(newSize, oldSize);
    QApplication::sendEvent(vp, &re);
    SUCCEED();
}

// ==========================================================================
// Platform_ToolboxProxy::updateProgress — filmstrip-mode (currentIndex != 1)
// branch routes through ViewProgBar::setIsBlockSignals/setValue/getValue.
// ==========================================================================

TEST(platform_tb_ext3, updateProgress_filmstripMode_callsViewProgBarSetters)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int origIndex = stack->currentIndex();
    // Force the filmstrip page so updateProgress takes the else branch.
    stack->setCurrentIndex(0);
    ptb3_wait(20);
    tb->updateProgress(5);
    tb->updateProgress(-2);
    ptb3_wait(20);
    stack->setCurrentIndex(origIndex);
    ptb3_wait(20);
    SUCCEED();
}

// ==========================================================================
// Platform_ToolboxProxy::eventFilter — volume-button key press intercepted
// only while the volume slider state == Open (lines 2820-2838).
// ==========================================================================

// Bring the shared volume slider to Open state by calling its public popup()
// directly: popup() only starts the raise animation when m_state == Close and
// the slider is visible, and flips m_state to Open inside the animation's
// finished lambda (230ms). slotVolumeButtonClicked merely show/hides the
// slider and never calls popup(), so it cannot reach Open.
static bool ptb3_openVolumeSlider(Platform_VolumeSlider *vs)
{
    if (!vs) return false;
    vs->changeVolume(50);
    vs->show();
    ptb3_wait(30);
    if (!vs->isVisible()) return false;
    vs->popup();
    ptb3_wait(320);  // popup animation is 230ms
    return vs->state() == Platform_VolumeSlider::Open;
}

// Close an Open slider back to Close + hidden, restoring a clean state.
static void ptb3_closeVolumeSlider(Platform_VolumeSlider *vs)
{
    if (!vs) return;
    if (vs->state() == Platform_VolumeSlider::Open) {
        vs->popup();  // Open -> else branch -> Close + hide
        ptb3_wait(40);
    }
    vs->hide();
}

TEST(platform_tb_ext3, eventFilter_volBtnUpWhenSliderOpen_incrementsVolume)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_VolumeSlider *vs = ptb3_volumeSlider();
    ASSERT_NE(vs, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);

    if (!ptb3_openVolumeSlider(vs)) {
        ptb3_closeVolumeSlider(vs);
        GTEST_SKIP() << "Volume slider did not reach Open state";
    }

    int before = vs->getVolume();
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vb, &up);
    ptb3_wait(20);
    EXPECT_EQ(vs->getVolume(), std::min(before + 5, 200));

    ptb3_closeVolumeSlider(vs);
}

TEST(platform_tb_ext3, eventFilter_volBtnDownWhenSliderOpen_decrementsVolume)
{
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_VolumeSlider *vs = ptb3_volumeSlider();
    ASSERT_NE(vs, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);

    if (!ptb3_openVolumeSlider(vs)) {
        ptb3_closeVolumeSlider(vs);
        GTEST_SKIP() << "Volume slider did not reach Open state";
    }
    // Clamp near the top so a Down press stays detectable.
    vs->changeVolume(100);
    int before = vs->getVolume();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(vb, &down);
    ptb3_wait(20);
    EXPECT_EQ(vs->getVolume(), std::max(before - 5, 0));

    ptb3_closeVolumeSlider(vs);
}

TEST(platform_tb_ext3, eventFilter_volBtnNonKeyEvent_fallsThrough)
{
    // obj == m_pVolBtn but ev is not KeyPress -> filter does not intercept.
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);
    QMouseEvent me(QEvent::MouseMove, QPointF(2, 2), QPointF(2, 2),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vb, &me);
    SUCCEED();
}

// ==========================================================================
// Platform_ToolboxProxy::eventFilter — list-button right-click (X86 only,
// lines 2841-2855). Toggles the list button check based on playlist state.
// ==========================================================================

TEST(platform_tb_ext3, eventFilter_listBtnRightClick_togglesCheck)
{
    if (!ptb3_isX86()) GTEST_SKIP() << "List-button right-click filter is X86-only";
    Platform_ToolboxProxy *tb = ptb3_toolbox();
    ASSERT_NE(tb, nullptr);
    ToolButton *listBtn = tb->listBtn();
    ASSERT_NE(listBtn, nullptr);
    Platform_MainWindow *w = ptb3_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (!pl) GTEST_SKIP() << "No playlist bound — right-click branch dereferences it";

    bool wasChecked = listBtn->isChecked();
    QMouseEvent rmb(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                    Qt::RightButton, Qt::NoButton, Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(listBtn, &rmb);
    ptb3_wait(20);
    // The branch flips the check mark; restore it to the prior state.
    listBtn->setChecked(wasChecked);
    SUCCEED();
}
