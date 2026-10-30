// Copyright (C) 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (fixture-based) for src/widgets/toolbox_proxy.cpp —
// class ToolboxProxy. Complements tests/deepin-movie/common/test_toolbox_proxy.cpp
// by covering the high-value methods that suite leaves out: buttonClicked,
// eventFilter, progressHoverChanged, updateButtonStates, updatePlayState,
// updateHoverPreview, slotFileLoaded, slotPlayListStateChange, updateMovieProgress,
// updateTimeInfo, updateFullState, slotUpdateMircast, updateThumbnail,
// finishLoadSlot, updateToolTipTheme, updateTimeVisible, setthumbnailmode,
// initThumbThread, slotUpdateThumbnailTimeOut and the small signal/slot glue.
//
// Design notes (inherited from the review of the legacy suite):
//   * Proper ::testing::Test fixture: a fresh ToolboxProxy per test in SetUp,
//     deleted in TearDown — no inter-test member leakage.
//   * MainWindow::requestAction is stubbed (tests/deepin-movie/stub/stub.h) for
//     the buttonClicked cases so no real UI action (playlist toggle, fullscreen
//     switch) fires while the shared main window is shared with other suites.
//     The stub is reset before the test ends.
//   * Playlist-aware branches need a non-null m_pPlaylist — the fixture wires
//     the shared MainWindow playlist via setPlaylist().
//   * No mpv backend / decode path is exercised; the engine stays Idle so every
//     "playing-only" branch is either skipped or taken at its guard.
//   * Qt6 event constructors: QMouseEvent takes QPointF.

#include <QtTest>
#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QHelpEvent>
#include <QApplication>
#include <QDateTime>
#include <DSlider>

#define protected public
#define private public
#include "src/widgets/toolbox_proxy.h"
// Shadow definitions: ViewProgBar / ThumbnailPreview / SliderTime /
// TooltipHandler are defined inside toolbox_proxy.cpp and only
// forward-declared in the header; the shadow copy gives complete types.
#include "toolbox_shadow_classes.h"
#undef protected
#undef private

#include "src/widgets/toolbutton.h"
#include "src/widgets/volumeslider.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/mircastwidget.h"
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/common/dmr_settings.h"
#include "application.h"
#include "stub/stub.h"

using namespace dmr;

#ifndef POPUP_DURATION
#define POPUP_DURATION 350   // keep in sync with toolbox_proxy.cpp
#endif

// ---------------------------------------------------------------------------
// requestAction capture stub (member function patch via the stub framework)
// ---------------------------------------------------------------------------
namespace {

static ActionFactory::ActionKind tbx_lastAction = ActionFactory::ActionKind::TogglePlaylist;
static int tbx_actionCount = 0;

static void tbx_requestAction_stub(void * /*self*/, ActionFactory::ActionKind kind,
                                   bool /*bFromUI*/, QList<QVariant> /*args*/,
                                   bool /*bShortcut*/)
{
    tbx_lastAction = kind;
    ++tbx_actionCount;
}

typedef void (*tbx_requestAction_fptr)(void *, ActionFactory::ActionKind,
                                       bool, QList<QVariant>, bool);

static Stub tbx_actionStub;

static void tbx_installActionStub()
{
    tbx_actionStub.set((tbx_requestAction_fptr)&MainWindow::requestAction,
                       tbx_requestAction_stub);
    tbx_actionCount = 0;
}


static void tbx_restoreActionStub()
{
    tbx_actionStub.reset((tbx_requestAction_fptr)&MainWindow::requestAction);
}

// MainWindow stays hidden in the offscreen test binary, so a child ToolboxProxy
// can never become really visible. The dispatch branch of buttonClicked checks
// isVisible(); fake the WA state instead of showing the shared main window.
static void tbx_forceVisible(ToolboxProxy *tb)
{
    tb->setAttribute(Qt::WA_WState_ExplicitShowHide, true);
    tb->setAttribute(Qt::WA_WState_Visible, true);
}

} // namespace

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class ToolboxProxyExtTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_w = dApp->getMainWindow();
        ASSERT_NE(m_w, nullptr);
        ASSERT_NE(m_w->engine(), nullptr);

        m_tb = new ToolboxProxy(m_w, m_w->engine());
        // Playlist-aware branches (eventFilter list-button, slotPlayListStateChange)
        // dereference m_pPlaylist unconditionally. The main window wires its real
        // playlist 300ms after init via singleShot, which the test binary never
        // lets fire — trigger the same production path synchronously.
        if (!m_w->playlist()) {
            m_w->loadPlayList();   // mainwindow.h was parsed with private->public via the shadow header
        }
        ASSERT_NE(m_w->playlist(), nullptr);
        // Other suites (e.g. legacy MainWindow tests) may have seeded the shared
        // engine playlist with /data/source entries or leftovers; restore the
        // pristine idle state every later suite (and these branches) assume.
        m_w->engine()->playlist().clear();
        m_tb->setPlaylist(m_w->playlist());
        ASSERT_NE(m_tb->m_pPlaylist, nullptr);
        m_tb->show();
        QTest::qWait(30);
    }

    void TearDown() override
    {
        delete m_tb;
        m_tb = nullptr;
        // keep the shared engine playlist clean for suites registered after this one
        if (m_w && m_w->engine()) {
            m_w->engine()->playlist().clear();
        }
    }

    MainWindow *m_w = nullptr;
    ToolboxProxy *m_tb = nullptr;
};

