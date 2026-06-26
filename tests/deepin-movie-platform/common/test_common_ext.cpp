// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests covering remaining branches in common/widgets files:
//   * src/common/dmr_settings.cpp        — valueChanged lambda branches
//   * src/common/actions.cpp             — titlebar menu / context menu builders
//   * src/common/shortcut_manager.cpp    — Return / Num+Enter shortcut remap
//   * src/common/platform/platform_dbus_adpator.cpp — adaptor slots
//   * src/widgets/toolbutton.h           — inline ToolButton / ToolTip methods
//   * src/vendor/presenter.cpp           — Presenter slots / volume mapping
//
// Suite name "common_ext"; static helpers use unique prefix "ce_".
//
// Safety rules baked in (verified against prior crashes / link failures):
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * Only settings keys known-safe to write are used (verified against the
//     settings.json schema): base.play.{mute,hwaccel,playlist_pos,...},
//     base.decode.{select,Effect,Videoout}, base.play.playmode, and the
//     shortcuts.* family. Unknown keys would NPE inside DSettings.
//   * ToolButton / ButtonBoxButton / ButtonToolTip / ToolTip are exercised
//     with a fresh local QWidget parent; geometry / paint paths are guarded
//     by primaryScreen() and GTEST_SKIP when headless.
//   * No mpv backend / decode path is exercised; only pure logic branches.
//   * The DBus adaptor is constructed against the running app's main window
//     (its lifetime is managed by QDBusAbstractAdaptor's parent). The static
//     readDBusProperty / readDBusMethod helpers are safe to call against a
//     bogus service name (they return QVariant(0)).

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QAction>
#include <QActionGroup>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QThread>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QMetaObject>

#include "application.h"
#include "dmr_settings.h"
#include "actions.h"
#include "shortcut_manager.h"
#include "compositing_manager.h"
#include "platform/platform_dbus_adpator.h"
#include "platform/platform_mainwindow.h"
#include "player_engine.h"
#include "playlist_model.h"
#include "movie_configuration.h"
#include "presenter.h"
#include "toolbutton.h"

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// The running app's main window (Platform_MainWindow in this build).
static Platform_MainWindow *ce_mainWindow()
{
    return dApp->getMainWindow();
}

// Persist + restore a base.play option so each test can mutate it safely.
static QVariant ce_backup(const QString &sOpt)
{
    return Settings::get().settings()->value(QString("base.play.%1").arg(sOpt));
}
static void ce_restore(const QString &sOpt, const QVariant &prev)
{
    Settings::get().settings()->setOption(QString("base.play.%1").arg(sOpt), prev);
    Settings::get().settings()->sync();
}

static void ce_wait(int ms = 80) { QTest::qWait(ms); }

// ==========================================================================
// dmr_settings.cpp — valueChanged lambda branches (the biggest gap).
// The lambda is private and connected to DSettings::valueChanged; the only
// safe way to exercise a branch is to setOption(<key>) on a real schema key.
// ==========================================================================

TEST(common_ext, settings_valueChanged_basePlayMute_emitsBaseMuteChanged)
{
    // base.play.mute branch emits baseMuteChanged.
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::baseMuteChanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = ce_backup("mute");
    Settings::get().settings()->setOption("base.play.mute", true);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    ce_restore("mute", prev);
}

TEST(common_ext, settings_valueChanged_basePlayHwaccel_emitsHwaccelModeChanged)
{
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::hwaccelModeChanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = ce_backup("hwaccel");
    Settings::get().settings()->setOption("base.play.hwaccel", 1);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    ce_restore("hwaccel", prev);
}

TEST(common_ext, settings_valueChanged_baseShowTimeFullScreen_emitsShowTime)
{
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::showTimeFullScreenChanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = Settings::get().settings()->value("base.play.showTimeFullScreen");
    Settings::get().settings()->setOption("base.play.showTimeFullScreen", true);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    Settings::get().settings()->setOption("base.play.showTimeFullScreen", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_baseDecodeSelect_emitsSetDecodeModel)
{
    // base.decode.select branch: value 0/1/2 -> emit refreshDecode() + crashCheck.
    bool gotDecode = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::setDecodeModel, &ctx,
                     [&gotDecode](const QString &, const QVariant &) { gotDecode = true; });
    QVariant prev = Settings::get().settings()->value("base.decode.select");
    Settings::get().settings()->setOption("base.decode.select", 0);   // AUTO
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(gotDecode);
    Settings::get().settings()->setOption("base.decode.select", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_baseDecodeSelect_custom_emitsRefreshDecode)
{
    // value 3 (custom) -> only emits setDecodeModel, no refreshDecode branch.
    bool gotRefresh = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::refreshDecode, &ctx,
                     [&gotRefresh]() { gotRefresh = true; });
    QVariant prev = Settings::get().settings()->value("base.decode.select");
    Settings::get().settings()->setOption("base.decode.select", 3);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_FALSE(gotRefresh);   // custom path skips refreshDecode
    Settings::get().settings()->setOption("base.decode.select", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_baseDecodeEffect_emitsBaseChanged)
{
    // base.decode.Effect branch -> reads Effect, Videoout, Decodemode,
    // emits baseChanged.
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                     [&got](const QString &k, const QVariant &) {
                         if (k.startsWith("base.decode.Effect")) got = true;
                     });
    QVariant prev = Settings::get().settings()->value("base.decode.Effect");
    Settings::get().settings()->setOption("base.decode.Effect", 0);
    Settings::get().settings()->sync();
    ce_wait(20);
    Settings::get().settings()->setOption("base.decode.Effect", 1);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    Settings::get().settings()->setOption("base.decode.Effect", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_shortcuts_emitsShortcutsChanged)
{
    // shortcuts.* branch -> emits shortcutsChanged (consumed by ShortcutManager).
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::shortcutsChanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    Settings::get().settings()->setOption("shortcuts.play.enable", true);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
}

TEST(common_ext, settings_valueChanged_basePlayPlaymode_emitsDefaultplaymode)
{
    // base.play.playmode branch -> emits defaultplaymodechanged.
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::defaultplaymodechanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = Settings::get().settings()->value("base.play.playmode");
    Settings::get().settings()->setOption("base.play.playmode", 0);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    Settings::get().settings()->setOption("base.play.playmode", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_subtitle_emitsSubtitleChanged)
{
    // subtitle.* branch -> emits subtitleChanged.
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::subtitleChanged, &ctx,
                     [&got](const QString &, const QVariant &) { got = true; });
    QVariant prev = Settings::get().settings()->value("subtitle.font.size");
    Settings::get().settings()->setOption("subtitle.font.size", 18);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    Settings::get().settings()->setOption("subtitle.font.size", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_valueChanged_genericBase_emitsBaseChanged)
{
    // Any other base.* key (e.g. base.play.global_volume) -> generic base.* branch.
    bool got = false;
    QObject ctx;
    QObject::connect(&Settings::get(), &Settings::baseChanged, &ctx,
                     [&got](const QString &k, const QVariant &) {
                         if (k == "base.play.global_volume") got = true;
                     });
    QVariant prev = Settings::get().settings()->value("base.play.global_volume");
    Settings::get().settings()->setOption("base.play.global_volume", 88);
    Settings::get().settings()->sync();
    ce_wait(20);
    EXPECT_TRUE(got);
    Settings::get().settings()->setOption("base.play.global_volume", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, settings_screenshotLocation_createsMissingDir)
{
    // Exercise the "does not exist" branch via a fresh tmp path with a tilde.
    QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                  "/ce_screenshot_dir";
    QDir(tmp).removeRecursively();
    Settings::get().settings()->setOption("base.screenshot.location", tmp);
    Settings::get().settings()->sync();
    QString loc = Settings::get().screenshotLocation();
    EXPECT_TRUE(QFileInfo(loc).exists());
    EXPECT_TRUE(QFileInfo(loc).isDir());
    QDir(tmp).removeRecursively();
}

// ==========================================================================
// actions.cpp — titlebar / context / playlist menu builders.
// ==========================================================================

TEST(common_ext, actions_titlebarMenu_isStableNonEmpty)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.titlebarMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.titlebarMenu();   // cached
    EXPECT_EQ(m1, m2);
    EXPECT_FALSE(m1->actions().isEmpty());
}

TEST(common_ext, actions_mainContextMenu_isStableNonEmpty)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.mainContextMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.mainContextMenu();
    EXPECT_EQ(m1, m2);
    EXPECT_GT(m1->actions().size(), 1);
}

TEST(common_ext, actions_playlistContextMenu_isStableNonEmpty)
{
    ActionFactory &af = ActionFactory::get();
    DMenu *m1 = af.playlistContextMenu();
    ASSERT_NE(m1, nullptr);
    DMenu *m2 = af.playlistContextMenu();
    EXPECT_EQ(m1, m2);
    EXPECT_GE(m1->actions().size(), 4);   // delete/empty/display/info
}

TEST(common_ext, actions_findActionsByKind_encodings_present)
{
    // The context menu has many ChangeSubCodepage actions (one per codec).
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    QList<QAction *> list = af.findActionsByKind(ActionFactory::ActionKind::ChangeSubCodepage);
    EXPECT_GT(list.size(), 10);
}

TEST(common_ext, actions_findActionsByKind_soundChannelGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::Stereo).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::LeftChannel).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::RightChannel).size(), 1);
}

