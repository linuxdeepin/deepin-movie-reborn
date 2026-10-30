// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch2: VolumeMonitoring (src/common/volumemonitoring.cpp)
//
// 用例数: 7   覆盖方法: 5/5 (ctor, dtor, start, stop, timeoutSlot)
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ctor: d_ptr/Private 创建 + connect + _bOpened=false → start_stop_togglesTimerAndDrivesTimeoutSlot
//   start: d->timer.start(1000)                → start_stop_togglesTimerAndDrivesTimeoutSlot
//   stop: d->timer.stop()                      → start_stop_togglesTimerAndDrivesTimeoutSlot
//   dtor: stop()                               → destructor_stopsTimer
//   timeoutSlot: SinkInputs 无效 return        → timeoutSlot_invalidSinkInputs_returnsEarly
//   timeoutSlot: 列表遍历 Name 不匹配 continue  → timeoutSlot_noMatchingSink_returnsEarly
//   timeoutSlot: sinkInputPath 为空 return     → timeoutSlot_noMatchingSink_returnsEarly
//   timeoutSlot: QDBusInterface 无效 return    → timeoutSlot_sinkInterfaceInvalid_returnsEarly
//   timeoutSlot: !_bOpened 首次发存储值+置位    → timeoutSlot_firstRun_emitsStoredVolumeMute
//   timeoutSlot: 音量变化发新值                → timeoutSlot_firstRun_emitsStoredVolumeMute (第二次调用)
//   timeoutSlot: 音量未变不发信号              → timeoutSlot_volumeUnchanged_noEmission
//
// 桩策略: 外部 DBus 服务 org.deepin.dde.Audio1 一律经 stub_ext 替换
// DBusUtils::readDBusProperty (本进程静态符号, 不触碰真实音频服务), 以属性名分派
// 受控返回值并计数; timeoutSlot 内部的真实 QDBusInterface( sinks 路径) 保持真实
// 构造 —— "接口无效"分支在无音频服务宿主天然成立, 在有音频服务宿主经 access()
// 预检后跳过, 保证确定性。
// Settings::get() 由 harness main 初始化(MovieConfiguration::get().init()), 直接
// setInternalOption 控制存储音量/静音值。

#include <gtest/gtest.h>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <sys/stat.h>

#include "volumemonitoring.h"
#include "dbusutils.h"
#include "dmr_settings.h"
#include "stub/stubext.h"

using namespace dmr;

namespace {

const QString kAudioSvc = QStringLiteral("org.deepin.dde.Audio1");

bool audioServiceAvailable()
{
    QDBusInterface probe(kAudioSvc, QStringLiteral("/org/deepin/dde/Audio1"),
                         kAudioSvc, QDBusConnection::sessionBus());
    return probe.isValid();
}

class VolumeMonitoringTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        // StubExt 析构自动恢复 DBusUtils 符号
    }

    stub_ext::StubExt m_stub;
};

// 属性名分派型受控桩: SinkInputs/Name/Volume/Mute 各自可控, 附带调用计数
struct DbusStubState
{
    int calls = 0;
    QVariant sinkInputs;   // 无效 QVariant → 首分支
    QVariant sinkName;     // Name 返回值
    double volume = 0.4242;
    bool mute = false;

    QVariant readDBusProperty(const QString &, const QString &,
                              const QString &, const char *pPropert)
    {
        ++calls;
        const QString prop = QString::fromLatin1(pPropert);
        if (prop == QLatin1String("SinkInputs"))
            return sinkInputs;
        if (prop == QLatin1String("Name"))
            return sinkName;
        if (prop == QLatin1String("Volume"))
            return QVariant(volume);
        if (prop == QLatin1String("Mute"))
            return QVariant(mute);
        return QVariant();
    }
};

static DbusStubState *s_stubState = nullptr;

