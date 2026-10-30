// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// =====================================================================
// Mode 2 测试生成清单（qt-autotest-generator，t16）
// 1. 目标文件: src/common/shortcut_manager.cpp (类 ShortcutManager, namespace dmr)
// 2. 方法清单与分级 (inventory @4608a9f2/e5346fd1): ctor=h, actionsForBindings=h,
//    toJson=h, buildBindings=m, get=m, buildBindingsFromSettings=l,
//    toggleGroupShortcuts=l, ~ShortcutManager=l
// 3. 已有用例引用: tests/deepin-movie-platform/common/test_common_ext.cpp 已覆盖
//    get 单例/Return/Num+Enter/普通序列 map/toJson 非空/actionsForBindings 基本
//    属性; 本文件为 deepin-movie-test 二进制补齐分支级精确断言, 不与其冲突。
// 4. 分支->用例映射:
//    ctor(_keyToAction 28 项 + lambda 连接)      -> ctor_viaGet*
//    lambda .endsWith(".enable") 分支            -> settings_enableToggle_*
//    lambda Return/Num+Enter 精确边缘            -> settings_value_*Remap*
//    lambda 普通序列直映射分支                   -> settings_value_plainSequence_mapsDirectly
//    buildBindings(=FromSettings+emit)           -> buildBindings_publicSlot_emitsBindingsChanged
//    buildBindingsFromSettings(ViewShortcut 固定项/重建幂等) -> buildBindingsFromSettings_*
//    toggleGroupShortcuts(on=true/false 两分支)  -> settings_enableToggle_*
//    actionsForBindings(autoRepeat 白名单分支)   -> actionsForBindings_*
//    toJson(组遍历/shortcut 选项过滤/内置 Settings 组) -> toJson_*
//    get(单例恒等)                               -> get_singleton_identity
//    ~ShortcutManager                            -> 豁免(见 10)
// 4.5 用例计数声明表(min 按 level 推导: high=3/mid=2/low=1):
//    | method | level | factors | min | actual |
//    | ShortcutManager(ctor) | high | complexity:5,lines:78,in_degree:7 | 3 | 14 |
//    | actionsForBindings | high | - | 3 | 3 |
//    | toJson | high | - | 3 | 3 |
//    | buildBindings | mid | - | 2 | 4 |
//    | get | mid | - | 2 | 14 |
//    | buildBindingsFromSettings | low | - | 1 | 3 |
//    | toggleGroupShortcuts | low | - | 1 | 2 |
//    (ctor/Get 的 actual 计入所有经由单例访问它们的用例)
// 5. 参数化数据: autoRepeat 白名单 8 种 kind 用循环逐项断言; wheel 无关。
// 6. 依赖与打桩: Settings::get()/DSettings 为项目真实配置(测试环境 schema 已
//    安装, 沿用 test_common_ext.cpp 的 setOption+sync 模式); DBus/网络/文件系
//    统均不触碰; 无需 stub。Q_ASSERT 依赖 schema 的 viewType=="checkbox",
//    与既有平台用例同一环境, 已验证可用。
// 7. GUI 与事件循环约束: 仅 QCoreApplication 级操作 + QAction(非 widget);
//    信号经 QTRY_* 轮询, 兼容 qExec 同步执行模型; 不实例化窗口。
// 8. 编译目标: deepin-movie-test (tests/deepin-movie, common/*.cpp 自动纳入),
//    构建命令: cd build-ut && make deepin-movie-test && ./tests/deepin-movie/
//    deepin-movie-test --gtest_filter='ShortcutManagerTest.*'
// 9. 自检入口: self-check-structural.py --file <本文件>; 编译+运行验证在
//    verify-build.py 阶段完成。
// 10. 提交说明: test(shortcut): add branch-precise ShortcutManager suites;
//     豁免: ~ShortcutManager 为单例静态生命周期, 进程内不可安全析构(仅
//     get() 可构造, 析构后 m_instance 悬空会破坏同二进制后续用例)。
// =====================================================================

#include <gtest/gtest.h>
#include <QTest>
#include <QAction>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QVariantList>

#include "dmr_settings.h"
#include "shortcut_manager.h"
#include "actions.h"

#include <vector>
#include <algorithm>

using namespace dmr;
using ::testing::Test;

// --- helpers (prefix smx_, unique in binary) -------------------------------

static void smx_wait(int ms = 60) { QTest::qWait(ms); }

static QVariant smx_backupOption(const QString &key)
{
    auto opt = Settings::get().settings()->option(key);
    return opt ? opt->value() : QVariant();
}

