// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// GTest cases for src/common/actions.cpp (ActionFactory) in the non-platform
// test binary.
//
// Safety rules baked in:
//   * ActionFactory::get() and its menus are process-global singletons whose
//     menus are lazily built once and cached. Branch-specific (mpv / pad)
//     structure is therefore asserted behind a GTEST_SKIP guard that adapts to
//     whichever variant was built first, while branch-invariant structure
//     (play mode group, window toggles, playlist menu, kind properties) is
//     asserted unconditionally.
//   * CompositingManager::isMpvExists / isPadSystem are stubbed in SetUp so a
//     first-time menu build is deterministic and no real device probing
//     happens inside the tests.
//   * updateMainActionsForMovie only mutates the synthesized sub/track menu
//     entries; no player backend is touched.

#include <gtest/gtest.h>
#include <QAction>
#include <QActionGroup>
#include <QMenu>

#include "actions.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "stub/stub.h"

using namespace dmr;

namespace {
bool ut_act_isMpvExists_true_stub()
{
    return true;
}

bool ut_act_isPadSystem_false_stub()
{
    return false;
}
}  // namespace

class ActionFactoryTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_stub.set(ADDR(CompositingManager, isMpvExists), ut_act_isMpvExists_true_stub);
        m_stub.set(ADDR(CompositingManager, isPadSystem), ut_act_isPadSystem_false_stub);
    }
    void TearDown() override
    {
        m_stub.reset(ADDR(CompositingManager, isMpvExists));
        m_stub.reset(ADDR(CompositingManager, isPadSystem));
    }
    Stub m_stub;
};

// ---- ActionFactory::get ----------------------------------------------------

TEST_F(ActionFactoryTest, Get_Returns_StableSingleton)
{
    // Arrange
    ActionFactory &a = ActionFactory::get();
    // Act
    ActionFactory &b = ActionFactory::get();
    // Assert: repeated lookups return the same process-global singleton.
    EXPECT_NE(&a, nullptr);
    EXPECT_EQ(&a, &b);
}

// ---- ActionFactory::actionKind ---------------------------------------------

TEST_F(ActionFactoryTest, ActionKind_UnsetProperty_ReadsInvalid)
{
    // Arrange
    QAction plainAction(nullptr);
    QAction kAction(nullptr);
    kAction.setProperty("kind", ActionFactory::ActionKind::Screenshot);
    // Act
    auto unsetKind = ActionFactory::actionKind(&plainAction);
    auto setKind = ActionFactory::actionKind(&kAction);
    // Assert: an action without a kind property reads as Invalid.
    EXPECT_EQ(unsetKind, ActionFactory::ActionKind::Invalid);
    // A set kind property round-trips verbatim through actionKind().
    EXPECT_EQ(setKind, ActionFactory::ActionKind::Screenshot);
}

// ---- ActionFactory::titlebarMenu -------------------------------------------

TEST_F(ActionFactoryTest, TitlebarMenu_IsCached_NonEmpty)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    DMenu *m1 = af.titlebarMenu();
    DMenu *m2 = af.titlebarMenu();
    // Assert: the lazily built menu is cached and non-empty.
    ASSERT_NE(m1, nullptr);
    EXPECT_EQ(m1, m2);
    EXPECT_FALSE(m1->actions().isEmpty());
}

TEST_F(ActionFactoryTest, TitlebarMenu_OpenFileList_HasKind)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    af.titlebarMenu();
    QList<QAction *> acts = af.findActionsByKind(ActionFactory::ActionKind::OpenFileList);
    // Assert: OpenFileList is added to both the titlebar menu and the main
    // context menu, and MainWindow may have built both before the tests run.
    EXPECT_GE(acts.size(), 1);
    for (QAction *act : acts) {
        EXPECT_EQ(ActionFactory::actionKind(act), ActionFactory::ActionKind::OpenFileList);
        EXPECT_FALSE(act->text().isEmpty());
    }
}

