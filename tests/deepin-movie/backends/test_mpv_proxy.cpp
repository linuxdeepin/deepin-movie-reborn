// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 覆盖：mpv_init / handle_mpv_events / configureJjwGPU / refreshDecode /
//       play / firstInit / initGpuInfoFuns / isSpecialHWHardware
// 测试环境约定（Qt6 + USE_TEST，_LIBDMR_ 未定义）：
//   1. libmpv 未链接进测试进程，所有 mpv 交互走成员函数指针，测试中直接赋值录制 stub；
//      fake handle 为哑指针，永不解引用。
//   2. MpvHandle 容器析构会触发真实 mpv_terminate_destroy，故测试中不 delete MpvProxy
//      （泄漏由 ASAN_OPTIONS=detect_leaks=0 容忍）。
//   3. CompositingManager::get().m_pMpvConfig 必须预分配，否则 mpv_init 尾部
//      m_pConfig->begin() 空指针解引用。
//   4. setState 存在缺陷：dynamic_cast<PlayerEngine*>(m_pParentWidget) 无空指针检查
//      （缺陷已记录 tests/.ut-defects.json），事件测试中必须 stub setState。

// —— Qt 头（在 #define private public 之前）——
#include <QtTest>
#include <QDebug>
#include <QTimer>
#include <QDir>
#include <QLibrary>
#include <QLibraryInfo>
#include <QProcess>
#include <QWidget>
#include <QUrl>
#include <QFileInfo>

#include <gtest/gtest.h>

#include <unistd.h>
#include <cstring>
#include <vector>

// —— 项目头（#define 获得成员访问权限）——
#define protected public
#define private public
#include "src/backends/mpv/mpv_proxy.h"
#include "src/libdmr/compositing_manager.h"
#include "src/libdmr/playlist_model.h"
#include "src/libdmr/movie_configuration.h"
#undef protected
#undef private

#include "src/libdmr/utils.h"
#include "src/common/dmr_settings.h"   // Settings 单例（重置前置 suite 残留的 decode.select）
#include "stub/stub.h"
#include "stub/addr_any.h"
#include "stub/stub_function.h"

using namespace dmr;

// isSpecialHWHardware 声明于 src/backends/mpv/mpv_proxy.cpp:110（namespace dmr 内，头文件未导出）
namespace dmr {
bool isSpecialHWHardware();
}

// ============================ 测试基建 ============================

namespace {

// fake mpv handle（哑指针，所有 mpv API 经 stub，永不解引用）
char g_ut_fakeHandleBuf[256];
mpv_handle *ut_fakeHandle()
{
    return reinterpret_cast<mpv_handle *>(g_ut_fakeHandleBuf);
}

// 录制容器
struct UtProp {
    QString name;
    QString value;
};
static QVector<UtProp> g_ut_props;        // my_set_property（m_bInited=true 时）
static QVector<UtProp> g_ut_propsAsync;   // my_set_property_async
static QVector<QVariantList> g_ut_cmds;   // my_command_async
static QVector<QString> g_ut_logLevels;   // m_requestLogMessage
static QVector<int> g_ut_setStates;       // setState（经 ADDR stub 录制）
static int g_ut_observeCalls = 0;         // m_observeProperty 调用计数

void ut_resetRecorders()
{
    g_ut_props.clear();
    g_ut_propsAsync.clear();
    g_ut_cmds.clear();
    g_ut_logLevels.clear();
    g_ut_setStates.clear();
    g_ut_observeCalls = 0;
}

// mpv_node → QString（录制解析辅助）
QString ut_nodeToString(const mpv_node *nd)
{
    if (!nd)
        return QString();
    switch (nd->format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(nd->u.string);
    case MPV_FORMAT_FLAG:   return nd->u.flag ? QStringLiteral("true") : QStringLiteral("false");
    case MPV_FORMAT_INT64:  return QString::number(static_cast<qlonglong>(nd->u.int64));
    case MPV_FORMAT_DOUBLE: return QString::number(nd->u.double_);
    default: return QString();
    }
}

// ============================ mpv API stub ============================
// 注意：这些函数被直接赋给 MpvProxy 的成员函数指针（非 stub 库安装），
// 签名必须与 mpv_proxy.h 中的 typedef 完全一致。

mpv_handle *ut_creat_stub()
{
    return ut_fakeHandle();
}

mpv_handle *ut_creat_null_stub()   // m_creat 返回 null（mpv_init 345 行检查分支验证）
{
    return nullptr;
}

int ut_initialize_stub(mpv_handle *ctx)
{
    Q_UNUSED(ctx);
    return 0;
}

int ut_initialize_fail_stub(mpv_handle *ctx)
{
    Q_UNUSED(ctx);
    return -1;
}

int ut_setProperty_stub(mpv_handle *ctx, const char *name, mpv_format format, void *data)
{
    Q_UNUSED(ctx);
    QString n = QString::fromUtf8(name);
    QString v;
    if (data && format == MPV_FORMAT_NODE)
        v = ut_nodeToString(static_cast<mpv_node *>(data));
    g_ut_props.append({n, v});
    return 0;
}

int ut_setPropertyAsync_stub(mpv_handle *ctx, uint64_t userdata, const char *name,
                             mpv_format format, void *data)
{
    Q_UNUSED(ctx);
    Q_UNUSED(userdata);
    QString n = QString::fromUtf8(name);
    QString v;
    if (data && format == MPV_FORMAT_NODE)
        v = ut_nodeToString(static_cast<mpv_node *>(data));
    g_ut_propsAsync.append({n, v});
    return 0;
}

int ut_commandNodeAsync_stub(mpv_handle *ctx, uint64_t userdata, mpv_node *args)
{
    Q_UNUSED(ctx);
    Q_UNUSED(userdata);
    QVariantList lst;
    if (args && args->format == MPV_FORMAT_NODE_ARRAY && args->u.list) {
        for (int i = 0; i < args->u.list->num; ++i)
            lst << ut_nodeToString(&args->u.list->values[i]);
    }
    g_ut_cmds.append(lst);
    return 0;
}

// mpv_eventName 存根：返回固定名称（END_FILE/IDLE 等分支日志会调用）
static const char *ut_eventName_stub(mpv_event_id event)
{
    Q_UNUSED(event);
    return "ut-event";
}

int ut_getProperty_stub(mpv_handle *ctx, const char *name, mpv_format format, void *data)
{
    Q_UNUSED(ctx);
    Q_UNUSED(name);
    if (format == MPV_FORMAT_NODE && data)
        static_cast<mpv_node *>(data)->format = MPV_FORMAT_NONE;
    return 0;
}

int ut_observeProperty_stub(mpv_handle *mpv, uint64_t reply_userdata, const char *name,
                            mpv_format format)
{
    Q_UNUSED(mpv);
    Q_UNUSED(reply_userdata);
    Q_UNUSED(name);
    Q_UNUSED(format);
    ++g_ut_observeCalls;
    return 0;
}

int ut_setOptionString_stub(mpv_handle *ctx, const char *name, const char *data)
{
    Q_UNUSED(ctx);
    Q_UNUSED(name);
    Q_UNUSED(data);
    return 0;
}

int ut_requestLogMessage_stub(mpv_handle *ctx, const char *min_level)
{
    Q_UNUSED(ctx);
    g_ut_logLevels.append(QString::fromUtf8(min_level));
    return 0;
}

void ut_setWakeupCallback_stub(mpv_handle *ctx, void (*cb)(void *d), void *d)
{
    Q_UNUSED(ctx);
    Q_UNUSED(cb);
    Q_UNUSED(d);
}

void ut_freeNodecontents_stub(mpv_node *node)
{
    Q_UNUSED(node);
}

// ============================ 事件序列 stub ============================

static QVector<mpv_event_id> g_ut_eventSeq;
static int g_ut_evIdx = 0;
static mpv_event g_ut_lastEvent;
static mpv_event_end_file g_ut_endFileData;

mpv_event *ut_waitEvent_stub(mpv_handle *ctx, double timeout)
{
    Q_UNUSED(ctx);
    Q_UNUSED(timeout);
    if (g_ut_evIdx >= g_ut_eventSeq.size()) {
        g_ut_lastEvent.event_id = MPV_EVENT_NONE;
        g_ut_lastEvent.error = 0;
        g_ut_lastEvent.reply_userdata = 0;
        g_ut_lastEvent.data = nullptr;
        return &g_ut_lastEvent;
    }
    mpv_event_id id = g_ut_eventSeq[g_ut_evIdx++];
    g_ut_lastEvent.event_id = id;
    g_ut_lastEvent.error = 0;
    g_ut_lastEvent.reply_userdata = 0;
    g_ut_lastEvent.data = nullptr;
    if (id == MPV_EVENT_END_FILE) {
        g_ut_endFileData.reason = MPV_END_FILE_REASON_EOF;
        g_ut_lastEvent.data = &g_ut_endFileData;
    }
    return &g_ut_lastEvent;
}

// ============================ 无参 no-op stub ============================
// 用于 ADDR 安装（成员函数 stub，忽略 this）

void ut_initMpvFuns_noop_stub() {}
void ut_refreshDecode_noop_stub() {}

// setState 录制 stub（绕开缺陷：setState 中 dynamic_cast 无空指针检查）
void ut_setState_rec_stub(void *obj, Backend::PlayState state)
{
    Q_UNUSED(obj);
    g_ut_setStates.append(static_cast<int>(state));
}

// ============================ QProcess / 环境存根 ============================

void ut_qprocWaitStarted_stub(void *, int) {}
void ut_qprocWaitFinished_stub(void *, int) {}
QByteArray ut_qprocReadKlvv_stub(void *) { return QByteArray("KLVV-XXXX Series\n"); }
QByteArray ut_qprocReadEmpty_stub(void *) { return QByteArray(); }
void ut_qprocSetProgram_stub(void *, const QString &) {}
bool ut_waylandTrue_stub() { return true; }
bool ut_libExistFalse_stub() { return false; }
bool ut_libExistTrue_stub() { return true; }
QFunctionPointer ut_qlibResolve_stub(const QString &, const char *)
{
    return (QFunctionPointer)(void *)0x1234;
}
QString ut_jjwPath_mwv207d_stub() { return QStringLiteral("mwv207d"); }
QString ut_jjwPath_jmgpu_stub() { return QStringLiteral("/dev/jmgpu"); }
QString ut_jjwPath_mwv206_stub() { return QStringLiteral("/dev/mwv206_0"); }
bool ut_qdirExistsTrue_stub(void *) { return true; }
const char *ut_gpuInfoVo_testvo_stub() { return "test-vo"; }
const char *ut_gpuInfoVo_x_stub() { return "x"; }

// ============================ 辅助 ============================

// GTest fixture（TEST_F 规范要求）；无前置状态，各用例自建 stub/proxy
// 用例命名规范：TEST_F(MpvProxyTest, {MethodName}_{Scenario}_{Expected})
class MpvProxyTest : public ::testing::Test {};

void ut_installMpvStubs(MpvProxy *proxy);   // 前置声明（定义在 ut_createPlayerEngine 前）

// 创建带 stub 的 MpvProxy；composited=true 使构造跳过 winId 相关路径
// 注意：composited() 是头文件内联函数（直接读 _composited 成员），stub 对内联副本不生效，
// 必须直接设置成员变量
MpvProxy *ut_createProxy(Stub &stub, QWidget *parent = nullptr, bool composited = true)
{
    // 防御：前置 suite（dmrsettings_ext.decodeAndEffectHandlers）可能残留 base.decode.select=3
    // （恢复时 setValue(QVariant) 未必生效），CUSTOM 分支会经 m_pConfig 覆盖 hwdec/vo 为 "vaapi"，
    // 每个用例前重置为 0 保证 mpv_init 断言确定性
    if (auto s = Settings::get().settings()) {
        if (auto opt = s->option("base.decode.select"))
            opt->setValue(0);
    }
    CompositingManager::get()._composited = composited;
    // 防御性预配置：m_pMpvConfig 为空会导致 mpv_init/refreshDecode 尾部崩溃
    if (!CompositingManager::get().m_pMpvConfig)
        CompositingManager::get().m_pMpvConfig = new QMap<QString, QString>();
    CompositingManager::get().m_bZXIntgraphics = false;   // 避开 apt policy QProcess 分支
    CompositingManager::setCanHwdec(true);                // hwdec=auto 不被拦截为 "no"
    MpvProxy *proxy = new MpvProxy(parent);
    ut_installMpvStubs(proxy);
    return proxy;
}

// 构造带空播放列表的 PlayerEngine parent（composited=true 保证构造安全）
// 安装全套 mpv API stub（nullptr 成员函数指针被调用会 SIGSEGV）
void ut_installMpvStubs(MpvProxy *proxy)
{
    proxy->m_creat = ut_creat_stub;
    proxy->m_initialize = ut_initialize_stub;
    proxy->m_setProperty = ut_setProperty_stub;
    proxy->m_setPropertyAsync = ut_setPropertyAsync_stub;
    proxy->m_commandNodeAsync = ut_commandNodeAsync_stub;
    proxy->m_getProperty = ut_getProperty_stub;
    proxy->m_observeProperty = ut_observeProperty_stub;
    proxy->m_setOptionString = ut_setOptionString_stub;
    proxy->m_requestLogMessage = ut_requestLogMessage_stub;
    proxy->m_setWakeupCallback = ut_setWakeupCallback_stub;
    proxy->m_freeNodecontents = ut_freeNodecontents_stub;
    proxy->m_eventName = ut_eventName_stub;
    proxy->m_waitEvent = ut_waitEvent_stub;
}

PlayerEngine *ut_createPlayerEngine(Stub &stub)
{
    CompositingManager::get()._composited = true;   // inline 函数直接读成员，stub 不生效
    if (!CompositingManager::get().m_pMpvConfig)
        CompositingManager::get().m_pMpvConfig = new QMap<QString, QString>();
    return new PlayerEngine(nullptr);
}

}  // namespace

