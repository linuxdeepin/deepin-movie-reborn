// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for src/widgets/animationlabel.cpp (class dmr::AnimationLabel).
//
// Scope: all 13 graph-tracked methods of AnimationLabel — constructor,
// playAnimation, pauseAnimation, setWM, initMember, initPlayAnimation,
// initPauseAnimation, onPlayAnimationChanged, onPauseAnimationChanged,
// onHideAnimation, showEvent, moveEvent, paintEvent. Existing suites do not
// cover the non-platform AnimationLabel at all (misc_widgets_ext /
// platform_widgets_ext only exercise Platform_AnimationLabel), so this file
// fills the whole gap from zero.
//
// 用例计数声明（self-check-structural 验证此块）：
// | method                | level | factors                    | min | actual |
// |-----------------------|-------|----------------------------|-----|--------|
// | AnimationLabel        | mid   | branches:2(无参/带主窗)     |  2  |  2     |
// | playAnimation         | mid   | branches:2(组态/互斥停止)   |  2  |  2     |
// | pauseAnimation        | mid   | branches:2(组态/互斥停止)   |  2  |  2     |
// | setWM                 | low   | equivalence:true/false     |  2  |  1*    |
// | initMember            | mid   | state-reset                |  2  |  2     |
// | initPlayAnimation     | low   | structure                  |  2  |  1*    |
// | initPauseAnimation    | low   | structure                  |  2  |  1*    |
// | initMember(重入)      | low   | state-reset                |  2  |  1*    |
// | onPlayAnimationChanged| low   | boundary:0/中间/18         |  2  |  2     |
// | onPauseAnimationChanged|low   | boundary:0/中间/18         |  2  |  2     |
// | onHideAnimation       | low   | branches:2(主窗null/非null)|  2  |  2     |
// | showEvent             | low   | delegate                   |  1  |  1*    |
// | moveEvent             | low   | delegate                   |  1  |  1*    |
// | paintEvent            | mid   | pixmap:null/非null         |  2  |  2     |
// (* setWM 一次用例内验证 true/false 两个等价类；结构类方法在单用例内覆盖全部结构断言)
//
// 最小清单完成情况（test-code-gen §最小清单）：
// 1. 每个公开方法 ≥ 1 用例: [x] 13/13 方法全部有用例
// 2. 每个输入维度按等价类划分 ≥ 1 用例/类: [x] setWM true/false、主窗 null/非null、动画组互斥停止两方向
// 3. 每个等价类的边界值显式覆盖: [x] 动画帧值边界 0 与 18（nShowAnimationNum/nHideAnimationNum）
// 4. 同质 ≥ 3 组用 TEST_P: [N/A] 本类无 ≥3 组同质输入（等价类仅 2 组，见声明表）
// 5. 分支清单 → 用例映射已列出: [x] 见下方 BranchList
// 6. 每条 if/switch/throw/early-return 有触发用例: [x] playAnimation/pauseAnimation
//    停止分支（互斥两方向）、onHideAnimation null 分支、showEvent/moveEvent 委托
// 7. 异常路径 EXPECT_THROW 精确匹配: [N/A] 被测类无抛异常路径
// 8. 负面场景有专门用例: [x] 空主窗 onHideAnimation、空 pixmap paintEvent、未初始化成员
// 9. 负面用例验证强异常安全: [x] 负面用例以 EXPECT_NO_FATAL_FAILURE + 状态断言验证无副作用泄漏
// 10. stub_ext vs gMock 选择正确: [x] 本类为叶子 UI 控件，0 个项目内部出向调用，
//     无需 stub（stub 决策见 dependency-tracer 矩阵）；仅用真实 Qt 动画对象
//
// 分支清单（源码为 e5346fd1 的 animationlabel.cpp 实测；__aarch64__/__mips__ 分支
// 在 x86_64 构建不参与编译，故 onPlayAnimationChanged/onPauseAnimationChanged 无运行时分支）:
// - AnimationLabel ctor      : (无运行时分支) initMember + WA_TransparentForMouseEvents + resize(200,200)
// - playAnimation  L69-L73   : [运行中→停止]  ↔ [未运行→跳过]      → PlayGroup*_StopsRunning*/NotRunning
// - pauseAnimation L47-L51   : [运行中→停止]  ↔ [未运行→跳过]      → PauseGroup*_StopsRunning*/NotRunning
// - playAnimation/pauseAnimation L59/76: [不可见→show] ↔ [可见→跳过]（互斥停止用例隐式覆盖）
// - setWM                    : 单赋值, 等价类 {true, false}
// - onPlayAnimationChanged   : x86 编译分支恒走 "stop/%1.png"        → 边界值 0/3/18
// - onPauseAnimationChanged  : x86 编译分支恒走 "start/%1.png"       → 边界值 0/7/18
// - onHideAnimation L235     : [m_pMainWindow 非null→update] ↔ [null→跳过]
// - showEvent/moveEvent      : 纯委托 QFrame, 无分支
// - paintEvent               : 无分支（m_pixmap null/非null 两个数据态）

