// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// =====================================================================
// Mode 2 测试生成清单（qt-autotest-generator，t16）
// 1. 目标文件: src/common/event_monitor.cpp (类 EventMonitor + 全局函数
//    callback, namespace dmr)
// 2. 方法清单与分级 (inventory @4608a9f2/e5346fd1): handleRecordEvent=h,
//    run=h, EventMonitor(ctor)=m, resumeRecording=m, suspendRecording=m,
//    callback=l (free function, plan 归组 common)
// 3. 已有用例引用: 无既有 EventMonitor 用例(全仓 grep 无匹配), 本文件为
//    首次覆盖。
// 4. 分支->用例映射:
//    callback(cast 路由)                          -> callback_globalFunction_routesToHandleRecordEvent
//    handleRecordEvent: m_recording==0 丢弃分支   -> handleRecordEvent_suspended_dropsEventsButFreesData
//    handleRecordEvent: category!=FromServer 分支 -> handleRecordEvent_nonServerCategory_skippedButFreed
//    handleRecordEvent: 非 ButtonPress 且非 4..7 -> handleRecordEvent_wheelPresses_ignoredAllFour /
//                                                    handleRecordEvent_motionWithoutPress_noSignal
//    handleRecordEvent: ButtonPress 置位分支     -> handleRecordEvent_buttonPress_emitsButtonedPress
//    handleRecordEvent: MotionNotify m_bIsPress 两分支 -> handleRecordEvent_motion* 两用例
//    handleRecordEvent: ButtonRelease 3 分支     -> handleRecordEvent_buttonRelease_emitsButtonedRelease /
//                                                    handleRecordEvent_wheelReleases_ignoredAllFour
//    suspend 时 m_bIsPress 补发分支              -> handleRecordEvent_suspendWhilePressed_emitsReleaseAtCursorPos
//    resumeRecording/suspendRecording            -> recording_stateTransitions_suspendDrops_resumeRestores
//    run: display==nullptr / pRange==nullptr / context==0 / enableContext 失败
//                                                -> run_*Fails_returnsCleanly 四用例
//    run: 成功安装回调 + XFree/XSync             -> run_installsCallback_andCallbackRoutes
// 4.5 用例计数声明表(min 按 level 推导: high=3/mid=2/low=1):
//    | method | level | factors | min | actual |
//    | handleRecordEvent | high | complexity:12 | 3 | 10 |
//    | run | high | complexity:9 | 3 | 5 |
//    | EventMonitor(ctor) | mid | - | 2 | 10 |
//    | resumeRecording | mid | - | 2 | 2 |
//    | suspendRecording | mid | - | 2 | 3 |
//    | callback(free) | low | - | 1 | 2 |
//    (ctor/run 的 actual 计入所有实例化/驱动工作线程的用例)
//    状态可见性说明: m_bIsPress/m_recording 为私有, 一律以"按下后再投递
//    MotionNotify 是否发 buttonedDrag"作为按下状态的行为探针, 不访问私有成员。
// 5. 参数化数据: 轮值 4..7(WheelUp/Down/Left/Right, event_monitor.h 定义)
//    以数组循环覆盖 press/release 两侧。
// 6. 依赖与打桩: X11/XRecord 全部经 Stub::set 打桩(x86_64 CODESIZE=13),
//    合成 xEvent/XRecordInterceptData 喂给生产入口 callback(); XRecordFreeData
//    打桩为计数器(不得 free 测试栈内存); 不依赖真实 X server/输入设备。
// 7. GUI 与事件循环约束: QThread::wait 阻塞 + 信号计数; QCursor::setPos 用于
//    suspend 补发分支的坐标断言(offscreen 下 QCursor 可用); 无窗口依赖。
// 8. 编译目标: deepin-movie-test (tests/deepin-movie/common 自动纳入),
//    make deepin-movie-test && --gtest_filter='EventMonitorTest.*'
// 9. 自检入口: self-check-structural.py --file <本文件>; verify-build.py
//    阶段完成编译运行验证。
// 10. 提交说明: test(event-monitor): cover EventMonitor record pipeline;
//     豁免: run() 的 XRecordEnableContext 成功后长驻循环分支依赖真实
//     X11 会话语义, 仅验证失败路径与回调安装。
// =====================================================================

// NOTE: all Qt/project/stub headers MUST be included before the X11 block:
// Xlib-style headers #define Bool/Status which Qt6 headers #error against.
#include <gtest/gtest.h>
#include <QTest>
#include <QCursor>
#include <QPoint>

#include "event_monitor.h"
#include "stub/stub.h"