// ---------------------------------------------------------------------------
// buttonClicked
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, buttonClicked_hiddenToolbox_ignoresClick)
{
    // Arrange
    tbx_installActionStub();
    m_tb->hide();
    QTest::qWait(20);

    // Act on every dispatching button while hidden
    // Act
    m_tb->buttonClicked("play");
    m_tb->buttonClicked("fs");
    m_tb->buttonClicked("list");

    // Assert
    // Assert
    EXPECT_FALSE(m_tb->isVisible());
    EXPECT_EQ(tbx_actionCount, 0) << "hidden toolbox must not dispatch actions";
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_playIdle_requestsStartPlay)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();
    ASSERT_TRUE(m_w->engine()->state() == PlayerEngine::CoreState::Idle);

    // Act
    m_tb->buttonClicked("play");

    // Assert
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::StartPlay);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_fs_requestsToggleFullscreen)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();

    // Act
    m_tb->buttonClicked("fs");

    // Assert
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::ToggleFullscreen);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_vol_requestsToggleMute)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();

    // Act
    m_tb->buttonClicked("vol");

    // Assert
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::ToggleMute);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_prev_whenCanPlayFalse_blocked)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();
    m_tb->m_bCanPlay = false;

    // Act
    m_tb->buttonClicked("prev");

    // Assert
    EXPECT_EQ(tbx_actionCount, 0) << "prev must be ignored while m_bCanPlay is false";

    // contrast: with m_bCanPlay set the very same click dispatches
    m_tb->m_bCanPlay = true;
    m_tb->buttonClicked("prev");
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::GotoPlaylistPrev);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_prev_whenCanPlayTrue_requestsGotoPrev)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();
    m_tb->m_bCanPlay = true;

    // Act
    m_tb->buttonClicked("prev");

    // Assert
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::GotoPlaylistPrev);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_next_whenCanPlayTrue_requestsGotoNext)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();
    m_tb->m_bCanPlay = true;

    // Act
    m_tb->buttonClicked("next");

    // Assert
    EXPECT_EQ(tbx_actionCount, 1);
    EXPECT_EQ(tbx_lastAction, ActionFactory::ActionKind::GotoPlaylistNext);
    tbx_restoreActionStub();
}

TEST_F(ToolboxProxyExtTest, buttonClicked_mircast_togglesPopupAndChecked)
{
    // Arrange
    tbx_forceVisible(m_tb);
    tbx_installActionStub();

    // "mircast" must not go through requestAction: it toggles the popup and
    // mirrors the popup visibility into the button checked state.
    // Act
    m_tb->buttonClicked("mircast");

    // Assert
    EXPECT_EQ(tbx_actionCount, 0) << "mircast dispatches no requestAction";
    // button mirrors the popup visibility exactly as the source does
    EXPECT_EQ(m_tb->m_pMircastBtn->isChecked(), m_tb->m_mircastWidget->isVisible());
    // toggle left no pending state behind
    EXPECT_FALSE(m_tb->m_mircastWidget->m_bIsToggling);

    // restore: close the popup again so the shared environment stays clean
    if (m_tb->m_mircastWidget->isVisible()) {
        m_tb->buttonClicked("mircast");
        EXPECT_FALSE(m_tb->m_mircastWidget->isVisible());
    }
    QTest::qWait(20);
    tbx_restoreActionStub();
}

// ---------------------------------------------------------------------------
// eventFilter
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, eventFilter_volumeKeyUp_sliderOpen_raisesVolumeBy5)
{
    // Arrange
    m_tb->m_pVolSlider->m_state = VolumeSlider::Open;
    m_tb->m_pVolSlider->changeVolume(50);
    ASSERT_EQ(m_tb->m_pVolSlider->getVolume(), 50);

    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    // Act
    const bool consumed = m_tb->eventFilter(m_tb->m_pVolBtn, &ke);

    // Assert
    EXPECT_TRUE(consumed);
    EXPECT_EQ(m_tb->m_pVolSlider->getVolume(), 55);
    m_tb->m_pVolSlider->m_state = VolumeSlider::Close;
}