// ---- ActionFactory::mainContextMenu ----------------------------------------

TEST_F(ActionFactoryTest, MainContextMenu_IsCached_NonEmpty)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    DMenu *m1 = af.mainContextMenu();
    DMenu *m2 = af.mainContextMenu();
    // Assert: the lazily built menu is cached and substantial.
    ASSERT_NE(m1, nullptr);
    EXPECT_EQ(m1, m2);
    EXPECT_GT(m1->actions().size(), 5);
}

TEST_F(ActionFactoryTest, MainContextMenu_PlayMode_GroupComplete)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    const ActionFactory::ActionKind kinds[] = {
        ActionFactory::ActionKind::OrderPlay,
        ActionFactory::ActionKind::ShufflePlay,
        ActionFactory::ActionKind::SinglePlay,
        ActionFactory::ActionKind::SingleLoop,
        ActionFactory::ActionKind::ListLoop,
    };
    // Act
    af.mainContextMenu();
    // Assert: every play mode kind resolves to exactly one checkable,
    // action-group member.
    for (auto kd : kinds) {
        QList<QAction *> acts = af.findActionsByKind(kd);
        EXPECT_EQ(acts.size(), 1) << "kind=" << static_cast<int>(kd);
        if (!acts.isEmpty()) {
            EXPECT_EQ(ActionFactory::actionKind(acts.first()), kd);
            EXPECT_TRUE(acts.first()->isCheckable());
            EXPECT_NE(acts.first()->actionGroup(), nullptr);
        }
    }
}

TEST_F(ActionFactoryTest, MainContextMenu_WindowToggles_AreCheckable)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    const ActionFactory::ActionKind kinds[] = {
        ActionFactory::ActionKind::ToggleFullscreen,
        ActionFactory::ActionKind::ToggleMiniMode,
        ActionFactory::ActionKind::WindowAbove,
    };
    // Act
    af.mainContextMenu();
    // Assert: each window toggle kind resolves to exactly one checkable action.
    for (auto kd : kinds) {
        QList<QAction *> acts = af.findActionsByKind(kd);
        EXPECT_EQ(acts.size(), 1) << "kind=" << static_cast<int>(kd);
        if (!acts.isEmpty()) {
            EXPECT_EQ(ActionFactory::actionKind(acts.first()), kd);
            EXPECT_TRUE(acts.first()->isCheckable());
        }
    }
}

TEST_F(ActionFactoryTest, MainContextMenu_Screenshot_GroupPresent)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    af.mainContextMenu();
    // Assert: both the mpv and the non-mpv menu variants carry these two kinds.
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::Screenshot).size(), 1);
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::GoToScreenshotSolder).size(), 1);
}

TEST_F(ActionFactoryTest, MainContextMenu_SubCodepage_CarriesCodepageArg)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    af.mainContextMenu();
    QList<QAction *> acts = af.findActionsByKind(ActionFactory::ActionKind::ChangeSubCodepage);
    if (acts.isEmpty()) {
        GTEST_SKIP() << "non-mpv context menu variant built on this host";
    }
    // Assert: one action per supported codepage (plus the leading "auto"
    // entry), each carrying a non-empty args list.
    EXPECT_GT(acts.size(), 10);
    for (QAction *act : acts) {
        EXPECT_EQ(ActionFactory::actionKind(act), ActionFactory::ActionKind::ChangeSubCodepage);
        EXPECT_TRUE(act->property("args").isValid());
        EXPECT_FALSE(act->property("args").toList().isEmpty());
    }
    // The first encoding is the "auto" entry.
    EXPECT_EQ(acts.first()->property("args").toList().first().toString(),
              QStringLiteral("auto"));
}

