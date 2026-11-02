// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// T19 Mode2: Presenter (src/vendor/presenter.cpp) 重写 —— deepin-movie 主线二进制
//
// 用例数: 14   覆盖方法: 14/14 (ctor×2, initMpris, slotplay, slotpause,
//   slotplaynext, slotplayprev, slotvolumeRequested, slotopenUrlRequested,
//   slotstateChanged, slotloopStatusRequested, slotplayModeChanged,
//   slotvolumeChanged, slotseek, slotstop)
//
// 分支清单 → 用例映射 (源码以工作目录 src/vendor/presenter.cpp 为准):
//   ctor(MainWindow*): MprisPlayer 能力属性 + initMpris 主线连接
//                                              → ctor_mainMainWindow_wiresMprisCapabilities
//   ctor(Platform_MainWindow*): 平台窗口全分支转发
//                                              → platformWindow_allSlotsForwardToPlatformMainWindow
//   initMpris: null 玩家早退                   → initMpris_nullPlayer_keepsExistingPlayer
//   slotplay: Stopped→StartPlay               → slotplay_whenIdle_forwardsStartPlay
//   slotplay: Paused→slotpause                → slotplay_whenPaused_forwardsTogglePause
//   slotpause: TogglePause                    → slotpause_forwardsTogglePause
//   slotplaynext/prev: GotoPlaylistNext/Prev  → slotplaynext_and_prev_forwardGotoPlaylist
//   slotvolumeRequested: (v+0.001)*100 → ChangeVolume
//                                              → slotvolumeRequested_convertsToPercent
//   slotopenUrlRequested: play({url})         → slotopenUrlRequested_forwardsPlayUrl
//   slotstateChanged: Idle/Playing/Paused → Stopped/Playing/Paused
//                                              → slotstateChanged_mapsEngineStateToMpris
//   slotloopStatusRequested: None/Track/Playlist → OrderPlay/SingleLoop/ListLoop
//     (+reflectActionToUI), InvalidLoopStatus 早退
//                                              → slotloopStatusRequested_mapsAndReflects
//   slotplayModeChanged: OrderPlay/SingleLoop/ListLoop/ShufflePlay
//     → None/Track/Playlist/InvalidLoopStatus → slotplayModeChanged_mapsPlayModeToLoopStatus
//   slotvolumeChanged: muted→0.0 / getDisplayVolume
//                                              → slotvolumeChanged_mutedAndDisplayVolume
//   slotseek/slotstop: engine seekAbsolute/stop
//                                              → slotseek_and_slotstop_forwardToEngine
//
// 桩策略:
//   * MainWindow / Platform_MainWindow 的 requestAction / play /
//     reflectActionToUI / getDisplayVolume 经 stub_ext 捕获替换 —— Presenter 是
//     动作转发层, 真实 requestAction 依赖动画状态机与 isActionAllowed, 不属于
//     本单元的被测逻辑。
//   * PlayerEngine::state / muted / seekAbsolute / stop 经 stub_ext 控制/捕获
//     —— 真实引擎在 Idle 态下不产生状态迁移, 无法覆盖 Playing/Paused 分支。
//   * MprisPlayer 状态经公开 API setPlaybackStatus / setLoopStatus 驱动, 经
//     playbackStatus() / loopStatus() / volume() 断言, 不打桩。
//   * 主线用例复用测试进程共享 MainWindow (dApp->getMainWindow()), 平台用例
//     栈构造 Platform_MainWindow (与 test_platform_dbusadaptor.cpp 同法)。
//
// 已记录缺陷 (只标红不修改, 详见 defects.json):
//   * Presenter 构造与 MovieApp::initMpris 各 new 一个 MprisPlayer 且均无 parent,
//     Presenter 无析构释放 m_mprisplayer, MovieApp 重建后旧实例泄漏 (内存泄漏)。
//   * Presenter(nullptr) / MovieApp(nullptr) 经 initMpris else 分支解引用
//     _platform_mw->engine() 空指针崩溃 (缺少双空守卫)。

#include <QtTest>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <MprisPlayer>
#include <gtest/gtest.h>

// 暴露 Presenter 私有成员 (m_mprisplayer / _mw / _platform_mw) 供断言使用。
// Qt/dtk/MprisQt 头文件已在上方正常解析; 仅项目头文件受宏影响。
// (与 test_toolbox_proxy.cpp / test_mainwindow.cpp 同模式)
#define protected public
#define private public
#include "presenter.h"
#undef protected
#undef private

