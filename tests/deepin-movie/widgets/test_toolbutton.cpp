// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gap-filling unit tests for src/widgets/toolbutton.cpp (dmr::VolumeButton)
// — UT-movie-t14.
//
// Mode 6 review verdict on the existing suite (tests/deepin-movie-platform/
// widgets/test_small_widgets_ext.cpp): construct / initToolTip / entered /
// leaved / setVolume levels / setMute / eventFilter disabled-press are
// covered smoke-style (EXPECT_NO_FATAL_FAILURE) and stay untouched. The
// gaps filled here with real assertions:
//   * changeStyle boundary values 66/65/33/32 (the >= branches) and the
//     mute / volume==0 icon override, asserted through a stub icon theme
//     (dcc_* icons only exist as DCI resources, invisible to a bare
//     QGuiApplication)
//   * eventFilter return values called directly on the button (disabled
//     press -> false + sigUnsupported, disabled non-press -> false, enabled
//     press -> false — every branch returns false)
//   * wheelEvent ignored branch (modifier / buttons held) and the
//     angleDelta().y() == 0 else-branch (requestVolumeDown)
//   * setButtonEnable(false) icon + re-enable icon restoration
//   * hideTip with an actually visible tooltip, and with a running timer
//   * enterEvent tooltip timer on the cached isWayland flag (toggled via
//     utils::set_wayland, the USE_TEST hook)
//   * leaveEvent hides a visible tooltip (non-Wayland branch)
//   * focusOutEvent emits leaved on non-MIPS/ARM builds
//
// Safety rules (mirroring the existing suites):
//   * Google Test only, no main() here; suite name "toolbutton_t14" is
//     unique inside deepin-movie-test.
//   * Fresh button per case with its own parent widget; the tooltip timer is
//     always stopped at the end of a case so no stray timeout can fire the
//     constructor's parentWidget()->mapToGlobal() lambda (which itself never
//     null-checks parentWidget() — see defects export).
//   * Cases touching windowing (tooltip visibility) are guarded by
//     QGuiApplication::primaryScreen().
//   * No real playback / backend work.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QSignalSpy>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QIcon>

#define protected public
#define private public
#include "src/widgets/toolbutton.h"
#include "src/libdmr/utils.h"
#undef private
#undef protected

using namespace dmr;

namespace {

#define UT_VOL_THEME_NAME "ut_vol_theme_t14"
static const char *kVolumeIcons[] = {
    "dcc_volume", "dcc_volumemid", "dcc_volumelow", "dcc_mute", "dcc_volumedisable"
};

// Installs a throwaway icon theme so QIcon::fromTheme("dcc_*") resolves to
// real files in every environment (headless included). Restores the previous
// theme settings on scope exit.
class VolumeThemeGuard
{
public:
    VolumeThemeGuard()
    {
        m_savedName = QIcon::themeName();
        m_savedPaths = QIcon::themeSearchPaths();

        QVERIFY2(m_dir.isValid(), "QTemporaryDir for stub icon theme");
        const QString themeDir = m_dir.path() + QLatin1Char('/') + UT_VOL_THEME_NAME;
        const QString appDir = themeDir + "/48x48/apps";
        QVERIFY2(QDir().mkpath(appDir), "mkpath for stub theme icons");

        QFile index(themeDir + "/index.theme");
        QVERIFY2(index.open(QIODevice::WriteOnly | QIODevice::Text),
                 "write stub index.theme");
        index.write("[Icon Theme]\n"
                    "Name=" UT_VOL_THEME_NAME "\n"
                    "Comment=Unit test stub theme\n"
                    "Directories=48x48/apps\n"
                    "\n"
                    "[48x48/apps]\n"
                    "Size=48\n"
                    "Context=Applications\n"
                    "Type=Fixed\n");
        index.close();

        for (const char *icon : kVolumeIcons) {
            QFile f(appDir + QLatin1Char('/') + icon + ".svg");
            QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text), icon);
            f.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"48\" "
                    "height=\"48\"><rect width=\"48\" height=\"48\"/></svg>");
            f.close();
        }

        QIcon::setThemeSearchPaths({m_dir.path()});
        QIcon::setThemeName(QLatin1String(UT_VOL_THEME_NAME));
    }

    ~VolumeThemeGuard()
    {
        QIcon::setThemeSearchPaths(m_savedPaths);
        QIcon::setThemeName(m_savedName);
    }

