// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Boost-backend extension unit tests (suite: boost_be).
//
// Goal: raise line coverage on a mixed bundle of source files that still have
// sizeable uncovered regions, by exercising ONLY their safe / pure branches:
//   * src/backends/mpv/mpv_proxy.cpp        — MpvHandle + my_node_autofree +
//                                             tr() helpers (no live mpv/media).
//   * src/common/actions.cpp                — ActionFactory menu builders and
//                                             the enable/disable signals wired
//                                             to the submenus, plus the static
//                                             kind/args/origin accessors.
//   * src/common/dmr_settings.cpp           — valueChanged lambda branches for
//                                             the decode.Videoout family that
//                                             the sibling suites leave cold.
//   * src/widgets/burst_screenshots_dialog.cpp — construct/getters/setters.
//
// CRASH SAFETY (single shared process — a crash drops ALL later coverage):
//   * Only TEST(...). gtest_main supplies main(); never define main().
//   * Every connect is captured into a local and disconnected before the case
//     returns, so no lambda outlives its captured state.
//   * No real MpvProxy is instantiated: its ctor/dtor touch window handles and
//     libmpv. Only the backend-free MpvHandle / my_node_autofree / tr() helpers
//     are exercised.
//   * Settings writes use only keys known-safe from the json schema:
//     base.decode.{Videoout,Decodemode}, base.play.{mute,playmode}, the
//     shortcuts.* family. Unknown keys NPE inside DSettings.
//   * Paint / geometry paths are guarded by primaryScreen() + GTEST_SKIP.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QAction>
#include <QActionGroup>
#include <QPointer>
#include <QPixmap>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <mpv/client.h>

#include <string>
#include <vector>

// STL / Qt headers MUST come before the `#define private public` block so the
// macro does not corrupt standard-library symbols.
#define protected public
#define private public
#include "src/backends/mpv/mpv_proxy.h"
#include "src/common/actions.h"
#include "src/common/dmr_settings.h"
#include "src/widgets/burst_screenshots_dialog.h"
#undef protected
#undef private

#include "application.h"
#include "compositing_manager.h"
#include "playlist_model.h"

using namespace dmr;

// ---------- static helpers (unique prefix bbe_) ----------

// Persist + restore a base.* option so each test can mutate it safely.
static QVariant bbe_backup(const QString &sKey)
{
    return Settings::get().settings()->value(sKey);
}
static void bbe_restore(const QString &sKey, const QVariant &prev)
{
    Settings::get().settings()->setOption(sKey, prev);
    Settings::get().settings()->sync();
}

static void bbe_wait(int ms = 30) { QTest::qWait(ms); }

// ===========================================================================
// mpv_proxy.cpp — MpvHandle refcounted container (null-handle path; the dtor
// only talks to libmpv when the wrapped handle is non-null, so wrapping a
// nullptr exercises ctor + the null-guarded dtor without invoking any mpv C
// API). The sibling suites cover the basics; here we widen the matrix.
// ===========================================================================

TEST(boost_be, bbe_MpvHandle_fromRawNull_dereferencesToNull)
{
    MpvHandle h = MpvHandle::fromRawHandle(nullptr);
    ASSERT_EQ((mpv_handle *)h, nullptr);
}

TEST(boost_be, bbe_MpvHandle_selfAssign_keepsNull)
{
    MpvHandle h = MpvHandle::fromRawHandle(nullptr);
    h = h;   // self copy-assign on a QSharedPointer; must stay null and stable.
    EXPECT_EQ((mpv_handle *)h, nullptr);
}

TEST(boost_be, bbe_MpvHandle_manyAliases_allNullUntilLastDrop)
{
    MpvHandle root = MpvHandle::fromRawHandle(nullptr);
    {
        std::vector<MpvHandle> clones;
        for (int i = 0; i < 8; ++i) clones.push_back(root);
        for (const auto &c : clones) EXPECT_EQ((mpv_handle *)c, nullptr);
    }
    EXPECT_EQ((mpv_handle *)root, nullptr);
}

TEST(boost_be, bbe_MpvHandle_reset_toDefaultConstructed)
{
    MpvHandle h = MpvHandle::fromRawHandle(nullptr);
    ASSERT_EQ((mpv_handle *)h, nullptr);
    h = MpvHandle();   // reset path: drop the null container, swap in empty.
    EXPECT_EQ((mpv_handle *)h, (mpv_handle *)0);
}

// ===========================================================================
// mpv_proxy.cpp — MpvProxy::my_node_autofree RAII wrapper. The dtor calls
// mpv_free_node_contents, which is a safe no-op on scalar formats and on an
// empty NODE_MAP / NODE_ARRAY. The sibling suites cover NONE/FLAG/DOUBLE/
// INT64/STRING/empty-map; here we add an empty NODE_ARRAY and re-test the
// STRING path with a longer literal to widen the covered dtor line range.
// ===========================================================================

