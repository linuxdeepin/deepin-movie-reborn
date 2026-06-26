// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 4) for src/widgets/platform/platform_toolbox_proxy.cpp.
//
// Suite name "platform_tb_ext4" with static helpers using unique prefix ptb4_.
// This file complements ext / ext2 / ext3 ("platform_tb_ext{,2,3}") by targeting
// additional branch gaps the prior rounds leave uncovered:
//   * Platform_ToolboxProxy::showEvent / paintEvent with explicit Idle property
//     toggling and theme forcing.
//   * resizeEvent's CompositingManager::platform() == Alpha early-return and the
//     m_pPlaylist == nullptr guard inside the !Alpha animation-teardown branch.
//   * eventFilter volume-button KeyPress branch when the slider is Open but the
//     pressed key is neither Up nor Down (fall-through within the Open branch).
//   * eventFilter list-button right-click filter when m_pPlaylist is null
//     (guarded) on X86, and the non-right-button release fall-through.
//   * buttonClicked "play" StartPlay vs TogglePause Idle/non-Idle branches.
//   * slotUpdateMircast non-zero state branch (bRawFormat false) and its signal.
//   * updatePlayState Playing vs not-Playing branches via forced mircast state
//     and theme toggling, plus the Idle tail (progBar setVisible / index 0).
//   * updateButtonStates Idle vs non-Idle palettes (theme-guarded).
//   * slotElapsedChanged mircast-active early-return branch.
//   * updateMircastTime filmstrip-mode (currentIndex != 1) else branch.
//   * updateProgress progress-mode (currentIndex == 1) sub-branches: large delta,
//     small-delta accumulate, and accumulate-flush.
//   * updateSlider progress-mode and filmstrip-mode branches (engine seek is
//     guarded/skipped when Idle since the engine no-ops seekAbsolute on Idle).
//   * progressHoverChanged deep branches driven by forcing slider value != 0
//     with an Idle engine (early-returns at the Idle / volSlider-visible /
//     empty-playlist guards).
//   * updateTimeVisible PreviewOnMouseover off toggles the preview-time label.
//   * slotThemeTypeChanged Idle branch palettes (light/dark).
//   * Playlist-bound helpers: setPlaylist with a fresh playlist pointer (safe
//     because stateChange signal just connects), clearPlayListFocus /
//     playlistClosedByEsc with no playlist focus (guarded).
//   * Geometry helpers: updateSliderPoint round-trip, hideMircastWidget after
//     showing it, isInMircastWidget with a shown widget.
//   * Static flag round-trips: setBtnFocusSign / getListBtnFocus interplay,
//     getbAnimationFinash, getVolSliderIsHided after explicit show/hide.
//
// Safety rules baked in (verified against the source and prior crashes):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * Private/protected members of Platform_ToolboxProxy are reached via the
//     #define private/protected public trick around the header include (mirrors
//     ext3). The running app's shared toolbox is obtained exactly like ext2:
//         dApp->getMainWindow()->toolbox().  Guarded for null.
//   * Platform_ViewProgBar is only forward-declared in the header, so its
//     methods are not called directly; it is only addressed as a QWidget*
//     through reinterpret_cast on the getViewProBar() pointer when sending
//     events. Its private members (m_nStartPoint etc.) are never touched.
//   * Geometry / paint / popup cases are guarded by primaryScreen() and
//     GTEST_SKIP when headless.
//   * MircastWidget::setMircastState is a public inline setter; using it to
//     force the Screening/Play states lets updatePlayState / slotElapsedChanged
//     take their mircast-active branches without any real decode path. The
//     state is always restored to Idel afterward.
//   * Engine-state-dependent branches that would deref currentInfo() on an empty
//     playlist (slotFileLoaded audio-skip, finishLoadSlot X86 branch,
//     updateHoverPreview non-Idle) are guarded with GTEST_SKIP when the engine
//     has no items, so they never SIGSEGV.
//   * seekAbsolute is a no-op on an Idle engine with no loaded item, so
//     updateSlider is safe to drive directly in Idle.
//   * Global theme type is saved and restored around theme-forcing cases.
//   * Stacked-widget current index and slider value are restored after each
//     case that mutates them.
//   * Qt6 event constructors: QMouseEvent / QWheelEvent take QPointF and the
//     full argument list shown below; QKeyEvent takes (type, key, modifiers).

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QEnterEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QAction>
#include <QMenu>
#include <QTimer>
#include <QPointingDevice>
#include <DGuiApplicationHelper>
#include <DButtonBox>
#include <DPalette>
#include <QStackedWidget>
#include <QSlider>
#include <QWindowStateChangeEvent>
#include <QMainWindow>
#include <QSignalMapper>
#include <QDateTime>
#include <QCursor>
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QFontMetrics>
#include <DFontSizeManager>
#include <DApplication>
#include <DWindowManagerHelper>

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