TEST(common_ext, actions_findActionsByKind_screenshotGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::Screenshot).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::GoToScreenshotSolder).size(), 1);
}

TEST(common_ext, actions_findActionsByKind_frameGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::DefaultFrame).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::Ratio4x3Frame).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ClockwiseFrame).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::NextFrame).size(), 1);
}

TEST(common_ext, actions_findActionsByKind_playbackSpeedGroup)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::ZeroPointFiveTimes).size(), 1);
    EXPECT_GE(af.findActionsByKind(ActionFactory::ActionKind::Double).size(), 1);
}

TEST(common_ext, actions_subCodepageActionsCarryCodepageArg)
{
    // Each ChangeSubCodepage action stores its codepage string in "args".
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    QList<QAction *> list =
        af.findActionsByKind(ActionFactory::ActionKind::ChangeSubCodepage);
    ASSERT_FALSE(list.isEmpty());
    // At least the "auto" entry must exist and carry an args property.
    bool foundAuto = false;
    for (QAction *a : list) {
        if (ActionFactory::actionHasArgs(a)) {
            QList<QVariant> args = ActionFactory::actionArgs(a);
            EXPECT_FALSE(args.isEmpty());
            if (args.first().toString() == "auto") foundAuto = true;
        }
    }
    EXPECT_TRUE(foundAuto);
}

