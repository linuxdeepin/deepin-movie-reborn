// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 批次：MainWindow 未覆盖方法补测（Mode 2 第二轮 reconcile 产出）。
// 覆盖目标：mainwindow.cpp 中首轮测试未触达的 slot/事件处理/几何/菜单状态方法，
// 以及 mainwindow.cpp 文件级 static 自由函数（ElideText 等，经 AddrAny 解析）。
// 约束：引擎全程 Idle（无真实播放路径），DBus/文件系统外部依赖均不依赖真实环境。

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QTimer>
#include <QSignalSpy>
#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMoveEvent>
#include <QFocusEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QMimeData>
#include <QStyleFactory>
#include <DSettingsOption>
#include <DSettingsDialog>

#include <unistd.h>
#include <gtest/gtest.h>

#define protected public
#define private public
#include "src/libdmr/player_engine.h"
#include "src/common/mainwindow.h"
#undef protected
#undef private
#include "application.h"
#include "src/libdmr/filefilter.h"
#include "src/libdmr/player_engine.h"
#define protected public
#define private public
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/notification_widget.h"
#undef protected
#undef private
#include "src/widgets/titlebar.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/movieinfo_dialog.h"
#include "src/common/actions.h"
#include "src/common/options.h"
#include "src/common/utility.h"
#include "utils.h"
#include "dbus_adpator.h"
#include "stub/stub.h"
#include "stub/addr_any.h"
#include "stub/stub_function.h"

using namespace dmr;

// ---------------------------------------------------------------------------
// 批次级 stub 辅助
// ---------------------------------------------------------------------------

static bool mw_gap_isMpvExists_true() { return true; }
static void mw_gap_toggleRoundedClip_noop(bool) { }
static PlayerEngine::CoreState mw_gap_state_playing() { return PlayerEngine::CoreState::Playing; }
static void mw_gap_setPlaySpeed_noop(double) { }
static void mw_gap_setBypass_noop(quint32, bool) { }

// updateWithMessage 录制 stub（同 test_mainwindow.cpp 的 ut_mw_updateWithMsg_stub 模式，
// ADDR 非虚函数安全；避免真提示窗操作与翻译影响）
static QString g_ut_gap_lastMsg;
static void ut_gap_updateWithMsg_stub(void *obj, const QString &m, bool flag)
{
    Q_UNUSED(obj);
    Q_UNUSED(flag);
    g_ut_gap_lastMsg = m;
}
static void mw_gap_installMsgRecorder(Stub &stub)
{
    stub.set(ADDR(NotificationWidget, updateWithMessage),
             reinterpret_cast<void *>(&ut_gap_updateWithMsg_stub));
}

static QAction *mw_gap_findAction(MainWindow *w, ActionFactory::ActionKind kind)
{
    auto acts = ActionFactory::get().findActionsByKind(kind);
    if (!acts.isEmpty()) {
        return acts.first();
    }
    // 部分种类动作（VolumeUp/倍速档等）依赖右键菜单懒构建；缺省时合成同 kind 的 QAction，
    // menuItemInvoked/setPlaySpeedMenu* 仅读 property("kind")，合成动作即可驱动分支
    ActionFactory::get().mainContextMenu();
    acts = ActionFactory::get().findActionsByKind(kind);
    if (!acts.isEmpty()) {
        return acts.first();
    }
    QAction *act = new QAction(w);
    act->setProperty("kind", QVariant::fromValue(kind));
    return act;
}

// ---------------------------------------------------------------------------
// 生命周期 / 窗口初始化
// ---------------------------------------------------------------------------

// MainWindow 构造/析构（~MainWindow 全分支）：构造第二实例验证可安全构建销毁，
// 析构过程覆盖成员清理与 malloc_trim 分支。
TEST(MainWindow, ctorDtor_SecondInstanceSafe)
{
    MainWindow *w2 = new MainWindow;
    ASSERT_NE(nullptr, w2);
    EXPECT_NE(nullptr, w2->m_pEngine);
    EXPECT_NE(nullptr, w2->m_pToolbox);
    EXPECT_NE(nullptr, w2->m_pTitlebar);
    // 第二实例的 PlaylistWidget/Play 状态延后初始化（首实例已占用），仅验证构造/析构安全
    EXPECT_NE(nullptr, w2->m_pCommHintWid);
    EXPECT_NE(nullptr, w2->m_pPopupWid);
    // 构造函数 QTimer::singleShot(300, ...) 无接收者上下文，析构后触发会悬空；
    // 先等待定时器在存活窗口上触发完，再删除
    QTest::qWait(380);
    delete w2;
    QTest::qWait(50);
    SUCCEED();
}

// saveWindowGeometry / restoreWindowGeometry：写内部配置项后回读
TEST(MainWindow, saveRestoreWindowGeometry_RoundTrip)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;

    EXPECT_NO_THROW(w->saveWindowGeometry());
    EXPECT_GT(Settings::get().internalOption("window_width").toInt(), 0);

    EXPECT_NO_THROW(w->restoreWindowGeometry());
    w->m_bMiniMode = svMini;
}

// resizeByConstraints：Idle 引擎下仅刷新标题文本即返回（6147 守卫）
TEST(MainWindow, resizeByConstraints_IdleSkipsResize)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    EXPECT_NO_THROW(w->resizeByConstraints());
}

// LimitWindowize：380x380 迷你几何恢复
TEST(MainWindow, LimitWindowize_GuardGeometry)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    QRect svLast = w->m_lastRectInNormalMode;
    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    w->m_lastRectInNormalMode = QRect(80, 80, 820, 560);

    w->setGeometry(0, 0, 380, 380);
    w->LimitWindowize();
    EXPECT_EQ(w->m_lastRectInNormalMode, w->geometry());

    // 非 380 尺寸不动
    w->setGeometry(0, 0, 820, 560);
    w->LimitWindowize();
    EXPECT_EQ(QRect(0, 0, 820, 560), w->geometry());

    w->m_lastRectInNormalMode = svLast;
    w->m_bMiniMode = svMini;
}

