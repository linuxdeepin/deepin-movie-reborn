// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for src/widgets/burst_screenshots_dialog.cpp
// (class dmr::BurstScreenshotsDialog) — gap-fill suite.
//
// Existing suites (misc_widgets_ext / small_widgets_ext / boost_backend)
// cover construction sizes, updateWithFrames thumbnail counts, and grid
// layout invariants. This suite targets the UNCOVERED methods:
//   - exec()            → virtual-call chain down to QDialog::exec (stubbed
//                         by address to prove the return value propagates)
//   - savePoster()      → Settings::screenshotNameTemplate stubbed to a
//                         QTemporaryDir path; the grabbed pixmap is written
//                         to that file and savedPosterPath() reports it
//   - sizeModeChanged   → real DGuiApplicationHelper::setSizeMode() toggles
//                         NormalMode ↔ CompactMode re-layout (spacings,
//                         fixed sizes, moveToCenter) — no stubbing
//   - updateWithFrames compact branch → ThumbnailFrame fixed to 118x67
//
// 用例计数声明（self-check-structural 验证此块）：
// | method            | level | factors                                | min | actual |
// |-------------------|-------|----------------------------------------|-----|--------|
// | BurstScreenshotsDialog | mid | NormalMode/CompactMode初始化        |  2  |  2     |
// | updateWithFrames  | mid   | Normal/Compact缩略图尺寸               |  2  |  2     |
// | exec              | low   | 委托QDialog::exec                      |  1  |  1     |
// | savePoster        | mid   | 模板路径打桩×直接调用/按钮触发         |  2  |  2     |
// | savedPosterPath   | low   | 保存后读取                             |  1  |  1*    |
// | sizeModeChanged   | mid   | Normal→Compact/Compact→Normal          |  2  |  2     |
// (* savedPosterPath 在 savePoster 两个用例内各自断言)
//
// 最小清单完成情况（test-code-gen §最小清单）：
// 1. 每个公开方法 ≥ 1 用例: [x] 6/6 方法全部有用例
// 2. 每个输入维度按等价类划分 ≥ 1 用例/类: [x] sizeMode {Normal, Compact} ×
//    savePoster 触发方式 {直接调用, SaveBtn clicked}
// 3. 每个等价类的边界值显式覆盖: [x] 紧凑/常规两套设计稿尺寸边界、0 帧 / 多帧
// 4. 同质 ≥ 3 组用 TEST_P: [N/A] 两套 sizeMode 等价类（2 组），各自断言集合不同
// 5. 分支清单 → 用例映射已列出: [x] 见下方 BranchList
// 6. 每条 if/switch/throw/early-return 有触发用例: [x] 构造/更新/信号三条
//    sizeMode 分支各有 Normal 与 Compact 两侧用例
// 7. 异常路径 EXPECT_THROW 精确匹配: [N/A] 被测类无抛异常路径
// 8. 负面场景有专门用例: [x] exec 返回链验证（拒绝真实事件循环）、
//    空 frames 列表（0 帧更新）
// 9. 负面用例验证强异常安全: [x] 保存路径打桩为临时目录，无真实用户目录写入
// 10. stub_ext vs gMock 选择正确: [x] QDialog::exec / Settings::screenshotNameTemplate
//    为外部依赖，用 coolxv Stub 按地址打桩（与仓库既有用例一致）；DTK sizeMode
//    用真实 setSizeMode 驱动，避免过度打桩
//
// 分支清单（源码为 e5346fd1 的 burst_screenshots_dialog.cpp 实测）:
// - ctor L159-L175     : [CompactMode→紧凑尺寸] ↔ [NormalMode→默认尺寸]
// - updateWithFrames L203-L211: [Compact→118x67+0.66 缩放] ↔ [Normal→MakeRounded(size)]
//   其内部 sizeModeChanged lambda: [Normal→178x100] ↔ [Compact→118x67]
// - ctor lambda L177-L196: [NormalMode→600x700] ↔ [CompactMode→396x462] + moveToCenter
// - exec               : 纯委托 DAbstractDialog::exec（最终 QDialog::exec）
// - savePoster         : 无分支（grab→模板路径保存→accept）；模板路径由 Settings 打桩

#include <gtest/gtest.h>
#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QList>
#include <QPair>
#include <QTemporaryDir>
#include <QFile>
#include <QPushButton>
#include <QGridLayout>
#include <QLabel>

#include "stub/stub.h"
#include "stub/addr_any.h"

// 被测头（private 成员白盒访问，Qt 头在访问宏外先行包含）
#define protected public
#define private public
#include "src/widgets/burst_screenshots_dialog.h"
#include "src/libdmr/playlist_model.h"
#include "src/common/dmr_settings.h"
#undef protected
#undef private