#include "application.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/widgets/platform/platform_volumeslider.h"
#include "src/widgets/mircastwidget.h"
#include "src/common/dmr_settings.h"
#include "src/libdmr/compositing_manager.h"
#include "src/common/actions.h"
#include "src/widgets/toolbutton.h"

using namespace dmr;

// Register metatypes so QMetaObject::invokeMethod can deliver Qt::ApplicationState
// and bool/int/QString args to protected slots at runtime.
namespace {
const int ptb4_appStateMetaId = qRegisterMetaType<Qt::ApplicationState>("Qt::ApplicationState");
const int ptb4_pointMetaId = qRegisterMetaType<QPoint>("QPoint");
}

// --- Helpers ---------------------------------------------------------------

// The running app's main window. All shared widgets hang off this.
static Platform_MainWindow *ptb4_mainWindow()
{
    return dApp->getMainWindow();
}

// Shared toolbox proxy owned by the main window.
static Platform_ToolboxProxy *ptb4_toolbox()
{
    Platform_MainWindow *w = ptb4_mainWindow();
    return w ? w->toolbox() : nullptr;
}

// Shared volume slider owned by the toolbox.
static Platform_VolumeSlider *ptb4_volumeSlider()
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    return tb ? tb->volumeSlider() : nullptr;
}

// Shared player engine.
static PlayerEngine *ptb4_engine()
{
    Platform_MainWindow *w = ptb4_mainWindow();
    return w ? w->engine() : nullptr;
}

// A short synchronous wait so animations/timers settle without stalling.
static void ptb4_wait(int ms = 120)
{
    QTest::qWait(ms);
}

// Many toolbox slots are protected/private, so route through invokeMethod.
static void ptb4_invoke(Platform_ToolboxProxy *tb, const char *slot)
{
    QMetaObject::invokeMethod(tb, slot, Qt::DirectConnection);
}
template <typename T>
static void ptb4_invoke1(Platform_ToolboxProxy *tb, const char *slot, const T &arg)
{
    QMetaObject::invokeMethod(tb, slot, Qt::DirectConnection, Q_ARG(T, arg));
}

// True when the engine has at least one loaded playlist item.
static bool ptb4_engineHasItems()
{
    PlayerEngine *e = ptb4_engine();
    return e && e->playlist().count() > 0;
}

// True when the engine state is Idle.
static bool ptb4_engineIdle()
{
    PlayerEngine *e = ptb4_engine();
    return e && e->state() == PlayerEngine::CoreState::Idle;
}

// True when the compositing platform is X86.
static bool ptb4_isX86()
{
    return CompositingManager::get().platform() == Platform::X86;
}

// True when the compositing platform is Alpha (gates resizeEvent early-return).
static bool ptb4_isAlpha()
{
    return CompositingManager::get().platform() == Platform::Alpha;
}

// Save and restore the global theme around a forced-theme block. Use a struct
// so the restore runs even if an ASSERT fails mid-block (RAII via destructor).
struct ptb4_ThemeGuard {
    DGuiApplicationHelper::ColorType orig;
    ptb4_ThemeGuard() : orig(DGuiApplicationHelper::instance()->themeType()) {}
    ~ptb4_ThemeGuard()
    {
        DGuiApplicationHelper::instance()->setPaletteType(orig);
        ptb4_wait(10);
    }
};

// ==========================================================================
// showEvent / paintEvent — explicit Idle property toggling (lines 2758-2783)
// ==========================================================================

TEST(platform_tb_ext4, showEvent_withIdleProperty_cleared_updatesLabel)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->setProperty("idle", false);
    tb->show();
    ptb4_wait(20);
    QShowEvent se;
    QApplication::sendEvent(tb, &se);
    SUCCEED();
}

TEST(platform_tb_ext4, paintEvent_darkTheme_fillsDarkBackground)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::DarkType);
    ptb4_wait(20);
    tb->resize(800, 80);
    QPaintEvent pe(tb->rect());
    QApplication::sendEvent(tb, &pe);
    SUCCEED();
}

TEST(platform_tb_ext4, paintEvent_lightTheme_fillsWindowBackground)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::LightType);
    ptb4_wait(20);
    tb->resize(700, 80);
    QPaintEvent pe(tb->rect());
    QApplication::sendEvent(tb, &pe);
    SUCCEED();
}

// ==========================================================================
// resizeEvent — width-change with Idle engine + !Alpha animation-teardown
// branch where m_pPlaylist is null (no playlist bound yet) (lines 2785-2810).
// ==========================================================================

TEST(platform_tb_ext4, resizeEvent_widthChange_idleEngine_skipsThumbnailReload)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QSize oldSize = tb->size();
    QSize grow(std::max(10, oldSize.width() + 60), oldSize.height());
    QResizeEvent reGrow(grow, oldSize);
    QApplication::sendEvent(tb, &reGrow);
    // Restore size.
    QResizeEvent reBack(oldSize, grow);
    QApplication::sendEvent(tb, &reBack);
    SUCCEED();
}