#include "application.h"
#include "libdmr/playlist_model.h"
#include "stub/stubext.h"

using namespace dmr;

namespace {

struct ActionCap
{
    int count = 0;
    ActionFactory::ActionKind kind = static_cast<ActionFactory::ActionKind>(-1);
    bool fromUI = false;
    QList<QVariant> args;
    bool isShortcut = false;
};

static ActionCap g_mwAct;
static ActionCap g_platAct;
static QList<QList<QString>> g_mwPlay;
static QList<QList<QString>> g_platPlay;
static QList<ActionFactory::ActionKind> g_mwReflect;
static QList<ActionFactory::ActionKind> g_platReflect;
static QList<int> g_seekPos;
static int g_stopCount = 0;
static bool g_muted = false;
static int g_displayVolume = 0;
static PlayerEngine::CoreState g_engineState = PlayerEngine::CoreState::Idle;

static void resetCaptures()
{
    g_mwAct = ActionCap{};
    g_platAct = ActionCap{};
    g_mwPlay.clear();
    g_platPlay.clear();
    g_mwReflect.clear();
    g_platReflect.clear();
    g_seekPos.clear();
    g_stopCount = 0;
    g_muted = false;
    g_displayVolume = 0;
    g_engineState = PlayerEngine::CoreState::Idle;
}

static void installPresenterStubs(stub_ext::StubExt &stub)
{
    // MainWindow 动作转发捕获
    stub.set_lamda(&MainWindow::requestAction,
                   [](MainWindow *, ActionFactory::ActionKind kind, bool fromUI,
                      QList<QVariant> args, bool isShortcut) {
                       g_mwAct.count++;
                       g_mwAct.kind = kind;
                       g_mwAct.fromUI = fromUI;
                       g_mwAct.args = args;
                       g_mwAct.isShortcut = isShortcut;
                   });
    stub.set_lamda(&MainWindow::play,
                   [](MainWindow *, const QList<QString> &files) {
                       g_mwPlay << files;
                   });
    stub.set_lamda(&MainWindow::reflectActionToUI,
                   [](MainWindow *, ActionFactory::ActionKind kind) {
                       g_mwReflect << kind;
                   });
    stub.set_lamda(&MainWindow::getDisplayVolume,
                   [](MainWindow *) -> int { return g_displayVolume; });

    // Platform_MainWindow 动作转发捕获
    stub.set_lamda(&Platform_MainWindow::requestAction,
                   [](Platform_MainWindow *, ActionFactory::ActionKind kind, bool fromUI,
                      QList<QVariant> args, bool isShortcut) {
                       g_platAct.count++;
                       g_platAct.kind = kind;
                       g_platAct.fromUI = fromUI;
                       g_platAct.args = args;
                       g_platAct.isShortcut = isShortcut;
                   });
    stub.set_lamda(&Platform_MainWindow::play,
                   [](Platform_MainWindow *, const QList<QString> &files) {
                       g_platPlay << files;
                   });
    stub.set_lamda(&Platform_MainWindow::reflectActionToUI,
                   [](Platform_MainWindow *, ActionFactory::ActionKind kind) {
                       g_platReflect << kind;
                   });
    stub.set_lamda(&Platform_MainWindow::getDisplayVolume,
                   [](Platform_MainWindow *) -> int { return g_displayVolume; });

    // 引擎状态控制/捕获 (真实引擎 Idle 态无法覆盖 Playing/Paused 分支)
    stub.set_lamda(&PlayerEngine::state,
                   [](PlayerEngine *) -> PlayerEngine::CoreState { return g_engineState; });
    stub.set_lamda(&PlayerEngine::muted,
                   [](const PlayerEngine *) -> bool { return g_muted; });
    stub.set_lamda(&PlayerEngine::seekAbsolute,
                   [](PlayerEngine *, int pos) { g_seekPos << pos; });
    stub.set_lamda(&PlayerEngine::stop,
                   [](PlayerEngine *) { g_stopCount++; });
}

// 主线用例共享的测试进程主窗口 (test_qtestmain 启动时已创建)
static MainWindow *utMainWindow()
{
    MainWindow *w = dApp->getMainWindow();
    return w;
}

}  // namespace

class PresenterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        resetCaptures();
        installPresenterStubs(m_stub);
    }

    void TearDown() override { m_stub.clear(); }

    stub_ext::StubExt m_stub;
};

// --- ctor(MainWindow*) ------------------------------------------------------

TEST_F(PresenterTest, ctor_mainMainWindow_wiresMprisCapabilities)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);

    // Act
    Presenter p(mw);

    // Assert: MprisPlayer 已创建并配置能力属性
    EXPECT_NE(p.m_mprisplayer, nullptr);
    EXPECT_EQ(p.m_mprisplayer->identity(), QStringLiteral("Deepin Movie Player"));
    EXPECT_EQ(p.m_mprisplayer->desktopEntry(), QStringLiteral("deepin-movie"));
    EXPECT_EQ(p.m_mprisplayer->serviceName(), QStringLiteral("Deepinmovie"));
    EXPECT_TRUE(p.m_mprisplayer->canQuit());
    EXPECT_TRUE(p.m_mprisplayer->canPlay());
    EXPECT_TRUE(p.m_mprisplayer->canControl());
    EXPECT_EQ(p.m_mprisplayer->supportedUriSchemes(), QStringList{QStringLiteral("file")});
    // Assert: 主线窗口指针与引擎/播放列表信号连接均基于 _mw
    EXPECT_EQ(p._mw, mw);
    EXPECT_EQ(p._platform_mw, nullptr);
}

// --- initMpris --------------------------------------------------------------

TEST_F(PresenterTest, initMpris_nullPlayer_keepsExistingPlayer)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);
    MprisPlayer *before = p.m_mprisplayer;
    ASSERT_NE(before, nullptr);

    // Act: null 玩家应早退, 不替换已有连接
    p.initMpris(nullptr);

    // Assert
    EXPECT_EQ(p.m_mprisplayer, before);
}

// --- slotplay ---------------------------------------------------------------

TEST_F(PresenterTest, slotplay_whenIdle_forwardsStartPlay)
{
    // Arrange: 默认 Mpris::Stopped (非 Paused) → StartPlay
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act
    p.slotplay();

    // Assert
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::StartPlay);
    EXPECT_EQ(g_platAct.count, 0);
}

TEST_F(PresenterTest, slotplay_whenPaused_forwardsTogglePause)
{
    // Arrange: Mpris 状态 Paused → 走 slotpause 分支
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);
    p.m_mprisplayer->setPlaybackStatus(Mpris::Paused);

    // Act
    p.slotplay();

    // Assert
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::TogglePause);
}

// --- slotpause --------------------------------------------------------------

TEST_F(PresenterTest, slotpause_forwardsTogglePause)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act
    p.slotpause();

    // Assert
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::TogglePause);
}

// --- slotplaynext / slotplayprev ---------------------------------------------

TEST_F(PresenterTest, slotplaynext_and_prev_forwardGotoPlaylist)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert
    p.slotplaynext();
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::GotoPlaylistNext);

    p.slotplayprev();
    ASSERT_EQ(g_mwAct.count, 2);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::GotoPlaylistPrev);
}

// --- slotvolumeRequested ------------------------------------------------------

TEST_F(PresenterTest, slotvolumeRequested_convertsToPercent)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act
    p.slotvolumeRequested(0.5);

    // Assert: (0.5 + 0.001) * 100.0, bFromUI=true
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::ChangeVolume);
    EXPECT_TRUE(g_mwAct.fromUI);
    ASSERT_EQ(g_mwAct.args.size(), 1);
    EXPECT_EQ(g_mwAct.args.at(0).toDouble(), (0.5 + 0.001) * 100.0);

    // Act: 负值同样按公式转发 (源码无下限钳制)
    p.slotvolumeRequested(-1.0);
    ASSERT_EQ(g_mwAct.count, 2);
    EXPECT_EQ(g_mwAct.args.at(0).toDouble(), (-1.0 + 0.001) * 100.0);
}

// --- slotopenUrlRequested -----------------------------------------------------

TEST_F(PresenterTest, slotopenUrlRequested_forwardsPlayUrl)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act
    p.slotopenUrlRequested(QUrl::fromLocalFile(QStringLiteral("/tmp/ut_t19_pres.mp4")));

    // Assert: URL 字符串直通 MainWindow::play
    ASSERT_EQ(g_mwPlay.size(), 1);
    EXPECT_EQ(g_mwPlay.at(0),
              QList<QString>{QUrl::fromLocalFile(QStringLiteral("/tmp/ut_t19_pres.mp4")).toString()});
}