TEST_F(VolumeMonitoringTest, start_stop_togglesTimerAndDrivesTimeoutSlot)
{
    // Arrange
    DbusStubState st;
    st.sinkInputs = QVariant();  // 无效 → timeoutSlot 早退, 只观察调用次数
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });
    VolumeMonitoring mon;

    // Act
    mon.start();
    QTest::qWait(1300);   // 定时器周期 1000ms → 至少触发一次 timeoutSlot
    int callsAfterStart = st.calls;
    mon.stop();
    QTest::qWait(1300);
    int callsAfterStop = st.calls;

    // Assert
    EXPECT_GE(callsAfterStart, 1);
    EXPECT_EQ(callsAfterStop, callsAfterStart);  // stop 后不再触发
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, destructor_stopsTimer)
{
    // Arrange
    DbusStubState st;
    st.sinkInputs = QVariant();
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });
    {
        VolumeMonitoring mon;
        mon.start();
    }   // ~VolumeMonitoring → stop()

    // Act: 等待两个周期, 析构后不应再触发
    QTest::qWait(1300);
    int calls = st.calls;

    // Assert
    QTest::qWait(1300);
    EXPECT_EQ(st.calls, calls);
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, timeoutSlot_invalidSinkInputs_returnsEarly)
{
    // Arrange
    VolumeMonitoring mon;
    QSignalSpy volSpy(&mon, &VolumeMonitoring::volumeChanged);
    QSignalSpy muteSpy(&mon, &VolumeMonitoring::muteChanged);
    DbusStubState st;
    st.sinkInputs = QVariant();  // 无效
    st.calls = 1;                // 直接调用, 已有一次计数
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });

    // Act
    mon.timeoutSlot();

    // Assert
    EXPECT_EQ(st.calls, 2);   // 仅 SinkInputs 一次查询后早退
    EXPECT_EQ(volSpy.count(), 0);
    EXPECT_EQ(muteSpy.count(), 0);
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, timeoutSlot_noMatchingSink_returnsEarly)
{
    // Arrange
    VolumeMonitoring mon;
    QSignalSpy volSpy(&mon, &VolumeMonitoring::volumeChanged);
    QSignalSpy muteSpy(&mon, &VolumeMonitoring::muteChanged);
    DbusStubState st;
    st.sinkInputs = QVariant::fromValue(
        QList<QDBusObjectPath>{QDBusObjectPath(QStringLiteral("/ut/sink1")),
                               QDBusObjectPath(QStringLiteral("/ut/sink2"))});
    st.sinkName = QVariant(QStringLiteral("Media Player"));  // 不含 Movie/deepin-movie
    st.calls = 1;
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });

    // Act
    mon.timeoutSlot();

    // Assert
    // 1(预热) + 1(SinkInputs) + 2(每个路径 Name 查询后 continue)
    EXPECT_EQ(st.calls, 4);
    EXPECT_EQ(volSpy.count(), 0);
    EXPECT_EQ(muteSpy.count(), 0);
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, timeoutSlot_sinkInterfaceInvalid_returnsEarly)
{
    // Arrange
    if (audioServiceAvailable())
        GTEST_SKIP() << "audio service present on host, invalid-interface branch unreachable";
    VolumeMonitoring mon;
    QSignalSpy volSpy(&mon, &VolumeMonitoring::volumeChanged);
    DbusStubState st;
    st.sinkInputs = QVariant::fromValue(
        QList<QDBusObjectPath>{QDBusObjectPath(QStringLiteral("/ut/sink1"))});
    st.sinkName = QVariant(QStringLiteral("deepin-movie"));  // 命中匹配
    st.calls = 1;
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });

    // Act
    mon.timeoutSlot();

    // Assert
    // 1(SinkInputs) + 1(Name 命中) → QDBusInterface 无效 → 不再查 Volume/Mute
    EXPECT_EQ(st.calls, 2);
    EXPECT_EQ(volSpy.count(), 0);
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, timeoutSlot_firstRun_emitsStoredVolumeMute)
{
    // Arrange
    if (!audioServiceAvailable())
        GTEST_SKIP() << "audio service absent, valid-interface branches unreachable";
    const QVariant oldVolumeBackup =
        Settings::get().internalOption(QStringLiteral("global_volume"));
    const QVariant oldMuteBackup =
        Settings::get().internalOption(QStringLiteral("mute"));
    Settings::get().setInternalOption(QStringLiteral("global_volume"), 100);
    Settings::get().setInternalOption(QStringLiteral("mute"), false);
    VolumeMonitoring mon;
    QSignalSpy volSpy(&mon, &VolumeMonitoring::volumeChanged);
    QSignalSpy muteSpy(&mon, &VolumeMonitoring::muteChanged);
    DbusStubState st;
    st.sinkInputs = QVariant::fromValue(
        QList<QDBusObjectPath>{QDBusObjectPath(QStringLiteral("/ut/sink1"))});
    st.sinkName = QVariant(QStringLiteral("deepin-Movie"));  // 大小写不敏感命中
    st.volume = 0.4242;   // → 42
    st.mute = false;
    st.calls = 1;
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });

    // Act + Assert (第一次: 发存储值 100, 置 _bOpened)
    mon.timeoutSlot();
    EXPECT_EQ(volSpy.count(), 1);
    EXPECT_EQ(volSpy.at(0).at(0).toInt(), 100);   // 存储值, 非 dbus 值 42
    EXPECT_EQ(muteSpy.count(), 1);
    EXPECT_EQ(muteSpy.at(0).at(0).toBool(), false);

    // Act + Assert (第二次: dbus 音量 42 != 100 → 发新值)
    mon.timeoutSlot();
    EXPECT_EQ(volSpy.count(), 2);
    EXPECT_EQ(volSpy.at(1).at(0).toInt(), 42);
    EXPECT_EQ(muteSpy.count(), 2);
    EXPECT_EQ(muteSpy.at(1).at(0).toBool(), false);
    Settings::get().setInternalOption(QStringLiteral("global_volume"), oldVolumeBackup);
    Settings::get().setInternalOption(QStringLiteral("mute"), oldMuteBackup);
    s_stubState = nullptr;
}

