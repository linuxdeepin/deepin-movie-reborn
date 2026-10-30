// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for src/widgets/volumeslider.cpp (class dmr::VolumeSlider).
// Suite: volume_slider_ext ; helper prefix: vse_.
//
// Coverage plan (branch -> case name):
//   * ctor initial state / child wiring          -> vse_ctor_*
//   * changeVolume clamp equivalence classes      -> vse_changeVolume/ClampEquivalence (TEST_P)
//   * changeVolume >=100 direct-emit branch       -> vse_changeVolume_over100_directEmit
//   * changeVolume <100 slider-signal path        -> vse_changeVolume_below100_sliderPath
//   * calculationStep same/opposite direction     -> vse_calculationStep/Direction (TEST_P)
//   * volumeUp/Down button path + clamps          -> vse_volumeUp_button_clamp200 / vse_volumeDown_button_clamp0
//   * volumeUp/Down wheel |step|>=120 vs <120     -> vse_volumeUp_wheel_* / vse_volumeDown_wheel_*
//   * changeMuteState guards (same state, vol==0) -> vse_changeMuteState_sameState_noop / _volumeZero_guard
//   * changeMuteState flip + settings persist     -> vse_changeMuteState_flip_emits_persists
//   * volumeChanged auto-unmute / keep-mute-at-0  -> vse_volumeChanged_autoUnmute / _zero_keepsMute
//   * refreshIcon 3 bands + mute/0 override       -> vse_refreshIcon_bands_labelText
//   * muteButtnClicked toggle                     -> vse_muteButtnClicked_toggles
//   * initVolume settings application (50ms shot) -> vse_initVolume_appliesSettings
//   * popup hidden-branch / animation state machine -> vse_popup_hidden_closeBranch / vse_popup_showAnimate_openClose
//   * showEvent geometry                          -> vse_showEvent_geometry
//   * eventFilter wheel up/down/modifier/buttons  -> vse_eventFilter_wheel* (isolated slider,
//                                                    DSlider internal wheel disabled for exact counts)
//   * eventFilter non-wheel delegation            -> vse_eventFilter_nonWheel_delegates
//   * enter/leave + delayedHide m_mouseIn guard   -> vse_delayedHide_hidesWhenNotIn / vse_enterEvent_rearmPreventsHide
//   * paintEvent both-theme smoke                 -> vse_paintEvent_doesNotCrash
//   * keyPressEvent delegates to QWidget          -> vse_keyPressEvent_delegates_noVolumeChange
//   * readSinkInputPath (DBus, stubbed)           -> vse_readSinkInputPath_*
//   * setMute guard + DBus-invalid path           -> vse_setMute_guard_zeroVolume / vse_setMute_dbusInvalid_noCrash
//   * stopTimer / setThemeType idempotent         -> vse_stopTimer_idempotent / vse_setThemeType_noop
//
// Safety rules:
//   * Only Google Test (TEST/TEST_P). The shared harness main (test_qtestmain.cpp)
//     supplies main(); never define main() here.
//   * Sliders are constructed ISOLATED per case (fresh VolumeSlider, parentless) —
//     the shared toolbox slider is wired into the app-wide volume chain
//     (toolbox -> MainWindow -> PlayerEngine) whose environment-dependent
//     round-trips made the existing shared-slider cases flaky/excluded.
//   * DBus is stubbed: ApplicationAdaptor::readDBusProperty is hooked with Stub
//     so readSinkInputPath/setMute are deterministic without org.deepin.dde.Audio1.
//   * DSlider internal wheel handling is disabled in Arrange so the event-filter
//     path yields exact +10/-10 assertions.
//   * Access to private members uses the project-wide `#define private public`
//     convention around the project headers only.

// STL / Qt / DTK headers BEFORE the access-unlocking #defines (same order as
// tests/deepin-movie/common/test_boost_dm_wid.cpp which compiles in this binary).
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
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QPropertyAnimation>
#include <QVariant>

#include <DSlider>
#include <DToolButton>
#include <DLabel>
#include <DArrowRectangle>

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

#include "application.h"

#define protected public
#define private public
#include "src/common/mainwindow.h"
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/volumeslider.h"
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

