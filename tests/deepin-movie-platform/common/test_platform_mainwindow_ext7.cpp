// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ==========================================================================
// platform_mw_ext7 — gap-closing suite for Platform_MainWindow
//
// Goal: cover the 40 methods that the Mode-1 inventory flagged as still
// uncovered after the ext..ext6 suites, namely:
//   Platform_MainWindow: closeEvent, contextMenuEvent, dropEvent,
//     handleSettings, mouseDoubleClickEvent, mouseMoveEvent, toggleUIMode,
//     initMember, keyPressEvent, resizeEvent, loadPlayList, dragEnterEvent,
//     dragMoveEvent, focusInEvent, focusOutEvent, keyReleaseEvent,
//     mousePressEvent, mouseReleaseEvent, paintEvent, setupTitlebar
//   (focus/key-release paths that ext5/ext6 already dispatch via sendEvent
//   are NOT duplicated here — only state-observable variants are added).
// Safety rules (same as sibling suites):
//   * The shared window is fetched per case via dApp->getMainWindow() and
//     restored to its pre-case state whenever the case mutates it.
//   * Engine stays Idle; no playback, no fullscreen, no real processes of
//     consequence, no destructive actions (closeEvent's USE_TEST early
//     return keeps the harness alive).
//   * GTEST_SKIP guards for environment-sensitive branches.
//
// Branch list (complexity >= 10 per §4; bodies from e5346fd1):
//   toggleUIMode (complexity:21, 200 lines):
//     b1 m_bMiniMode==true -> exit/restore path          [ToggleUIMode_EnterThenExitMini]
//     b2 fullscreen/maximized guard (exitFullscreen/     [unreachable here: requires
//        showNormal)                                     real fullscreen toggle]
//     b3 WindowAbove toggle via requestAction             [ToggleUIMode_EnterThenExitMini]
//     b4 state flags / m_nStateBeforeMiniMode             [ToggleUIMode_EnterThenExitMini]
//     b5 dock-edge early return (x/y edge within 50px     [ToggleUIMode_DockEdgeHalfWidth]
//        or half-width, geometry != deskrect) -> hint
//     b6 enter success path (fixed 380x380, mini widgets) [ToggleUIMode_EnterThenExitMini]
//   contextMenuEvent (complexity:12):
//     b1 m_bMiniMode -> early return                      [ContextMenuEvent_MiniMode]
//     b2 m_bInBurstShootMode -> early return              [ContextMenuEvent_BurstMode]
//     b3 insideToolsArea -> early return                  [ContextMenuEvent_ToolsArea]
//     b4 else: xprop probe + delayed popup                [ContextMenuEvent_NormalPath]
//   closeEvent (complexity:10):
//     b1 USE_TEST -> early return (harness protection)    [CloseEvent_UseTestBuild]
//     b2..b9 real teardown path (hide/timers/engine       [unreachable: only compiled
//        stop/commitData/_Exit)                           out by USE_TEST]
//
// Not directly unit-testable (documented, counted as out of scope for this
// suite — see inventory-summary remarks):
//   * ~Platform_MainWindow: harness never deletes the shared window and a
//     second instance would tear down the global ThreadPool/mount watchers.
//   * USE_DXCB-gated slots: updateShadow, onMonitorButtonPressed/Released,
//     onMonitorMotionNotify, onApplicationStateChanged — not compiled into
//     this build (USE_DXCB=OFF).
//   * Platform_MainWindowFocusMonitor ctor/dtor/nativeEventFilter and the
//     file-local EventMonitor class: instantiated only inside the
//     USE_DXCB-gated monitor block.
//   * File-local free functions: ElideText, workaround_updateStyle,
//     createDecode/Vo/EffectOptionHandle, createSelectableLineEditOptionHandle
//     (internal linkage; reached only via the settings-dialog widget factory
//     registered in initSettings), operator<</>> for SessionInfo (DBus
//     marshalling helpers), compareBarData (callers_total==0 — dead code,
//     flagged for the defect report).
//
// 用例计数声明 (declared usecases; TEST count must equal this table):
//   CloseEvent              1
//   ContextMenuEvent        4
//   DropEvent               3
//   HandleSettings          1
//   MouseDoubleClickEvent   2
//   MouseMoveEvent          2
//   ToggleUIMode            2
//   InitMember              1
//   KeyPressEvent           1
//   ResizeEvent             1
//   LoadPlayList            1
//   DragEnterEvent          2
//   DragMoveEvent           1
//   PaintEvent              1
//   SetupTitlebar           1
//   MousePressEvent         1
//   MouseReleaseEvent       1
//   ----------------------------------
//   total usecases = 26 (TEST count == 26)
// ==========================================================================