#ifndef Bool
#define Bool int
#endif
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/record.h>

using namespace dmr;

// Global callback defined in event_monitor.cpp (not declared in the header).
void callback(XPointer ptr, XRecordInterceptData *pData);

// --- synthetic X11 fixtures ------------------------------------------------

namespace emfix {

struct Counters {
    int freeDataCalls = 0;
    int enableCalls = 0;
};
static Counters g_counters;

static XRecordInterceptProc g_installedCallback = nullptr;
static XPointer g_installedClosure = nullptr;
static int g_enableResult = 1; // 1=True, 0=False for XRecordEnableContext stub

static XRecordInterceptData makeData(xEvent *ev, int cat)
{
    XRecordInterceptData d;
    memset(&d, 0, sizeof(d));
    d.id_base = 0;
    d.server_time = 1000;
    d.client_seq = 1;
    d.category = cat;
    d.client_swapped = False;
    d.data = reinterpret_cast<unsigned char *>(ev);
    d.data_len = sizeof(xEvent) / 4;
    return d;
}

static xEvent makeEvent(int type, int detail, int x, int y)
{
    xEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.u.u.type = type;
    ev.u.u.detail = detail;
    ev.u.keyButtonPointer.rootX = static_cast<INT16>(x);
    ev.u.keyButtonPointer.rootY = static_cast<INT16>(y);
    return ev;
}

static void deliver(EventMonitor &em, xEvent *ev, int cat)
{
    XRecordInterceptData d = makeData(ev, cat);
    // Act through the production entry point (global callback -> handleRecordEvent).
    callback(reinterpret_cast<XPointer>(&em), &d);
}

// Behavior probe: after delivering a MotionNotify, buttonedDrag fires iff a
// non-wheel button press is currently armed (m_bIsPress == true).
static bool pressArmed(EventMonitor &em)
{
    xEvent move = makeEvent(MotionNotify, 0, 30, 40);
    bool dragged = false;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedDrag, &ctx,
                     [&dragged](int, int) { dragged = true; });
    deliver(em, &move, XRecordFromServer);
    return dragged;
}

// RAII guard: keeps X11 stubs installed for the whole test body.
struct StubGuard {
    Stub st;
    explicit StubGuard(bool enableOk)
    {
        g_enableResult = enableOk ? 1 : 0;
        st.set(&XOpenDisplay, +[](const char *) -> Display * {
            return reinterpret_cast<Display *>(0x1);
        });
        st.set(&XRecordAllocRange, +[]() -> XRecordRange * {
            // 返回真实可写存储: 生产 run() 会对返回值做 memset/成员写入,
            // 伪指针(0x2)会在 memset 处 SIGSEGV。
            static XRecordRange range_storage;
            memset(&range_storage, 0, sizeof(range_storage));
            return &range_storage;
        });
        st.set(&XRecordCreateContext,
               +[](Display *, int, XRecordClientSpec *, int, XRecordRange **, int) -> XRecordContext {
                   return static_cast<XRecordContext>(42);
               });
        st.set(&XRecordEnableContext,
               +[](Display *, XRecordContext, XRecordInterceptProc cb, XPointer closure) -> Status {
                   ++g_counters.enableCalls;
                   g_installedCallback = cb;
                   g_installedClosure = closure;
                   return g_enableResult ? True : False;
               });
        st.set(&XFree, +[](void *) -> int { return 0; });
        st.set(&XSync, +[](Display *, Bool) -> int { return 0; });
        st.set(&XRecordFreeData, +[](XRecordInterceptData *) { ++g_counters.freeDataCalls; });
    }
    Stub &stub() { return st; }
};

static void resetCounters()
{
    g_counters = Counters();
    g_installedCallback = nullptr;
    g_installedClosure = nullptr;
    g_enableResult = 1;
}

} // namespace emfix

// ==========================================================================
// 测试夹具（::testing::Test 结构要求; stub 均为用例内局部 StubGuard）
// ==========================================================================

class EventMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==========================================================================
// callback() — 全局函数
// ==========================================================================

TEST_F(EventMonitorTest, callback_globalFunction_routesToHandleRecordEvent)
{
    // Arrange: monitor with recording enabled; press then motion probe via
    // the synthetic intercept data; XRecordFreeData stubbed as counter.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 11, 22);
    // Act: invoke the global callback exactly as XRecord would.
    XRecordInterceptData d = emfix::makeData(&ev, XRecordFromServer);
    callback(reinterpret_cast<XPointer>(&em), &d);
    // Assert: the press was processed (motion now drags) and data "freed".
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 1); // press delivery freed
    EXPECT_TRUE(emfix::pressArmed(em));            // probe motion confirms armed
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 2); // probe delivery freed too
}

