// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QUrl>
#include <QStandardPaths>

#include <gtest/gtest.h>

// #define 必须在所有项目头之前（player_engine.h 链中已 include playlist_model.h，
// 后置 #define 因 include guard 失效）；initFFmpeg 为 private，测试需手动初始化
// g_mvideo_* 函数指针：测试 target 无 _LIBDMR_，PlaylistModel 构造不调 initFFmpeg，
// g_mvideo_* 全 null，parseFromFile 调 avformat_open_input 会 SIGSEGV at 0x0
#define protected public
#define private public
#include "application.h"
#include "player_engine.h"
#include "playlist_model.h"
#include "src/backends/mpv/mpv_proxy.h"
#undef protected
#undef private
#include "stub/stub.h"
#include "ut_sample_media.h"

// ============================ PersistentManager.save ============================
// 源码：src/libdmr/playlist_model.cpp PersistentManager（.cpp 内私有类，类型不可见，
// 经 PlaylistModel::calculatePlayInfo 的 save 路径间接触发）。
// save 流程：parseFromFile（ffmpeg）ok=true 且本地文件 → 写 cacheinfo（magic/version/
// MovieInfo/lastModified）+ thumbs（thumbnail/thumbnail_dark）两个缓存文件。

// calculatePlayInfo 对本地媒体文件触发缓存保存：cacheinfo 与 thumbs 目录应出现文件
TEST(PlaylistModel, PersistentManagerSave_RoundTripValid)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    // 测试 target 无 _LIBDMR_：构造不调 initFFmpeg，g_mvideo_* 为 null，
    // 手动初始化（真实加载 avformat/avcodec/avutil 库）
    if (!model->m_initFFmpeg)
        model->initFFmpeg();
    ASSERT_TRUE(model->m_initFFmpeg);

    // 真实媒体文件（ffmpeg 内容探测返回 ok=true，否则不触发 save）
    const QString media = ut_ensureSampleMedia();
    if (!QFileInfo::exists(media))
        GTEST_SKIP() << "sample media missing";

    QFileInfo fi(media);
    QUrl url = QUrl::fromLocalFile(media);

    // PersistentManager 缓存目录（构造时由 QStandardPaths 推导）
    const QString cacheDir = QString("%1/%2/%3/cacheinfo")
                                 .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                                 .arg(qApp->organizationName())
                                 .arg(qApp->applicationName());
    const QString thumbDir = QString("%1/%2/%3/thumbs")
                                 .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                                 .arg(qApp->organizationName())
                                 .arg(qApp->applicationName());
    QDir(cacheDir).removeRecursively();   // 清空保证计数确定
    QDir().mkpath(cacheDir);   // 重建：PersistentManager 单例可能已由其它用例创建（不再 mkpath），
                               // 否则 save 内 f.open(WriteOnly) 因父目录缺失失败
    QDir(thumbDir).removeRecursively();
    QDir().mkpath(thumbDir);

    // calculatePlayInfo：parseFromFile ok=true → PersistentManager::get().save(pif)
    PlayItemInfo pif = model->calculatePlayInfo(url, fi);
    Q_UNUSED(pif);

    // save 触发：cacheinfo 与 thumbs 目录应各出现缓存文件
    EXPECT_TRUE(QDir(cacheDir).exists());
    EXPECT_GT(QDir(cacheDir).entryList(QDir::Files).count(), 0);
    EXPECT_GT(QDir(thumbDir).entryList(QDir::Files).count(), 0);
}

// 缓存文件格式：magic "DMRC" + version 2（v2 增加 lastModified 字段）
TEST(PlaylistModel, PersistentManagerSave_CacheFileHasDmrcMagic)
{
    MainWindow *w = dApp->getMainWindow();
    ASSERT_NE(nullptr, w);
    PlayerEngine *engine = w->engine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    if (!model->m_initFFmpeg)
        model->initFFmpeg();
    ASSERT_TRUE(model->m_initFFmpeg);

    const QString media = ut_ensureSampleMedia();
    if (!QFileInfo::exists(media))
        GTEST_SKIP() << "sample media missing";

    QFileInfo fi(media);
    QUrl url = QUrl::fromLocalFile(media);

    const QString cacheDir = QString("%1/%2/%3/cacheinfo")
                                 .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                                 .arg(qApp->organizationName())
                                 .arg(qApp->applicationName());
    QDir(cacheDir).removeRecursively();
    QDir().mkpath(cacheDir);   // PersistentManager 单例已创建时 get() 不再 mkpath，需手动重建，
                               // 否则 save 内 f.open(WriteOnly) 因父目录缺失失败

    model->calculatePlayInfo(url, fi);

    QStringList files = QDir(cacheDir).entryList(QDir::Files);
    ASSERT_GT(files.count(), 0);

    // 读取缓存文件：前两个 qint32 应为 CACHE_MAGIC(0x444D5243) 与 CACHE_VERSION(2)
    QFile f(QString("%1/%2").arg(cacheDir).arg(files.first()));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QDataStream ds(&f);
    qint32 magic = 0, version = 0;
    ds >> magic >> version;
    EXPECT_EQ(0x444D5243, magic);   // "DMRC"
    EXPECT_EQ(2, version);
}