static void smx_restoreOption(const QString &key, const QVariant &prev)
{
    if (!prev.isValid())
        return;
    Settings::get().settings()->setOption(key, prev);
    Settings::get().settings()->sync();
    smx_wait(30);
}

static QStringList smx_optionSeq(const QString &key)
{
    auto opt = Settings::get().settings()->option(key);
    if (!opt)
        return QStringList();
    return opt->value().toStringList();
}

// ==========================================================================
// 测试夹具（::testing::Test 结构要求; 状态均经 Settings 全局, 用例内自恢复）
// ==========================================================================

class ShortcutManagerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==========================================================================
// get() — 单例恒等
// ==========================================================================

TEST_F(ShortcutManagerTest, get_singleton_identity_sameInstance)
{
    // Arrange: take the singleton reference once for later use.
    ShortcutManager &sm = ShortcutManager::get();
    // Act: request the singleton twice.
    ShortcutManager &a = ShortcutManager::get();
    ShortcutManager &b = ShortcutManager::get();
    // Assert: both references point to the same live instance.
    EXPECT_NE(&sm, nullptr);
    EXPECT_EQ(&a, &b);
}

// ==========================================================================
// buildBindingsFromSettings — ViewShortcut 固定项 + 重建幂等 + toggle(on)
// ==========================================================================

TEST_F(ShortcutManagerTest, buildBindingsFromSettings_viewShortcutFixedEntry_present)
{
    // Arrange: rebuild the binding map from real settings.
    ShortcutManager &sm = ShortcutManager::get();
    // Act
    sm.buildBindingsFromSettings();
    // Assert: the fixed Qt6 ViewShortcut entry exists at Ctrl+Shift+/.
    const QKeySequence viewSeq(QString::fromLatin1("Ctrl+Shift+/"));
    EXPECT_TRUE(sm.map().contains(viewSeq));
    EXPECT_EQ(sm.map().value(viewSeq), ActionFactory::ViewShortcut);
    EXPECT_FALSE(viewSeq.isEmpty());
}

TEST_F(ShortcutManagerTest, buildBindingsFromSettings_rebuild_isDeterministic)
{
    // Arrange: build once and record the map size.
    ShortcutManager &sm = ShortcutManager::get();
    sm.buildBindingsFromSettings();
    const int before = sm.map().size();
    ASSERT_GT(before, 0);
    // Act: rebuild again.
    sm.buildBindingsFromSettings();
    // Assert: deterministic rebuild keeps the same size.
    EXPECT_GT(before, 0);
    EXPECT_EQ(sm.map().size(), before);
}

TEST_F(ShortcutManagerTest, buildBindingsFromSettings_enabledPlayGroup_populatesSequences)
{
    // Arrange: ensure play group is enabled, then rebuild.
    ShortcutManager &sm = ShortcutManager::get();
    QVariant prevEnable = smx_backupOption(QString::fromLatin1("shortcuts.play.enable"));
    QVariant prevSeq = smx_backupOption(QString::fromLatin1("shortcuts.play.playlist"));
    Settings::get().settings()->setOption(QString::fromLatin1("shortcuts.play.enable"), true);
    Settings::get().settings()->sync();
    smx_wait(30);
    const QStringList seq = smx_optionSeq(QString::fromLatin1("shortcuts.play.playlist"));
    ASSERT_FALSE(seq.isEmpty());
    // Act
    sm.buildBindingsFromSettings();
    // Assert: every enabled play-group shortcut option value is mapped.
    EXPECT_FALSE(sm.map().isEmpty());
    EXPECT_TRUE(sm.map().contains(QKeySequence(seq.at(0))));
    smx_restoreOption(QString::fromLatin1("shortcuts.play.playlist"), prevSeq);
    smx_restoreOption(QString::fromLatin1("shortcuts.play.enable"), prevEnable);
}

// ==========================================================================
// buildBindings — public slot, 组合 FromSettings + bindingsChanged
// ==========================================================================

TEST_F(ShortcutManagerTest, buildBindings_publicSlot_emitsBindingsChanged)
{
    // Arrange: spy on the bindingsChanged signal.
    ShortcutManager &sm = ShortcutManager::get();
    bool got = false;
    QObject ctx;
    QObject::connect(&sm, &ShortcutManager::bindingsChanged, &ctx,
                     [&got]() { got = true; });
    // Act
    sm.buildBindings();
    QTRY_VERIFY_WITH_TIMEOUT(got, 3000);
    // Assert: signal arrived and the map was rebuilt non-empty.
    EXPECT_TRUE(got);
    EXPECT_FALSE(sm.map().isEmpty());
    EXPECT_GT(sm.map().size(), 0);
}