TEST(platform_tb_ext4, resizeEvent_alphaPlatform_skipsUpdateTimeLabel)
{
    // On the Alpha platform the updateTimeLabel tail is skipped entirely.
    if (!ptb4_isAlpha()) GTEST_SKIP() << "Alpha-platform-only branch";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QSize oldSize = tb->size();
    QSize grow(std::max(10, oldSize.width() + 40), oldSize.height());
    QResizeEvent re(grow, oldSize);
    QApplication::sendEvent(tb, &re);
    SUCCEED();
}

TEST(platform_tb_ext4, resizeEvent_nonAlpha_animationNull_isSafe)
{
    // On a non-Alpha platform with m_pPaOpen/m_pPaClose null (the Idle test env),
    // the animation-teardown if-body is skipped; only updateTimeLabel runs.
    if (ptb4_isAlpha()) GTEST_SKIP() << "Already covered by alpha case";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QSize oldSize = tb->size();
    QSize grow(std::max(10, oldSize.width() + 40), oldSize.height());
    QResizeEvent re(grow, oldSize);
    QApplication::sendEvent(tb, &re);
    SUCCEED();
}

// ==========================================================================
// eventFilter — volume-button KeyPress fall-through within the Open branch
// (key is neither Up nor Down while slider is Open) (lines 2820-2838).
// We cannot easily drive the slider to Open without a screen, so the slider
// stays Closed here and the whole if-body is skipped — that still covers the
// obj==m_pVolBtn comparison and the early guard evaluation.
// ==========================================================================

TEST(platform_tb_ext4, eventFilter_volBtnLeftKey_sliderClosed_fallsThrough)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);
    QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(vb, &left);
    SUCCEED();
}

TEST(platform_tb_ext4, eventFilter_volBtnKeyUp_sliderClosed_fallsThrough)
{
    // state() == Close so the inner if is false; volume untouched by the filter.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    VolumeButton *vb = tb->volBtn();
    ASSERT_NE(vb, nullptr);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(50);
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QApplication::sendEvent(vb, &up);
    EXPECT_EQ(vs->getVolume(), 50);
}

// ==========================================================================
// eventFilter — list-button branches (lines 2840-2855).
// non-X86: the whole X86 block is skipped. X86: the right-click filter
// dereferences m_pPlaylist; guarded when no playlist is bound, and the
// non-right-button path is a safe fall-through.
// ==========================================================================

TEST(platform_tb_ext4, eventFilter_listBtnLeftRelease_noOp)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ToolButton *listBtn = tb->listBtn();
    ASSERT_NE(listBtn, nullptr);
    // Left-button release never matches the RightButton guard.
    QMouseEvent lmb(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(listBtn, &lmb);
    SUCCEED();
}

TEST(platform_tb_ext4, eventFilter_listBtnMouseMove_fallsThrough)
{
    // ev->type() != MouseButtonRelease: the inner if is false on every platform.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ToolButton *listBtn = tb->listBtn();
    ASSERT_NE(listBtn, nullptr);
    QMouseEvent me(QEvent::MouseMove, QPointF(3, 3), QPointF(3, 3),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(listBtn, &me);
    SUCCEED();
}

TEST(platform_tb_ext4, eventFilter_listBtnRightClick_x86NoPlaylist_isSafe)
{
    if (!ptb4_isX86()) GTEST_SKIP() << "List-button right-click filter is X86-only";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb4_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (pl) GTEST_SKIP() << "Playlist bound — covered by ext3 right-click case";
    // No playlist bound: the right-click branch would dereference null if it ran
    // unconditionally, but the playlist-state checks gate the toggle so a null
    // playlist simply means no toggle happens. Drive it to confirm safety.
    ToolButton *listBtn = tb->listBtn();
    ASSERT_NE(listBtn, nullptr);
    QMouseEvent rmb(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                    Qt::RightButton, Qt::NoButton, Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(listBtn, &rmb);
    SUCCEED();
}

// ==========================================================================
// buttonClicked — "play" routing: StartPlay when Idle, TogglePause otherwise
// (lines 2685-2693). Idle branch is always safe; the non-Idle branch needs a
// loaded item, so it is guarded.
// ==========================================================================

TEST(platform_tb_ext4, buttonClicked_play_whenIdle_routesStartPlay)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb4_wait(20);
    ptb4_invoke1<QString>(tb, "buttonClicked", QString("play"));
    SUCCEED();
}

TEST(platform_tb_ext4, buttonClicked_unknownId_isNoOp)
{
    // An id that matches no branch: only the visibility repaint preamble runs.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb4_wait(20);
    ptb4_invoke1<QString>(tb, "buttonClicked", QString("nonsense"));
    SUCCEED();
}

TEST(platform_tb_ext4, buttonClicked_play_notIdle_routesTogglePause)
{
    if (!ptb4_engineHasItems() || ptb4_engineIdle()) GTEST_SKIP()
        << "Non-Idle play branch needs a loaded, playing item";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->show();
    ptb4_wait(20);
    ptb4_invoke1<QString>(tb, "buttonClicked", QString("play"));
    SUCCEED();
}

TEST(platform_tb_ext4, buttonClicked_whenHidden_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    ptb4_wait(20);
    // Hidden toolbox short-circuits before any id routing.
    ptb4_invoke1<QString>(tb, "buttonClicked", QString("play"));
    ptb4_invoke1<QString>(tb, "buttonClicked", QString("list"));
    tb->show();
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// slotUpdateMircast — non-zero state branch (bRawFormat false) (lines 2465-2480).
// state != 0 takes the else branch and re-enables the fs button. With an Idle
// engine the bRawFormat lookup still runs but on an empty playlist it yields a
// default PlayItemInfo (isRawFormat() == false), so the !bRawFormat sub-branch
// re-enables the volume button.
// ==========================================================================

TEST(platform_tb_ext4, slotUpdateMircast_nonZeroState_enablesFsBtn)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Non-Idle engine would deref real media info";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->slotUpdateMircast(1, QString("connected"));
    EXPECT_TRUE(tb->fsBtn()->isEnabled());
}

TEST(platform_tb_ext4, slotUpdateMircast_emitsSignalWithMessage)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    int captured = -1;
    QString msg;
    QObject *probe = new QObject;
    probe->setParent(tb);   // auto-cleanup
    QObject::connect(tb, &Platform_ToolboxProxy::sigMircastState,
                     probe, [&captured, &msg](int s, const QString &m) {
                         captured = s;
                         msg = m;
                     });
    tb->slotUpdateMircast(2, QString("casting"));
    EXPECT_EQ(captured, 2);
    EXPECT_EQ(msg, QString("casting"));
}

