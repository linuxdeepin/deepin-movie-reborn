// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for src/widgets/platform/platform_volumeslider.cpp
// (class dmr::Platform_VolumeSlider).
// Suite: platform_volume_slider_ext ; helper prefix: pvse_.
//
// Scope note: tests/deepin-movie-platform/widgets/test_platform_widgets_ext.cpp
// already covers the shared toolbox slider (suite platform_widgets_ext,
// cases volume_*). This file covers the remaining gaps on ISOLATED instances:
//   * ctor state + platform window-flag branch        -> pvse_ctor_*
//   * updatePoint + move-to-computed-point            -> pvse_updatePoint_movesToComputedPoint
//   * keyPressEvent +5/-5 (replaces the two crasher
//     cases excluded from the shared-slider run)      -> pvse_keyPressEvent_*
//   * popup animation state machine / hidden branch /
//     while-animating early return                    -> pvse_popup_*
//   * showEvent geometry                              -> pvse_showEvent_geometry
//   * delayedHide cursor-rect branch (100ms path) and
//     m_mouseIn re-arm guard                          -> pvse_delayedHide_*
//   * initVolume settings application                 -> pvse_initVolume_appliesSettings
//   * changeVolume(0) keeps mute / auto-unmute        -> pvse_volumeChanged_*
//   * calculationStep opposite-direction reset        -> pvse_calculationStep_opposite_resets
//   * volumeUp/Down wheel small-step no-op            -> pvse_volumeUp/Down_wheel_smallStep_noChange
//   * eventFilter exact counts (DSlider internal wheel
//     disabled) + modifier/button guards              -> pvse_eventFilter_*
//   * refreshIcon band label text                     -> pvse_refreshIcon_bands_labelText
//   * changeMuteState flip + settings persistence     -> pvse_changeMuteState_flip_persists
//   * readSinkInputPath / setMute (DBus stubbed)      -> pvse_readSinkInputPath_* / pvse_setMute_*
//   * muteButtnClicked toggle                         -> pvse_muteButtnClicked_toggles
//   * paintEvent smoke                                -> pvse_paintEvent_doesNotCrash
//
// Safety rules:
//   * Only Google Test (TEST(...)). The shared harness main (test_qtestmain.cpp)
//     supplies main(); never define main() here.
//   * All sliders are constructed ISOLATED per case (fresh Platform_VolumeSlider,
//     parentless) — never the shared toolbox slider, which is wired into the
//     app-wide volume chain whose environment-dependent round-trips caused the
//     excluded crasher cases.
//   * DBus is stubbed via ApplicationAdaptor::readDBusProperty so no
//     org.deepin.dde.Audio1 traffic happens.
//   * Geometry/visibility cases are guarded by primaryScreen() and GTEST_SKIP
//     when headless.

// STL / Qt / DTK headers BEFORE the access-unlocking #defines.
#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QEnterEvent>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QVariant>

#include <DSlider>
#include <DToolButton>
#include <DLabel>
#include <DArrowRectangle>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

// NOTE: the access-unlocking defines must wrap application.h too — on the
// platform side it pulls platform_mainwindow.h which includes
// platform_volumeslider.h; leaving application.h outside the defines would
// burn the include guard with normal access and keep members private.
#define protected public
#define private public
#include "application.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/widgets/platform/platform_volumeslider.h"
#include "src/libdmr/player_engine.h"
#include "src/common/volumemonitoring.h"
#undef protected
#undef private

#include "stub/stub.h"
#include "src/common/dmr_settings.h"
#include "src/common/dbus_adpator.h"
#include "src/libdmr/compositing_manager.h"

using namespace dmr;
DWIDGET_USE_NAMESPACE

// --- Helpers ---------------------------------------------------------------

// Isolated Platform_VolumeSlider: parentless; the volume label hangs off the
// harness main window (ctor requirement).
static Platform_VolumeSlider *pvse_makeSlider()
{
    Platform_MainWindow *mw = dApp->getMainWindow();
    EXPECT_NE(mw, nullptr);
    return new Platform_VolumeSlider(mw, nullptr);
}

