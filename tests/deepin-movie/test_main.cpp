// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// T19 Mode2: src/main.cpp 可测函数 —— deepin-movie 主线二进制 (新增)
//
// 用例数: 12   覆盖方法: 4/4 可测函数 (runSingleInstance, getFunctionMovieName,
//   killOldMovie, getDecodeConfigInfo)
//
// 不可测方法 (进程级副作用, 经 inventory 标记 testable=false):
//   * main            —— 378 行应用入口, 走 exit 退出路径, 需完整应用栈
//   * checkIsCanHwdec —— 构造 QApplication + X11 Display + exit(-1)
//   * appX11IOErrorHandler —— _exit(1) / pthread_exit 直接终止进程//
// 分支清单 → 用例映射 (源码以工作目录 src/main.cpp 为准):
//   runSingleInstance: 非 root 路径 + lockf 成功 → runSingleInstance_lockAndPath
//   runSingleInstance: open 失败 (目录不可创建) → runSingleInstance_lockAndPath
//     (lockf 被占分支需跨进程持有锁, 进程内不可构造, 不覆盖)
//   getFunctionMovieName: copilot 服务无效 → else 分支返回 ""
//                                              → getFunctionMovieName_noService_returnsEmpty
//   getFunctionMovieName: 合法 JSON 提取 arguments.name → "UOS_AI"+name
//                                              → getFunctionMovieName_fakeService_parsesName
//   getFunctionMovieName: 非法 JSON → isObject 失败返回 ""
//                                              → getFunctionMovieName_fakeService_badJson_returnsEmpty
//   getFunctionMovieName: arguments 无 name 键 → 内层循环走完返回 ""
//                                              → getFunctionMovieName_fakeService_noNameArg_returnsEmpty
//   killOldMovie: parts<3 跳过 / 非本程序命令行跳过 / 自身 pid 跳过 /
//     取最早启动进程 kill → killOldMovie_killsEarliestOnly
//   killOldMovie: 空输入 → 无 kill → killOldMovie_emptyInput_noKill
//   getDecodeConfigInfo: select 越界 → 整体 no-op
//                                              → getDecodeConfigInfo_outOfRange_noOp
//   getDecodeConfigInfo: 普通模式 (0..2) → 仅 Select-Mode
//                                              → getDecodeConfigInfo_normalMode_logsSelectMode
//   getDecodeConfigInfo: 专业模式 X11 分支 (Effect/Render)
//                                              → getDecodeConfigInfo_proMode_x11Branch
//   getDecodeConfigInfo: 专业模式 Wayland 分支 (无 Effect/Render)
//                                              → getDecodeConfigInfo_proMode_waylandBranch
//   getDecodeConfigInfo: 专业模式 Effect/Decodemode/Videoout 越界子分支
//                                              → getDecodeConfigInfo_proMode_outOfRangeSubIdx
//
// 桩策略:
//   * src/main.cpp 不在测试目标源码 GLOB 内, 经 #define main app_main 后
//     #include 整个 main.cpp, 规避其 main() 入口冲突 (其余函数原样被测)。
//   * killOldMovie: dmr::utils::runPipeProcess 返回构造的 ps 行; QProcess::start /
//     waitForFinished 捕获替换 —— 绝不真实 kill 进程。
//   * getDecodeConfigInfo: utils::check_wayland_env 经 stub_ext 控制分支;
//     qInstallMessageHandler 捕获日志断言输出内容。
//   * runSingleInstance: DStandardPaths::writableLocation 经 stub_ext 指向
//     不可创建目录覆盖 open 失败分支; 真实路径用例仅写用户配置目录。
//   * getFunctionMovieName: QDBusAbstractInterface::isValid/call 经 stub_ext 替换,
//     不依赖真实 uos-ai-assistant 服务是否在线 (本机常驻 com.deepin.copilot,
//     注册同名服务法不可行)

#include <QtWidgets>
#include <QtDBus>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <gtest/gtest.h>

#include "stub/stubext.h"

// 规避 main.cpp 的 main() 入口: 重命名后引入其余可测函数
#define main app_main
#include "../../src/main.cpp"
#undef main

using namespace dmr;