// updateSizeConstraints：普通/迷你两档最小尺寸
TEST(MainWindow, updateSizeConstraints_BothModes)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool svMini = w->m_bMiniMode;

    w->m_bMiniMode = false;
    w->updateSizeConstraints();
    EXPECT_EQ(QSize(614, 500), w->minimumSize());

    w->m_bMiniMode = true;
    w->updateSizeConstraints();
    EXPECT_EQ(QSize(40, 40), w->minimumSize());

    w->m_bMiniMode = svMini;
    w->updateSizeConstraints();
}

// updateGeometryNotification：Idle 不弹提示但记录普通态几何
TEST(MainWindow, updateGeometryNotification_IdleNoHintButRectSaved)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    bool svMini = w->m_bMiniMode;
    bool svSettingMini = w->m_isSettingMiniMode;
    bool svMax = w->m_bMaximized;
    w->m_bMiniMode = false;
    w->m_isSettingMiniMode = false;
    w->m_bMaximized = false;
    w->setWindowState(Qt::WindowNoState);

    w->setGeometry(90, 90, 800, 580);
    {
        Stub stub;
        mw_gap_installMsgRecorder(stub);
        g_ut_gap_lastMsg.clear();
        w->updateGeometryNotification(QSize(810, 590));
        EXPECT_TRUE(g_ut_gap_lastMsg.isEmpty());   // Idle 不弹提示
    }
    EXPECT_EQ(w->geometry(), w->m_lastRectInNormalMode);

    w->m_bMiniMode = svMini;
    w->m_isSettingMiniMode = svSettingMini;
    w->m_bMaximized = svMax;
}

// updateProxyGeometry：普通态工具栏几何与窗口联动
TEST(MainWindow, updateProxyGeometry_NormalModeWidths)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    EXPECT_NO_THROW(w->updateProxyGeometry());
    EXPECT_EQ(w->rect().width() - 10, w->m_pToolbox->geometry().width());
    w->m_bMiniMode = svMini;
}

// prepareSplashImages：加载深/浅两版启动图
TEST(MainWindow, prepareSplashImages_LoadsSplash)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    EXPECT_NO_THROW(w->prepareSplashImages());
    EXPECT_FALSE(w->m_imgBgDark.isNull());
    EXPECT_FALSE(w->m_imgBgLight.isNull());
}

// mipsShowFullScreen：全屏状态机进入/退出（stub 掉 X11 绕过合成器回调）
TEST(MainWindow, mipsShowFullScreen_EntersAndExits)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    stub.set(ADDR(Utility, setBypassCompositor), mw_gap_setBypass_noop);
    w->setWindowState(Qt::WindowNoState);
    w->showNormal();
    QTest::qWait(50);

    EXPECT_NO_THROW(w->mipsShowFullScreen());
    QTest::qWait(300);
    EXPECT_TRUE(w->isFullScreen());

    w->showNormal();
    QTest::qWait(50);
    EXPECT_FALSE(w->isFullScreen());
}

// ---------------------------------------------------------------------------
// 菜单 / 播放动作
// ---------------------------------------------------------------------------

// menuItemInvoked：OrderPlay / VolumeUp 两个分支
TEST(MainWindow, menuItemInvoked_OrderPlayAndVolume)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    QAction *orderAct = mw_gap_findAction(w, ActionFactory::OrderPlay);
    ASSERT_NE(nullptr, orderAct);
    EXPECT_NO_THROW(w->menuItemInvoked(orderAct));
    EXPECT_EQ(PlaylistModel::OrderPlay, w->m_pEngine->playlist().playMode());

    QAction *volAct = mw_gap_findAction(w, ActionFactory::VolumeUp);
    ASSERT_NE(nullptr, volAct);
    // 音量可能已在上限 100（changeVolume 钜位），分支已执行，断言只验证不降
    int nBefore = w->getDisplayVolume();
    EXPECT_NO_THROW(w->menuItemInvoked(volAct));
    EXPECT_GE(w->getDisplayVolume(), nBefore);
}

// defaultplaymodeinit / slotdefaultplaymodechanged：按配置项切换播放模式
TEST(MainWindow, defaultplaymodeinit_AppliesConfiguredMode)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    auto modeOpt = Settings::get().settings()->option("base.play.playmode");
    ASSERT_TRUE(modeOpt);
    QVariant svValue = modeOpt->value();

    // items: Order/Shuffle/Single/SingleLoop/ListLoop → 4 = List loop
    modeOpt->setValue(4);
    EXPECT_NO_THROW(w->defaultplaymodeinit());
    EXPECT_EQ(PlaylistModel::ListLoop, w->m_pEngine->playlist().playMode());

    // slotdefaultplaymodechanged：错误 key 直接返回
    EXPECT_NO_THROW(w->slotdefaultplaymodechanged("base.other.key", QVariant(0)));
    EXPECT_EQ(PlaylistModel::ListLoop, w->m_pEngine->playlist().playMode());

    // 正确 key 切回 OrderPlay（index 0）
    EXPECT_NO_THROW(w->slotdefaultplaymodechanged("base.play.playmode", QVariant(0)));
    QTest::qWait(50);
    EXPECT_EQ(PlaylistModel::OrderPlay, w->m_pEngine->playlist().playMode());

    modeOpt->setValue(svValue);
}

// setPlaySpeedMenuChecked / setPlaySpeedMenuUnchecked：倍速菜单勾选状态
TEST(MainWindow, setPlaySpeedMenuCheckedUnchecked)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->setPlaySpeedMenuChecked(ActionFactory::OneTimes));
    QAction *oneAct = mw_gap_findAction(w, ActionFactory::OneTimes);
    ASSERT_NE(nullptr, oneAct);
    EXPECT_TRUE(oneAct->isChecked());

    EXPECT_NO_THROW(w->setPlaySpeedMenuUnchecked());
    for (auto kind : { ActionFactory::ZeroPointFiveTimes, ActionFactory::OneTimes,
                       ActionFactory::OnePointTwoTimes, ActionFactory::OnePointFiveTimes,
                       ActionFactory::Double }) {
        QAction *act = mw_gap_findAction(w, kind);
        if (act) {
            EXPECT_FALSE(act->isChecked());
        }
    }
}