// ==========================================================================
// handleRecordEvent — recording 门控与 category 过滤
// ==========================================================================

TEST_F(EventMonitorTest, handleRecordEvent_suspended_dropsEventsButFreesData)
{
    // Arrange: recording disabled via suspendRecording.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    em.suspendRecording();
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 5, 6);
    // Act: deliver a press while suspended.
    emfix::deliver(em, &ev, XRecordFromServer);
    // Assert: dropped early (no press armed), but XRecordFreeData still called
    // for every delivered event (the press and the probe motion).
    EXPECT_FALSE(emfix::pressArmed(em));
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 2);
}

TEST_F(EventMonitorTest, handleRecordEvent_nonServerCategory_skippedButFreed)
{
    // Arrange: recording enabled, event category != XRecordFromServer.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 7, 8);
    // Act: deliver with XRecordStartOfData category.
    emfix::deliver(em, &ev, XRecordStartOfData);
    // Assert: skipped (not armed), freed — for the event and the probe motion.
    EXPECT_FALSE(emfix::pressArmed(em));
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 2);
}

// ==========================================================================
// handleRecordEvent — ButtonPress / wheel 过滤
// ==========================================================================

TEST_F(EventMonitorTest, handleRecordEvent_buttonPress_emitsButtonedPress)
{
    // Arrange: recording monitor, press at (11,22) with non-wheel detail 1.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    int px = -1, py = -1, presses = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedPress, &ctx,
                     [&px, &py, &presses](int x, int y) { px = x; py = y; ++presses; });
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 11, 22);
    // Act
    emfix::deliver(em, &ev, XRecordFromServer);
    // Assert: signal with root coordinates; press state armed.
    EXPECT_EQ(presses, 1);
    EXPECT_EQ(px, 11);
    EXPECT_EQ(py, 22);
    EXPECT_TRUE(emfix::pressArmed(em));
}

TEST_F(EventMonitorTest, handleRecordEvent_wheelPresses_ignoredAllFour)
{
    // Arrange: monitor; wheel details 4..7 on ButtonPress.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    int presses = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedPress, &ctx,
                     [&presses](int, int) { ++presses; });
    // Act: deliver one event per wheel detail (4..7).
    const int wheels[] = { WheelUp, WheelDown, WheelLeft, WheelRight };
    for (int w : wheels) {
        xEvent ev = emfix::makeEvent(ButtonPress, w, 3, 4);
        emfix::deliver(em, &ev, XRecordFromServer);
    }
    // Assert: each wheel detail is ignored and leaves no press state.
    EXPECT_FALSE(emfix::pressArmed(em));
    EXPECT_EQ(presses, 0);
    // 4 wheel events freed + 1 probe motion freed.
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 5);
}

// ==========================================================================
// handleRecordEvent — MotionNotify 两分支
// ==========================================================================

TEST_F(EventMonitorTest, handleRecordEvent_motionWithoutPress_noSignal)
{
    // Arrange: monitor never pressed; count drag emissions.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    int drags = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedDrag, &ctx,
                     [&drags](int, int) { ++drags; });
    xEvent ev = emfix::makeEvent(MotionNotify, 0, 30, 40);
    // Act
    emfix::deliver(em, &ev, XRecordFromServer);
    // Assert: m_bIsPress==0 branch -> no drag signal.
    EXPECT_EQ(drags, 0);
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 1);
}

TEST_F(EventMonitorTest, handleRecordEvent_motionAfterPress_emitsButtonedDrag)
{
    // Arrange: press first to arm dragging.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent press = emfix::makeEvent(ButtonPress, 1, 1, 2);
    emfix::deliver(em, &press, XRecordFromServer);
    int dx = -1, dy = -1, drags = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedDrag, &ctx,
                     [&dx, &dy, &drags](int x, int y) { dx = x; dy = y; ++drags; });
    xEvent move = emfix::makeEvent(MotionNotify, 0, 33, 44);
    // Act
    emfix::deliver(em, &move, XRecordFromServer);
    // Assert: m_bIsPress==1 branch emits drag with the motion coordinates.
    EXPECT_EQ(drags, 1);
    EXPECT_EQ(dx, 33);
    EXPECT_EQ(dy, 44);
}

// ==========================================================================
// handleRecordEvent — ButtonRelease 分支
// ==========================================================================