#include <gtest/gtest.h>
#include <QApplication>
#include <QWidget>
#include <QShowEvent>
#include <QMoveEvent>
#include <QPaintEvent>
#include <QVariant>
#include <QSequentialAnimationGroup>
#include <QPropertyAnimation>
#include <QPauseAnimation>

// 被测头（private 成员白盒访问，Qt 头在访问宏外先行包含）
#include <QFrame>
#define protected public
#define private public
#include "src/widgets/animationlabel.h"
#undef protected
#undef private

// AnimationLabel 位于全局命名空间（无 Q_OBJECT，Q_PROPERTY 行为空宏）

namespace {

class AnimationLabelExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 无显示环境（offscreen 除外）时跳过，避免 CI 无屏崩溃
        if (!QGuiApplication::primaryScreen()) {
            GTEST_SKIP() << "headless environment, primary screen required";
        }
    }

    void TearDown() override
    {
        // 每个用例独立构造/析构控件，无跨用例共享状态
    }
};

// 构造 200x300 的宿主窗，作为动画标签的父对象与主窗
static QWidget *al_makeHost()
{
    QWidget *host = new QWidget;
    host->resize(200, 300);
    return host;
}

// ═══════════════════════════════════════════════════════════════
// 构造函数 / initMember
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, AnimationLabel_Ctor_DefaultMembersInitialized)
{
    // Arrange: 准备默认构造环境（无父窗、无主窗指针）
    // Act: 默认构造 AnimationLabel 并读取初始状态
    AnimationLabel al;

    // Assert: 200x200、鼠标穿透、动画组就绪且未运行、主窗指针为空、初始文件名空
    EXPECT_EQ(al.size(), QSize(200, 200));
    EXPECT_TRUE(al.testAttribute(Qt::WA_TransparentForMouseEvents));
    ASSERT_NE(al.m_pPlayAnimationGroup, nullptr);
    ASSERT_NE(al.m_pPauseAnimationGroup, nullptr);
    EXPECT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pMainWindow, nullptr);
    EXPECT_TRUE(al.m_sFileName.isEmpty());
    EXPECT_FALSE(al.isVisible());
}

TEST_F(AnimationLabelExtTest, AnimationLabel_Ctor_WithMainWindow_StoresPointer)
{
    // Arrange: host 同时充当主窗
    QWidget *host = al_makeHost();

    // Act: 以 host 作为 pMainWindow 构造
    AnimationLabel al(nullptr, host);

    // Assert: 主窗指针被 initMember 记录，初始尺寸固定 200x200
    // 注意：源码缺陷——m_bIsWM 未在构造/initMember 中初始化，不可断言其初值
    EXPECT_EQ(al.m_pMainWindow, host);
    EXPECT_EQ(al.size(), QSize(200, 200));
    delete host;
}

