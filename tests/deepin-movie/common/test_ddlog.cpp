// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch3: ddlog (src/common/ddlog.cpp)
//
// 用例数: 3   覆盖方法: 3/3 (initLogRulesWatcher, applyLogRules, joinRules[间接])
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   initLogRulesWatcher: 记录 env + qunsetenv        → envRulesAppliedAndEnvCleared
//   initLogRulesWatcher: DConfig::create 失败 return → 不可达分支(实际不返回 null), 未映射
//   initLogRulesWatcher: applyLogRules(config)       → 全部用例(applyLogRules/joinRules
//     为匿名命名空间内部链接函数, 无法直接链接, 统一经公开入口 initLogRulesWatcher 间接覆盖)
//
// 桩策略: 使用真实 DConfig("org.deepin.movie") — 宿主 dconfig 服务可用且 log_rules
// 为 readwrite 键; 测试内 setValue 写入受控规则并恢复原值, 不依赖外部桩。
// envcat 自定义分类名不匹配默认规则(org.deepin.movie*), 结果确定。
// applyLogRules/joinRules 为匿名命名空间函数(内部链接), 无法 extern 声明直接调用;
// 全部经公开入口 initLogRulesWatcher 间接覆盖(其内部依次调 joinRules+applyLogRules)。

#include <gtest/gtest.h>
#include <QLoggingCategory>
#include <QScopedPointer>

#include <DConfig>

#include "ddlog.h"

namespace {

constexpr const char *kAppName = "org.deepin.movie";

class DdlogTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_config.reset(Dtk::Core::DConfig::create(kAppName, kAppName, nullptr));
        ASSERT_NE(m_config, nullptr);
        m_oldRules = m_config->value(QStringLiteral("log_rules")).toString();
    }

    void TearDown() override
    {
        if (m_config && m_config->value(QStringLiteral("log_rules")).toString() != m_oldRules)
            m_config->setValue(QStringLiteral("log_rules"), m_oldRules);
        m_config.reset();
        qunsetenv("QT_LOGGING_RULES");
    }

    // 写入受控规则; 键不可写时返回 false
    bool writeRules(const QString &rules)
    {
        m_config->setValue(QStringLiteral("log_rules"), rules);
        return m_config->value(QStringLiteral("log_rules")).toString() == rules;
    }

    QScopedPointer<Dtk::Core::DConfig> m_config;
    QString m_oldRules;
};

TEST_F(DdlogTest, envRulesAppliedAndEnvCleared)
{
    // Arrange: 清空 dconfig 规则, 隔离宿主残留(崩溃运行可能留下 *.debug=false
    // 这类追加在后会覆盖环境规则的值), 使 joinRules 仅返回环境规则
    ASSERT_TRUE(writeRules(QString()));
    QLoggingCategory envCat("ut.ddl.envcat");
    qputenv("QT_LOGGING_RULES", "ut.ddl.envcat.debug=true");

    // Act
    initLogRulesWatcher();

    // Assert
    // 1) 环境变量已被 qunsetenv 清除  2) 环境规则经 joinRules 合并并生效
    EXPECT_EQ(qgetenv("QT_LOGGING_RULES"), QByteArray());
    EXPECT_TRUE(envCat.isDebugEnabled());
}

TEST_F(DdlogTest, applyLogRules_readsDConfigAndApplies)
{
    // Arrange
    QLoggingCategory dcCat("ut.ddl.dccat");

    // Act: 写入启用 dccat 的规则并应用(经 initLogRulesWatcher 内部走 applyLogRules)
    if (!writeRules(QStringLiteral("ut.ddl.dccat.debug=true;  ;ut.ddl.dccat.debug=true"))) {
        GTEST_SKIP() << "log_rules key not writable in this environment";
    }
    initLogRulesWatcher();

    // Assert: dconfig 规则已生效; 空规则与重复规则不干扰(continue 分支)
    EXPECT_TRUE(dcCat.isDebugEnabled());

    // Act2: 应用全局关闭规则
    ASSERT_TRUE(writeRules(QStringLiteral("*.debug=false")));
    initLogRulesWatcher();

    // Assert2: dccat 与 appMovie 的 debug 均被关闭
    EXPECT_FALSE(QLoggingCategory("ut.ddl.dccat").isDebugEnabled());
    EXPECT_FALSE(appMovie().isDebugEnabled());
}

TEST_F(DdlogTest, applyLogRules_mergesEnvAndDconfigRules)
{
    // Arrange: 环境规则与 dconfig 规则分别命中不同分类
    QLoggingCategory envCat("ut.ddl.merge.env");
    QLoggingCategory dcCat("ut.ddl.merge.dc");
    if (!writeRules(QStringLiteral("ut.ddl.merge.dc.debug=true")))
        GTEST_SKIP() << "log_rules key not writable in this environment";
    qputenv("QT_LOGGING_RULES", "ut.ddl.merge.env.debug=true");

    // Act
    initLogRulesWatcher();

    // Assert: joinRules 先取环境规则再追加 dconfig 规则, 两者同时生效
    EXPECT_TRUE(envCat.isDebugEnabled());
    EXPECT_TRUE(dcCat.isDebugEnabled());
}

}  // namespace