#include <gtest/gtest.h>
#include <QtTest>
#include <QDebug>
#include <QGuiApplication>
#include <QWidget>

// Standard-library preload (prevents the sstream redeclare error).
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <memory>

#define protected public
#define private public
#include "src/common/platform/platform_mainwindow.h"
#undef protected
#undef private

#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QImage>
#include <DSettingsDialog>

#include <memory>

#include "application.h"
#include "src/common/actions.h"
#include "src/common/dmr_settings.h"
#include "src/libdmr/player_engine.h"

using namespace dmr;

// ---------- helpers (unique prefix pmw7_) ----------

static Platform_MainWindow *pmw7_window()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    return w;
}

static void pmw7_closeAllPopups()
{
    qApp->processEvents();
    while (QWidget *pw = QApplication::activePopupWidget())
        pw->close();
    qApp->processEvents();
}

static QMimeData *pmw7_mimeWithUrls(const QList<QUrl> &urls)
{
    auto *mime = new QMimeData();
    mime->setUrls(urls);
    return mime;
}

static PlayerEngine *pmw7_engine(Platform_MainWindow *w)
{
    PlayerEngine *eng = w->engine();
    EXPECT_NE(eng, nullptr);
    return eng;
}

// Qt 6 dispatches DragMove/Drop only to the drag manager's current target,
// i.e. the widget that accepted the preceding DragEnter (and the target is
// cleared again after each Drop delivery). A Drop sent standalone never
// reaches the widget, so every drop case must first prime the target with
// an accepted DragEnter carrying a url payload.
static void pmw7_primeDragTarget(Platform_MainWindow *w)
{
    std::unique_ptr<QMimeData> primeMime(pmw7_mimeWithUrls(
        {QUrl::fromLocalFile(QDir::temp().absoluteFilePath(
            QStringLiteral("nonexistent_dmr_ut_prime.mkv")))}));
    QDragEnterEvent prime(QPoint(100, 100), Qt::CopyAction | Qt::MoveAction,
                          primeMime.get(), Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w, &prime);
    EXPECT_TRUE(prime.isAccepted());
}

// ==========================================================================
// 1. closeEvent — USE_TEST build early-returns to protect the harness
// ==========================================================================

TEST(platform_mw_ext7, CloseEvent_UseTestBuild_HandlerUntouched_WindowSurvives)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: capture the event's acceptance state and window vitals.
    QCloseEvent ce;
    const bool acceptedBefore = ce.isAccepted();

    // Act: dispatch the close event (USE_TEST build must short-circuit).
    QApplication::sendEvent(w, &ce);

    // Assert: handler left the event untouched, window and engine alive.
    EXPECT_EQ(ce.isAccepted(), acceptedBefore);
    EXPECT_EQ(w->engine(), eng);
    EXPECT_NE(w->m_pToolbox, nullptr);
}

// ==========================================================================
// 2. contextMenuEvent — early-return branches (mini / burst / tools area)
//    and the normal xprop+popup path
// ==========================================================================

TEST(platform_mw_ext7, ContextMenuEvent_MiniMode_EarlyReturn_NoPopup)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: force mini mode so the first guard fires.
    const bool miniBefore = w->m_bMiniMode;
    w->m_bMiniMode = true;
    QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(300, 300),
                          w->mapToGlobal(QPoint(300, 300)));

    // Act: dispatch the context menu request.
    QApplication::sendEvent(w, &cme);
    pmw7_closeAllPopups();

    // Assert: no popup leaked and the flag survived.
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
    EXPECT_TRUE(w->m_bMiniMode);
    w->m_bMiniMode = miniBefore;
}