using namespace dmr;
DWIDGET_USE_NAMESPACE

namespace {

// 构造仅含标题等静态信息的 PlayItemInfo（对话框只读 .mi）
static PlayItemInfo bsdi_makePlayItemInfo()
{
    PlayItemInfo pif;
    pif.valid = true;
    pif.loaded = true;
    pif.url = QUrl("bst://sample/movie.mp4");
    pif.mi.title = "bst_title";
    pif.mi.duration = 3661;
    pif.mi.resolution = "1280x720";
    pif.mi.fileSize = 5 * 1024 * 1024;
    pif.mi.width = 1280;
    pif.mi.height = 720;
    pif.mi.valid = true;
    return pif;
}

// 构造一帧纯色截图
static QImage bsdi_makeFrame()
{
    QImage img(320, 180, QImage::Format_RGB32);
    img.fill(0x2255AA);
    return img;
}

// ═══════════════════════════════════════════════════════════════
// exec() — 按地址打桩 QDialog::exec，验证返回链
// ═══════════════════════════════════════════════════════════════

static int bsdi_execResult = 0;
static int bsdi_qdialog_exec_stub()
{
    return bsdi_execResult;
}

typedef int (*bsdi_exec_fptr)();

// ═══════════════════════════════════════════════════════════════
// savePoster() — 打桩 Settings::screenshotNameTemplate 到临时目录
// ═══════════════════════════════════════════════════════════════

static QString bsdi_posterTemplate;
static QString bsdi_screenshotNameTemplate_stub()
{
    return bsdi_posterTemplate;
}

typedef QString (*bsdi_tmpl_fptr)();

class BurstScreenshotsDialogExtTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 无显示环境（offscreen 除外）时跳过，避免 CI 无屏崩溃
        if (!QGuiApplication::primaryScreen()) {
            GTEST_SKIP() << "headless environment, primary screen required";
        }
        ASSERT_TRUE(m_tmpDir.isValid());
        // sizeMode 基线恢复为 NormalMode（TearDown 再次兜底）
        DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::NormalMode);
    }

    void TearDown() override
    {
        DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::NormalMode);
        bsdi_posterTemplate.clear();
        bsdi_execResult = 0;
    }

    // 安装 QDialog::exec 地址桩；返回 false 表示动态符号不可用（用例跳过）
    static bool installExecStub(Stub &stub, int result)
    {
        AddrAny any("libQt6Widgets.so.6");
        std::map<std::string, void *> res;
        // AddrAny 对符号做 demangle 后再按正则匹配，需用可读名而非 mangled 名
        any.get_global_func_addr_dynsym("QDialog::exec", res);
        if (res.empty())
            return false;
        bsdi_execResult = result;
        stub.set((bsdi_exec_fptr)res.begin()->second, bsdi_qdialog_exec_stub);
        return true;
    }

    QTemporaryDir m_tmpDir;
};

// ═══════════════════════════════════════════════════════════════
// 构造函数（NormalMode 基线 + CompactMode 初始化）
// ═══════════════════════════════════════════════════════════════

TEST_F(BurstScreenshotsDialogExtTest, BurstScreenshotsDialog_Ctor_NormalMode_DefaultSizes)
{
    // Arrange: NormalMode 基线（SetUp 已设置）
    PlayItemInfo pif = bsdi_makePlayItemInfo();

    // Act: 构造对话框
    BurstScreenshotsDialog dlg(pif);

    // Assert: 常规设计稿尺寸与控件结构
    EXPECT_EQ(dlg.size(), QSize(600, 700));
    ASSERT_NE(dlg.m_pTitlebar, nullptr);
    EXPECT_EQ(dlg.m_pTitlebar->size(), QSize(590, 50));
    // DTitlebar 无公开 title 读取器；改验菜单已挂载（构造流程 setMenu 产物）
    EXPECT_NE(dlg.m_pTitlebar->menu(), nullptr);
    ASSERT_NE(dlg.m_pGrid, nullptr);
    EXPECT_EQ(dlg.m_pGrid->horizontalSpacing(), 12);
    EXPECT_EQ(dlg.m_pGrid->verticalSpacing(), 15);
    EXPECT_EQ(dlg.m_pGrid->columnMinimumWidth(0), 160);
    EXPECT_EQ(dlg.m_pGrid->columnMinimumWidth(2), 160);
    QPushButton *save = dlg.findChild<QPushButton *>("SaveBtn");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->size(), QSize(70, 30));
    EXPECT_TRUE(save->isDefault());
}