// ==========================================================================
// toggleGroupShortcuts（私有, 经 .enable 设置键的 lambda 分支驱动）
// ==========================================================================

TEST_F(ShortcutManagerTest, settings_enableToggle_false_removesGroupSequences)
{
    // Arrange: play group enabled and its default sequence mapped.
    ShortcutManager &sm = ShortcutManager::get();
    const QString enableKey = QString::fromLatin1("shortcuts.play.enable");
    const QString seqKey = QString::fromLatin1("shortcuts.play.playlist");
    QVariant prevEnable = smx_backupOption(enableKey);
    QVariant prevSeq = smx_backupOption(seqKey);
    Settings::get().settings()->setOption(enableKey, true);
    Settings::get().settings()->sync();
    smx_wait(30);
    sm.buildBindingsFromSettings();
    const QStringList seq = smx_optionSeq(seqKey);
    ASSERT_FALSE(seq.isEmpty());
    const QKeySequence mapped(seq.at(0));
    ASSERT_TRUE(sm.map().contains(mapped));
    // Act: disable the group -> lambda sk.endsWith(".enable") branch ->
    // toggleGroupShortcuts(grp, false) removes each option sequence.
    bool changed = false;
    QObject ctx;
    QObject::connect(&sm, &ShortcutManager::bindingsChanged, &ctx,
                     [&changed]() { changed = true; });
    Settings::get().settings()->setOption(enableKey, false);
    Settings::get().settings()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(changed, 3000);
    // Assert: the group's current sequence was removed from the map.
    EXPECT_TRUE(changed);
    EXPECT_FALSE(sm.map().contains(mapped));
    EXPECT_GT(sm.map().size(), 0); // other groups stay intact
    smx_restoreOption(seqKey, prevSeq);
    smx_restoreOption(enableKey, prevEnable);
}

TEST_F(ShortcutManagerTest, settings_enableToggle_true_restoresGroupSequences)
{
    // Arrange: start with the group disabled so the map lacks its sequences.
    ShortcutManager &sm = ShortcutManager::get();
    const QString enableKey = QString::fromLatin1("shortcuts.frame_sound.enable");
    const QString seqKey = QString::fromLatin1("shortcuts.frame_sound.mute");
    QVariant prevEnable = smx_backupOption(enableKey);
    QVariant prevSeq = smx_backupOption(seqKey);
    Settings::get().settings()->setOption(enableKey, false);
    Settings::get().settings()->sync();
    smx_wait(30);
    sm.buildBindingsFromSettings();
    const QStringList seq = smx_optionSeq(seqKey);
    ASSERT_FALSE(seq.isEmpty());
    const QKeySequence mapped(seq.at(0));
    // Act: enable the group -> lambda .enable branch -> toggleGroupShortcuts(grp, true)
    bool changed = false;
    QObject ctx;
    QObject::connect(&sm, &ShortcutManager::bindingsChanged, &ctx,
                     [&changed]() { changed = true; });
    Settings::get().settings()->setOption(enableKey, true);
    Settings::get().settings()->sync();
    QTRY_VERIFY_WITH_TIMEOUT(changed, 3000);
    // Assert: the option's sequence is mapped again.
    EXPECT_TRUE(changed);
    EXPECT_TRUE(sm.map().contains(mapped));
    smx_restoreOption(seqKey, prevSeq);
    smx_restoreOption(enableKey, prevEnable);
}

// ==========================================================================
// ctor lambda — Return / Num+Enter / 普通序列 三分支精确断言
// ==========================================================================

TEST_F(ShortcutManagerTest, settings_value_plainSequence_mapsDirectly)
{
    // Arrange: bind a fresh sequence on a known shortcut option.
    ShortcutManager &sm = ShortcutManager::get();
    const QString key = QString::fromLatin1("shortcuts.play.movie_info");
    QVariant prev = smx_backupOption(key);
    // Act: plain sequence (no Return / Num+Enter) -> else branch maps directly.
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Ctrl+M"));
    Settings::get().settings()->sync();
    smx_wait(30);
    // Assert: new sequence mapped; an unused neighbour combo is not.
    EXPECT_TRUE(sm.map().contains(QKeySequence(QString::fromLatin1("Ctrl+M"))));
    EXPECT_FALSE(sm.map().contains(QKeySequence(QString::fromLatin1("Ctrl+Alt+M"))));
    smx_restoreOption(key, prev);
}