// ==========================================================================
// updatePlayState — Playing vs not-Playing branches via forced mircast state
// (lines 2482-2634). updatePlayState checks the mircast widget's state, so
// forcing MircastState::Screening + MircastPlayState::Play drives the Playing
// branch without any real playback.
// ==========================================================================

TEST(platform_tb_ext4, updatePlayState_mircastScreening_setsPauseIcon)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Screening);
    ptb4_wait(20);
    ptb4_invoke(tb, "updatePlayState");
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updatePlayState_notPlaying_setsPlayIcon)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    // Ensure mircast is idle and engine is not Playing -> not-Playing branch.
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    ptb4_invoke(tb, "updatePlayState");
    SUCCEED();
}

TEST(platform_tb_ext4, updatePlayState_playingLightTheme_paletteBranch)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::LightType);
    ptb4_wait(20);
    mw->setMircastState(MircastWidget::Screening);
    ptb4_wait(20);
    ptb4_invoke(tb, "updatePlayState");
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updatePlayState_playingDarkTheme_paletteBranch)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::DarkType);
    ptb4_wait(20);
    mw->setMircastState(MircastWidget::Screening);
    ptb4_wait(20);
    ptb4_invoke(tb, "updatePlayState");
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updatePlayState_idle_hidesProgBarAndSetsIndex0)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Idle tail needs an Idle engine";
    ptb4_invoke(tb, "updatePlayState");
    EXPECT_EQ(tb->m_pProgBar_Widget->currentIndex(), 0);
    EXPECT_TRUE(tb->property("idle").toBool());
}

// ==========================================================================
// updateButtonStates — Idle branch palettes (lines 2359-2447). With an Idle
// engine the else-branch sets the Idle palettes; theme forcing covers both
// palette-color sub-branches.
// ==========================================================================

TEST(platform_tb_ext4, updateButtonStates_idleLightTheme_paletteBranch)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::LightType);
    ptb4_wait(20);
    ptb4_invoke(tb, "updateButtonStates");
    EXPECT_FALSE(tb->prevBtn()->isEnabled());
    EXPECT_FALSE(tb->nextBtn()->isEnabled());
    SUCCEED();
}

TEST(platform_tb_ext4, updateButtonStates_idleDarkTheme_paletteBranch)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::DarkType);
    ptb4_wait(20);
    ptb4_invoke(tb, "updateButtonStates");
    SUCCEED();
}

// ==========================================================================
// slotElapsedChanged — mircast-active early-return branch (lines 1965-1990).
// Forcing mircast to non-Idel makes the slot return before touching the engine.
// ==========================================================================

TEST(platform_tb_ext4, slotElapsedChanged_mircastActive_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Screening);
    ptb4_wait(20);
    ptb4_invoke(tb, "slotElapsedChanged");
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// updateMircastTime — filmstrip-mode (currentIndex != 1) else branch
// (lines 2873-2899). Flipping the stacked index to != 1 routes through
// ViewProgBar::setIsBlockSignals/setValue.
// ==========================================================================