namespace {

// --- killOldMovie 捕获 --------------------------------------------------------
static QStringList g_psLines;             // runPipeProcess 返回的 ps 行
static QList<QStringList> g_killArgs;     // QProcess::start 捕获的参数组

// --- getFunctionMovieName 假服务 ------------------------------------------------

static QString g_copilotJson;

// copilot DBus 交互打桩: isValid/call 经 stub_ext 替换, 不依赖真实
// uos-ai-assistant 服务是否在线 (本机常驻 com.deepin.copilot, 注册法不可行)
static void stubCopilotValid(stub_ext::StubExt &stub, bool valid)
{
    stub.set_lamda(&QDBusAbstractInterface::isValid,
                   [valid](const QDBusAbstractInterface *) -> bool { return valid; });
}

static void stubCopilotReply(stub_ext::StubExt &stub, const QString &json)
{
    stubCopilotValid(stub, true);
    stub.set_lamda(
        static_cast<QDBusMessage (QDBusAbstractInterface::*)(const QString &)>(
            &QDBusAbstractInterface::call),
        [json](QDBusAbstractInterface *, const QString &) -> QDBusMessage {
            QDBusMessage base = QDBusMessage::createMethodCall(
                QStringLiteral("com.deepin.copilot"), QStringLiteral("/com/deepin/copilot"),
                QStringLiteral("com.deepin.copilot"), QStringLiteral("cachedFunctions"));
            return base.createReply(QVariantList{json});
        });
}

// --- getDecodeConfigInfo 日志捕获 ------------------------------------------------

static QList<QString> g_msgs;

static void utMsgHandler(QtMsgType, const QMessageLogContext &, const QString &m)
{
    g_msgs << m;
}

struct MsgCapture
{
    QtMessageHandler prev = nullptr;
    MsgCapture()
    {
        g_msgs.clear();
        prev = qInstallMessageHandler(&utMsgHandler);
    }
    ~MsgCapture() { qInstallMessageHandler(prev); }
};

static int countMsgContaining(const QString &needle)
{
    int n = 0;
    for (const QString &m : g_msgs)
        if (m.contains(needle))
            ++n;
    return n;
}

// --- Settings 选项备份/恢复 -------------------------------------------------------

struct DecodeOptBackup
{
    QVariant select, effect, decodemode, videoout;
    explicit DecodeOptBackup()
    {
        auto s = Settings::get().settings();
        select = s->getOption(QStringLiteral("base.decode.select"));
        effect = s->getOption(QStringLiteral("base.decode.Effect"));
        decodemode = s->getOption(QStringLiteral("base.decode.Decodemode"));
        videoout = s->getOption(QStringLiteral("base.decode.Videoout"));
    }
    ~DecodeOptBackup()
    {
        auto s = Settings::get().settings();
        s->setOption(QStringLiteral("base.decode.select"), select);
        s->setOption(QStringLiteral("base.decode.Effect"), effect);
        s->setOption(QStringLiteral("base.decode.Decodemode"), decodemode);
        s->setOption(QStringLiteral("base.decode.Videoout"), videoout);
        s->sync();
    }
};

}  // namespace

class MainFunctionsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_psLines.clear();
        g_killArgs.clear();
        g_copilotJson.clear();
        g_msgs.clear();
    }

    void TearDown() override { m_stub.clear(); }

    stub_ext::StubExt m_stub;
};

// --- runSingleInstance -----------------------------------------------------------

TEST_F(MainFunctionsTest, runSingleInstance_lockAndPath)
{
    // Act + Assert: 非 root 用户走 AppConfigLocation, lockf 成功
    EXPECT_TRUE(runSingleInstance());
    // 同进程重复调用: lockf 记录锁进程内可重入, 仍返回 true
    EXPECT_TRUE(runSingleInstance());

    // open 失败分支: 指向不可创建目录 (无系统路径写入)
    m_stub.set_lamda(&Dtk::Core::DStandardPaths::writableLocation,
                     [](QStandardPaths::StandardLocation) -> QString {
                         return QStringLiteral("/proc/1/ut-t19-no-such-dir");
                     });
    EXPECT_FALSE(runSingleInstance());
}

// --- getFunctionMovieName ----------------------------------------------------------

