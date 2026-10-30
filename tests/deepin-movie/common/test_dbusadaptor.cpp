// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch3: ApplicationAdaptor (src/common/dbus_adpator.cpp)
//
// 用例数: 12  覆盖方法: 8/8 (ctor, initMember, Raise, funOpenFile, openFile,
//                            openFiles, readDBusMethod, readDBusProperty)
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   ctor: initMember + 挂主窗口           → ctor_bindsParentMainWindow
//   Raise: showNormal/raise/activate      → Raise_showsAndActivatesMainWindow
//   funOpenFile: 主窗口为空早退           → funOpenFile_nullMainWindow_safeNoop
//   funOpenFile: 播放列表无匹配           → funOpenFile_noMatch_doesNotPlay
//   funOpenFile: 文件名大小写不敏感匹配   → funOpenFile_matchesByFilename_playsPath
//   openFile: UOS_AI 前缀转播放列表搜索   → openFile_uosAiPrefix_routesToPlaylistSearch
//   openFile: 本地路径 → file URL         → openFile_localPath_convertsToUrl
//   openFile: \w+:// 前缀保留 URL         → openFile_urlScheme_keptAsUrl
//   openFiles: 直通 play(list)            → openFiles_forwardsToPlay
//   readDBusMethod: 接口无效 → 0          → readDBusMethod_invalidInterface_returnsZero
//   readDBusMethod: 回复合法仍返回 0(缺陷) → readDBusMethod_validReply_defectAlwaysZero
//   readDBusProperty: 接口无效 → 0        → readDBusProperty_invalidInterface_returnsZero
//   readDBusProperty: 合法服务返回属性值  → readDBusProperty_validService_returnsProperty
//
// 桩策略: 真实播放副作用(MainWindow::play → mpv 引擎, UT 下会挂起)以 stub_ext
// 替换 MainWindow::play 捕获参数; 播放列表数据经 PlaylistModel::items 注入;
// 外部 DBus 以进程内注册的 UtAdaptorTestService 提供(不依赖外部服务), 无会话
// 总线时相关用例 GTEST_SKIP。MainWindow 实例复用 harness main() 创建的单例。
//
// 已记录缺陷: readDBusMethod 在 reply 合法时仍返回 QVariant(0)(reply.value()
// 被注释掉), 与头文件"读取 d-bus 接口参数值"的语义相悖 → 用例锁定现状并标红。

#include <gtest/gtest.h>
#include <QDBusConnection>
#include <QDBusVariant>
#include <QFileInfo>

#include "dbus_adpator.h"
#include "application.h"
#include "libdmr/playlist_model.h"
#include "stub/stubext.h"

using namespace dmr;

// 测试内自注册的会话总线服务(非外部依赖)
class UtAdaptorTestService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie.ut.adaptor")
    Q_PROPERTY(int utProp READ utProp CONSTANT)
public:
    explicit UtAdaptorTestService(QObject *parent = nullptr) : QObject(parent) {}
    int utProp() const { return 42; }
public slots:
    QDBusVariant ping() { return QDBusVariant(QVariant(7)); }
};

namespace {

constexpr const char *kSvc = "com.deepin.movie.ut.adaptor";
constexpr const char *kPath = "/ut/adaptor";

struct PlayCapture
{
    int count = 0;
    QList<QString> last;
};
static PlayCapture g_playCap;

static QList<PlayItemInfo> g_playlistItems;

void stubPlayCapture(stub_ext::StubExt &stub)
{
    g_playCap = PlayCapture{};
    stub.set_lamda(static_cast<void (MainWindow::*)(const QList<QString> &)>(&MainWindow::play),
                   [](MainWindow *, const QList<QString> &list) {
                       g_playCap.count++;
                       g_playCap.last = list;
                   });
}

// 注入受控播放列表 items(非 const 链路)
void stubPlaylistItems(stub_ext::StubExt &stub, const QList<PlayItemInfo> &items)
{
    g_playlistItems = items;
    stub.set_lamda(static_cast<QList<PlayItemInfo> &(PlaylistModel::*)()>(&PlaylistModel::items),
                   [](PlaylistModel *) -> QList<PlayItemInfo> & { return g_playlistItems; });
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

class ApplicationAdaptorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_mainWindow = dApp->getMainWindow();
        ASSERT_NE(m_mainWindow, nullptr) << "harness must provide MainWindow";
        // MainWindow 经 300ms singleShot 异步创建播放列表; 测试阶段可能尚未触发,
        // 此处同步补建, 避免 funOpenFile/openFile 链路解引用空 m_pPlaylist
        if (!m_mainWindow->playlist())
            m_mainWindow->loadPlayList();
    }

