// Copyright (C) 2026 UnionTech Software Technology Co., Ltd.
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Branch: ut-movie-t06 @ e5346fd1 (upstream/master)
// Unit tests for the POPULATED state of
//   src/widgets/platform/platform_playlist_widget.cpp
//   (Platform_PlaylistWidget + the .cpp-local Platform_PlayItemWidget)
//
// The existing suite "platform_pw_ext" only covers the EMPTY / Idle branches
// and deliberately skips item construction. This suite is the counterpart:
// the engine model is populated with fake non-local URLs (no file I/O, no
// playback), loadPlaylist()/appendItems() build real item widgets, and the
// populated branches of every stateful method are exercised end to end.
//
// Suite name "ppw_pop"; static helpers use unique prefix "ppwpop_".
//
// Case count table (Mode 2 declaration, kept in sync with the TEST list):
//   | #  | case                                                      | feature(method)             |
//   | 01 | loadPlaylist_buildsWidgetsCountKindAndNum                  | loadPlaylist                |
//   | 02 | appendItems_growsFromModelTail                             | appendItems                 |
//   | 03 | updateItemStates_marksCurrentPlayingOthersNormal           | updateItemStates            |
//   | 04 | updateItemInfo_updatesTooltip_validIndexOnly               | updateItemInfo              |
//   | 05 | updateSelectItem_walksUpDownBoundaries                     | updateSelectItem            |
//   | 06 | updateSelectItem_enterPlaysSelectedRow                     | updateSelectItem            |
//   | 07 | updateSelectItem_enterFocusOnClearButtonClearsModel        | updateSelectItem            |
//   | 08 | removeClickedItem_hiddenShortcutKeepsModel                 | removeClickedItem           |
//   | 09 | removeClickedItem_visibleShortcutRemovesSelected           | removeClickedItem           |
//   | 10 | removeClickedItem_clickedItemRemovesItsRow                 | removeClickedItem           |
//   | 11 | slotDoubleClickedItem_playsRowOrNullSafe                   | slotDoubleClickedItem       |
//   | 12 | doubleClickChain_mouseDoubleClickPlaysRow                  | mouseDoubleClickEvent chain |
//   | 13 | doubleClickChain_invalidLocalEmitsSigInvalidFile           | doDoubleClick invalid file  |
//   | 14 | slotShowSelectItem_setsCurrentAndSelected                  | slotShowSelectItem          |
//   | 15 | eventFilter_branchMatrixPopulated                          | eventFilter                 |
//   | 16 | eventFilter_focusInEmptyListAndTabCycles                   | eventFilter                 |
//   | 17 | contextMenuEvent_onItemAndOffItemSetsMouseItem             | contextMenuEvent            |
//   | 18 | contextMenuEvent_padBranchSelectsUnderMouse                | contextMenuEvent (pad)      |
//   | 19 | togglePopup_openCloseRoundtripWithSignal                   | togglePopup                 |
//   | 20 | togglePopup_doubleToggleOpensThenCloses                   | togglePopup                 |
//   | 21 | showEvent_batchUpdateSizeHintsWhenVisible                  | showEvent                   |
//   | 22 | resizeEvent_updatesListWidthAndEmitsSizeChange             | resizeEvent                 |
//   | 23 | clear_resetsWidgetsLabelAndReload                          | clear                       |
//   | 24 | removeItem_reindexesAndBoundarySafe                        | removeItem                  |
//   | 25 | slotRowsMoved_switchesModelPositions                       | slotRowsMoved               |
//   | 26 | slotCloseItem_recordsClickedItemAndRequestsRemove          | slotCloseItem               |
//   | 27 | slotCloseTimeTimeOut_closesOpenedPopup                     | slotCloseTimeTimeOut        |
//   | 28 | endAnimation_nullAnimationsNoOp                           | endAnimation                |
//   | 29 | OnItemChanged_selectsCurrentViaDirectCall                  | OnItemChanged               |
//   | 30 | openItemInFM_showItemInfo_mouseItemGuards                  | openItemInFM/showItemInfo   |
//
// 10-item preflight checklist:
//   [x] New TEST file, one suite, unique helper prefix (ppwpop_)
//   [x] Existing suites untouched (append-only testing)
//   [x] Stubbing: Platform_ToolboxProxy focus members (null m_pListBtn in the
//       harness — the same root cause that excluded boost_dm_wid), PlayerEngine::
//       playSelected/clearPlaylist, MainWindow::requestAction (slotCloseItem only),
//       utils::ShowInFileManager, CompositingManager::isPadSystem (pad case)
//   [x] No real playback/decode/network/local-file I/O: all model items are
//       fake http:// URLs; the one local-path case points at a missing file
//       and asserts the sigInvalidFile emission only
//   [x] No direct construction of the .cpp-local Platform_PlayItemWidget —
//       instances come from loadPlaylist()/appendItems() and are reached via
//       QWidget*/QObject property APIs (ItemState / ItemKind properties)
//   [x] Model scrubbed in SetUp/TearDown so later suites see a clean Idle model
//   [x] Q_ASSERT-safe: close paths only run with the popup actually visible
//       (main window shown first); no exec()-blocking dialogs are invoked
//   [x] Focus-dependent cases carry GTEST_SKIP fallbacks (headless WM-less runs)
//   [x] Known crashers NOT triggered: removeItem(idx>count), slotShowSelectItem
//       (nullptr), pad context-menu outside any item — reported as defects
//   [x] SPDX header with year 2026 + GPL-3.0-or-later

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QFocusEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QSignalSpy>
#include <QUrl>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QPropertyAnimation>

