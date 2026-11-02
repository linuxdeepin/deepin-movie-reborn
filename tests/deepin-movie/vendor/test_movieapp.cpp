// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// T19 Mode2: MovieApp (src/vendor/movieapp.cpp) 重写 —— deepin-movie 主线二进制
//
// 用例数: 6   覆盖方法: 5/5 (ctor×2, initMpris, show, quit)
//
// 分支清单 → 用例映射 (源码以工作目录 src/vendor/movieapp.cpp 为准):
//   ctor(MainWindow*): 构建 Presenter + initMpris
//                                            → ctor_mainMainWindow_buildsPresenter
//   ctor(Platform_MainWindow*): 平台构建 + quit 平台转发
//                                            → ctor_platformMainWindow_quitForwardsExit
//   initMpris: MprisPlayer 配置 + quitRequested→quit 连接
//                                            → initMpris_wiresQuitRequestedToQuit
//   show: 空实现                              → show_isNoop
//   quit: _mw → requestAction(Exit)          → quit_forwardsExitToMainWindow
//   quit: 双空 → else 分支 (无动作, 不崩溃)   → quit_noWindows_noCrash
//
// 桩策略:
//   * MainWindow / Platform_MainWindow::requestAction 经 stub_ext 捕获 ——
//     真实 requestAction 依赖动画状态机, 不属于本单元。
//   * quitRequested→quit 的信号接线经公开信号 emit 驱动 (Qt 信号为 public)。
//   * 主线用例复用测试进程共享 MainWindow (dApp->getMainWindow()), 平台用例
//     栈构造 Platform_MainWindow (与 test_platform_dbusadaptor.cpp 同法)。
//
// 已记录缺陷 (只标红不修改, 详见 defects.json):
//   * MovieApp(nullptr) → new Presenter(nullptr) → Presenter::initMpris else
//     分支解引用 _platform_mw->engine() 空指针崩溃 (缺少双空守卫)。
//   * MovieApp::initMpris 每次调用 new MprisPlayer() 无 parent, 且 Presenter
//     构造时已创建过一个是实例, 旧实例无 parent 无释放 → 内存泄漏。

#include <QtTest>
#include <QTest>
#include <MprisPlayer>
#include <gtest/gtest.h>

// 暴露 MovieApp 私有成员 (_presenter / _mw / _mw_platform) 供断言使用。
// Qt/dtk/MprisQt 头文件已在上方正常解析; 仅项目头文件受宏影响。
#define protected public
#define private public
#include "movieapp.h"
#undef protected
#undef private

#include "application.h"
#include "stub/stubext.h"

using namespace dmr;

namespace {

struct ActionCap
{
    int count = 0;
    ActionFactory::ActionKind kind = static_cast<ActionFactory::ActionKind>(-1);
};

static ActionCap g_mwAct;
static ActionCap g_platAct;

static void resetCaptures()
{
    g_mwAct = ActionCap{};
    g_platAct = ActionCap{};
}

static void installMovieAppStubs(stub_ext::StubExt &stub)
{
    stub.set_lamda(&MainWindow::requestAction,
                   [](MainWindow *, ActionFactory::ActionKind kind, bool,
                      QList<QVariant>, bool) {
                       g_mwAct.count++;
                       g_mwAct.kind = kind;
                   });
    stub.set_lamda(&Platform_MainWindow::requestAction,
                   [](Platform_MainWindow *, ActionFactory::ActionKind kind, bool,
                      QList<QVariant>, bool) {
                       g_platAct.count++;
                       g_platAct.kind = kind;
                   });
}

}  // namespace

class MovieAppTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        resetCaptures();
        installMovieAppStubs(m_stub);
    }

    void TearDown() override { m_stub.clear(); }

    stub_ext::StubExt m_stub;
};

// --- ctor(MainWindow*) --------------------------------------------------------

TEST_F(MovieAppTest, ctor_mainMainWindow_buildsPresenter)
{
    // Arrange
    MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);

    // Act
    MovieApp ma(mw);

    // Assert: Presenter 已构建并完成 Mpris 初始化
    EXPECT_EQ(ma._mw, mw);
    EXPECT_EQ(ma._mw_platform, nullptr);
    ASSERT_NE(ma._presenter, nullptr);
    EXPECT_EQ(ma._presenter->_mw, mw);
    ASSERT_NE(ma._presenter->m_mprisplayer, nullptr);
    EXPECT_EQ(ma._presenter->m_mprisplayer->identity(),
              QStringLiteral("Deepin Movie Player"));
}

// --- initMpris ------------------------------------------------------------------

TEST_F(MovieAppTest, initMpris_wiresQuitRequestedToQuit)
{
    // Arrange
    MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);
    MovieApp ma(mw);
    MprisPlayer *mpris = ma._presenter->m_mprisplayer;
    ASSERT_NE(mpris, nullptr);

    // Act: MprisPlayer 发出 quitRequested → MovieApp::quit → requestAction(Exit)
    emit mpris->quitRequested();

    // Assert
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::Exit);
    EXPECT_EQ(g_platAct.count, 0);
}

// --- quit ------------------------------------------------------------------------

TEST_F(MovieAppTest, quit_forwardsExitToMainWindow)
{
    // Arrange
    MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);
    MovieApp ma(mw);

    // Act
    ma.quit();

    // Assert
    ASSERT_EQ(g_mwAct.count, 1);
    EXPECT_EQ(g_mwAct.kind, ActionFactory::Exit);
}

TEST_F(MovieAppTest, quit_noWindows_noCrash)
{
    // Arrange: 双空窗口走 else 分支 (构造需有效窗口, 构造后置空)
    MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);
    MovieApp ma(mw);
    ma._mw = nullptr;
    ma._mw_platform = nullptr;

    // Act: 不崩溃、不产生任何动作
    ma.quit();

    // Assert
    EXPECT_EQ(g_mwAct.count, 0);
    EXPECT_EQ(g_platAct.count, 0);
}

// --- show --------------------------------------------------------------------------

TEST_F(MovieAppTest, show_isNoop)
{
    // Arrange
    MainWindow *mw = dApp->getMainWindow();
    ASSERT_NE(mw, nullptr);
    MovieApp ma(mw);

    // Act + Assert: 空实现, 调用即可
    ma.show();
    SUCCEED();
}

// --- ctor(Platform_MainWindow*) ------------------------------------------------------

TEST_F(MovieAppTest, ctor_platformMainWindow_quitForwardsExit)
{
    // Arrange: 栈构造平台主窗口 (与 test_platform_dbusadaptor.cpp 同法)
    Platform_MainWindow pw;
    MovieApp ma(&pw);

    // 平台窗口构造/异步播放列表初始化会内部触发 requestAction, 断言前归零
    resetCaptures();

    // Assert: 平台指针已绑定, Presenter 构建在平台窗口上
    EXPECT_EQ(ma._mw_platform, &pw);
    EXPECT_EQ(ma._mw, nullptr);
    ASSERT_NE(ma._presenter, nullptr);
    EXPECT_EQ(ma._presenter->_platform_mw, &pw);

    // Act: quit → 平台 requestAction(Exit)
    ma.quit();

    // Assert
    ASSERT_EQ(g_platAct.count, 1);
    EXPECT_EQ(g_platAct.kind, ActionFactory::Exit);
    EXPECT_EQ(g_mwAct.count, 0);
}
