// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch3: Platform_ApplicationAdaptor
// (src/common/platform/platform_dbus_adpator.cpp)
//
// 用例数: 6   覆盖方法: 8/8 (ctor, initMember, Raise, funOpenFile, openFile,
//                            openFiles, readDBusMethod, readDBusProperty)
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ctor: initMember + 挂主窗口(平台)     → ctor_bindsParentPlatformMainWindow
//   Raise: showNormal/raise/activate      → windowCalls_smoke_onRealPlatformMainWindow
//   funOpenFile: 主窗口为空告警早退       → funOpenFile_nullMainWindow_safeNoop
//   funOpenFile: 匹配播放列表(else 分支)  → windowCalls_smoke_onRealPlatformMainWindow
//   openFile: URL/本地路径分支 + UOS_AI   → windowCalls_smoke_onRealPlatformMainWindow
//   openFiles: 直通 play(list)            → windowCalls_smoke_onRealPlatformMainWindow
//   readDBusMethod: 接口无效 → 0          → readDBusMethod_invalidInterface_returnsZero
//   readDBusMethod: 合法回复仍 0(缺陷, 平台同) → readDBusMethod_validReply_defectAlwaysZero
//   readDBusProperty: 接口无效 → 0        → readDBusProperty_invalidInterface_returnsZero
//   readDBusProperty: 合法服务返回属性值  → readDBusProperty_validService_returnsProperty
//
// 桩策略: Platform_MainWindow::play 经 stub_ext 替换捕获(真实播放会拉起平台引擎
// 导致挂起); 平台主窗口实例在单一 smoke 用例内构造验证真实窗口链路, 其余用例
// 不构造窗口以控制风险; 外部 DBus 用进程内 UtPlatformAdaptorTestService。
//
// 已记录缺陷: 平台变体 readDBusMethod 同样存在合法回复仍返回 QVariant(0) 的缺陷。

#include <gtest/gtest.h>
#include <QDBusConnection>
#include <QDBusVariant>
#include <QFileInfo>

#include "platform_dbus_adpator.h"
#include "platform_mainwindow.h"
#include "libdmr/playlist_model.h"
#include "stub/stubext.h"

using namespace dmr;

class UtPlatformAdaptorTestService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie.ut.padaptor")
    Q_PROPERTY(int utProp READ utProp CONSTANT)
public:
    explicit UtPlatformAdaptorTestService(QObject *parent = nullptr) : QObject(parent) {}
    int utProp() const { return 43; }
public slots:
    QDBusVariant ping() { return QDBusVariant(QVariant(8)); }
};

namespace {

constexpr const char *kSvc = "com.deepin.movie.ut.padaptor";
constexpr const char *kPath = "/ut/padaptor";

struct PlayCapture
{
    int count = 0;
    QList<QString> last;
};
static PlayCapture g_pplayCap;

static QList<PlayItemInfo> g_pplaylistItems;

void stubPlatformPlayCapture(stub_ext::StubExt &stub)
{
    g_pplayCap = PlayCapture{};
    stub.set_lamda(
        static_cast<void (Platform_MainWindow::*)(const QList<QString> &)>(&Platform_MainWindow::play),
        [](Platform_MainWindow *, const QList<QString> &list) {
            g_pplayCap.count++;
            g_pplayCap.last = list;
        });
}

void stubPlatformPlaylistItems(stub_ext::StubExt &stub, const QList<PlayItemInfo> &items)
{
    g_pplaylistItems = items;
    stub.set_lamda(static_cast<QList<PlayItemInfo> &(PlaylistModel::*)()>(&PlaylistModel::items),
                   [](PlaylistModel *) -> QList<PlayItemInfo> & { return g_pplaylistItems; });
}

PlayItemInfo makeItem(const QString &filePath)
{
    PlayItemInfo info{};
    info.valid = true;
    info.loaded = true;
    info.mi.filePath = filePath;
    info.info = QFileInfo(filePath);
    return info;
}

class PlatformApplicationAdaptorTest : public ::testing::Test
{
protected:
    void TearDown() override { m_stub.clear(); }

    bool registerService()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            return false;
        m_service.reset(new UtPlatformAdaptorTestService);
        if (!QDBusConnection::sessionBus().registerService(kSvc))
            return false;
        return QDBusConnection::sessionBus().registerObject(
            kPath, m_service.data(),
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    }

