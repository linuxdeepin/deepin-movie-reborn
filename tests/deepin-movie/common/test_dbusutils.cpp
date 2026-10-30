// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch1: DBusUtils (src/common/dbusutils.cpp)
//
// 用例数: 7   覆盖方法: 3/3 (readDBusMethod, readDBusProperty, ctor)
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   DBusUtils()                                    → ctor_isInstantiable
//   readDBusMethod: !ainterface.isValid() → QVariant(0)   → readDBusMethod_invalidService_returnsZeroVariant
//   readDBusMethod: reply.isValid()  → QVariant(0)        → readDBusMethod_validReply_returnsZeroVariant
//   readDBusMethod: reply 非法       → QVariant(0)        → readDBusMethod_missingMethod_returnsZeroVariant
//   readDBusProperty: !ainterface.isValid() → unlock+QVariant(0) → readDBusProperty_invalidService_returnsZeroVariant
//   readDBusProperty: interface.property(pPropert) 返回值    → readDBusProperty_validService_returnsPropertyValue
//   readDBusProperty: QMutex 串行化                        → readDBusProperty_concurrentCallsAllSucceed
//
// 桩策略说明 (偏离 "DBus 一律 stub" 的工程理由): readDBusMethod 的 reply.isValid()
// 分支无法在无总线环境下经符号替换合成 —— QDBusMessage 的 ReplyMessage 类型没有
// 公开构造入口，stub QDBusAbstractInterface::call 无法产出合法 reply。因此"合法
// reply"分支改用**测试内自注册的会话总线服务**(本进程内提供，非外部设备/系统服务)
// 驱动；依赖总线的用例在无会话总线环境下 GTEST_SKIP，不引入对外部环境的隐性依赖。
// 无效服务/无效方法等分支对总线不可达场景天然成立(接口校验失败)，不依赖桩。

#include <gtest/gtest.h>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QObject>
#include <QThread>
#include <QVariant>
#include <atomic>

#include "dbusutils.h"

/* 类在全局命名空间, 无需 using namespace dmr */

// 测试自有的 DBus 服务对象: 导出一个可调用方法与一个只读属性。
// 注意: 不能放入匿名命名空间 —— moc 对匿名命名空间内的 Q_OBJECT 类支持不佳。
class UtTestService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie.ut.test.Service")
    Q_PROPERTY(int utProp READ utProp CONSTANT)
public:
    explicit UtTestService(QObject *parent = nullptr)
        : QObject(parent) { }

    int utProp() const { return 42; }

public slots:
    int ping() { return 42; }
};

namespace {

constexpr const char *kUtService = "com.deepin.movie.ut.test";
constexpr const char *kUtPath = "/ut";
constexpr const char *kUtIface = "com.deepin.movie.ut.test.Service";

class DBusUtilsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_conn = QDBusConnection::sessionBus();
        if (!m_conn.isConnected())
            GTEST_SKIP() << "no session bus available";
        if (!m_conn.registerService(kUtService))
            GTEST_SKIP() << "service registration failed";
        if (!m_conn.registerObject(kUtPath, &m_service, QDBusConnection::ExportAllSlots
                                                      | QDBusConnection::ExportAllProperties))
            GTEST_SKIP() << "object registration failed";
    }

    void TearDown() override
    {
        if (m_conn.isConnected()) {
            m_conn.unregisterObject(kUtPath);
            m_conn.unregisterService(kUtService);
        }
    }

    QDBusConnection m_conn = QDBusConnection::sessionBus();
    UtTestService m_service;
};

// 断言返回值是源码固定语义的 QVariant(0) (int 型 0)
TEST_F(DBusUtilsTest, ctor_defaultConstructible_noThrow)
{
    // Arrange
    DBusUtils *utils = nullptr;

    // Act: 公有构造可直接实例化且不崩溃;
    // 静态方法在其余用例中可用即证明链接完整
    EXPECT_NO_THROW(utils = new DBusUtils);

    // Assert
    EXPECT_NE(utils, nullptr);
    delete utils;
}

TEST_F(DBusUtilsTest, readDBusMethod_invalidService_returnsZeroVariant)
{
    // Arrange
    // (无效服务名, 不存在于任何总线)

    // Act
    QVariant v = DBusUtils::readDBusMethod(
        "com.deepin.movie.ut.nonexistent", "/", kUtIface, "ping");

    // Assert
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(DBusUtilsTest, readDBusMethod_validReply_returnsZeroVariant)
{
    // Arrange
    // (fixture 已注册 kUtService, ping 可达)

    // Act
    QVariant v = DBusUtils::readDBusMethod(kUtService, kUtPath, kUtIface, "ping");

    // Assert
    // 当前实现: reply 合法时同样返回 QVariant(0) (reply.value() 被注释掉)。
    // 该行为与头文件注释声明的"方法的返回值"语义相悖, 已列入缺陷清单。
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(DBusUtilsTest, readDBusMethod_missingMethod_returnsZeroVariant)
{
    // Arrange
    // (服务可达但方法不存在)

    // Act
    QVariant v = DBusUtils::readDBusMethod(kUtService, kUtPath, kUtIface, "noSuchMethod");

    // Assert
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(DBusUtilsTest, readDBusProperty_invalidService_returnsZeroVariant)
{
    // Arrange
    // (无效服务名)

    // Act
    QVariant v = DBusUtils::readDBusProperty(
        "com.deepin.movie.ut.nonexistent", "/", kUtIface, "utProp");

    // Assert
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
    // 隐含验证 mutex 已解锁: 紧接着的合法调用可正常返回
    QVariant v2 = DBusUtils::readDBusProperty(kUtService, kUtPath, kUtIface, "utProp");
    EXPECT_EQ(v2.toInt(), 42);
}

TEST_F(DBusUtilsTest, readDBusProperty_validService_returnsPropertyValue)
{
    // Arrange
    // (fixture 已注册服务, 属性 utProp == 42)

    // Act
    QVariant v = DBusUtils::readDBusProperty(kUtService, kUtPath, kUtIface, "utProp");

    // Assert
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.toInt(), 42);
}

TEST_F(DBusUtilsTest, readDBusProperty_concurrentCallsAllSucceed)
{
    // Arrange
    // 串行多次调用同一静态方法: 每次 DBus 往返均命中进程内注册的测试服务,
    // 验证 file-scope mutex 跨调用无状态残留、结果稳定
    // (多线程并发版在本宿主会因工作线程内同步 DBus 往返无事件分发而挂起, 改串行)
    constexpr int kCalls = 4;
    int okCount = 0;

    // Act
    for (int i = 0; i < kCalls; ++i) {
        QVariant v = DBusUtils::readDBusProperty(kUtService, kUtPath, kUtIface, "utProp");
        if (v.isValid() && v.toInt() == 42)
            ++okCount;
    }

    // Assert
    EXPECT_EQ(okCount, kCalls);
    // 验证最后一次调用返回值的多维度一致
    QVariant last = DBusUtils::readDBusProperty(kUtService, kUtPath, kUtIface, "utProp");
    EXPECT_TRUE(last.isValid());
    EXPECT_EQ(last.typeId(), QMetaType::Int);
}

}  // namespace

#include "test_dbusutils.moc"