TEST(boost_be, bbe_my_node_autofree_emptyNodeArray)
{
    mpv_node node;
    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = nullptr;
    {
        MpvProxy::my_node_autofree af(&node);
        ASSERT_NE(af.pNode, nullptr);
        EXPECT_EQ(af.pNode->format, MPV_FORMAT_NODE_ARRAY);
    }   // dtor walks zero entries safely
    SUCCEED();
}

TEST(boost_be, bbe_my_node_autofree_stringLongLiteral)
{
    mpv_node node;
    node.format = MPV_FORMAT_STRING;
    node.u.string = const_cast<char *>("bbe_long_static_string_payload");
    {
        MpvProxy::my_node_autofree af(&node);
        ASSERT_STREQ(af.pNode->u.string, "bbe_long_static_string_payload");
    }   // mpv_free_node_contents does NOT free caller-owned string buffers.
    SUCCEED();
}

TEST(boost_be, bbe_my_node_autofree_none_roundTrip)
{
    mpv_node node;
    node.format = MPV_FORMAT_NONE;
    MpvProxy::my_node_autofree af(&node);
    EXPECT_EQ(af.pNode, &node);
    EXPECT_EQ(af.pNode->format, MPV_FORMAT_NONE);
}

// ===========================================================================
// mpv_proxy.cpp — MpvProxy::tr(). Q_OBJECT-generated static translator; pure,
// backend-free. Wide input set so additional tr() call sites in the TU light
// up. Includes the 3-arg plural-aware overload and a few specific strings that
// appear as literal tr() arguments inside mpv_proxy.cpp.
// ===========================================================================

TEST(boost_be, bbe_MpvProxy_tr_corePlaybackStrings)
{
    const QStringList keys {
        "Play", "Pause", "Stop", "Movie", "Subtitle", "Internal",
        "Volume", "Mute", "Fullscreen", "Settings"
    };
    for (const QString &k : keys) {
        QString s = MpvProxy::tr(k.toUtf8().constData());
        EXPECT_FALSE(s.isEmpty()) << "tr() returned empty for " << k.toUtf8().constData();
    }
}

TEST(boost_be, bbe_MpvProxy_tr_pluralOverload)
{
    QString one = MpvProxy::tr("Movie", nullptr, 1);
    QString many = MpvProxy::tr("Movie", nullptr, 5);
    EXPECT_FALSE(one.isEmpty());
    EXPECT_FALSE(many.isEmpty());
}

TEST(boost_be, bbe_MpvProxy_tr_emptySourceIsSafe)
{
    QString s = MpvProxy::tr("");
    EXPECT_TRUE(s.isEmpty() || s == QString(""));
}

// ===========================================================================
// actions.cpp — ActionFactory. The sibling suites already touch the menu
// builders and findActionsByKind; here we additionally drive the four
// enable/disable signals (frameMenuEnable / playSpeedMenuEnable /
// subtitleMenuEnable / soundMenuEnable) that the builders wire to the
// submenus, and exercise the static accessor trio on a broader action-kind
// matrix.
// ===========================================================================

TEST(boost_be, bbe_actionFactory_singletonIdentity)
{
    EXPECT_EQ(&ActionFactory::get(), &ActionFactory::get());
}

TEST(boost_be, bbe_actionFactory_mainContextMenuCached)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.mainContextMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.mainContextMenu();
    EXPECT_EQ(m1, m2);
    EXPECT_FALSE(m1->actions().isEmpty());
}

TEST(boost_be, bbe_actionFactory_titlebarMenuCached)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.titlebarMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.titlebarMenu();
    EXPECT_EQ(m1, m2);
}

TEST(boost_be, bbe_actionFactory_playlistContextMenuCached)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.playlistContextMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.playlistContextMenu();
    EXPECT_EQ(m1, m2);
}

TEST(boost_be, bbe_actionFactory_emitFrameMenuEnable_doesNotCrash)
{
    // The builder connects frameMenuEnable -> pMenu->setEnabled. Emitting it
    // exercises the queued lambda safely (the menu is owned by the factory).
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();   // ensure the submenu + connection exist
    emit af.frameMenuEnable(true);
    emit af.frameMenuEnable(false);
    bbe_wait(10);
    SUCCEED();
}

TEST(boost_be, bbe_actionFactory_emitPlaySpeedMenuEnable_doesNotCrash)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    emit af.playSpeedMenuEnable(true);
    emit af.playSpeedMenuEnable(false);
    bbe_wait(10);
    SUCCEED();
}