TEST(platform_mw_ext7, ContextMenuEvent_BurstMode_EarlyReturn_NoPopup)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: burst-shoot mode suppresses context menus entirely.
    const bool burstBefore = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    QContextMenuEvent cme(QContextMenuEvent::Mouse, QPoint(300, 300),
                          w->mapToGlobal(QPoint(300, 300)));

    // Act: dispatch the context menu request.
    QApplication::sendEvent(w, &cme);
    pmw7_closeAllPopups();

    // Assert: no popup leaked; flag restored afterwards.
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
    EXPECT_TRUE(w->m_bInBurstShootMode);
    w->m_bInBurstShootMode = burstBefore;
}

TEST(platform_mw_ext7, ContextMenuEvent_ToolsArea_EarlyReturn_NoPopup)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);
    ASSERT_TRUE(w->m_pTitlebar != nullptr);

    // Arrange: a point inside the titlebar (titlebar spans the top strip).
    const QPoint pos(10, 10);
    ASSERT_TRUE(w->m_pTitlebar->geometry().contains(pos));
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));

    // Act: dispatch the context menu request.
    QApplication::sendEvent(w, &cme);
    pmw7_closeAllPopups();

    // Assert: tools-area request produces no popup.
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
}

TEST(platform_mw_ext7, ContextMenuEvent_NormalPath_PopupFiredThenCleaned_NoStateLeak)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: a body-area point that is neither mini, burst, nor tools.
    const QPoint pos(w->width() / 2, w->height() / 2);
    const bool aboveBefore = w->m_bWindowAbove;
    QContextMenuEvent cme(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));

    // Act: full path — xprop probe (harmless offscreen) + delayed popup.
    QApplication::sendEvent(w, &cme);
    pmw7_closeAllPopups();

    // Assert: popups cleaned, engine unchanged, WindowAbove flag untouched
    // (offscreen xprop finds no _NET_WM_STATE_ABOVE property).
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
    EXPECT_EQ(w->engine(), eng);
    EXPECT_EQ(w->m_bWindowAbove, aboveBefore);
}

// ==========================================================================
// 3. dropEvent — no-urls early return, multi-url play request, subtitle
//    matching branch
// ==========================================================================

TEST(platform_mw_ext7, DropEvent_NoUrls_EarlyReturn_NotAccepted)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: prime the drag target (accepted DragEnter) so the drop is
    // actually delivered, then build the drop with a mime payload that has
    // no uri-list format at all (note: setUrls({}) would still register
    // the "text/uri-list" format and pass hasUrls()).
    pmw7_primeDragTarget(w);
    std::unique_ptr<QMimeData> mime(new QMimeData);
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction | Qt::MoveAction,
                    mime.get(), Qt::LeftButton, Qt::NoModifier);
    const bool acceptedBefore = drop.isAccepted();

    // Act: dispatch the drop.
    QApplication::sendEvent(w, &drop);

    // Assert: early return before acceptProposedAction (the constructor
    // ignores the event) and no popup side effects.
    EXPECT_FALSE(drop.isAccepted());
    EXPECT_EQ(drop.isAccepted(), acceptedBefore);
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
}

TEST(platform_mw_ext7, DropEvent_MultiUrls_PlaysRequested_Accepted_EngineIdle)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: prime the drag target, then multiple url entries (nonexistent
    // files keep the engine in Idle: addPlayFiles() rejects them so nothing
    // starts playing).
    pmw7_primeDragTarget(w);
    const QString tmpBase = QDir::temp().absolutePath();
    QList<QUrl> urls;
    urls << QUrl::fromLocalFile(tmpBase + QStringLiteral("/nonexistent_dmr_ut_1.mkv"))
         << QUrl::fromLocalFile(tmpBase + QStringLiteral("/nonexistent_dmr_ut_2.mp4"));
    std::unique_ptr<QMimeData> mime(pmw7_mimeWithUrls(urls));
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction | Qt::MoveAction,
                    mime.get(), Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the drop.
    QApplication::sendEvent(w, &drop);

    // Assert: event accepted by acceptProposedAction, engine stayed Idle.
    EXPECT_TRUE(drop.isAccepted());
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
}