TEST_F(BurstScreenshotsDialogExtTest, BurstScreenshotsDialog_Ctor_CompactMode_CompactSizes)
{
    // Arrange: 切换到紧凑模式（真实 setSizeMode 驱动）
    DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::CompactMode);
    PlayItemInfo pif = bsdi_makePlayItemInfo();

    // Act: 构造对话框
    BurstScreenshotsDialog dlg(pif);

    // Assert: 紧凑设计稿尺寸
    EXPECT_EQ(dlg.size(), QSize(396, 462));
    ASSERT_NE(dlg.m_pTitlebar, nullptr);
    EXPECT_EQ(dlg.m_pTitlebar->size(), QSize(389, 33));
    ASSERT_NE(dlg.m_pGrid, nullptr);
    EXPECT_EQ(dlg.m_pGrid->horizontalSpacing(), 8);
    EXPECT_EQ(dlg.m_pGrid->verticalSpacing(), 10);
    QPushButton *save = dlg.findChild<QPushButton *>("SaveBtn");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->size(), QSize(46, 20));
}

// ═══════════════════════════════════════════════════════════════
// updateWithFrames（Normal / Compact 缩略图尺寸分支）
// ═══════════════════════════════════════════════════════════════

TEST_F(BurstScreenshotsDialogExtTest, UpdateWithFrames_NormalMode_ThumbnailCountAndPixmap)
{
    // Arrange: 常规模式对话框 + 3 帧
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QList<QPair<QImage, qint64>> frames;
    for (int i = 0; i < 3; ++i)
        frames.append(qMakePair(bsdi_makeFrame(), qint64(1000 + i)));

    // Act: 注入帧
    dlg.updateWithFrames(frames);

    // Assert: 3 个缩略图子项且像素图非空
    auto thumbs = dlg.findChildren<ThumbnailFrame *>();
    ASSERT_EQ(thumbs.size(), 3);
    for (ThumbnailFrame *tf : thumbs) {
        ASSERT_NE(tf, nullptr);
        EXPECT_FALSE(tf->pixmap().isNull());
    }
    EXPECT_EQ(dlg.m_pGrid->count(), 3);
}

TEST_F(BurstScreenshotsDialogExtTest, UpdateWithFrames_CompactMode_ThumbnailFixed118x67)
{
    // Arrange: 紧凑模式对话框 + 2 帧
    DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::CompactMode);
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QList<QPair<QImage, qint64>> frames;
    for (int i = 0; i < 2; ++i)
        frames.append(qMakePair(bsdi_makeFrame(), qint64(2000 + i)));

    // Act: 注入帧
    dlg.updateWithFrames(frames);

    // Assert: 紧凑分支把缩略图固定为 118x67
    auto thumbs = dlg.findChildren<ThumbnailFrame *>();
    ASSERT_EQ(thumbs.size(), 2);
    EXPECT_EQ(thumbs.at(0)->size(), QSize(118, 67));
    EXPECT_EQ(dlg.m_pGrid->count(), 2);
}

TEST_F(BurstScreenshotsDialogExtTest, UpdateWithFrames_EmptyFrames_NoThumbnailAdded)
{
    // Arrange: 常规模式对话框，空帧列表（负面场景）
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);

    // Act: 注入空列表
    dlg.updateWithFrames(QList<QPair<QImage, qint64>>());

    // Assert: 无缩略图产生，网格为空
    EXPECT_TRUE(dlg.findChildren<ThumbnailFrame *>().isEmpty());
    EXPECT_EQ(dlg.m_pGrid->count(), 0);
}

// ═══════════════════════════════════════════════════════════════
// sizeModeChanged 信号（Normal→Compact→Normal 重排）
// ═══════════════════════════════════════════════════════════════

TEST_F(BurstScreenshotsDialogExtTest, SizeModeChanged_SignalToCompact_RearrangesLayout)
{
    // Arrange: 常规模式构造后注入 2 帧（缩略图子项参与 sizeMode 联动）
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QList<QPair<QImage, qint64>> frames;
    for (int i = 0; i < 2; ++i)
        frames.append(qMakePair(bsdi_makeFrame(), qint64(3000 + i)));
    dlg.updateWithFrames(frames);
    ASSERT_EQ(dlg.size(), QSize(600, 700));

    // Act: 真实切换到紧凑模式（触发 sizeModeChanged 与缩略图重排 lambda）
    DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::CompactMode);

    // Assert: 对话框重排为紧凑尺寸，缩略图被固定为 118x67
    EXPECT_EQ(dlg.size(), QSize(396, 462));
    EXPECT_EQ(dlg.m_pGrid->horizontalSpacing(), 8);
    QPushButton *save = dlg.findChild<QPushButton *>("SaveBtn");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->size(), QSize(46, 20));
    for (ThumbnailFrame *tf : dlg.findChildren<ThumbnailFrame *>())
        EXPECT_EQ(tf->size(), QSize(118, 67));
}

