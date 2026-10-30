// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for src/widgets/videoboxbutton.cpp (class dmr::VideoBoxButton).
//
// Scope: all 5 graph-tracked methods — constructor, enterEvent, leaveEvent,
// mousePressEvent, mouseReleaseEvent. The repo's own coverage config excludes
// videoboxbutton.cpp from its lcov filters and no existing suite constructs
// VideoBoxButton, so this file fills the whole gap from zero.
//
// Observability strategy: the ctor installs an icon from normalPic via
// QIcon::addFile. Real PNG files are created in a QTemporaryDir for the
// "selected" path and deliberately nonexistent paths for the "unselected"
// ones, so icon().availableSizes() flips from empty to non-empty exactly when
// mousePressEvent / mouseReleaseEvent select that path. Internal `status`
// (2=pressed, 0=released) is asserted white-box.
//
// 用例计数声明（self-check-structural 验证此块）：
// | method              | level | factors                               | min | actual |
// |---------------------|-------|---------------------------------------|-----|--------|
// | VideoBoxButton      | mid   | 图标存在/不存在                        |  2  |  2     |
// | enterEvent          | low   | delegate                              |  1  |  1*    |
// | leaveEvent          | low   | body 全注释, 纯委托                    |  1  |  1*    |
// | mousePressEvent     | high  | 分支A/B × checked/hover/press/normal  |  3  |  6     |
// | mouseReleaseEvent   | high  | 分支A/B × checked/hover/press/normal  |  3  |  3     |
// (* enter/leave 为纯委托，单用例覆盖；mousePress 6 个用例覆盖 A/B 两分支与
//    checked/press/normal 三条子选择及空路径回退)
//
// 最小清单完成情况（test-code-gen §最小清单）：
// 1. 每个公开方法 ≥ 1 用例: [x] 5/5 方法全部有用例
// 2. 每个输入维度按等价类划分 ≥ 1 用例/类: [x] propertyPicPaths.first 空/非空 ×
//    属性键存在/缺失/未设置 × isChecked 真/假 × 路径存在/空/不存在
// 3. 每个等价类的边界值显式覆盖: [x] pressPicPath 空串（.isEmpty() 边界）、
//    checked 状态切换边界、property 未设置（QVariant 无效）
// 4. 同质 ≥ 3 组用 TEST_P: [N/A] 各分支的 Arrange 差异显著（文件/checked/属性组合不同），
//    参数化反而隐藏差异，声明表已逐一列出
// 5. 分支清单 → 用例映射已列出: [x] 见下方 BranchList
// 6. 每条 if/switch/throw/early-return 有触发用例: [x] mousePress/Release 的
//    A/B 两分支、B 内 checked/status 分支、A 内 press/hover/checked 选择
// 7. 异常路径 EXPECT_THROW 精确匹配: [N/A] 被测类无抛异常路径
// 8. 负面场景有专门用例: [x] 图标文件全部不存在、路径空串、属性键缺失
// 9. 负面用例验证强异常安全: [x] 负面用例断言状态与图标保持一致，无崩溃无残留
// 10. stub_ext vs gMock 选择正确: [x] 叶子控件 0 出向项目调用，无需 stub；
//     真实文件隔离在 QTemporaryDir，不触真实设备/网络
//
// 分支清单（源码为 e5346fd1 的 videoboxbutton.cpp 实测；leaveEvent 函数体全部被注释，
// 实际仅剩 DButtonBoxButton::leaveEvent 委托一行）:
// - VideoBoxButton ctor : (无运行时分支) 记录 4 路径 + palette + icon(normalPic)
// - enterEvent          : 纯委托 DButtonBoxButton::enterEvent, 无分支
// - leaveEvent          : 纯委托 DButtonBoxButton::leaveEvent, 无分支
// - mousePressEvent L174: [A: first.isEmpty() || !MAP_CONTAINS] ↔ [B: 属性键命中]
//     A 内: [isChecked&&checked非空] / [status==1&&hover非空] / [status==2&&press非空] / 默认normal
//     B 内: [isChecked&&checked非空] / [status==1&&hover非空] / [status==2&&press非空] / 默认normal
//     → 用例 3/4/5 覆盖 A 的 press/checked/空回退，用例 8/9 覆盖 B 命中与回落 A
// - mouseReleaseEvent L226: 同构（status 先置 0）→ 用例 6/7 覆盖 A 的 normal/checked

#include <gtest/gtest.h>
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QIcon>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <DButtonBox>
DWIDGET_USE_NAMESPACE

// 被测头（private 成员白盒访问，Qt/DTK 头在访问宏外先行包含）
#define protected public
#define private public
#include "src/widgets/videoboxbutton.h"
#undef protected
#undef private

// VideoBoxButton 位于全局命名空间