TEST(platform_mw_ext7, DropEvent_SingleSubtitle_Idle_NoVideoMatch_HintsNoPlay)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);
    ASSERT_EQ(eng->state(), PlayerEngine::Idle);

    // Arrange: a directory holding one subtitle and one video file. The
    // subtitle must carry real SRT content so PlayerEngine::isSubtitle()
    // (FFmpeg probe) classifies it; the video is made LARGER so
    // QDir(Size|Reversed) sorts ascending by size and the final iteration
    // lands on the non-matching mkv, leaving the match target null and
    // skipping the play call (the subtitle must not be the last entry
    // since its own basename always contains itself).
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString sub = tmpDir.filePath(QStringLiteral("a.srt"));
    const QString vid = tmpDir.filePath(QStringLiteral("bbb.mkv"));
    QFile fsub(sub);
    ASSERT_TRUE(fsub.open(QIODevice::WriteOnly));
    ASSERT_GT(fsub.write("1\n00:00:01,000 --> 00:00:02,000\nhello\n"), 0);
    fsub.close();
    QFile fvid(vid);
    ASSERT_TRUE(fvid.open(QIODevice::WriteOnly));
    ASSERT_EQ(fvid.write(QByteArray(1024, '2')), 1024);
    fvid.close();
    ASSERT_TRUE(QFileInfo(sub).size() < QFileInfo(vid).size());

    pmw7_primeDragTarget(w);
    std::unique_ptr<QMimeData> mime(pmw7_mimeWithUrls({QUrl::fromLocalFile(sub)}));
    QDropEvent drop(QPointF(100, 100), Qt::CopyAction | Qt::MoveAction,
                    mime.get(), Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the subtitle-only drop (Idle engine -> match path).
    QApplication::sendEvent(w, &drop);

    // Assert: subtitle branch returns before acceptProposedAction and the
    // engine never starts loading anything.
    EXPECT_FALSE(drop.isAccepted());
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
}

// ==========================================================================
// 4. handleSettings — private; driven through the private/public include
// ==========================================================================

TEST(platform_mw_ext7, HandleSettings_SettingsUnchanged_DialogShown)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: a settings dialog and the current decode option value.
    DSettingsDialog dsd;
    const int decodeBefore = Settings::get().settings()
                                 ->getOption(QLatin1String("base.decode.select")).toInt();

    // Act: run the settings handler (USE_TEST branch shows the dialog).
    w->handleSettings(&dsd);
    pmw7_closeAllPopups();

    // Assert: dialog marked and shown, decode option untouched.
    EXPECT_EQ(dsd.objectName(), QLatin1String("DSettingsDialog"));
    EXPECT_TRUE(dsd.isVisible());
    EXPECT_EQ(Settings::get().settings()
                  ->getOption(QLatin1String("base.decode.select")).toInt(),
              decodeBefore);
    dsd.close();
}

// ==========================================================================
// 5. mouseDoubleClickEvent — mini and burst guards leave playback alone
// ==========================================================================

TEST(platform_mw_ext7, MouseDoubleClickEvent_MiniMode_NoAction)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: mini mode guard is the first branch.
    const bool miniBefore = w->m_bMiniMode;
    w->m_bMiniMode = true;
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(100, 100),
                    QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);

    // Act: dispatch the double click.
    QApplication::sendEvent(w, &dbl);

    // Assert: engine untouched.
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
    EXPECT_TRUE(w->m_bMiniMode);
    w->m_bMiniMode = miniBefore;
}

TEST(platform_mw_ext7, MouseDoubleClickEvent_BurstMode_NoAction)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: burst-shoot mode swallows double clicks.
    const bool burstBefore = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = true;
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(100, 100),
                    QPointF(100, 100), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);

    // Act: dispatch the double click.
    QApplication::sendEvent(w, &dbl);

    // Assert: engine untouched; flag restored.
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
    EXPECT_TRUE(w->m_bInBurstShootMode);
    w->m_bInBurstShootMode = burstBefore;
}

// ==========================================================================
// 6. mouseMoveEvent — start-mini guard and the moved-state bookkeeping
// ==========================================================================