// ============================ mpv_init ============================
// 源码路径：src/backends/mpv/mpv_proxy.cpp mpv_init()

// BUG376907：AUTO 模式 + 合成器开启，hwdec 应保留 "auto"（isCanHwdec 默认 true 不拦截），
// 且 composited 模式最终 vo 必须回退为 libmpv
TEST_F(MpvProxyTest, mpv_init_AutoModeCompositedKeepsHwdecAuto)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::AUTO;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    // AUTO 模式 → hwdec=auto（不被拦截）
    EXPECT_TRUE(proxy->m_mapWaitSet.contains("hwdec"));
    EXPECT_TRUE(proxy->m_mapWaitSet.value("hwdec").toString() == "auto");
    // composited 模式：vo 最终覆盖为 libmpv
    EXPECT_EQ(QString("libmpv"), proxy->m_sInitVo);
    EXPECT_TRUE(proxy->m_mapWaitSet.value("vo").toString() == "libmpv");
}

// m_creat 返回 null 时 mpv_init 应安全返回 nullptr（mpv_proxy.cpp:345 有空指针检查）
TEST_F(MpvProxyTest, mpv_init_CreateNullReturnsNull)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_creat = ut_creat_null_stub;

    mpv_handle *h = proxy->mpv_init();
    EXPECT_EQ(nullptr, h);
}

// 已知现状（缺陷记录）：m_initialize < 0 时源码构造了 std::runtime_error 却未 throw，
// 按"现状"验证：不抛异常且返回非空 handle，同时后续属性设置流程正常执行
TEST_F(MpvProxyTest, mpv_init_InitializeFailNoThrowByDesign)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_initialize = ut_initialize_fail_stub;
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    ut_resetRecorders();

    mpv_handle *h = nullptr;
    EXPECT_NO_THROW(h = proxy->mpv_init());
    // 缺陷现状：未 throw，仍返回 handle
    EXPECT_NE(nullptr, h);
}

// BUG351097：AUTO 模式下 gpuinfo VO 命中时，vo 应先记录 GPU 特定值，最终仍被 libmpv 覆盖
TEST_F(MpvProxyTest, mpv_init_GpuInfoVoRecordedBeforeCompositedOverride)
{
    Stub stub;
    QWidget *parent = new QWidget;
    MpvProxy *proxy = ut_createProxy(stub, parent, false);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::AUTO;
    // 假 gpuinfo VO：非空指针 → AUTO 分支 gpuinfo 块生效
    proxy->m_gpuInfoVo = ut_gpuInfoVo_testvo_stub;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    // GPU VO 推荐 vo 被记录：m_sInitVo 断言验证；m_mapWaitSet 中 vo 会被尾部 profile
    // 加载覆盖（default.profile 含 vo=opengl,xv,x11），故仅断言 vo 键曾被写入
    EXPECT_EQ(QString("test-vo"), proxy->m_sInitVo);
    EXPECT_TRUE(proxy->m_mapWaitSet.contains("vo"));
}

// SOFTWARE 模式：hwdec 强制为 no，composited 模式 vo 仍为 libmpv
TEST_F(MpvProxyTest, mpv_init_SoftwareModeForcesHwdecNo)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    EXPECT_EQ(QVariant("no").toString(), proxy->m_mapWaitSet.value("hwdec").toString());
    EXPECT_EQ(QString("libmpv"), proxy->m_sInitVo);
}

// debugLevel=Info 时应向 mpv 请求 "info" 级日志
TEST_F(MpvProxyTest, mpv_init_DebugLevelInfoRequestsInfoLogs)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    Backend::_debugLevel = Backend::DebugLevel::Info;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    EXPECT_TRUE(g_ut_logLevels.contains("info"));
}

// BUG356841：非合成器模式（composited=false）必须携带 wid 属性，且 vo 保持初始值不覆盖
TEST_F(MpvProxyTest, mpv_init_NotCompositedSetsWid)
{
    Stub stub;
    QWidget *parent = new QWidget;
    MpvProxy *proxy = ut_createProxy(stub, parent, false);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    // composited=false：无 libmpv 覆盖，wid 属性存在
    EXPECT_NE(QString("libmpv"), proxy->m_sInitVo);
    EXPECT_TRUE(proxy->m_mapWaitSet.contains("wid"));
}

// BUG41445：HARDWARE 模式在无特殊硬件环境下降级为 hwdec=auto
TEST_F(MpvProxyTest, mpv_init_HardwareModeFallsBackToAuto)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::HARDWARE;
    ut_resetRecorders();

    mpv_handle *h = proxy->mpv_init();
    ASSERT_NE(nullptr, h);

    EXPECT_EQ(QVariant("auto").toString(), proxy->m_mapWaitSet.value("hwdec").toString());
    EXPECT_EQ(QString("libmpv"), proxy->m_sInitVo);
}