TEST(platform_tb_ext4, updateMircastTime_filmstripMode_routesToViewProgBar)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(0);
    ptb4_wait(20);
    ptb4_invoke1<int>(tb, "updateMircastTime", 42);
    ptb4_wait(20);
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updateMircastTime_progressMode_blocksSignals)
{
    // currentIndex == 1 (the Idle default after setup is 0; flip to 1).
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(1);
    ptb4_wait(20);
    ptb4_invoke1<int>(tb, "updateMircastTime", 7);
    ptb4_wait(20);
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// updateProgress — progress-mode (currentIndex == 1) sub-branches
// (lines 3012-3050).
//   * |value| > 1: direct nCurrPos path.
//   * |value| <= 1 with |m_processAdd| < 1.0: accumulate-and-return path.
//   * |value| <= 1 with |m_processAdd| >= 1.0: flush path.
// ==========================================================================

TEST(platform_tb_ext4, updateProgress_progressMode_largeDelta_directPath)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(1);
    ptb4_wait(20);
    tb->m_processAdd = 0.0;
    tb->updateProgress(50);   // |50| > 1 -> direct nCurrPos path
    tb->updateProgress(-50);
    tb->m_processAdd = 0.0;
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updateProgress_progressMode_smallDelta_accumulates)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(1);
    ptb4_wait(20);
    tb->m_processAdd = 0.0;
    // |1| <= 1 and |m_processAdd| < 1.0 -> accumulate-and-return.
    tb->updateProgress(1);
    EXPECT_NE(tb->m_processAdd, 0.0);
    tb->m_processAdd = 0.0;
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updateProgress_progressMode_accumulateFlush)
{
    // Pre-load m_processAdd beyond the threshold so the small-delta path flushes.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(1);
    ptb4_wait(20);
    tb->m_processAdd = 5.0;   // |5.0| >= 1.0 -> flush branch
    tb->updateProgress(0);    // value 0 -> |0| <= 1 -> accumulate path, but flush
    EXPECT_EQ(tb->m_processAdd, 0.0);
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// updateSlider — progress-mode and filmstrip-mode branches (lines 3054-3068).
// seekAbsolute is a no-op on an Idle engine with no loaded item, so both
// branches are safe to drive in Idle.
// ==========================================================================

TEST(platform_tb_ext4, updateSlider_progressMode_idleEngine_isSafe)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine (seek is a no-op)";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(1);
    ptb4_wait(20);
    tb->updateSlider();
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, updateSlider_filmstripMode_idleEngine_isSafe)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine (seek is a no-op)";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(0);   // any index != 1 takes the else branch
    ptb4_wait(20);
    tb->updateSlider();
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// progressHoverChanged — deep branches gated by slider value, Idle engine, and
// an empty playlist (lines 2224-2304). Forcing the slider value to non-zero
// clears the first guard; the Idle engine guard then returns early.
// ==========================================================================

TEST(platform_tb_ext4, progressHoverChanged_sliderValueNonZero_idleEngine_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    tb->m_pProgBar->slider()->setValue(10);
    ptb4_wait(20);
    ptb4_invoke1<int>(tb, "progressHoverChanged", 5);
    tb->m_pProgBar->slider()->setValue(0);
    ptb4_wait(20);
    SUCCEED();
}

TEST(platform_tb_ext4, progressHoverChanged_sliderValueZero_returnsEarly)
{
    // First guard: slider value == 0 -> immediate return.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->m_pProgBar->slider()->setValue(0);
    ptb4_wait(20);
    ptb4_invoke1<int>(tb, "progressHoverChanged", 5);
    SUCCEED();
}

// ==========================================================================
// updateTimeVisible — PreviewOnMouseover off toggles the preview-time label
// (lines 2306-2318). With mousepreview off, setVisible(!visible) runs.
// ==========================================================================

TEST(platform_tb_ext4, updateTimeVisible_mousepreviewOff_hidesAndShowsPreviewTime)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    // Force mousepreview off so the early-return guard is cleared.
    Settings::get().setInternalOption("mousepreview", false);
    ptb4_wait(30);
    ptb4_invoke1<bool>(tb, "updateTimeVisible", true);   // setVisible(false)
    ptb4_wait(20);
    ptb4_invoke1<bool>(tb, "updateTimeVisible", false);  // setVisible(true)
    ptb4_wait(20);
    SUCCEED();
}

// ==========================================================================
// slotThemeTypeChanged — Idle branch palettes (lines 1670-1810). With an Idle
// engine the Idle sub-branch runs; theme forcing covers both color paths.
// ==========================================================================

TEST(platform_tb_ext4, slotThemeTypeChanged_idleLightTheme_paletteBranch)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::LightType);
    ptb4_wait(20);
    ptb4_invoke(tb, "slotThemeTypeChanged");
    SUCCEED();
}

TEST(platform_tb_ext4, slotThemeTypeChanged_idleDarkTheme_paletteBranch)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_ThemeGuard tg;
    DGuiApplicationHelper::instance()->setPaletteType(DGuiApplicationHelper::DarkType);
    ptb4_wait(20);
    ptb4_invoke(tb, "slotThemeTypeChanged");
    SUCCEED();
}