// Isolated VolumeSlider instance: parentless, parented label hangs off the
// harness main window (ctor requirement). Never the shared toolbox slider.
static VolumeSlider *vse_makeSlider()
{
    MainWindow *mw = dApp->getMainWindow();
    EXPECT_NE(mw, nullptr);
    return new VolumeSlider(mw, nullptr);
}

// Find the inner DSlider child (objectName VOLUME_SLIDER).
static DSlider *vse_slider(VolumeSlider *vs)
{
    return vs->findChild<DSlider *>(VOLUME_SLIDER);
}

// DBus stub state for ApplicationAdaptor::readDBusProperty.
static int vse_dbusCallCount = 0;
static bool vse_dbusValid = false;          // SinkInputs property validity
static QList<QDBusObjectPath> vse_sinkPaths; // returned when valid
static bool vse_namesValid = true;           // per-sink Name property validity

static QVariant vse_readDBusProperty_stub(const QString &sService, const QString &sPath,
                                          const QString &sInterface, const char *pPropert)
{
    Q_UNUSED(sService); Q_UNUSED(sInterface); Q_UNUSED(pPropert);
    vse_dbusCallCount++;
    if (sPath == "/org/deepin/dde/Audio1") {
        // SinkInputs list request.
        if (!vse_dbusValid) return QVariant();
        return QVariant::fromValue(vse_sinkPaths);
    }
    // Per-sink Name request.
    if (!vse_namesValid) return QVariant();
    if (sPath.endsWith("/1")) return QVariant(QString("Firefox"));
    if (sPath.endsWith("/2")) return QVariant(QString("Deepin Movie"));
    return QVariant(QString("Other"));
}

// Install the DBus stub (call at Arrange time; Stub dtor restores).
typedef QVariant (*vse_fptr)(const QString &, const QString &, const QString &, const char *);
static void vse_installDBusStub()
{
    qRegisterMetaType<QList<QDBusObjectPath>>("QList<QDBusObjectPath>");
    static Stub stub;
    stub.set((vse_fptr)&ApplicationAdaptor::readDBusProperty, vse_readDBusProperty_stub);
    vse_dbusCallCount = 0;
    vse_dbusValid = false;
    vse_sinkPaths.clear();
    vse_namesValid = true;
}

// Wheel event helper (NoButton/NoModifier unless overridden).
static void vse_sendWheel(DSlider *slider, int deltaY, Qt::KeyboardModifiers mods = Qt::NoModifier,
                          Qt::MouseButtons btns = Qt::NoButton)
{
    QPoint pos = slider->rect().center();
    QWheelEvent we(pos, slider->mapToGlobal(pos), QPoint(0, deltaY), QPoint(0, deltaY),
                   btns, mods, Qt::NoScrollPhase, false);
    QApplication::sendEvent(slider, &we);
}

static bool vse_hasScreen()
{
    return QGuiApplication::primaryScreen() != nullptr;
}

// Restore safe settings defaults after a case touched global_volume/mute.
static void vse_restoreVolumeSettings()
{
    Settings::get().setInternalOption("global_volume", 100);
    Settings::get().setInternalOption("mute", false);
}

// ==========================================================================
// ctor / initial state
// ==========================================================================

TEST(volume_slider_ext, vse_ctor_initial_state)
{
    // Arrange + Act: fresh isolated slider.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    // Assert: documented initial values.
    EXPECT_EQ(vs->state(), VolumeSlider::Close);
    EXPECT_FALSE(vs->getsliderstate());
    EXPECT_EQ(vs->getVolume(), 100);
    EXPECT_FALSE(vs->m_bIsMute);
    EXPECT_FALSE(vs->m_bIsWheel);
    EXPECT_EQ(vs->m_iStep, 0);
    EXPECT_FALSE(vs->isVisible());
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->slider()->minimum(), 0);
    EXPECT_EQ(slider->slider()->maximum(), 100);
    EXPECT_EQ(slider->slider()->value(), 100);
    DToolButton *btn = vs->findChild<DToolButton *>(MUTE_BTN);
    ASSERT_NE(btn, nullptr);
    // Volume label parented to the main window, initial text "0%".
    EXPECT_NE(vs->m_pLabShowVolume, nullptr);
    EXPECT_EQ(vs->m_pLabShowVolume->text(), QString("0%"));

    delete vs;
}