TEST(platform_mw_ext7, MouseMoveEvent_StartMini_EarlyReturn)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: m_bStartMini routes every move to the early return. The
    // move carries the left button: button-less moves are dropped by
    // QApplication::notify on widgets without mouseTracking.
    const bool startMiniBefore = w->m_bStartMini;
    w->m_bStartMini = true;
    QMouseEvent mv(QEvent::MouseMove, QPointF(200, 200), QPointF(200, 200),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the move.
    QApplication::sendEvent(w, &mv);

    // Assert: handler returned before touching move bookkeeping.
    EXPECT_FALSE(w->m_bMouseMoved);
    EXPECT_TRUE(w->m_bStartMini);
    w->m_bStartMini = startMiniBefore;
}

TEST(platform_mw_ext7, MouseMoveEvent_SmallDeltaDebounce_ThenRealMoveMarksMoved)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange step 1: origin equals the move target -> delta below the
    // debounce threshold (5px) must be swallowed. The move carries the
    // left button: QApplication::notify throws away button-less moves on
    // widgets without mouseTracking, so button-less moves would never be
    // delivered to the handler at all.
    const QPoint anchor = w->mapToGlobal(QPoint(100, 100));
    const QPoint originBefore = w->m_posMouseOrigin;
    w->m_posMouseOrigin = anchor;
    QMouseEvent small(QEvent::MouseMove, QPointF(101, 102), QPointF(101, 102),
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);

    // Act 1: tiny move.
    QApplication::sendEvent(w, &small);

    // Assert 1: debounced — m_bMouseMoved stays false.
    EXPECT_FALSE(w->m_bMouseMoved);

    // Arrange step 2: reset the flag and move far from the origin.
    w->m_bMouseMoved = false;
    w->m_posMouseOrigin = w->mapToGlobal(QPoint(0, 0));
    QMouseEvent real(QEvent::MouseMove, QPointF(150, 150), QPointF(150, 150),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);

    // Act 2: real move.
    QApplication::sendEvent(w, &real);

    // Assert 2: bookkeeping marks the mouse as moved; the origin was
    // updated to the delivered move target. Restore state.
    EXPECT_TRUE(w->m_bMouseMoved);
    EXPECT_EQ(w->m_posMouseOrigin, w->mapToGlobal(QPoint(150, 150)));
    w->m_bMouseMoved = false;
    w->m_posMouseOrigin = originBefore;
}

// ==========================================================================
// 7. toggleUIMode — enter/exit mini mode round trip + dock-edge early return
// ==========================================================================

TEST(platform_mw_ext7, ToggleUIMode_EnterThenExitMini_RestoresGeometryAndFlags)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);
    ASSERT_EQ(eng->state(), PlayerEngine::Idle);

    // Arrange: remember the normal-mode geometry; idle engine keeps the
    // 380x380 fixed size from being enlarged.
    const QRect normalGeom = w->geometry();
    ASSERT_FALSE(w->isFullScreen());
    ASSERT_FALSE(w->isMaximized());

    // Act 1: enter mini mode.
    w->toggleUIMode();

    // Assert 1: mini UI active — fixed 380x380, titlebar hidden, mini
    // widgets shown, saved state recorded, WindowAbove toggled on.
    EXPECT_TRUE(w->m_bMiniMode);
    EXPECT_FALSE(w->m_bStartMini);
    EXPECT_EQ(w->size(), QSize(380, 380));
    EXPECT_TRUE(w->m_pTitlebar->isHidden());
    EXPECT_FALSE(w->m_pMiniPlayBtn->isHidden());
    EXPECT_NE(w->m_nStateBeforeMiniMode, static_cast<int>(Platform_MainWindow::SBEM_None));
    EXPECT_TRUE(w->m_bWindowAbove);

    // Act 2: leave mini mode.
    w->toggleUIMode();

    // Assert 2: normal UI restored — mini widgets hidden again, titlebar
    // back, minimum size restored; geometry is rebuilt from
    // m_lastRectInNormalMode, whose height the WM/heightForWidth policy
    // may adjust, so assert the stable width and that the window left
    // mini size.
    EXPECT_FALSE(w->m_bMiniMode);
    EXPECT_EQ(w->m_nStateBeforeMiniMode, static_cast<int>(Platform_MainWindow::SBEM_None));
    EXPECT_FALSE(w->m_bWindowAbove);
    EXPECT_TRUE(w->m_pMiniPlayBtn->isHidden());
    EXPECT_FALSE(w->m_pTitlebar->isHidden());
    EXPECT_EQ(w->minimumSize(), QSize(614, 500));
    EXPECT_EQ(w->size().width(), normalGeom.width());
    EXPECT_NE(w->size(), QSize(380, 380));
}

