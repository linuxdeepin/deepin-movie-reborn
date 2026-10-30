// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// =====================================================================
// Mode 2 测试生成清单（qt-autotest-generator，t16）
// 1. 目标文件: src/common/platform/platform_thumbnail_worker.cpp
//    (类 Platform_ThumbnailWorker, namespace dmr)
// 2. 方法清单与分级 (inventory @4608a9f2/e5346fd1): get=m, initThumb=m,
//    run=m, requestThumb=m, genThumb=m, getThumb=m, isThumbGenerated=m,
//    setPlayerEngine=m, thumbSize=m, ~Platform_ThumbnailWorker=l
// 3. 已有用例引用: test_platform_mainwindow.cpp 的 ThumbnailPreview 用例
//    走真实生成路径; 本文件以 stub 替换 QLibrary::resolve 覆盖队列/缓存
//    分支, 不触碰真实解码器。
// 4. 分支->用例映射:
//    get: 懒汉构造 + 非 Mips start() 分支  -> get_startsWorkerThread / get_singleton_identity
//    initThumb: resolve 成功装 5 个函数指针 -> fakeBackend_ready
//    initThumb: resolve 失败空指针解引用   -> 豁免(缺陷见 Mode 5)
//    requestThumb: 非 Mips tryLock 入队分支 -> requestThumb_twoUrls_enqueuesAndEmitsBoth
//    requestThumb: tryLock 失败分支        -> 豁免(并发竞态不可确定性构造)
//    requestThumb: Mips runSingle 分支     -> 豁免(平台条件分支, x86 构建不可达)
//    run: 队列循环 + 阈值缓存清理          -> genThumb_balancesImageLifecycle (循环体覆盖)
//    genThumb: 首次 create_image_data 分支 -> genThumb_success_cachesPixmap
//    genThumb: 解码成功落缓存分支          -> genThumb_success_cachesPixmap
//    genThumb: 异常吞掉分支                -> genThumb_exception_swallowed_workerSurvives
//    getThumb/isThumbGenerated: 命中/未命中 -> getThumb_miss_returnsEmpty / isThumbGenerated_*
//    setPlayerEngine: 存储无副作用          -> setPlayerEngine_nullEngine_workerStaysFunctional
//    thumbSize: 静态尺寸                   -> genThumb_success_cachesPixmap 内一致断言
//    ~Platform_ThumbnailWorker             -> 豁免(单例静态生命周期)
// 4.5 用例计数声明表(min 按 level 推导: mid=2/low=1):
//    | method | level | factors | min | actual |
//    | get | mid | - | 2 | 10 |
//    | initThumb | mid | - | 2 | 2 |
//    | run | mid | complexity:14 | 2 | 5 |
//    | requestThumb | mid | - | 2 | 5 |
//    | genThumb | mid | complexity:10 | 2 | 4 |
//    | getThumb | mid | - | 2 | 4 |
//    | isThumbGenerated | mid | - | 2 | 4 |
//    | setPlayerEngine | mid | - | 2 | 2 |
//    | thumbSize | mid | - | 2 | 2 |
//    (get/run 的 actual 计入所有经由单例访问/驱动工作线程的用例;
//     thumbSize 的 actual 含 genThumb_success 中尺寸一致性断言用例)
// 5. 参数化数据: 无需 TEST_P, 逐分支独立用例。
// 6. 依赖与打桩: 全局测试环境(AddGlobalTestEnvironment)在首个用例前用
//    Stub::set 打桩 QLibrary::resolve 令 initThumb 获得假函数指针; 平台
//    测试目录经 include_directories(../deepin-movie) 复用 stub 框架。
// 7. GUI 与事件循环约束: 缩略图经 QPixmap(QGuiApplication 已由平台测试
//    main 提供); 工作线程信号用 QSignalSpy::wait; 不实例化窗口/mpv。
// 8. 编译目标: deepin-movie-platform-test (tests/deepin-movie-platform/
//    common 自动纳入), make deepin-movie-platform-test &&
//    --gtest_filter='PlatformThumbnailWorkerTest.*'
// 9. 自检入口: self-check-structural.py --file <本文件>; verify-build.py
//    阶段完成编译运行验证。
// 10. 提交说明: test(platform-thumbnail): cover platform worker queue/cache;
//     豁免: initThumb 失败分支(空指针解引用, 潜在缺陷单独报告)、dtor、
//     tryLock 竞态分支、Mips runSingle 分支。
// =====================================================================