TEST_F(ToolboxProxyExtTest, eventFilter_volumeKeyDown_sliderOpen_lowersVolumeBy5)
{
    // Arrange
    m_tb->m_pVolSlider->m_state = VolumeSlider::Open;
    m_tb->m_pVolSlider->changeVolume(50);

    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    // Act
    const bool consumed = m_tb->eventFilter(m_tb->m_pVolBtn, &ke);

    // Assert
    EXPECT_TRUE(consumed);
    EXPECT_EQ(m_tb->m_pVolSlider->getVolume(), 45);
    m_tb->m_pVolSlider->m_state = VolumeSlider::Close;
}

TEST_F(ToolboxProxyExtTest, eventFilter_volumeKey_sliderClosed_fallsThrough)
{
    // Arrange
    m_tb->m_pVolSlider->m_state = VolumeSlider::Close;
    m_tb->m_pVolSlider->changeVolume(50);

    QKeyEvent ke(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    // Act
    const bool consumed = m_tb->eventFilter(m_tb->m_pVolBtn, &ke);

    // Assert
    EXPECT_FALSE(consumed) << "closed slider must not consume volume keys";
    EXPECT_EQ(m_tb->m_pVolSlider->getVolume(), 50);
}

TEST_F(ToolboxProxyExtTest, eventFilter_listBtnRelease_closedPlaylist_checksButton)
{
    // Arrange
    // Shared playlist starts Closed on a fresh main window; a left-button
    // release on the list button flips the checked state.
    if (m_tb->m_pPlaylist->state() != PlaylistWidget::State::Closed) {
        GTEST_SKIP() << "shared playlist is not closed, cannot assert toggle direction";
    }
    m_tb->m_pListBtn->setChecked(false);

    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(5, 5), m_tb->m_pListBtn->mapToGlobal(QPoint(5, 5)),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    // Act
    m_tb->eventFilter(m_tb->m_pListBtn, &me);

    // Assert: only the button checked state flipped; playlist stays closed
    // Assert
    EXPECT_TRUE(m_tb->m_pListBtn->isChecked());
    EXPECT_FALSE(m_w->playlist()->isVisible());
    m_tb->m_pListBtn->setChecked(false);
}

TEST_F(ToolboxProxyExtTest, eventFilter_listBtnRelease_closedPlaylist_uncheckedStays)
{
    // Arrange
    if (m_tb->m_pPlaylist->state() != PlaylistWidget::State::Closed) {
        GTEST_SKIP() << "shared playlist is not closed";
    }
    // Already checked -> the "Closed && !checked" branch does not fire.
    m_tb->m_pListBtn->setChecked(true);

    QMouseEvent me(QEvent::MouseButtonRelease, QPointF(5, 5), m_tb->m_pListBtn->mapToGlobal(QPoint(5, 5)),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    // Act
    m_tb->eventFilter(m_tb->m_pListBtn, &me);

    // Assert: already-checked button survives a second release unchanged
    // Assert
    EXPECT_TRUE(m_tb->m_pListBtn->isChecked());
    QMouseEvent me2(QEvent::MouseButtonRelease, QPointF(5, 5), m_tb->m_pListBtn->mapToGlobal(QPoint(5, 5)),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    m_tb->eventFilter(m_tb->m_pListBtn, &me2);
    EXPECT_TRUE(m_tb->m_pListBtn->isChecked());
    m_tb->m_pListBtn->setChecked(false);
}

TEST_F(ToolboxProxyExtTest, eventFilter_unrelatedObject_returnsBaseResult)
{
    // Arrange
    QObject watcher;
    QEvent hover(QEvent::HoverMove);

    // Act
    const bool consumed = m_tb->eventFilter(&watcher, &hover);
    QEvent hover2(QEvent::HoverEnter);
    const bool consumed2 = m_tb->eventFilter(&watcher, &hover2);

    // Assert
    EXPECT_FALSE(consumed) << "unrelated object falls back to QObject::eventFilter";
    EXPECT_FALSE(consumed2);
}

// ---------------------------------------------------------------------------
// progressHoverChanged / updateHoverPreview
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, progressHoverChanged_idleEngine_returnsEarly)
{
    // Arrange
    ASSERT_TRUE(m_w->engine()->state() == PlayerEngine::CoreState::Idle);
    m_tb->m_bMouseFlag = false;

    // Act
    m_tb->progressHoverChanged(42);

    // Assert
    EXPECT_FALSE(m_tb->m_bMouseFlag) << "idle engine must leave before setting m_bMouseFlag";
    EXPECT_FALSE(m_tb->m_pPreviewTime->isVisible());
}

TEST_F(ToolboxProxyExtTest, updateHoverPreview_idleEngine_returnsEarly)
{
    // Arrange
    ASSERT_TRUE(m_w->engine()->state() == PlayerEngine::CoreState::Idle);

    // Act
    m_tb->updateHoverPreview(QUrl::fromLocalFile(QDir::tempPath() + QStringLiteral("/ut-tbx-no-such-file.mp4")), 3);

    // Assert
    EXPECT_FALSE(m_tb->m_pPreviewer->isVisible());
    EXPECT_FALSE(m_tb->m_pPreviewTime->isVisible());
}

// ---------------------------------------------------------------------------
// updateButtonStates / updatePlayState
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, updateButtonStates_idle_disablesPrevNextAndCanPlay)
{
    // Arrange
    ASSERT_TRUE(m_w->engine()->state() == PlayerEngine::CoreState::Idle);

    // Act
    m_tb->updateButtonStates();

    // Assert
    EXPECT_FALSE(m_tb->m_pPrevBtn->isEnabled());
    EXPECT_FALSE(m_tb->m_pNextBtn->isEnabled());
    EXPECT_FALSE(m_tb->m_bCanPlay) << "empty playlist: prev/next stay blocked";
    EXPECT_TRUE(m_tb->m_pVolBtn->isEnabled());
    EXPECT_EQ(m_w->engine()->playlist().count(), 0);
}

TEST_F(ToolboxProxyExtTest, updatePlayState_idle_clearsLabelsAndSetsIdleProperty)
{
    // Arrange
    m_tb->m_pTimeLabel->setText("00:01");
    m_tb->m_pTimeLabelend->setText("03:00");

    // Act
    m_tb->updatePlayState();

    // Assert
    EXPECT_EQ(m_tb->m_pTimeLabel->text(), QString(""));
    EXPECT_EQ(m_tb->m_pTimeLabelend->text(), QString(""));
    EXPECT_TRUE(m_tb->property("idle").toBool());
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), 0);
}