TEST(volume_slider_ext, vse_ctor_child_objectNames)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->objectName(), QString(VOLUME_SLIDER));
    EXPECT_EQ(slider->slider()->objectName(), QString(SLIDER));
    DToolButton *btn = vs->findChild<DToolButton *>(MUTE_BTN);
    ASSERT_NE(btn, nullptr);
    EXPECT_EQ(btn->objectName(), QString(MUTE_BTN));

    delete vs;
}

// ==========================================================================
// changeVolume: clamp equivalence classes (negative / zero / low / boundary 100 / high / over-200)
// ==========================================================================

namespace {
struct VseCV { int in; int expected; };
std::ostream &operator<<(std::ostream &os, const VseCV &p)
{
    return os << "{in=" << p.in << ", expected=" << p.expected << "}";
}
class VseChangeVolume : public ::testing::TestWithParam<VseCV> {};
TEST_P(VseChangeVolume, ClampEquivalence)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    const VseCV p = GetParam();

    // Act
    vs->changeVolume(p.in);

    // Assert: internal volume clamped, slider mirrors min(volume, 100).
    EXPECT_EQ(vs->getVolume(), p.expected);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->slider()->value(), qMin(p.expected, 100));

    delete vs;
}
INSTANTIATE_TEST_SUITE_P(volume_slider_ext, VseChangeVolume,
    ::testing::Values(VseCV{-50, 0}, VseCV{0, 0}, VseCV{1, 1}, VseCV{42, 42},
                      VseCV{99, 99}, VseCV{100, 100}, VseCV{150, 150},
                      VseCV{200, 200}, VseCV{1000, 200}));
} // namespace

TEST(volume_slider_ext, vse_changeVolume_over100_directEmit)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    int captured = -1;
    auto c = QObject::connect(vs, &VolumeSlider::sigVolumeChanged,
                              [&captured](int v) { captured = v; });

    // Act: >=100 branch calls volumeChanged directly (slider is capped at 100).
    vs->changeVolume(150);

    // Assert: emitted synchronously with the unclamped-but-in-range value.
    EXPECT_EQ(captured, 150);
    EXPECT_EQ(vs->getVolume(), 150);
    QObject::disconnect(c);

    delete vs;
}

TEST(volume_slider_ext, vse_changeVolume_below100_sliderPath)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    int captured = -1;
    auto c = QObject::connect(vs, &VolumeSlider::sigVolumeChanged,
                              [&captured](int v) { captured = v; });

    // Act: <100 branch relies on DSlider valueChanged -> volumeChanged.
    vs->changeVolume(70);

    // Assert
    EXPECT_EQ(captured, 70);
    EXPECT_EQ(vs->getVolume(), 70);
    QObject::disconnect(c);

    delete vs;
}

// ==========================================================================
// calculationStep: same-direction accumulate vs direction reset
// ==========================================================================

namespace {
struct VseStep { int init; int delta; int expected; };
std::ostream &operator<<(std::ostream &os, const VseStep &p)
{
    return os << "{init=" << p.init << ", delta=" << p.delta << ", expected=" << p.expected << "}";
}
class VseCalcStep : public ::testing::TestWithParam<VseStep> {};
TEST_P(VseCalcStep, Direction)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    const VseStep p = GetParam();

    // Arrange: seed previous step.
    vs->m_iStep = p.init;

    // Act
    vs->calculationStep(p.delta);

    // Assert: same sign accumulates, otherwise resets; wheel flag set.
    EXPECT_EQ(vs->m_iStep, p.expected);
    EXPECT_TRUE(vs->m_bIsWheel);

    delete vs;
}
INSTANTIATE_TEST_SUITE_P(volume_slider_ext, VseCalcStep,
    ::testing::Values(VseStep{0, 120, 120}, VseStep{120, 120, 240},
                      VseStep{-120, -120, -240}, VseStep{120, -120, -120},
                      VseStep{-120, 120, 120}, VseStep{0, -120, -120}));
} // namespace