    void TearDown() override { m_stub.clear(); }

    // 注册进程内 DBus 服务; 无总线时返回 false
    bool registerService()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            return false;
        m_service.reset(new UtAdaptorTestService);
        if (!QDBusConnection::sessionBus().registerService(kSvc))
            return false;
        return QDBusConnection::sessionBus().registerObject(
            kPath, m_service.data(),
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    }

    stub_ext::StubExt m_stub;
    QScopedPointer<UtAdaptorTestService> m_service;
    MainWindow *m_mainWindow = nullptr;
};

TEST_F(ApplicationAdaptorTest, ctor_bindsParentMainWindow)
{
    // Act
    ApplicationAdaptor adaptor(m_mainWindow);

    // Assert: QDBusAbstractAdaptor 以主窗口为父对象(initMember 后赋值)
    EXPECT_EQ(adaptor.parent(), m_mainWindow);
}

TEST_F(ApplicationAdaptorTest, Raise_showsAndActivatesMainWindow)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);

    // Act
    adaptor.Raise();

    // Assert: showNormal 后窗口可见
    EXPECT_TRUE(m_mainWindow->isVisible());
}

TEST_F(ApplicationAdaptorTest, funOpenFile_nullMainWindow_safeNoop)
{
    // Arrange
    ApplicationAdaptor adaptor(nullptr);
    stubPlayCapture(m_stub);

    // Act
    adaptor.funOpenFile(QStringLiteral("anything"));

    // Assert: 空窗口走告警分支, 不触发播放
    EXPECT_EQ(g_playCap.count, 0);
}

TEST_F(ApplicationAdaptorTest, funOpenFile_noMatch_doesNotPlay)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);
    stubPlaylistItems(m_stub, QList<PlayItemInfo>{makeItem(QStringLiteral("/tmp/ut_v/clip.mkv"))});

    // Act
    adaptor.funOpenFile(QStringLiteral("nomatch_12345"));

    // Assert
    EXPECT_EQ(g_playCap.count, 0);
}

TEST_F(ApplicationAdaptorTest, funOpenFile_matchesByFilename_playsPath)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);
    stubPlaylistItems(m_stub, QList<PlayItemInfo>{
        makeItem(QStringLiteral("/tmp/ut_v/BigMovie.MP4"))});

    // Act: 文件名大小写不敏感匹配
    adaptor.funOpenFile(QStringLiteral("bigmovie"));

    // Assert: 播放的为列表内完整路径(file URL)
    ASSERT_EQ(g_playCap.count, 1);
    EXPECT_EQ(g_playCap.last,
              QList<QString>{QUrl::fromLocalFile(QStringLiteral("/tmp/ut_v/BigMovie.MP4")).toString()});
}

TEST_F(ApplicationAdaptorTest, openFile_uosAiPrefix_routesToPlaylistSearch)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);
    stubPlaylistItems(m_stub, QList<PlayItemInfo>{
        makeItem(QStringLiteral("/tmp/ut_v/voice.mp4"))});

    // Act: "UOS_AI" 前缀去掉前 6 字符后走 funOpenFile
    adaptor.openFile(QStringLiteral("UOS_AIvoice"));

    // Assert
    ASSERT_EQ(g_playCap.count, 1);
    EXPECT_EQ(g_playCap.last,
              QList<QString>{QUrl::fromLocalFile(QStringLiteral("/tmp/ut_v/voice.mp4")).toString()});

    // UOS_AI 前缀 + 无匹配 → 不播放
    adaptor.openFile(QStringLiteral("UOS_AI_nomatch_999"));
    EXPECT_EQ(g_playCap.count, 1);
}