    stub_ext::StubExt m_stub;
    QScopedPointer<UtPlatformAdaptorTestService> m_service;
};

TEST_F(PlatformApplicationAdaptorTest, ctor_bindsParentPlatformMainWindow)
{
    // Arrange
    Platform_MainWindow mw;

    // Act
    Platform_ApplicationAdaptor adaptor(&mw);

    // Assert
    EXPECT_EQ(adaptor.parent(), &mw);
}

TEST_F(PlatformApplicationAdaptorTest, funOpenFile_nullMainWindow_safeNoop)
{
    // Arrange
    Platform_ApplicationAdaptor adaptor(nullptr);
    stubPlatformPlayCapture(m_stub);

    // Act
    adaptor.funOpenFile(QStringLiteral("anything"));

    // Assert: 空窗口走告警分支, 不触发播放
    EXPECT_EQ(g_pplayCap.count, 0);
}

TEST_F(PlatformApplicationAdaptorTest, windowCalls_smoke_onRealPlatformMainWindow)
{
    // Arrange: 构造真实平台主窗口, 播放副作用全部经桩捕获
    stubPlatformPlayCapture(m_stub);
    stubPlatformPlaylistItems(m_stub, QList<PlayItemInfo>{
        makeItem(QStringLiteral("/tmp/ut_v/plat.MP4"))});
    Platform_MainWindow mw;
    // Platform_MainWindow 同样经 300ms singleShot 异步创建播放列表, 同步补建
    if (!mw.playlist())
        mw.loadPlayList();
    Platform_ApplicationAdaptor adaptor(&mw);

    // Act + Assert: Raise → 窗口可见
    adaptor.Raise();
    EXPECT_TRUE(mw.isVisible());

    // Act + Assert: funOpenFile 命中大小写不敏感匹配
    // (mw 初始化/异步 loadPlayList 可能触发内部 play, 断言前先归零只看本步增量)
    g_pplayCap = PlayCapture{};
    adaptor.funOpenFile(QStringLiteral("plat"));
    ASSERT_EQ(g_pplayCap.count, 1);
    EXPECT_EQ(g_pplayCap.last,
              QList<QString>{QUrl::fromLocalFile(QStringLiteral("/tmp/ut_v/plat.MP4")).toString()});

    // Act + Assert: openFile 本地路径转 file URL
    adaptor.openFile(QStringLiteral("/tmp/ut_plain_p.mp4"));
    EXPECT_EQ(g_pplayCap.last, QList<QString>{QStringLiteral("file:///tmp/ut_plain_p.mp4")});

    // Act + Assert: openFile http URL 保留
    adaptor.openFile(QStringLiteral("http://example.com/p.mp4"));
    EXPECT_EQ(g_pplayCap.last, QList<QString>{QStringLiteral("http://example.com/p.mp4")});

    // Act + Assert: openFile UOS_AI 前缀 → 走播放列表搜索(无匹配不播放)
    g_pplayCap = PlayCapture{};
    int before = g_pplayCap.count;
    adaptor.openFile(QStringLiteral("UOS_AI_nomatch_x"));
    EXPECT_EQ(g_pplayCap.count, before);

    // Act + Assert: openFiles 直通
    const QList<QString> files{QStringLiteral("x.mkv")};
    adaptor.openFiles(files);
    ASSERT_EQ(g_pplayCap.count, before + 1);
    EXPECT_EQ(g_pplayCap.last, files);
}

TEST_F(PlatformApplicationAdaptorTest, readDBusProperty_invalidInterface_returnsZero)
{
    // Act + Assert: 静态方法, 服务不存在 → QVariant(0)
    QVariant v = Platform_ApplicationAdaptor::readDBusProperty(
        QStringLiteral("com.deepin.ut.no.svc"), QStringLiteral("/no"),
        QStringLiteral("com.deepin.ut.no.if"), "prop");
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(PlatformApplicationAdaptorTest, readDBusProperty_validService_returnsProperty)
{
    // Arrange
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = Platform_ApplicationAdaptor::readDBusProperty(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.padaptor"), "utProp");

    // Assert
    EXPECT_EQ(v.toInt(), 43);
}

TEST_F(PlatformApplicationAdaptorTest, readDBusMethod_invalidInterface_returnsZero)
{
    // Act + Assert
    QVariant v = Platform_ApplicationAdaptor::readDBusMethod(
        QStringLiteral("com.deepin.ut.no.svc"), QStringLiteral("/no"),
        QStringLiteral("com.deepin.ut.no.if"), "ping");
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(PlatformApplicationAdaptorTest, readDBusMethod_validReply_defectAlwaysZero)
{
    // Arrange: 合法服务 + 返回 QDBusVariant 的方法
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = Platform_ApplicationAdaptor::readDBusMethod(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.padaptor"), "ping");

    // Assert: 【已知缺陷】合法回复仍返回 QVariant(0), 而非真实返回值 8
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(PlatformApplicationAdaptorTest, readDBusMethod_missingMethod_fallbackZero)
{
    // Arrange: 合法服务+接口但方法名不存在 → QDBus 错误回复合 →
    // 复现源码 144-148 行的无效回复合退分支
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = Platform_ApplicationAdaptor::readDBusMethod(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.padaptor"), "noSuchMethod");

    // Assert: 无效回复合 → 返回初值 0
    EXPECT_EQ(v.toInt(), 0);
}

}  // namespace

#include "test_platform_dbusadaptor.moc"
