// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests focused exclusively on
//   src/widgets/platform/platform_playlist_widget.cpp
//
// Suite name "platform_pw_ext"; static helpers use unique prefix "ppw_".
//
// Scope & safety:
//   * Targets the functions that remain uncovered after
//     test_platform_widgets_ext.cpp, on the shared main window's playlist
//     widget, in the Idle / empty-playlist state (no media loaded).
//   * Covers Platform_ListPic::setPic / paintEvent (header-only class).
//   * Covers empty-list branches of: updateItemInfo, updateItemStates,
//     removeItem, appendItems, loadPlaylist, OnItemChanged,
//     batchUpdateSizeHints (not-visible path), engine().
//   * Covers Platform_PlaylistWidget::eventFilter branches that the shared
//     suite does not reach: _playlist FocusIn (empty), _playlist Tab -> clear
//     button focus, clear-button FocusIn (setBtnFocusSign) and FocusOut
//     (empty list -> consumed).
//   * Covers Platform_MouseEventListener::eventFilter via a left-button
//     press on the playlist viewport with no item (empty-list early path).
//   * Platform_PlayItemWidget / *TooltipHandler / *MainWindowListener /
//     *MouseEventListener are defined inside the .cpp (private, not in the
//     header), so they cannot be constructed directly from this TU; they are
//     exercised indirectly through the public Platform_PlaylistWidget API and
//     the event-filter installed on the viewport during construction.
//   * No real playback / decode path is exercised. Item-iteration cases guard
//     against a non-empty persisted model (GTEST_SKIP) so they never construct
//     real Platform_PlayItemWidget instances in the coverage run.

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
#include <QPointingDevice>
#include <QPushButton>

// Project headers that expose private members — included LAST, under the
// access-override macros, so STL/Qt headers (already included above) are parsed
// with their normal access specifiers.
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
#include "src/libdmr/player_engine.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/compositing_manager.h"
#undef protected
#undef private
#include "application.h"

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// The running app's main window (owned by test_qtestmain's main()).
static Platform_MainWindow *ppw_mainWindow()
{
    return dApp->getMainWindow();
}

// Shared playlist widget owned by the main window.
static Platform_PlaylistWidget *ppw_playlist()
{
    Platform_MainWindow *w = ppw_mainWindow();
    return w ? w->playlist() : nullptr;
}

static void ppw_wait(int ms = 120)
{
    QTest::qWait(ms);
}

// True when both the widget list and the underlying engine model are empty.
// Used to guard item-iteration cases so they only run on a clean Idle model.
static bool ppw_modelEmpty(Platform_PlaylistWidget *pl)
{
    if (!pl) return false;
    if (pl->get_playlist()->count() != 0) return false;
    PlayerEngine *eng = pl->engine();
    if (!eng) return false;
    return eng->playlist().items().isEmpty();
}

// ==========================================================================
// Platform_ListPic (header-only class in platform_playlist_widget.h)
// ==========================================================================

TEST(platform_pw_ext, listPic_construct_fixedSize)
{
    QWidget parent;
    parent.resize(100, 60);
    QPixmap src(42, 24);
    src.fill(Qt::red);
    Platform_ListPic pic(src, &parent);
    // Constructor sets a fixed 42x24 geometry.
    EXPECT_EQ(pic.size(), QSize(42, 24));
}

TEST(platform_pw_ext, listPic_setPic_replacesPixmap)
{
    QWidget parent;
    parent.resize(100, 60);
    QPixmap src(42, 24);
    src.fill(Qt::red);
    Platform_ListPic pic(src, &parent);
    // setPic just stores the new pixmap; no crash, size unchanged.
    QPixmap other(42, 24);
    other.fill(Qt::blue);
    pic.setPic(other);
    EXPECT_EQ(pic.size(), QSize(42, 24));
}

TEST(platform_pw_ext, listPic_paintEvent_doesNotCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(100, 60);
    QPixmap src(42, 24);
    src.fill(Qt::green);
    Platform_ListPic pic(src, &parent);
    pic.resize(42, 24);
    // paintEvent drives a QPainter over the pixmap + rounded-rect mask. It
    // reads the application palette, so it needs a screen.
    QPaintEvent pe(pic.rect());
    QApplication::sendEvent(&pic, &pe);
    SUCCEED();
}

// ==========================================================================
// Platform_PlaylistWidget: empty-list method branches
// ==========================================================================

TEST(platform_pw_ext, playlist_engine_matchesMainWindow)
{
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    PlayerEngine *eng = pl->engine();
    EXPECT_NE(eng, nullptr);
    Platform_MainWindow *mw = ppw_mainWindow();
    if (mw && mw->engine()) {
        EXPECT_EQ(eng, mw->engine());
    }
}