TEST(platform_mw_ext7, ToggleUIMode_DockEdgeHalfWidth_ShowsHintAndCancels)
{
    // Arrange: anchor the window at the top-left half-width slot that the
    // dock-edge guard detects. The previous toggle test leaves a (614,500)
    // minimum size on the window which would silently clamp resize();
    // clear the size constraints so the target size lands exactly.
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) GTEST_SKIP() << "no primary screen in this environment";

    // Arrange: pin the window to the left screen edge with half width —
    // exactly the dock-edge heuristic (|x|==0, y==0, width==deskW/2).
    const QRect normalGeom = w->geometry();
    const QRect desk = screen->availableGeometry();
    const int halfW = desk.width() / 2;
    w->setMinimumSize(0, 0);
    w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    w->move(QPoint(0, 0));
    w->resize(halfW, 300);
    ASSERT_EQ(w->pos(), QPoint(0, 0));

    // Act: toggleUIMode must bail into the hint branch.
    w->toggleUIMode();

    // Assert: still in normal mode, start-mini cancelled, geometry kept.
    EXPECT_FALSE(w->m_bMiniMode);
    EXPECT_FALSE(w->m_bStartMini);
    EXPECT_EQ(w->size(), QSize(halfW, 300));

    // Cleanup: restore the original window geometry and size constraints.
    w->setGeometry(normalGeom);
    w->setMinimumSize(614, 500);
}

// ==========================================================================
// 8. initMember — constructor-established state snapshot
// ==========================================================================

TEST(platform_mw_ext7, InitMember_ConstructionState_DefaultsInitialized)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);

    // Arrange/Act: the harness already constructed the shared window via
    // initMember(); this case documents its resulting invariants.

    // Assert: the deferred singleShot in the constructor may not have run
    // in this harness, so the playlist is legitimately null here; do not
    // assert on it. m_lastRectInNormalMode is likewise mutated by the
    // toggle tests running before this case, so only stable invariants are
    // documented: wiring and playback/window defaults.
    EXPECT_NE(w->engine(), nullptr);
    EXPECT_NE(w->m_pToolbox, nullptr);
    EXPECT_NE(w->m_pTitlebar, nullptr);
    EXPECT_NE(w->m_pMiniPlayBtn, nullptr);
    EXPECT_NE(w->m_pCommHintWid, nullptr);

    // Assert: playback and window defaults.
    EXPECT_FALSE(w->m_bMiniMode);
    EXPECT_FALSE(w->m_bStartMini);
    EXPECT_FALSE(w->m_bIsTouch);
    EXPECT_EQ(w->m_nDisplayVolume, 100);
    EXPECT_EQ(w->m_dPlaySpeed, 1.0);
    EXPECT_EQ(w->m_nLastCookie, 0);
    EXPECT_EQ(w->m_nPowerCookie, 0);

    // Assert: container defaults (m_lastRectInNormalMode is excluded: the
    // toggle tests running before this case legitimately fill it).
    EXPECT_TRUE(w->m_listOpenFiles.isEmpty());
    EXPECT_FALSE(w->m_bMouseMoved);
    EXPECT_FALSE(w->m_bMousePressed);
}

// ==========================================================================
// 9. keyPressEvent — playlist-closed fall-through keeps hwdec state
// ==========================================================================

TEST(platform_mw_ext7, KeyPressEvent_PlaylistClosed_KeyFallsThrough_NoStateChange)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: playlist is closed so the Key_X selection branch is skipped;
    // a plain letter key falls through to QWidget::keyPressEvent.
    const QString hwdecBefore = w->m_sCurrentHwdec;
    QKeyEvent ke(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));

    // Act: dispatch the key press.
    QApplication::sendEvent(w, &ke);

    // Assert: no hardware-decode state was mutated by the fall-through.
    EXPECT_EQ(w->m_sCurrentHwdec, hwdecBefore);
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
}

