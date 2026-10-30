// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch2: HwdecProbe (src/common/hwdec_probe.cpp)
//
// 用例数: 8   覆盖方法: 6/6 (ctor, get, initffmpegInterface, getHwTypes,
//                            hwDecoderInit, isTypeHaveHwdec) + isFileCanHwdec
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ctor: m_hwDeviceCtx=nullptr           → get_returnsSingletonInstance (隐式)
//   get: return m_ffmpegProbe             → get_returnsSingletonInstance
//   initffmpegInterface: 9 处 resolve     → initffmpegInterface_resolvesFunctionPointers
//   getHwTypes: while 迭代收集类型        → getHwTypes_matchesManualIterate
//   hwDecoderInit: 无效 type err<0        → hwDecoderInit_invalidType_returnsError
//   hwDecoderInit: 成功创建 + 二次 unref  → hwDecoderInit_vaapiTwice (VAAPI 可用时)
//   isTypeHaveHwdec: config 命中/穷尽     → isTypeHaveHwdec_selfConsistent (含 NONE 穷尽分支)
//   isFileCanHwdec: open 失败 false       → isFileCanHwdec_unopenableFile_returnsFalse
//   isFileCanHwdec: open 成功但流信息失败 → isFileCanHwdec_garbageWavFile_returnsFalse
//
// 缺陷说明(标红不修复): initffmpegInterface/getHwTypes 均为 private 且全工程
// 无调用点(mpv_proxy.cpp:1701 的 isFileCanHwdec 调用已注释), 构造函数内初始化
// 被注释 ——  生产路径永远不会初始化函数指针; isFileCanHwdec 直接经指针调用,
// 静态存储零初始化下等价于空指针调用。本测试用 #define private public 范式按生产语义
// 补齐初始化(调 initffmpegInterface 本体), 以覆盖其余逻辑分支。
// hwDecoderInit 成功分支需要宿主 VAAPI 硬件上下文, 经 getHwTypes 结果预检,
// 不可用时跳过; 其余分支全部确定性。

#include <gtest/gtest.h>
#include <QFile>
#include <QLibrary>
#include <QTemporaryFile>
#include <QUrl>
#include <malloc.h>

// 私有函数/字段访问: 本仓库平台侧测试同款范式(#define private public 仅作用于本 TU)。
#define protected public
#define private public
#include "hwdec_probe.h"
#undef protected
#undef private

#include "sysutils.h"
#include "stub/stubext.h"

using namespace dmr;

namespace {

// 与源码同源的动态库解析辅助(避免测试目标直链 ffmpeg)
struct AvFuncs
{
    const AVCodec *(*findDecoder)(AVCodecID) = nullptr;
    AVCodecContext *(*allocContext3)(const AVCodec *) = nullptr;
    void (*freeContext)(AVCodecContext **) = nullptr;
    const AVCodecHWConfig *(*getHwConfig)(const AVCodec *, int) = nullptr;
    AVHWDeviceType (*iterateTypes)(AVHWDeviceType) = nullptr;

    static AvFuncs &instance()
    {
        static AvFuncs f = [] {
            QLibrary avcodec(SysUtils::libPath(QStringLiteral("libavcodec.so")));
            QLibrary avutil(SysUtils::libPath(QStringLiteral("libavutil.so")));
            avcodec.load();
            avutil.load();
            f.findDecoder = reinterpret_cast<const AVCodec *(*)(AVCodecID)>(
                avcodec.resolve("avcodec_find_decoder"));
            f.allocContext3 = reinterpret_cast<AVCodecContext *(*)(const AVCodec *)>(
                avcodec.resolve("avcodec_alloc_context3"));
            f.freeContext = reinterpret_cast<void (*)(AVCodecContext **)>(
                avcodec.resolve("avcodec_free_context"));
            f.getHwConfig = reinterpret_cast<const AVCodecHWConfig *(*)(const AVCodec *, int)>(
                avcodec.resolve("avcodec_get_hw_config"));
            f.iterateTypes = reinterpret_cast<AVHWDeviceType (*)(AVHWDeviceType)>(
                avutil.resolve("av_hwdevice_iterate_types"));
            return f;
        }();
        return f;
    }
};

bool vaapiAvailable()
{
    const AVCodec *dec = AvFuncs::instance().findDecoder(AV_CODEC_ID_H264);
    if (!dec)
        return false;
    for (int j = 0;; ++j) {
        const AVCodecHWConfig *cfg = AvFuncs::instance().getHwConfig(dec, j);
        if (!cfg)
            return false;
        if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == AV_HWDEVICE_TYPE_VAAPI)
            return true;
    }
}

class HwdecProbeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        HwdecProbe &probe = HwdecProbe::get();
        probe.initffmpegInterface();
        probe.getHwTypes();
    }
};

TEST_F(HwdecProbeTest, get_returnsSingletonInstance)
{
    // Arrange + Act
    HwdecProbe &a = HwdecProbe::get();
    HwdecProbe &b = HwdecProbe::get();

    // Assert
    EXPECT_EQ(&a, &b);
}

TEST_F(HwdecProbeTest, initffmpegInterface_resolvesFunctionPointers)
{
    // Arrange
    HwdecProbe &probe = HwdecProbe::get();

    // Act: SetUp 中已调用 initffmpegInterface

    // Assert: 迭代接口已解析(空指针调用会直接崩溃, 以此证明初始化生效)
    ffmAvHwdeviceIterateTypes fn = probe.m_avHwdeviceIterateTypes;
    ASSERT_NE(fn, nullptr);
    EXPECT_NE(fn(AV_HWDEVICE_TYPE_NONE), AV_HWDEVICE_TYPE_NONE);
}

TEST_F(HwdecProbeTest, getHwTypes_matchesManualIterate)
{
    // Arrange
    HwdecProbe &probe = HwdecProbe::get();
    int expected = 0;
    AVHWDeviceType t = AV_HWDEVICE_TYPE_NONE;
    while ((t = AvFuncs::instance().iterateTypes(t)) != AV_HWDEVICE_TYPE_NONE)
        ++expected;

    // Act
    probe.getHwTypes();

    // Assert: 清空重收后与手动迭代计数一致
    const QList<AVHWDeviceType> &list = probe.m_hwTypeList;
    EXPECT_EQ(list.size(), expected);
}

TEST_F(HwdecProbeTest, hwDecoderInit_invalidType_returnsError)
{
    // Arrange
    HwdecProbe &probe = HwdecProbe::get();

    // Act
    // AV_HWDEVICE_TYPE_NONE 无法创建硬件上下文, 走 err<0 分支
    int err = probe.hwDecoderInit(
        static_cast<AVCodecContext *>(nullptr), AV_HWDEVICE_TYPE_NONE);

    // Assert
    EXPECT_LT(err, 0);
}

TEST_F(HwdecProbeTest, hwDecoderInit_vaapiTwice_succeedsAndUnrefs)
{
    // Arrange
    if (!vaapiAvailable())
        GTEST_SKIP() << "host has no VAAPI hw config for H264";
    HwdecProbe &probe = HwdecProbe::get();
    const AVCodec *dec = AvFuncs::instance().findDecoder(AV_CODEC_ID_H264);
    ASSERT_NE(dec, nullptr);
    AVCodecContext *ctx = AvFuncs::instance().allocContext3(dec);
    ASSERT_NE(ctx, nullptr);

    // Act
    int err1 = probe.hwDecoderInit( ctx, AV_HWDEVICE_TYPE_VAAPI);
    int err2 = probe.hwDecoderInit( ctx, AV_HWDEVICE_TYPE_VAAPI);

    // Assert
    EXPECT_EQ(err1, 0);
    EXPECT_EQ(err2, 0);   // 二次调用先 m_avBufferUnref 旧 ctx 再重建
    EXPECT_NE(ctx->hw_device_ctx, nullptr);
    AvFuncs::instance().freeContext(&ctx);
}