static DSlider *pvse_slider(Platform_VolumeSlider *vs)
{
    return vs->findChild<DSlider *>(VOLUME_SLIDER);
}

// DBus stub state for ApplicationAdaptor::readDBusProperty.
static int pvse_dbusCallCount = 0;
static bool pvse_dbusValid = false;
static QList<QDBusObjectPath> pvse_sinkPaths;

static QVariant pvse_readDBusProperty_stub(const QString &sService, const QString &sPath,
                                           const QString &sInterface, const char *pPropert)
{
    Q_UNUSED(sService); Q_UNUSED(sInterface); Q_UNUSED(pPropert);
    pvse_dbusCallCount++;
    if (sPath == "/org/deepin/dde/Audio1") {
        if (!pvse_dbusValid) return QVariant();
        return QVariant::fromValue(pvse_sinkPaths);
    }
    if (sPath.endsWith("/2")) return QVariant(QString("Deepin Movie"));
    return QVariant(QString("Firefox"));
}

typedef QVariant (*pvse_fptr)(const QString &, const QString &, const QString &, const char *);
static void pvse_installDBusStub()
{
    qRegisterMetaType<QList<QDBusObjectPath>>("QList<QDBusObjectPath>");
    static Stub stub;
    stub.set((pvse_fptr)&ApplicationAdaptor::readDBusProperty, pvse_readDBusProperty_stub);
    pvse_dbusCallCount = 0;
    pvse_dbusValid = false;
    pvse_sinkPaths.clear();
}

static void pvse_sendWheel(DSlider *slider, int deltaY, Qt::KeyboardModifiers mods = Qt::NoModifier,
                           Qt::MouseButtons btns = Qt::NoButton)
{
    QPoint pos = slider->rect().center();
    QWheelEvent we(pos, slider->mapToGlobal(pos), QPoint(0, deltaY), QPoint(0, deltaY),
                   btns, mods, Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &we);
}

static bool pvse_hasScreen()
{
    return QGuiApplication::primaryScreen() != nullptr;
}

static void pvse_restoreVolumeSettings()
{
    Settings::get().setInternalOption("global_volume", 100);
    Settings::get().setInternalOption("mute", false);
}

// ==========================================================================
// ctor / initial state / window flags
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_ctor_initial_state)
{
    // Arrange + Act
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);

    // Assert: documented initial values.
    EXPECT_EQ(vs->state(), Platform_VolumeSlider::Close);
    EXPECT_FALSE(vs->getsliderstate());
    EXPECT_EQ(vs->getVolume(), 100);
    EXPECT_FALSE(vs->m_bIsMute);
    EXPECT_FALSE(vs->m_bIsWheel);
    EXPECT_EQ(vs->m_iStep, 0);
    EXPECT_FALSE(vs->isVisible());
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->slider()->minimum(), 0);
    EXPECT_EQ(slider->slider()->maximum(), 100);
    DToolButton *btn = vs->findChild<DToolButton *>(MUTE_BTN);
    ASSERT_NE(btn, nullptr);
    EXPECT_NE(vs->m_pLabShowVolume, nullptr);
    EXPECT_EQ(vs->m_pLabShowVolume->text(), QString("0%"));

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_ctor_windowFlags_platformBranch)
{
    // Both branches keep the frameless hint; X86 uses Qt::Tool, others
    // Qt::ToolTip. Assert the invariant part and that exactly one branch ran.
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);

    Qt::WindowFlags fl = vs->windowFlags();
    EXPECT_TRUE(fl & Qt::FramelessWindowHint);
    bool toolOrTip = (fl & Qt::WindowType_Mask) == Qt::Tool
                  || (fl & Qt::WindowType_Mask) == Qt::ToolTip;
    EXPECT_TRUE(toolOrTip);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_updatePoint_movesToComputedPoint)
{
    // Arrange
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    Platform_MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);

    // Expected position mirrors the production arithmetic exactly.
    QRect viewRect = mw->rect().marginsRemoved(QMargins(1, 1, 1, 1));
    QPoint offset(viewRect.width() - (TOOLBOX_BUTTON_WIDTH * 3 + 40
                                      + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2),
                  viewRect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT);
    QPoint expected = QPoint(10, 10) + offset;

    // Act
    vs->updatePoint(QPoint(10, 10));

    // Assert: both the stored anchor and the widget position moved.
    EXPECT_EQ(vs->m_point, expected);
    EXPECT_EQ(vs->pos(), expected);

    delete vs;
}

