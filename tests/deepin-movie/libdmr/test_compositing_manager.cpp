// Copyright (C) 2020 ~ 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 覆盖目标（工作单批次 1）：
//   - PlatformChecker::check     (bugs: 352745, 350929, 234565, 220641, 165659)
//   - detect550Series            (bugs: 352745, 350929, 266595, 97009, 41445)
// 说明：
//   PlatformChecker / detect550Series 定义于 compositing_manager.cpp（内部链接）。
//   - PlatformChecker 通过同名类副本声明 + 弱符号链接到测试二进制中已编译的实例；
//   - detect550Series 通过 AddrAny 从 SHT_SYMTAB 解析内部符号后调用。

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QProcess>
#include <QLibraryInfo>

#include <unistd.h>
#include <string>
#include <map>
#include <gtest/gtest.h>

#include "compositing_manager.h"
#include "utils.h"
#include "stub/stub.h"
#include "stub/addr_any.h"
#include "stub/stub_function.h"

using namespace dmr;

// ===== 存根全局状态 =====
static QByteArray g_ut_procStdout;      // uname -m 输出
static bool g_ut_waitStarted = true;
static bool g_ut_waitFinished = true;
static QStringList g_ut_pipeOut;        // runPipeProcess 返回

// QProcess 非静态成员：stub 首参接收 this
static bool ut_waitForStarted_stub(void *obj, int msecs)
{
    Q_UNUSED(obj);
    Q_UNUSED(msecs);
    return g_ut_waitStarted;
}
static bool ut_waitForFinished_stub(void *obj, int msecs)
{
    Q_UNUSED(obj);
    Q_UNUSED(msecs);
    return g_ut_waitFinished;
}
static QByteArray ut_readAllStdout_stub(void *obj)
{
    Q_UNUSED(obj);
    return g_ut_procStdout;
}
// runPipeProcess 为 dmr::utils 命名空间自由函数：无 this
static QStringList ut_runPipeProcess_stub(const QString &command, const QString &filter)
{
    Q_UNUSED(command);
    Q_UNUSED(filter);
    return g_ut_pipeOut;
}
// setProgram 置空阻止真实 spawn（避免 QProcess 析构 kill 子进程触发 SIGPIPE）
static void ut_setProgram_stub(void *obj, const QString &program)
{
    Q_UNUSED(obj);
    Q_UNUSED(program);
}

static void ut_installProcStubs(Stub &stub)
{
    stub.set(ADDR(QProcess, waitForStarted), ut_waitForStarted_stub);
    stub.set(ADDR(QProcess, waitForFinished), ut_waitForFinished_stub);
    stub.set(ADDR(QProcess, readAllStandardOutput), ut_readAllStdout_stub);
    stub.set(ADDR(QProcess, setProgram), ut_setProgram_stub);
}

// ===== PlatformChecker 类副本（链接 compositing_manager.o 中的弱符号） =====
namespace dmr {
class PlatformChecker
{
public:
    PlatformChecker() {}
    Platform check();
private:
    Platform _pf {Platform::Unknown};
};
}

// ===== detect550Series 内部符号解析 =====
typedef bool (*ut_detect550Series_fn)();
static ut_detect550Series_fn ut_resolveDetect550Series()
{
    // PIE 可执行文件：AddrAny 返回链接时 VA，需加运行时加载基址。
    // 用已知全局符号 runPipeProcess 的运行时地址（&fn 重定位后）与链接时 VA 的差值求基址。
    AddrAny anyBase;
    std::map<std::string, void *> baseResult;
    anyBase.get_global_func_addr_symtab("dmr::utils::runPipeProcess", baseResult);
    if (baseResult.empty()) {
        return nullptr;
    }
    intptr_t base = reinterpret_cast<intptr_t>(&dmr::utils::runPipeProcess)
                    - reinterpret_cast<intptr_t>(baseResult.begin()->second);

    AddrAny any;
    std::map<std::string, void *> result;
    // POSIX basic regex：() 为字面量；demangled 形如 "dmr::detect550Series()"
    any.get_local_func_addr_symtab("detect550Series()$", result);
    if (result.empty()) {
        return nullptr;
    }
    return reinterpret_cast<ut_detect550Series_fn>(
        reinterpret_cast<char *>(result.begin()->second) + base);
}

// ============================================================
// PlatformChecker::check —— 平台识别分支
// ============================================================

// x86_64 平台识别为 X86
TEST(PlatformChecker, check_X86)
{
    g_ut_procStdout = "x86_64\n";
    dmr::PlatformChecker pc;
    Stub stub;
    ut_installProcStubs(stub);

    EXPECT_EQ(Platform::X86, pc.check());
}

// alpha / sw_64（申威）识别为 Alpha
TEST(PlatformChecker, check_Alpha)
{
    Stub stub;
    ut_installProcStubs(stub);
    dmr::PlatformChecker pc;

    g_ut_procStdout = "alpha\n";
    EXPECT_EQ(Platform::Alpha, pc.check());

    dmr::PlatformChecker pc2;
    g_ut_procStdout = "sw_64\n";
    EXPECT_EQ(Platform::Alpha, pc2.check());
}