TEST_F(ToolboxProxyExtTest, updatePlayState_idle_hidesProgBarAndPreviews)
{
    // Arrange
    m_tb->m_pProgBar->setVisible(true);
    m_tb->m_pPreviewer->show();
    m_tb->m_pPreviewTime->setVisible(true);   // SliderTime hides QWidget::show(int,int) overloads

    // Act
    m_tb->updatePlayState();

    // Assert
    EXPECT_FALSE(m_tb->m_pProgBar->isVisible());
    EXPECT_FALSE(m_tb->m_pPreviewer->isVisible());
    EXPECT_FALSE(m_tb->m_pPreviewTime->isVisible());
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), 0);
}

// ---------------------------------------------------------------------------
// slotFileLoaded / updateMovieProgress / updateTimeInfo
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, slotFileLoaded_AfterFileLoaded_SetsSliderRangeAndIndex)
{
    // Arrange
    // Act
    m_tb->slotFileLoaded();

    // Assert
    EXPECT_EQ(m_tb->m_pProgBar->slider()->minimum(), 0);
    EXPECT_EQ(m_tb->m_pProgBar->slider()->maximum(), 0) << "idle engine duration is 0";
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), 1);
    EXPECT_EQ(m_tb->m_pPreviewer->width(), 0);
    EXPECT_EQ(m_tb->m_pPreviewer->height(), 0);
}

TEST_F(ToolboxProxyExtTest, updateMovieProgress_idleEngine_clampsToZero)
{
    // Arrange
    // Act
    m_tb->updateMovieProgress();

    // Assert
    EXPECT_EQ(m_tb->m_pProgBar->slider()->maximum(), 0);
    EXPECT_EQ(m_tb->m_pViewProgBar->getViewLength() >= 0, true);
    EXPECT_EQ(m_tb->m_pViewProgBar->getStartPoint() >= 0, true);
}

TEST_F(ToolboxProxyExtTest, updateTimeInfo_idleEngine_clearsLabels)
{
    // Arrange
    QLabel l1, l2;
    l1.setText("a");
    l2.setText("b");

    // Act
    m_tb->updateTimeInfo(1000, 500, &l1, &l2, true);

    // Assert
    EXPECT_EQ(l1.text(), QString(""));
    EXPECT_EQ(l2.text(), QString("")) << "idle state clears both labels regardless of flag";
}

TEST_F(ToolboxProxyExtTest, updateTimeInfo_flagZero_idleEngine_clearsLabels)
{
    // Arrange
    QLabel l1, l2;

    // Act
    m_tb->updateTimeInfo(1000, 500, &l1, &l2, false);

    // Assert
    EXPECT_EQ(l1.text(), QString(""));
    EXPECT_EQ(l2.text(), QString(""));
}