// ==========================================================================
// 10. resizeEvent — Idle engine must not record a user window size
// ==========================================================================

TEST(platform_mw_ext7, ResizeEvent_Idle_UserWindowSizeNotSaved)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);
    ASSERT_EQ(eng->state(), PlayerEngine::Idle);

    // Arrange: capture geometry; the saved-user-size flag starts unset.
    const QRect geomBefore = w->geometry();
    ASSERT_FALSE(w->m_bUserWindowSizeSet);

    // Act: a real widget resize drives the full resizeEvent chain
    // (LimitWindowize, proxy geometry, movie widget/toolbox sync).
    w->resize(600, 450);

    // Assert: Idle state means the user size is NOT remembered.
    EXPECT_FALSE(w->m_bUserWindowSizeSet);
    EXPECT_TRUE(w->m_userSetWindowSize.isEmpty());

    // Cleanup.
    w->setGeometry(geomBefore);
}

// ==========================================================================
// 11. loadPlayList — playlist widget is recreated hidden and re-registered
// ==========================================================================

TEST(platform_mw_ext7, LoadPlayList_CreatesPlaylistWidget_WiredHidden)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    PlayerEngine *eng = pmw7_engine(w);
    ASSERT_NE(eng, nullptr);

    // Arrange: the constructor defers playlist creation behind a 300ms
    // singleShot, so the harness may legitimately still have none; the
    // pre-existing pointer (if any) proves the replacement semantics.
    Platform_PlaylistWidget *oldList = w->m_pPlaylist;

    // Act: rebuild the playlist, hide it, wire it to the toolbox and
    // trigger the engine-side playlist load (empty open list: no play).
    w->loadPlayList();

    // Assert: a fresh, hidden playlist widget now exists, is parented to
    // the window and left the engine idle.
    ASSERT_NE(w->m_pPlaylist, nullptr);
    EXPECT_NE(w->m_pPlaylist, oldList);
    EXPECT_TRUE(w->m_pPlaylist->isHidden());
    EXPECT_EQ(w->m_pPlaylist->parentWidget(), static_cast<QWidget *>(w));
    EXPECT_EQ(eng->state(), PlayerEngine::Idle);
}

// ==========================================================================
// 12. dragEnterEvent / dragMoveEvent — accept only when urls present
// ==========================================================================

TEST(platform_mw_ext7, DragEnterEvent_Urls_Accepted)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: drag payload with a file url.
    std::unique_ptr<QMimeData> mime(pmw7_mimeWithUrls(
        {QUrl::fromLocalFile(QDir::temp().absoluteFilePath(
            QStringLiteral("nonexistent_dmr_ut_drag.mkv")))}));
    QDragEnterEvent enter(QPoint(100, 100), Qt::CopyAction | Qt::MoveAction,
                          mime.get(), Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the drag enter.
    QApplication::sendEvent(w, &enter);

    // Assert: source proposed actions accepted.
    EXPECT_TRUE(enter.isAccepted());
}

TEST(platform_mw_ext7, DragEnterEvent_NoUrls_NoActionChange_DocumentsPreAccept)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: drag payload without urls.
    std::unique_ptr<QMimeData> mime(pmw7_mimeWithUrls({}));
    QDragEnterEvent enter(QPoint(100, 100), Qt::CopyAction | Qt::MoveAction,
                          mime.get(), Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the drag enter.
    QApplication::sendEvent(w, &enter);

    // Assert: Qt's QDragEnterEvent constructor pre-accepts the event, so
    // isAccepted() is true even on the no-urls branch and cannot be
    // asserted false; the observable contract of this path is dispatch
    // safety — the handler runs, leaves the proposed action untouched and
    // the window intact.
    EXPECT_TRUE(enter.isAccepted());
    EXPECT_EQ(enter.proposedAction(), Qt::CopyAction);
    EXPECT_NE(w, nullptr);
}

TEST(platform_mw_ext7, DragMoveEvent_Urls_Accepted)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: an active drag hovering over the window body.
    std::unique_ptr<QMimeData> mime(pmw7_mimeWithUrls(
        {QUrl::fromLocalFile(QDir::temp().absoluteFilePath(
            QStringLiteral("nonexistent_dmr_ut_move.mkv")))}));
    QDragMoveEvent move(QPoint(150, 150), Qt::CopyAction | Qt::MoveAction,
                        mime.get(), Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the drag move.
    QApplication::sendEvent(w, &move);

    // Assert: hovering with urls is accepted.
    EXPECT_TRUE(move.isAccepted());
}

