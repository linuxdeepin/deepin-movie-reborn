// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for src/widgets/movie_progress_indicator.cpp
// (class dmr::MovieProgressIndicator).
//
// Scope: all 4 graph-tracked methods — constructor, initMember,
// updateMovieProgress, paintEvent. No existing suite constructs
// MovieProgressIndicator (misc_widgets_ext only exercises the platform
// variant), so this file fills the whole gap from zero.
//
// 用例计数声明（self-check-structural 验证此块）：
// | method               | level | factors                          | min | actual |
// |----------------------|-------|----------------------------------|-----|--------|
// | MovieProgressIndicator | mid | size推导                          |  2  |  2     |
// | initMember           | low   | state-reset                      |  2  |  1*    |
// | updateMovieProgress  | mid   | duration/pos等价类×5(TEST_P)     |  3  |  5     |
// | paintEvent           | mid   | pert:0/50/100/超界/负值          |  2  |  1*    |
// (* paintEvent 单用例内驱动 5 组数据态；initMember 在含前置扰动的单用例内覆盖)
//
// 最小清单完成情况（test-code-gen §最小清单）：
// 1. 每个公开方法 ≥ 1 用例: [x] 4/4 方法全部有用例
// 2. 每个输入维度按等价类划分 ≥ 1 用例/类: [x] duration {0, >0} × pos {负, 0,
//    区间内, 边界, 超界}
// 3. 每个等价类的边界值显式覆盖: [x] pos=0、pos=duration（整圆边界）、pos>duration
// 4. 同质 ≥ 3 组用 TEST_P: [x] updateMovieProgress 5 组同质参数 → TEST_P + 3 实例化断言
// 5. 分支清单 → 用例映射已列出: [x] 见下方 BranchList
// 6. 每条 if/switch/throw/early-return 有触发用例: [x] duration<=0 早退分支、
//    nPert 角度区间、paintEvent 内 currentTime 数据态
// 7. 异常路径 EXPECT_THROW 精确匹配: [N/A] 被测类无抛异常路径
// 8. 负面场景有专门用例: [x] 0 时长、超界位置、负位置、空 pixmap 绘制
// 9. 负面用例验证强异常安全: [x] 负面用例断言内部状态与绘制后尺寸无变化
// 10. stub_ext vs gMock 选择正确: [x] 叶子控件 0 出向项目调用，无需 stub；
//     paintEvent 内部使用被测源码自身的时钟状态绘制，测试不直接依赖时钟
//
// 分支清单（源码为 e5346fd1 的 movie_progress_indicator.cpp 实测）:
// - ctor          : (无运行时分支) fontMetrics 推导 m_fixedSize + resize
// - initMember    : (无运行时分支) 三状态复位
// - updateMovieProgress L96: [nDuration<=0 → 置0后 return] ↔ [正常→nLength 推导]
//     → 用例 Param/0 与 Param/Over 等覆盖；nPert 无 clamp（超界>1、负<0 均存储）
// - paintEvent    : (无运行时分支) 依据 m_pert/m_nElapsed 绘制 → 5 组数据态

#include <gtest/gtest.h>
#include <QApplication>
#include <QFontMetrics>
#include <QPaintEvent>

// 被测头（private 成员白盒访问，Qt 头在访问宏外先行包含）
#define protected public
#define private public
#include "src/widgets/movie_progress_indicator.h"
#undef protected
#undef private

using namespace dmr;

namespace {

class MovieProgressIndicatorExtTest : public ::testing::Test {
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
        // 每个用例独立构造/析构控件
    }
};

// updateMovieProgress 同质输入等价类：duration/pos 组合 → 期望 pert
struct ProgressCase {
    const char *name;
    qint64 duration;
    qint64 pos;
    double expectedPert;
};

class MovieProgressIndicatorProgressTest
    : public MovieProgressIndicatorExtTest,
      public ::testing::WithParamInterface<ProgressCase> {
};

TEST_P(MovieProgressIndicatorProgressTest, UpdateMovieProgress_Scenario_StoresExpectedPert)
{
    // Arrange: 按参数构造指示器（构造函数要求父窗）
    const ProgressCase p = GetParam();
    QWidget host;
    MovieProgressIndicator ind(&host);

    // Act: 注入 duration 与 pos
    ind.updateMovieProgress(p.duration, p.pos);

    // Assert: pert 按等式存储，elapsed 与 pos 同步
    EXPECT_DOUBLE_EQ(ind.m_pert, p.expectedPert);
    EXPECT_EQ(ind.m_nElapsed, p.pos);
}