// ==========================================================================
// keyPressEvent: +5 / -5 / other (isolated replacements for the two excluded
// shared-slider crasher cases)
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_keyPressEvent_up_increments_by5)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);

    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vs, &up);

    EXPECT_EQ(vs->getVolume(), 45);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_keyPressEvent_down_decrements_by5)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);

    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(vs, &down);

    EXPECT_EQ(vs->getVolume(), 35);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_keyPressEvent_otherKey_ignored)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);

    QKeyEvent other(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(vs, &other);

    EXPECT_EQ(vs->getVolume(), 40);

    delete vs;
}

// ==========================================================================
// popup / showEvent
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_popup_hidden_closeBranch)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);

    vs->popup();

    EXPECT_EQ(vs->state(), Platform_VolumeSlider::Close);
    EXPECT_FALSE(vs->isVisible());
    EXPECT_EQ(vs->pVolAnimation, nullptr);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_popup_showAnimate_openClose)
{
    if (!pvse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act 1: open animation; getsliderstate() reports pending while running.
    vs->popup();
    EXPECT_TRUE(vs->getsliderstate());
    QTest::qWait(400);
    EXPECT_EQ(vs->state(), Platform_VolumeSlider::Open);
    EXPECT_FALSE(vs->getsliderstate());
    EXPECT_TRUE(vs->isVisible());

    // Act 2: second popup closes (state Open -> Close + hide).
    vs->popup();
    EXPECT_EQ(vs->state(), Platform_VolumeSlider::Close);
    QTest::qWait(30);
    EXPECT_FALSE(vs->isVisible());

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_popup_whileAnimating_flagsHideWhenFinished)
{
    if (!pvse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    vs->popup();
    ASSERT_TRUE(vs->pVolAnimation != nullptr);
    ASSERT_FALSE(vs->m_bHideWhenFinished);

    // Act: popup() during the animation takes the early return.
    vs->popup();

    EXPECT_TRUE(vs->m_bHideWhenFinished);
    QTest::qWait(600);
    EXPECT_EQ(vs->pVolAnimation, nullptr);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_showEvent_geometry)
{
    if (!pvse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);

    vs->show();
    QTest::qWait(30);

    EXPECT_EQ(vs->geometry().width(), VOLSLIDER_WIDTH);
    EXPECT_EQ(vs->geometry().height(), VOLSLIDER_HEIGHT);

    delete vs;
}

// ==========================================================================
// delayedHide: cursor-rect branch + m_mouseIn re-arm guard
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_delayedHide_cursorOutside_100msHides)
{
    if (!pvse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    // Arrange: widget at default m_point (0,0); the adRect sits below the
    // slider around (6,205)..(68,223). On offscreen hosts QCursor::pos() is
    // (0,0), which is outside that rect -> the 100ms branch.
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    QRect adRect = QRect(vs->m_point + QPoint(6, VOLSLIDER_HEIGHT),
                         vs->m_point + QPoint(6 + VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT + 18));
    ASSERT_FALSE(adRect.contains(QCursor::pos()))
        << "precondition: cursor must be outside the volume-button rect";
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act
    vs->delayedHide();
    QTest::qWait(300);

    // Assert
    EXPECT_FALSE(vs->isVisible());

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_delayedHide_rearmPreventsHide)
{
    if (!pvse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    // Arrange
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act: delayedHide schedules a 100ms hide (cursor outside); re-enter
    // before it fires sets m_mouseIn=true and disarms it.
    vs->delayedHide();
    QTest::qWait(30);
    QPoint pos = vs->rect().center();
    QEnterEvent ee(pos, vs->mapToGlobal(pos), pos);
    QApplication::sendEvent(vs, &ee);
    EXPECT_TRUE(vs->m_mouseIn);
    QTest::qWait(250);

    // Assert
    EXPECT_TRUE(vs->isVisible());

    delete vs;
}

// ==========================================================================
// initVolume / changeMuteState / volumeChanged interactions
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_initVolume_appliesSettings)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    Settings::get().setInternalOption("global_volume", 77);
    Settings::get().setInternalOption("mute", true);

    vs->initVolume();
    QTest::qWait(150);

    EXPECT_EQ(vs->getVolume(), 77);
    EXPECT_TRUE(vs->m_bIsMute);
    pvse_restoreVolumeSettings();

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_changeMuteState_flip_persists)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    vs->changeVolume(50);
    int captured = -1;
    auto c = QObject::connect(vs, &Platform_VolumeSlider::sigMuteStateChanged,
                              [&captured](bool b) { captured = b ? 1 : 0; });

    vs->changeMuteState(true);
    EXPECT_EQ(captured, 1);
    EXPECT_TRUE(vs->m_bIsMute);
    EXPECT_EQ(Settings::get().internalOption("mute").toBool(), true);

    vs->changeMuteState(false);
    EXPECT_EQ(captured, 0);
    EXPECT_FALSE(vs->m_bIsMute);
    EXPECT_EQ(Settings::get().internalOption("mute").toBool(), false);
    QObject::disconnect(c);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_volumeChanged_autoUnmute)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    vs->changeVolume(50);
    vs->changeMuteState(true);
    ASSERT_TRUE(vs->m_bIsMute);
    QList<int> volumes;
    QList<bool> mutes;
    auto c1 = QObject::connect(vs, &Platform_VolumeSlider::sigVolumeChanged,
                               [&volumes](int v) { volumes << v; });
    auto c2 = QObject::connect(vs, &Platform_VolumeSlider::sigMuteStateChanged,
                               [&mutes](bool b) { mutes << b; });

    vs->changeVolume(80);

    EXPECT_EQ(mutes, QList<bool>{false});
    EXPECT_EQ(volumes, QList<int>{80});
    EXPECT_FALSE(vs->m_bIsMute);
    QObject::disconnect(c1);
    QObject::disconnect(c2);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_volumeChanged_zero_keepsMute)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    vs->changeVolume(50);
    vs->changeMuteState(true);
    ASSERT_TRUE(vs->m_bIsMute);
    QList<bool> mutes;
    auto c2 = QObject::connect(vs, &Platform_VolumeSlider::sigMuteStateChanged,
                               [&mutes](bool b) { mutes << b; });

    vs->changeVolume(0);

    EXPECT_EQ(mutes, QList<bool>{});
    EXPECT_TRUE(vs->m_bIsMute);
    EXPECT_EQ(vs->getVolume(), 0);
    QObject::disconnect(c2);

    delete vs;
}