// --- slotstateChanged ---------------------------------------------------------

TEST_F(PresenterTest, slotstateChanged_mapsEngineStateToMpris)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert: Idle → Stopped
    g_engineState = PlayerEngine::CoreState::Idle;
    p.slotstateChanged();
    EXPECT_EQ(p.m_mprisplayer->playbackStatus(), Mpris::Stopped);

    // Act + Assert: Playing → Playing
    g_engineState = PlayerEngine::CoreState::Playing;
    p.slotstateChanged();
    EXPECT_EQ(p.m_mprisplayer->playbackStatus(), Mpris::Playing);

    // Act + Assert: Paused → Paused
    g_engineState = PlayerEngine::CoreState::Paused;
    p.slotstateChanged();
    EXPECT_EQ(p.m_mprisplayer->playbackStatus(), Mpris::Paused);
}

// --- slotloopStatusRequested ---------------------------------------------------

TEST_F(PresenterTest, slotloopStatusRequested_mapsAndReflects)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert: None → OrderPlay + UI 回写
    p.slotloopStatusRequested(Mpris::LoopStatus::None);
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::OrderPlay);
    ASSERT_EQ(g_mwReflect.size(), 1);
    EXPECT_EQ(g_mwReflect.at(0), ActionFactory::OrderPlay);

    // Act + Assert: Track → SingleLoop
    p.slotloopStatusRequested(Mpris::LoopStatus::Track);
    ASSERT_EQ(g_mwAct.count, 2);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::SingleLoop);
    EXPECT_EQ(g_mwReflect.at(1), ActionFactory::SingleLoop);

    // Act + Assert: Playlist → ListLoop
    p.slotloopStatusRequested(Mpris::LoopStatus::Playlist);
    ASSERT_EQ(g_mwAct.count, 3);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::ListLoop);
    EXPECT_EQ(g_mwReflect.at(2), ActionFactory::ListLoop);

    // Act + Assert: InvalidLoopStatus 早退, 不触发动作
    p.slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);
    EXPECT_EQ(g_mwAct.count, 3);
    EXPECT_EQ(g_mwReflect.size(), 3);
}

// --- slotplayModeChanged --------------------------------------------------------

TEST_F(PresenterTest, slotplayModeChanged_mapsPlayModeToLoopStatus)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert: 四种播放模式到 Mpris 循环状态的映射
    p.slotplayModeChanged(PlaylistModel::PlayMode::OrderPlay);
    EXPECT_EQ(p.m_mprisplayer->loopStatus(), Mpris::LoopStatus::None);

    p.slotplayModeChanged(PlaylistModel::PlayMode::SingleLoop);
    EXPECT_EQ(p.m_mprisplayer->loopStatus(), Mpris::LoopStatus::Track);

    p.slotplayModeChanged(PlaylistModel::PlayMode::ListLoop);
    EXPECT_EQ(p.m_mprisplayer->loopStatus(), Mpris::LoopStatus::Playlist);

    // ShufflePlay 等其余模式落入 else → InvalidLoopStatus
    p.slotplayModeChanged(PlaylistModel::PlayMode::ShufflePlay);
    EXPECT_EQ(p.m_mprisplayer->loopStatus(), Mpris::LoopStatus::InvalidLoopStatus);
}

// --- slotvolumeChanged -----------------------------------------------------------

TEST_F(PresenterTest, slotvolumeChanged_mutedAndDisplayVolume)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert: 静音 → Mpris 音量 0.0
    g_muted = true;
    p.slotvolumeChanged();
    EXPECT_EQ(p.m_mprisplayer->volume(), 0.0);

    // Act + Assert: 非静音 → getDisplayVolume 原样写入
    g_muted = false;
    g_displayVolume = 75;
    p.slotvolumeChanged();
    EXPECT_EQ(p.m_mprisplayer->volume(), 75.0);
}

// --- slotseek / slotstop -----------------------------------------------------------