// PlayerEngine::nextFrame / PlaylistModel::playNext

// my_command 录制 stub（录制原始 args，args 实际类型是 QList<QVariant>，
// 直接 toStringList 会因类型不符崩溃）
static QVector<QVariant> g_ut_b5Cmds;
QVariant ut_b5_my_command_rec_stub(void *obj, mpv_handle *h, const QVariant &args)
{
    Q_UNUSED(obj); Q_UNUSED(h);
    g_ut_b5Cmds.append(args);
    return QVariant();
}

// 局部 PlayerEngine 工厂（复刻 ut_createPlayerEngine，测试 TU 内不可见）
static PlayerEngine *ut_b5_createEngine()
{
    CompositingManager::get()._composited = true;
    if (!CompositingManager::get().m_pMpvConfig)
        CompositingManager::get().m_pMpvConfig = new QMap<QString, QString>();
    return new PlayerEngine(nullptr);
}

// PlayerEngine::nextFrame：_current 为空时早退
TEST(PlayerEngine, nextFrame_NullBackendEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    g_ut_b5Cmds.clear();

    EXPECT_NO_THROW(engine->nextFrame());
    EXPECT_EQ(0, g_ut_b5Cmds.count());
    delete engine;
}

// PlayerEngine::nextFrame：委派给 backend，MpvProxy 发 frame-step
TEST(PlayerEngine, nextFrame_DelegatesToBackend)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_b5_my_command_rec_stub));
    proxy->_state = Backend::Playing;
    g_ut_b5Cmds.clear();

    engine->nextFrame();

    EXPECT_EQ(1, g_ut_b5Cmds.count());
    if (g_ut_b5Cmds.count() == 1) {
        EXPECT_TRUE(g_ut_b5Cmds.at(0).toList().contains(QVariant("frame-step")));
    }
}

// tryPlayCurrent 录制 stub
static int g_ut_b5TryPlayCount = 0;
void ut_b5_tryPlayCurrent_stub(void *obj, bool next)
{
    Q_UNUSED(obj); Q_UNUSED(next);
    g_ut_b5TryPlayCount++;
}

// PlayerEngine::waitLastEnd 录制 stub（避免真调 pollingEndOfPlayback）
static int g_ut_b5WaitLastEnd = 0;
void ut_b5_waitLastEnd_stub(void *obj)
{
    Q_UNUSED(obj);
    g_ut_b5WaitLastEnd++;
}

// PlaylistModel::playNext：空播放列表早退，_current/_last 不变
TEST(PlaylistModel, playNext_EmptyPlaylistEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    EXPECT_EQ(0, model->count());
    model->_current = -7;
    model->_last = -7;

    model->playNext(true);

    EXPECT_EQ(-7, model->_current);
    EXPECT_EQ(-7, model->_last);
    delete engine;
}

// PlaylistModel::playNext：SinglePlay + 用户请求 → 推进 _current/_last 并触发 tryPlayCurrent
TEST(PlaylistModel, playNext_SinglePlayFromUserAdvances)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    stub.set(ADDR(PlaylistModel, tryPlayCurrent),
             reinterpret_cast<void *>(&ut_b5_tryPlayCurrent_stub));
    stub.set(ADDR(PlayerEngine, waitLastEnd),
             reinterpret_cast<void *>(&ut_b5_waitLastEnd_stub));
    PlayItemInfo pif;
    pif.url = QUrl::fromLocalFile("/tmp/fake_b5.mp4");
    model->_infos.append(pif);
    model->_playMode = PlaylistModel::SinglePlay;
    model->_last = -1;
    model->_current = -1;
    g_ut_b5TryPlayCount = 0;

    model->playNext(true);

    EXPECT_EQ(0, model->_current);
    EXPECT_EQ(0, model->_last);
    EXPECT_EQ(1, g_ut_b5TryPlayCount);
    delete engine;
}

// durationStr / previousFrame / pauseResume / isSubVisible / toggleSubtitle / playPrev