// ==========================================================================
// volumeUp / volumeDown: button path and wheel path
// ==========================================================================

TEST(volume_slider_ext, vse_volumeUp_button_clamp200)
{
    // Arrange: 195 -> one more +10 step clamps at 200.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(195);
    vs->m_bIsWheel = false;

    // Act
    vs->volumeUp();

    // Assert
    EXPECT_EQ(vs->getVolume(), 200);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeDown_button_clamp0)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(5);
    vs->m_bIsWheel = false;

    vs->volumeDown();

    EXPECT_EQ(vs->getVolume(), 0);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeUp_wheel_step120_plus10)
{
    // Arrange: isolated slider, internal wheel disabled for exact counts.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    // Act: one wheel notch -> calculationStep(120) then volumeUp.
    vse_sendWheel(slider, 120);

    // Assert: |step|>=120 branch adds exactly one 10-step and resets.
    EXPECT_EQ(vs->getVolume(), 50);
    EXPECT_EQ(vs->m_iStep, 0);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeUp_wheel_multiStep_plus20)
{
    // Arrange: accumulate two notches before delivering the up-call.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);
    vs->calculationStep(120);
    vs->calculationStep(120);

    // Act
    vs->volumeUp();

    // Assert: 240/120 * 10 = 20.
    EXPECT_EQ(vs->getVolume(), 60);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeUp_wheel_smallStep_noChange)
{
    // Arrange: |step| < 120 (high-resolution wheel).
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);
    vs->calculationStep(100);

    // Act
    vs->volumeUp();

    // Assert: threshold branch leaves volume and step untouched.
    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 100);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeDown_wheel_step120_minus10)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    vse_sendWheel(slider, -120);

    EXPECT_EQ(vs->getVolume(), 30);
    EXPECT_EQ(vs->m_iStep, 0);
    EXPECT_FALSE(vs->m_bIsWheel);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeDown_wheel_smallStep_noChange)
{
    VolumeSlider *vs = vse_makeSlider();
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
// changeMuteState: guards, flip + persistence, auto-unmute interactions
// ==========================================================================

TEST(volume_slider_ext, vse_changeMuteState_sameState_noop)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(50);
    int emitted = 0;
    auto c = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                              [&emitted](bool) { emitted++; });

    // Act: same-state request must early-return.
    vs->changeMuteState(false);
    vs->changeMuteState(true);
    vs->changeMuteState(true);

    // Assert: exactly one real flip emitted.
    EXPECT_EQ(emitted, 1);
    EXPECT_TRUE(vs->m_bIsMute);
    QObject::disconnect(c);

    delete vs;
}

TEST(volume_slider_ext, vse_changeMuteState_volumeZero_guard)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(0);
    int emitted = 0;
    auto c = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                              [&emitted](bool) { emitted++; });

    vs->changeMuteState(true);

    // Volume-0 guard: request ignored.
    EXPECT_EQ(emitted, 0);
    EXPECT_FALSE(vs->m_bIsMute);
    QObject::disconnect(c);

    delete vs;
}

TEST(volume_slider_ext, vse_changeMuteState_flip_emits_persists)
{
    // Arrange: DBus stubbed so setMute() inside muteButtnClicked never blocks.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(50);
    int captured = -1;
    auto c = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                              [&captured](bool b) { captured = b ? 1 : 0; });

    // Act
    vs->changeMuteState(true);

    // Assert: signal + settings persistence.
    EXPECT_EQ(captured, 1);
    EXPECT_TRUE(vs->m_bIsMute);
    EXPECT_EQ(Settings::get().internalOption("mute").toBool(), true);

    // Act again
    vs->changeMuteState(false);
    EXPECT_EQ(captured, 0);
    EXPECT_FALSE(vs->m_bIsMute);
    EXPECT_EQ(Settings::get().internalOption("mute").toBool(), false);
    QObject::disconnect(c);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeChanged_autoUnmute)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(50);
    vs->changeMuteState(true);
    ASSERT_TRUE(vs->m_bIsMute);
    QList<int> volumes;
    QList<bool> mutes;
    auto c1 = QObject::connect(vs, &VolumeSlider::sigVolumeChanged,
                               [&volumes](int v) { volumes << v; });
    auto c2 = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                               [&mutes](bool b) { mutes << b; });

    // Act: raising the volume while muted must unmute.
    vs->changeVolume(80);

    // Assert: unmute emitted before the volume signal.
    EXPECT_EQ(mutes, QList<bool>{false});
    EXPECT_EQ(volumes, QList<int>{80});
    EXPECT_FALSE(vs->m_bIsMute);
    QObject::disconnect(c1);
    QObject::disconnect(c2);

    delete vs;
}