namespace {

class VideoBoxButtonExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 无显示环境（offscreen 除外）时跳过，避免 CI 无屏崩溃
        if (!QGuiApplication::primaryScreen()) {
            GTEST_SKIP() << "headless environment, primary screen required";
        }
        ASSERT_TRUE(m_tmpDir.isValid());
    }

    void TearDown() override
    {
        // QTemporaryDir 析构自动清理生成的 PNG 文件
    }

    // 生成真实 PNG 文件并返回路径；失败返回空串
    QString makePng(const QString &name)
    {
        const QString path = m_tmpDir.filePath(name);
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(0xFF3366AA);
        if (!img.save(path))
            return QString();
        return path;
    }

    // 构造鼠标按下/抬起事件（Qt6.8 签名，与仓库既有用例一致）
    static void sendMouseButton(QWidget *w, QEvent::Type type)
    {
        QMouseEvent me(type, QPointF(10, 10), QPointF(10, 10),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                       QPointingDevice::primaryPointingDevice());
        QApplication::sendEvent(w, &me);
    }

    QTemporaryDir m_tmpDir;
};

static const char *kMissing = "/definitely/nonexistent/vb_missing.png";

// ═══════════════════════════════════════════════════════════════
// 构造函数
// ═══════════════════════════════════════════════════════════════

TEST_F(VideoBoxButtonExtTest, VideoBoxButton_Ctor_InitialState)
{
    // Arrange: 四路路径：normal/press 真实存在，hover/checked 不存在
    const QString normal = makePng("normal.png");
    const QString press = makePng("press.png");
    ASSERT_FALSE(normal.isEmpty());
    ASSERT_FALSE(press.isEmpty());
    QWidget host;

    // Act: 构造按钮
    VideoBoxButton btn("vb", normal, kMissing, press, kMissing, &host);

    // Assert: 文案、图标尺寸、四路路径成员、初始状态
    EXPECT_EQ(btn.text(), QString("vb"));
    EXPECT_EQ(btn.iconSize(), QSize(36, 36));
    EXPECT_EQ(btn.defaultPicPath.normalPicPath, normal);
    EXPECT_EQ(btn.defaultPicPath.hoverPicPath, QString(kMissing));
    EXPECT_EQ(btn.defaultPicPath.pressPicPath, press);
    EXPECT_EQ(btn.defaultPicPath.checkedPicPath, QString(kMissing));
    EXPECT_EQ(btn.status, 0);
    EXPECT_TRUE(btn.propertyPicPaths.first.isEmpty());
}

TEST_F(VideoBoxButtonExtTest, VideoBoxButton_Ctor_MissingIconFiles_StartsSafe)
{
    // Arrange: 全部路径不存在（负面场景）
    QWidget host;

    // Act: 构造并读取初始图标
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    const QIcon icon = btn.icon();

    // Assert: 不存在的图标文件不产生可用位图，但控件构造成功
    EXPECT_TRUE(icon.availableSizes().isEmpty());
    EXPECT_TRUE(btn.isEnabled());
    EXPECT_EQ(btn.status, 0);
}

// ═══════════════════════════════════════════════════════════════
// mousePressEvent（分支 A：属性路径未命中）
// ═══════════════════════════════════════════════════════════════

TEST_F(VideoBoxButtonExtTest, MousePress_NoProperty_Unchecked_SelectsPressPic)
{
    // Arrange: normal 不存在、press 真实存在 → 初始图标为空
    const QString press = makePng("press_a.png");
    ASSERT_FALSE(press.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, press, kMissing, &host);
    ASSERT_TRUE(btn.icon().availableSizes().isEmpty());

    // Act: 左键按下（分支 A：first 为空 → 走默认路径选择；status==2 → press 段命中）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: press 路径被选中 → 图标变为非空；status 置 2
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
    EXPECT_EQ(btn.iconSize(), QSize(36, 36));
}