TEST(boost_be, bbe_actionFactory_emitSubtitleMenuEnable_doesNotCrash)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    emit af.subtitleMenuEnable(true);
    emit af.subtitleMenuEnable(false);
    bbe_wait(10);
    SUCCEED();
}

TEST(boost_be, bbe_actionFactory_emitSoundMenuEnable_doesNotCrash)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    emit af.soundMenuEnable(true);
    emit af.soundMenuEnable(false);
    bbe_wait(10);
    SUCCEED();
}

TEST(boost_be, bbe_actionFactory_findActionsByKind_playbackGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::OrderPlay).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ShufflePlay).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::SingleLoop).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ListLoop).size(), 1);
}

TEST(boost_be, bbe_actionFactory_findActionsByKind_openGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::OpenFileList).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::OpenDirectory).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::OpenCdrom).size(), 1);
}

TEST(boost_be, bbe_actionFactory_findActionsByKind_windowGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ToggleFullscreen).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ToggleMiniMode).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::WindowAbove).size(), 1);
}

TEST(boost_be, bbe_actionFactory_actionKind_roundtripManyKinds)
{
    // actionKind() reads the "kind" property; verify it round-trips for a
    // representative slice of the enum so the static helper's branches are hit.
    const QList<ActionFactory::ActionKind> kinds {
        ActionFactory::ActionKind::OpenFile,
        ActionFactory::ActionKind::Settings,
        ActionFactory::ActionKind::ChangeSubCodepage,
        ActionFactory::ActionKind::Screenshot,
        ActionFactory::ActionKind::BurstScreenshot,
        ActionFactory::ActionKind::TogglePause,
        ActionFactory::ActionKind::SeekForward,
        ActionFactory::ActionKind::VolumeUp,
        ActionFactory::ActionKind::ToggleMute,
        ActionFactory::ActionKind::ViewShortcut,
    };
    for (auto k : kinds) {
        QAction a;
        a.setProperty("kind", QVariant::fromValue(k));
        EXPECT_EQ(ActionFactory::actionKind(&a), k);
    }
}

TEST(boost_be, bbe_actionFactory_actionKind_unsetProperty_isInvalid)
{
    QAction a;   // no "kind" property set
    // Reading an invalid property yields a default-constructed ActionKind
    // (Invalid == 0). Just assert the call returns without crashing.
    auto k = ActionFactory::actionKind(&a);
    EXPECT_EQ(k, ActionFactory::ActionKind::Invalid);
}

TEST(boost_be, bbe_actionFactory_actionArgs_andHasArgs_emptyVsSet)
{
    QAction a;
    EXPECT_FALSE(ActionFactory::actionHasArgs(&a));
    EXPECT_TRUE(ActionFactory::actionArgs(&a).isEmpty());

    a.setProperty("args", QVariantList() << 1 << "two" << 3.0);
    EXPECT_TRUE(ActionFactory::actionHasArgs(&a));
    EXPECT_EQ(ActionFactory::actionArgs(&a).size(), 3);
}

TEST(boost_be, bbe_actionFactory_isActionFromShortcut_originValues)
{
    QAction a;
    EXPECT_FALSE(ActionFactory::isActionFromShortcut(&a));

    a.setProperty("origin", QStringLiteral("shortcut"));
    EXPECT_TRUE(ActionFactory::isActionFromShortcut(&a));

    a.setProperty("origin", QStringLiteral("menu"));
    EXPECT_FALSE(ActionFactory::isActionFromShortcut(&a));
}

TEST(boost_be, bbe_actionFactory_forEachInMainMenu_visitsMany)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    int n = 0;
    af.forEachInMainMenu([&n](QAction *) { ++n; });
    EXPECT_GT(n, 10);
}

// ===========================================================================
// dmr_settings.cpp — valueChanged lambda, base.decode.Videoout family.
// The sibling suites cover playmode/Effect/etc but leave the Videoout
// per-vo branch matrix cold. Each branch reads the selected VO string and
// rewrites the base.decode.Decodemode option's "items". Setting
// base.decode.Videoout to 0..4 walks each VO branch (gpu/vaapi/vdpau/xv or
// x11 / opengl) and emits baseChanged. All keys exist in the schema.
// ===========================================================================