TEST_F(MainFunctionsTest, getFunctionMovieName_noService_returnsEmpty)
{
    // Arrange: isValid → false (存根替换, 不依赖真实服务状态)
    stubCopilotValid(m_stub, false);

    // Act + Assert: isValid=false → else 分支返回 ""
    EXPECT_TRUE(getFunctionMovieName().isEmpty());
}

TEST_F(MainFunctionsTest, getFunctionMovieName_fakeService_parsesName)
{
    // Arrange: 存根返回含 arguments.name 的合法 JSON
    stubCopilotReply(m_stub, QStringLiteral(
        "{\"functions\":[{\"name\":\"play_movie\","
        "\"arguments\":\"{\\\"name\\\":\\\"播放本地视频\\\"}\"}]}"));

    // Act + Assert: 命中 name 键 → "UOS_AI" + name
    EXPECT_EQ(getFunctionMovieName(), QStringLiteral("UOS_AI播放本地视频"));
}

TEST_F(MainFunctionsTest, getFunctionMovieName_fakeService_badJson_returnsEmpty)
{
    // Arrange
    stubCopilotReply(m_stub, QStringLiteral("this is not json"));

    // Act + Assert: isObject 失败 → 返回 ""
    EXPECT_TRUE(getFunctionMovieName().isEmpty());
}

TEST_F(MainFunctionsTest, getFunctionMovieName_fakeService_noNameArg_returnsEmpty)
{
    // Arrange: arguments 对象无 name 键 → 内层循环走完无命中
    stubCopilotReply(m_stub, QStringLiteral(
        "{\"functions\":[{\"name\":\"play_movie\","
        "\"arguments\":\"{\\\"action\\\":\\\"open\\\"}\"}]}"));

    // Act + Assert
    EXPECT_TRUE(getFunctionMovieName().isEmpty());
}

// --- killOldMovie --------------------------------------------------------------------

TEST_F(MainFunctionsTest, killOldMovie_killsEarliestOnly)
{
    // Arrange: 构造 ps 输出 —— 自身 pid / 最早 / 较晚 / 坏行 / 非本程序
    const QString selfPid = QString::number(QCoreApplication::applicationPid());
    g_psLines = QStringList{
        QStringLiteral("%1  Mon Jan  1 12:00:00 2026 /usr/bin/deepin-movie").arg(selfPid),
        QStringLiteral("4242  Mon Jan  1 08:00:00 2026 /usr/bin/deepin-movie"),
        QStringLiteral("4243  Mon Jan  1 09:00:00 2026 /usr/bin/deepin-movie"),
        QStringLiteral("junk"),
        QStringLiteral("4244  Mon Jan  1 10:00:00 2026 /usr/bin/other-app")};
    m_stub.set_lamda(&dmr::utils::runPipeProcess,
                     [](const QString &, const QString &) -> QStringList {
                         return g_psLines;
                     });
    m_stub.set_lamda(
        static_cast<void (QProcess::*)(const QString &, const QStringList &,
                                       QIODevice::OpenMode)>(&QProcess::start),
        [](QProcess *, const QString &prog, const QStringList &args, QIODevice::OpenMode) {
            g_killArgs << (QStringList{prog} + args);
        });
    m_stub.set_lamda(static_cast<bool (QProcess::*)(int)>(&QProcess::waitForFinished),
                     [](QProcess *, int) -> bool { return true; });

    // Act
    killOldMovie();

    // Assert: 仅 kill 最早启动的 4242; 自身/较晚/坏行/非本程序均跳过
    ASSERT_EQ(g_killArgs.size(), 1);
    EXPECT_EQ(g_killArgs.at(0), (QStringList{QStringLiteral("kill"),
                                             QStringLiteral("4242")}));
}

TEST_F(MainFunctionsTest, killOldMovie_emptyInput_noKill)
{
    // Arrange: 空输入
    m_stub.set_lamda(&dmr::utils::runPipeProcess,
                     [](const QString &, const QString &) -> QStringList {
                         return QStringList{};
                     });
    m_stub.set_lamda(
        static_cast<void (QProcess::*)(const QString &, const QStringList &,
                                       QIODevice::OpenMode)>(&QProcess::start),
        [](QProcess *, const QString &, const QStringList &, QIODevice::OpenMode) {
            g_killArgs << QStringList{QStringLiteral("unexpected")};
        });
    m_stub.set_lamda(static_cast<bool (QProcess::*)(int)>(&QProcess::waitForFinished),
                     [](QProcess *, int) -> bool { return true; });

    // Act
    killOldMovie();

    // Assert: 无进程被 kill
    EXPECT_TRUE(g_killArgs.isEmpty());
}