TEST(volume_slider_ext, vse_volumeChanged_zero_keepsMute)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(50);
    vs->changeMuteState(true);
    ASSERT_TRUE(vs->m_bIsMute);
    QList<bool> mutes;
    auto c2 = QObject::connect(vs, &VolumeSlider::sigMuteStateChanged,
                               [&mutes](bool b) { mutes << b; });

    // Act: changeVolume(0) must not touch the mute state.
    vs->changeVolume(0);

    EXPECT_EQ(mutes, QList<bool>{});
    EXPECT_TRUE(vs->m_bIsMute);
    EXPECT_EQ(vs->getVolume(), 0);
    QObject::disconnect(c2);

    delete vs;
}

// ==========================================================================
// refreshIcon: band selection + label text
// ==========================================================================

TEST(volume_slider_ext, vse_refreshIcon_bands_labelText)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    DToolButton *btn = vs->findChild<DToolButton *>(MUTE_BTN);
    ASSERT_NE(btn, nullptr);

    struct Band { int vol; const char *text; };
    const Band bands[] = {
        {80, "80%"},  // >= 66 high band
        {66, "66%"},  // boundary
        {65, "65%"},  // 33..65 mid band
        {33, "33%"},  // boundary
        {32, "32%"},  // < 33 low band
        {10, "10%"},
        {0,  "0%"},   // zero-volume override band
    };
    for (const Band &b : bands) {
        vs->changeVolume(b.vol);
        // Label text proves refreshIcon ran with the band value. The icon
        // itself resolves through the system icon theme, which is unavailable
        // on offscreen hosts (icon stays null), so only the text is asserted.
        EXPECT_EQ(vs->m_pLabShowVolume->text(), QString(b.text)) << "volume " << b.vol;
    }

    delete vs;
}

// ==========================================================================
// muteButtnClicked
// ==========================================================================

TEST(volume_slider_ext, vse_muteButtnClicked_toggles)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(50);
    ASSERT_FALSE(vs->m_bIsMute);

    // Act: two clicks toggle twice.
    vs->muteButtnClicked();
    EXPECT_TRUE(vs->m_bIsMute);
    vs->muteButtnClicked();
    EXPECT_FALSE(vs->m_bIsMute);

    delete vs;
}

// ==========================================================================
// initVolume: reads global_volume/mute on a 50ms single shot
// ==========================================================================

TEST(volume_slider_ext, vse_initVolume_appliesSettings)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    Settings::get().setInternalOption("global_volume", 77);
    Settings::get().setInternalOption("mute", true);

    // Act
    vs->initVolume();
    QTest::qWait(150);   // single shot fires at 50ms; generous margin

    // Assert
    EXPECT_EQ(vs->getVolume(), 77);
    EXPECT_TRUE(vs->m_bIsMute);
    vse_restoreVolumeSettings();

    delete vs;
}

// ==========================================================================
// popup / showEvent: hidden branch and animation state machine
// ==========================================================================

TEST(volume_slider_ext, vse_popup_hidden_closeBranch)
{
    // Arrange: never-shown slider.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    // Act: popup() while hidden takes the else branch.
    vs->popup();

    // Assert: state forced Close, still hidden, no animation object leaked.
    EXPECT_EQ(vs->state(), VolumeSlider::Close);
    EXPECT_FALSE(vs->isVisible());
    EXPECT_EQ(vs->pVolAnimation, nullptr);

    delete vs;
}