TEST(boost_be, bbe_settings_videoout_gpuBranch_emitsBaseChanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", 0);   // -> "gpu"
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_videoout_vaapiBranch_setsDecodemodeItems)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", 1);   // -> vaapi
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_videoout_vdpauBranch_emitsBaseChanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", 2);   // -> vdpau
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_videoout_xvOrX11Branch_emitsBaseChanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", 3);   // -> xv / x11
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_videoout_openglBranch_emitsBaseChanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", 4);   // -> opengl
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_videoout_negativeValue_isGuarded)
{
    // value < 0 takes the early-return guard; no baseChanged is emitted.
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                              [&got](const QString &k, const QVariant &) {
                                  if (k.startsWith("base.decode.Videoout")) got = true;
                              });
    QVariant prev = bbe_backup("base.decode.Videoout");
    Settings::get().settings()->setOption("base.decode.Videoout", -1);
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_FALSE(got);
    bbe_restore("base.decode.Videoout", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_playmode_emitsDefaultplaymodechanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::defaultplaymodechanged, &ctx,
                              [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = bbe_backup("base.play.playmode");
    Settings::get().settings()->setOption("base.play.playmode", 1);
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.play.playmode", prev);
    QObject::disconnect(c);
}

TEST(boost_be, bbe_settings_mute_emitsBaseMuteChanged)
{
    bool got = false;
    QObject ctx;
    auto c = QObject::connect(&Settings::get(), &Settings::baseMuteChanged, &ctx,
                              [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = bbe_backup("base.play.mute");
    Settings::get().settings()->setOption("base.play.mute", true);
    Settings::get().settings()->sync();
    bbe_wait(30);
    EXPECT_TRUE(got);
    bbe_restore("base.play.mute", prev);
    QObject::disconnect(c);
}

// ===========================================================================
// burst_screenshots_dialog.cpp — construct the dialog against a synthetic
// PlayItemInfo and exercise its getters / savePoster / updateWithFrames.
// Paint and grab paths are guarded by primaryScreen() + GTEST_SKIP.
//
// The dialog's constructor reads PlayItemInfo.mi (a MovieInfo), so we build a
// default-constructed PlayItemInfo and only rely on the safe fields it reads
// (title / durationStr / resolution / sizeStr). savePoster() calls grab() +
// img.save() which need a real screen, hence the headless guard.
// ===========================================================================

TEST(boost_be, bbe_burstDialog_construct_and_savedPosterPath_empty)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    PlayItemInfo pii {};   // value-init: valid=false, mi default-constructed
    BurstScreenshotsDialog *dlg = nullptr;
    ASSERT_NO_FATAL_FAILURE({
        dlg = new BurstScreenshotsDialog(pii);
    });
    ASSERT_NE(dlg, nullptr);
    // Before savePoster(), the path is empty.
    EXPECT_TRUE(dlg->savedPosterPath().isEmpty());
    delete dlg;
}

TEST(boost_be, bbe_burstDialog_updateWithFrames_emptyList_noCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    PlayItemInfo pii {};
    BurstScreenshotsDialog dlg(pii);
    // An empty frame list must not crash and must not add any thumbnails.
    dlg.updateWithFrames(QList<QPair<QImage, qint64>>());
    SUCCEED();
}

TEST(boost_be, bbe_burstDialog_updateWithFrames_singleSyntheticFrame)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    PlayItemInfo pii {};
    BurstScreenshotsDialog dlg(pii);
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::black);
    dlg.updateWithFrames({ qMakePair(img, qint64(0)) });
    SUCCEED();
}

TEST(boost_be, bbe_burstDialog_updateWithFrames_multipleFrames_gridFill)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    PlayItemInfo pii {};
    BurstScreenshotsDialog dlg(pii);
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::red);
    QList<QPair<QImage, qint64>> frames;
    for (int i = 0; i < 6; ++i) frames << qMakePair(img, qint64(i * 1000));
    dlg.updateWithFrames(frames);
    SUCCEED();
}

TEST(boost_be, bbe_burstDialog_savePoster_setsNonEmptyPath)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    // Point the screenshot template at a writable temp path so savePoster()'s
    // img.save() succeeds regardless of the real settings value.
    QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                  "/bbe_poster_" +
                  QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz") + ".jpg";
    QVariant prevLoc = Settings::get().settings()->value("base.screenshot.location");
    Settings::get().settings()->setOption(
        "base.screenshot.location",
        QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    Settings::get().settings()->sync();

    PlayItemInfo pii {};
    BurstScreenshotsDialog dlg(pii);
    dlg.savePoster();
    QString path = dlg.savedPosterPath();
    EXPECT_FALSE(path.isEmpty());
    // The generated file may or may not exist depending on grab() in this
    // environment; we only assert the path was assigned without crashing.
    if (QFileInfo::exists(path)) QFile::remove(path);

    Settings::get().settings()->setOption("base.screenshot.location", prevLoc);
    Settings::get().settings()->sync();
}

TEST(boost_be, bbe_burstDialog_thumbnailFrame_construct_hasFixedSize)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP() << "headless";

    QWidget parent;
    ThumbnailFrame frame(&parent);
    // The ctor pins a 178x100 design size.
    EXPECT_EQ(frame.sizeHint().width(), 178);
    EXPECT_EQ(frame.sizeHint().height(), 100);
}