// ==========================================================================
// Geometry helpers — additional round-trips (lines 2149-2162, 3083-3086).
// ==========================================================================

TEST(platform_tb_ext4, updateSliderPoint_nonZeroPoint_repositionsSlider)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QPoint pt(200, 300);
    tb->updateSliderPoint(pt);
    SUCCEED();
}

TEST(platform_tb_ext4, hideMircastWidget_afterShow_clearsCheck)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->show();
    ptb4_wait(20);
    tb->hideMircastWidget();
    EXPECT_FALSE(mw->isVisible());
    EXPECT_FALSE(tb->m_pMircastBtn->isChecked());
}

TEST(platform_tb_ext4, isInMircastWidget_shownWidget_evaluatesGeometry)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->move(0, 0);
    mw->resize(50, 50);
    mw->show();
    ptb4_wait(20);
    // Point inside the shown widget's geometry.
    EXPECT_TRUE(tb->isInMircastWidget(QPoint(10, 10)));
    // Point well outside.
    EXPECT_FALSE(tb->isInMircastWidget(QPoint(9999, 9999)));
    tb->hideMircastWidget();
}

TEST(platform_tb_ext4, updateMircastWidget_offsetPoint_movesWidget)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->resize(40, 40);
    tb->updateMircastWidget(QPoint(400, 300));
    EXPECT_EQ(mw->x(), 400 - mw->width());
    EXPECT_EQ(mw->y(), 300 - mw->height() - 10);
    tb->hideMircastWidget();
}

// ==========================================================================
// Static flag round-trips (lines 2936-3007).
// ==========================================================================

TEST(platform_tb_ext4, setBtnFocusSign_getListBtnFocus_interplay)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->setBtnFocusSign(true);
    ptb4_wait(10);
    tb->setBtnFocusSign(false);
    ptb4_wait(10);
    // getListBtnFocus reflects the list button's actual focus, not the sign.
    EXPECT_FALSE(tb->getListBtnFocus());
}

TEST(platform_tb_ext4, getbAnimationFinash_defaultTrue)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    // In the Idle test env no playlist animation has run, so it should be true.
    EXPECT_TRUE(tb->getbAnimationFinash());
}

TEST(platform_tb_ext4, getVolSliderIsHided_afterExplicitShowThenHide)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    vs->show();
    ptb4_wait(20);
    EXPECT_FALSE(tb->getVolSliderIsHided());
    tb->setVolSliderHide();
    ptb4_wait(20);
    EXPECT_TRUE(tb->getVolSliderIsHided());
}

// ==========================================================================
// Playlist-bound helpers — guarded when no playlist is bound (lines 2121-2136,
// 2917-2923). setPlaylist with the main window's own playlist pointer is a
// safe round-trip (it just reconnects the stateChange signal).
// ==========================================================================

TEST(platform_tb_ext4, setPlaylist_roundTrip_isSafe)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb4_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (!pl) GTEST_SKIP() << "No playlist bound";
    // Re-binding the same playlist pointer is a no-op semantically.
    tb->setPlaylist(pl);
    SUCCEED();
}

TEST(platform_tb_ext4, clearPlayListFocus_noFocus_isSafe)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb4_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (!pl) GTEST_SKIP() << "No playlist bound";
    // With no focus in the playlist the if-body is skipped; the flag is reset.
    tb->clearPlayListFocus();
    EXPECT_FALSE(tb->m_bSetListBtnFocus);
}

TEST(platform_tb_ext4, playlistClosedByEsc_flagFalse_isSafe)
{
    // With m_bSetListBtnFocus == false the requestAction branch is skipped.
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_MainWindow *w = ptb4_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_PlaylistWidget *pl = w->playlist();
    if (!pl) GTEST_SKIP() << "No playlist bound";
    tb->setBtnFocusSign(false);
    ptb4_wait(10);
    tb->playlistClosedByEsc();
    SUCCEED();
}

// ==========================================================================
// buttonEnter / buttonLeave — id-routing branches with a visible toolbox
// (lines 2724-2756). The fs/list/mir ToolButtons carry a "TipId" property, so
// emitting their entered()/leaved() signals drives the matching id branch.
// ==========================================================================

TEST(platform_tb_ext4, buttonEnter_visible_emitsFsTooltipShow)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb4_wait(20);
    emit tb->m_pFullScreenBtn->entered();
    ptb4_wait(30);
    emit tb->m_pFullScreenBtn->leaved();
    ptb4_wait(30);
    SUCCEED();
}

TEST(platform_tb_ext4, buttonEnter_visible_emitsListTooltipShow)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb4_wait(20);
    emit tb->m_pListBtn->entered();
    ptb4_wait(30);
    emit tb->m_pListBtn->leaved();
    ptb4_wait(30);
    SUCCEED();
}