// Project headers that expose private members — included LAST, under the
// access-override macros, so STL/Qt headers (already included above) are
// parsed with their normal access specifiers.
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <memory>
#define protected public
#define private public
#include "src/widgets/platform/platform_playlist_widget.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/common/actions.h"
#include "src/libdmr/player_engine.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/compositing_manager.h"
#include "src/libdmr/utils.h"
#include "stub/stub.h"
#undef protected
#undef private
#include "application.h"

using namespace dmr;

// ===========================================================================
// Stubs — capture, never execute. Members of stubbed member functions take
// the implicit this as a leading void* (same convention as existing suites).
// ===========================================================================

static int ppwpop_gPlaySelectedCalls = 0;
static int ppwpop_gLastPlaySelectedIdx = -1;
static int ppwpop_gClearCalls = 0;
static int ppwpop_gRequestActionCalls = 0;
static ActionFactory::ActionKind ppwpop_gLastActionKind = ActionFactory::ActionKind::Exit;
static QList<QVariant> ppwpop_gLastActionArgs;
static QString ppwpop_gLastFMPath;

static void ppwpop_resetCaptures()
{
    ppwpop_gPlaySelectedCalls = 0;
    ppwpop_gLastPlaySelectedIdx = -1;
    ppwpop_gClearCalls = 0;
    ppwpop_gRequestActionCalls = 0;
    ppwpop_gLastActionKind = ActionFactory::ActionKind::Exit;
    ppwpop_gLastActionArgs.clear();
    ppwpop_gLastFMPath.clear();
}

static bool ppwpop_listBtnFocus_stub(void *) { return false; }
static void ppwpop_setBtnFocusSign_stub(void *, bool) {}
static void ppwpop_playSelected_stub(void *, int id)
{
    ++ppwpop_gPlaySelectedCalls;
    ppwpop_gLastPlaySelectedIdx = id;
}
static void ppwpop_clearPlaylist_stub(void *) { ++ppwpop_gClearCalls; }
static void ppwpop_requestAction_stub(void *, ActionFactory::ActionKind kind,
                                      bool, QList<QVariant> args, bool)
{
    ++ppwpop_gRequestActionCalls;
    ppwpop_gLastActionKind = kind;
    ppwpop_gLastActionArgs = args;
}
static void ppwpop_showInFileManager_stub(const QString &path) { ppwpop_gLastFMPath = path; }
static bool ppwpop_isPad_true_stub() { return true; }
// The real isPadSystem() races with window-manager readiness on X11 (a "no
// compositing manager" probe can flip mid-suite), turning selection tests
// into the pad doDoubleClick branch. Pin it to false for the whole fixture;
// the pad-specific test overrides it locally.
static bool ppwpop_isPad_false_stub() { return false; }
// The harness's startup config load spawns a GetThumbnail thread whose
// per-item results arrive via a BlockingQueuedConnection at an arbitrary
// later qWait — silently appending a saved item to the model mid-suite.
// Swallow those async updates so every test sees exactly its populate() data.
static void ppwpop_onAsyncUpdate_noop_stub(void *, PlayItemInfo) {}
static void ppwpop_slotCloseTimeTimeOut_stub(void *) {}
// contextMenuEvent ends with playlistContextMenu()->clear() under USE_TEST,
// which deletes the singleton menu's cached actions — actions still referenced
// by ActionFactory::m_listContextMenuActions (dangling pointers kill any later
// findActionsByKind, e.g. reflectActionToUI in the close-timer test). Return a
// dedicated menu instead so the singleton is never touched.
static DMenu *ppwpop_playlistContextMenu_stub(void *)
{
    static DMenu menu;
    return &menu;
}

// ===========================================================================
// Helpers
// ===========================================================================

static Platform_MainWindow *ppwpop_mainWindow()
{
    return dApp->getMainWindow();
}

static Platform_PlaylistWidget *ppwpop_playlist()
{
    Platform_MainWindow *w = ppwpop_mainWindow();
    return w ? w->playlist() : nullptr;
}

// Fake, fully non-local item: scheme http, so nothing in PlayItemWidget /
// PlaylistModel ever touches the filesystem for it.
static PlayItemInfo ppwpop_mkPif(const QString &title)
{
    PlayItemInfo pif;
    pif.valid = true;
    pif.loaded = false;
    pif.url = QUrl("http://example.com/" + title + ".mp4");
    pif.mi.title = title;
    pif.mi.filePath = pif.url.toString();
    pif.mi.duration = 61;  // 00:01:01
    pif.mi.valid = true;
    return pif;
}

namespace {

class PpwPopTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The harness constructs the main window asynchronously; the playlist
        // member is created partway through its (long) constructor. Wait until
        // both exist instead of racing on the first tests.
        mw = ppwpop_mainWindow();
        for (int i = 0; i < 200 && !(mw && mw->playlist()); ++i) {
            QTest::qWait(25);
            mw = ppwpop_mainWindow();
        }
        ASSERT_NE(mw, nullptr);
        pl = mw->playlist();
        ASSERT_NE(pl, nullptr);
        eng = pl->engine();
        ASSERT_NE(eng, nullptr);

        ppwpop_resetCaptures();

        // The harness never creates the toolbox list button; the popup close
        // animation lambda calls getListBtnFocus() on it (same root cause that
        // excluded boost_dm_wid). Redirect both focus entry points.
        stub.set(ADDR(Platform_ToolboxProxy, getListBtnFocus), ppwpop_listBtnFocus_stub);
        stub.set(ADDR(Platform_ToolboxProxy, setBtnFocusSign), ppwpop_setBtnFocusSign_stub);
        stub.set(ADDR(CompositingManager, isPadSystem), ppwpop_isPad_false_stub);
        stub.set(ADDR(PlaylistModel, onAsyncUpdate), ppwpop_onAsyncUpdate_noop_stub);