TEST_F(EventMonitorTest, handleRecordEvent_buttonRelease_emitsButtonedRelease)
{
    // Arrange: press then release at another position.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent press = emfix::makeEvent(ButtonPress, 1, 1, 2);
    emfix::deliver(em, &press, XRecordFromServer);
    int rx = -1, ry = -1, releases = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedRelease, &ctx,
                     [&rx, &ry, &releases](int x, int y) { rx = x; ry = y; ++releases; });
    xEvent release = emfix::makeEvent(ButtonRelease, 1, 55, 66);
    // Act
    emfix::deliver(em, &release, XRecordFromServer);
    // Assert: release emitted with release coordinates; press state cleared.
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(rx, 55);
    EXPECT_EQ(ry, 66);
    EXPECT_FALSE(emfix::pressArmed(em));
}

TEST_F(EventMonitorTest, handleRecordEvent_wheelReleases_ignoredAllFour)
{
    // Arrange: armed press, then wheel releases 4..7.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent press = emfix::makeEvent(ButtonPress, 1, 1, 2);
    emfix::deliver(em, &press, XRecordFromServer);
    int releases = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedRelease, &ctx,
                     [&releases](int, int) { ++releases; });
    // Act: deliver wheel release 4..7 while press is armed.
    const int wheels[] = { WheelUp, WheelDown, WheelLeft, WheelRight };
    for (int w : wheels) {
        xEvent ev = emfix::makeEvent(ButtonRelease, w, 9, 9);
        emfix::deliver(em, &ev, XRecordFromServer);
    }
    // Assert: wheel release details are ignored, press stays armed.
    EXPECT_TRUE(emfix::pressArmed(em));
    EXPECT_EQ(releases, 0);
}

// ==========================================================================
// suspend 时 m_bIsPress 补发分支（suspendRecording 内部逻辑的外部可见效果）
// ==========================================================================

TEST_F(EventMonitorTest, handleRecordEvent_suspendWhilePressed_emitsReleaseAtCursorPos)
{
    // Arrange: press armed at (1,2), then suspend; cursor parked at (77,88).
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent press = emfix::makeEvent(ButtonPress, 1, 1, 2);
    emfix::deliver(em, &press, XRecordFromServer);
    ASSERT_TRUE(emfix::pressArmed(em));
    QCursor::setPos(77, 88);
    int rx = -1, ry = -1, releases = 0;
    QObject ctx;
    QObject::connect(&em, &EventMonitor::buttonedRelease, &ctx,
                     [&rx, &ry, &releases](int x, int y) { rx = x; ry = y; ++releases; });
    // Act: suspendRecording while pressed -> emits release at cursor position.
    em.suspendRecording();
    // Assert
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(rx, 77);
    EXPECT_EQ(ry, 88);
}

// ==========================================================================
// suspend/resume 状态迁移（m_recording 门控的对外行为）
// ==========================================================================

TEST_F(EventMonitorTest, recording_stateTransitions_suspendDrops_resumeRestores)
{
    // Arrange: fresh monitor is recording (m_recording==1) by construction.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 2, 3);
    // Act 1: deliver press while recording.
    emfix::deliver(em, &ev, XRecordFromServer);
    // Assert 1: processed.
    EXPECT_TRUE(emfix::pressArmed(em));
    // Act 2: suspend then deliver a release + a new press; both must be dropped.
    em.suspendRecording();
    xEvent rel = emfix::makeEvent(ButtonRelease, 1, 6, 7);
    emfix::deliver(em, &rel, XRecordFromServer);
    xEvent ev2 = emfix::makeEvent(ButtonPress, 1, 4, 5);
    emfix::deliver(em, &ev2, XRecordFromServer);
    // Assert 2: after the dropped release the press state is unchanged; the
    // dropped press cannot re-arm (probe motion stays silent).
    EXPECT_FALSE(emfix::pressArmed(em));
    // Act 3: resume then deliver a press.
    em.resumeRecording();
    xEvent ev3 = emfix::makeEvent(ButtonPress, 1, 8, 9);
    emfix::deliver(em, &ev3, XRecordFromServer);
    // Assert 3: press processed again after resume.
    EXPECT_TRUE(emfix::pressArmed(em));
}

TEST_F(EventMonitorTest, resumeRecording_idempotent_keepsRecordingActive)
{
    // Arrange: fresh monitor already recording; resume again twice.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    // Act
    em.resumeRecording();
    em.resumeRecording();
    // Assert: still recording — a press is processed normally.
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 21, 22);
    emfix::deliver(em, &ev, XRecordFromServer);
    EXPECT_TRUE(emfix::pressArmed(em));
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 2); // event + probe motion
}