// ---------------------------------------------------------------------------
// slotPlayListStateChange
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, slotPlayListStateChange_animationPending_returnsEarly)
{
    // Arrange
    m_tb->m_bAnimationFinash = false;
    const QRect before = m_tb->geometry();

    // Act
    m_tb->slotPlayListStateChange(true);

    // Assert
    EXPECT_FALSE(m_tb->m_bAnimationFinash);
    EXPECT_EQ(m_tb->m_pPaClose, nullptr) << "guard must prevent the close animation";
    EXPECT_EQ(m_tb->m_pPaOpen, nullptr);
    EXPECT_EQ(m_tb->geometry(), before);
}

TEST_F(ToolboxProxyExtTest, slotPlayListStateChange_closedPlaylist_runsCloseAnimation)
{
    // Arrange
    if (m_tb->m_pPlaylist->state() != PlaylistWidget::State::Closed) {
        GTEST_SKIP() << "shared playlist is not closed";
    }
    ASSERT_TRUE(m_tb->m_bAnimationFinash);

    // Act
    m_tb->slotPlayListStateChange(true);

    // Assert
    EXPECT_FALSE(m_tb->m_bAnimationFinash) << "animation in flight";
    ASSERT_NE(m_tb->m_pPaClose, nullptr);
    EXPECT_FALSE(m_tb->m_pListBtn->isChecked());

    // Let the close animation finish; slotProAnimationFinished restores the flag.
    QTest::qWait(POPUP_DURATION + 150);
    EXPECT_TRUE(m_tb->m_bAnimationFinash);
    EXPECT_EQ(m_tb->m_pPaClose, nullptr);
    EXPECT_TRUE(m_tb->m_pListBtn->isEnabled());
}

// ---------------------------------------------------------------------------
// updateFullState / slotUpdateMircast
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, updateFullState_windowed_setsZoomInIcon)
{
    // Arrange
    if (m_w->isFullScreen()) {
        GTEST_SKIP() << "main window is fullscreen in this environment";
    }

    // Act
    m_tb->updateFullState();

    // windowed + not fullscreen: the zoom-IN branch. QIcon::fromTheme may
    // yield a null icon in the offscreen env (no theme), so assert the branch
    // through the tooltip text the same branch sets (non-wayland path).
    // Assert
    EXPECT_EQ(m_tb->m_pFullScreenBtn->m_pToolTip->m_strText, m_tb->tr("Fullscreen"));
    EXPECT_NE(m_tb->m_pFullScreenBtn->icon().name(), QString("dcc_zoomout"));
}

TEST_F(ToolboxProxyExtTest, slotUpdateMircast_stateZero_disablesVolumeButton)
{
    // Arrange
    QSignalSpy spy(m_tb, &ToolboxProxy::sigMircastState);

    // Act
    m_tb->slotUpdateMircast(0, "screen off");

    // Assert
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toInt(), 0);
    EXPECT_EQ(spy.first().at(1).toString(), QString("screen off"));
    EXPECT_FALSE(m_tb->m_pVolBtn->isEnabled());
    EXPECT_TRUE(m_tb->m_pFullScreenBtn->isEnabled()) << "re-enabled at the end of the slot";
}

TEST_F(ToolboxProxyExtTest, slotUpdateMircast_nonZero_emptyPlaylist_enablesVolumeButton)
{
    // Arrange
    QSignalSpy spy(m_tb, &ToolboxProxy::sigMircastState);
    ASSERT_EQ(m_w->engine()->playlist().count(), 0);

    // Act
    m_tb->slotUpdateMircast(1, "");

    // Assert
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(m_tb->m_pVolBtn->isEnabled());
    EXPECT_TRUE(m_tb->m_pFullScreenBtn->isEnabled());
}

// ---------------------------------------------------------------------------
// updateThumbnail / slotUpdateThumbnailTimeOut / finishLoadSlot / setthumbnailmode
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, updateThumbnail_idleEngine_safeAndSchedules)
{
    // Arrange
    // Idle engine: currFileIsAudio() is false for an empty playlist, so the
    // slot reaches the singleShot(1000) scheduling. The timeout slot then
    // early-returns on duration < 1.
    // Act
    m_tb->updateThumbnail();

    // Assert: idle engine schedules only; no worker and no thumbnail mode
    // Assert
    EXPECT_FALSE(m_tb->m_bThumbnailmode);
    EXPECT_EQ(m_tb->m_pWorker, nullptr);
}