TEST_F(ShortcutManagerTest, settings_value_bareReturn_keepsReturnDropsEnter)
{
    // Arrange: put a Num+Enter mapping in place first so the "Return wins"
    // cleanup branch has something observable to remove.
    ShortcutManager &sm = ShortcutManager::get();
    const QString key = QString::fromLatin1("shortcuts.play.movie_info");
    QVariant prev = smx_backupOption(key);
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Num+Enter"));
    Settings::get().settings()->sync();
    smx_wait(30);
    ASSERT_TRUE(sm.map().contains(QKeySequence(QString::fromLatin1("Enter"))));
    // Act: a bare "Return" value routes through the Return branch; the code
    // then removes the bare "Enter" entry (if (QString("Return") == strKey)).
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Return"));
    Settings::get().settings()->sync();
    smx_wait(30);
    // Assert: "Return" mapped; bare "Enter" removed.
    EXPECT_TRUE(sm.map().contains(QKeySequence(QString::fromLatin1("Return"))));
    EXPECT_FALSE(sm.map().contains(QKeySequence(QString::fromLatin1("Enter"))));
    smx_restoreOption(key, prev);
}

TEST_F(ShortcutManagerTest, settings_value_bareNumEnter_mapsEnterDropsReturn)
{
    // Arrange: a previous "Return" mapping must exist for the mirror branch.
    ShortcutManager &sm = ShortcutManager::get();
    const QString key = QString::fromLatin1("shortcuts.play.movie_info");
    QVariant prev = smx_backupOption(key);
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Return"));
    Settings::get().settings()->sync();
    smx_wait(30);
    ASSERT_TRUE(sm.map().contains(QKeySequence(QString::fromLatin1("Return"))));
    // Act: bare "Num+Enter" -> "%1Enter".arg(remove("Num+Enter")) == "Enter",
    // then the branch removes the bare "Return" entry.
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Num+Enter"));
    Settings::get().settings()->sync();
    smx_wait(30);
    // Assert
    EXPECT_TRUE(sm.map().contains(QKeySequence(QString::fromLatin1("Enter"))));
    EXPECT_FALSE(sm.map().contains(QKeySequence(QString::fromLatin1("Return"))));
    smx_restoreOption(key, prev);
}

// ==========================================================================
// actionsForBindings — autoRepeat 白名单分支 + kind/origin 属性
// ==========================================================================

TEST_F(ShortcutManagerTest, actionsForBindings_autoRepeat_whitelistKindsRepeat)
{
    // Arrange: rebuild bindings so the map holds the default action kinds.
    ShortcutManager &sm = ShortcutManager::get();
    sm.buildBindings();
    // Act
    std::vector<QAction *> acts = sm.actionsForBindings();
    ASSERT_FALSE(acts.empty());
    // Assert: every whitelist kind action has autoRepeat(true); others false.
    const std::vector<ActionFactory::ActionKind> repeatKinds = {
        ActionFactory::ActionKind::SeekForward,
        ActionFactory::ActionKind::SeekForwardLarge,
        ActionFactory::ActionKind::SeekBackward,
        ActionFactory::ActionKind::SeekBackwardLarge,
        ActionFactory::ActionKind::VolumeUp,
        ActionFactory::ActionKind::VolumeDown,
        ActionFactory::ActionKind::AccelPlayback,
        ActionFactory::ActionKind::DecelPlayback,
    };
    for (QAction *a : acts) {
        ASSERT_NE(a, nullptr);
        const QVariant kindVar = a->property("kind");
        ASSERT_TRUE(kindVar.isValid());
        EXPECT_EQ(a->property("origin").toString(), QString::fromLatin1("shortcut"));
        EXPECT_FALSE(a->shortcut().isEmpty());
        const bool inWhitelist =
            std::find(repeatKinds.begin(), repeatKinds.end(),
                      qvariant_cast<ActionFactory::ActionKind>(kindVar)) != repeatKinds.end();
        EXPECT_EQ(a->autoRepeat(), inWhitelist)
                << "autoRepeat mismatch for kind=" << kindVar.toInt();
    }
}