// my_get_property 录制 stub（返回 sub-visibility=true 以区分早退与委派）
static QStringList g_ut_b7GetProps;
QVariant ut_b7_myGetProperty_rec_stub(void *obj, mpv_handle *h, const QString &name)
{
    Q_UNUSED(obj); Q_UNUSED(h);
    g_ut_b7GetProps.append(name);
    return QVariant(true);
}

// my_set_property 录制 stub（本 TU 本地容器；test_mpv_proxy.cpp 的 static 跨 TU 不可见）
static QList<QPair<QString, QString>> g_ut_b7Props;
void ut_b7_mySetProperty_rec_stub(void *obj, mpv_handle *h, const QString &k, const QVariant &v)
{
    Q_UNUSED(obj); Q_UNUSED(h);
    g_ut_b7Props.append({k, v.toString()});
}

// my_set_property_async 录制 stub（本 TU 本地）
static QList<QPair<QString, QString>> g_ut_b7PropAsync;
int ut_b7_mySetPropertyAsync_rec_stub(void *obj, mpv_handle *h, const QString &k, const QVariant &v, uint64_t tag)
{
    Q_UNUSED(obj); Q_UNUSED(h); Q_UNUSED(tag);
    g_ut_b7PropAsync.append({k, v.toString()});
    return 0;
}

// MovieInfo::durationStr：委托 Time2str 格式化时长
TEST(PlaylistModel, durationStr_FormatsViaTime2str)
{
    MovieInfo mi;
    mi.duration = 3661;
    EXPECT_EQ(QString("01:01:01"), mi.durationStr());
    mi.duration = 0;
    EXPECT_EQ(QString("00:00:00"), mi.durationStr());
}

// PlayerEngine::previousFrame：_current 为空时早退
TEST(PlayerEngine, previousFrame_NullBackendEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    g_ut_b5Cmds.clear();

    EXPECT_NO_THROW(engine->previousFrame());
    EXPECT_EQ(0, g_ut_b5Cmds.count());
    delete engine;
}

// PlayerEngine::previousFrame：委派给 backend 发送 frame-back-step
TEST(PlayerEngine, previousFrame_DelegatesToBackend)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_command),
             reinterpret_cast<void *>(&ut_b5_my_command_rec_stub));
    proxy->_state = Backend::Playing;
    g_ut_b5Cmds.clear();

    engine->previousFrame();

    EXPECT_EQ(1, g_ut_b5Cmds.count());
    if (g_ut_b5Cmds.count() == 1) {
        EXPECT_TRUE(g_ut_b5Cmds.at(0).toList().contains(QVariant("frame-back-step")));
    }
    delete engine;
}

// PlayerEngine::pauseResume：_current 为空时早退
TEST(PlayerEngine, pauseResume_NullBackendEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    g_ut_b7Props.clear();

    EXPECT_NO_THROW(engine->pauseResume());
    EXPECT_EQ(0, g_ut_b7Props.count());
    delete engine;
}

// PlayerEngine::pauseResume：Idle 状态早退
TEST(PlayerEngine, pauseResume_IdleEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_b7_mySetProperty_rec_stub));
    engine->_state = PlayerEngine::CoreState::Idle;
    g_ut_b7Props.clear();

    engine->pauseResume();
    EXPECT_EQ(0, g_ut_b7Props.count());
    delete engine;
}

// PlayerEngine::pauseResume：委派给 backend 切换 pause 属性
TEST(PlayerEngine, pauseResume_DelegatesToBackend)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property),
             reinterpret_cast<void *>(&ut_b7_mySetProperty_rec_stub));
    engine->_state = PlayerEngine::CoreState::Playing;
    proxy->_state = Backend::Playing;   // paused() = (_state == Paused) = false → 发 pause=true
    g_ut_b7Props.clear();

    engine->pauseResume();

    EXPECT_EQ(1, g_ut_b7Props.count());
    if (g_ut_b7Props.count() == 1) {
        EXPECT_EQ(QString("pause"), g_ut_b7Props.at(0).first);
        EXPECT_EQ("true", g_ut_b7Props.at(0).second);
    }
    delete engine;
}

// PlayerEngine::isSubVisible：Idle 状态返回 false
TEST(PlayerEngine, isSubVisible_IdleReturnsFalse)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    engine->_state = PlayerEngine::CoreState::Idle;

    EXPECT_FALSE(engine->isSubVisible());
    delete engine;
}

// PlayerEngine::isSubVisible：_current 为空且非 Idle 返回 false
TEST(PlayerEngine, isSubVisible_NullBackendReturnsFalse)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    engine->_state = PlayerEngine::CoreState::Playing;

    EXPECT_FALSE(engine->isSubVisible());
    delete engine;
}