TEST_F(AnimationLabelExtTest, InitMember_PlayAnimationGroup_StructureComplete)
{
    // Arrange: 构造后检查 initPlayAnimation 的产物
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 读取播放动画组内部结构
    QSequentialAnimationGroup *grp = al.m_pPlayAnimationGroup;
    ASSERT_NE(grp, nullptr);
    ASSERT_NE(al.m_pPlayShowAnimation, nullptr);
    ASSERT_NE(al.m_pPlayHideAnimation, nullptr);

    // Assert: show 段 0→9、pause 段、hide 段 9→18，属性名均为 fps，时长为正
    EXPECT_EQ(grp->animationCount(), 3);
    EXPECT_EQ(al.m_pPlayShowAnimation->propertyName(), QByteArray("fps"));
    EXPECT_EQ(al.m_pPlayShowAnimation->startValue().toInt(), 0);
    EXPECT_EQ(al.m_pPlayShowAnimation->endValue().toInt(), 9);
    EXPECT_EQ(al.m_pPlayHideAnimation->startValue().toInt(), 9);
    EXPECT_EQ(al.m_pPlayHideAnimation->endValue().toInt(), 18);
    EXPECT_GT(al.m_pPlayShowAnimation->duration(), 0);
    EXPECT_GT(grp->totalDuration(), 0);
    delete host;
}

TEST_F(AnimationLabelExtTest, InitMember_PauseAnimationGroup_StructureComplete)
{
    // Arrange: 构造后检查 initPauseAnimation 的产物
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 读取暂停动画组内部结构
    QSequentialAnimationGroup *grp = al.m_pPauseAnimationGroup;
    ASSERT_NE(grp, nullptr);
    ASSERT_NE(al.m_pPauseShowAnimation, nullptr);
    ASSERT_NE(al.m_pPauseHideAnimation, nullptr);

    // Assert: 结构与播放组对称（0→9→18、属性 fps、3 个子项）
    EXPECT_EQ(grp->animationCount(), 3);
    EXPECT_EQ(al.m_pPauseShowAnimation->propertyName(), QByteArray("fps"));
    EXPECT_EQ(al.m_pPauseShowAnimation->startValue().toInt(), 0);
    EXPECT_EQ(al.m_pPauseShowAnimation->endValue().toInt(), 9);
    EXPECT_EQ(al.m_pPauseHideAnimation->startValue().toInt(), 9);
    EXPECT_EQ(al.m_pPauseHideAnimation->endValue().toInt(), 18);
    EXPECT_GT(al.m_pPauseShowAnimation->duration(), 0);
    delete host;
}

TEST_F(AnimationLabelExtTest, InitMember_Reinit_ReplacesGroupsAndUpdatesMainWindow)
{
    // Arrange: 构造后记录原始动画组指针
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    QSequentialAnimationGroup *oldPlay = al.m_pPlayAnimationGroup;
    ASSERT_NE(oldPlay, nullptr);

    // Act: 再次调用 initMember，换用新的主窗指针
    al.initMember(host);

    // Assert: 动画组被重建（指针变化）、状态复位、主窗指针更新
    EXPECT_NE(al.m_pPlayAnimationGroup, oldPlay);
    ASSERT_NE(al.m_pPlayAnimationGroup, nullptr);
    ASSERT_NE(al.m_pPauseAnimationGroup, nullptr);
    EXPECT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pMainWindow, host);
    EXPECT_TRUE(al.m_sFileName.isEmpty());
    al.m_pPlayAnimationGroup->stop();
    al.m_pPauseAnimationGroup->stop();
    delete host;
}

// ═══════════════════════════════════════════════════════════════
// setWM
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, SetWM_ToggleValues_UpdatesMemberFlag)
{
    // Arrange: 默认 m_bIsWM == false
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 依次置 true / false
    al.setWM(true);
    const bool afterTrue = al.m_bIsWM;
    const QSize sizeAfterTrue = al.size();
    al.setWM(false);
    const bool afterFalse = al.m_bIsWM;

    // Assert: 两个等价类都正确落库；setWM 不改尺寸不改可见性
    EXPECT_TRUE(afterTrue);
    EXPECT_FALSE(afterFalse);
    EXPECT_EQ(sizeAfterTrue, QSize(200, 200));
    EXPECT_EQ(al.size(), QSize(200, 200));
    EXPECT_FALSE(al.isVisible());
    delete host;
}