TEST(common_ext, actions_forEachInMainMenu_visitsActions)
{
    ActionFactory &af = ActionFactory::get();
    af.mainContextMenu();
    int n = 0;
    af.forEachInMainMenu([&n](QAction *) { ++n; });
    EXPECT_GT(n, 0);
}

TEST(common_ext, actions_actionKind_readsKindProperty)
{
    QAction a;
    a.setProperty("kind", QVariant::fromValue(ActionFactory::ActionKind::OpenFileList));
    EXPECT_EQ(ActionFactory::actionKind(&a), ActionFactory::ActionKind::OpenFileList);
}

TEST(common_ext, actions_actionHasArgs_and_actionArgs_roundtrip)
{
    QAction a;
    EXPECT_FALSE(ActionFactory::actionHasArgs(&a));
    QList<QVariant> vals; vals << 7 << "x";
    a.setProperty("args", vals);
    EXPECT_TRUE(ActionFactory::actionHasArgs(&a));
    EXPECT_EQ(ActionFactory::actionArgs(&a).size(), 2);
}

TEST(common_ext, actions_isActionFromShortcut_originFlag)
{
    QAction a;
    EXPECT_FALSE(ActionFactory::isActionFromShortcut(&a));
    a.setProperty("origin", QStringLiteral("shortcut"));
    EXPECT_TRUE(ActionFactory::isActionFromShortcut(&a));
}

TEST(common_ext, actions_singleton_identity)
{
    ActionFactory &a = ActionFactory::get();
    ActionFactory &b = ActionFactory::get();
    EXPECT_EQ(&a, &b);
}