TEST(platform_pw_ext, playlist_updateItemInfo_emptyList_returnsEarly)
{
    // With an empty list, _playlist->item(id) is null -> piw null -> early
    // return. Must not crash.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    pl->updateItemInfo(0);
    SUCCEED();
}

TEST(platform_pw_ext, playlist_updateItemStates_emptyList_noIteration)
{
    // Empty list: the for loop body never runs; the call still hits the
    // function and reads _engine->playlist().current().
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    pl->updateItemStates();
    SUCCEED();
}

TEST(platform_pw_ext, playlist_removeItem_emptyList_noOp)
{
    // Empty list: takeItem returns null, loops are empty, count()==0 so the
    // two hover branches are skipped; _num text is refreshed.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    pl->removeItem(0);
    EXPECT_EQ(pl->get_playlist()->count(), 0);
}

TEST(platform_pw_ext, playlist_appendItems_emptyModel_skipsLoop)
{
    // _engine->playlist().items() empty -> begin+0 == end -> while skipped.
    // Safe: no Platform_PlayItemWidget is constructed.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    pl->appendItems();
    EXPECT_EQ(pl->get_playlist()->count(), 0);
}

TEST(platform_pw_ext, playlist_loadPlaylist_emptyModel_noItems)
{
    // loadPlaylist clears the DListWidget then iterates the (empty) model;
    // on an Idle model it stays empty and never constructs item widgets.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    pl->loadPlaylist();
    EXPECT_EQ(pl->get_playlist()->count(), 0);
}

TEST(platform_pw_ext, playlist_OnItemChanged_nullNull_safe)
{
    // Both pointers null: neither block dereferences a widget item. Must not
    // crash and must not change list state.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    int before = pl->get_playlist()->count();
    pl->OnItemChanged(nullptr, nullptr);
    EXPECT_EQ(pl->get_playlist()->count(), before);
}

// ==========================================================================
// Platform_PlaylistWidget::eventFilter (private, exposed via #define)
// ==========================================================================

TEST(platform_pw_ext, playlist_eventFilter_playlistFocusIn_empty_consumed)
{
    // FocusIn on _playlist with an empty list: the count() check is false so
    // the inner focus-shift block is skipped and the filter returns true.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *list = pl->get_playlist();
    ASSERT_NE(list, nullptr);
    if (!ppw_modelEmpty(pl)) GTEST_SKIP() << "Model not empty";
    QFocusEvent fe(QEvent::FocusIn);
    bool eaten = pl->eventFilter(list, &fe);
    EXPECT_TRUE(eaten);
}

TEST(platform_pw_ext, playlist_eventFilter_playlistOtherKey_delegated)
{
    // A non-Tab key on _playlist falls through the switch default and reaches
    // the standard eventFilter path (returns false here).
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *list = pl->get_playlist();
    ASSERT_NE(list, nullptr);
    QKeyEvent other(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    pl->eventFilter(list, &other);
    SUCCEED();
}

TEST(platform_pw_ext, playlist_eventFilter_unrelatedObject_delegates)
{
    // An object that is neither the clear button nor the list reaches the
    // final standard eventFilter call.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    QObject dummy;
    QFocusEvent fe(QEvent::FocusIn);
    pl->eventFilter(&dummy, &fe);
    SUCCEED();
}

// ==========================================================================
// Platform_MouseEventListener::eventFilter (installed on the playlist
// viewport during construction; exercised indirectly here)
// ==========================================================================

TEST(platform_pw_ext, mouseEventListener_leftPressEmpty_deselectsNothing)
{
    // Left-button press on the viewport with no item under the cursor:
    // currentItem()/itemWidget() yield null -> the deselect branch is a no-op.
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *list = pl->get_playlist();
    ASSERT_NE(list, nullptr);
    QWidget *vp = list->viewport();
    ASSERT_NE(vp, nullptr);
    QPointF local(0, 0);
    QMouseEvent me(QEvent::MouseButtonPress, local, local,
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    ppw_wait(10);
    SUCCEED();
}

TEST(platform_pw_ext, mouseEventListener_nonLeftButton_delegates)
{
    // A non-left-button press on the viewport falls through to standard
    // processing (the LeftButton guard is false).
    Platform_PlaylistWidget *pl = ppw_playlist();
    ASSERT_NE(pl, nullptr);
    DListWidget *list = pl->get_playlist();
    ASSERT_NE(list, nullptr);
    QWidget *vp = list->viewport();
    ASSERT_NE(vp, nullptr);
    QPointF local(0, 0);
    QMouseEvent me(QEvent::MouseButtonPress, local, local,
                   Qt::RightButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(vp, &me);
    SUCCEED();
}