TEST_F(ApplicationAdaptorTest, openFile_localPath_convertsToUrl)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);

    // Act
    adaptor.openFile(QStringLiteral("/tmp/ut_plain.mp4"));

    // Assert: 本地路径经 QUrl::fromLocalFile 转换
    ASSERT_EQ(g_playCap.count, 1);
    EXPECT_EQ(g_playCap.last, QList<QString>{QStringLiteral("file:///tmp/ut_plain.mp4")});
}

TEST_F(ApplicationAdaptorTest, openFile_urlScheme_keptAsUrl)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);

    // Act: \w+:// 前缀 → 直接 QUrl
    adaptor.openFile(QStringLiteral("http://example.com/v.mp4"));

    // Assert
    ASSERT_EQ(g_playCap.count, 1);
    EXPECT_EQ(g_playCap.last, QList<QString>{QStringLiteral("http://example.com/v.mp4")});
}

TEST_F(ApplicationAdaptorTest, openFiles_forwardsToPlay)
{
    // Arrange
    ApplicationAdaptor adaptor(m_mainWindow);
    stubPlayCapture(m_stub);
    const QList<QString> files{QStringLiteral("a.mp4"), QStringLiteral("b.mp4")};

    // Act
    adaptor.openFiles(files);

    // Assert
    ASSERT_EQ(g_playCap.count, 1);
    EXPECT_EQ(g_playCap.last, files);
}

TEST_F(ApplicationAdaptorTest, readDBusProperty_invalidInterface_returnsZero)
{
    // Act + Assert: 静态方法, 服务不存在 → 接口无效 → QVariant(0)
    QVariant v = ApplicationAdaptor::readDBusProperty(
        QStringLiteral("com.deepin.ut.no.svc"), QStringLiteral("/no"),
        QStringLiteral("com.deepin.ut.no.if"), "prop");
    EXPECT_TRUE(v.isValid());
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(ApplicationAdaptorTest, readDBusProperty_validService_returnsProperty)
{
    // Arrange
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = ApplicationAdaptor::readDBusProperty(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.adaptor"), "utProp");

    // Assert: 经 Properties.Get 取到真实属性值
    EXPECT_EQ(v.toInt(), 42);
}

TEST_F(ApplicationAdaptorTest, readDBusMethod_invalidInterface_returnsZero)
{
    // Act + Assert
    QVariant v = ApplicationAdaptor::readDBusMethod(
        QStringLiteral("com.deepin.ut.no.svc"), QStringLiteral("/no"),
        QStringLiteral("com.deepin.ut.no.if"), "ping");
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(ApplicationAdaptorTest, readDBusMethod_validReply_defectAlwaysZero)
{
    // Arrange: 合法服务 + 返回 QDBusVariant 的方法 → reply.isValid() 为真
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = ApplicationAdaptor::readDBusMethod(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.adaptor"), "ping");

    // Assert: 【已知缺陷】合法回复合仍返回 QVariant(0), 而非方法真实返回值 7
    // (reply.value() 被注释掉) — 锁定现状, 待缺陷闭环后改为 EXPECT_EQ(v, 7)
    EXPECT_EQ(v.typeId(), QMetaType::Int);
    EXPECT_EQ(v.toInt(), 0);
}

TEST_F(ApplicationAdaptorTest, readDBusMethod_missingMethod_fallbackZero)
{
    // Arrange: 合法服务+接口但方法名不存在 → QDBus 错误回复合 →
    // 复现源码 144-148 行的无效回复合退分支
    if (!registerService())
        GTEST_SKIP() << "session bus unavailable";

    // Act
    QVariant v = ApplicationAdaptor::readDBusMethod(
        QString::fromLatin1(kSvc), QString::fromLatin1(kPath),
        QStringLiteral("com.deepin.movie.ut.adaptor"), "noSuchMethod");

    // Assert: 无效回复合 → 返回初值 0
    EXPECT_EQ(v.toInt(), 0);
}

}  // namespace

#include "test_dbusadaptor.moc"