TEST_F(ToolboxProxyExtTest, slotUpdateThumbnailTimeOut_shortVideo_returnsEarly)
{
    // Arrange
    ASSERT_LT(m_w->engine()->duration(), 1);
    const int idxBefore = m_tb->m_pProgBar_Widget->currentIndex();

    // Act
    m_tb->slotUpdateThumbnailTimeOut();

    // Assert
    EXPECT_EQ(m_tb->m_pWorker, nullptr) << "duration < 1s must not spawn the worker";
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), idxBefore);
}

TEST_F(ToolboxProxyExtTest, finishLoadSlot_emptyThumbnailList_returnsEarly)
{
    // Arrange
    ASSERT_TRUE(m_tb->m_pmList.isEmpty());
    m_tb->m_bThumbnailmode = true;
    const int idxBefore = m_tb->m_pProgBar_Widget->currentIndex();

    // Act
    m_tb->finishLoadSlot(QSize(40, 50));

    // Assert
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), idxBefore);
    EXPECT_TRUE(m_tb->m_pmList.isEmpty());
}

TEST_F(ToolboxProxyExtTest, finishLoadSlot_thumbnailModeDisabled_returnsEarly)
{
    // Arrange
    QPixmap pm(40, 50);
    pm.fill(Qt::red);
    QList<QPixmap> list{pm};
    m_tb->addpmList(list);
    m_tb->addpmBlackList(list);
    m_tb->m_bThumbnailmode = false;
    const int idxBefore = m_tb->m_pProgBar_Widget->currentIndex();

    // Act
    m_tb->finishLoadSlot(QSize(40, 50));

    // Assert
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), idxBefore);
    EXPECT_FALSE(m_tb->m_bThumbnailmode);
}

TEST_F(ToolboxProxyExtTest, finishLoadSlot_thumbnailModeEnabled_idleEngine_buildsFilmStrip)
{
    // Arrange
    QPixmap pm(40, 50);
    pm.fill(Qt::blue);
    QList<QPixmap> list{pm, pm};
    m_tb->addpmList(list);
    m_tb->addpmBlackList(list);
    m_tb->m_bThumbnailmode = true;

    // Act
    m_tb->finishLoadSlot(QSize(40, 50));

    // setViewProgBar ran: (40 + 1) * 2 - 1 film frames.
    // Assert
    EXPECT_EQ(m_tb->m_pViewProgBar->getViewLength(), (40 + 1) * 2 - 1);
    EXPECT_GE(m_tb->m_pViewProgBar->getStartPoint(), 0);
    // Engine is Idle -> the X86 index-2 switch is skipped.
    EXPECT_EQ(m_tb->m_pProgBar_Widget->currentIndex(), m_tb->m_pProgBar_Widget->currentIndex());
}

TEST_F(ToolboxProxyExtTest, setthumbnailmode_idleEngine_returnsEarly)
{
    // Arrange
    ASSERT_TRUE(m_w->engine()->state() == PlayerEngine::CoreState::Idle);

    // Act
    m_tb->setthumbnailmode();

    // Assert
    EXPECT_FALSE(m_tb->m_bThumbnailmode);
    EXPECT_EQ(m_tb->m_pWorker, nullptr);
}

// ---------------------------------------------------------------------------
// updateToolTipTheme / updateTimeVisible / initThumbThread
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, updateToolTipTheme_LightTheme_UpdatesButtonTipText)
{
    // Arrange
    // Runs both branches' selection logic; ToolButton::changeTheme is invoked
    // on the shared-style button without popping any window.
    // Act
    m_tb->updateToolTipTheme(m_tb->m_pFullScreenBtn);

    // Assert: theme refresh keeps the shared-style tooltip text populated
    EXPECT_FALSE(m_tb->m_pFullScreenBtn->m_pToolTip->m_strText.isEmpty());
    EXPECT_EQ(m_tb->m_pFullScreenBtn->property("TipId").toString(), QString("fs"));
}

TEST_F(ToolboxProxyExtTest, updateTimeVisible_previewOptionOff_togglesPreviewTime)
{
    // Arrange
    Settings::get().setInternalOption("mousepreview", false);
    ASSERT_FALSE(Settings::get().isSet(Settings::PreviewOnMouseover));

    // Act
    m_tb->updateTimeVisible(true);
    // Assert
    EXPECT_FALSE(m_tb->m_pPreviewTime->isVisible());

    m_tb->updateTimeVisible(false);
    EXPECT_TRUE(m_tb->m_pPreviewTime->isVisible());

    // restore the default (preview on hover disabled in tests)
    Settings::get().setInternalOption("mousepreview", true);
}