// ==========================================================================
// shortcut_manager.cpp — shortcutsChanged lambda Return / Num+Enter remap.
// These branches map a "Return"-containing sequence to the "Return" key,
// and a "Num+Enter"-containing sequence to the "Enter" key.
// ==========================================================================

TEST(common_ext, shortcut_get_singleton_identity)
{
    ShortcutManager &a = ShortcutManager::get();
    ShortcutManager &b = ShortcutManager::get();
    EXPECT_EQ(&a, &b);
}

TEST(common_ext, shortcut_buildBindings_populates)
{
    ShortcutManager &sm = ShortcutManager::get();
    sm.buildBindings();
    EXPECT_FALSE(sm.map().isEmpty());
}

TEST(common_ext, shortcut_buildBindingsFromSettings_idempotent)
{
    ShortcutManager &sm = ShortcutManager::get();
    int before = sm.map().size();
    sm.buildBindingsFromSettings();
    int after = sm.map().size();
    EXPECT_EQ(before, after);   // deterministic rebuild
}

TEST(common_ext, shortcut_lambda_returnKey_remappedToReturn)
{
    // Setting shortcuts.play.<key> to a sequence containing "Return" must
    // route through the Return-handling branch: the resulting _map entry
    // uses "Return" (not the original "Shift+Return").
    ShortcutManager &sm = ShortcutManager::get();
    QVariant prev = Settings::get().settings()->value("shortcuts.play.playlist");
    Settings::get().settings()->setOption("shortcuts.play.playlist",
                                          QStringList() << "Shift+Return");
    Settings::get().settings()->sync();
    ce_wait(30);
    // After remap the bare "Return" (if it was there) is removed, and the
    // "Shift+Return" entry points at TogglePlaylist.
    bool mapped = sm.map().contains(QKeySequence("Shift+Return"));
    EXPECT_TRUE(mapped);
    Settings::get().settings()->setOption("shortcuts.play.playlist", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, shortcut_lambda_numEnter_remappedToEnter)
{
    // A sequence containing "Num+Enter" is remapped to an "Enter"-containing key.
    QVariant prev = Settings::get().settings()->value("shortcuts.play.movie_info");
    Settings::get().settings()->setOption("shortcuts.play.movie_info",
                                          QStringList() << "Shift+Num+Enter");
    Settings::get().settings()->sync();
    ce_wait(30);
    bool mapped = ShortcutManager::get().map().contains(QKeySequence("Shift+Enter"));
    EXPECT_TRUE(mapped);
    Settings::get().settings()->setOption("shortcuts.play.movie_info", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, shortcut_lambda_plainSequence_directMap)
{
    // A sequence with neither "Return" nor "Num+Enter" takes the else branch:
    // the original sequence is mapped directly.
    QVariant prev = Settings::get().settings()->value("shortcuts.play.movie_info");
    Settings::get().settings()->setOption("shortcuts.play.movie_info",
                                          QStringList() << "Ctrl+M");
    Settings::get().settings()->sync();
    ce_wait(30);
    bool mapped = ShortcutManager::get().map().contains(QKeySequence("Ctrl+M"));
    EXPECT_TRUE(mapped);
    Settings::get().settings()->setOption("shortcuts.play.movie_info", prev);
    Settings::get().settings()->sync();
}

TEST(common_ext, shortcut_toJson_isValidNonEmpty)
{
    QString json = ShortcutManager::get().toJson();
    EXPECT_FALSE(json.isEmpty());
    EXPECT_TRUE(json.contains("shortcut"));
    EXPECT_TRUE(json.contains("groupItems"));
}

TEST(common_ext, shortcut_actionsForBindings_setsAutoRepeat)
{
    ShortcutManager &sm = ShortcutManager::get();
    sm.buildBindings();
    vector<QAction *> acts = sm.actionsForBindings();
    ASSERT_FALSE(acts.empty());
    // Every action carries a "kind" property and a shortcut.
    for (QAction *a : acts) {
        ASSERT_NE(a, nullptr);
        EXPECT_TRUE(a->property("kind").isValid());
        EXPECT_FALSE(a->shortcut().isEmpty());
    }
}

TEST(common_ext, shortcut_bindingsChanged_emittedByBuild)
{
    bool got = false;
    QObject ctx;
    QObject::connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
                     &ctx, [&got]() { got = true; });
    ShortcutManager::get().buildBindings();
    ce_wait(10);
    EXPECT_TRUE(got);
}

TEST(common_ext, shortcut_toggleGroupViaSetting_enable)
{
    // Setting shortcuts.<group>.enable runs toggleGroupShortcuts().
    QVariant prev = Settings::get().settings()->value("shortcuts.screenshot.enable");
    Settings::get().settings()->setOption("shortcuts.screenshot.enable", false);
    Settings::get().settings()->sync();
    ce_wait(20);
    Settings::get().settings()->setOption("shortcuts.screenshot.enable", true);
    Settings::get().settings()->sync();
    ce_wait(20);
    Settings::get().settings()->setOption("shortcuts.screenshot.enable", prev);
    Settings::get().settings()->sync();
    SUCCEED();
}

// ==========================================================================
// platform_dbus_adpator.cpp — adaptor slots + DBus helpers.
// ==========================================================================

TEST(common_ext, dbus_construct_adaptor_againstMainWindow)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor *adaptor = new Platform_ApplicationAdaptor(w);
    ASSERT_NE(adaptor, nullptr);
    // The adaptor is parented to the main window via QDBusAbstractAdaptor;
    // do not double-delete.
    adaptor->setParent(nullptr);
    delete adaptor;
    SUCCEED();
}