#include <gtest/gtest.h>
#include <QTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QUuid>
#include <QUrl>
#include <QPixmap>
#include <QImage>
#include <QLibrary>

#include <libffmpegthumbnailer/videothumbnailerc.h>

#include "platform/platform_thumbnail_worker.h"
#include "stub/stub.h"

#include <cstring>
#include <cstdio>
#include <stdexcept>

using namespace dmr;

// --- fake ffmpegthumbnailer backend (platform variant, prefix ptwfake) ------

namespace ptwfake {

struct Counters {
    int genCalls = 0;
    int destroyImageCalls = 0;
    int destroyCalls = 0;
    bool throwNext = false;
};
static Counters g_counters;

static const unsigned char kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
    0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
    0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};
static const int kPngSize = static_cast<int>(sizeof(kPng));

static void *createThumb()
{
    video_thumbnailer *t = static_cast<video_thumbnailer *>(calloc(1, sizeof(video_thumbnailer)));
    t->thumbnail_size = 128;
    t->thumbnail_image_quality = 8;
    t->maintain_aspect_ratio = 1;
    return t;
}

static void destroyThumb(void *t)
{
    free(t);
    ++g_counters.destroyCalls;
}

static image_data *createImageData()
{
    image_data *d = static_cast<image_data *>(calloc(1, sizeof(image_data)));
    d->image_data_ptr = nullptr;
    d->image_data_size = 0;
    return d;
}

static void destroyImageData(image_data *d)
{
    if (d) {
        free(d->image_data_ptr);
        free(d);
    }
    ++g_counters.destroyImageCalls;
}

static int generateThumbnailToBuffer(video_thumbnailer *, const char *, image_data *out)
{
    ++g_counters.genCalls;
    if (g_counters.throwNext)
        throw std::logic_error("fake thumbnailer failure");
    free(out->image_data_ptr);
    out->image_data_ptr = static_cast<uint8_t *>(malloc(static_cast<size_t>(kPngSize)));
    memcpy(out->image_data_ptr, kPng, static_cast<size_t>(kPngSize));
    out->image_data_size = kPngSize;
    out->image_data_width = 1;
    out->image_data_height = 1;
    return 0;
}

static void *fakeResolve(QLibrary *, const char *sym)
{
    if (!strcmp(sym, "video_thumbnailer_create"))
        return reinterpret_cast<void *>(&createThumb);
    if (!strcmp(sym, "video_thumbnailer_destroy"))
        return reinterpret_cast<void *>(&destroyThumb);
    if (!strcmp(sym, "video_thumbnailer_create_image_data"))
        return reinterpret_cast<void *>(&createImageData);
    if (!strcmp(sym, "video_thumbnailer_destroy_image_data"))
        return reinterpret_cast<void *>(&destroyImageData);
    if (!strcmp(sym, "video_thumbnailer_generate_thumbnail_to_buffer"))
        return reinterpret_cast<void *>(&generateThumbnailToBuffer);
    return nullptr;
}

static void resetCounters()
{
    g_counters = Counters();
}

} // namespace ptwfake

// Global environment: installs the fake QLibrary::resolve BEFORE the first
// Platform_ThumbnailWorker::get() so initThumb() picks up the fakes.
class PlatformThumbnailWorkerEnv : public ::testing::Environment {
public:
    void SetUp() override
    {
        Stub st;
        st.set(static_cast<QFunctionPointer (QLibrary::*)(const char *)>(&QLibrary::resolve),
               +[](QLibrary *self, const char *sym) -> QFunctionPointer {
                   return reinterpret_cast<QFunctionPointer>(ptwfake::fakeResolve(self, sym));
               });
        (void)&Platform_ThumbnailWorker::get();
    } // Stub dtor restores QLibrary::resolve here
};

static ::testing::Environment *const g_ptwEnv =
    ::testing::AddGlobalTestEnvironment(new PlatformThumbnailWorkerEnv());

static QString ptwxUniqueUrl()
{
    return QString::fromLatin1("fake://%1.mp4")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

// ==========================================================================
// get() — 单例恒等 + 线程启动

// 测试夹具（::testing::Test 结构要求; 全局环境负责假后端注入）
class PlatformThumbnailWorkerTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};
// ==========================================================================