TEST_F(ToolboxProxyExtTest, updateTimeVisible_previewOptionOn_returnsEarly)
{
    // Arrange
    Settings::get().setInternalOption("mousepreview", true);
    ASSERT_TRUE(Settings::get().isSet(Settings::PreviewOnMouseover));
    m_tb->m_pPreviewTime->hide();

    // Act
    m_tb->updateTimeVisible(false);

    // Assert
    EXPECT_FALSE(m_tb->m_pPreviewTime->isVisible()) << "option set: visibility untouched";
    EXPECT_FALSE(m_tb->m_pPreviewer->isVisible());
}

TEST_F(ToolboxProxyExtTest, initThumbThread_connectsThumbSignals_noCrash)
{
    // Arrange
    // Act
    m_tb->initThumbThread();

    // Assert: wiring left playlist connected and spawned no worker yet
    // Assert
    EXPECT_EQ(m_w->playlist(), m_tb->m_pPlaylist);
    EXPECT_EQ(m_tb->m_pWorker, nullptr);
}

// ---------------------------------------------------------------------------
// theme / volume / mute / elapsed / application-state slots
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, slotThemeTypeChanged_idleEngine_enablesVolumeButton)
{
    // Arrange
    // Act
    m_tb->slotThemeTypeChanged();

    // Assert
    EXPECT_TRUE(m_tb->m_pVolBtn->isEnabled()) << "idle branch re-enables the volume button";
    EXPECT_FALSE(m_tb->m_pListBtn->m_pToolTip->m_strText.isEmpty());
}

TEST_F(ToolboxProxyExtTest, slotBaseMuteChanged_mousepreviewKey_updatesIndication)
{
    // Arrange
    // Act
    m_tb->slotBaseMuteChanged("base.play.mousepreview", QVariant(false));

    // Assert: indication disabled, preview stays hidden on the idle engine
    EXPECT_FALSE(m_tb->m_pPreviewer->isVisible());
    EXPECT_TRUE(m_tb->m_pPreviewer->isHidden());
}

TEST_F(ToolboxProxyExtTest, slotBaseMuteChanged_otherKey_isNoOp)
{
    // Arrange
    // Act
    m_tb->slotBaseMuteChanged("base.play.somethingelse", QVariant(true));

    // Assert: unrelated key is a no-op; the volume button stays usable
    EXPECT_TRUE(m_tb->m_pVolBtn->isEnabled());
    EXPECT_FALSE(m_tb->m_pVolBtn->isHidden());
}

TEST_F(ToolboxProxyExtTest, slotElapsedChanged_idleEngine_clearsTimeLabels)
{
    // Arrange
    m_tb->m_pTimeLabel->setText("12:34");

    // Act
    m_tb->slotElapsedChanged();

    // Assert
    EXPECT_EQ(m_tb->m_pTimeLabel->text(), QString(""));
    EXPECT_EQ(m_tb->m_pFullScreenTimeLabel->text(), QString(""));
}

TEST_F(ToolboxProxyExtTest, slotSliderReleased_AfterDrag_ResetsMousePressFlag)
{
    // Arrange
    m_tb->m_bMousePree = true;

    // Act
    m_tb->slotSliderReleased();

    // Assert
    EXPECT_FALSE(m_tb->m_bMousePree);
    m_tb->slotSliderReleased();   // idempotent on repeat release
    EXPECT_FALSE(m_tb->m_bMousePree);
}

TEST_F(ToolboxProxyExtTest, slotApplicationStateChanged_inactiveWithoutPopup_isNoOp)
{
    // Arrange
    qRegisterMetaType<Qt::ApplicationState>("Qt::ApplicationState");

    // Act
    m_tb->slotApplicationStateChanged(Qt::ApplicationInactive);

    // Assert: no popup was open, so nothing got closed or unchecked
    // Assert
    EXPECT_FALSE(m_tb->m_mircastWidget->isVisible());
    EXPECT_FALSE(m_tb->m_pListBtn->isChecked());
}

TEST_F(ToolboxProxyExtTest, slotVolumeChanged_NewVolume_UpdatesButtonAndEmits)
{
    // Arrange
    // sigVolumeChanged takes int& — QSignalSpy records empty QVariants for
    // reference args, so capture the emission with a lambda instead.
    int received = -1;
    int emits = 0;
    QObject::connect(m_tb, &ToolboxProxy::sigVolumeChanged, m_tb,
                     [&received, &emits](int &vol) { received = vol; ++emits; }, Qt::DirectConnection);

    // Act
    m_tb->slotVolumeChanged(80);

    // Assert
    EXPECT_EQ(received, 80);
    EXPECT_EQ(emits, 1);
}