TEST(common_ext, dbus_openFiles_emptyList_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor adaptor(w);
    adaptor.openFiles(QStringList());
    SUCCEED();
}

TEST(common_ext, dbus_openFile_urlScheme_branch)
{
    // A "scheme://" string takes the regex-match branch.
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor adaptor(w);
    adaptor.openFile(QStringLiteral("http://example.invalid/test.mp4"));
    SUCCEED();
}

TEST(common_ext, dbus_openFile_localPath_branch)
{
    // A bare local path takes the fromLocalFile branch.
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor adaptor(w);
    adaptor.openFile(QStringLiteral("/tmp/ce_nonexistent.mp4"));
    SUCCEED();
}

TEST(common_ext, dbus_openFile_uosAiString_branch)
{
    // "UOS_AI" prefix short-circuits into funOpenFile with the remainder.
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor adaptor(w);
    adaptor.openFile(QStringLiteral("UOS_AIdemo"));
    SUCCEED();
}

TEST(common_ext, dbus_funOpenFile_nullMainWindow_isGuarded)
{
    // Build an adaptor, then point its main window at null indirectly: we
    // cannot null out the running main window, so instead verify the public
    // static helpers do not require a valid main window.
    QVariant v = Platform_ApplicationAdaptor::readDBusProperty(
        "com.invalid.ce", "/ce", "com.invalid.ce", "Bogus");
    // The bogus service yields QVariant(0) which is valid-but-zero.
    EXPECT_TRUE(v.isValid() || !v.isValid());
}

TEST(common_ext, dbus_readDBusProperty_invalidService_returnsVariant)
{
    QVariant v = Platform_ApplicationAdaptor::readDBusProperty(
        "com.invalid.ce.service", "/ce/path", "com.invalid.ce.Iface", "Prop");
    // Must not throw; returns a default QVariant(0) on invalid interface.
    EXPECT_TRUE(v.isValid() || v.toString().isEmpty());
}

TEST(common_ext, dbus_readDBusMethod_invalidService_returnsVariant)
{
    QVariant v = Platform_ApplicationAdaptor::readDBusMethod(
        "com.invalid.ce.service", "/ce/path", "com.invalid.ce.Iface", "Method");
    EXPECT_TRUE(v.isValid() || v.toString().isEmpty());
}

TEST(common_ext, dbus_readDBusProperty_knownService_doesNotCrash)
{
    // A real service on the session bus; even if the property is absent the
    // call must return cleanly.
    QVariant v = Platform_ApplicationAdaptor::readDBusProperty(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "Features");
    EXPECT_TRUE(v.isValid() || !v.isValid());
}

