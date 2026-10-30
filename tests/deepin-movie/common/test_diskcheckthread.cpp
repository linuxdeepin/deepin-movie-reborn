// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch2: Diskcheckthread (src/common/diskcheckthread.cpp)
//
// 用例数: 8   覆盖方法: 4/4 (ctor, start, stop, diskChecking) + 信号 diskRemove
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ctor: setInterval(2000)+connect           → ctor_setsCheckIntervalAndConnects
//   start: m_timerDiskCheck.start()           → start_stop_toggleTimer (isActive=true)
//   stop: m_timerDiskCheck.stop()             → start_stop_toggleTimer (isActive=false)
//   diskChecking: /proc/mounts 打开失败 return → diskChecking_openFailed_returnsEarly
//   diskChecking: 行含 /dev/sr|/dev/cdrom     → 内容注入组 (newSrDisk/mountChanged/tracked/leftover)
//   diskChecking: ioctl 托盘开 (bOpen=false)   → 需真实光驱, 本环境无 /dev/sr* — 未映射(缺陷区说明)
//   diskChecking: 新光盘插入 (b4)              → diskChecking_newSrDisk_insertsMap
//   diskChecking: 挂载点变化 (b5)              → diskChecking_mountPointChanged_updatesMap
//   diskChecking: 已跟踪重复出现+bOpen (b6)     → diskChecking_trackedDiskRepeated_keepsMap
//   diskChecking: 托盘开移除映射 (b7)          → 同 ioctl 分支, 未映射
//   diskChecking: listDisk 残留 emit diskRemove (b8) → diskChecking_leftoverDisk_emitsSignal
//
// 桩策略: /proc/mounts 路径硬编码且为系统文件, 只能经 QFile::open/readLine/atEnd
// 符号替换注入受控行集(窗口期全局生效); 实测分析确认窗口内其它 QFile 使用方仅有
// harness 的光盘监测线程(注入行集对其为无害空转), 风险可控, 用例结束立即析构恢复。
// 特别注意: QFile::open(OpenMode) 与 QIODevice::atEnd() 均为虚函数, PMF 编码的是
// vtable 索引(实测 0x6e)而非代码地址, 直接 set_lamda 会段错误; 二者必须经
// AddrAny 从 libQt6Core 的 dynsym 解析真实代码地址后按自由函数指针打桩
// (虚派发的 vtable 表项即指向该代码地址, 对其打桩同样覆盖虚调用路径)。
// readLine(qint64) 为非虚函数, PMF 直打即可。
// ioctl 托盘状态分支需要真实光驱设备, 测试环境无 /dev/sr* —— 真实 ioctl 无法控制。
// 托盘状态经 AddrAny 从 libc dynsym 解析 ioctl 代码地址后打桩(x86_64 ioctl 调用点
// 至多 3 个寄存器参数, 3 参无变长桩在 ABI 上安全), 使 CDROM_DRIVE_STATUS 分支
// 在无设备宿主上可确定复现。
//
// 私有成员访问: m_mapDisk2Name/m_timerDiskCheck 经 addr_pri 私有访问宏, 仅用于
// 种子数据与状态断言, 不改变被测行为。

#include <gtest/gtest.h>
#include <QFile>
#include <QIODevice>
#include <QSignalSpy>
#include <QTimer>
#include <QThread>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/cdrom.h>
#include <map>
#include <string>

#include "diskcheckthread.h"
#include "stub/stubext.h"
#include "stub/addr_pri.h"
#include "stub/addr_any.h"

/* 类在全局命名空间, 无需 using namespace dmr */

// 暴露 protected slot diskChecking 供直调; receivers 为 protected 经包装访问
class TestDiskcheckthread : public Diskcheckthread
{
public:
    using Diskcheckthread::diskChecking;
    int receiverCount(const char *signal) const { return receivers(signal); }
};

// QTimer 的 timeout 信号接收者计数: receivers 为 protected, 经布局兼容 shim 转为 public。
// 注意信号挂在 m_timerDiskCheck 成员上, 必须 query timer 而非 Diskcheckthread 自身。
class QTimerProbe : public QObject
{
public:
    int receiverCount(const char *signal) { return receivers(signal); }
};

// 虚函数代码地址解析(libQt6Core dynsym), 进程内缓存一次
using QFileOpenFn = bool (*)(QFile *, QIODevice::OpenMode);
using QIOAtEndFn = bool (*)(const QIODevice *);
using IoctlFn = int (*)(int, unsigned long, void *);
static QFileOpenFn s_qfileOpen = nullptr;
static QIOAtEndFn s_qioAtEnd = nullptr;
static IoctlFn s_ioctl = nullptr;