// PlayerEngine::isSubVisible：委派 backend 查询 sub-visibility
TEST(PlayerEngine, isSubVisible_DelegatesToBackend)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_get_property),
             reinterpret_cast<void *>(&ut_b7_myGetProperty_rec_stub));
    engine->_state = PlayerEngine::CoreState::Playing;
    proxy->_state = Backend::Playing;
    g_ut_b7GetProps.clear();

    EXPECT_TRUE(engine->isSubVisible());
    EXPECT_TRUE(g_ut_b7GetProps.contains("sub-visibility"));
    delete engine;
}

// PlayerEngine::toggleSubtitle：_current 为空时早退
TEST(PlayerEngine, toggleSubtitle_NullBackendEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    engine->_current = nullptr;
    g_ut_b7PropAsync.clear();

    EXPECT_NO_THROW(engine->toggleSubtitle());
    EXPECT_EQ(0, g_ut_b7PropAsync.count());
    delete engine;
}

// PlayerEngine::toggleSubtitle：委派 backend 翻转 sub-visibility
TEST(PlayerEngine, toggleSubtitle_DelegatesToBackend)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    MpvProxy *proxy = static_cast<MpvProxy *>(engine->_current);
    ASSERT_NE(nullptr, proxy);
    stub.set(ADDR(MpvProxy, my_set_property_async),
             reinterpret_cast<void *>(&ut_b7_mySetPropertyAsync_rec_stub));
    proxy->_state = Backend::Playing;
    g_ut_b7PropAsync.clear();

    engine->toggleSubtitle();

    EXPECT_EQ(1, g_ut_b7PropAsync.count());
    if (g_ut_b7PropAsync.count() == 1) {
        EXPECT_EQ(QString("sub-visibility"), g_ut_b7PropAsync.at(0).first);
    }
    delete engine;
}

// PlaylistModel::playPrev：空播放列表早退
TEST(PlaylistModel, playPrev_EmptyPlaylistEarlyReturn)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    model->_current = -9;
    model->_last = -9;

    model->playPrev(true);

    EXPECT_EQ(-9, model->_current);
    EXPECT_EQ(-9, model->_last);
    delete engine;
}

// PlaylistModel::playPrev：SinglePlay + 用户请求 → 回绕到最后一条
TEST(PlaylistModel, playPrev_SinglePlayFromUserWrapsToLast)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    stub.set(ADDR(PlaylistModel, tryPlayCurrent),
             reinterpret_cast<void *>(&ut_b5_tryPlayCurrent_stub));
    stub.set(ADDR(PlayerEngine, waitLastEnd),
             reinterpret_cast<void *>(&ut_b5_waitLastEnd_stub));
    for (int i = 0; i < 2; ++i) {
        PlayItemInfo pif;
        pif.url = QUrl::fromLocalFile(QString("/tmp/fake_b7_%1.mp4").arg(i));
        model->_infos.append(pif);
    }
    model->_playMode = PlaylistModel::SinglePlay;
    model->_last = 0;
    model->_current = 0;
    g_ut_b5TryPlayCount = 0;
    g_ut_b5WaitLastEnd = 0;

    model->playPrev(true);

    // _last-1 < 0 → _last = count() = 2 → _current = 1
    EXPECT_EQ(1, model->_current);
    EXPECT_EQ(1, model->_last);
    EXPECT_EQ(1, g_ut_b5WaitLastEnd);
    EXPECT_EQ(1, g_ut_b5TryPlayCount);
    delete engine;
}

// PlaylistModel::playPrev：SinglePlay 非用户请求（播放结束）→ 无动作
TEST(PlaylistModel, playPrev_SinglePlayNotFromUserNoop)
{
    Stub stub;
    PlayerEngine *engine = ut_b5_createEngine();
    ASSERT_NE(nullptr, engine);
    PlaylistModel *model = engine->getplaylist();
    ASSERT_NE(nullptr, model);
    stub.set(ADDR(PlaylistModel, tryPlayCurrent),
             reinterpret_cast<void *>(&ut_b5_tryPlayCurrent_stub));
    PlayItemInfo pif;
    pif.url = QUrl::fromLocalFile("/tmp/fake_b7_0.mp4");
    model->_infos.append(pif);
    model->_playMode = PlaylistModel::SinglePlay;
    model->_last = 0;
    model->_current = 0;
    g_ut_b5TryPlayCount = 0;

    model->playPrev(false);

    EXPECT_EQ(0, model->_current);
    EXPECT_EQ(0, model->_last);
    EXPECT_EQ(0, g_ut_b5TryPlayCount);
    delete engine;
}