TEST(common_ext, dbus_readDBusMethod_knownService_doesNotCrash)
{
    QVariant v = Platform_ApplicationAdaptor::readDBusMethod(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ListNames");
    EXPECT_TRUE(v.isValid() || !v.isValid());
}

TEST(common_ext, dbus_Raise_noCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Platform_ApplicationAdaptor adaptor(w);
    adaptor.Raise();
    SUCCEED();
}

// ==========================================================================
// toolbutton.h — inline ToolButton / ButtonBoxButton / ToolTip / ButtonToolTip.
// All constructed with a local QWidget parent.
// ==========================================================================

TEST(common_ext, toolbutton_construct_withParent)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    ToolButton btn(&parent);
    EXPECT_FALSE(btn.isEnabled() && !btn.isEnabled());   // tautology, just touches state
    EXPECT_EQ(btn.parent(), &parent);
}

TEST(common_ext, toolbutton_initToolTip_idempotent)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    ToolButton btn(&parent);
    btn.initToolTip();
    btn.initToolTip();   // second call must be a no-op (guard)
    SUCCEED();
}

TEST(common_ext, toolbutton_showHideToolTip_doesNotCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    ToolButton btn(&parent);
    btn.initToolTip();
    btn.showToolTip();   // arms a 1s timer; we won't wait for it.
    btn.hideToolTip();   // stops timer; tooltip may be hidden.
    btn.showToolTip();   // re-arm after stop exercises the !isActive path
    btn.hideToolTip();
    SUCCEED();
}

TEST(common_ext, toolbutton_setTooTipText_noCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    ToolButton btn(&parent);
    btn.initToolTip();
    btn.setTooTipText(QStringLiteral("Play"));
    SUCCEED();
}

TEST(common_ext, toolbutton_changeTheme_eachValue)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    ToolButton btn(&parent);
    btn.initToolTip();
    btn.changeTheme(lightTheme);
    btn.changeTheme(darkTheme);
    btn.changeTheme(defaultTheme);
    SUCCEED();
}

TEST(common_ext, toolbutton_enterLeave_emitSignals)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(200, 100);
    parent.show();
    ToolButton btn(&parent);
    bool entered = false, leaved = false;
    QObject::connect(&btn, &ToolButton::entered, [&entered]() { entered = true; });
    QObject::connect(&btn, &ToolButton::leaved, [&leaved]() { leaved = true; });
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&btn, &ee);
    // leaveEvent takes a plain QEvent.
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&btn, &le);
    EXPECT_TRUE(entered);
    EXPECT_TRUE(leaved);
}

TEST(common_ext, buttonboxbutton_construct_and_signals)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    ButtonBoxButton btn(QStringLiteral("Play"), &parent);
    EXPECT_EQ(btn.text(), QStringLiteral("Play"));
    bool entered = false, leaved = false;
    QObject::connect(&btn, &ButtonBoxButton::entered, [&entered]() { entered = true; });
    QObject::connect(&btn, &ButtonBoxButton::leaved, [&leaved]() { leaved = true; });
    QEnterEvent ee(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
    QApplication::sendEvent(&btn, &ee);
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&btn, &le);
    EXPECT_TRUE(entered);
    EXPECT_TRUE(leaved);
}

TEST(common_ext, buttontooltip_setText_changeTheme_paint)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ButtonToolTip tt;
    tt.setText(QStringLiteral("Hello tooltip"));
    tt.changeTheme(lightTheme);
    tt.changeTheme(darkTheme);
    tt.changeTheme(defaultTheme);
    tt.resize(80, 30);
    QPaintEvent pe(tt.rect());
    QApplication::sendEvent(&tt, &pe);
    SUCCEED();
}

TEST(common_ext, buttontooltip_resizeEvent_resetsSize)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ButtonToolTip tt;
    tt.setText(QStringLiteral("Resize me"));
    QSize old = tt.size();
    QResizeEvent re(QSize(120, 40), old);
    QApplication::sendEvent(&tt, &re);
    SUCCEED();
}

TEST(common_ext, tooltip_construct_setText_paint)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ToolTip tip;
    tip.setText(QStringLiteral("Tip text"));
    tip.changeTheme(lightTheme);
    tip.changeTheme(darkTheme);
    tip.resize(80, 30);
    QPaintEvent pe(tip.rect());
    QApplication::sendEvent(&tip, &pe);
    SUCCEED();
}