static bool resolveQtSymbols()
{
    static bool resolved = false;
    if (resolved) return s_qfileOpen && s_qioAtEnd;
    AddrAny core("libQt6Core.so.6");   // addr_any.h 定义在全局命名空间
    std::map<std::string, void *> m1, m2;
    // regcomp flags=0 即 POSIX BRE, 括号本身为字面量
    core.get_global_func_addr_dynsym("^QFile::open(QFlags<QIODeviceBase::OpenModeFlag>)$", m1);
    core.get_global_func_addr_dynsym("^QIODevice::atEnd() const$", m2);   // const 成员 demangle 带后缀
    for (auto &kv : m1) { s_qfileOpen = reinterpret_cast<QFileOpenFn>(kv.second); break; }
    for (auto &kv : m2) { s_qioAtEnd = reinterpret_cast<QIOAtEndFn>(kv.second); break; }
    AddrAny libc("libc.so.6");
    std::map<std::string, void *> m3;
    // glibc 的 ioctl 符号绑定类型为 WEAK, 需走 weak 解析接口
    libc.get_weak_func_addr_dynsym("^ioctl$", m3);
    for (auto &kv : m3) { s_ioctl = reinterpret_cast<IoctlFn>(kv.second); break; }
    resolved = true;
    return s_qfileOpen && s_qioAtEnd;
}

// 私有成员访问器
using DiskMap = QMap<QString, QString>;
ACCESS_PRIVATE_FIELD(Diskcheckthread, DiskMap, m_mapDisk2Name)
ACCESS_PRIVATE_FIELD(Diskcheckthread, QTimer, m_timerDiskCheck)

namespace {

bool hostHasOpticalDevice()
{
    return access("/dev/sr0", F_OK) == 0 || access("/dev/cdrom", F_OK) == 0;
}

class DiskcheckthreadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // 虚函数打桩依赖 dynsym 解析, 失败则整套跳过(环境不支持时保持确定性)
        if (!resolveQtSymbols())
            GTEST_SKIP() << "libQt6Core QFile::open/QIODevice::atEnd symbol not resolved";
    }

    void TearDown() override
    {
        // StubExt 析构自动恢复全部符号替换
        fakeLines.clear();
        fakePos = 0;
    }

    // 注入受控 /proc/mounts 行集: 在 TearDown 前生效
    // open/atEnd 为虚函数, 必须用 AddrAny 解析出的代码地址打桩(见文件头说明,
    // 解析失败已在 SetUp 统一跳过)
    void stubMountFile(const QStringList &lines)
    {
        fakeLines = lines;
        fakePos = 0;
        m_stub.set_lamda(s_qfileOpen,
                         [](QFile *, QIODevice::OpenMode) { return true; });
        m_stub.set_lamda(static_cast<QByteArray (QIODevice::*)(qint64)>(&QIODevice::readLine),
                         [this](QIODevice *, qint64) -> QByteArray {
                             if (fakePos >= fakeLines.size())
                                 return QByteArray();
                             return fakeLines.at(fakePos++).toLatin1();
                         });
        m_stub.set_lamda(s_qioAtEnd,
                         [this](const QIODevice *) { return fakePos >= fakeLines.size(); });
    }

    stub_ext::StubExt m_stub;
    QStringList fakeLines;
    int fakePos = 0;
};

TEST_F(DiskcheckthreadTest, ctor_setsCheckIntervalAndConnects)
{
    // Arrange
    TestDiskcheckthread d;
    QTimer &timer = access_private_field::Diskcheckthreadm_timerDiskCheck(d);

    // Assert
    EXPECT_EQ(timer.interval(), 2000);
    // connect 有效性: timeout 信号挂在 m_timerDiskCheck 上(非 d 本身),
    // 经 shim 查询其 receivers, 应有且仅有一个接收者(diskChecking)
    EXPECT_EQ(reinterpret_cast<QTimerProbe &>(timer).receiverCount(SIGNAL(timeout())), 1);
}

TEST_F(DiskcheckthreadTest, start_stop_toggleTimer)
{
    // Arrange
    TestDiskcheckthread d;
    QTimer &timer = access_private_field::Diskcheckthreadm_timerDiskCheck(d);

    // Act
    d.start();

    // Assert
    EXPECT_TRUE(timer.isActive());
    d.stop();
    EXPECT_FALSE(timer.isActive());
}

TEST_F(DiskcheckthreadTest, diskChecking_openFailed_returnsEarly)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    m_stub.set_lamda(s_qfileOpen,
                     [](QFile *, QIODevice::OpenMode) { return false; });

    // Act
    d.diskChecking();

    // Assert
    EXPECT_TRUE(access_private_field::Diskcheckthreadm_mapDisk2Name(d).isEmpty());
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(DiskcheckthreadTest, diskChecking_noSrLines_keepsEmptyMap)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, mounts content not deterministic";
    stubMountFile(QStringList{});

    // Act
    // 空 fake 行集 → 立即 atEnd, 不进入 sr 分支; 等价于真实无光驱挂载表
    d.diskChecking();

    // Assert
    EXPECT_TRUE(access_private_field::Diskcheckthreadm_mapDisk2Name(d).isEmpty());
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(DiskcheckthreadTest, diskChecking_newSrDisk_insertsMap)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, ioctl result not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("/dev/sr0 /media/udf iso9660 ro 0 0\n")});

    // Act
    d.diskChecking();

    // Assert
    // open("/dev/sr0") 失败(-1, 环境无设备) → nFd 真值但 ioctl 非 CDS_TRAY_OPEN → bOpen=true → 新盘入表
    const QMap<QString, QString> &map =
        access_private_field::Diskcheckthreadm_mapDisk2Name(d);
    EXPECT_EQ(map.value(QStringLiteral("/dev/sr0")), QStringLiteral("/media/udf"));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(DiskcheckthreadTest, diskChecking_mountPointChanged_updatesMap)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, ioctl result not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("/dev/sr0 /new/mnt iso9660 ro 0 0\n")});
    access_private_field::Diskcheckthreadm_mapDisk2Name(d)
        .insert(QStringLiteral("/dev/sr0"), QStringLiteral("/old/mnt"));

    // Act
    d.diskChecking();

    // Assert
    const QMap<QString, QString> &map =
        access_private_field::Diskcheckthreadm_mapDisk2Name(d);
    EXPECT_EQ(map.value(QStringLiteral("/dev/sr0")), QStringLiteral("/new/mnt"));
    EXPECT_EQ(spy.count(), 0);  // listDisk 中 /old/mnt 已被 removeOne, 无残留
}