// --- getDecodeConfigInfo ----------------------------------------------------------------

TEST_F(MainFunctionsTest, getDecodeConfigInfo_outOfRange_noOp)
{
    // Arrange: select 越界 (负值与超界) → 整体 no-op
    DecodeOptBackup backup;
    auto s = Settings::get().settings();
    s->setOption(QStringLiteral("base.decode.select"), -1);
    {
        MsgCapture cap;
        getDecodeConfigInfo();
        EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 0);
    }
    s->setOption(QStringLiteral("base.decode.select"), 99);
    {
        MsgCapture cap;
        getDecodeConfigInfo();
        EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 0);
    }
}

TEST_F(MainFunctionsTest, getDecodeConfigInfo_normalMode_logsSelectMode)
{
    // Arrange: 普通模式 (select=0) → 仅记录 Select-Mode
    DecodeOptBackup backup;
    auto s = Settings::get().settings();
    s->setOption(QStringLiteral("base.decode.select"), 0);

    // Act + Assert
    MsgCapture cap;
    getDecodeConfigInfo();
    EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Effect:")), 0);
}

TEST_F(MainFunctionsTest, getDecodeConfigInfo_proMode_x11Branch)
{
    // Arrange: 专业模式 (select=3) + 非 Wayland → Effect/Render 分支
    DecodeOptBackup backup;
    auto s = Settings::get().settings();
    s->setOption(QStringLiteral("base.decode.select"), 3);
    s->setOption(QStringLiteral("base.decode.Effect"), 0);
    s->setOption(QStringLiteral("base.decode.Decodemode"), 0);
    s->setOption(QStringLiteral("base.decode.Videoout"), 0);
    m_stub.set_lamda(&dmr::utils::check_wayland_env, []() -> bool { return false; });

    // Act + Assert
    MsgCapture cap;
    getDecodeConfigInfo();
    EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Effect:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Render:")), 1);
}

TEST_F(MainFunctionsTest, getDecodeConfigInfo_proMode_waylandBranch)
{
    // Arrange: 专业模式 + Wayland → 仅 Select-Mode + Hardware-Decode
    DecodeOptBackup backup;
    auto s = Settings::get().settings();
    s->setOption(QStringLiteral("base.decode.select"), 3);
    s->setOption(QStringLiteral("base.decode.Effect"), 0);
    s->setOption(QStringLiteral("base.decode.Decodemode"), 0);
    s->setOption(QStringLiteral("base.decode.Videoout"), 0);
    m_stub.set_lamda(&dmr::utils::check_wayland_env, []() -> bool { return true; });

    // Act + Assert
    MsgCapture cap;
    getDecodeConfigInfo();
    EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Hardware-Decode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Effect:")), 0);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Render:")), 0);
}

TEST_F(MainFunctionsTest, getDecodeConfigInfo_proMode_outOfRangeSubIdx)
{
    // Arrange: 专业模式 + Effect/Decodemode/Videoout 全部越界 → 空串兜底仍记录
    DecodeOptBackup backup;
    auto s = Settings::get().settings();
    s->setOption(QStringLiteral("base.decode.select"), 3);
    s->setOption(QStringLiteral("base.decode.Effect"), 99);
    s->setOption(QStringLiteral("base.decode.Decodemode"), 99);
    s->setOption(QStringLiteral("base.decode.Videoout"), 99);
    m_stub.set_lamda(&dmr::utils::check_wayland_env, []() -> bool { return false; });

    // Act + Assert
    MsgCapture cap;
    getDecodeConfigInfo();
    EXPECT_EQ(countMsgContaining(QStringLiteral("Select-Mode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Effect:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Hardware-Decode:")), 1);
    EXPECT_EQ(countMsgContaining(QStringLiteral("Render:")), 1);
}