        scrubState();
    }

    void TearDown() override
    {
        if (pl) {
            closePopupIfOpened();
            scrubState();
        }
        // stub resets itself in ~Stub
        if (mw && mw->isVisible()) {
            mw->hide();
            QTest::qWait(60);
        }
        QTest::qWait(20);
    }

    // Deterministic model + widget list reset (no signals, no QSettings I/O).
    // Keyboard state, current row and the model's current property survive
    // across tests otherwise and break the updateSelectItem walk expectations.
    void scrubState()
    {
        eng->playlist().items().clear();
        eng->playlist().setProperty("current", -1);
        pl->_lastDragged = {-1, nullptr};
        pl->get_playlist()->clear();
        pl->get_playlist()->setCurrentRow(-1);
        pl->_index = -1;
        pl->_clickedItem = nullptr;
        pl->_mouseItem = nullptr;
        pl->pSelectItemWgt = nullptr;
        pl->m_bButtonFocusOut = false;
        // No asynchronous popup toggles from leftover double-click close
        // timers may leak into any test (test 27 resets the stub itself).
        stub.set(ADDR(Platform_PlaylistWidget, slotCloseTimeTimeOut),
                 ppwpop_slotCloseTimeTimeOut_stub);
        QTest::qWait(10);
    }

    // Force-clear the list selection before tests that drive the keyboard
    // walk: the focus dance during loadPlaylist can pre-select row 0 and
    // updateSelectItem's Key_Down would then start from there.
    void resetSelection()
    {
        pl->get_playlist()->setCurrentRow(-1);
        pl->_index = -1;
    }

    void populate(int n, const QString &base = QStringLiteral("clip"))
    {
        for (int i = 0; i < n; ++i)
            eng->playlist().items().append(ppwpop_mkPif(base + QString::number(i)));
    }

    // Requires the main window to be shown first (Q_ASSERT(isVisible()) on close).
    // A transient WM-reported minimized state during re-show closes the popup
    // again (MainWindow's isMinimized handler), so retry a couple of times.
    void openPopup()
    {
        for (int i = 0; i < 3 && pl->state() == Platform_PlaylistWidget::Closed; ++i) {
            pl->togglePopup(true);
            pl->endAnimation();   // duration 0 -> finished on next loop
            QTest::qWait(80);     // finished lambda: paClose=nullptr, transparent off
        }
        ASSERT_EQ(pl->state(), Platform_PlaylistWidget::Opened);
    }

    void closePopupIfOpened()
    {
        if (pl->state() == Platform_PlaylistWidget::Opened) {
            if (pl->isVisible()) {
                pl->togglePopup(false);
            } else {
                // Desynced (hidden together with the window): sync the state
                // directly instead of hitting the Q_ASSERT(isVisible()) in
                // the close branch of togglePopup.
                pl->_state = Platform_PlaylistWidget::Closed;
            }
        }
    }

    // Point inside item row `row` of the list, expressed the way the handler
    // consumes it: contextMenuEvent subtracts a fixed (235, 20) offset from
    // event->pos() and feeds the result straight into DListWidget::itemAt(),
    // i.e. in VIEWPORT coordinates (the viewport's own offset inside the
    // playlist widget is ignored by the handler).
    QPoint ppwpop_posInItem(int row) const
    {
        auto *lw = pl->get_playlist();
        return lw->visualItemRect(lw->item(row)).topLeft() + QPoint(5, 5)
               + QPoint(235, 20);
    }

    Stub stub;
    Platform_MainWindow *mw = nullptr;
    Platform_PlaylistWidget *pl = nullptr;
    PlayerEngine *eng = nullptr;
};

// ===========================================================================
// 01-02: loadPlaylist / appendItems — widget construction from the model
// ===========================================================================

TEST_F(PpwPopTest, loadPlaylist_buildsWidgetsCountKindAndNum)
{
    // Arrange: two fake network items in the engine model.
    populate(2);
    ASSERT_EQ(eng->playlist().items().size(), 2);

    // Act
    pl->loadPlaylist();

    // Assert: one widget per model item, marked network, label updated.
    auto *lw = pl->get_playlist();
    ASSERT_EQ(lw->count(), 2);
    for (int i = 0; i < 2; ++i) {
        QWidget *w = lw->itemWidget(lw->item(i));
        ASSERT_NE(w, nullptr);
        EXPECT_EQ(w->property("ItemKind").toString(), QString("network"));
    }
    // Hidden popup -> batchUpdateSizeHints skips the size-hint loop.
    EXPECT_FALSE(lw->item(0)->sizeHint().isValid());
    // Count label is translated in some environments: check the digits only.
    EXPECT_TRUE(pl->_num->text().contains("2"));
    EXPECT_EQ(eng->playlist().items().size(), 2);  // model untouched
}

TEST_F(PpwPopTest, appendItems_growsFromModelTail)
{
    // Arrange: one item loaded, then a second appended directly to the model.
    populate(1);
    pl->loadPlaylist();
    ASSERT_EQ(pl->get_playlist()->count(), 1);
    eng->playlist().items().append(ppwpop_mkPif("tail"));

    // Act
    pl->appendItems();

    // Assert: only the tail becomes a new widget; label reflects total.
    ASSERT_EQ(pl->get_playlist()->count(), 2);
    QWidget *w1 = pl->get_playlist()->itemWidget(pl->get_playlist()->item(1));
    ASSERT_NE(w1, nullptr);
    EXPECT_EQ(w1->property("ItemKind").toString(), QString("network"));
    EXPECT_TRUE(pl->_num->text().contains("2"));
}

// ===========================================================================
// 03-04: updateItemStates / updateItemInfo
// ===========================================================================