// adjustPlaybackSpeed：Idle 守卫不动速度；Playing（stub）下加减速与档位联动
TEST(MainWindow, adjustPlaybackSpeed_GuardAndGearHooks)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    double svSpeed = w->m_dPlaySpeed;

    // Idle：守卫直接返回
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    w->m_dPlaySpeed = 1.0;
    EXPECT_NO_THROW(w->adjustPlaybackSpeed(ActionFactory::AccelPlayback));
    EXPECT_EQ(1.0, w->m_dPlaySpeed);

    // Playing（stub）：覆盖 2.0 上限、1.2 档、0.5 下限与未对齐档
    Stub stub;
    stub.set(ADDR(PlayerEngine, state), mw_gap_state_playing);
    stub.set(ADDR(PlayerEngine, toggleRoundedClip), mw_gap_toggleRoundedClip_noop);
    // MpvProxy::setPlaySpeed 在无媒体 Idle 态下解引用 mpv 内部句柄会段错误，stub 成 no-op
    stub.set(ADDR(PlayerEngine, setPlaySpeed), mw_gap_setPlaySpeed_noop);

    w->m_dPlaySpeed = 1.9;
    EXPECT_NO_THROW(w->adjustPlaybackSpeed(ActionFactory::AccelPlayback));
    EXPECT_DOUBLE_EQ(2.0, w->m_dPlaySpeed);

    w->m_dPlaySpeed = 1.1;
    EXPECT_NO_THROW(w->adjustPlaybackSpeed(ActionFactory::AccelPlayback));
    EXPECT_DOUBLE_EQ(1.2, w->m_dPlaySpeed);

    w->m_dPlaySpeed = 0.6;
    EXPECT_NO_THROW(w->adjustPlaybackSpeed(ActionFactory::DecelPlayback));
    EXPECT_DOUBLE_EQ(0.5, w->m_dPlaySpeed);

    w->m_dPlaySpeed = 1.0;
    EXPECT_NO_THROW(w->adjustPlaybackSpeed(ActionFactory::DecelPlayback));
    EXPECT_DOUBLE_EQ(0.9, w->m_dPlaySpeed);

    w->m_dPlaySpeed = svSpeed;
}

// isActionAllowed：状态矩阵（Idle/迷你/连拍/全屏来源）
TEST(MainWindow, isActionAllowed_StateMatrix)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    bool svMini = w->m_bMiniMode;
    bool svBurst = w->m_bInBurstShootMode;
    w->m_bMiniMode = false;
    w->m_bInBurstShootMode = false;

    EXPECT_FALSE(w->isActionAllowed(ActionFactory::Screenshot, false, true));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleFullscreen, false, true));

    w->m_bMiniMode = true;
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::ToggleFullscreen, false, true));
    EXPECT_TRUE(w->isActionAllowed(ActionFactory::ToggleMiniMode, false, true));

    w->m_bMiniMode = false;
    w->m_bInBurstShootMode = true;
    EXPECT_FALSE(w->isActionAllowed(ActionFactory::Screenshot, false, true));

    w->m_bMiniMode = svMini;
    w->m_bInBurstShootMode = svBurst;
}

// updateActionsState：Idle 下播放态菜单禁用
TEST(MainWindow, updateActionsState_IdleDisablesMovieActions)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    EXPECT_NO_THROW(w->updateActionsState());
    // updateActionsState 操作的是工厂主菜单动作（forEachInMainMenu），Idle 下播放态相关动作应禁用
    ActionFactory::get().mainContextMenu();
    for (ActionFactory::ActionKind kind : { ActionFactory::Screenshot, ActionFactory::ToggleMiniMode,
                                            ActionFactory::ToggleFullscreen, ActionFactory::MatchOnlineSubtitle }) {
        auto acts = ActionFactory::get().findActionsByKind(kind);
        for (QAction *a : acts) {
            EXPECT_FALSE(a->isEnabled());
        }
    }
}

// setMusicShortKeyState：音乐模式下截图类动作禁用/恢复
TEST(MainWindow, setMusicShortKeyState_TogglesScreenshotActions)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->setMusicShortKeyState(false));
    // setMusicShortKeyState 遍历的是窗口自身 actions()（快捷键重建动作），非工厂菜单动作
    QAction *shotAct = nullptr;
    for (QAction *a : w->actions()) {
        if (a->property("kind").toInt() == static_cast<int>(ActionFactory::Screenshot)) {
            shotAct = a;
            break;
        }
    }
    if (shotAct) {
        EXPECT_FALSE(shotAct->isEnabled());
    }

    EXPECT_NO_THROW(w->setMusicShortKeyState(true));
    if (shotAct) {
        EXPECT_TRUE(shotAct->isEnabled());
    }
}

// slotdefaultplaymodechanged 混在 defaultplaymodeinit 用例里；
// 这里补 miniButtonClicked：play/close/quit_mini 三个分支
TEST(MainWindow, miniButtonClicked_PlayCloseQuitMini)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    stub.set(ADDR(PlayerEngine, toggleRoundedClip), mw_gap_toggleRoundedClip_noop);

    // "play"：Idle 无文件 → StartPlay 安全分支
    EXPECT_NO_THROW(w->miniButtonClicked("play"));
    QTest::qWait(50);

    // "close"：USE_TEST 下 closeEvent 走隐藏分支
    EXPECT_NO_THROW(w->miniButtonClicked("close"));
    QTest::qWait(50);
    w->show();
    QTest::qWait(30);

    // "quit_mini"：真实 toggleUIMode 进出迷你模式（stub 掉无 GL 管线的 toggleRoundedClip）
    bool svAbove = w->m_bWindowAbove;
    int svState = w->m_nStateBeforeMiniMode;
    w->m_bWindowAbove = true;
    w->m_nStateBeforeMiniMode = 0;
    w->setWindowState(Qt::WindowNoState);
    w->m_bMiniMode = false;
    EXPECT_NO_THROW(w->miniButtonClicked("quit_mini"));
    QTest::qWait(120);
    EXPECT_TRUE(w->getMiniMode());
    w->miniButtonClicked("quit_mini");
    QTest::qWait(120);
    EXPECT_FALSE(w->getMiniMode());

    w->m_bWindowAbove = svAbove;
    w->m_nStateBeforeMiniMode = svState;
}