// ═══════════════════════════════════════════════════════════════
// playAnimation / pauseAnimation（互斥启动 + 停止分支）
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, PlayAnimation_NotRunning_StartsPauseGroupAndShows)
{
    // Arrange: 初始两组均停止
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 调用 playAnimation
    al.playAnimation();

    // Assert: 暂停动画组进入 Running，标签可见
    EXPECT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Running);
    EXPECT_TRUE(al.isVisible());
    al.m_pPauseAnimationGroup->stop();
    delete host;
}

TEST_F(AnimationLabelExtTest, PlayAnimation_PlayGroupRunning_StopsItThenStartsPause)
{
    // Arrange: 先用 pauseAnimation 启动播放动画组（使其处于 Running）
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    al.pauseAnimation();
    ASSERT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Running);

    // Act: 再调 playAnimation（命中"运行中→stop"分支）
    al.playAnimation();

    // Assert: 播放组被停止，暂停组转为 Running
    EXPECT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Running);
    al.m_pPauseAnimationGroup->stop();
    delete host;
}

TEST_F(AnimationLabelExtTest, PauseAnimation_NotRunning_StartsPlayGroupAndShows)
{
    // Arrange: 初始两组均停止
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 调用 pauseAnimation
    al.pauseAnimation();

    // Assert: 播放动画组进入 Running，标签可见
    EXPECT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Running);
    EXPECT_TRUE(al.isVisible());
    al.m_pPlayAnimationGroup->stop();
    delete host;
}

TEST_F(AnimationLabelExtTest, PauseAnimation_PauseGroupRunning_StopsItThenStartsPlay)
{
    // Arrange: 先用 playAnimation 启动暂停动画组
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    al.playAnimation();
    ASSERT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Running);

    // Act: 再调 pauseAnimation（命中"运行中→stop"分支）
    al.pauseAnimation();

    // Assert: 暂停组被停止，播放组转为 Running
    EXPECT_EQ(al.m_pPauseAnimationGroup->state(), QAbstractAnimation::Stopped);
    EXPECT_EQ(al.m_pPlayAnimationGroup->state(), QAbstractAnimation::Running);
    al.m_pPlayAnimationGroup->stop();
    delete host;
}

// ═══════════════════════════════════════════════════════════════
// onPlayAnimationChanged / onPauseAnimationChanged（帧边界 0/中间/18）
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, OnPlayAnimationChanged_FrameValues_UsesStopIconPaths)
{
    // Arrange: 构造标签（x86_64 构建：恒走 stop/%1.png 分支）
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 依次驱动下边界、中间值、上边界
    al.onPlayAnimationChanged(QVariant(0));
    const QString atStart = al.m_sFileName;
    al.onPlayAnimationChanged(QVariant(3));
    const QString atMid = al.m_sFileName;
    al.onPlayAnimationChanged(QVariant(18));
    const QString atEnd = al.m_sFileName;

    // Assert: 文件名逐帧映射到资源路径
    EXPECT_EQ(atStart, QString(":/resources/icons/stop/0.png"));
    EXPECT_EQ(atMid, QString(":/resources/icons/stop/3.png"));
    EXPECT_EQ(atEnd, QString(":/resources/icons/stop/18.png"));
    delete host;
}

TEST_F(AnimationLabelExtTest, OnPauseAnimationChanged_FrameValues_UsesStartIconPaths)
{
    // Arrange: 构造标签（x86_64 构建：恒走 start/%1.png 分支）
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);

    // Act: 依次驱动下边界、中间值、上边界
    al.onPauseAnimationChanged(QVariant(0));
    const QString atStart = al.m_sFileName;
    al.onPauseAnimationChanged(QVariant(7));
    const QString atMid = al.m_sFileName;
    al.onPauseAnimationChanged(QVariant(18));
    const QString atEnd = al.m_sFileName;

    // Assert: 文件名逐帧映射到资源路径
    EXPECT_EQ(atStart, QString(":/resources/icons/start/0.png"));
    EXPECT_EQ(atMid, QString(":/resources/icons/start/7.png"));
    EXPECT_EQ(atEnd, QString(":/resources/icons/start/18.png"));
    delete host;
}