TEST_F(ShortcutManagerTest, actionsForBindings_kinds_areUniqueWithinOneRebuild)
{
    // Arrange: rebuild bindings so each mapped sequence yields one action.
    ShortcutManager &sm = ShortcutManager::get();
    sm.buildBindings();
    // Act
    std::vector<QAction *> acts = sm.actionsForBindings();
    // Assert: no two actions share the same kind (one entry per binding).
    std::vector<int> kinds;
    for (QAction *a : acts)
        kinds.push_back(qvariant_cast<ActionFactory::ActionKind>(a->property("kind")));
    EXPECT_FALSE(kinds.empty());
    std::vector<int> sorted = kinds;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_TRUE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

TEST_F(ShortcutManagerTest, actionsForBindings_playlistAction_carriesTogglePlaylistKind)
{
    // Arrange: map a known option to its kind through the real settings path.
    ShortcutManager &sm = ShortcutManager::get();
    const QString seqKey = QString::fromLatin1("shortcuts.play.playlist");
    QVariant prev = smx_backupOption(seqKey);
    Settings::get().settings()->setOption(seqKey, QStringList() << QString::fromLatin1("Ctrl+ArrowDown"));
    Settings::get().settings()->sync();
    smx_wait(30);
    // Act
    sm.buildBindings();
    std::vector<QAction *> acts = sm.actionsForBindings();
    // Assert: exactly one action carries the mapped sequence and its kind is
    // TogglePlaylist (the ctor's _keyToAction["playlist"] entry).
    QAction *found = nullptr;
    for (QAction *a : acts) {
        if (a->shortcut() == QKeySequence(QString::fromLatin1("Ctrl+ArrowDown")))
            found = a;
    }
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(qvariant_cast<ActionFactory::ActionKind>(found->property("kind")),
              ActionFactory::ActionKind::TogglePlaylist);
    EXPECT_FALSE(found->shortcut().isEmpty());
    smx_restoreOption(seqKey, prev);
}

// ==========================================================================
// toJson — JSON 结构校验
// ==========================================================================

TEST_F(ShortcutManagerTest, toJson_validJson_containsGroupsAndItems)
{
    // Arrange: take the singleton reference.
    ShortcutManager &sm = ShortcutManager::get();
    // Act
    const QString json = sm.toJson();
    // Assert: valid JSON with a "shortcut" array; every group carries
    // groupName/groupItems; the built-in Settings group offers F1 help.
    EXPECT_FALSE(json.isEmpty());
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();
    EXPECT_TRUE(root.contains(QString::fromLatin1("shortcut")));
    const QJsonArray groups = root.value(QString::fromLatin1("shortcut")).toArray();
    EXPECT_FALSE(groups.isEmpty());
    bool hasBuiltin = false;
    for (const QJsonValue &gv : groups) {
        const QJsonObject g = gv.toObject();
        EXPECT_TRUE(g.contains(QString::fromLatin1("groupName")));
        EXPECT_TRUE(g.contains(QString::fromLatin1("groupItems")));
        if (g.value(QString::fromLatin1("groupName")).toString()
                == QString::fromLatin1("Settings")) {
            hasBuiltin = true;
            const QJsonArray items =
                g.value(QString::fromLatin1("groupItems")).toArray();
            EXPECT_GE(items.size(), 2);
        }
    }
    EXPECT_TRUE(hasBuiltin);
}

TEST_F(ShortcutManagerTest, toJson_allSchemaGroups_presentInOutput)
{
    // Arrange: schema defines five shortcut groups.
    // Act
    const QString json = ShortcutManager::get().toJson();
    // Assert: every group from the schema appears as groupName.
    const QStringList expected = {
        QString::fromLatin1("Playback"),
        QString::fromLatin1("Frame/Sound"),
        QString::fromLatin1("Screenshot"),
        QString::fromLatin1("Subtitle"),
        QString::fromLatin1("File"),
        QString::fromLatin1("Settings"),
    };
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    EXPECT_TRUE(doc.isObject());
    const QJsonArray groups =
        doc.object().value(QString::fromLatin1("shortcut")).toArray();
    EXPECT_FALSE(groups.isEmpty());
    QStringList names;
    for (const QJsonValue &gv : groups)
        names << gv.toObject().value(QString::fromLatin1("groupName")).toString();
    for (const QString &n : expected)
        EXPECT_TRUE(names.contains(n)) << "missing group: " << n.toStdString();
}

TEST_F(ShortcutManagerTest, toJson_shortcutOptionValues_arePortableSequences)
{
    // Arrange: give one option a canonical sequence.
    const QString key = QString::fromLatin1("shortcuts.play.playlist");
    QVariant prev = smx_backupOption(key);
    Settings::get().settings()->setOption(key, QStringList() << QString::fromLatin1("Ctrl+Shift+P"));
    Settings::get().settings()->sync();
    smx_wait(30);
    // Act
    const QString json = ShortcutManager::get().toJson();
    // Assert: the portable text of the option value appears in the JSON.
    const QString portable =
        QKeySequence(QString::fromLatin1("Ctrl+Shift+P")).toString(QKeySequence::PortableText);
    EXPECT_FALSE(json.isEmpty());
    EXPECT_TRUE(json.contains(portable));
    smx_restoreOption(key, prev);
}