// ============================ firstInit ============================

// firstInit 中 mpv_init 返回 nullptr 时不得崩溃，且 m_bInited 应置位
TEST_F(MpvProxyTest, firstInit_CreateNullDoesNotCrash)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, initMpvFuns), ut_initMpvFuns_noop_stub);
    proxy->m_creat = nullptr;   // mpv_init 早退返回 nullptr
    proxy->m_bInited = false;

    EXPECT_NO_THROW(proxy->firstInit());
    EXPECT_TRUE(proxy->m_bInited);
}

// 非合成器模式 firstInit：不应创建 GL widget（composited=false 分支）
TEST_F(MpvProxyTest, firstInit_NotCompositedSkipsGLWidget)
{
    Stub stub;
    // composited=false 分支会调用 m_pParentWidget->winId()，必须用真 QWidget
    QWidget *parent = new QWidget;
    MpvProxy *proxy = ut_createProxy(stub, parent, false);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, initMpvFuns), ut_initMpvFuns_noop_stub);
    proxy->m_bInited = false;

    EXPECT_NO_THROW(proxy->firstInit());
    EXPECT_TRUE(proxy->m_bInited);
    EXPECT_EQ(nullptr, proxy->m_pMpvGLwidget);
}

// ============================ initGpuInfoFuns ============================

// libgpuinfo.so 缺失时函数指针必须置空
TEST_F(MpvProxyTest, initGpuInfoFuns_LibMissingClearsPointers)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(SysUtils, libExist), ut_libExistFalse_stub);

    proxy->m_gpuInfo = (void *)0x1;
    proxy->m_gpuInfoVo = []() -> const char * { return "x"; };

    proxy->initGpuInfoFuns();

    EXPECT_EQ(nullptr, proxy->m_gpuInfo);
    EXPECT_EQ(nullptr, proxy->m_gpuInfoVo);
}

// libgpuinfo.so 存在且 resolve 成功时，函数指针应被赋值
TEST_F(MpvProxyTest, initGpuInfoFuns_ResolveSuccessAssignsPointers)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(SysUtils, libExist), ut_libExistTrue_stub);
    // QLibrary::resolve 多重重载，typedef 消歧
    typedef QFunctionPointer (*ResolveFn)(const QString &, const char *);
    stub.set((ResolveFn)&QLibrary::resolve, ut_qlibResolve_stub);

    proxy->m_gpuInfo = nullptr;
    proxy->m_gpuInfoVo = nullptr;

    proxy->initGpuInfoFuns();

    EXPECT_NE(nullptr, proxy->m_gpuInfo);
    EXPECT_NE(nullptr, proxy->m_gpuInfoVo);
}

// ============================ handle_mpv_events ============================
// 缺陷规避：setState 中 dynamic_cast 无空指针检查，必须 stub（ut_setState_rec_stub）

// 事件序列为 NONE 时循环立即退出，不触发任何状态变更
TEST_F(MpvProxyTest, handleEvents_NoneExitsLoop)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, setState), ut_setState_rec_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    g_ut_eventSeq.clear();
    g_ut_eventSeq << MPV_EVENT_NONE;
    g_ut_evIdx = 0;
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->handle_mpv_events());
    EXPECT_TRUE(g_ut_setStates.isEmpty());
}

// BUG247589：COMMAND_REPLY（reply_userdata=SEEK）应清除 m_bPendingSeek
TEST_F(MpvProxyTest, handleEvents_CommandReplySeekClearsPending)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, setState), ut_setState_rec_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_bPendingSeek = true;
    g_ut_eventSeq.clear();
    g_ut_eventSeq << MPV_EVENT_COMMAND_REPLY << MPV_EVENT_NONE;
    g_ut_evIdx = 0;
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->handle_mpv_events());
    EXPECT_FALSE(proxy->m_bPendingSeek);
}

// END_FILE 事件应将状态置为 Stopped（并回写配置，测试环境安全）
TEST_F(MpvProxyTest, handleEvents_EndFileSetsStopped)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, setState), ut_setState_rec_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    g_ut_eventSeq.clear();
    g_ut_eventSeq << MPV_EVENT_END_FILE << MPV_EVENT_NONE;
    g_ut_evIdx = 0;
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->handle_mpv_events());
    EXPECT_TRUE(g_ut_setStates.contains(static_cast<int>(Backend::PlayState::Stopped)));
}

// IDLE 事件应将状态置为 Stopped
TEST_F(MpvProxyTest, handleEvents_IdleSetsStopped)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, setState), ut_setState_rec_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    g_ut_eventSeq.clear();
    g_ut_eventSeq << MPV_EVENT_IDLE << MPV_EVENT_NONE;
    g_ut_evIdx = 0;
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->handle_mpv_events());
    EXPECT_TRUE(g_ut_setStates.contains(static_cast<int>(Backend::PlayState::Stopped)));
}

// BUG240393：FILE_LOADED 事件应将 m_pConfig 中的参数逐个异步下发、
// 状态置为 Playing，并清除 m_bLoadMedia
TEST_F(MpvProxyTest, handleEvents_FileLoadedFlushesConfigAndSetsPlaying)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, setState), ut_setState_rec_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_pConfig->insert("sub-delay", "0.5");
    proxy->m_bLoadMedia = true;
    g_ut_eventSeq.clear();
    g_ut_eventSeq << MPV_EVENT_FILE_LOADED << MPV_EVENT_NONE;
    g_ut_evIdx = 0;
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->handle_mpv_events());
    EXPECT_TRUE(g_ut_setStates.contains(static_cast<int>(Backend::PlayState::Playing)));
    EXPECT_FALSE(proxy->m_bLoadMedia);
    // m_pConfig 中的参数经 my_set_property_async 下发
    bool found = false;
    for (const auto &p : g_ut_propsAsync) {
        if (p.name == "sub-delay") { found = true; break; }
    }
    EXPECT_TRUE(found);
}

// ============================ configureJjwGPU ============================

// mwv207d 驱动路径：无需驱动目录即可命中 vaapi 分支
TEST_F(MpvProxyTest, configureJjwGPU_Mwv207dChoosesVaapi)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(utils, getJjwGPUPath), ut_jjwPath_mwv207d_stub);
    proxy->m_bInited = false;
    ut_resetRecorders();

    mpv_handle *h = ut_fakeHandle();
    EXPECT_NO_THROW(proxy->configureJjwGPU(h, true));

    // m_bInited=false：属性进入 m_mapWaitSet
    EXPECT_TRUE(proxy->m_mapWaitSet.value("hwdec").toString() == "vaapi");
    EXPECT_TRUE(proxy->m_mapWaitSet.value("vo").toString() == "vaapi");
    EXPECT_EQ(QString("vaapi"), proxy->m_sInitVo);
}

// /dev/jmgpu 但驱动目录不存在：走 else 分支（auto + "vdpau,xv,x11"）
TEST_F(MpvProxyTest, configureJjwGPU_JmgpuNoDriverFallsBackAuto)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(utils, getJjwGPUPath), ut_jjwPath_jmgpu_stub);
    proxy->m_bInited = false;
    proxy->m_mapWaitSet.clear();
    proxy->m_sInitVo = QString();
    ut_resetRecorders();

    mpv_handle *h = ut_fakeHandle();
    EXPECT_NO_THROW(proxy->configureJjwGPU(h, true));

    // else 分支：hwdec=auto，vo 列表 "vdpau,xv,x11"
    EXPECT_TRUE(proxy->m_mapWaitSet.value("hwdec").toString() == "auto");
    EXPECT_TRUE(proxy->m_mapWaitSet.value("vo").toString() == "vdpau,xv,x11");
    EXPECT_EQ(QString("vdpau,xv,x11"), proxy->m_sInitVo);
}

// mwv206 驱动 + 驱动目录存在（stub QDir::exists 命中目录判断）：vdpau 分支
TEST_F(MpvProxyTest, configureJjwGPU_Mwv206WithDriverDirChoosesVdpau)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(utils, getJjwGPUPath), ut_jjwPath_mwv206_stub);
    // QDir::exists()（无参成员版）stub：sdir/jmdir 存在性判断命中
    stub.set(static_cast<bool (QDir::*)() const>(&QDir::exists), ut_qdirExistsTrue_stub);
    proxy->m_bInited = false;
    proxy->m_mapWaitSet.clear();
    proxy->m_sInitVo = QString();
    ut_resetRecorders();

    mpv_handle *h = ut_fakeHandle();
    EXPECT_NO_THROW(proxy->configureJjwGPU(h, true));

    EXPECT_TRUE(proxy->m_mapWaitSet.value("hwdec").toString() == "vdpau");
    EXPECT_TRUE(proxy->m_mapWaitSet.value("vo").toString() == "vdpau");
    EXPECT_EQ(QString("vdpau"), proxy->m_sInitVo);
}