TEST_F(HwdecProbeTest, isTypeHaveHwdec_selfConsistentWithHwConfig)
{
    // Arrange
    HwdecProbe &probe = HwdecProbe::get();
    const AVCodec *dec = AvFuncs::instance().findDecoder(AV_CODEC_ID_H264);
    ASSERT_NE(dec, nullptr);

    // Act + Assert
    // 与源码同逻辑的手算期望: H264 是否有 VAAPI 硬解配置
    bool expected = false;
    for (int j = 0;; ++j) {
        const AVCodecHWConfig *cfg = AvFuncs::instance().getHwConfig(dec, j);
        if (!cfg)
            break;
        if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == AV_HWDEVICE_TYPE_VAAPI) {
            expected = true;
            break;
        }
    }
    EXPECT_EQ(probe.isTypeHaveHwdec(dec, AV_HWDEVICE_TYPE_VAAPI), expected);

    // NONE 类型不会出现在任何 hw config 中 → 穷尽 config 表 → false
    EXPECT_FALSE(probe.isTypeHaveHwdec(dec, AV_HWDEVICE_TYPE_NONE));
}

TEST_F(HwdecProbeTest, isFileCanHwdec_unopenableFile_returnsFalse)
{
    // Arrange
    HwdecProbe &probe = HwdecProbe::get();
    QList<QString> hwList{QStringLiteral("should-be-cleared")};

    // Act
    bool ok = probe.isFileCanHwdec(
        QUrl::fromLocalFile(QStringLiteral("/nonexistent_ut_file_12345.mp4")), hwList);

    // Assert
    EXPECT_FALSE(ok);
    EXPECT_TRUE(hwList.isEmpty());
}

TEST_F(HwdecProbeTest, isFileCanHwdec_garbageWavFile_returnsFalse)
{
    // Arrange: 仅 WAV 头无数据 → open 成功而 find_stream_info 失败或无视频流
    QTemporaryFile wav;
    ASSERT_TRUE(wav.open());
    const QByteArray riff = QByteArrayLiteral("RIFF")
        + QByteArray("\x24\x00\x00\x00", 4) + QByteArrayLiteral("WAVE")
        + QByteArrayLiteral("fmt ")
        + QByteArray("\x10\x00\x00\x00", 4)
        + QByteArray("\x01\x00\x02\x00\x44\xac\x00\x00\x10\xb1\x02\x00\x04\x00\x10\x00",
                     16)
        + QByteArrayLiteral("data")
        + QByteArray("\x00\x00\x00\x00", 4);
    wav.write(riff);
    wav.flush();
    HwdecProbe &probe = HwdecProbe::get();
    QList<QString> hwList{QStringLiteral("should-be-cleared")};

    // Act
    bool ok = probe.isFileCanHwdec(QUrl::fromLocalFile(wav.fileName()), hwList);

    // Assert: 无视频硬解流 → false; 无论走 find_stream_info 失败分支还是空流分支
    EXPECT_FALSE(ok);
    EXPECT_TRUE(hwList.isEmpty());
}

TEST_F(HwdecProbeTest, isFileCanHwdec_streamInfoFail_returnsFalse)
{
    // Arrange: 桩 openInput 成功 + findStreamInfo 失败, 复现源码 40-43 行分支;
    // WAV 垃圾文件用例走的是 find_stream_info 成功而无视频流分支, 二者互补
    HwdecProbe &probe = HwdecProbe::get();
    static AVFormatContext s_dummyCtx;
    probe.m_avformatOpenInput =
        [](AVFormatContext **ps, const char *, AVInputFormat *, AVDictionary **) -> int {
            *ps = &s_dummyCtx;
            return 0;
        };
    probe.m_avformatFindStreamInfo = [](AVFormatContext *, AVDictionary **) -> int {
        return -1;
    };
    probe.m_avformatCloseInput = [](AVFormatContext **s) -> void { *s = nullptr; };

    QList<QString> hwList{QStringLiteral("stale")};
    bool ok = probe.isFileCanHwdec(QUrl(QStringLiteral("file:///ut/fake/video.mp4")), hwList);

    // Assert: 找不到流信息 → false 且列表被清空
    EXPECT_FALSE(ok);
    EXPECT_TRUE(hwList.isEmpty());
    probe.initffmpegInterface();   // 恢复真实函数指针, 避免污染后续用例
}