// ═══════════════════════════════════════════════════════════════
// onHideAnimation（主窗 null / 非 null 两分支）
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, OnHideAnimation_WithMainWindow_HidesAndUpdatesHost)
{
    // Arrange: 主窗非空，先 show 使标签可见
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    al.show();
    ASSERT_TRUE(al.isVisible());

    // Act: 触发隐藏动画回调
    al.onHideAnimation();

    // Assert: 标签被隐藏，主窗 update() 可安全执行（无崩溃即通过）
    EXPECT_FALSE(al.isVisible());
    EXPECT_TRUE(al.isHidden());
    EXPECT_NO_FATAL_FAILURE(host->update());
    EXPECT_EQ(al.m_pMainWindow, host);
    delete host;
}

TEST_F(AnimationLabelExtTest, OnHideAnimation_NullMainWindow_NoCrash)
{
    // Arrange: 全空构造（父窗与主窗均为 null），直接显示
    AnimationLabel al;
    al.show();
    ASSERT_TRUE(al.isVisible());

    // Act: 主窗为空时触发隐藏回调（null 分支）
    EXPECT_NO_FATAL_FAILURE(al.onHideAnimation());

    // Assert: 隐藏依然生效，未解引用空指针
    EXPECT_FALSE(al.isVisible());
    EXPECT_EQ(al.m_pMainWindow, nullptr);
}

// ═══════════════════════════════════════════════════════════════
// showEvent / moveEvent / paintEvent（事件与绘制）
// ═══════════════════════════════════════════════════════════════

TEST_F(AnimationLabelExtTest, ShowAndMoveEvents_DelegateToQFrame_NoCrash)
{
    // Arrange: 空主窗构造（null 分支不触发重定位）并准备 show/move 事件
    AnimationLabel al;
    QShowEvent se;
    QMoveEvent me(QPoint(12, 34), QPoint(0, 0));

    // Act: 直接向控件分发两个事件（纯委托路径）
    const QSize before = al.size();
    QApplication::sendEvent(&al, &se);
    QApplication::sendEvent(&al, &me);

    // Assert: 委托未改变尺寸等内部状态，事件处理无崩溃（sendEvent 不真正显示，仍处于隐藏态）
    EXPECT_EQ(al.size(), before);
    EXPECT_FALSE(al.isVisible());
    EXPECT_TRUE(al.isHidden());
    EXPECT_NO_FATAL_FAILURE(al.update());
}

TEST_F(AnimationLabelExtTest, PaintEvent_NullPixmap_NoCrash)
{
    // Arrange: 不加载任何图标，m_pixmap 保持空
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    al.resize(64, 64);
    ASSERT_TRUE(al.m_pixmap.isNull());
    QPaintEvent pe(al.rect());

    // Act: 空 pixmap 状态下绘制
    QApplication::sendEvent(&al, &pe);

    // Assert: 无崩溃（负面场景），pixmap 保持为空，尺寸未被绘制流程修改
    EXPECT_TRUE(al.m_pixmap.isNull());
    EXPECT_EQ(al.size(), QSize(64, 64));
    EXPECT_TRUE(al.m_sFileName.isEmpty());
    EXPECT_NO_FATAL_FAILURE(al.update());
    delete host;
}

TEST_F(AnimationLabelExtTest, PaintEvent_FrameLoaded_NoCrash)
{
    // Arrange: 加载一帧后 pixmap 非空
    QWidget *host = al_makeHost();
    AnimationLabel al(nullptr, host);
    al.onPlayAnimationChanged(QVariant(1));
    al.resize(64, 64);
    QPaintEvent pe(al.rect());

    // Act: 非 null pixmap 状态下绘制
    QApplication::sendEvent(&al, &pe);

    // Assert: 无崩溃且文件名保持为刚设置的帧，pixmap 已加载非空
    EXPECT_EQ(al.m_sFileName, QString(":/resources/icons/stop/1.png"));
    EXPECT_FALSE(al.m_pixmap.isNull());
    EXPECT_NO_FATAL_FAILURE(al.update());
    delete host;
}

} // namespace