TEST(volume_slider_ext, vse_popup_showAnimate_openClose)
{
    if (!vse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    // Arrange: shown slider (showEvent lays out geometry).
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act 1: first popup opens the animation; getsliderstate() reports pending.
    vs->popup();
    EXPECT_TRUE(vs->getsliderstate());
    QTest::qWait(400);   // animation runs 230ms
    EXPECT_EQ(vs->state(), VolumeSlider::Open);
    EXPECT_FALSE(vs->getsliderstate());
    EXPECT_TRUE(vs->isVisible());

    // Act 2: second popup takes the else branch (state Open -> Close + hide).
    vs->popup();
    EXPECT_EQ(vs->state(), VolumeSlider::Close);
    QTest::qWait(30);
    EXPECT_FALSE(vs->isVisible());

    delete vs;
}

TEST(volume_slider_ext, vse_popup_whileAnimating_flagsHideWhenFinished)
{
    if (!vse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    vs->popup();
    ASSERT_TRUE(vs->pVolAnimation != nullptr);   // animation running
    ASSERT_TRUE(vs->m_bHideWhenFinished == false);

    // Act: popup() during animation hits the early return.
    vs->popup();

    // Assert: deferred-hide latch set, no second animation created.
    EXPECT_TRUE(vs->m_bHideWhenFinished);
    QTest::qWait(600);
    EXPECT_EQ(vs->pVolAnimation, nullptr);

    delete vs;
}

TEST(volume_slider_ext, vse_showEvent_geometry)
{
    if (!vse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    vs->show();
    QTest::qWait(30);

    EXPECT_EQ(vs->geometry().width(), VOLSLIDER_WIDTH);
    EXPECT_EQ(vs->geometry().height(), VOLSLIDER_HEIGHT);

    delete vs;
}

// ==========================================================================
// eventFilter: wheel dispatch and delegation
// ==========================================================================

TEST(volume_slider_ext, vse_eventFilter_wheelModifiers_ignored)
{
    // Arrange
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    // Act: modifier held -> filter's NoModifier branch skipped.
    vse_sendWheel(slider, 120, Qt::ControlModifier);

    // Assert
    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(volume_slider_ext, vse_eventFilter_wheelButtons_ignored)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    slider->setMouseWheelEnabled(false);
    vs->changeVolume(40);

    vse_sendWheel(slider, 120, Qt::NoModifier, Qt::LeftButton);

    EXPECT_EQ(vs->getVolume(), 40);
    EXPECT_EQ(vs->m_iStep, 0);

    delete vs;
}

TEST(volume_slider_ext, vse_eventFilter_nonWheel_delegates)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    vs->changeVolume(40);

    // Non-wheel event falls through to QObject::eventFilter.
    QPoint pos = slider->rect().center();
    QMouseEvent me(QEvent::MouseMove, pos, slider->mapToGlobal(pos),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &me);

    EXPECT_EQ(vs->getVolume(), 40);

    delete vs;
}

// ==========================================================================
// enter / leave / delayedHide: m_mouseIn guard
// ==========================================================================

TEST(volume_slider_ext, vse_delayedHide_hidesWhenNotIn)
{
    if (!vse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act: leave triggers delayedHide; 200ms later, if not re-entered, hide.
    QHideEvent he;   // unused; deliver a synthetic Leave
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(vs, &leave);
    Q_UNUSED(he)
    QTest::qWait(350);

    EXPECT_FALSE(vs->isVisible());

    delete vs;
}

TEST(volume_slider_ext, vse_enterEvent_rearmPreventsHide)
{
    if (!vse_hasScreen()) GTEST_SKIP() << "headless: no primary screen";
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->show();
    QTest::qWait(30);
    ASSERT_TRUE(vs->isVisible());

    // Act: leave schedules the 200ms hide; re-enter inside the window disarms it.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(vs, &leave);
    QTest::qWait(50);
    QPoint pos = vs->rect().center();
    QEnterEvent ee(pos, vs->mapToGlobal(pos), pos);
    QApplication::sendEvent(vs, &ee);
    EXPECT_TRUE(vs->m_mouseIn);
    QTest::qWait(300);

    // Assert: guard kept the popup visible.
    EXPECT_TRUE(vs->isVisible());

    delete vs;
}

// ==========================================================================
// paintEvent / keyPressEvent / misc accessors
// ==========================================================================

TEST(volume_slider_ext, vse_paintEvent_doesNotCrash)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    DSlider *slider = vse_slider(vs);
    ASSERT_NE(slider, nullptr);
    vs->changeVolume(60);

    // Non-X86 hosts take the plain-rect path; X86 the rounded+triangle path.
    QPaintEvent pe(vs->rect());
    QApplication::sendEvent(vs, &pe);

    EXPECT_TRUE(vs != nullptr);
    delete vs;
}

TEST(volume_slider_ext, vse_keyPressEvent_delegates_noVolumeChange)
{
    // Arrange: VolumeSlider::keyPressEvent delegates to QWidget (no handling).
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(40);

    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vs, &up);

    EXPECT_EQ(vs->getVolume(), 40);

    delete vs;
}

TEST(volume_slider_ext, vse_stopTimer_idempotent)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    vs->stopTimer();
    vs->stopTimer();

    EXPECT_FALSE(vs->m_autoHideTimer.isActive());
    delete vs;
}