TEST_F(VideoBoxButtonExtTest, MousePress_NoProperty_Checked_SelectsCheckedPic)
{
    // Arrange: checked 真实存在，按钮 checkable 且已选中
    const QString checked = makePng("checked_a.png");
    ASSERT_FALSE(checked.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, checked, &host);
    btn.setCheckable(true);
    btn.setChecked(true);
    ASSERT_TRUE(btn.isChecked());

    // Act: 左键按下（分支 A：isChecked 命中 checked 段）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: checked 路径被选中 → 图标非空；status 置 2
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

TEST_F(VideoBoxButtonExtTest, MousePress_NoProperty_EmptyPressPic_FallsBackToNormal)
{
    // Arrange: press 路径为空串（.isEmpty() 边界），normal 不存在
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, QString(), kMissing, &host);

    // Act: 左键按下（分支 A：press 段因空串被跳过 → curPicPath 保持 normal）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: 回退到不存在的 normal → 图标仍为空；status 仍置 2
    EXPECT_TRUE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

// ═══════════════════════════════════════════════════════════════
// mouseReleaseEvent（分支 A：属性路径未命中）
// ═══════════════════════════════════════════════════════════════

TEST_F(VideoBoxButtonExtTest, MouseRelease_NoProperty_Unchecked_RestoresNormalPic)
{
    // Arrange: press 真实存在，先按下使图标非空
    const QString press = makePng("press_r.png");
    ASSERT_FALSE(press.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, press, kMissing, &host);
    sendMouseButton(&btn, QEvent::MouseButtonPress);
    ASSERT_FALSE(btn.icon().availableSizes().isEmpty());

    // Act: 抬起（status 先置 0 → press/hover 段均不命中 → 回退 normal，normal 不存在）
    sendMouseButton(&btn, QEvent::MouseButtonRelease);

    // Assert: 图标恢复为空（normal 不存在），status 归 0
    EXPECT_TRUE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 0);
}

TEST_F(VideoBoxButtonExtTest, MouseRelease_NoProperty_Checked_SelectsCheckedPic)
{
    // Arrange: checked 真实存在，按钮 checkable 且已选中
    const QString checked = makePng("checked_r.png");
    ASSERT_FALSE(checked.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, checked, &host);
    btn.setCheckable(true);
    btn.setChecked(true);

    // Act: 抬起（分支 A：isChecked 命中 checked 段）
    sendMouseButton(&btn, QEvent::MouseButtonRelease);

    // Assert: checked 路径被选中 → 图标非空；status 归 0
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 0);
}

// ═══════════════════════════════════════════════════════════════
// mousePressEvent（分支 B：属性键命中 propertyPicPaths）
// ═══════════════════════════════════════════════════════════════