TEST_F(DiskcheckthreadTest, diskChecking_trackedDiskRepeated_keepsMap)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, ioctl result not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("/dev/sr0 /media/x iso9660 ro 0 0\n")});
    access_private_field::Diskcheckthreadm_mapDisk2Name(d)
        .insert(QStringLiteral("/dev/sr0"), QStringLiteral("/media/x"));

    // Act
    d.diskChecking();

    // Assert
    // b6 分支: 只从 listDisk 移除残留, 不改映射
    const QMap<QString, QString> &map =
        access_private_field::Diskcheckthreadm_mapDisk2Name(d);
    EXPECT_EQ(map.value(QStringLiteral("/dev/sr0")), QStringLiteral("/media/x"));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(DiskcheckthreadTest, diskChecking_leftoverDisk_emitsSignal)
{
    // Arrange
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, mounts content not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("proc /proc proc rw 0 0\n")});
    access_private_field::Diskcheckthreadm_mapDisk2Name(d)
        .insert(QStringLiteral("/dev/sr0"), QStringLiteral("/media/x"));

    // Act
    d.diskChecking();

    // Assert
    // 本轮挂载表无 sr 行 → 上一轮跟踪的挂载名残留 → emit diskRemove + 清空映射
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("/media/x"));
    EXPECT_TRUE(access_private_field::Diskcheckthreadm_mapDisk2Name(d).isEmpty());
}

TEST_F(DiskcheckthreadTest, diskChecking_trayOpenIoctl_skipsInsert)
{
    // Arrange: 桩 ioctl → CDS_TRAY_OPEN, 复现光驱托盘开启(源码 63-64 行分支)
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (!s_ioctl)
        GTEST_SKIP() << "ioctl dynsym not resolved";
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, mounts content not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("/dev/sr0 /media/cdrom0 iso9660 ro 0 0\n")});
    // open("/dev/sr0") 真实执行: 无光驱宿主返回 -1(真值), 有光驱宿主返回 fd≥0,
    // 两种情况 ioctl 均被桩接管, 结果确定
    m_stub.set_lamda(s_ioctl, [](int, unsigned long, void *) -> int {
        return CDS_TRAY_OPEN;
    });

    // Act
    d.diskChecking();

    // Assert: 托盘开启 → bOpen=false → 不插入映射
    EXPECT_TRUE(access_private_field::Diskcheckthreadm_mapDisk2Name(d).isEmpty());
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(DiskcheckthreadTest, diskChecking_trayOpenIoctl_removesTrackedDisk)
{
    // Arrange: 第一轮真实 ioctl(无光驱宿主 open 失败 fd=-1 → ioctl 返 EBADF ≠
    // CDS_TRAY_OPEN)插入映射; 第二轮桩 ioctl → CDS_TRAY_OPEN → 走托盘开启移除分支
    TestDiskcheckthread d;
    QSignalSpy spy(&d, &Diskcheckthread::diskRemove);
    if (!s_ioctl)
        GTEST_SKIP() << "ioctl dynsym not resolved";
    if (hostHasOpticalDevice())
        GTEST_SKIP() << "host has optical device, ioctl result not deterministic";
    stubMountFile(QStringList{
        QStringLiteral("/dev/sr0 /media/cdrom0 iso9660 ro 0 0\n")});

    // Act 1: 正常插入
    d.diskChecking();
    ASSERT_TRUE(access_private_field::Diskcheckthreadm_mapDisk2Name(d)
                    .contains(QStringLiteral("/dev/sr0")));

    // Act 2: 托盘开启 → 移除(源码 79-80 行分支)
    m_stub.set_lamda(s_ioctl, [](int, unsigned long, void *) -> int {
        return CDS_TRAY_OPEN;
    });
    fakePos = 0;   // 重放同一份 fake mounts
    d.diskChecking();

    // Assert
    EXPECT_FALSE(access_private_field::Diskcheckthreadm_mapDisk2Name(d)
                     .contains(QStringLiteral("/dev/sr0")));
}

}  // namespace