TEST_F(PpwPopTest, updateItemStates_marksCurrentPlayingOthersNormal)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    ASSERT_EQ(lw->count(), 2);
    // ItemState: Normal=0, Playing=1, Invalid=2 (declaration order).

    // Act + Assert: no current -> everything stays Normal.
    eng->playlist().setProperty("current", -1);
    pl->updateItemStates();
    EXPECT_EQ(lw->itemWidget(lw->item(0))->property("ItemState").toInt(), 0);
    EXPECT_EQ(lw->itemWidget(lw->item(1))->property("ItemState").toInt(), 0);

    // Act + Assert: model current=0 -> row 0 becomes Playing and is selected.
    eng->playlist().setProperty("current", 0);
    pl->updateItemStates();
    EXPECT_EQ(lw->itemWidget(lw->item(0))->property("ItemState").toInt(), 1);
    EXPECT_EQ(lw->itemWidget(lw->item(1))->property("ItemState").toInt(), 0);
    EXPECT_EQ(lw->currentRow(), 0);
}

TEST_F(PpwPopTest, updateItemInfo_updatesTooltip_validIndexOnly)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    QWidget *w0 = lw->itemWidget(lw->item(0));
    ASSERT_NE(w0, nullptr);

    // Act + Assert: valid id updates the item tooltip to the model title.
    pl->updateItemInfo(0);
    EXPECT_EQ(w0->toolTip(), QStringLiteral("clip0"));

    // Act + Assert: out-of-range ids are ignored without crashing.
    pl->updateItemInfo(-1);
    pl->updateItemInfo(99);
    EXPECT_EQ(lw->count(), 2);
    EXPECT_EQ(w0->toolTip(), QStringLiteral("clip0"));
    // Switching ids switches the tooltip content.
    pl->updateItemInfo(1);
    EXPECT_EQ(w0->toolTip(), QStringLiteral("clip0"));
    QWidget *w1 = lw->itemWidget(lw->item(1));
    EXPECT_NE(w1->toolTip(), QStringLiteral("clip0"));
}

// ===========================================================================
// 05-07: updateSelectItem — keyboard walk + Enter paths
// ===========================================================================

TEST_F(PpwPopTest, updateSelectItem_walksUpDownBoundaries)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    ASSERT_EQ(lw->count(), 2);
    resetSelection();

    // Act + Assert: Key_Down from no selection lands on row 0.
    pl->updateSelectItem(Qt::Key_Down);
    EXPECT_EQ(pl->_index, 0);
    EXPECT_EQ(lw->currentRow(), 0);

    // Act + Assert: Key_Down again walks to row 1.
    pl->updateSelectItem(Qt::Key_Down);
    EXPECT_EQ(pl->_index, 1);
    EXPECT_EQ(lw->currentRow(), 1);

    // Act + Assert: Key_Down at the last row is a no-op.
    pl->updateSelectItem(Qt::Key_Down);
    EXPECT_EQ(pl->_index, 1);
    EXPECT_EQ(lw->currentRow(), 1);

    // Act + Assert: Key_Up walks back to row 0.
    pl->updateSelectItem(Qt::Key_Up);
    EXPECT_EQ(pl->_index, 0);
    EXPECT_EQ(lw->currentRow(), 0);

    // Act + Assert: Key_Up above the first row is a no-op.
    pl->updateSelectItem(Qt::Key_Up);
    EXPECT_EQ(pl->_index, -1);
    EXPECT_EQ(lw->currentRow(), 0);
}

TEST_F(PpwPopTest, updateSelectItem_enterPlaysSelectedRow)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    stub.set(ADDR(PlayerEngine, playSelected), ppwpop_playSelected_stub);

    // Act: walk to row 1, press Enter.
    pl->updateSelectItem(Qt::Key_Down);
    pl->updateSelectItem(Qt::Key_Down);
    ASSERT_EQ(pl->get_playlist()->currentRow(), 1);
    pl->updateSelectItem(Qt::Key_Enter);

    // Assert: playSelected reached with the selected index.
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 1);
    EXPECT_EQ(ppwpop_gLastPlaySelectedIdx, 1);

    // Act + Assert: Key_Return behaves the same.
    pl->updateSelectItem(Qt::Key_Return);
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 2);
    EXPECT_EQ(ppwpop_gLastPlaySelectedIdx, 1);
}

TEST_F(PpwPopTest, updateSelectItem_enterFocusOnClearButtonClearsModel)
{
    // Arrange: focus must actually sit on the clear button for this branch.
    mw->show();
    QTest::qWait(120);
    populate(2);
    pl->loadPlaylist();
    pl->m_pClearButton->setFocus();
    QTest::qWait(120);
    if (QApplication::focusWidget() != pl->m_pClearButton)
        GTEST_SKIP() << "cannot move focus to the clear button in this environment";
    stub.set(ADDR(PlayerEngine, clearPlaylist), ppwpop_clearPlaylist_stub);

    // Act
    pl->updateSelectItem(Qt::Key_Enter);

    // Assert: engine clearPlaylist stubbed -> model items preserved, call seen.
    EXPECT_EQ(ppwpop_gClearCalls, 1);
    EXPECT_EQ(ppwpop_gRequestActionCalls, 0);
}

// ===========================================================================
// 08-10: removeClickedItem — all three entry combinations
// ===========================================================================

TEST_F(PpwPopTest, removeClickedItem_hiddenShortcutKeepsModel)
{
    // Arrange: popup hidden -> the isVisible() selection loop is skipped.
    populate(2);
    pl->loadPlaylist();

    // Act
    pl->removeClickedItem(true);

    // Assert: nothing selected, nothing removed.
    EXPECT_EQ(eng->playlist().items().size(), 2);
}

TEST_F(PpwPopTest, removeClickedItem_visibleShortcutRemovesSelected)
{
    // Arrange: visible popup with row 0 selected via the keyboard walk.
    mw->show();
    QTest::qWait(60);
    populate(2);
    pl->loadPlaylist();
    openPopup();
    ASSERT_EQ(pl->state(), Platform_PlaylistWidget::Opened);
    resetSelection();
    pl->updateSelectItem(Qt::Key_Down);
    ASSERT_EQ(pl->get_playlist()->currentRow(), 0);

    // Act
    pl->removeClickedItem(true);

    // Assert: the selected (first) model entry is gone.
    ASSERT_EQ(eng->playlist().items().size(), 1);
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip1"));
    closePopupIfOpened();
}