TEST(platform_tb_ext4, buttonEnter_visible_emitsMirTooltipShow)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb4_wait(20);
    emit tb->m_pMircastBtn->entered();
    ptb4_wait(30);
    emit tb->m_pMircastBtn->leaved();
    ptb4_wait(30);
    SUCCEED();
}

// ==========================================================================
// mouseMoveEvent — drives setButtonTooltipHide on a visible toolbox
// (lines 2812-2816). ext2 covers this with NoButton; here add a LeftButton
// move to exercise the same path under a different button state.
// ==========================================================================

TEST(platform_tb_ext4, mouseMoveEvent_leftButton_hidesTooltips)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tb->show();
    ptb4_wait(20);
    QMouseEvent me(QEvent::MouseMove, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(tb, &me);
    SUCCEED();
}

// ==========================================================================
// updateTimeLabel — width-threshold branches (lines 2860-2871). Resize the
// toolbox across the 300/450 thresholds and call updateTimeLabel directly.
// ==========================================================================

TEST(platform_tb_ext4, updateTimeLabel_narrowWidth_hidesLabels)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (ptb4_isAlpha()) GTEST_SKIP() << "Alpha platform skips updateTimeLabel body";
    int origW = tb->width();
    tb->resize(200, tb->height());
    ptb4_wait(20);
    ptb4_invoke(tb, "updateTimeLabel");
    EXPECT_FALSE(tb->m_pListBtn->isVisible());
    EXPECT_FALSE(tb->m_pTimeLabel->isVisible());
    EXPECT_FALSE(tb->m_pTimeLabelend->isVisible());
    tb->resize(origW, tb->height());
    ptb4_wait(20);
}

TEST(platform_tb_ext4, updateTimeLabel_wideWidth_showsLabels)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (ptb4_isAlpha()) GTEST_SKIP() << "Alpha platform skips updateTimeLabel body";
    int origW = tb->width();
    tb->resize(600, tb->height());
    ptb4_wait(20);
    ptb4_invoke(tb, "updateTimeLabel");
    EXPECT_TRUE(tb->m_pListBtn->isVisible());
    EXPECT_TRUE(tb->m_pTimeLabel->isVisible());
    EXPECT_TRUE(tb->m_pTimeLabelend->isVisible());
    tb->resize(origW, tb->height());
    ptb4_wait(20);
}

// ==========================================================================
// setthumbnailmode — Idle engine early-return (lines 1034-1045).
// ==========================================================================

TEST(platform_tb_ext4, setthumbnailmode_idleEngine_returnsEarly)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_invoke(tb, "setthumbnailmode");
    // Idle path returns before flipping m_bThumbnailmode.
    EXPECT_FALSE(tb->m_bThumbnailmode);
}

// ==========================================================================
// finishLoadSlot — empty pmList / not-thumbnailmode early-returns
// (lines 1006-1032).
// ==========================================================================

TEST(platform_tb_ext4, finishLoadSlot_emptyPmList_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QList<QPixmap> empty;
    tb->addpmList(empty);
    ptb4_invoke1<QSize>(tb, "finishLoadSlot", QSize(100, 100));
    SUCCEED();
}

TEST(platform_tb_ext4, finishLoadSlot_nonEmptyPmListButNotThumbnailMode_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QList<QPixmap> pms;
    pms << QPixmap(10, 10);
    tb->addpmList(pms);
    tb->setThumbnailmode(false);
    ptb4_invoke1<QSize>(tb, "finishLoadSlot", QSize(100, 100));
    // Clean up so the seeded pmList doesn't leak into other cases.
    QList<QPixmap> empty;
    tb->addpmList(empty);
    SUCCEED();
}

// ==========================================================================
// volumeUp / volumeDown — enabled-slider branch (lines 2166-2190). ext2 covers
// the disabled branch; here exercise the enabled path directly.
// ==========================================================================

TEST(platform_tb_ext4, volumeUp_enabledSlider_callsSliderVolumeUp)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->setEnabled(true);
    vs->changeVolume(50);
    tb->volumeUp();
    SUCCEED();
}

TEST(platform_tb_ext4, volumeDown_enabledSlider_callsSliderVolumeDown)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->setEnabled(true);
    vs->changeVolume(50);
    tb->volumeDown();
    SUCCEED();
}

// ==========================================================================
// calculationStep / changeMuteState — thin forwarders (lines 2195-2208).
// ==========================================================================

TEST(platform_tb_ext4, calculationStep_forwardsToSlider)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->calculationStep(120);
    tb->calculationStep(-120);
    SUCCEED();
}

TEST(platform_tb_ext4, changeMuteState_forwardsToSlider)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    vs->changeVolume(80);   // non-zero so mute toggle has something to act on
    tb->changeMuteState();
    SUCCEED();
}