private:
    QTemporaryDir m_dir;
    QString m_savedName;
    QStringList m_savedPaths;
};

struct ButtonFixture {
    VolumeThemeGuard theme;   // must come first: the ctor resolves icons
    QWidget parent;
    VolumeButton button{&parent};

    ButtonFixture() { button.resize(36, 36); }
};

} // namespace

// ------------------------------------------------------------------ ctor

TEST(toolbutton_t14, ctorDefaultsVolume100UnmutedHighIcon)
{
    ButtonFixture fx;
    EXPECT_EQ(fx.button.m_nVolume, 100);
    EXPECT_FALSE(fx.button.m_bMute);
    EXPECT_TRUE(fx.button.isEnabled());
    EXPECT_NE(fx.button.m_pToolTip, nullptr);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volume"));
}

// ------------------------------------------------------------ changeStyle

TEST(toolbutton_t14, changeStyleBoundaryValuesPickCorrectIcon)
{
    ButtonFixture fx;

    fx.button.setVolume(66); // >= 66 -> high
    EXPECT_EQ(fx.button.m_nVolume, 66);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volume"));

    fx.button.setVolume(65); // 33..65 -> mid
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volumemid"));

    fx.button.setVolume(33); // still mid
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volumemid"));

    fx.button.setVolume(32); // < 33 -> low
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volumelow"));
}

TEST(toolbutton_t14, changeStyleMuteAndZeroOverrideToMuteIcon)
{
    ButtonFixture fx;

    fx.button.setVolume(0);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_mute")); // volume==0 branch

    fx.button.setVolume(80);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volume"));

    fx.button.setMute(true);
    EXPECT_TRUE(fx.button.m_bMute);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_mute")); // mute override

    fx.button.setMute(false);
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volume")); // restored
}

// --------------------------------------------------------------- setButtonEnable

TEST(toolbutton_t14, setButtonEnableFalseThenTrueRestoresStyle)
{
    ButtonFixture fx;

    fx.button.setButtonEnable(false);
    EXPECT_FALSE(fx.button.isEnabled());
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volumedisable"));

    fx.button.setButtonEnable(true);
    EXPECT_TRUE(fx.button.isEnabled());
    EXPECT_EQ(fx.button.icon().name(), QString("dcc_volume")); // changeStyle()
}

// ----------------------------------------------------------------- eventFilter

TEST(toolbutton_t14, eventFilterDisabledButtonNonPressEventSilent)
{
    ButtonFixture fx;
    fx.button.setButtonEnable(false);
    QSignalSpy unsupported(&fx.button, &VolumeButton::sigUnsupported);

    // a disabled button receiving a RELEASE must stay silent (only press
    // signals unsupported); called directly to observe the filter verdict
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5),
                        QPointF(5, 5), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    const bool handled = fx.button.eventFilter(&fx.button, &release);

    EXPECT_FALSE(handled);
    EXPECT_EQ(unsupported.count(), 0);
}

TEST(toolbutton_t14, eventFilterDisabledButtonPressEventSignalsUnsupported)
{
    ButtonFixture fx;
    fx.button.setButtonEnable(false);
    QSignalSpy unsupported(&fx.button, &VolumeButton::sigUnsupported);

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    const bool handled = fx.button.eventFilter(&fx.button, &press);

    // emits sigUnsupported AND still returns false (letting the event through)
    EXPECT_FALSE(handled);
    EXPECT_EQ(unsupported.count(), 1);
}

TEST(toolbutton_t14, eventFilterEnabledButtonDelegatesToBase)
{
    ButtonFixture fx;
    QSignalSpy unsupported(&fx.button, &VolumeButton::sigUnsupported);

    // enabled buttons delegate to QObject::eventFilter -> false, no signal
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    EXPECT_FALSE(fx.button.eventFilter(&fx.button, &press));
    EXPECT_EQ(unsupported.count(), 0);
}

// ------------------------------------------------------------------ wheelEvent