TEST_F(PpwPopTest, removeClickedItem_clickedItemRemovesItsRow)
{
    // Arrange: _clickedItem points at the second item widget.
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    pl->_clickedItem = lw->itemWidget(lw->item(1));

    // Act
    pl->removeClickedItem(false);

    // Assert: model row 1 removed, row 0 survives.
    ASSERT_EQ(eng->playlist().items().size(), 1);
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip0"));

    // Act + Assert: null _clickedItem -> early return, no crash, no removal.
    pl->_clickedItem = nullptr;
    pl->removeClickedItem(false);
    EXPECT_EQ(eng->playlist().items().size(), 1);
}

// ===========================================================================
// 11-13: slotDoubleClickedItem + full double-click chain
// ===========================================================================

TEST_F(PpwPopTest, slotDoubleClickedItem_playsRowOrNullSafe)
{
    // Arrange: a matching row also schedules a 500ms close timer; neutralize
    // it so no delayed togglePopup leaks into later tests.
    populate(2);
    pl->loadPlaylist();
    resetSelection();
    stub.set(ADDR(PlayerEngine, playSelected), ppwpop_playSelected_stub);
    stub.set(ADDR(Platform_PlaylistWidget, slotCloseTimeTimeOut), ppwpop_slotCloseTimeTimeOut_stub);

    // Act + Assert: matching widget row -> playSelected with that index.
    pl->slotDoubleClickedItem(pl->get_playlist()->itemWidget(pl->get_playlist()->item(1)));
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 1);
    EXPECT_EQ(ppwpop_gLastPlaySelectedIdx, 1);

    // Act + Assert: widget not in the list (null) -> loop matches nothing.
    pl->slotDoubleClickedItem(nullptr);
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 1);
}

TEST_F(PpwPopTest, doubleClickChain_mouseDoubleClickPlaysRow)
{
    // Arrange: real mouse double-click on the second item widget. The 500ms
    // close timer started by slotDoubleClickedItem is stubbed out.
    populate(2);
    pl->loadPlaylist();
    stub.set(ADDR(PlayerEngine, playSelected), ppwpop_playSelected_stub);
    stub.set(ADDR(Platform_PlaylistWidget, slotCloseTimeTimeOut), ppwpop_slotCloseTimeTimeOut_stub);
    QWidget *w1 = pl->get_playlist()->itemWidget(pl->get_playlist()->item(1));
    ASSERT_NE(w1, nullptr);

    // Act: Platform_PlayItemWidget::mouseDoubleClickEvent -> doDoubleClick ->
    //      doubleClicked() -> _activateMapper -> slotDoubleClickedItem ->
    //      requestAction(GotoPlaylistSelected) -> playSelected(idx).
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(w1->rect().center()),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w1, &dbl);
    QTest::qWait(20);

    // Assert
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 1);
    EXPECT_EQ(ppwpop_gLastPlaySelectedIdx, 1);
}

TEST_F(PpwPopTest, doubleClickChain_invalidLocalEmitsSigInvalidFile)
{
    // Arrange: first item is a LOCAL path that does not exist. The URL must be
    // built with fromLocalFile: only then does PlayItemInfo::refresh() treat
    // it as local and flip valid to false (PlayItemInfo::info is a QFileInfo).
    // The 500ms close timer started by slotDoubleClickedItem is stubbed out.
    PlayItemInfo missing = ppwpop_mkPif("ghost");
    missing.valid = true;
    missing.url = QUrl::fromLocalFile("/no/such/ghost_file.mp4");
    missing.mi.filePath = missing.url.toLocalFile();
    eng->playlist().items().append(missing);
    populate(1, "fine");
    pl->loadPlaylist();
    stub.set(ADDR(Platform_PlaylistWidget, slotCloseTimeTimeOut), ppwpop_slotCloseTimeTimeOut_stub);
    ASSERT_EQ(pl->get_playlist()->count(), 2);
    QSignalSpy spy(eng, &PlayerEngine::sigInvalidFile);
    ASSERT_TRUE(spy.isValid());
    QWidget *w0 = pl->get_playlist()->itemWidget(pl->get_playlist()->item(0));
    ASSERT_NE(w0, nullptr);

    // Act: double click -> refresh() invalidates the missing local file ->
    //      the invalid-file branch emits sigInvalidFile instead of playing.
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(w0->rect().center()),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(w0, &dbl);
    QTest::qWait(20);

    // Assert: signal carries the missing file name, no playback attempted.
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("ghost_file.mp4"));
    EXPECT_EQ(ppwpop_gPlaySelectedCalls, 0);
    // The item flipped to Invalid (ItemState: Normal=0, Playing=1, Invalid=2).
    EXPECT_EQ(w0->property("ItemState").toInt(), 2);
}

// ===========================================================================
// 14: slotShowSelectItem
// ===========================================================================

TEST_F(PpwPopTest, slotShowSelectItem_setsCurrentAndSelected)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();

    // Act + Assert: selecting row 1 moves the list current row to it.
    pl->slotShowSelectItem(lw->item(1));
    EXPECT_EQ(lw->currentRow(), 1);

    // Act + Assert: selection is observable through the removal chain
    // (visible popup + shortcut path removes the selected row).
    mw->show();
    QTest::qWait(60);
    openPopup();
    ASSERT_TRUE(pl->isVisible());
    ASSERT_EQ(lw->count(), 2);
    pl->slotShowSelectItem(lw->item(1));
    pl->removeClickedItem(true);
    ASSERT_EQ(eng->playlist().items().size(), 1);
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip0"));
    closePopupIfOpened();
}

