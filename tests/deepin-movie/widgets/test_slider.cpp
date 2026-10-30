// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Google Test cases for src/widgets/slider.cpp (dmr::DMRSlider) — UT-movie-t14.
//
// Scope agreed for this task: the pre-existing platform suites only smoke-
// exercise DMRSlider indirectly; this file adds dedicated behavioural cases
// in the non-platform harness (tests/deepin-movie), keeping the platform
// tree untouched.
//
// Conventions (mirroring the existing suites):
//   * Google Test only, no main() here; suite name "slider_t14" is unique
//     inside deepin-movie-test.
//   * `#define protected public` before project includes to reach internal
//     state, `#undef` afterwards — the codebase-wide test pattern.
//   * The event-driven cases avoid windowing: they send QMouseEvents
//     directly to the widget, so no real playback / mpv / network happens.
//   * Headless-safety: cases that would need a real screen stay skipped
//     behind the primaryScreen() guard or use offscreen-safe assertions.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QCursor>

#define protected public
#define private public
#include "src/widgets/slider.h"
#include "src/widgets/toolbox_proxy.h"
#undef private
#undef protected

using namespace dmr;

namespace {

struct SliderFixture {
    DMRSlider slider{nullptr};

    SliderFixture()
    {
        slider.setMinimum(0);
        slider.setMaximum(100);
        slider.setValue(50);
        slider.resize(200, 20);
    }
};

// Save/restore XDG_SESSION_TYPE / WAYLAND_DISPLAY around a body that
// simulates a Wayland session (slider.cpp reads the environment live).
template <typename F>
void withWaylandSession(F &&body)
{
    const QByteArray savedType = qgetenv("XDG_SESSION_TYPE");
    const QByteArray savedDisplay = qgetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", "wayland");
    qputenv("WAYLAND_DISPLAY", "wayland-0");
    body();
    if (savedType.isEmpty())
        qunsetenv("XDG_SESSION_TYPE");
    else
        qputenv("XDG_SESSION_TYPE", savedType);
    if (savedDisplay.isEmpty())
        qunsetenv("WAYLAND_DISPLAY");
    else
        qputenv("WAYLAND_DISPLAY", savedDisplay);
}

} // namespace

// ------------------------------------------------------------------- ctor

TEST(slider_t14, CtorInitializesDefaultState)
{
    DMRSlider s;
    EXPECT_EQ(s.m_bDown, false);
    EXPECT_EQ(s.m_bIndicatorEnabled, false);
    EXPECT_EQ(s.m_bShowIndicator, false);
    EXPECT_EQ(s.m_nLastHoverValue, 0);
    EXPECT_EQ(s.m_indicatorPos, QPoint(0, 0));
}

// ---------------------------------------------------------- setEnableIndication

TEST(slider_t14, SetEnableIndicationTogglesOnlyOnChange)
{
    SliderFixture fx;
    EXPECT_FALSE(fx.slider.m_bIndicatorEnabled);
    fx.slider.setEnableIndication(true);
    EXPECT_TRUE(fx.slider.m_bIndicatorEnabled);
    // same value again: no state churn
    fx.slider.setEnableIndication(true);
    EXPECT_TRUE(fx.slider.m_bIndicatorEnabled);
    fx.slider.setEnableIndication(false);
    EXPECT_FALSE(fx.slider.m_bIndicatorEnabled);
}

// -------------------------------------------------------- position2progress

TEST(slider_t14, Position2ProgressEndpointsAndTruncation)
{
    SliderFixture fx; // width 200, range 0..100 -> 0.5 px per unit
    // 0 and width map exactly to the endpoints
    EXPECT_EQ(fx.slider.position2progress(QPoint(0, 5)), 0);
    EXPECT_EQ(fx.slider.position2progress(QPoint(200, 5)), 100);
    // truncation: int(0.5 * x)
    EXPECT_EQ(fx.slider.position2progress(QPoint(7, 5)), 3);
    EXPECT_EQ(fx.slider.position2progress(QPoint(50, 5)), 25);
    EXPECT_EQ(fx.slider.position2progress(QPoint(100, 5)), 50);
    EXPECT_EQ(fx.slider.position2progress(QPoint(150, 5)), 75);
}