// setInitVo=false：不应覆盖 m_sInitVo
TEST_F(MpvProxyTest, configureJjwGPU_NoInitVoKeepsCurrentVo)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(utils, getJjwGPUPath), ut_jjwPath_mwv207d_stub);
    proxy->m_bInited = false;
    proxy->m_mapWaitSet.clear();
    proxy->m_sInitVo = QString("keep-me");
    ut_resetRecorders();

    mpv_handle *h = ut_fakeHandle();
    EXPECT_NO_THROW(proxy->configureJjwGPU(h, false));

    EXPECT_EQ(QString("keep-me"), proxy->m_sInitVo);
}

// ============================ refreshDecode ============================
// 缺陷规避：refreshDecode 中 dynamic_cast<PlayerEngine*> 直接解引用，必须真实 PlayerEngine parent

// 播放列表为空：早退，不产生任何异步属性设置
TEST_F(MpvProxyTest, refreshDecode_EmptyPlaylistEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    proxy->_file = QUrl::fromLocalFile("/tmp/nonexistent.mp4");
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->refreshDecode());
    // 空 playlist → 早退，无 hwdec/vo 设置
    EXPECT_TRUE(g_ut_propsAsync.isEmpty());
}

// 播放项 valid=false：早退
TEST_F(MpvProxyTest, refreshDecode_InvalidItemEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    PlaylistModel *playlist = engine->getplaylist();
    ASSERT_NE(nullptr, playlist);
    PlayItemInfo pif;
    pif.valid = false;
    pif.url = QUrl::fromLocalFile("/tmp/fake.mp4");
    playlist->_infos.append(pif);
    playlist->_current = 0;
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    proxy->_file = QUrl::fromLocalFile("/tmp/fake.mp4");
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->refreshDecode());
    EXPECT_TRUE(g_ut_propsAsync.isEmpty());
}

// BUG376907：SOFTWARE 模式 + 合成器开启 → hwdec=no 且 vo 强制 libmpv
TEST_F(MpvProxyTest, refreshDecode_SoftwareCompositedForcesLibmpv)
{
    Stub stub;
    CompositingManager::get()._composited = true;
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    PlaylistModel *playlist = engine->getplaylist();
    ASSERT_NE(nullptr, playlist);
    PlayItemInfo pif;
    pif.valid = true;
    pif.url = QUrl::fromLocalFile("/tmp/test.mp4");
    playlist->_infos.append(pif);
    playlist->_current = 0;
    proxy->m_decodeMode = DecodeMode::SOFTWARE;
    proxy->_file = QUrl::fromLocalFile("/tmp/test.mp4");
    ut_installMpvStubs(proxy);   // proxy 由 PlayerEngine 构造创建，成员函数指针未 stub
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->refreshDecode());

    bool foundHwdec = false, foundVo = false;
    for (const auto &p : g_ut_propsAsync) {
        if (p.name == "hwdec" && p.value == "no") foundHwdec = true;
        if (p.name == "vo" && p.value == "libmpv") foundVo = true;
    }
    EXPECT_TRUE(foundHwdec);
    EXPECT_TRUE(foundVo);
    EXPECT_EQ(QString("libmpv"), proxy->m_sInitVo);
}

// ============================ play ============================

// m_bLoadMedia=true（媒体加载中）：立即返回，不发起 loadfile
TEST_F(MpvProxyTest, play_MediaLoadingEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_bLoadMedia = true;
    proxy->_file = QUrl::fromLocalFile("/tmp/test.mp4");
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->play());
    EXPECT_TRUE(g_ut_cmds.isEmpty());
}

// 正常播放本地文件：loadfile 命令携带绝对路径 + pause 属性下发
TEST_F(MpvProxyTest, play_NormalLocalFileSendsLoadfile)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, refreshDecode), ut_refreshDecode_noop_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_bLoadMedia = false;
    proxy->m_bPauseOnStart = false;
    proxy->m_sInitVo = QString("gpu,xv,x11");
    proxy->_file = QUrl::fromLocalFile("/tmp/normal.mp4");
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->play());

    // loadfile 命令已发出且携带文件路径
    ASSERT_FALSE(g_ut_cmds.isEmpty());
    bool foundLoad = false;
    for (const auto &cmd : g_ut_cmds) {
        if (cmd.size() >= 2 && cmd[0] == QVariant("loadfile")
                && cmd[1].toString().contains("normal.mp4")) {
            foundLoad = true;
            break;
        }
    }
    EXPECT_TRUE(foundLoad);
    // pause 属性（m_bPauseOnStart=false → "no"）
    bool foundPause = false;
    for (const auto &p : g_ut_propsAsync) {
        if (p.name == "pause") { foundPause = true; break; }
    }
    EXPECT_TRUE(foundPause);
    // vo 经 my_set_property_async 下发（m_sInitVo）
    bool foundVo = false;
    for (const auto &p : g_ut_propsAsync) {
        if (p.name == "vo") { foundVo = true; break; }
    }
    EXPECT_TRUE(foundVo);
}

// m_bPauseOnStart=true：pause 属性应为 "yes"
TEST_F(MpvProxyTest, play_PauseOnStartSendsPauseYes)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, refreshDecode), ut_refreshDecode_noop_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_bLoadMedia = false;
    proxy->m_bPauseOnStart = true;
    proxy->m_sInitVo = QString("gpu,xv,x11");
    proxy->_file = QUrl::fromLocalFile("/tmp/pause.mp4");
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->play());

    bool foundPauseYes = false;
    for (const auto &p : g_ut_propsAsync) {
        // bool true 经 node_builder→MPV_FORMAT_FLAG→ut_nodeToString 记录为 "true"
        if (p.name == "pause" && p.value == "true") { foundPauseYes = true; break; }
    }
    EXPECT_TRUE(foundPauseYes);
}

// BUG355725：本地中文路径应正确下发 loadfile（QFileInfo 绝对路径保留中文）
TEST_F(MpvProxyTest, play_ChineseLocalPathPreserved)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, refreshDecode), ut_refreshDecode_noop_stub);
    proxy->m_bInited = true;
    proxy->m_pConfig = new QMap<QString, QString>();
    proxy->m_bLoadMedia = false;
    proxy->m_bPauseOnStart = false;
    proxy->m_sInitVo = QString("gpu,xv,x11");
    proxy->_file = QUrl::fromLocalFile(QStringLiteral("/tmp/中文视频测试.mp4"));
    ut_resetRecorders();

    EXPECT_NO_THROW(proxy->play());

    ASSERT_FALSE(g_ut_cmds.isEmpty());
    bool foundChinese = false;
    for (const auto &cmd : g_ut_cmds) {
        if (cmd.size() >= 2 && cmd[0] == QVariant("loadfile")
                && cmd[1].toString().contains(QStringLiteral("中文视频测试"))) {
            foundChinese = true;
            break;
        }
    }
    EXPECT_TRUE(foundChinese);
}

// ============================ isSpecialHWHardware ============================

// 非 Wayland 环境：早退返回 false（不触碰静态缓存）
TEST_F(MpvProxyTest, isSpecialHWHardware_WaylandOffReturnsFalse)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    // 不 stub check_wayland_env：测试环境（offscreen/X11）恒为 false

    bool ret = false;
    EXPECT_NO_THROW(ret = isSpecialHWHardware());
    EXPECT_FALSE(ret);
}

// Wayland 环境 + dmidecode 输出命中特殊设备（KLVV）：返回 true
TEST_F(MpvProxyTest, isSpecialHWHardware_KlvvDetectedReturnsTrue)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(utils, check_wayland_env), ut_waylandTrue_stub);
    // QProcess dmidecode 检测：stub 阻止真实 spawn，输出 KLVV
    stub.set(ADDR(QProcess, waitForStarted), ut_qprocWaitStarted_stub);
    stub.set(ADDR(QProcess, waitForFinished), ut_qprocWaitFinished_stub);
    stub.set(ADDR(QProcess, readAllStandardOutput), ut_qprocReadKlvv_stub);
    stub.set(ADDR(QProcess, setProgram), ut_qprocSetProgram_stub);

    bool ret = false;
    EXPECT_NO_THROW(ret = isSpecialHWHardware());
    EXPECT_TRUE(ret);
}

// isSupportHardWareDecode / processLogMessage / changeSoundMode / toggleMute /
// initMember / setState / pollingEndOfPlayback / getDecodeProbeValue / videoAspect