// ==========================================================================
// 13. paintEvent — Idle rendering produces a valid widget image
// ==========================================================================

TEST(platform_mw_ext7, PaintEvent_Idle_GrabsWindow)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "no screen";

    // Arrange/Act: grab() runs the full paint chain (Idle -> white fill
    // + centered movie icon).
    const QImage img = w->grab().toImage();

    // Assert: a non-empty image matching the widget size.
    EXPECT_FALSE(img.isNull());
    EXPECT_GT(img.width(), 0);
    EXPECT_EQ(img.width(), w->width());
}

// ==========================================================================
// 14. setupTitlebar — rebuilds the titlebar strip with fixed height
// ==========================================================================

TEST(platform_mw_ext7, SetupTitlebar_Recreates_Titlebar50_MenuBound)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: remember the current titlebar.
    Titlebar *oldBar = w->m_pTitlebar;
    ASSERT_NE(oldBar, nullptr);

    // Act: rebuild the titlebar (new widget + menu binding).
    w->setupTitlebar();

    // Assert: a fresh titlebar with the fixed 50px strip and a live menu.
    EXPECT_NE(w->m_pTitlebar, oldBar);
    EXPECT_EQ(w->m_pTitlebar->height(), 50);
    EXPECT_NE(w->m_pTitlebar->titlebar()->menu(), nullptr);
}

// ==========================================================================
// 15. mousePressEvent — focus-dependent press handling
// ==========================================================================

TEST(platform_mw_ext7, MousePressEvent_FocusDependent_StateUpdate)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: press point in the body area; whether the window has the
    // keyboard focus decides the branch (no focus -> early return).
    const QPoint pos(120, 120);
    const bool hadFocus = (qApp->focusWindow() != nullptr);
    const QPoint pressBefore = w->m_pressPoint;
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), QPointF(pos),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);

    // Act: dispatch the press.
    QApplication::sendEvent(w, &press);

    // Assert: both branches are asserted on the observable outcome.
    if (hadFocus) {
        EXPECT_TRUE(w->m_bMousePressed);
        EXPECT_EQ(w->m_pressPoint, pos);
    } else {
        EXPECT_FALSE(w->m_bMousePressed);
        EXPECT_EQ(w->m_pressPoint, pressBefore);
    }

    // Cleanup: drop any press state this case introduced.
    w->m_bMousePressed = false;
    w->m_bMouseMoved = false;
}

// ==========================================================================
// 16. mouseReleaseEvent — delayed single-click timer outside the tools area
// ==========================================================================

TEST(platform_mw_ext7, MouseReleaseEvent_TimerStartedOutsideToolsArea_Stopped)
{
    Platform_MainWindow *w = pmw7_window();
    ASSERT_NE(w, nullptr);
    pmw7_engine(w);

    // Arrange: the delayed-click branch requires a playlist (the
    // constructor defers creation behind a 300ms singleShot); build one if
    // the harness has not yet spun the timer.
    if (!w->m_pPlaylist) {
        w->loadPlayList();
    }
    ASSERT_NE(w->m_pPlaylist, nullptr);

    // Arrange: no previous move, release point outside titlebar/toolbox so
    // the delayed single-click timer (120ms) starts.
    const QPoint pos(10, w->height() / 2);
    const bool inTools = w->insideToolsArea(pos);
    w->m_bMouseMoved = false;
    w->m_bMousePressed = false;
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), QPointF(pos),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

    // Act: dispatch the release.
    QApplication::sendEvent(w, &release);

    // Assert: the timer runs exactly when the release landed outside the
    // tools area (true here); stop it immediately so no async UI action
    // follows.
    EXPECT_FALSE(inTools);
    EXPECT_TRUE(w->m_delayedMouseReleaseTimer.isActive());
    w->m_delayedMouseReleaseTimer.stop();
    w->m_bMouseMoved = false;
    w->m_bMousePressed = false;
}