TEST(toolbutton_t14, wheelEventZeroDeltaFallsToVolumeDownBranch)
{
    ButtonFixture fx;
    QSignalSpy up(&fx.button, &VolumeButton::requestVolumeUp);
    QSignalSpy down(&fx.button, &VolumeButton::requestVolumeDown);

    // angleDelta().y() == 0 takes the else branch -> requestVolumeDown
    QWheelEvent flat(QPointF(18, 18), QPointF(18, 18), QPoint(), QPoint(0, 0),
                     Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&fx.button, &flat);

    EXPECT_EQ(up.count(), 0);
    EXPECT_EQ(down.count(), 1);
}

TEST(toolbutton_t14, wheelEventWithModifierIgnored)
{
    ButtonFixture fx;
    QSignalSpy up(&fx.button, &VolumeButton::requestVolumeUp);
    QSignalSpy down(&fx.button, &VolumeButton::requestVolumeDown);

    QWheelEvent ctrl(QPointF(18, 18), QPointF(18, 18), QPoint(), QPoint(0, 120),
                     Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&fx.button, &ctrl);
    EXPECT_EQ(up.count(), 0);
    EXPECT_EQ(down.count(), 0);

    // buttons held also suppresses the volume request
    QWheelEvent held(QPointF(18, 18), QPointF(18, 18), QPoint(), QPoint(0, 120),
                     Qt::LeftButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&fx.button, &held);
    EXPECT_EQ(up.count(), 0);
    EXPECT_EQ(down.count(), 0);
}

// -------------------------------------------------------------------- hideTip

TEST(toolbutton_t14, hideTipStopsRunningTimer)
{
    ButtonFixture fx;
    fx.button.m_showTime.start(100000);
    ASSERT_TRUE(fx.button.m_showTime.isActive());

    fx.button.hideTip();
    EXPECT_FALSE(fx.button.m_showTime.isActive());
}

TEST(toolbutton_t14, hideTipHidesVisibleTooltip)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ButtonFixture fx;

    fx.button.m_pToolTip->show();
    ASSERT_TRUE(fx.button.m_pToolTip->isVisible());

    fx.button.hideTip();
    EXPECT_FALSE(fx.button.m_pToolTip->isVisible());
    EXPECT_FALSE(fx.button.m_showTime.isActive());
}

// --------------------------------------------------------------- enter / leave

TEST(toolbutton_t14, enterEventStartsTimerNonWayland)
{
    ButtonFixture fx;
    QSignalSpy entered(&fx.button, &VolumeButton::entered);

    QEnterEvent ee(QPointF(18, 18), QPointF(18, 18), QPointF(18, 18));
    QApplication::sendEvent(&fx.button, &ee);

    EXPECT_EQ(entered.count(), 1);
    EXPECT_TRUE(fx.button.m_showTime.isActive()); // 1000ms tooltip timer
    fx.button.hideTip();                          // leave the case clean
}

TEST(toolbutton_t14, enterEventWaylandSkipsTimer)
{
    ButtonFixture fx;
    QSignalSpy entered(&fx.button, &VolumeButton::entered);

    // check_wayland_env() caches the verdict in a process-global flag; the
    // USE_TEST build exposes utils::set_wayland as the toggle
    utils::set_wayland(true);
    QEnterEvent ee(QPointF(18, 18), QPointF(18, 18), QPointF(18, 18));
    QApplication::sendEvent(&fx.button, &ee);
    utils::set_wayland(false); // restore even if assertions fail

    EXPECT_EQ(entered.count(), 1);
    EXPECT_FALSE(fx.button.m_showTime.isActive()); // no timer on Wayland
}

TEST(toolbutton_t14, leaveEventStopsTimerAndHidesVisibleTooltip)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ButtonFixture fx;
    QSignalSpy leaved(&fx.button, &VolumeButton::leaved);

    fx.button.m_showTime.start(100000);
    fx.button.m_pToolTip->show();

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&fx.button, &leave);

    EXPECT_EQ(leaved.count(), 1);
    EXPECT_FALSE(fx.button.m_showTime.isActive());
    EXPECT_FALSE(fx.button.m_pToolTip->isVisible());
}

// ---------------------------------------------------------------- focusOut

TEST(toolbutton_t14, focusOutEventEmitsLeavedOnX86)
{
    ButtonFixture fx;
    QSignalSpy leaved(&fx.button, &VolumeButton::leaved);

    QFocusEvent fo(QEvent::FocusOut);
    QApplication::sendEvent(&fx.button, &fo);

#if !defined(__mips__) && !defined(__aarch64__)
    EXPECT_EQ(leaved.count(), 1);
#endif
}