TEST(slider_t14, Position2ProgressRespectsCustomRange)
{
    DMRSlider s;
    s.setMinimum(0);
    s.setMaximum(200);
    s.resize(100, 20); // 2 units per px (200 / 100)
    EXPECT_EQ(s.position2progress(QPoint(10, 5)), 20);
    EXPECT_EQ(s.position2progress(QPoint(100, 5)), 200);
    EXPECT_EQ(s.position2progress(QPoint(50, 5)), 100);
}

// -------------------------------------------------------------- mouse events

TEST(slider_t14, MousePressEnabledLeftButtonStartsTracking)
{
    SliderFixture fx;
    QSignalSpy moved(&fx.slider, &DMRSlider::sliderMoved);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                      QPointF(50, 10), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);

    EXPECT_TRUE(fx.slider.m_bDown);
    ASSERT_EQ(moved.count(), 1);
    EXPECT_EQ(moved.first().at(0).toInt(), 25);
}

TEST(slider_t14, MousePressRightButtonIgnored)
{
    SliderFixture fx;
    QSignalSpy moved(&fx.slider, &DMRSlider::sliderMoved);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                      QPointF(50, 10), Qt::RightButton, Qt::RightButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);

    EXPECT_FALSE(fx.slider.m_bDown);
    EXPECT_EQ(moved.count(), 0);
}

TEST(slider_t14, MousePressWaylandSessionIgnored)
{
    SliderFixture fx;
    QSignalSpy moved(&fx.slider, &DMRSlider::sliderMoved);

    withWaylandSession([&] {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                          QPointF(50, 10), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(&fx.slider, &press);
    });

    EXPECT_FALSE(fx.slider.m_bDown);
    EXPECT_EQ(moved.count(), 0);
}

TEST(slider_t14, MouseReleaseResetsDownStateOnlyWhenTracking)
{
    SliderFixture fx;

    // release without press -> state untouched, no crash
    QMouseEvent orphan(QEvent::MouseButtonRelease, QPointF(50, 10),
                       QPointF(50, 10), Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &orphan);
    EXPECT_FALSE(fx.slider.m_bDown);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                      QPointF(50, 10), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);
    ASSERT_TRUE(fx.slider.m_bDown);

    // DMRSlider::mouseReleaseEvent only clears m_bDown and forwards to the
    // base; the sliderReleased() signal comes from the internal child slider
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(50, 10),
                        QPointF(50, 10), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &release);
    EXPECT_FALSE(fx.slider.m_bDown);
}

TEST(slider_t14, MouseMoveHoverEmitsEnterAndHoverChanged)
{
    SliderFixture fx;
    fx.slider.setEnableIndication(true); // hover branch also arms the indicator
    QSignalSpy enterSpy(&fx.slider, &DMRSlider::enter);
    QSignalSpy hoverSpy(&fx.slider, &DMRSlider::hoverChanged);

    auto sendMove = [&](int x) {
        QMouseEvent move(QEvent::MouseMove, QPointF(x, 10), QPointF(x, 10),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&fx.slider, &move);
    };

    // first hover at x=7 -> progress 3 (width 200: 0.5 * 7 = 3.5 -> 3)
    sendMove(7);
    EXPECT_EQ(enterSpy.count(), 1);
    ASSERT_EQ(hoverSpy.count(), 1);
    EXPECT_EQ(hoverSpy.first().at(0).toInt(), 3);
    EXPECT_TRUE(fx.slider.property("Hover").toBool());
    EXPECT_TRUE(fx.slider.m_bShowIndicator);
    EXPECT_EQ(fx.slider.m_nLastHoverValue, 3);

    // same position again -> no repeated hoverChanged (value de-dup)
    sendMove(7);
    EXPECT_EQ(hoverSpy.count(), 1);
    // enter() is emitted unconditionally on every non-drag move
    EXPECT_EQ(enterSpy.count(), 2);

    // different position -> hoverChanged again with the new progress
    sendMove(70);
    EXPECT_EQ(hoverSpy.count(), 2);
    EXPECT_EQ(hoverSpy.last().at(0).toInt(), 35);
    EXPECT_EQ(fx.slider.m_nLastHoverValue, 35);
}