TEST_F(ActionFactoryTest, FindActionsByKind_UnknownKinds_ReturnEmpty)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    af.mainContextMenu();
    af.titlebarMenu();
    // Assert: About/Help/Exit are commented out in the non-platform menus; no
    // action ever carries these kinds.
    EXPECT_TRUE(af.findActionsByKind(ActionFactory::ActionKind::About).isEmpty());
    EXPECT_TRUE(af.findActionsByKind(ActionFactory::ActionKind::Help).isEmpty());
    EXPECT_TRUE(af.findActionsByKind(ActionFactory::ActionKind::Exit).isEmpty());
    EXPECT_TRUE(af.findActionsByKind(ActionFactory::ActionKind::Invalid).isEmpty());
}

// ---- ActionFactory::playlistContextMenu ------------------------------------

TEST_F(ActionFactoryTest, PlaylistContextMenu_Exactly_FourKinds)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    // Act
    DMenu *m1 = af.playlistContextMenu();
    DMenu *m2 = af.playlistContextMenu();
    // Assert: the lazily built playlist menu is cached and carries exactly one
    // action for each of its four kinds.
    ASSERT_NE(m1, nullptr);
    EXPECT_EQ(m1, m2);
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::PlaylistRemoveItem).size(), 1);
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::EmptyPlaylist).size(), 1);
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::PlaylistOpenItemInFM).size(), 1);
    EXPECT_EQ(af.findActionsByKind(ActionFactory::ActionKind::PlaylistItemInfo).size(), 1);
}

// ---- ActionFactory::updateMainActionsForMovie ------------------------------

// NOTE: the "empty lists" path is deliberately NOT covered here: calling
// updateMainActionsForMovie with empty sub/audio lists clears the menus and
// leaves expired QPointer entries in m_listContextMenuActions, and any
// subsequent findActionsByKind() call then SIGSEGVs inside the null
// dereference (*itor)->property("kind") (reproduced: signal 11 at 0x8).
// That is a source defect and is reported in the defect list, not asserted.

TEST_F(ActionFactoryTest, UpdateMainActionsForMovie_Populates_SubAndTrackMenus)
{
    // Arrange
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    if (af.findActionsByKind(ActionFactory::ActionKind::ChangeSubCodepage).isEmpty()) {
        GTEST_SKIP() << "non-mpv context menu variant built on this host";
    }

    PlayingMovieInfo pmf;
    SubtitleInfo sub1;
    sub1["title"] = QStringLiteral("Sub A");
    SubtitleInfo sub2;
    sub2["title"] = QStringLiteral("Sub B");
    pmf.subs << sub1 << sub2;
    AudioInfo aud1;
    aud1["title"] = QStringLiteral("[internal]");
    AudioInfo aud2;
    aud2["title"] = QStringLiteral("English");
    pmf.audios << aud1 << aud2;

    // Act
    af.updateMainActionsForMovie(pmf);

    // Assert: one synthesized menu entry per subtitle / audio track, in order,
    // each carrying its 0-based track index in the args property.
    QList<QAction *> subs = af.findActionsByKind(ActionFactory::ActionKind::SelectSubtitle);
    ASSERT_EQ(subs.size(), 2);
    EXPECT_EQ(subs[0]->text(), QStringLiteral("Sub A"));
    EXPECT_EQ(subs[1]->text(), QStringLiteral("Sub B"));
    EXPECT_EQ(subs[0]->property("args").toList().first().toInt(), 0);
    EXPECT_EQ(subs[1]->property("args").toList().first().toInt(), 1);

    QList<QAction *> tracks = af.findActionsByKind(ActionFactory::ActionKind::SelectTrack);
    ASSERT_EQ(tracks.size(), 2);
    // "[internal]" audio titles are renamed to "Track<N>" (1-based).
    EXPECT_EQ(tracks[0]->text(), QStringLiteral("Track1"));
    EXPECT_EQ(tracks[1]->text(), QStringLiteral("English"));
    EXPECT_EQ(tracks[0]->property("args").toList().first().toInt(), 0);
    EXPECT_EQ(tracks[1]->property("args").toList().first().toInt(), 1);
}