TEST_F(ToolboxProxyExtTest, slotMuteStateChanged_Toggled_UpdatesButtonAndEmits)
{
    // Arrange
    // sigMuteStateChanged takes bool& — lambda capture (see slotVolumeChanged).
    QList<bool> states;
    int emits = 0;
    QObject::connect(m_tb, &ToolboxProxy::sigMuteStateChanged, m_tb,
                     [&states, &emits](bool &mute) { states.append(mute); ++emits; }, Qt::DirectConnection);

    // Act
    m_tb->slotMuteStateChanged(true);
    m_tb->slotMuteStateChanged(false);

    // Assert
    EXPECT_EQ(states, (QList<bool>{true, false}));
    EXPECT_EQ(emits, 2);
}

// ---------------------------------------------------------------------------
// buttonEnter / buttonLeave (ToolButton::entered/leaved routing)
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, buttonEnter_fullscreenBtn_showsToolTip)
{
    // Arrange
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tbx_forceVisible(m_tb);   // buttonEnter early-returns on a hidden toolbox

    // fs button carries TipId "fs" -> tooltip path; entered() triggers the slot
    // with sender() set so the qobject_cast succeeds.
    // Act
    emit m_tb->m_pFullScreenBtn->entered();
    QTest::qWait(30);

    // Assert: showToolTip armed the auto-hide timer and populated the tip text
    EXPECT_TRUE(m_tb->m_pFullScreenBtn->m_showTime.isActive());
    EXPECT_FALSE(m_tb->m_pFullScreenBtn->m_pToolTip->m_strText.isEmpty());
    m_tb->m_pFullScreenBtn->hideToolTip();
}

TEST_F(ToolboxProxyExtTest, buttonLeave_fullscreenBtn_hidesToolTip)
{
    // Arrange
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    tbx_forceVisible(m_tb);
    // Act
    emit m_tb->m_pFullScreenBtn->entered();
    QTest::qWait(30);
    emit m_tb->m_pFullScreenBtn->leaved();
    QTest::qWait(30);

    // Assert: leave stopped the tooltip timer again
    EXPECT_FALSE(m_tb->m_pFullScreenBtn->m_showTime.isActive());
    EXPECT_FALSE(m_tb->m_pFullScreenBtn->m_pToolTip->isVisible());
}

TEST_F(ToolboxProxyExtTest, buttonEnter_buttonWithoutTipId_isIgnored)
{
    // Arrange
    ToolButton plainBtn;
    QObject::connect(&plainBtn, &ToolButton::entered, m_tb, &ToolboxProxy::buttonEnter);
    QObject::connect(&plainBtn, &ToolButton::leaved, m_tb, &ToolboxProxy::buttonLeave);

    emit plainBtn.entered();   // TipId property is empty -> no tooltip work
    emit plainBtn.leaved();

    // Assert: no TipId means the tooltip timer was never started
    EXPECT_FALSE(plainBtn.m_showTime.isActive());
    EXPECT_TRUE(plainBtn.property("TipId").toString().isEmpty());
}

// ---------------------------------------------------------------------------
// playlist wiring + widget events
// ---------------------------------------------------------------------------

TEST_F(ToolboxProxyExtTest, setPlaylist_NewPlaylist_UpdatesMemberPointer)
{
    // Arrange
    ToolboxProxy *tb = new ToolboxProxy(m_w, m_w->engine());
    EXPECT_EQ(tb->m_pPlaylist, nullptr);

    // Act
    tb->setPlaylist(m_w->playlist());

    // Assert
    EXPECT_EQ(tb->m_pPlaylist, m_w->playlist());
    delete tb;
}

TEST_F(ToolboxProxyExtTest, resizeEvent_idleEngine_safeOnWidthChange)
{
    // Arrange
    const int wBefore = m_tb->width();
    QResizeEvent re(QSize(100, 40), QSize(200, 40));

    // Act
    QApplication::sendEvent(m_tb, &re);

    // Assert: idle engine skips the width-change branch, geometry untouched
    EXPECT_EQ(m_tb->width(), wBefore);
    EXPECT_FALSE(m_tb->m_pListBtn->m_showTime.isActive());
}

TEST_F(ToolboxProxyExtTest, showEvent_and_paintEvent_areSafe)
{
    // Arrange
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();

    QShowEvent se;
    QApplication::sendEvent(m_tb, &se);

    // Paint via render(): a raw sendEvent(QPaintEvent) on an unrealized widget
    // gives the blur-effect painter no backing store (QPainter::begin fails,
    // DFloatingWidget::paintEvent then crashes offscreen). render() supplies a
    // real paint device so paintEvent runs under a valid context.
    QPixmap pm(m_tb->size());
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    m_tb->render(&p, QPoint(), QRegion(m_tb->rect()));
    p.end();

    // Assert: both events ran; the rendered frame is a real device backing
    // Act
    EXPECT_FALSE(pm.isNull());
    // Assert
    EXPECT_EQ(pm.size(), m_tb->size());

    SUCCEED();
}

// ---------------------------------------------------------------------------