// mips / loongarch64（龙芯）识别为 Mips
TEST(PlatformChecker, check_Mips)
{
    Stub stub;
    ut_installProcStubs(stub);
    dmr::PlatformChecker pc;

    g_ut_procStdout = "mips64\n";
    EXPECT_EQ(Platform::Mips, pc.check());

    dmr::PlatformChecker pc2;
    g_ut_procStdout = "loongarch64\n";
    EXPECT_EQ(Platform::Mips, pc2.check());
}

// aarch64（飞腾/鲲鹏）识别为 Arm64
TEST(PlatformChecker, check_Arm64)
{
    g_ut_procStdout = "aarch64\n";
    dmr::PlatformChecker pc;
    Stub stub;
    ut_installProcStubs(stub);

    EXPECT_EQ(Platform::Arm64, pc.check());
}

// 未知架构（如 riscv）保持 Unknown
TEST(PlatformChecker, check_UnknownArch)
{
    g_ut_procStdout = "riscv64\n";
    dmr::PlatformChecker pc;
    Stub stub;
    ut_installProcStubs(stub);

    EXPECT_EQ(Platform::Unknown, pc.check());
}

// waitForStarted 失败 → 平台保持 Unknown（读 stderr 告警分支）
TEST(PlatformChecker, check_WaitStartedFail)
{
    g_ut_waitStarted = false;
    g_ut_procStdout = "x86_64\n";
    dmr::PlatformChecker pc;
    Stub stub;
    ut_installProcStubs(stub);

    EXPECT_EQ(Platform::Unknown, pc.check());

    g_ut_waitStarted = true;  // 恢复全局状态，避免污染后续用例
}

// waitForFinished 失败 → 平台保持 Unknown
TEST(PlatformChecker, check_WaitFinishedFail)
{
    g_ut_waitFinished = false;
    g_ut_procStdout = "x86_64\n";
    dmr::PlatformChecker pc;
    Stub stub;
    ut_installProcStubs(stub);

    EXPECT_EQ(Platform::Unknown, pc.check());

    g_ut_waitFinished = true;  // 恢复全局状态，避免污染后续用例
}

// ============================================================
// detect550Series —— 550 系列显卡检测（vaapi 路径，影响启动性能）
// ============================================================

// lspci 输出为空 → false
TEST(Detect550Series, EmptyList)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn) << "detect550Series symbol not found in symtab";

    Stub stub;
    g_ut_pipeOut = QStringList();
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_FALSE(fn());
}

// 无 550 系列设备 → false
TEST(Detect550Series, NoMatch)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn);

    Stub stub;
    g_ut_pipeOut = QStringList{"00:02.0 VGA compatible controller: 8086:1234", "01:00.0: 1002:67df"};
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_FALSE(fn());
}

// 命中 1002:699f（Radeon 540/550X）→ true
TEST(Detect550Series, Match699f)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn);

    Stub stub;
    g_ut_pipeOut = QStringList{"01:00.0 VGA compatible controller [0300]: 1002:699f"};
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_TRUE(fn());
}

// 命中 1002:6987（Lexa 540X/630）→ true
TEST(Detect550Series, Match6987)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn);

    Stub stub;
    g_ut_pipeOut = QStringList{"01:00.0 VGA compatible controller [0300]: 1002:6987"};
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_TRUE(fn());
}

// 命中 6766:3d02 → true
TEST(Detect550Series, Match6766_3d02)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn);

    Stub stub;
    g_ut_pipeOut = QStringList{"03:00.0 Display controller [0380]: 6766:3d02"};
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_TRUE(fn());
}

// 多行输出中第二行命中 → true（短路返回）
TEST(Detect550Series, MatchInLaterLine)
{
    ut_detect550Series_fn fn = ut_resolveDetect550Series();
    ASSERT_NE(nullptr, fn);

    Stub stub;
    g_ut_pipeOut = QStringList{"00:1f.3 Audio device [0400]: 8086:a0c8",
                               "01:00.0 VGA compatible controller [0300]: 1002:699f Lexa PRO"};
    stub.set(ADDR(utils, runPipeProcess), ut_runPipeProcess_stub);

    EXPECT_TRUE(fn());
}

// setTestFlag

// setTestFlag 翻转 _isCoreFlag，isTestFlag 回读一致；结束后恢复默认避免污染单例状态
TEST(CompositingManager, setTestFlag_TogglesCoreFlag)
{
    CompositingManager &mgr = CompositingManager::get();
    bool saved = mgr.isTestFlag();   // 保存现场（单例状态）

    mgr.setTestFlag(true);
    EXPECT_TRUE(mgr.isTestFlag());

    mgr.setTestFlag(false);
    EXPECT_FALSE(mgr.isTestFlag());

    mgr.setTestFlag(saved);   // 恢复
}