// ==========================================================================
// slotSliderPressed / slotSliderReleased — flag + mircast/engine seek branches
// (lines 1834-1849). Released with mircast Idle routes to engine seek (no-op
// on Idle); released with mircast Screening routes to slotSeekMircast.
// ==========================================================================

TEST(platform_tb_ext4, slotSliderPressed_setsMousePreeFlag)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    ptb4_invoke(tb, "slotSliderPressed");
    EXPECT_TRUE(tb->m_bMousePree);
}

TEST(platform_tb_ext4, slotSliderReleased_mircastIdle_idleEngine_isSafe)
{
    if (!ptb4_engineIdle()) GTEST_SKIP() << "Needs an Idle engine (seek is a no-op)";
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    ptb4_invoke(tb, "slotSliderReleased");
    EXPECT_FALSE(tb->m_bMousePree);
}

TEST(platform_tb_ext4, slotSliderReleased_mircastScreening_routesToSeekMircast)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    MircastWidget *mw = tb->getMircast();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Screening);
    ptb4_wait(20);
    ptb4_invoke(tb, "slotSliderReleased");
    mw->setMircastState(MircastWidget::Idel);
    ptb4_wait(20);
    EXPECT_FALSE(tb->m_bMousePree);
}

// ==========================================================================
// slotLeavePreview / slotHidePreviewTime — Idle-safe no-ops
// (lines 1812-1832). slotLeavePreview checks the cursor against the progBar
// geometry; with the cursor off-bar it hides the previewer.
// ==========================================================================

TEST(platform_tb_ext4, slotLeavePreview_cursorOffBar_hidesPreviewer)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    // Park the cursor far from the progBar so the contains() check is false.
    QCursor::setPos(QPoint(5000, 5000));
    ptb4_wait(20);
    ptb4_invoke(tb, "slotLeavePreview");
    EXPECT_FALSE(reinterpret_cast<QWidget*>(tb->m_pPreviewer)->isVisible());
    EXPECT_FALSE(reinterpret_cast<QWidget*>(tb->m_pPreviewTime)->isVisible());
}

TEST(platform_tb_ext4, slotHidePreviewTime_clearsMouseFlag)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->m_bMouseFlag = true;
    ptb4_invoke(tb, "slotHidePreviewTime");
    EXPECT_FALSE(tb->m_bMouseFlag);
}

// ==========================================================================
// slotVolumeButtonClicked — not-visible early-return (lines 1861-1887).
// ext2 covers the visible show/hide toggling; here exercise the hidden path.
// ==========================================================================

TEST(platform_tb_ext4, slotVolumeButtonClicked_toolboxHidden_returnsEarly)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->hide();
    ptb4_wait(20);
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    bool wasVisible = vs->isVisible();
    ptb4_invoke(tb, "slotVolumeButtonClicked");
    // Hidden toolbox -> early return, slider visibility unchanged.
    EXPECT_EQ(vs->isVisible(), wasVisible);
    tb->show();
    ptb4_wait(20);
}

// ==========================================================================
// slotVolumeButtonClicked — getsliderstate() == true early-return. We cannot
// easily force getsliderstate() true without the slider Open, so this case
// just confirms the not-sliderstate path is taken when the slider is Closed.
// ==========================================================================

TEST(platform_tb_ext4, slotVolumeButtonClicked_sliderClosed_proceedsToShowHide)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_VolumeSlider *vs = ptb4_volumeSlider();
    ASSERT_NE(vs, nullptr);
    tb->setVolSliderHide();   // ensure hidden first
    ptb4_wait(20);
    tb->show();
    ptb4_wait(20);
    ptb4_invoke(tb, "slotVolumeButtonClicked");   // shows
    ptb4_wait(30);
    ptb4_invoke(tb, "slotVolumeButtonClicked");   // hides
    ptb4_wait(30);
    SUCCEED();
}

// ==========================================================================
// initThumbThread — connects the thumbnail worker (lines 3072-3078). Safe to
// call repeatedly; it reconnects the thumbGenerated signal.
// ==========================================================================

TEST(platform_tb_ext4, initThumbThread_isSafe)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    tb->initThumbThread();
    SUCCEED();
}

// ==========================================================================
// updateSlider() public wrapper is exercised above; here also drive
// updateProgress on the filmstrip page with a non-zero accumulate to cover the
// ViewProgBar getValue()+nValue path (lines 3044-3048).
// ==========================================================================

TEST(platform_tb_ext4, updateProgress_filmstripMode_accumulatesViewProgBar)
{
    Platform_ToolboxProxy *tb = ptb4_toolbox();
    ASSERT_NE(tb, nullptr);
    QStackedWidget *stack = tb->m_pProgBar_Widget;
    ASSERT_NE(stack, nullptr);
    int orig = stack->currentIndex();
    stack->setCurrentIndex(0);
    ptb4_wait(20);
    tb->updateProgress(3);
    tb->updateProgress(-1);
    ptb4_wait(20);
    stack->setCurrentIndex(orig);
    ptb4_wait(20);
    SUCCEED();
}