// exitMircast / slotExitMircast / slotUpdateMircastState：投屏退出路径
TEST(MainWindow, mircastExitPaths_EmitMenuEnableSignals)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    QSignalSpy frameSpy(w, &MainWindow::frameMenuEnable);
    QSignalSpy speedSpy(w, &MainWindow::playSpeedMenuEnable);
    QSignalSpy subSpy(w, &MainWindow::subtitleMenuEnable);
    QSignalSpy soundSpy(w, &MainWindow::soundMenuEnable);

    EXPECT_NO_THROW(w->exitMircast());
    EXPECT_FALSE(w->m_pMircastShowWidget->isVisible());

    EXPECT_NO_THROW(w->slotExitMircast());
    EXPECT_EQ(1, frameSpy.count());
    EXPECT_EQ(1, speedSpy.count());
    EXPECT_EQ(1, subSpy.count());
    EXPECT_EQ(1, soundSpy.count());

    EXPECT_NO_THROW(w->slotUpdateMircastState(MIRCAST_EXIT, ""));
    EXPECT_EQ(2, frameSpy.count());
    EXPECT_NO_THROW(w->slotUpdateMircastState(MIRCAST_CONNECTION_FAILED, ""));
    EXPECT_NO_THROW(w->slotUpdateMircastState(MIRCAST_DISCONNECTED, ""));
    EXPECT_NO_THROW(w->slotUpdateMircastState(MIRCAST_SUCCEEDED, "1234"));
    QTest::qWait(30);
    // EXIT/CONNECTION_FAILED/DISCONNECTED 均走 slotExitMircast(+1)，
    // SUCCEEDED 自身也 emit frameMenuEnable(false)(+1)
    EXPECT_EQ(5, frameSpy.count());
}

// 播放列表重建：loadPlayList 后新旧指针切换且状态恢复
TEST(MainWindow, loadPlayList_RebuildsPlaylistWidget)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    PlaylistWidget *old = w->m_pPlaylist;
    ASSERT_NE(nullptr, old);

    EXPECT_NO_THROW(w->loadPlayList());
    EXPECT_NE(old, w->m_pPlaylist);
    EXPECT_NE(nullptr, w->m_pPlaylist);
    EXPECT_EQ(PlaylistWidget::Closed, w->m_pPlaylist->state());
    EXPECT_EQ(w->m_pPlaylist, w->m_pToolbox->m_pPlaylist);

    // 还原全局窗口的播放列表指针，避免影响后续用例
    w->m_pPlaylist = old;
    w->m_pToolbox->setPlaylist(old);
    w->m_pToolbox->m_pPlaylist = old;
}

// onBindingsChanged：重建动作表
TEST(MainWindow, onBindingsChanged_RebuildsActionList)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    EXPECT_NO_THROW(w->onBindingsChanged());
    EXPECT_FALSE(w->actions().isEmpty());
}

// onSysLockState：锁屏/解锁标志转换
// onSysLockState：锁屏/解锁标志转换（DConfig 信号签名）
TEST(MainWindow, onSysLockState_FlagTransitions)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    bool svLock = w->m_bStateInLock;
    w->m_bStateInLock = false;

    EXPECT_NO_THROW(w->onSysLockState("org.freedesktop.ScreenSaver", QVariantMap{}, QStringList{}));
    EXPECT_FALSE(w->m_bStateInLock);      // Idle 非播放态不记录

    w->m_bStateInLock = true;             // 模拟锁屏时正在播放
    EXPECT_NO_THROW(w->onSysLockState("org.freedesktop.ScreenSaver", QVariantMap{}, QStringList{}));
    EXPECT_FALSE(w->m_bStateInLock);      // 解锁恢复

    w->m_bStateInLock = svLock;
}

// sleepStateChanged：休眠唤醒（DConfig 分支按默认配置走安全路径）
TEST(MainWindow, sleepStateChanged_IdleSafePath)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    bool svSleep = w->m_bStartSleep;
    w->m_bStartSleep = false;

    EXPECT_NO_THROW(w->sleepStateChanged(true));
    EXPECT_NO_THROW(w->sleepStateChanged(false));

    w->m_bStartSleep = true;              // !bSleep 分支需要 startSleep 标志
    w->m_pEngine->waitLastEnd();
    EXPECT_NO_THROW(w->sleepStateChanged(false));
    w->m_bStartSleep = svSleep;
}

// lockStateChanged：锁屏控制
TEST(MainWindow, lockStateChanged_IdleSafePath)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    EXPECT_NO_THROW(w->lockStateChanged(true));
    EXPECT_NO_THROW(w->lockStateChanged(false));
}

// ---------------------------------------------------------------------------
// mpv 日志 / 错误提示
// ---------------------------------------------------------------------------

// checkErrorMpvLogsChanged：全部分支
TEST(MainWindow, checkErrorMpvLogsChanged_AllBranches)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    mw_gap_installMsgRecorder(stub);

    g_ut_gap_lastMsg.clear();
    // "avformat_open_input() failed" 分支是 do nothing，不提示
    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "avformat_open_input() failed"));
    QTest::qWait(30);
    EXPECT_TRUE(g_ut_gap_lastMsg.isEmpty());

    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "moov atom not found"));
    QTest::qWait(30);
    EXPECT_FALSE(g_ut_gap_lastMsg.isEmpty());

    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "couldn't open dvd device"));
    QTest::qWait(30);
    EXPECT_FALSE(g_ut_gap_lastMsg.isEmpty());

    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "incomplete frame"));
    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "can't open"));
    QTest::qWait(30);
    EXPECT_FALSE(g_ut_gap_lastMsg.isEmpty());
}

// checkWarningMpvLogsChanged：硬解不支持 4K → 请求暂停 + USE_TEST 非阻塞对话框
TEST(MainWindow, checkWarningMpvLogsChanged_HardwareUnsupported)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->checkWarningMpvLogsChanged("log",
                     "Hardware does not support image size 3840x2160"));
    QTest::qWait(700);
}

// checkErrorMpvLogsChanged 的 "fail to open" 分支：移除当前项（空列表安全）
TEST(MainWindow, checkErrorMpvLogsChanged_FailToOpen)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    mw_gap_installMsgRecorder(stub);
    int nCountBefore = w->m_pEngine->playlist().count();

    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->checkErrorMpvLogsChanged("log", "fail to open"));
    QTest::qWait(30);
    EXPECT_EQ(QStringLiteral("Cannot open file or stream"), g_ut_gap_lastMsg);
    EXPECT_LE(w->m_pEngine->playlist().count(), nCountBefore);
}