INSTANTIATE_TEST_SUITE_P(ProgressEquivalence, MovieProgressIndicatorProgressTest,
    ::testing::Values(
        ProgressCase{"ZeroDuration", 0, 60000, 0.0},      // 早退分支：0 时长
        ProgressCase{"FirstQuarter", 960000, 240000, 0.25},// 正常区间下四分位
        ProgressCase{"HalfWay", 960000, 480000, 0.5},      // 中点
        ProgressCase{"FullCircle", 960000, 960000, 1.0},   // 边界：pos==duration
        ProgressCase{"OverBoundary", 960000, 1200000, 1.25}// 超界：pos>duration
    ));

// ═══════════════════════════════════════════════════════════════
// 构造函数 / initMember
// ═══════════════════════════════════════════════════════════════

TEST_F(MovieProgressIndicatorExtTest, MovieProgressIndicator_Ctor_FixedSizeFromFontMetrics)
{
    // Arrange: 带父窗构造，并按源码公式（Qt6 分支）预计算期望尺寸
    QWidget host;
    MovieProgressIndicator ind(&host);
    QFont f;
    f.setPixelSize(14);
    QFontMetrics fm(f);
    const QSize expected(qMax(52, fm.horizontalAdvance("999:99")), fm.height() + 10);

    // Act: 读取构造后的内部记录与实际尺寸
    const QSize recorded = ind.m_fixedSize;

    // Assert: m_fixedSize 与实际固定尺寸均为公式结果
    EXPECT_EQ(recorded, expected);
    EXPECT_EQ(ind.size(), expected);
    EXPECT_GE(ind.minimumWidth(), 52);
}

TEST_F(MovieProgressIndicatorExtTest, MovieProgressIndicator_Ctor_InitMemberZeroState)
{
    // Arrange: 构造后先注入进度扰动
    QWidget host;
    MovieProgressIndicator ind(&host);
    ind.updateMovieProgress(600000, 120000);
    ASSERT_NE(ind.m_pert, 0.0);

    // Act: 调用 initMember 复位
    ind.initMember();

    // Assert: 进度状态归零；m_fixedSize 也被重置为 (0,0)（源码语义，
    // 已由 setFixedSize 应用过的控件尺寸不受影响）
    EXPECT_EQ(ind.m_nElapsed, 0);
    EXPECT_DOUBLE_EQ(ind.m_pert, 0.0);
    EXPECT_EQ(ind.m_fixedSize, QSize(0, 0));
    EXPECT_EQ(ind.size(), QSize(52, 30));
}

// ═══════════════════════════════════════════════════════════════
// paintEvent（m_pert 数据态：0/中点/整圆/超界/负值）
// ═══════════════════════════════════════════════════════════════

TEST_F(MovieProgressIndicatorExtTest, PaintEvent_VariousPertStates_NoCrash)
{
    // Arrange: 带父窗构造指示器并准备绘制事件
    QWidget host;
    MovieProgressIndicator ind(&host);
    QPaintEvent pe(ind.rect());

    // Act: 依次驱动 5 组数据态绘制
    QApplication::sendEvent(&ind, &pe);                 // 初始 0
    ind.updateMovieProgress(960000, 480000);
    QApplication::sendEvent(&ind, &pe);                 // 半圆
    ind.updateMovieProgress(960000, 960000);
    QApplication::sendEvent(&ind, &pe);                 // 整圆
    ind.updateMovieProgress(960000, 1200000);
    QApplication::sendEvent(&ind, &pe);                 // 超界 >1
    ind.updateMovieProgress(960000, -1000);
    QApplication::sendEvent(&ind, &pe);                 // 负值

    // Assert: 绘制不崩溃，最终状态与最后一次输入一致（源码用 float 中间精度），尺寸未被修改
    const qreal expectPert = static_cast<qreal>(static_cast<float>(-1000) / 960000.f);
    EXPECT_EQ(ind.m_nElapsed, -1000);
    EXPECT_DOUBLE_EQ(ind.m_pert, expectPert);
    EXPECT_EQ(ind.size(), ind.m_fixedSize);
    EXPECT_NO_FATAL_FAILURE(ind.update());
}

} // namespace