// ==========================================================================
// calculationStep / wheel small-step paths (gaps in the shared-slider suite)
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_calculationStep_opposite_resets)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->m_iStep = 240;

    vs->calculationStep(-120);

    EXPECT_EQ(vs->m_iStep, -120);
    EXPECT_TRUE(vs->m_bIsWheel);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_volumeUp_wheel_smallStep_noChange)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);
    vs->calculationStep(100);

    vs->volumeUp();

    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 100);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_volumeDown_wheel_smallStep_noChange)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);
    vs->calculationStep(-100);

    vs->volumeDown();

    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, -100);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

// ==========================================================================
// eventFilter: exact wheel counts + guards (DSlider internal wheel disabled)
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_eventFilter_wheelUp_exactPlus10)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    pvse_sendWheel(slider, 120);

    EXPECT_EQ(vs->getVolume(), 50);
    EXPECT_EQ(vs->m_iStep, 0);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_eventFilter_wheelDown_exactMinus10)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    pvse_sendWheel(slider, -120);

    EXPECT_EQ(vs->getVolume(), 30);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_eventFilter_wheelModifiers_ignored)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    pvse_sendWheel(slider, 120, Qt::ControlModifier);

    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_eventFilter_wheelButtons_ignored)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    pvse_sendWheel(slider, 120, Qt::NoModifier, Qt::LeftButton);

    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_eventFilter_nonWheel_delegates)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = pvse_slider(vs);
    ASSERT_NE(slider, nullptr);
    vs->changeVolume(40);

    QPoint pos = slider->rect().center();
    QMouseEvent me(QEvent::MouseMove, pos, slider->mapToGlobal(pos),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &me);

    EXPECT_EQ(vs->getVolume(), 40);

    delete vs;
}