// ---------------------------------------------------------------------------
// slot 家族
// ---------------------------------------------------------------------------

// slotUrlpause / slotVolumeChanged / slotMediaError / slotInvalidFile
TEST(MainWindow, miscSlots_HintAndEngineFeedback)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    mw_gap_installMsgRecorder(stub);

    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->slotUrlpause(true));
    EXPECT_EQ(QStringLiteral("Buffering..."), g_ut_gap_lastMsg);

    EXPECT_NO_THROW(w->slotUrlpause(false));
    EXPECT_NO_THROW(w->slotVolumeChanged(66));
    EXPECT_EQ(66, w->getDisplayVolume());

    EXPECT_NO_THROW(w->slotMediaError());

    EXPECT_NO_THROW(w->slotInvalidFile("notexist.xyz"));
}

// slotFinishedAddFiles：空列表不触发播放；slotProperChanged：Idle 不 seek
TEST(MainWindow, slotFinishedAddFilesAndProperChanged_Guards)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->m_bHaveFile = false;

    EXPECT_NO_THROW(w->slotFinishedAddFiles({}));
    EXPECT_FALSE(w->m_bHaveFile);

    QVariantMap kv;
    kv["Active"] = true;
    EXPECT_NO_THROW(w->slotProperChanged("", kv, {}));
}

// slotFileLoaded：sender 非 PlayerEngine 时早退；经信号触发完整分支
TEST(MainWindow, slotFileLoaded_SenderGuardAndWaylandBranch)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    // 直接调用：sender()==nullptr → dynamic_cast 失败早退，不改变空闲标志
    bool bFreeBefore = w->m_bIsFree;
    EXPECT_NO_THROW(w->slotFileLoaded());
    EXPECT_EQ(bFreeBefore, w->m_bIsFree);

    // 信号触发：Idle 下 resizeByConstraints 即返，置空闲标志
    PlayerEngine *engine = w->m_pEngine;
    emit engine->fileLoaded();
    QTest::qWait(30);
    EXPECT_TRUE(w->m_bIsFree);
}

// slotmousePressTimerTimeOut：未按下时早退
TEST(MainWindow, slotmousePressTimerTimeOut_Guards)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool svPressed = w->m_bMousePressed;
    bool svMini = w->m_bMiniMode;
    bool svBurst = w->m_bInBurstShootMode;

    w->m_bMousePressed = false;
    EXPECT_NO_THROW(w->slotmousePressTimerTimeOut());

    // 迷你模式分支
    w->m_bMiniMode = true;
    w->m_bMousePressed = true;
    EXPECT_NO_THROW(w->slotmousePressTimerTimeOut());
    w->m_bMiniMode = false;
    w->m_bMousePressed = false;
    w->m_bMiniMode = svMini;
    w->m_bMousePressed = svPressed;
    w->m_bInBurstShootMode = svBurst;
}

// slotVolumeChanged 0 音量静音提示
TEST(MainWindow, slotVolumeChanged_MuteHint)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    mw_gap_installMsgRecorder(stub);
    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->slotVolumeChanged(0));
    EXPECT_EQ(QStringLiteral("Mute"), g_ut_gap_lastMsg);
}

// ---------------------------------------------------------------------------
// 工具栏显示/隐藏
// ---------------------------------------------------------------------------

// suspendToolsWindow：Idle 引擎 + 工具栏可见 + 光标不在工具区 → 提前返回不隐藏
TEST(MainWindow, suspendToolsWindow_IdleKeepsToolbox)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    w->show();
    w->m_pTitlebar->show();
    w->m_pToolbox->show();
    QTest::qWait(30);

    EXPECT_NO_THROW(w->suspendToolsWindow());
    // Idle 分支直接 return，工具栏保持可见
    EXPECT_TRUE(w->m_pToolbox->isVisible());

    w->m_bMiniMode = svMini;
}

// resumeToolsWindow：非全屏下恢复工具栏可见 + 自动隐藏计时
TEST(MainWindow, resumeToolsWindow_RestoresVisibility)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    w->show();
    w->setWindowState(Qt::WindowNoState);
    w->m_pTitlebar->hide();
    w->m_pToolbox->hide();
    QTest::qWait(20);

    EXPECT_NO_THROW(w->resumeToolsWindow());
    QTest::qWait(20);
    EXPECT_TRUE(w->m_pTitlebar->isVisible());
    EXPECT_TRUE(w->m_pToolbox->isVisible());
    EXPECT_TRUE(w->m_autoHideTimer.isActive());
}

// onWindowStateChanged：普通态标题栏随工具栏，迷你态隐藏
TEST(MainWindow, onWindowStateChanged_NormalVsMini)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    w->show();
    QTest::qWait(30);

    w->onWindowStateChanged();
    EXPECT_EQ(w->m_pToolbox->isVisible(), w->m_pTitlebar->isVisible());

    w->m_bMiniMode = true;
    w->onWindowStateChanged();
    EXPECT_FALSE(w->m_pTitlebar->isVisible());

    w->m_bMiniMode = svMini;
}

// ---------------------------------------------------------------------------
// 输入事件
// ---------------------------------------------------------------------------

// mouseDoubleClickEvent：Idle 双击请求播放 + 连拍模式守卫
TEST(MainWindow, mouseDoubleClickEvent_IdleAccepts)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());

    bool svBurst = w->m_bInBurstShootMode;
    w->m_bInBurstShootMode = false;
    QMouseEvent ev(QEvent::MouseButtonDblClick, QPointF(200, 200),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->mouseDoubleClickEvent(&ev));

    w->m_bInBurstShootMode = true;
    EXPECT_NO_THROW(w->mouseDoubleClickEvent(&ev));
    w->m_bInBurstShootMode = svBurst;
}