TEST_F(PresenterTest, slotseek_and_slotstop_forwardToEngine)
{
    // Arrange
    MainWindow *mw = utMainWindow();
    ASSERT_NE(mw, nullptr);
    Presenter p(mw);

    // Act + Assert: seek 转发到引擎
    p.slotseek(static_cast<qlonglong>(1234));
    ASSERT_EQ(g_seekPos.size(), 1);
    EXPECT_EQ(g_seekPos.at(0), 1234);

    // Act + Assert: stop 转发到引擎
    p.slotstop();
    EXPECT_EQ(g_stopCount, 1);
}

// --- 平台侧分支 (ctor(Platform_MainWindow*) + 全部 slot 的平台转发) ------------------

TEST_F(PresenterTest, platformWindow_allSlotsForwardToPlatformMainWindow)
{
    // Arrange: 栈构造平台主窗口 (与 test_platform_dbusadaptor.cpp 同法)
    Platform_MainWindow pw;
    Presenter p(&pw);

    // 平台窗口构造/异步播放列表初始化会内部触发 requestAction, 断言前归零
    resetCaptures();

    // Assert: 平台指针已绑定
    EXPECT_EQ(p._platform_mw, &pw);
    EXPECT_EQ(p._mw, nullptr);
    EXPECT_NE(p.m_mprisplayer, nullptr);

    // Act + Assert: slotplay → StartPlay (平台)
    p.slotplay();
    ASSERT_EQ(g_platAct.count, 1);
    EXPECT_EQ(g_platAct.kind, ActionFactory::StartPlay);

    // Act + Assert: slotpause → TogglePause (平台)
    p.slotpause();
    ASSERT_EQ(g_platAct.count, 2);
    EXPECT_EQ(g_platAct.kind, ActionFactory::TogglePause);

    // Act + Assert: slotplaynext/prev (平台)
    p.slotplaynext();
    ASSERT_EQ(g_platAct.count, 3);
    EXPECT_EQ(g_platAct.kind, ActionFactory::GotoPlaylistNext);
    p.slotplayprev();
    ASSERT_EQ(g_platAct.count, 4);
    EXPECT_EQ(g_platAct.kind, ActionFactory::GotoPlaylistPrev);

    // Act + Assert: slotvolumeRequested (平台)
    p.slotvolumeRequested(0.3);
    ASSERT_EQ(g_platAct.count, 5);
    EXPECT_EQ(g_platAct.kind, ActionFactory::ChangeVolume);
    EXPECT_EQ(g_platAct.args.at(0).toDouble(), (0.3 + 0.001) * 100.0);

    // Act + Assert: slotopenUrlRequested (平台)
    p.slotopenUrlRequested(QUrl::fromLocalFile(QStringLiteral("/tmp/ut_t19_plat.mp4")));
    ASSERT_EQ(g_platPlay.size(), 1);
    EXPECT_EQ(g_platPlay.at(0),
              QList<QString>{QUrl::fromLocalFile(QStringLiteral("/tmp/ut_t19_plat.mp4")).toString()});

    // Act + Assert: slotstateChanged (平台, Idle → Stopped)
    p.slotstateChanged();
    EXPECT_EQ(p.m_mprisplayer->playbackStatus(), Mpris::Stopped);

    // Act + Assert: slotloopStatusRequested (平台, Track → SingleLoop + 回写)
    p.slotloopStatusRequested(Mpris::LoopStatus::Track);
    ASSERT_EQ(g_platAct.count, 6);
    EXPECT_EQ(g_platAct.kind, ActionFactory::SingleLoop);
    ASSERT_EQ(g_platReflect.size(), 1);
    EXPECT_EQ(g_platReflect.at(0), ActionFactory::SingleLoop);

    // Act + Assert: slotloopStatusRequested InvalidLoopStatus 早退 (平台)
    p.slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);
    EXPECT_EQ(g_platAct.count, 6);

    // Act + Assert: slotvolumeChanged (平台, 非静音 → 显示音量)
    g_displayVolume = 60;
    p.slotvolumeChanged();
    EXPECT_EQ(p.m_mprisplayer->volume(), 60.0);

    // Act + Assert: slotseek / slotstop (平台)
    p.slotseek(static_cast<qlonglong>(77));
    ASSERT_EQ(g_seekPos.size(), 1);
    EXPECT_EQ(g_seekPos.at(0), 77);
    p.slotstop();
    EXPECT_EQ(g_stopCount, 1);
}