// ==========================================================================
// run() — 打桩 X11 全链路的失败早退分支
// ==========================================================================

TEST_F(EventMonitorTest, run_displayOpenFails_returnsCleanly)
{
    // Arrange: XOpenDisplay returns nullptr -> run() early return.
    Stub st;
    st.set(&XOpenDisplay, +[](const char *) -> Display * { return nullptr; });
    EventMonitor em;
    // Act
    em.start();
    // Assert: thread finishes quickly and cleanly.
    EXPECT_TRUE(em.wait(3000));
    EXPECT_FALSE(em.isRunning());
}

TEST_F(EventMonitorTest, run_rangeAllocFails_returnsCleanly)
{
    // Arrange: display opens but XRecordAllocRange returns nullptr.
    Stub st;
    st.set(&XOpenDisplay, +[](const char *) -> Display * {
        return reinterpret_cast<Display *>(0x1);
    });
    st.set(&XRecordAllocRange, +[]() -> XRecordRange * { return nullptr; });
    EventMonitor em;
    // Act
    em.start();
    // Assert: early return branch, no crash, thread exits.
    EXPECT_TRUE(em.wait(3000));
    EXPECT_FALSE(em.isRunning());
}

TEST_F(EventMonitorTest, run_contextCreateFails_returnsCleanly)
{
    // Arrange: range allocates but context creation fails.
    Stub st;
    st.set(&XOpenDisplay, +[](const char *) -> Display * {
        return reinterpret_cast<Display *>(0x1);
    });
    st.set(&XRecordAllocRange, +[]() -> XRecordRange * {
        // 真实可写存储: 生产 run() 会 memset 该返回值。
        static XRecordRange range_storage;
        memset(&range_storage, 0, sizeof(range_storage));
        return &range_storage;
    });
    st.set(&XRecordCreateContext,
           +[](Display *, int, XRecordClientSpec *, int, XRecordRange **, int) -> XRecordContext {
               return static_cast<XRecordContext>(0);
           });
    EventMonitor em;
    // Act
    em.start();
    // Assert: context==0 branch frees the range and returns.
    EXPECT_TRUE(em.wait(3000));
    EXPECT_FALSE(em.isRunning());
}

TEST_F(EventMonitorTest, run_enableContextFails_returnsCleanly)
{
    // Arrange: everything succeeds until XRecordEnableContext returns False.
    Stub st;
    st.set(&XOpenDisplay, +[](const char *) -> Display * {
        return reinterpret_cast<Display *>(0x1);
    });
    st.set(&XRecordAllocRange, +[]() -> XRecordRange * {
        static XRecordRange range_storage;
        memset(&range_storage, 0, sizeof(range_storage));
        return &range_storage;
    });
    st.set(&XRecordCreateContext,
           +[](Display *, int, XRecordClientSpec *, int, XRecordRange **, int) -> XRecordContext {
               return static_cast<XRecordContext>(42);
           });
    st.set(&XRecordEnableContext,
           +[](Display *, XRecordContext, XRecordInterceptProc, XPointer) -> Status {
               return False;
           });
    st.set(&XFree, +[](void *) -> int { return 0; });
    st.set(&XSync, +[](Display *, Bool) -> int { return 0; });
    EventMonitor em;
    // Act
    em.start();
    // Assert: enable-failure branch returns cleanly.
    EXPECT_TRUE(em.wait(3000));
    EXPECT_FALSE(em.isRunning());
}

TEST_F(EventMonitorTest, run_installsCallback_andCallbackRoutes)
{
    // Arrange: full success path; XRecordEnableContext captures the callback.
    emfix::StubGuard guard(true);
    emfix::resetCounters();
    EventMonitor em;
    // Act
    em.start();
    const bool finished = em.wait(3000);
    // Assert: the global callback() was installed with the monitor as closure;
    // the captured callback routes into this monitor's handleRecordEvent.
    ASSERT_TRUE(finished);
    ASSERT_NE(emfix::g_installedCallback, nullptr);
    EXPECT_EQ(emfix::g_installedClosure, reinterpret_cast<XPointer>(&em));
    xEvent ev = emfix::makeEvent(ButtonPress, 1, 13, 14);
    XRecordInterceptData d = emfix::makeData(&ev, XRecordFromServer);
    emfix::g_installedCallback(emfix::g_installedClosure, &d);
    EXPECT_TRUE(emfix::pressArmed(em));            // probe motion confirms routed
    EXPECT_EQ(emfix::g_counters.freeDataCalls, 2); // event + probe motion freed
}