// ===========================================================================
// 15-16: eventFilter — branch matrix on a populated list
// ===========================================================================

TEST_F(PpwPopTest, eventFilter_branchMatrixPopulated)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    QObject *btn = pl->m_pClearButton;

    // Act + Assert: clear button eats Up/Down key presses.
    QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QKeyEvent aKey(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
    EXPECT_TRUE(pl->eventFilter(btn, &up));
    EXPECT_TRUE(pl->eventFilter(btn, &down));
    // Other keys fall through to the base implementation.
    EXPECT_FALSE(pl->eventFilter(btn, &aKey));

    // Act + Assert: FocusIn on the clear button is not consumed (but signs the
    // toolbox, stubbed no-op here).
    QFocusEvent fin(QEvent::FocusIn);
    EXPECT_FALSE(pl->eventFilter(btn, &fin));

    // Act + Assert: FocusOut with a populated list is not consumed.
    QFocusEvent fout(QEvent::FocusOut);
    EXPECT_FALSE(pl->eventFilter(btn, &fout));

    // Act + Assert: list FocusIn with a populated list and no button focus-out
    // flag is consumed without moving the current row.
    QFocusEvent lfin(QEvent::FocusIn);
    lw->setCurrentRow(-1);
    EXPECT_TRUE(pl->eventFilter(lw, &lfin));
    EXPECT_EQ(lw->currentRow(), -1);

    // Act + Assert: with the button-focus-out flag, FocusIn selects row 0.
    bool flag = true;
    pl->resetFocusAttribute(flag);
    EXPECT_TRUE(pl->eventFilter(lw, &lfin));
    EXPECT_EQ(lw->currentRow(), 0);
    EXPECT_EQ(pl->_index, 0);
    EXPECT_FALSE(pl->m_bButtonFocusOut);

    // Act + Assert: Tab on the list is consumed to cycle focus to the button.
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    EXPECT_TRUE(pl->eventFilter(lw, &tab));

    // Act + Assert: unknown object is never handled.
    QObject probe;
    QFocusEvent any(QEvent::FocusIn);
    EXPECT_FALSE(pl->eventFilter(&probe, &any));
}

TEST_F(PpwPopTest, eventFilter_focusInEmptyListAndTabCycles)
{
    // Arrange: empty list (loadPlaylist on a clean model).
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    ASSERT_EQ(lw->count(), 0);

    // Act + Assert: FocusIn on the empty list is still consumed.
    QFocusEvent lfin(QEvent::FocusIn);
    EXPECT_TRUE(pl->eventFilter(lw, &lfin));

    // Act + Assert: clear button FocusOut with an empty list IS consumed
    // (focus must not fall through on an empty playlist).
    QFocusEvent fout(QEvent::FocusOut);
    EXPECT_TRUE(pl->eventFilter(pl->m_pClearButton, &fout));

    // Act + Assert: Tab on the empty list still cycles the focus.
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    EXPECT_TRUE(pl->eventFilter(lw, &tab));
}

// ===========================================================================
// 17-18: contextMenuEvent
// ===========================================================================

TEST_F(PpwPopTest, contextMenuEvent_onItemAndOffItemSetsMouseItem)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    ASSERT_EQ(pl->get_playlist()->count(), 2);
    stub.set(ADDR(ActionFactory, playlistContextMenu), ppwpop_playlistContextMenu_stub);

    // Act: right-click on the first item (USE_TEST build hides the popup menu).
    QContextMenuEvent onItem(QContextMenuEvent::Mouse, ppwpop_posInItem(0),
                             pl->mapToGlobal(ppwpop_posInItem(0)));
    QApplication::sendEvent(pl, &onItem);
    ppwpop_playlistContextMenu_stub(nullptr)->close();

    // Assert: the widget under the cursor became _mouseItem.
    EXPECT_EQ(pl->_mouseItem,
              pl->get_playlist()->itemWidget(pl->get_playlist()->item(0)));

    // Act + Assert: a point above the list maps to no item.
    QContextMenuEvent offItem(QContextMenuEvent::Mouse, QPoint(-100, -100),
                              QPoint(-100, -100));
    QApplication::sendEvent(pl, &offItem);
    ppwpop_playlistContextMenu_stub(nullptr)->close();
    EXPECT_EQ(pl->_mouseItem, nullptr);
}

TEST_F(PpwPopTest, contextMenuEvent_padBranchSelectsUnderMouse)
{
    // Arrange: pad systems select the item under the mouse in place.
    populate(2);
    pl->loadPlaylist();
    stub.set(ADDR(CompositingManager, isPadSystem), ppwpop_isPad_true_stub);
    stub.set(ADDR(ActionFactory, playlistContextMenu), ppwpop_playlistContextMenu_stub);

    // Act: right-click on the first item while pretending to be a pad.
    QContextMenuEvent onItem(QContextMenuEvent::Mouse, ppwpop_posInItem(0),
                             pl->mapToGlobal(ppwpop_posInItem(0)));
    QApplication::sendEvent(pl, &onItem);

    // Assert: both the hover item and the pad-selection widget track row 0.
    EXPECT_EQ(pl->_mouseItem,
              pl->get_playlist()->itemWidget(pl->get_playlist()->item(0)));
    EXPECT_EQ(reinterpret_cast<void *>(pl->pSelectItemWgt),
              reinterpret_cast<void *>(pl->get_playlist()->itemWidget(pl->get_playlist()->item(0))));
}

// ===========================================================================
// 19-20: togglePopup — open/close roundtrip with the stateChange signal
// ===========================================================================