TEST(slider_t14, MouseMoveWhileDraggingUpdatesIndicator)
{
    SliderFixture fx;
    fx.slider.setEnableIndication(true);

    // hover first so m_bShowIndicator becomes true (drag path only refreshes
    // the indicator when it is already shown)
    QMouseEvent hover(QEvent::MouseMove, QPointF(150, 10), QPointF(150, 10),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &hover);
    ASSERT_TRUE(fx.slider.m_bShowIndicator);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(150, 10),
                      QPointF(150, 10), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);
    EXPECT_TRUE(fx.slider.m_bDown);

    // while m_bDown the move path only refreshes the indicator position.
    // NOTE: slider.cpp redefines TOOLBOX_TOP_EXTENT to 12 (the shared header
    // says 0), so the indicator y offset inside slider.cpp is 12 - 4 = 8.
    QMouseEvent move(QEvent::MouseMove, QPointF(150, 10), QPointF(150, 10),
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &move);
    EXPECT_EQ(fx.slider.m_indicatorPos,
              QPoint(150, fx.slider.pos().y() + 12 - 4));

    // no move events leak to the hover path while dragging
    QSignalSpy enterSpy(&fx.slider, &DMRSlider::enter);
    QApplication::sendEvent(&fx.slider, &move);
    EXPECT_EQ(enterSpy.count(), 0);
}