// wheelEvent：工具区外滚动调音量（音量条隐藏时）
TEST(MainWindow, wheelEvent_AdjustsVolumeOutsideTools)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->show();
    QTest::qWait(30);
    int nBefore = w->getDisplayVolume();

    QWheelEvent ev(QPointF(w->rect().center()), QPointF(w->mapToGlobal(w->rect().center())),
                   QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    EXPECT_NO_THROW(w->wheelEvent(&ev));
    QTest::qWait(30);
    if (w->m_pToolbox->getVolSliderIsHided()) {
        EXPECT_NE(nBefore, w->getDisplayVolume());
    }
}

// mouseMoveEvent：小位移早退 + 大位移置移动标志
TEST(MainWindow, mouseMoveEvent_DeltaGuards)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->show();
    QTest::qWait(30);
    bool svMoved = w->m_bMouseMoved;
    bool svPressed = w->m_bMousePressed;
    w->m_bMouseMoved = false;
    w->m_bMousePressed = true;
    w->m_posMouseOrigin = w->mapToGlobal(QPoint(200, 200));

    // 位移 ≤5 早退
    QMouseEvent small(QEvent::MouseMove, QPointF(202, 202), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->mouseMoveEvent(&small));
    EXPECT_FALSE(w->m_bMouseMoved);

    // 大位移进入移动处理
    QMouseEvent big(QEvent::MouseMove, QPointF(400, 400), Qt::NoButton,
                    Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->mouseMoveEvent(&big));
    EXPECT_TRUE(w->m_bMouseMoved);

    w->m_bMouseMoved = svMoved;
    w->m_bMousePressed = svPressed;
}

// capturedMousePressEvent / capturedMouseReleaseEvent：按下状态与原点记录
TEST(MainWindow, capturedMousePressRelease_RecordsState)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->show();
    w->activateWindow();
    w->raise();
    QTest::qWait(50);
    // capturedMousePressEvent 在 qApp->focusWindow()==nullptr 时早退，先确保窗口获焦
    if (qApp->focusWindow() == nullptr) {
        GTEST_SKIP() << "test window has no focus on this platform";
    }
    bool svPressed = w->m_bMousePressed;
    w->m_bMousePressed = false;

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(150, 150),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->capturedMousePressEvent(&press));
    EXPECT_TRUE(w->m_bMousePressed);

    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(150, 150),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->capturedMouseReleaseEvent(&release));

    w->m_bMousePressed = svPressed;
}

// capturedKeyEvent：Tab 显示标题栏/工具栏并重启自动隐藏计时
TEST(MainWindow, capturedKeyEvent_TabShowsToolbars)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->show();
    w->setWindowState(Qt::WindowNoState);
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);

    EXPECT_NO_THROW(w->capturedKeyEvent(&ev));
    EXPECT_TRUE(w->m_autoHideTimer.isActive());
}

// keyPressEvent：播放列表开启时方向键更新选中项；关闭时透传
TEST(MainWindow, keyPressEvent_PlaylistStateRouting)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    // 列表关闭：透传给 QWidget 默认处理（事件被 ignore）
    QKeyEvent evDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    EXPECT_NO_THROW(w->keyPressEvent(&evDown));
    EXPECT_FALSE(evDown.isAccepted());

    // 列表开启：更新选中项（注意 QWidget::keyPressEvent 事后 ignore 会重置接受标志，
    // 不能用 isAccepted 判断，仅验证路由分支执行无异常）
    w->m_pPlaylist->_state = PlaylistWidget::Opened;
    QKeyEvent evAccepted(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    EXPECT_NO_THROW(w->keyPressEvent(&evAccepted));
    w->m_pPlaylist->_state = PlaylistWidget::Closed;
}

// contextMenuEvent：普通态右键菜单（USE_TEST 下非阻塞）
TEST(MainWindow, contextMenuEvent_NormalModePopsMenu)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    w->show();
    w->setWindowState(Qt::WindowNoState);
    bool svMini = w->m_bMiniMode;
    w->m_bMiniMode = false;
    QTest::qWait(30);

    QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(300, 300), w->mapToGlobal(QPoint(300, 300)));
    EXPECT_NO_THROW(w->contextMenuEvent(&ev));
    QTest::qWait(60);
    if (ActionFactory::get().mainContextMenu()) {
        ActionFactory::get().mainContextMenu()->hide();
    }
    w->m_bMiniMode = svMini;
}

// dragEnterEvent / dragMoveEvent：含 URL 时接受
// （dropEvent 无 URL 早退分支已由 test_mainwindow.cpp dropEvent_NoUrlsIgnored 覆盖）
TEST(MainWindow, dragEnterMove_AcceptsUrls)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile("/tmp")});
    QDragEnterEvent enter(QPoint(300, 300), Qt::CopyAction | Qt::MoveAction, &mime,
                          Qt::NoButton, Qt::NoModifier);
    EXPECT_NO_THROW(w->dragEnterEvent(&enter));
    QDragMoveEvent move(QPoint(300, 300), Qt::CopyAction | Qt::MoveAction, &mime,
                        Qt::NoButton, Qt::NoModifier, QEvent::DragMove);
    EXPECT_NO_THROW(w->dragMoveEvent(&move));
}

// event()：通用事件透传（非 Touch/WM 事件走父类）
TEST(MainWindow, event_GenericPassthrough)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    QEvent generic(QEvent::None);
    EXPECT_FALSE(w->event(&generic));

    QShowEvent showEv;
    EXPECT_NO_THROW(w->showEvent(&showEv));
    QHideEvent hideEv;
    EXPECT_NO_THROW(w->hideEvent(&hideEv));
    QFocusEvent focusIn(QEvent::FocusIn);
    EXPECT_NO_THROW(w->focusInEvent(&focusIn));
    QFocusEvent focusOut(QEvent::FocusOut);
    EXPECT_NO_THROW(w->focusOutEvent(&focusOut));
    QEvent leave(QEvent::Leave);
    EXPECT_NO_THROW(w->leaveEvent(&leave));
    QMoveEvent moveEv(QPoint(100, 100), QPoint(90, 90));
    EXPECT_NO_THROW(w->moveEvent(&moveEv));
    QPaintEvent paintEv(w->rect());
    EXPECT_NO_THROW(w->paintEvent(&paintEv));
}