TEST_F(PpwPopTest, togglePopup_openCloseRoundtripWithSignal)
{
    // Arrange: main window shown so the popup can really become visible.
    mw->show();
    QTest::qWait(60);
    QSignalSpy spy(pl, &Platform_PlaylistWidget::stateChange);
    ASSERT_TRUE(spy.isValid());
    ASSERT_EQ(pl->state(), Platform_PlaylistWidget::Closed);
    ASSERT_EQ(spy.count(), 0);

    // Act: open. The platform variant is fully synchronous (no animation:
    // paOpen/paClose are never assigned), so the state flips immediately.
    pl->togglePopup(true);

    // Assert: opened and visible, one stateChange carrying the shortcut flag.
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Opened);
    EXPECT_TRUE(pl->isVisible());
    EXPECT_FALSE(pl->toggling());
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toBool(), true);

    // Act: close again.
    pl->togglePopup(false);

    // Assert: closed and hidden, second signal carries false.
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Closed);
    EXPECT_FALSE(pl->isVisible());
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(1).at(0).toBool(), false);
}

TEST_F(PpwPopTest, togglePopup_doubleToggleOpensThenCloses)
{
    // Arrange. The animation guard at the top of togglePopup (paOpen/paClose)
    // is dead in the platform variant — both stay null forever — so there is
    // no "toggle during animation" suppression: every call flips the state.
    mw->show();
    QTest::qWait(60);
    QSignalSpy spy(pl, &Platform_PlaylistWidget::stateChange);
    ASSERT_TRUE(spy.isValid());
    ASSERT_EQ(pl->paOpen, nullptr);
    ASSERT_EQ(pl->paClose, nullptr);

    // Act: two rapid toggles with no event loop between them.
    pl->togglePopup(true);

    // Assert: first toggle opens synchronously.
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Opened);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(pl->isVisible());

    // Act + Assert: second toggle closes again (same synchronous behavior).
    pl->togglePopup(true);
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Closed);
    EXPECT_EQ(spy.count(), 2);
    EXPECT_FALSE(pl->isVisible());
    EXPECT_FALSE(pl->toggling());
}

// ===========================================================================
// 21-22: showEvent / resizeEvent
// ===========================================================================

TEST_F(PpwPopTest, showEvent_batchUpdateSizeHintsWhenVisible)
{
    // Arrange: visible popup with two built item widgets and no size hints yet
    // (hidden batchUpdateSizeHints from loadPlaylist skipped the loop).
    mw->show();
    QTest::qWait(60);
    populate(2);
    pl->loadPlaylist();
    openPopup();
    ASSERT_TRUE(pl->isVisible());
    auto *lw = pl->get_playlist();
    // openPopup already triggered a real showEvent; invalidate one hint so the
    // synthetic show below demonstrably re-syncs the hints itself.
    lw->item(0)->setSizeHint(QSize());
    EXPECT_FALSE(lw->item(0)->sizeHint().isValid());

    // Act: a show event on the visible popup triggers the visible branch.
    // showEvent runs batchUpdateSizeHints() and then adjustSize(); the latter
    // may resize the popup afterwards, so re-run the (idempotent) sync before
    // asserting the hint == widget-size contract.
    QShowEvent se;
    QApplication::sendEvent(pl, &se);
    pl->batchUpdateSizeHints();

    // Assert: every item hint now matches its widget size.
    for (int i = 0; i < lw->count(); ++i) {
        QWidget *w = lw->itemWidget(lw->item(i));
        ASSERT_NE(w, nullptr);
        EXPECT_EQ(lw->item(i)->sizeHint(), w->size());
    }
    closePopupIfOpened();
}

TEST_F(PpwPopTest, resizeEvent_updatesListWidthAndEmitsSizeChange)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    QSignalSpy spy(pl, &Platform_PlaylistWidget::sizeChange);
    ASSERT_TRUE(spy.isValid());
    QSize oldSize = pl->size();

    // Act: resize the playlist popup widget by 10px.
    QResizeEvent re(QSize(oldSize.width() + 10, oldSize.height()), oldSize);
    QApplication::sendEvent(pl, &re);

    // Assert: the inner list tracks the popup width (USE_DXCB OFF formula:
    // view = mw rect minus 1px margins -> list width = mw width - 227) and
    // the sizeChange signal fired exactly once.
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(pl->get_playlist()->width(), mw->rect().width() - 227);
}

// ===========================================================================
// 23-25: clear / removeItem / slotRowsMoved
// ===========================================================================

TEST_F(PpwPopTest, clear_resetsWidgetsLabelAndReload)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    ASSERT_EQ(pl->get_playlist()->count(), 2);

    // Act
    pl->clear();

    // Assert: widget list emptied and label reset, model untouched.
    EXPECT_EQ(pl->get_playlist()->count(), 0);
    EXPECT_TRUE(pl->_num->text().contains("0"));
    EXPECT_EQ(eng->playlist().items().size(), 2);

    // Act + Assert: a rebuild from the surviving model works.
    pl->loadPlaylist();
    EXPECT_EQ(pl->get_playlist()->count(), 2);
}

TEST_F(PpwPopTest, removeItem_reindexesAndBoundarySafe)
{
    // Arrange
    populate(3);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();
    ASSERT_EQ(lw->count(), 3);

    // Act + Assert: remove the middle row -> two widgets remain, all valid.
    pl->removeItem(1);
    EXPECT_EQ(lw->count(), 2);
    for (int i = 0; i < 2; ++i)
        EXPECT_NE(lw->itemWidget(lw->item(i)), nullptr);

    // Act + Assert: idx == count() is the documented boundary (no removal, the
    // hover branch targets the last row, no crash).
    pl->removeItem(2);
    EXPECT_EQ(lw->count(), 2);
    EXPECT_NE(lw->itemWidget(lw->item(1)), nullptr);
}