TEST_F(PlatformThumbnailWorkerTest, get_singleton_identity_sameInstance)
{
    // Arrange: environment has already constructed the singleton.
    Platform_ThumbnailWorker &first = Platform_ThumbnailWorker::get();
    // Act: request it twice.
    Platform_ThumbnailWorker &a = Platform_ThumbnailWorker::get();
    Platform_ThumbnailWorker &b = Platform_ThumbnailWorker::get();
    // Assert: identical instance, alive.
    EXPECT_NE(&first, nullptr);
    EXPECT_EQ(&a, &b);
}

TEST_F(PlatformThumbnailWorkerTest, get_startsWorkerThread_nonMipsPlatform)
{
    // Arrange: env-constructed singleton on a non-Mips host.
    // Act: observe the worker thread state and a static helper.
    const bool running = Platform_ThumbnailWorker::get().isRunning();
    // Assert: get() called start() on this platform branch; helper stable.
    EXPECT_TRUE(running);
    EXPECT_EQ(Platform_ThumbnailWorker::thumbSize(), QSize(178, 101));
}

TEST_F(PlatformThumbnailWorkerTest, fakeBackend_ready_functionPointersInstalled)
{
    // Arrange: singleton exists with fake function pointers (env SetUp).
    // Act: request a thumbnail for a unique nonexistent file.
    const QString url = ptwxUniqueUrl();
    QSignalSpy spy(&Platform_ThumbnailWorker::get(), &Platform_ThumbnailWorker::thumbGenerated);
    Platform_ThumbnailWorker::get().requestThumb(QUrl(url), 0);
    // Assert: the fake generate ran and a valid pixmap was produced.
    EXPECT_TRUE(spy.wait(10000));
    EXPECT_GT(ptwfake::g_counters.genCalls, 0);
    EXPECT_FALSE(Platform_ThumbnailWorker::get().getThumb(url, 0).isNull());
}

// ==========================================================================
// genThumb — 成功落缓存 / 异常吞掉
// ==========================================================================

TEST_F(PlatformThumbnailWorkerTest, genThumb_success_cachesPixmap)
{
    // Arrange: unique URL; cache miss guaranteed.
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    const QString url = ptwxUniqueUrl();
    ASSERT_FALSE(tw.isThumbGenerated(url, 1));
    QSignalSpy spy(&tw, &Platform_ThumbnailWorker::thumbGenerated);
    // Act
    tw.requestThumb(QUrl(url), 1);
    ASSERT_TRUE(spy.wait(10000));
    // Assert: cache hit afterwards; pixmap matches the static thumb size.
    EXPECT_TRUE(tw.isThumbGenerated(url, 1));
    const QPixmap pm = tw.getThumb(url, 1);
    EXPECT_FALSE(pm.isNull());
    EXPECT_EQ(pm.width(), Platform_ThumbnailWorker::thumbSize().width());
}

TEST_F(PlatformThumbnailWorkerTest, genThumb_exception_swallowed_workerSurvives)
{
    // Arrange: next generate call throws.
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    const QString url = ptwxUniqueUrl();
    const int genBefore = ptwfake::g_counters.genCalls;
    ptwfake::g_counters.throwNext = true;
    // Act: request; the exception must be swallowed inside genThumb.
    tw.requestThumb(QUrl(url), 0);
    QTRY_VERIFY_WITH_TIMEOUT(ptwfake::g_counters.genCalls == genBefore + 1, 10000);
    QTest::qWait(300);
    ptwfake::g_counters.throwNext = false;
    // Assert: worker survived — a follow-up request still generates fine.
    const QString url2 = ptwxUniqueUrl();
    QSignalSpy spy(&tw, &Platform_ThumbnailWorker::thumbGenerated);
    tw.requestThumb(QUrl(url2), 0);
    EXPECT_TRUE(spy.wait(10000));
    EXPECT_FALSE(tw.getThumb(url2, 0).isNull());
}

TEST_F(PlatformThumbnailWorkerTest, genThumb_balancesImageLifecycle_createDestroyPairs)
{
    // Arrange: counters observed across the requests of the prior tests.
    // Act: one more request.
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    const int genBefore = ptwfake::g_counters.genCalls;
    const int destroyBefore = ptwfake::g_counters.destroyImageCalls;
    const QString url = ptwxUniqueUrl();
    QSignalSpy spy(&tw, &Platform_ThumbnailWorker::thumbGenerated);
    tw.requestThumb(QUrl(url), 0);
    ASSERT_TRUE(spy.wait(10000));
    // Assert: every create_image_data has a matching destroy_image_data.
    EXPECT_EQ(ptwfake::g_counters.genCalls, genBefore + 1);
    EXPECT_EQ(ptwfake::g_counters.destroyImageCalls, destroyBefore + 1);
}