namespace {

// getDecodeProbeValue stub（ADDR 安装；H264 = decoder_profile 枚举值 4）
int ut_getDecodeProbeValue_h264_stub(void *obj, const QString s)
{
    Q_UNUSED(obj); Q_UNUSED(s);
    return 4;
}
int ut_getDecodeProbeValue_unknow_stub(void *obj, const QString s)
{
    Q_UNUSED(obj); Q_UNUSED(s);
    return 0;   // decoder_profile::UN_KNOW
}

// VDP_Decoder_t 与源码 mpv_proxy.cpp:88-98 布局一致（VdpBool=int、枚举=int）；
// 该类型定义在源码 .cpp 内不可见，测试侧以同布局结构 + 指针参数 stub
struct UtVDPDecoder {
    int func; int is_supported; uint32_t max_width; uint32_t max_height;
    uint32_t max_level; uint32_t max_macroblocks; char ret_info[512];
};
unsigned int ut_gpuDecoderInfo_4096_stub(int index, void *result)
{
    Q_UNUSED(index);
    UtVDPDecoder *p = static_cast<UtVDPDecoder *>(result);
    p->is_supported = 1; p->max_width = 4096; p->max_height = 2160;
    return 1;
}
unsigned int ut_gpuDecoderInfo_1080_stub(int index, void *result)
{
    Q_UNUSED(index);
    UtVDPDecoder *p = static_cast<UtVDPDecoder *>(result);
    p->is_supported = 1; p->max_width = 1920; p->max_height = 1080;
    return 1;
}

// my_command / my_get_property stub（ADDR 安装；成员方法，首参 this）
static QVector<QVariant> g_ut_myCmds;
QVariant ut_myCommand_rec_stub(void *obj, mpv_handle *h, const QVariant &args)
{
    Q_UNUSED(obj); Q_UNUSED(h);
    g_ut_myCmds.append(args);
    return QVariant();
}
QVariant ut_myGetProperty_aspect_stub(void *obj, mpv_handle *h, const QString &s)
{
    Q_UNUSED(obj); Q_UNUSED(h); Q_UNUSED(s);
    return QStringLiteral("2.35");
}

// stop no-op stub（pollingEndOfPlayback 内部调用，避免真实 mpv 操作）
void ut_stop_noop_stub(void *obj) { Q_UNUSED(obj); }

}  // namespace

// 探测工具未安装（UN_KNOW）时默认支持硬解：isHardWare 初始 true 直接返回
TEST_F(MpvProxyTest, isSupportHardWareDecode_UnknownProbeDefaultsTrue)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    // getDecodeProbeValue 返回 UN_KNOW(0) → 跳过探测，默认 true
    stub.set(ADDR(MpvProxy, getDecodeProbeValue),
             reinterpret_cast<void *>(&ut_getDecodeProbeValue_unknow_stub));

    EXPECT_TRUE(proxy->isSupportHardWareDecode(QStringLiteral("h264"), 1920, 1080));
}

// gpuinfo 探测支持：nSupport>0 且最大宽高 >= 视频宽高 → true（BUG351097 4K 播放）
TEST_F(MpvProxyTest, isSupportHardWareDecode_GpuInfoSupportsResolutionTrue)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, getDecodeProbeValue),
             reinterpret_cast<void *>(&ut_getDecodeProbeValue_h264_stub));
    proxy->m_gpuInfo = reinterpret_cast<void *>(&ut_gpuDecoderInfo_4096_stub);

    // 4096x2160 探测上限 >= 1920x1080 视频尺寸 → 支持硬解
    EXPECT_TRUE(proxy->isSupportHardWareDecode(QStringLiteral("h264"), 1920, 1080));
    // 4K 视频 3840x2160 也在探测上限内
    EXPECT_TRUE(proxy->isSupportHardWareDecode(QStringLiteral("h264"), 3840, 2160));
}

// 视频尺寸超过探测最大宽高 → false
TEST_F(MpvProxyTest, isSupportHardWareDecode_GpuInfoRejectsLargeResolutionFalse)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, getDecodeProbeValue),
             reinterpret_cast<void *>(&ut_getDecodeProbeValue_h264_stub));
    proxy->m_gpuInfo = reinterpret_cast<void *>(&ut_gpuDecoderInfo_1080_stub);

    // 探测最大 1920x1080 < 视频尺寸 3840x2160 → 不支持
    EXPECT_FALSE(proxy->isSupportHardWareDecode(QStringLiteral("h264"), 3840, 2160));
}

// ============================ processLogMessage ============================
// 源码：src/backends/mpv/mpv_proxy.cpp processLogMessage()

// WARN 级日志：应 emit mpvWarningLogsChanged 且携带 prefix/text
TEST_F(MpvProxyTest, processLogMessage_WarnEmitsWarningSignal)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    QSignalSpy spy(proxy, &Backend::mpvWarningLogsChanged);
    char prefix[] = "mpv";
    char text[] = "some warning text";
    mpv_event_log_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.log_level = MPV_LOG_LEVEL_WARN;
    msg.prefix = prefix;
    msg.text = text;

    proxy->processLogMessage(&msg);

    EXPECT_EQ(1, spy.count());
    if (spy.count() == 1) {
        EXPECT_EQ(QString("mpv"), spy.at(0).at(0).toString());
        EXPECT_EQ(QString("some warning text"), spy.at(0).at(1).toString());
    }
}

// ERROR 级含 vdpau 失败文本：应置 m_bLastIsSpecificFormat 并 emit mpvErrorLogsChanged
// （m_bLastIsSpecificFormat 驱动特定 VO 回退逻辑）
TEST_F(MpvProxyTest, processLogMessage_ErrorVdpauSetsRawFormatFlag)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_bLastIsSpecificFormat = false;

    QSignalSpy spy(proxy, &Backend::mpvErrorLogsChanged);
    char prefix[] = "vo";
    char text[] = "Failed setup for format vdpau.";
    mpv_event_log_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.log_level = MPV_LOG_LEVEL_ERROR;
    msg.prefix = prefix;
    msg.text = text;

    proxy->processLogMessage(&msg);

    EXPECT_TRUE(proxy->m_bLastIsSpecificFormat);
    EXPECT_EQ(1, spy.count());
}

// INFO 级日志：仅记日志，不 emit 警告/错误信号
TEST_F(MpvProxyTest, processLogMessage_InfoEmitsNothing)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    QSignalSpy spyW(proxy, &Backend::mpvWarningLogsChanged);
    QSignalSpy spyE(proxy, &Backend::mpvErrorLogsChanged);
    char prefix[] = "cplayer";
    char text[] = "info message";
    mpv_event_log_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.log_level = MPV_LOG_LEVEL_INFO;
    msg.prefix = prefix;
    msg.text = text;

    proxy->processLogMessage(&msg);

    EXPECT_EQ(0, spyW.count());
    EXPECT_EQ(0, spyE.count());
    EXPECT_FALSE(proxy->m_bLastIsSpecificFormat);
}

// ============================ changeSoundMode / toggleMute ============================
// 源码：changeSoundMode 组装 stereotools 命令；toggleMute 发送 cycle mute

// Stereo 模式：应发送 af set stereotools=muter=false 命令
TEST_F(MpvProxyTest, changeSoundMode_StereoSendsMuterFalseCommand)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    g_ut_myCmds.clear();

    proxy->changeSoundMode(Backend::SoundMode::Stereo);

    EXPECT_EQ(1, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 1) {
        QStringList args = g_ut_myCmds.at(0).toStringList();
        EXPECT_EQ(QString("af"), args.value(0));
        EXPECT_EQ(QString("set"), args.value(1));
        EXPECT_EQ(QString("stereotools=muter=false"), args.value(2));
    }
}

// Left 模式：右声道静音 muter=true
TEST_F(MpvProxyTest, changeSoundMode_LeftSendsMuterTrueCommand)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    g_ut_myCmds.clear();

    proxy->changeSoundMode(Backend::SoundMode::Left);

    EXPECT_EQ(1, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 1) {
        QStringList args = g_ut_myCmds.at(0).toStringList();
        EXPECT_EQ(QString("stereotools=muter=true"), args.value(2));
    }
}

// Right 模式：左声道静音 mutel=true
TEST_F(MpvProxyTest, changeSoundMode_RightSendsMutelTrueCommand)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    g_ut_myCmds.clear();

    proxy->changeSoundMode(Backend::SoundMode::Right);

    EXPECT_EQ(1, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 1) {
        QStringList args = g_ut_myCmds.at(0).toStringList();
        EXPECT_EQ(QString("stereotools=mutel=true"), args.value(2));
    }
}

// toggleMute：应发送 cycle mute 命令
TEST_F(MpvProxyTest, toggleMute_SendsCycleMuteCommand)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    g_ut_myCmds.clear();

    proxy->toggleMute();

    EXPECT_EQ(1, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 1) {
        QStringList args = g_ut_myCmds.at(0).toStringList();
        EXPECT_EQ(QString("cycle"), args.value(0));
        EXPECT_EQ(QString("mute"), args.value(1));
    }
}

// ============================ initMember ============================
// 源码：initMember() 重置连拍/轮询/加载等成员标志

// initMember 重置全部状态标志与指针
TEST_F(MpvProxyTest, initMember_ResetsBurstFlagsSafe)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    // 预置脏状态
    proxy->m_nBurstStart = 5;
    proxy->m_bInBurstShotting = true;
    proxy->m_bPolling = true;
    proxy->m_bLoadMedia = true;
    proxy->m_pMpvGLwidget = reinterpret_cast<MpvGLWidget *>(0x1);

    proxy->initMember();

    EXPECT_EQ(0, proxy->m_nBurstStart);
    EXPECT_FALSE(proxy->m_bInBurstShotting);
    EXPECT_FALSE(proxy->m_bPolling);
    EXPECT_FALSE(proxy->m_bLoadMedia);
    EXPECT_EQ(nullptr, proxy->m_pMpvGLwidget);
    EXPECT_EQ(nullptr, proxy->m_pParentWidget);
}

// ============================ setState ============================
// 源码：setState 切换 _state 并经 QTimer::singleShot(0) 延迟 emit stateChanged（防死锁）