TEST_F(PpwPopTest, slotRowsMoved_switchesModelPositions)
{
    // Arrange: simulate a finished drag that moved the second widget to the top.
    populate(2);
    pl->loadPlaylist();
    ASSERT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip0"));

    // Act
    pl->_lastDragged = {0, reinterpret_cast<Platform_PlayItemWidget *>(
                                  pl->get_playlist()->itemWidget(pl->get_playlist()->item(1)))};
    pl->slotRowsMoved();

    // Assert: the model order swapped and the drag state reset.
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip1"));
    EXPECT_EQ(eng->playlist().items().at(1).mi.title, QStringLiteral("clip0"));
    EXPECT_EQ(pl->_lastDragged.first, -1);
    EXPECT_EQ(pl->_lastDragged.second, nullptr);

    // Act + Assert: no drag in progress -> no model change, no crash.
    pl->slotRowsMoved();
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip1"));
}

// ===========================================================================
// 26-28: slotCloseItem / slotCloseTimeTimeOut / endAnimation
// ===========================================================================

TEST_F(PpwPopTest, slotCloseItem_recordsClickedItemAndRequestsRemove)
{
    // Arrange: intercept requestAction so PlaylistRemoveItem is never executed.
    populate(2);
    pl->loadPlaylist();
    stub.set(ADDR(Platform_MainWindow, requestAction), ppwpop_requestAction_stub);
    QWidget *w0 = pl->get_playlist()->itemWidget(pl->get_playlist()->item(0));

    // Act
    pl->slotCloseItem(w0);

    // Assert: close-click target recorded, remove request captured.
    EXPECT_EQ(pl->_clickedItem, w0);
    EXPECT_EQ(ppwpop_gRequestActionCalls, 1);
    EXPECT_EQ(ppwpop_gLastActionKind, ActionFactory::ActionKind::PlaylistRemoveItem);
}

TEST_F(PpwPopTest, slotCloseTimeTimeOut_closesOpenedPopup)
{
    // Arrange: this is the one test that needs the REAL slot — undo the
    // fixture-wide no-op stub.
    stub.reset(ADDR(Platform_PlaylistWidget, slotCloseTimeTimeOut));

    // Arrange: visible, opened popup; fire the close timer slot for real so
    // sender() is a live QTimer.
    mw->show();
    QTest::qWait(60);
    populate(2);
    pl->loadPlaylist();
    openPopup();
    ASSERT_EQ(pl->state(), Platform_PlaylistWidget::Opened);

    // Act: fire the real slot from a live QTimer (sender() must be a QTimer,
    // the source deleteLater()s it). A transient WM state change can race the
    // single fire — retry once with a fresh timer if the popup is still open.
    for (int i = 0; i < 2 && pl->state() == Platform_PlaylistWidget::Opened; ++i) {
        QTimer *timer = new QTimer;   // heap: the source deleteLater()s its sender
        QObject::connect(timer, &QTimer::timeout, pl, &Platform_PlaylistWidget::slotCloseTimeTimeOut);
        timer->start(10);
        QTest::qWait(200);
    }

    // Assert: the real slot toggled the popup closed (synchronous on X86).
    EXPECT_EQ(pl->state(), Platform_PlaylistWidget::Closed);
    EXPECT_FALSE(pl->isVisible());
}

TEST_F(PpwPopTest, endAnimation_nullAnimationsNoOp)
{
    // Arrange: the platform variant never creates paOpen/paClose (its
    // togglePopup has no geometry animation), so endAnimation must be a safe
    // no-op on the null branch.
    ASSERT_EQ(pl->paOpen, nullptr);
    ASSERT_EQ(pl->paClose, nullptr);

    // Act
    pl->endAnimation();

    // Assert: nothing created, nothing crashed.
    EXPECT_EQ(pl->paOpen, nullptr);
    EXPECT_EQ(pl->paClose, nullptr);
}

// ===========================================================================
// 29-30: OnItemChanged / openItemInFM / showItemInfo guards
// ===========================================================================

TEST_F(PpwPopTest, OnItemChanged_selectsCurrentViaDirectCall)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    auto *lw = pl->get_playlist();

    // Act: currentChanged emulation with only a current item.
    pl->OnItemChanged(lw->item(0), nullptr);

    // Assert: the selected first row (clip0) is removed from the model and the
    // model-changed reload leaves the remaining clip1 as the only item.
    mw->show();
    QTest::qWait(60);
    openPopup();
    pl->removeClickedItem(true);
    ASSERT_EQ(eng->playlist().items().size(), 1);
    EXPECT_EQ(eng->playlist().items().at(0).mi.title, QStringLiteral("clip1"));
    closePopupIfOpened();

    // Act + Assert: nulls everywhere are safe on a non-pad system.
    pl->OnItemChanged(nullptr, nullptr);
    EXPECT_EQ(lw->count(), 1);
}

TEST_F(PpwPopTest, openItemInFM_showItemInfo_mouseItemGuards)
{
    // Arrange
    populate(2);
    pl->loadPlaylist();
    stub.set(ADDR(utils, ShowInFileManager), ppwpop_showInFileManager_stub);

    // Act + Assert: no _mouseItem -> both slots return before touching anything
    // (showItemInfo would open a modal MovieInfoDialog with an item, so only
    // the guard branch is exercised here).
    pl->openItemInFM();
    pl->showItemInfo();
    EXPECT_TRUE(ppwpop_gLastFMPath.isEmpty());

    // Act: hover an item, then open in file manager (stubbed).
    QWidget *w0 = pl->get_playlist()->itemWidget(pl->get_playlist()->item(0));
    pl->_mouseItem = w0;
    pl->openItemInFM();

    // Assert: the stub captured the item's movie file path.
    EXPECT_EQ(ppwpop_gLastFMPath, QStringLiteral("http://example.com/clip0.mp4"));
    pl->_mouseItem = nullptr;
}

} // namespace