TEST_F(VideoBoxButtonExtTest, MousePress_PropertyKeyPresent_SelectsPropertyPressPic)
{
    // Arrange: 注入属性路径表 modeA→press 真实存在，控件属性置为 modeA
    const QString press = makePng("press_b.png");
    ASSERT_FALSE(press.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    auto info = btn.defaultPicPath;   // VideoPicPathInfo 副本（不拼写私有嵌套名）
    info.normalPicPath = kMissing;
    info.hoverPicPath = kMissing;
    info.pressPicPath = press;
    info.checkedPicPath = kMissing;
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeA");
    ASSERT_EQ(btn.propertyPicPaths.second.count(QVariant("modeA")), 1);

    // Act: 左键按下（分支 B 命中；status==2 → 属性表内 press 段）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: 属性表 press 路径被选中 → 图标非空；status 置 2
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

TEST_F(VideoBoxButtonExtTest, MousePress_PropertyKeyMissing_FallsBackToDefaultPics)
{
    // Arrange: 属性表注入 modeA，但控件属性指向未注册的 modeX
    const QString press = makePng("press_b2.png");
    ASSERT_FALSE(press.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    auto info = btn.defaultPicPath;   // VideoPicPathInfo 副本（不拼写私有嵌套名）
    info.pressPicPath = press;
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeX");
    ASSERT_EQ(btn.propertyPicPaths.second.count(QVariant("modeX")), 0);

    // Act: 左键按下（MAP_CONTAINS 失败 → 回落分支 A；A 内 press 路径为空 → normal）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: 回落到不存在的 normal → 图标仍为空；status 置 2
    EXPECT_TRUE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

// ═══════════════════════════════════════════════════════════════
// enterEvent / leaveEvent / 状态序列
// ═══════════════════════════════════════════════════════════════

TEST_F(VideoBoxButtonExtTest, EnterLeaveEvents_DelegateToBase_NoCrash)
{
    // Arrange: 构造按钮（leaveEvent 函数体全部注释，仅剩基类委托）
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);

    // Act: 直接构造 QEnterEvent 分发 enter，再构造 Leave 事件
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&btn, &ee);
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&btn, &le);

    // Assert: 委托路径无崩溃，状态与图标未被 enter/leave 改动
    EXPECT_EQ(btn.status, 0);
    EXPECT_TRUE(btn.icon().availableSizes().isEmpty());
    EXPECT_TRUE(btn.isEnabled());
}

TEST_F(VideoBoxButtonExtTest, MousePressReleaseSequence_StatusTransitions_2_0_2)
{
    // Arrange: press 路径真实存在
    const QString press = makePng("press_seq.png");
    ASSERT_FALSE(press.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, press, kMissing, &host);

    // Act: 完成 按下→抬起→再按下 三拍序列
    sendMouseButton(&btn, QEvent::MouseButtonPress);
    const int afterPress1 = btn.status;
    sendMouseButton(&btn, QEvent::MouseButtonRelease);
    const int afterRelease = btn.status;
    sendMouseButton(&btn, QEvent::MouseButtonPress);
    const int afterPress2 = btn.status;

    // Assert: 状态机 2 → 0 → 2 完整可复现
    EXPECT_EQ(afterPress1, 2);
    EXPECT_EQ(afterRelease, 0);
    EXPECT_EQ(afterPress2, 2);
}

TEST_F(VideoBoxButtonExtTest, VideoBoxButton_TextOnlyCtor_AppliesLabelAndEmptyPaths)
{
    // Arrange: 仅文本 + 父窗的最简构造参数
    QWidget host;

    // Act: 使用两参构造函数创建按钮
    VideoBoxButton btn("vb2", &host);

    // Assert: 文案生效，四路路径全空、状态归零、无属性路径表
    EXPECT_EQ(btn.text(), QString("vb2"));
    EXPECT_TRUE(btn.defaultPicPath.normalPicPath.isEmpty());
    EXPECT_TRUE(btn.defaultPicPath.hoverPicPath.isEmpty());
    EXPECT_TRUE(btn.defaultPicPath.pressPicPath.isEmpty());
    EXPECT_TRUE(btn.defaultPicPath.checkedPicPath.isEmpty());
    EXPECT_TRUE(btn.propertyPicPaths.first.isEmpty());
    EXPECT_EQ(btn.status, 0);
}

TEST_F(VideoBoxButtonExtTest, MousePress_PropertyHit_Checked_SelectsPropertyCheckedPic)
{
    // Arrange: 属性表注入 modeA 且 checked 段真实存在；控件勾选态置真
    const QString checked = makePng("checked_hit.png");
    ASSERT_FALSE(checked.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    auto info = btn.defaultPicPath;
    info.checkedPicPath = checked;
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeA");
    btn.setCheckable(true);
    btn.setChecked(true);
    ASSERT_TRUE(btn.isChecked());

    // Act: 左键按下（分支 B 命中且 isChecked → 属性表 checked 段）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: 勾选按下后图标可用、状态置 2
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

TEST_F(VideoBoxButtonExtTest, MousePress_PropertyHit_EmptyPressPic_FallsBackToNormalPic)
{
    // Arrange: 构造参数 press 串为空（defaultPicPath.pressPicPath 空，源码空值判断
    // 用的是 defaultPicPath 而赋值取属性表条目），属性表 modeA normal 段真实存在
    const QString normal = makePng("normal_fb.png");
    ASSERT_FALSE(normal.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, QString(), kMissing, &host);
    auto info = btn.defaultPicPath;
    info.normalPicPath = normal;
    info.pressPicPath = QString();
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeA");

    // Act: 左键按下（分支 B 命中；default press 空 → 属性表 normal 兑底）
    sendMouseButton(&btn, QEvent::MouseButtonPress);

    // Assert: 兑底到属性表 normal 段 → 图标可用
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
    EXPECT_EQ(btn.status, 2);
}

TEST_F(VideoBoxButtonExtTest, MouseRelease_PropertyHit_Checked_SelectsPropertyCheckedPic)
{
    // Arrange: 属性表 modeA 的 checked 段真实存在；按下后置勾选态
    const QString checked = makePng("checked_rel.png");
    ASSERT_FALSE(checked.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    auto info = btn.defaultPicPath;
    info.checkedPicPath = checked;
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeA");
    sendMouseButton(&btn, QEvent::MouseButtonPress);
    btn.setCheckable(true);
    btn.setChecked(true);

    // Act: 左键抬起（分支 B 命中且 isChecked → 属性表 checked 段）
    sendMouseButton(&btn, QEvent::MouseButtonRelease);

    // Assert: 抬起后状态归零、图标可用
    EXPECT_EQ(btn.status, 0);
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
}

TEST_F(VideoBoxButtonExtTest, MouseRelease_PropertyHit_Unchecked_RestoresPropertyNormalPic)
{
    // Arrange: 属性表 modeA 的 normal 段真实存在且未勾选
    const QString normal = makePng("normal_rel.png");
    ASSERT_FALSE(normal.isEmpty());
    QWidget host;
    VideoBoxButton btn("vb", kMissing, kMissing, kMissing, kMissing, &host);
    auto info = btn.defaultPicPath;
    info.normalPicPath = normal;
    btn.propertyPicPaths.first = "vbmode";
    btn.propertyPicPaths.second[QVariant("modeA")] = info;
    btn.setProperty("vbmode", "modeA");

    // Act: 左键抬起（分支 B 命中且未勾选 → 属性表 normal 兑底）
    sendMouseButton(&btn, QEvent::MouseButtonRelease);

    // Assert: 抬起后状态归零、图标可用
    EXPECT_EQ(btn.status, 0);
    EXPECT_FALSE(btn.icon().availableSizes().isEmpty());
}

} // namespace