// ==========================================================================
// refreshIcon / muteButtnClicked
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_refreshIcon_bands_labelText)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);

    struct Band { int vol; const char *text; };
    const Band bands[] = {
        {80, "80%"}, {66, "66%"}, {65, "65%"}, {33, "33%"},
        {32, "32%"}, {10, "10%"}, {0, "0%"},
    };
    for (const Band &b : bands) {
        vs->changeVolume(b.vol);
        // Icon resources resolve through the system theme, unavailable on
        // offscreen hosts; the label text is the deterministic observable.
        EXPECT_EQ(vs->m_pLabShowVolume->text(), QString(b.text)) << "volume " << b.vol;
    }

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_muteButtnClicked_toggles)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    vs->changeVolume(50);
    ASSERT_FALSE(vs->m_bIsMute);

    vs->muteButtnClicked();
    EXPECT_TRUE(vs->m_bIsMute);
    vs->muteButtnClicked();
    EXPECT_FALSE(vs->m_bIsMute);

    delete vs;
}

// ==========================================================================
// paintEvent smoke
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_paintEvent_doesNotCrash)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(60);

    QPaintEvent pe(vs->rect());
    QApplication::sendEvent(vs, &pe);

    EXPECT_TRUE(vs != nullptr);
    delete vs;
}

// ==========================================================================
// readSinkInputPath / setMute (DBus fully stubbed)
// ==========================================================================

TEST(platform_volume_slider_ext, pvse_readSinkInputPath_findsMovieSink)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    pvse_dbusValid = true;
    pvse_sinkPaths << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/1")
                   << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/2");

    QString path = vs->readSinkInputPath();

    EXPECT_EQ(path, QString("/org/deepin/dde/Audio1/SinkInput/2"));
    EXPECT_EQ(pvse_dbusCallCount, 3);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_readSinkInputPath_invalidProperty_returnsEmpty)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();

    QString path = vs->readSinkInputPath();

    EXPECT_TRUE(path.isEmpty());
    EXPECT_EQ(pvse_dbusCallCount, 1);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_setMute_guard_zeroVolume)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    vs->changeVolume(0);
    vs->m_bIsMute = true;

    vs->setMute(true);

    EXPECT_EQ(pvse_dbusCallCount, 0);

    delete vs;
}

TEST(platform_volume_slider_ext, pvse_setMute_dbusInvalid_noCrash)
{
    Platform_VolumeSlider *vs = pvse_makeSlider();
    ASSERT_NE(vs, nullptr);
    pvse_installDBusStub();
    pvse_dbusValid = true;
    pvse_sinkPaths << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/2");
    vs->changeVolume(50);
    vs->m_bIsMute = true;

    vs->setMute(true);

    EXPECT_GT(pvse_dbusCallCount, 0);
    EXPECT_TRUE(vs->m_bIsMute);

    delete vs;
}