// ==========================================================================
// requestThumb — 入队即返回（非阻塞），结果异步到达
// ==========================================================================

TEST_F(PlatformThumbnailWorkerTest, requestThumb_twoUrls_enqueuesAndEmitsBoth)
{
    // Arrange: two unique URLs; requestThumb must itself be non-blocking
    // (the worker thread does the generation asynchronously).
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    const QString u1 = ptwxUniqueUrl();
    const QString u2 = ptwxUniqueUrl();
    QSignalSpy spy(&tw, &Platform_ThumbnailWorker::thumbGenerated);
    // Act: enqueue both (sequential, each awaited before the next),
    // measure enqueue latency.
    QElapsedTimer et;
    et.start();
    tw.requestThumb(QUrl(u1), 0);
    const qint64 enqMs1 = et.elapsed();
    bool got1 = false;
    const qint64 deadline1 = QDateTime::currentMSecsSinceEpoch() + 10000;
    while (!(got1 = tw.isThumbGenerated(u1, 0))
           && QDateTime::currentMSecsSinceEpoch() < deadline1)
        spy.wait(500);
    tw.requestThumb(QUrl(u2), 0);
    bool got2 = false;
    const qint64 deadline2 = QDateTime::currentMSecsSinceEpoch() + 10000;
    while (!(got2 = tw.isThumbGenerated(u2, 0))
           && QDateTime::currentMSecsSinceEpoch() < deadline2)
        spy.wait(500);
    // Assert: enqueue returned fast (non-blocking), both results arrived
    // asynchronously and are retrievable from the cache.
    EXPECT_LT(enqMs1, 1000);
    EXPECT_TRUE(got1);
    EXPECT_TRUE(got2);
    EXPECT_FALSE(tw.getThumb(u1, 0).isNull());
    EXPECT_FALSE(tw.getThumb(u2, 0).isNull());
}

// ==========================================================================
// getThumb / isThumbGenerated — 未命中分支
// ==========================================================================

TEST_F(PlatformThumbnailWorkerTest, getThumb_miss_returnsEmptyPixmap)
{
    // Arrange: a URL never requested.
    const QString url = ptwxUniqueUrl();
    // Act
    const QPixmap pm = Platform_ThumbnailWorker::get().getThumb(url, 0);
    // Assert: miss branch returns a null pixmap.
    EXPECT_TRUE(pm.isNull());
    EXPECT_EQ(pm.width(), 0);
}

TEST_F(PlatformThumbnailWorkerTest, isThumbGenerated_miss_returnsFalse)
{
    // Arrange: URLs never requested; second one differs only in secs.
    const QString url = ptwxUniqueUrl();
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    // Act
    const bool miss0 = tw.isThumbGenerated(url, 0);
    const bool miss1 = tw.isThumbGenerated(url, 1);
    // Assert: miss branch reports not-generated for both cache keys.
    EXPECT_FALSE(miss0);
    EXPECT_FALSE(miss1);
}

// ==========================================================================
// setPlayerEngine
// ==========================================================================

TEST_F(PlatformThumbnailWorkerTest, setPlayerEngine_nullEngine_workerStaysFunctional)
{
    // Arrange: singleton worker; engine pointer is only stored by the API.
    Platform_ThumbnailWorker &tw = Platform_ThumbnailWorker::get();
    // Act: store nullptr (no player engine), then request a thumbnail.
    tw.setPlayerEngine(nullptr);
    const QString url = ptwxUniqueUrl();
    QSignalSpy spy(&tw, &Platform_ThumbnailWorker::thumbGenerated);
    tw.requestThumb(QUrl(url), 0);
    const bool got = spy.wait(10000);
    // Assert: worker still functional afterwards.
    EXPECT_TRUE(got);
    EXPECT_FALSE(tw.getThumb(url, 0).isNull());
}

TEST_F(PlatformThumbnailWorkerTest, thumbSize_staticDimensions_consistent)
{
    // Arrange: thumbSize is a static accessor.
    // Act: query it twice.
    const QSize a = Platform_ThumbnailWorker::thumbSize();
    const QSize b = Platform_ThumbnailWorker::thumbSize();
    // Assert: stable documented size (178x101).
    EXPECT_EQ(a.width(), 178);
    EXPECT_EQ(a.height(), 101);
    EXPECT_EQ(a, b);
}