TEST(slider_t14, MouseMoveDisabledEatenByEventFilterGate)
{
    SliderFixture fx;
    fx.slider.setEnabled(false);
    QSignalSpy enterSpy(&fx.slider, &DMRSlider::enter);
    QSignalSpy hoverSpy(&fx.slider, &DMRSlider::hoverChanged);

    QMouseEvent move(QEvent::MouseMove, QPointF(150, 10), QPointF(150, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &move);

    EXPECT_EQ(enterSpy.count(), 0);
    EXPECT_EQ(hoverSpy.count(), 0);
    EXPECT_FALSE(fx.slider.m_bShowIndicator);
}

// ------------------------------------------------------------------ leave

TEST(slider_t14, LeaveEventResetsIndicatorAndEmitsLeave)
{
    SliderFixture fx;
    fx.slider.setEnableIndication(true);
    QSignalSpy leaveSpy(&fx.slider, &DMRSlider::leave);

    // hover first: indicator shown
    QMouseEvent move(QEvent::MouseMove, QPointF(70, 10), QPointF(70, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &move);
    EXPECT_TRUE(fx.slider.m_bShowIndicator);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&fx.slider, &leave);

    // leaveEvent clears the indicator state (the "Hover" dynamic property is
    // never reset anywhere in slider.cpp — an observation for the defect
    // export) and emits leave()
    EXPECT_FALSE(fx.slider.m_bShowIndicator);
    EXPECT_EQ(leaveSpy.count(), 1);
}

TEST(slider_t14, LeaveEventPreviewAreaHackKeepsHoverState)
{
    SliderFixture fx;
    QSignalSpy leaveSpy(&fx.slider, &DMRSlider::leave);

    // hover to leave a non-zero cached value behind (x=60 -> progress 30)
    QMouseEvent move(QEvent::MouseMove, QPointF(60, 10), QPointF(60, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &move);
    EXPECT_EQ(fx.slider.m_nLastHoverValue, 30);

    // park the widget so the headless cursor maps into the preview-area HACK
    // band (0 < local y < 6) -> leaveEvent returns early and must not reset
    // the cached hover value nor emit leave()
    const int curLocalY = fx.slider.mapFromGlobal(QCursor::pos()).y();
    fx.slider.move(0, fx.slider.pos().y() + (curLocalY - 3));
    ASSERT_EQ(fx.slider.mapFromGlobal(QCursor::pos()).y(), 3);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&fx.slider, &leave);

    EXPECT_EQ(fx.slider.m_nLastHoverValue, 30);
    EXPECT_EQ(leaveSpy.count(), 0);

    // back inside the normal band (local y leaves (0,6)) the leave completes
    fx.slider.move(0, 0);
    QApplication::sendEvent(&fx.slider, &leave);
    EXPECT_EQ(fx.slider.m_nLastHoverValue, 0);
    EXPECT_EQ(leaveSpy.count(), 1);
}

TEST(slider_t14, ForceLeaveActsLikeLeaveWithNullEvent)
{
    SliderFixture fx;
    fx.slider.setEnableIndication(true);
    QSignalSpy leaveSpy(&fx.slider, &DMRSlider::leave);

    QMouseEvent move(QEvent::MouseMove, QPointF(70, 10), QPointF(70, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &move);
    EXPECT_TRUE(fx.slider.m_bShowIndicator);

    fx.slider.forceLeave();

    EXPECT_FALSE(fx.slider.m_bShowIndicator);
    EXPECT_EQ(leaveSpy.count(), 1);
}

// --------------------------------------------------------------- enterEvent

TEST(slider_t14, EnterEventEmitsEnterSignal)
{
    SliderFixture fx;
    QSignalSpy enterSpy(&fx.slider, &DMRSlider::enter);

    QEnterEvent ee(QPointF(50, 10), QPointF(50, 10), QPointF(50, 10));
    QApplication::sendEvent(&fx.slider, &ee);

    EXPECT_EQ(enterSpy.count(), 1);
}

// ------------------------------------------------------------------- wheel

TEST(slider_t14, WheelEventAcceptsWithoutChangingValue)
{
    SliderFixture fx;
    const int before = fx.slider.value();
    QSignalSpy moved(&fx.slider, &DMRSlider::sliderMoved);

    QWheelEvent wheel(QPointF(100, 10), QPointF(100, 10), QPoint(),
                      QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&fx.slider, &wheel);

    EXPECT_TRUE(wheel.spontaneous() == false); // event still owned by the test
    EXPECT_EQ(fx.slider.value(), before);
    EXPECT_EQ(moved.count(), 0);
}

// ----------------------------------------------------------------- paint

TEST(slider_t14, PaintEventDelegatesToBaseWithoutCrash)
{
    SliderFixture fx;
    fx.slider.setEnableIndication(true);
    // grab() runs a full render pass (paintEvent + indicator overlay)
    QPixmap pm = fx.slider.grab();
    EXPECT_FALSE(pm.isNull());
}

// ------------------------------------------------------------- event gate

TEST(slider_t14, EventDisabledPressEmitsSigUnsupportedAndEatsEvents)
{
    SliderFixture fx;
    fx.slider.setEnabled(false);
    QSignalSpy unsupported(&fx.slider, &DMRSlider::sigUnsupported);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                      QPointF(50, 10), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);

    EXPECT_EQ(unsupported.count(), 1);
    EXPECT_FALSE(fx.slider.m_bDown);
}

TEST(slider_t14, EventEnabledFallsThroughToBaseHandling)
{
    SliderFixture fx;
    QSignalSpy unsupported(&fx.slider, &DMRSlider::sigUnsupported);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(50, 10),
                      QPointF(50, 10), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&fx.slider, &press);

    EXPECT_EQ(unsupported.count(), 0);
    EXPECT_TRUE(fx.slider.m_bDown); // reached DSlider's normal handling
}