// 空播放列表：setState 切换状态并延迟 emit stateChanged（BUG341125 死锁回归）
TEST_F(MpvProxyTest, setState_PlaylistEmptyChangesStateAndEmits)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    proxy->m_pParentWidget = engine;   // setState 内 dynamic_cast<PlayerEngine*> 解引用
    proxy->_state = Backend::Stopped;
    proxy->m_pMpvGLwidget = nullptr;

    QSignalSpy spy(proxy, &Backend::stateChanged);
    proxy->setState(Backend::PlayState::Playing);

    // 状态立即切换
    EXPECT_EQ(Backend::PlayState::Playing, proxy->_state);
    // 延迟 emit（QTimer::singleShot(0)）需事件循环驱动
    QTest::qWait(50);
    EXPECT_GE(spy.count(), 1);
}

// 相同状态：不重复切换也不 emit
TEST_F(MpvProxyTest, setState_SameStateNoSignal)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    proxy->m_pParentWidget = engine;
    proxy->_state = Backend::Stopped;
    proxy->m_pMpvGLwidget = nullptr;

    QSignalSpy spy(proxy, &Backend::stateChanged);
    proxy->setState(Backend::PlayState::Stopped);   // 与 _state 相同

    QTest::qWait(50);
    EXPECT_EQ(0, spy.count());
    EXPECT_EQ(Backend::PlayState::Stopped, proxy->_state);
}

// ============================ pollingEndOfPlayback ============================
// 源码：pollingEndOfPlayback 轮询 mpv 事件直到 END_FILE，含超时保护

// 已处于 Stopped：直接 no-op，不进入轮询
TEST_F(MpvProxyTest, pollingEndOfPlayback_AlreadyStoppedNoOp)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->_state = Backend::Stopped;
    proxy->m_bPolling = false;

    proxy->pollingEndOfPlayback();

    EXPECT_FALSE(proxy->m_bPolling);
    EXPECT_EQ(Backend::Stopped, proxy->_state);
}

// 播放中收到 END_FILE 事件：应停止轮询并置 Stopped（BUG45357 播放结束回归）
TEST_F(MpvProxyTest, pollingEndOfPlayback_EndFileForcesStopped)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    PlayerEngine *engine = ut_createPlayerEngine(stub);
    proxy->m_pParentWidget = engine;
    proxy->_state = Backend::PlayState::Playing;
    proxy->m_pMpvGLwidget = nullptr;
    // stop() 自身有 null handle 保护（2210 行 m_handle 为空时安全返回），无需 stub；
    // m_waitEvent（成员函数指针，ut_installMpvStubs 已装）返回 END_FILE
    g_ut_eventSeq.clear();
    g_ut_eventSeq.append(MPV_EVENT_END_FILE);
    g_ut_evIdx = 0;

    QSignalSpy spy(proxy, &Backend::stateChanged);
    proxy->pollingEndOfPlayback();

    EXPECT_FALSE(proxy->m_bPolling);
    EXPECT_EQ(Backend::Stopped, proxy->_state);
    QTest::qWait(50);   // setState 内延迟 emit
    EXPECT_GE(spy.count(), 1);

    // 清理事件序列，避免污染后续用例
    g_ut_eventSeq.clear();
    g_ut_evIdx = 0;
}

// ============================ getDecodeProbeValue / videoAspect ============================
// 源码：getDecodeProbeValue 纯逻辑（格式名 → profile 值）；videoAspect 读 mpv 属性

// 已知格式名映射：h264 → H264(4)；大小写不敏感；未知格式 → UN_KNOW(0)
TEST_F(MpvProxyTest, getDecodeProbeValue_KnownFormatReturnsProfile)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    // "MPEG1"(1) MPEG2(2) MPEG4(3) H264(4) VC1(5) DIVX4(6) DIVX5(7) HEVC(8)
    EXPECT_EQ(4, proxy->getDecodeProbeValue(QStringLiteral("h264")));
    EXPECT_EQ(4, proxy->getDecodeProbeValue(QStringLiteral("H264")));
    EXPECT_EQ(1, proxy->getDecodeProbeValue(QStringLiteral("mpeg1")));
    EXPECT_EQ(8, proxy->getDecodeProbeValue(QStringLiteral("hevc-main")));
    EXPECT_EQ(0, proxy->getDecodeProbeValue(QStringLiteral("unknown-format")));
    EXPECT_EQ(0, proxy->getDecodeProbeValue(QString()));
}

// videoAspect：my_get_property 的 video-aspect-override 值转 double
TEST_F(MpvProxyTest, videoAspect_ReturnsPropertyOverrideValue)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_get_property),
             reinterpret_cast<void *>(&ut_myGetProperty_aspect_stub));

    EXPECT_DOUBLE_EQ(2.35, proxy->videoAspect());
}

// updatePropertyCache / initSetting

namespace {

// my_set_property 录制 stub（ADDR 安装；成员方法，首参 this）
static QVector<QPair<QString, QString>> g_ut_myProps;
void ut_mySetProperty_rec_stub(void *obj, mpv_handle *h, const QString &k, const QVariant &v)
{
    Q_UNUSED(obj); Q_UNUSED(h);
    g_ut_myProps.append({k, v.toString()});
}

}  // namespace

// duration 属性：m_cachedDuration 更新为 toLongLong
TEST_F(MpvProxyTest, updatePropertyCache_DurationUpdatesCache)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_cachedDuration = -1;

    proxy->updatePropertyCache(QStringLiteral("duration"), QVariant(qlonglong(123456)));

    EXPECT_EQ(123456, proxy->m_cachedDuration);
}

// 全部标量属性：time-pos/volume/mute/aid/sid/pause/idle-active/paused-for-cache
TEST_F(MpvProxyTest, updatePropertyCache_AllScalarProperties)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    proxy->updatePropertyCache(QStringLiteral("time-pos"), QVariant(qlonglong(42)));
    proxy->updatePropertyCache(QStringLiteral("volume"), QVariant(75));
    proxy->updatePropertyCache(QStringLiteral("mute"), QVariant(true));
    proxy->updatePropertyCache(QStringLiteral("aid"), QVariant(2));
    proxy->updatePropertyCache(QStringLiteral("sid"), QVariant(3));
    proxy->updatePropertyCache(QStringLiteral("pause"), QVariant(true));
    proxy->updatePropertyCache(QStringLiteral("idle-active"), QVariant(true));
    proxy->updatePropertyCache(QStringLiteral("paused-for-cache"), QVariant(true));

    EXPECT_EQ(42, proxy->m_cachedElapsed);
    EXPECT_EQ(75, proxy->m_cachedVolume);
    EXPECT_TRUE(proxy->m_cachedMute);
    EXPECT_EQ(2, proxy->m_cachedAid);
    EXPECT_EQ(3, proxy->m_cachedSid);
    EXPECT_TRUE(proxy->m_cachedPause);
    EXPECT_TRUE(proxy->m_cachedIdleActive);
    EXPECT_TRUE(proxy->m_cachedPausedForCache);
}

// 视频尺寸与旋转：dwidth/dheight 设置尺寸；rotate 90/270 转置（宽高互换）
TEST_F(MpvProxyTest, updatePropertyCache_VideoSizeWithRotateTranspose)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    proxy->updatePropertyCache(QStringLiteral("dwidth"), QVariant(3840));
    proxy->updatePropertyCache(QStringLiteral("dheight"), QVariant(2160));
    EXPECT_EQ(3840, proxy->m_cachedVideoSize.width());
    EXPECT_EQ(2160, proxy->m_cachedVideoSize.height());

    // 竖屏旋转 90：宽高互换
    proxy->updatePropertyCache(QStringLiteral("video-out-params/rotate"), QVariant(90));
    EXPECT_EQ(2160, proxy->m_cachedVideoSize.width());
    EXPECT_EQ(3840, proxy->m_cachedVideoSize.height());

    // 横屏旋转 270：再次互换回原始
    proxy->updatePropertyCache(QStringLiteral("video-out-params/rotate"), QVariant(270));
    EXPECT_EQ(3840, proxy->m_cachedVideoSize.width());
    EXPECT_EQ(2160, proxy->m_cachedVideoSize.height());

    // 180 旋转：不变换
    proxy->updatePropertyCache(QStringLiteral("video-out-params/rotate"), QVariant(180));
    EXPECT_EQ(3840, proxy->m_cachedVideoSize.width());
    EXPECT_EQ(2160, proxy->m_cachedVideoSize.height());
}

// 未知属性名：不修改任何缓存
TEST_F(MpvProxyTest, updatePropertyCache_UnknownNameNoChange)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_cachedDuration = 777;
    proxy->m_cachedVolume = 55;

    proxy->updatePropertyCache(QStringLiteral("unknown-prop"), QVariant(999));

    EXPECT_EQ(777, proxy->m_cachedDuration);
    EXPECT_EQ(55, proxy->m_cachedVolume);
}

// initSetting：m_mapWaitSet 全部属性经 my_set_property 刷入
TEST_F(MpvProxyTest, initSetting_FlushesWaitSetProperties)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    g_ut_myProps.clear();
    proxy->m_mapWaitSet.clear();
    proxy->m_mapWaitSet.insert("hwdec", QVariant("auto"));
    proxy->m_mapWaitSet.insert("volume", QVariant(66));

    proxy->initSetting();

    EXPECT_EQ(2, g_ut_myProps.count());
    // QMap 按字母序：hwdec 先于 volume
    if (g_ut_myProps.count() == 2) {
        EXPECT_EQ(QString("hwdec"), g_ut_myProps.at(0).first);
        EXPECT_EQ(QString("auto"), g_ut_myProps.at(0).second);
        EXPECT_EQ(QString("volume"), g_ut_myProps.at(1).first);
        EXPECT_EQ(QString("66"), g_ut_myProps.at(1).second);
    }
}