TEST_F(HwdecProbeTest, isFileCanHwdec_deepVideoPath_appendsHwType)
{
    // Arrange: 桩穿 ffmpeg 全链路, 驱动 isFileCanHwdec 深层循环(源码 56-74 行):
    // allocContext3→paramsToContext→hwDecoderInit→open2 成功, codec_type=VIDEO
    HwdecProbe &probe = HwdecProbe::get();
    static AVCodecParameters s_par;
    static AVStream s_stream;
    static AVFormatContext s_ctx;
    static AVCodecContext s_codecCtx;

    s_par.codec_id = AV_CODEC_ID_H264;
    s_stream.codecpar = &s_par;
    // streams 数组会被 SUT 以 free() 释放, 必须走 malloc(ASAN 拦截 new/free 混用)
    AVStream **streamsArr = static_cast<AVStream **>(malloc(sizeof(AVStream *)));
    streamsArr[0] = &s_stream;
    s_ctx.nb_streams = 1;
    s_ctx.streams = streamsArr;
    s_codecCtx.codec_type = AVMEDIA_TYPE_VIDEO;

    // 不打代码桩(isTypeHaveHwdec/hwDecoderInit 函数体过小, trampoline 会踩到
    // 相邻函数的代码尾, 实测 corrupted isFileCanHwdec 尾部导致 free 错误地址);
    // 改为桩它们依赖的函数指针字段, 语义等价且无副作用
    static AVCodecHWConfig s_cfg;
    s_cfg.methods = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX;
    s_cfg.device_type = AV_HWDEVICE_TYPE_VAAPI;
    static AVBufferRef s_bufRef;
    probe.m_avcodecGetHwConfig = [](const AVCodec *, int index) -> const AVCodecHWConfig * {
        return index == 0 ? &s_cfg : nullptr;   // isTypeHaveHwdec 命中 VAAPI 配置
    };
    probe.m_avHwdeviceCtxCreate =
        [](AVBufferRef **ctx, AVHWDeviceType, const char *, AVDictionary *, int) -> int {
            *ctx = &s_bufRef;
            return 0;                            // hwDecoderInit 建上下文成功
        };
    probe.m_avBufferRef = [](AVBufferRef *buf) -> AVBufferRef * { return buf; };
    // 假 s_ctx 非 ffmpeg 分配, close/unref 不能走真实实现(会 free 静态对象砸堆)
    probe.m_avformatCloseInput = [](AVFormatContext **s) -> void { *s = nullptr; };
    probe.m_avBufferUnref = [](AVBufferRef **) -> void {};
    probe.m_avformatOpenInput =
        [](AVFormatContext **ps, const char *, AVInputFormat *, AVDictionary **) -> int {
            *ps = &s_ctx;
            return 0;
        };
    probe.m_avformatFindStreamInfo = [](AVFormatContext *, AVDictionary **) -> int { return 0; };
    probe.m_avcodecAllocContext3 = [](const AVCodec *) -> AVCodecContext * {
        return &s_codecCtx;
    };
    probe.m_avcodecParametersToContext = [](AVCodecContext *, const AVCodecParameters *) -> int {
        return 0;
    };
    probe.m_avcodecOpen2 = [](AVCodecContext *, const AVCodec *, AVDictionary **) -> int {
        return 0;
    };
    // 假 ctx 非 ffmpeg 分配, close/free 必须置空防真实释放砸堆
    probe.m_avcodecClose = [](AVCodecContext *) -> int { return 0; };
    probe.m_avcodecFreeContext = [](AVCodecContext **) -> void {};
    probe.m_hwTypeList = {AV_HWDEVICE_TYPE_VAAPI};

    QList<QString> hwList;
    bool ok = probe.isFileCanHwdec(QUrl(QStringLiteral("file:///ut/fake/video.mp4")), hwList);

    // Assert: 成功硬解 → true 且追加真实 av_hwdevice_get_type_name 名称
    EXPECT_TRUE(ok);
    ASSERT_EQ(hwList.size(), 1);
    EXPECT_EQ(hwList.first(), QStringLiteral("vaapi"));
    probe.m_hwDeviceCtx = nullptr;
    probe.initffmpegInterface();   // 恢复真实函数指针
}

}  // namespace