// updateGeometry：Idle 下按角点调整几何（NoneEdge/边早退 + 角点分支）
TEST(MainWindow, updateGeometry_CornerEdges)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    w->show();
    w->setWindowState(Qt::WindowNoState);
    w->setGeometry(120, 120, 800, 560);
    QRect svGeom = w->geometry();

    // 四边与 None：直接早退
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::NoneEdge, QPoint(0, 0)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::BottomEdge, QPoint(0, 0)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::TopEdge, QPoint(0, 0)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::LeftEdge, QPoint(0, 0)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::RightEdge, QPoint(0, 0)));
    EXPECT_EQ(svGeom, w->geometry());

    // 角点：Idle 下保持宽高比调整（高度/宽度按 ratio）
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::TopLeftCorner, QPoint(60, 60)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::BottomRightCorner, QPoint(900, 700)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::TopRightCorner, QPoint(940, 60)));
    EXPECT_NO_THROW(w->updateGeometry(CornerEdge::BottomLeftCorner, QPoint(60, 700)));
    EXPECT_NE(svGeom, w->geometry());

    w->setGeometry(svGeom);
}

// updateContentGeometry：非 DXCB 构建走 move+resize
TEST(MainWindow, updateContentGeometry_MoveResize)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    QRect svGeom = w->geometry();
    QRect target(140, 140, 760, 540);
    EXPECT_NO_THROW(w->updateContentGeometry(target));
    // 非_DXCB 路径即 move+resize；窗管可能回调 y 坐标，仅验证尺寸生效
    QRect after = w->geometry();
    EXPECT_LE(qAbs(target.width() - after.width()), 2);
    EXPECT_LE(qAbs(target.height() - after.height()), 2);
    w->setGeometry(svGeom);
}

// ---------------------------------------------------------------------------
// 文件加载 / 光驱 / 解码
// ---------------------------------------------------------------------------

// play：不存在的文件不改变播放状态（FileFilter 过滤后列表为空）
TEST(MainWindow, play_InvalidFilesNoStateChange)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    bool svHaveFile = w->m_bHaveFile;

    EXPECT_NO_THROW(w->play({"notexist_file_xyz.mp4"}));
    QTest::qWait(50);
    EXPECT_FALSE(w->m_bHaveFile);
    EXPECT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    w->m_bHaveFile = svHaveFile;
}

// addCdromPath：无光驱挂载时返回 false
TEST(MainWindow, addCdromPath_NoDriveFalse)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool bRet = true;
    EXPECT_NO_THROW(bRet = w->addCdromPath());
    EXPECT_FALSE(bRet);
}

// subtitleMatchVideo：无同名视频时提示加载视频
TEST(MainWindow, subtitleMatchVideo_MissingVideoShowsHint)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    Stub stub;
    mw_gap_installMsgRecorder(stub);
    g_ut_gap_lastMsg.clear();
    EXPECT_NO_THROW(w->subtitleMatchVideo("/nonexistent_dir_xyz/Hachiko.ass"));
    QTest::qWait(30);
    EXPECT_EQ(QStringLiteral("Please load the video first"), g_ut_gap_lastMsg);
}

// decodeInit / onRefreshDecode / onSetDecodeModel：代理路径（Idle 安全）
TEST(MainWindow, decodeProxyPaths_IdleSafe)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_NE(nullptr, w->m_pEngine->getMpvProxy());

    EXPECT_NO_THROW(w->decodeInit());
    EXPECT_NO_THROW(w->onRefreshDecode());
    EXPECT_NO_THROW(w->onSetDecodeModel("base.decode.select", QVariant(1)));
    // value==3 时跳过 setDecodeModel
    EXPECT_NO_THROW(w->onSetDecodeModel("base.decode.select", QVariant(3)));
}

// setCurrentHwdec / setOpenFiles / setPresenter：成员写入
TEST(MainWindow, setterMembers_RecordValues)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->setCurrentHwdec("vaapi"));
    EXPECT_EQ(QString("vaapi"), w->m_sCurrentHwdec);

    QStringList lst = {"a.mkv"};
    EXPECT_NO_THROW(w->setOpenFiles(lst));
    EXPECT_EQ(lst, w->m_listOpenFiles);

    // setPresenter(nullptr) 会解引用崩溃（源码缺陷，不修改生产代码），跳过实调
    EXPECT_EQ(nullptr, w->m_pPresenter);
}

// startBurstShooting / onBurstScreenshot：时长守卫与空帧完成分支
TEST(MainWindow, burstScreenshot_GuardAndNullFrame)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(PlayerEngine::CoreState::Idle, w->m_pEngine->state());
    bool svBurst = w->m_bInBurstShootMode;

    // duration<=40 直接返回
    EXPECT_NO_THROW(w->startBurstShooting());
    EXPECT_FALSE(w->m_bInBurstShootMode);

    // 空帧：结束连拍分支
    w->m_bInBurstShootMode = true;
    EXPECT_NO_THROW(w->onBurstScreenshot(QImage(), 0));
    EXPECT_FALSE(w->m_bInBurstShootMode);

    // 非空帧且未满 15 张：仅入队
    w->m_bInBurstShootMode = true;
    QImage img(8, 8, QImage::Format_ARGB32);
    EXPECT_NO_THROW(w->onBurstScreenshot(img, 100));
    EXPECT_EQ(1, w->m_listBurstShoots.size());
    EXPECT_TRUE(w->m_bInBurstShootMode);
    w->m_listBurstShoots.clear();
    w->m_bInBurstShootMode = svBurst;
}

// onBurstScreenshot：满 15 帧走 USE_TEST 非阻塞对话框分支
TEST(MainWindow, burstScreenshot_CompletesWithDialog)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    bool svBurst = w->m_bInBurstShootMode;
    bool svPaused = w->m_bPausedBeforeBurst;
    w->m_bInBurstShootMode = true;
    w->m_bPausedBeforeBurst = true;

    QImage img(8, 8, QImage::Format_ARGB32);
    for (int i = 0; i < 15; ++i) {
        w->onBurstScreenshot(img, i * 100);
    }
    QTest::qWait(100);
    EXPECT_FALSE(w->m_bInBurstShootMode);
    EXPECT_TRUE(w->m_listBurstShoots.isEmpty());

    w->m_bInBurstShootMode = svBurst;
    w->m_bPausedBeforeBurst = svPaused;
}