// initSetting：m_vecWaitCommand 全部命令经 my_command 刷入
TEST_F(MpvProxyTest, initSetting_FlushesWaitCommands)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    g_ut_myCmds.clear();
    proxy->m_vecWaitCommand.clear();
    proxy->m_vecWaitCommand.append(QVariantList{"cycle", "mute"});

    proxy->initSetting();

    // append(QVariantList) 在 Qt6 匹配 append(const QList&) 重载 → 2 个元素逐项追加
    EXPECT_EQ(2, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 2) {
        EXPECT_EQ(QString("cycle"), g_ut_myCmds.at(0).toString());
        EXPECT_EQ(QString("mute"), g_ut_myCmds.at(1).toString());
    }
}

// setSubDelay / selectSubtitle / setMute / stepBurstScreenshot

namespace {

// MovieConfiguration::updateUrl 录制 stub（ADDR 安装；重载 KnownKey 版本）
static QStringList g_ut_myCfgUpdates;
void ut_myUpdateUrl_stub(void *obj, const QUrl &url, int key, const QVariant &val)
{
    Q_UNUSED(obj); Q_UNUSED(url); Q_UNUSED(val);
    g_ut_myCfgUpdates.append(QString::number(key));   // KnownKey 枚举按 int ABI
}

// my_set_property_async 录制 stub（ADDR 安装；成员方法，首参 this）
static QVector<QPair<QString, QString>> g_ut_myPropAsync;
int ut_mySetPropertyAsync_rec_stub(void *obj, mpv_handle *h, const QString &k, const QVariant &v, uint64_t ud)
{
    Q_UNUSED(obj); Q_UNUSED(h); Q_UNUSED(ud);
    g_ut_myPropAsync.append({k, v.toString()});
    return 0;
}

}  // namespace

// setSubDelay：发送 sub-delay 属性并更新配置缓存
TEST_F(MpvProxyTest, setSubDelay_SendsPropertyAndUpdatesConfig)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    void (MovieConfiguration::*pUpdateUrlK)(const QUrl &, MovieConfiguration::KnownKey, const QVariant &) = &MovieConfiguration::updateUrl;
    stub.set(pUpdateUrlK,
             reinterpret_cast<void (*)(void *, const QUrl &, int, const QVariant &)>(&ut_myUpdateUrl_stub));
    g_ut_myProps.clear();
    g_ut_myCfgUpdates.clear();

    proxy->setSubDelay(1.5);

    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("sub-delay"), g_ut_myProps.at(0).first);
    }
    EXPECT_FALSE(g_ut_myCfgUpdates.isEmpty());   // updateUrl 被调用（SubDelay key）
}

// selectSubtitle：字幕列表为空时超限 id 收敛为 -1 并立即更新缓存
TEST_F(MpvProxyTest, selectSubtitle_EmptySubsClampsToMinusOne)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property_async),
             reinterpret_cast<void *>(&ut_mySetPropertyAsync_rec_stub));
    void (MovieConfiguration::*pUpdateUrlK)(const QUrl &, MovieConfiguration::KnownKey, const QVariant &) = &MovieConfiguration::updateUrl;
    stub.set(pUpdateUrlK,
             reinterpret_cast<void (*)(void *, const QUrl &, int, const QVariant &)>(&ut_myUpdateUrl_stub));
    g_ut_myPropAsync.clear();
    proxy->m_movieInfo.subs.clear();

    proxy->selectSubtitle(5);   // 超限（subs 空）

    EXPECT_EQ(1, g_ut_myPropAsync.count());
    if (g_ut_myPropAsync.count() == 1) {
        EXPECT_EQ(QString("sid"), g_ut_myPropAsync.at(0).first);
        EXPECT_EQ(QString("-1"), g_ut_myPropAsync.at(0).second);
    }
    EXPECT_EQ(-1, proxy->m_cachedSid);
}

// selectSubtitle：nId 未超限（<= subs.size()）时直通
TEST_F(MpvProxyTest, selectSubtitle_ValidIdPassthrough)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property_async),
             reinterpret_cast<void *>(&ut_mySetPropertyAsync_rec_stub));
    void (MovieConfiguration::*pUpdateUrlK)(const QUrl &, MovieConfiguration::KnownKey, const QVariant &) = &MovieConfiguration::updateUrl;
    stub.set(pUpdateUrlK,
             reinterpret_cast<void (*)(void *, const QUrl &, int, const QVariant &)>(&ut_myUpdateUrl_stub));
    g_ut_myPropAsync.clear();
    proxy->m_movieInfo.subs.clear();

    proxy->selectSubtitle(0);   // 0 不大于 size(0)，直通

    EXPECT_EQ(1, g_ut_myPropAsync.count());
    if (g_ut_myPropAsync.count() == 1) {
        EXPECT_EQ(QString("0"), g_ut_myPropAsync.at(0).second);
    }
    EXPECT_EQ(0, proxy->m_cachedSid);
}

// setMute：发送 mute 属性（true/false 均覆盖）
TEST_F(MpvProxyTest, setMute_SendsMuteProperty)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    g_ut_myProps.clear();

    proxy->setMute(true);
    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("mute"), g_ut_myProps.at(0).first);
        EXPECT_EQ(QString("true"), g_ut_myProps.at(0).second);
    }

    proxy->setMute(false);
    EXPECT_EQ(2, g_ut_myProps.count());
    if (g_ut_myProps.count() == 2) {
        EXPECT_EQ(QString("false"), g_ut_myProps.at(1).second);
    }
}

// stepBurstScreenshot：非连拍模式早退（不触发 seek/waitEvent 循环）
TEST_F(MpvProxyTest, stepBurstScreenshot_NotActiveEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_bInBurstShotting = false;
    g_ut_myCmds.clear();   // 全局录制容器，前置清空防跨用例污染

    EXPECT_NO_THROW(proxy->stepBurstScreenshot());
    EXPECT_EQ(0, g_ut_myCmds.count());   // 无 seek 命令发出
}

// stopBurstScreenshot / nextFrame

// stopBurstScreenshot：清除连拍标记并恢复 time-pos 属性
TEST_F(MpvProxyTest, stopBurstScreenshot_RestoresPosAndClearsFlag)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    g_ut_myProps.clear();
    proxy->m_bInBurstShotting = true;
    proxy->m_posBeforeBurst = 3.5;

    proxy->stopBurstScreenshot();

    EXPECT_FALSE(proxy->m_bInBurstShotting);
    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("time-pos"), g_ut_myProps.at(0).first);
    }
}

// nextFrame：Stopped 状态早退，不发 frame-step 命令
TEST_F(MpvProxyTest, nextFrame_StoppedEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->_state = Backend::Stopped;
    g_ut_myCmds.clear();

    proxy->nextFrame();

    EXPECT_EQ(0, g_ut_myCmds.count());
}

// nextFrame：Playing 状态发送 frame-step 命令
TEST_F(MpvProxyTest, nextFrame_PlayingSendsFrameStep)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_myCommand_rec_stub));
    proxy->_state = Backend::Playing;
    g_ut_myCmds.clear();

    proxy->nextFrame();

    EXPECT_EQ(1, g_ut_myCmds.count());
    if (g_ut_myCmds.count() == 1) {
        QStringList args = g_ut_myCmds.at(0).toStringList();
        EXPECT_TRUE(args.contains("frame-step"));
    }
}

// seekForward / seekBackward / setPlaySpeed / setVideoAspect / setDecodeModel /
// selectTrack / volumeDown / initPropertyCache

// my_command_async 录制 stub
static QVector<std::tuple<QString, QVariant, QString>> g_ut_myCmdsAsync;
bool ut_b6_myCommandAsync_rec_stub(void *obj, mpv_handle *h, const QVariant &args, uint64_t tag)
{
    Q_UNUSED(obj); Q_UNUSED(h); Q_UNUSED(tag);
    QVariantList lst = args.toList();
    g_ut_myCmdsAsync.append({lst.value(0).toString(), lst.value(1),
                             lst.value(2).toString()});
    return true;
}

// m_observeProperty 录制 stub（函数指针成员，直接赋值）
static QList<QString> g_ut_b6Observed;
int ut_b6_observe_rec_stub(mpv_handle *h, uint64_t ud, const char *name, mpv_format fmt)
{
    Q_UNUSED(h); Q_UNUSED(ud); Q_UNUSED(fmt);
    g_ut_b6Observed.append(QString::fromUtf8(name));
    return 0;
}

// seekForward：Stopped 状态早退
TEST_F(MpvProxyTest, seekForward_StoppedEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command_async),
             reinterpret_cast<void *>(&ut_b6_myCommandAsync_rec_stub));
    proxy->_state = Backend::Stopped;
    g_ut_myCmdsAsync.clear();

    proxy->seekForward(5);
    EXPECT_EQ(0, g_ut_myCmdsAsync.count());
}