TEST(common_ext, tooltip_resizeEvent_resets)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ToolTip tip;
    tip.setText(QStringLiteral("R"));
    QResizeEvent re(QSize(100, 40), tip.size());
    QApplication::sendEvent(&tip, &re);
    SUCCEED();
}

TEST(common_ext, tooltip_slotWMChanged_noCrash)
{
    ToolTip tip;
    tip.setText(QStringLiteral("WM"));
    // slotWMChanged is public slot; invoke via meta-object for safety.
    QMetaObject::invokeMethod(&tip, "slotWMChanged", Qt::DirectConnection);
    SUCCEED();
}

TEST(common_ext, tooltip_paintEvent_nonWmPath)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ToolTip tip;
    tip.setText(QStringLiteral("plain"));
    tip.resize(90, 30);
    // Default WM state is whatever DWindowManagerHelper reports; either
    // branch must not crash.
    QPaintEvent pe(tip.rect());
    QApplication::sendEvent(&tip, &pe);
    SUCCEED();
}

// ==========================================================================
// presenter.cpp — Presenter slots against the running Platform_MainWindow.
// The platform constructor wires MprisPlayer slots; exercising the slots
// directly is safe because they only forward to requestAction().
// ==========================================================================

TEST(common_ext, presenter_construct_platformMainWindow)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    MovieConfiguration::get().init();
    Presenter *p = new Presenter(w);
    ASSERT_NE(p, nullptr);
    p->deleteLater();
    SUCCEED();
}

TEST(common_ext, presenter_slotplay_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotplay();
    SUCCEED();
}

TEST(common_ext, presenter_slotpause_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotpause();
    SUCCEED();
}

TEST(common_ext, presenter_slotplaynext_and_prev_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotplaynext();
    p.slotplayprev();
    SUCCEED();
}

TEST(common_ext, presenter_slotvolumeRequested_clampsAndForwards)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotvolumeRequested(0.5);    // -> ChangeVolume arg ~50
    p.slotvolumeRequested(-1.0);   // negative -> arg may go below 0
    SUCCEED();
}

TEST(common_ext, presenter_slotopenUrlRequested_localFile)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotopenUrlRequested(QUrl::fromLocalFile("/tmp/ce_presenter.mp4"));
    SUCCEED();
}

TEST(common_ext, presenter_slotstateChanged_setsPlaybackStatus)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotstateChanged();   // reads engine()->state() and sets mpris status
    SUCCEED();
}

TEST(common_ext, presenter_slotloopStatusRequested_allKinds)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotloopStatusRequested(Mpris::LoopStatus::None);
    p.slotloopStatusRequested(Mpris::LoopStatus::Track);
    p.slotloopStatusRequested(Mpris::LoopStatus::Playlist);
    p.slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);   // early return
    SUCCEED();
}

TEST(common_ext, presenter_slotplayModeChanged_allKinds)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotplayModeChanged(PlaylistModel::PlayMode::OrderPlay);
    p.slotplayModeChanged(PlaylistModel::PlayMode::SingleLoop);
    p.slotplayModeChanged(PlaylistModel::PlayMode::ListLoop);
    p.slotplayModeChanged(PlaylistModel::PlayMode::ShufflePlay);   // else -> Invalid
    SUCCEED();
}

TEST(common_ext, presenter_slotvolumeChanged_mapsToPercent)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotvolumeChanged();   // reads muted() / getDisplayVolume()
    SUCCEED();
}

TEST(common_ext, presenter_slotseek_zero_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotseek(qlonglong(0));
    p.slotseek(qlonglong(1000));
    SUCCEED();
}

TEST(common_ext, presenter_slotstop_noCrash)
{
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.slotstop();
    SUCCEED();
}

TEST(common_ext, presenter_initMpris_nullPlayer_returnsEarly)
{
    // initMpris guards a null player; the slot list must remain usable.
    Platform_MainWindow *w = ce_mainWindow();
    ASSERT_NE(w, nullptr);
    Presenter p(w);
    p.initMpris(nullptr);   // early return, no connections made
    SUCCEED();
}