TEST_F(VolumeMonitoringTest, timeoutSlot_volumeUnchanged_noEmission)
{
    // Arrange
    if (!audioServiceAvailable())
        GTEST_SKIP() << "audio service absent, valid-interface branches unreachable";
    const QVariant oldVolumeBackup =
        Settings::get().internalOption(QStringLiteral("global_volume"));
    const QVariant oldMuteBackup =
        Settings::get().internalOption(QStringLiteral("mute"));
    Settings::get().setInternalOption(QStringLiteral("global_volume"), 42);
    Settings::get().setInternalOption(QStringLiteral("mute"), true);
    VolumeMonitoring mon;
    QSignalSpy volSpy(&mon, &VolumeMonitoring::volumeChanged);
    QSignalSpy muteSpy(&mon, &VolumeMonitoring::muteChanged);
    DbusStubState st;
    st.sinkInputs = QVariant::fromValue(
        QList<QDBusObjectPath>{QDBusObjectPath(QStringLiteral("/ut/sink1"))});
    st.sinkName = QVariant(QStringLiteral("deepin-movie"));
    st.volume = 0.42;   // → int 42, 与存储值一致
    st.mute = false;
    st.calls = 1;
    s_stubState = &st;
    m_stub.set_lamda(&DBusUtils::readDBusProperty,
                     [](const QString &s, const QString &p, const QString &i, const char *m) {
                         return s_stubState->readDBusProperty(s, p, i, m);
                     });

    // Act
    mon.timeoutSlot();          // 首次: 发存储值
    int volAfterFirst = volSpy.count();
    mon.timeoutSlot();          // 第二次: 音量未变 → 不发

    // Assert
    EXPECT_EQ(volAfterFirst, 1);
    EXPECT_EQ(volSpy.count(), 1);       // 无新增
    EXPECT_EQ(muteSpy.count(), 1);
    Settings::get().setInternalOption(QStringLiteral("global_volume"), oldVolumeBackup);
    Settings::get().setInternalOption(QStringLiteral("mute"), oldMuteBackup);
    s_stubState = nullptr;
}

}  // namespace