// seekForward：pending seek 未完成时早退
TEST_F(MpvProxyTest, seekForward_PendingSeekEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command_async),
             reinterpret_cast<void *>(&ut_b6_myCommandAsync_rec_stub));
    proxy->_state = Backend::Playing;
    proxy->m_bPendingSeek = true;
    g_ut_myCmdsAsync.clear();

    proxy->seekForward(5);
    EXPECT_EQ(0, g_ut_myCmdsAsync.count());
    EXPECT_TRUE(proxy->m_bPendingSeek);
}

// seekForward：发送 seek relative+exact 异步命令并置 pending 标志
TEST_F(MpvProxyTest, seekForward_SendsSeekCommand)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command_async),
             reinterpret_cast<void *>(&ut_b6_myCommandAsync_rec_stub));
    proxy->_state = Backend::Playing;
    proxy->m_bPendingSeek = false;
    g_ut_myCmdsAsync.clear();

    proxy->seekForward(5);

    EXPECT_EQ(1, g_ut_myCmdsAsync.count());
    if (g_ut_myCmdsAsync.count() == 1) {
        EXPECT_EQ(QString("seek"), std::get<0>(g_ut_myCmdsAsync.at(0)));
        EXPECT_EQ(QVariant(5), std::get<1>(g_ut_myCmdsAsync.at(0)));
        EXPECT_EQ(QString("relative+exact"), std::get<2>(g_ut_myCmdsAsync.at(0)));
    }
    EXPECT_TRUE(proxy->m_bPendingSeek);
}

// seekBackward：正数秒数取负后发送
TEST_F(MpvProxyTest, seekBackward_NegatesPositiveSeconds)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command_async),
             reinterpret_cast<void *>(&ut_b6_myCommandAsync_rec_stub));
    proxy->_state = Backend::Playing;
    proxy->m_bPendingSeek = false;
    g_ut_myCmdsAsync.clear();

    proxy->seekBackward(10);

    EXPECT_EQ(1, g_ut_myCmdsAsync.count());
    if (g_ut_myCmdsAsync.count() == 1) {
        EXPECT_EQ(QVariant(-10), std::get<1>(g_ut_myCmdsAsync.at(0)));
    }
}

// seekBackward：Stopped 状态早退
TEST_F(MpvProxyTest, seekBackward_StoppedEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command_async),
             reinterpret_cast<void *>(&ut_b6_myCommandAsync_rec_stub));
    proxy->_state = Backend::Stopped;
    g_ut_myCmdsAsync.clear();

    proxy->seekBackward(10);
    EXPECT_EQ(0, g_ut_myCmdsAsync.count());
}

// setPlaySpeed：发送 speed 属性（异步）
TEST_F(MpvProxyTest, setPlaySpeed_SendsSpeedProperty)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property_async),
             reinterpret_cast<void *>(&ut_mySetPropertyAsync_rec_stub));
    g_ut_myPropAsync.clear();

    proxy->setPlaySpeed(1.5);

    EXPECT_EQ(1, g_ut_myPropAsync.count());
    if (g_ut_myPropAsync.count() == 1) {
        EXPECT_EQ(QString("speed"), g_ut_myPropAsync.at(0).first);
        EXPECT_EQ("1.5", g_ut_myPropAsync.at(0).second);
    }
}

// setVideoAspect：发送 video-aspect-override 属性
TEST_F(MpvProxyTest, setVideoAspect_SendsAspectOverride)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    g_ut_myProps.clear();

    proxy->setVideoAspect(2.35);

    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("video-aspect-override"), g_ut_myProps.at(0).first);
    }
}

// setDecodeModel：按 QVariant 整数值更新解码模式
TEST_F(MpvProxyTest, setDecodeModel_UpdatesDecodeMode)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);

    proxy->setDecodeModel(QVariant(1));
    EXPECT_EQ(1, static_cast<int>(proxy->m_decodeMode));
    proxy->setDecodeModel(QVariant(0));
    EXPECT_EQ(0, static_cast<int>(proxy->m_decodeMode));
}

// selectTrack：越界 id 早退，不发 aid 属性
TEST_F(MpvProxyTest, selectTrack_InvalidIdEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    proxy->m_movieInfo.audios.clear();
    g_ut_myProps.clear();

    proxy->selectTrack(0);

    EXPECT_EQ(0, g_ut_myProps.count());
}

// selectTrack：有效 id 发送 aid 属性
TEST_F(MpvProxyTest, selectTrack_ValidIdSendsAid)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    proxy->m_movieInfo.audios.append({{"id", 5}});
    g_ut_myProps.clear();

    proxy->selectTrack(0);

    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("aid"), g_ut_myProps.at(0).first);
    }
}

// volumeDown：音量为 0 时早退（m_cachedVolume=40 → 显示音量 0）
TEST_F(MpvProxyTest, volumeDown_AtZeroEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    proxy->m_cachedVolume = 40;   // (40-40)/60*100 = 0
    g_ut_myProps.clear();

    proxy->volumeDown();
    EXPECT_EQ(0, g_ut_myProps.count());   // 早退不发 volume 属性
}

// volumeDown：音量减少 10（显示音量 30 → changeVolume(20)）
TEST_F(MpvProxyTest, volumeDown_DecreasesVolume)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_mySetProperty_rec_stub));
    proxy->m_cachedVolume = 58;   // 显示音量 (58-40)/60*100 = 30
    g_ut_myProps.clear();

    proxy->volumeDown();

    // changeVolume(20) → volumeCorrection(20) 换算后发送 volume 属性
    EXPECT_EQ(1, g_ut_myProps.count());
    if (g_ut_myProps.count() == 1) {
        EXPECT_EQ(QString("volume"), g_ut_myProps.at(0).first);
    }
}

// initPropertyCache：注册 8 个属性观察者
TEST_F(MpvProxyTest, initPropertyCache_RegistersObservers)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_observeProperty = &ut_b6_observe_rec_stub;
    g_ut_b6Observed.clear();

    proxy->initPropertyCache(proxy->m_handle);

    EXPECT_EQ(8, g_ut_b6Observed.count());
    if (g_ut_b6Observed.count() == 8) {
        EXPECT_TRUE(g_ut_b6Observed.contains("duration"));
        EXPECT_TRUE(g_ut_b6Observed.contains("volume"));
        EXPECT_TRUE(g_ut_b6Observed.contains("mute"));
        EXPECT_TRUE(g_ut_b6Observed.contains("aid"));
        EXPECT_TRUE(g_ut_b6Observed.contains("sid"));
        EXPECT_TRUE(g_ut_b6Observed.contains("dwidth"));
        EXPECT_TRUE(g_ut_b6Observed.contains("dheight"));
        EXPECT_TRUE(g_ut_b6Observed.contains("video-out-params/rotate"));
    }
}

// volume：m_cachedVolume → 显示音量换算边界

// volume：最小缓存值 40 → 显示 0
TEST_F(MpvProxyTest, volume_AtMinimumReturnsZero)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_cachedVolume = 40;
    EXPECT_EQ(0, proxy->volume());
}

// volume：普通值按 (v-40)/60*100 换算
TEST_F(MpvProxyTest, volume_ConvertsCachedToDisplay)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_cachedVolume = 58;   // (58-40)/60*100 = 30
    EXPECT_EQ(30, proxy->volume());
}

// volume：换算结果超过 100 时返回原值
TEST_F(MpvProxyTest, volume_AboveHundredReturnsRaw)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_cachedVolume = 150;   // (150-40)/60*100 = 183 > 100 → raw
    EXPECT_EQ(150, proxy->volume());
}

// savePlaybackPosition / resizeEvent / showEvent

// savePlaybackPosition：Stopped 状态早退，不触碰 MovieConfiguration
TEST_F(MpvProxyTest, savePlaybackPosition_StoppedEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->_state = Backend::Stopped;

    EXPECT_NO_THROW(proxy->savePlaybackPosition());
}

// resizeEvent：Stopped 状态早退
TEST_F(MpvProxyTest, resizeEvent_StoppedEarlyReturn)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->_state = Backend::Stopped;

    QResizeEvent ev(QSize(640, 480), QSize(320, 240));
    EXPECT_NO_THROW(proxy->resizeEvent(&ev));
}

// resizeEvent：非 Stopped 走 Backend 默认处理
TEST_F(MpvProxyTest, resizeEvent_ForwardsToBase)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->_state = Backend::Playing;

    QResizeEvent ev(QSize(640, 480), QSize(320, 240));
    EXPECT_NO_THROW(proxy->resizeEvent(&ev));
}

// showEvent：首次调用翻转连接状态标志
TEST_F(MpvProxyTest, showEvent_FlipsConnectStateFlag)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_bConnectStateChange = false;

    QShowEvent ev;
    proxy->showEvent(&ev);
    EXPECT_TRUE(proxy->m_bConnectStateChange);
}

// showEvent：标志已置位时保持不变
TEST_F(MpvProxyTest, showEvent_KeepsFlagWhenAlreadySet)
{
    Stub stub;
    MpvProxy *proxy = ut_createProxy(stub, nullptr, true);
    ASSERT_NE(nullptr, proxy);
    proxy->m_bConnectStateChange = true;

    QShowEvent ev;
    EXPECT_NO_THROW(proxy->showEvent(&ev));
    EXPECT_TRUE(proxy->m_bConnectStateChange);
}