TEST_F(BurstScreenshotsDialogExtTest, SizeModeChanged_SignalBackToNormal_RestoresLayout)
{
    // Arrange: 紧凑模式构造后注入 2 帧
    DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::CompactMode);
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QList<QPair<QImage, qint64>> frames;
    for (int i = 0; i < 2; ++i)
        frames.append(qMakePair(bsdi_makeFrame(), qint64(4000 + i)));
    dlg.updateWithFrames(frames);
    ASSERT_EQ(dlg.size(), QSize(396, 462));

    // Act: 真实切换回常规模式（触发缩略图 Normal 分支 lambda）
    DGuiApplicationHelper::instance()->setSizeMode(DGuiApplicationHelper::NormalMode);

    // Assert: 对话框恢复常规尺寸与间距，缩略图恢复 178x100
    EXPECT_EQ(dlg.size(), QSize(600, 700));
    EXPECT_EQ(dlg.m_pGrid->horizontalSpacing(), 12);
    QPushButton *save = dlg.findChild<QPushButton *>("SaveBtn");
    ASSERT_NE(save, nullptr);
    EXPECT_EQ(save->size(), QSize(70, 30));
    for (ThumbnailFrame *tf : dlg.findChildren<ThumbnailFrame *>())
        EXPECT_EQ(tf->size(), QSize(178, 100));
}

// ═══════════════════════════════════════════════════════════════
// exec / savePoster / savedPosterPath
// ═══════════════════════════════════════════════════════════════

TEST_F(BurstScreenshotsDialogExtTest, Exec_DelegatesToQDialogExec_ReturnsStubbedResult)
{
    // Arrange: 安装 QDialog::exec 地址桩（返回固定值 777）
    Stub stub;
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    if (!installExecStub(stub, 777))
        GTEST_SKIP() << "QDialog::exec dynsym unavailable in libQt6Widgets";
    BurstScreenshotsDialog dlg(pif);

    // Act: 调用被测 exec（委托链末端命中地址桩）
    const int rc = dlg.exec();

    // Assert: 返回值自 QDialog::exec 桩透传
    EXPECT_EQ(rc, 777);
    EXPECT_EQ(dlg.result(), QDialog::Rejected); // 桩短路，未走真实事件循环
}

TEST_F(BurstScreenshotsDialogExtTest, SavePoster_DirectCall_WritesStubbedTemplateFile)
{
    // Arrange: 模板路径打桩到临时目录；对话框注入 1 帧
    Stub stub;
    bsdi_posterTemplate = m_tmpDir.filePath("poster_direct.png");
    stub.set((bsdi_tmpl_fptr)&Settings::screenshotNameTemplate, bsdi_screenshotNameTemplate_stub);
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QList<QPair<QImage, qint64>> frames;
    frames.append(qMakePair(bsdi_makeFrame(), qint64(3000)));
    dlg.updateWithFrames(frames);

    // Act: 直接调用保存
    dlg.savePoster();

    // Assert: 文件写入成功、路径可读回、标题栏加宽、对话框进入 accepted 隐藏态
    EXPECT_TRUE(QFile::exists(bsdi_posterTemplate));
    EXPECT_EQ(dlg.savedPosterPath(), bsdi_posterTemplate);
    EXPECT_EQ(dlg.m_pTitlebar->width(), 610);
    EXPECT_EQ(dlg.result(), QDialog::Accepted);
    EXPECT_FALSE(dlg.isVisible());
}

TEST_F(BurstScreenshotsDialogExtTest, SavePoster_SaveBtnClicked_TriggersSave)
{
    // Arrange: 模板路径打桩；通过按钮触发（覆盖 ctor 中的 connect）
    Stub stub;
    bsdi_posterTemplate = m_tmpDir.filePath("poster_clicked.png");
    stub.set((bsdi_tmpl_fptr)&Settings::screenshotNameTemplate, bsdi_screenshotNameTemplate_stub);
    PlayItemInfo pif = bsdi_makePlayItemInfo();
    BurstScreenshotsDialog dlg(pif);
    QPushButton *save = dlg.findChild<QPushButton *>("SaveBtn");
    ASSERT_NE(save, nullptr);

    // Act: 发送点击事件
    save->click();

    // Assert: 与直接调用一致：文件写入 + 路径记录 + accepted
    EXPECT_TRUE(QFile::exists(bsdi_posterTemplate));
    EXPECT_EQ(dlg.savedPosterPath(), bsdi_posterTemplate);
    EXPECT_EQ(dlg.result(), QDialog::Accepted);
    EXPECT_EQ(dlg.m_pTitlebar->width(), 610);
}

} // namespace