TEST(volume_slider_ext, vse_setThemeType_noop)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);

    vs->setThemeType(2);

    EXPECT_EQ(vs->getVolume(), 100);
    delete vs;
}

// ==========================================================================
// readSinkInputPath / setMute (DBus fully stubbed)
// ==========================================================================

TEST(volume_slider_ext, vse_readSinkInputPath_invalidProperty_returnsEmpty)
{
    // Arrange: SinkInputs read returns invalid -> early return.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();

    // Act
    QString path = vs->readSinkInputPath();

    // Assert
    EXPECT_TRUE(path.isEmpty());
    EXPECT_EQ(vse_dbusCallCount, 1);

    delete vs;
}

TEST(volume_slider_ext, vse_readSinkInputPath_findsMovieSink)
{
    // Arrange: two sinks; only the second matches "Movie" (case-insensitive).
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vse_dbusValid = true;
    vse_sinkPaths << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/1")
                  << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/2");

    // Act
    QString path = vs->readSinkInputPath();

    // Assert: continue-branch skipped the non-movie sink; match+break returned it.
    EXPECT_EQ(path, QString("/org/deepin/dde/Audio1/SinkInput/2"));
    EXPECT_EQ(vse_dbusCallCount, 3);   // SinkInputs + two Name reads

    delete vs;
}

TEST(volume_slider_ext, vse_readSinkInputPath_noMatch_returnsEmpty)
{
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vse_dbusValid = true;
    vse_sinkPaths << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/1");

    QString path = vs->readSinkInputPath();

    EXPECT_TRUE(path.isEmpty());
    EXPECT_EQ(vse_dbusCallCount, 2);   // SinkInputs + one Name read

    delete vs;
}

TEST(volume_slider_ext, vse_setMute_guard_zeroVolume)
{
    // Arrange: m_bIsMute already matches the request, but volume 0 hits the
    // second half of the guard before any DBus traffic.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vs->changeVolume(0);
    vs->m_bIsMute = true;

    // Act
    vs->setMute(true);

    // Assert: no SinkInputs read happened.
    EXPECT_EQ(vse_dbusCallCount, 0);

    delete vs;
}

TEST(volume_slider_ext, vse_setMute_dbusInvalid_noCrash)
{
    // Arrange: setMute proceeds only when m_bIsMute already equals the request
    // ("push current state to dock" semantics). The stubbed sink path points
    // at a fake object, so the QDBusInterface is invalid on any host -> the
    // SetMute call is skipped and the function returns cleanly.
    VolumeSlider *vs = vse_makeSlider();
    ASSERT_NE(vs, nullptr);
    vse_installDBusStub();
    vse_dbusValid = true;
    vse_sinkPaths << QDBusObjectPath("/org/deepin/dde/Audio1/SinkInput/2");
    vs->changeVolume(50);
    vs->m_bIsMute = true;

    // Act
    vs->setMute(true);

    // Assert: readSinkInputPath ran; no crash, mute flag untouched by setMute.
    EXPECT_GT(vse_dbusCallCount, 0);
    EXPECT_TRUE(vs->m_bIsMute);

    delete vs;
}