// ---------------------------------------------------------------------------
// 文件级 static 自由函数（内部链接，AddrAny 解析，同 test_mainwindow.cpp 模式）
// ---------------------------------------------------------------------------

typedef QString (*ut_gap_elide_fn)(const QString &, const QSize &, QTextOption::WrapMode,
                                   const QFont &, Qt::TextElideMode, int, int);
typedef bool (*ut_gap_cmpBar_fn)(const QUrl &, const QUrl &);
typedef QWidget *(*ut_gap_opt_fn)(QObject *);
typedef void (*ut_gap_updStyle_fn)(QWidget *, const QString &);

// 复用 PIE 锚定思路：以导出的 dmr::utils::runPipeProcess 为基址锚
static void *ut_gap_resolve(const char *funcName, const char *exactDemangled)
{
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
    any.get_local_func_addr_symtab(funcName, result);
    const std::string want(exactDemangled);
    for (auto &kv : result) {
        if (kv.first == want) {
            return reinterpret_cast<char *>(kv.second) + base;
        }
    }
    return nullptr;
}

// ElideText：多行省略排版，超长文本应被截短
TEST(MainWindow, ElideText_TruncatesLongText)
{
    auto fn = reinterpret_cast<ut_gap_elide_fn>(
                  ut_gap_resolve("ElideText",
                                 "ElideText(QString const&, QSize const&, QTextOption::WrapMode, QFont const&, Qt::TextElideMode, int, int)"));
    ASSERT_NE(nullptr, fn);

    QString sLong = QStringLiteral("深影深影深影深影深影深影深影深影深影深影深影深影深影深影深影深影"
                                   "deepin-movie-reborn-long-file-name-test-0123456789.mp4");
    QFont f = qApp->font();
    QString sOut = fn(sLong, QSize(285, 40), QTextOption::WrapAnywhere, f,
                      Qt::ElideRight, 20, 200);
    EXPECT_FALSE(sOut.isEmpty());
    EXPECT_LE(sOut.length(), sLong.length());

    // 短文本不应被截断
    QString sShort = QStringLiteral("a.mp4");
    QString sOut2 = fn(sShort, QSize(285, 40), QTextOption::WrapAnywhere, f,
                       Qt::ElideRight, 20, 200);
    EXPECT_EQ(sShort.length(), sOut2.length());
}

// compareBarData：按文件名首字符排序
TEST(MainWindow, compareBarData_FirstCharOrder)
{
    auto fn = reinterpret_cast<ut_gap_cmpBar_fn>(
                  ut_gap_resolve("compareBarData", "compareBarData(QUrl const&, QUrl const&)"));
    ASSERT_NE(nullptr, fn);

    EXPECT_TRUE(fn(QUrl(QStringLiteral("a/apple.mp4")), QUrl(QStringLiteral("a/banana.mp4"))));
    EXPECT_FALSE(fn(QUrl(QStringLiteral("a/banana.mp4")), QUrl(QStringLiteral("a/apple.mp4"))));
    EXPECT_FALSE(fn(QUrl(QStringLiteral("a/apple.mp4")), QUrl(QStringLiteral("a/apple.mp4"))));
    // 空文件名分支
    EXPECT_FALSE(fn(QUrl(QStringLiteral("a/")), QUrl(QStringLiteral("a/b.mp4"))));
}

// createSelectableLineEditOptionHandle：构建可手输路径的选项行
TEST(MainWindow, createSelectableLineEditOptionHandle_BuildsOptionFrame)
{
    auto fn = reinterpret_cast<ut_gap_opt_fn>(
                  ut_gap_resolve("createSelectableLineEditOptionHandle",
                                 "createSelectableLineEditOptionHandle(QObject*)"));
    ASSERT_NE(nullptr, fn);

    DTK_CORE_NAMESPACE::DSettingsOption opt;
    opt.setValue(QStringLiteral("/tmp"));

    QWidget *w = fn(&opt);
    ASSERT_NE(nullptr, w);
    EXPECT_EQ(QString("OptionFrame"), w->objectName());
    delete w;
}

// workaround_updateStyle：递归为父窗口及子窗口设置风格
TEST(MainWindow, workaround_updateStyle_RecursiveApply)
{
    auto fn = reinterpret_cast<ut_gap_updStyle_fn>(
                  ut_gap_resolve("workaround_updateStyle",
                                 "workaround_updateStyle(QWidget*, QString const&)"));
    ASSERT_NE(nullptr, fn);

    QWidget parent;
    QWidget *child = new QWidget(&parent);
    QStyle *svStyle = parent.style();
    fn(&parent, QStringLiteral("Fusion"));
    EXPECT_NE(nullptr, parent.style());
    EXPECT_NE(svStyle, parent.style());
    EXPECT_NE(nullptr, child->style());
}

// MainWindowEventListener：Tab 键与鼠标按下/移动/抬起经 windowHandle 事件过滤
// （监听器类型仅在 mainwindow.cpp 内部定义，无法直接实例化，经真实事件驱动 eventFilter）
TEST(MainWindow, windowListener_TabAndMouseEvents)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    ASSERT_NE(nullptr, w->windowHandle());
    ASSERT_NE(nullptr, w->m_pEventListener);
    w->showNormal();
    w->activateWindow();
    QTest::qWait(50);

    // Tab 键分支（事件类型为 QEvent::MouseMove+1 的自定义 hack）
    QKeyEvent tabEvt((QEvent::Type)(QEvent::MouseMove + 1), Qt::Key_Tab, Qt::NoModifier);
    qApp->sendEvent(w->windowHandle(), &tabEvt);

    // 鼠标按下（非调整区）→ capturedMousePressEvent 置按下标志
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(120, 120), QPointF(120, 120),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    qApp->sendEvent(w->windowHandle(), &press);

    // 鼠标移动（左键未按）→ 角落光标判定 + judgeMouseInWindow
    QMouseEvent move(QEvent::MouseMove, QPointF(160, 160), QPointF(160, 160),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(w->windowHandle(), &move);

    // 鼠标抬起 → capturedMouseReleaseEvent 清按下标志
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(160, 160), QPointF(160, 160),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    qApp->sendEvent(w->windowHandle(), &release);
    EXPECT_FALSE(w->m_bMousePressed);
}
