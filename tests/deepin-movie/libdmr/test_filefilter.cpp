// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <unistd.h>
#include <thread>
#include <gtest/gtest.h>
#include "application.h"
#include "filefilter.h"
#include "stub/stub.h"
using namespace dmr;

// ---------------------------------------------------------------------------
// Helper: create a unique temporary directory tree on disk for filterDir tests.
// Static + uniquely named so it does not collide with other suites.
// ---------------------------------------------------------------------------
static QDir filefilter_ext_makeTempTree()
{
    const QString root = QDir::tempPath() + "/filefilter_ext_XXXXXX";
    QByteArray tpl = root.toUtf8();
    QByteArray arr = mkdtemp(tpl.data());
    QDir base(QString::fromUtf8(arr));
    return base;
}

// ===========================================================================
// fileTransfer: the workhorse path-conversion routine. Pure logic, safe.
// ===========================================================================
TEST(filefilter_ext, fileTransfer_plainLocalPath)
{
    FileFilter *ff = FileFilter::instance();

    QUrl url = ff->fileTransfer("/tmp/no_such_file_xyz.mp4");
    // Non-existent path: produced via QUrl(strFile), scheme-less, path preserved.
    EXPECT_FALSE(url.isLocalFile());
    EXPECT_TRUE(url.toString().contains("no_such_file_xyz.mp4"));
}

TEST(filefilter_ext, fileTransfer_existingFile)
{
    FileFilter *ff = FileFilter::instance();

    // Create a real file so canonicalFilePath() applies.
    QString path = QDir::tempPath() + "/filefilter_ext_real.mp4";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("dummy");
            f.close();
        }
    }

    QUrl url = ff->fileTransfer(path);
    EXPECT_TRUE(url.isLocalFile());
    EXPECT_EQ(url.toLocalFile(), path);

    QFile::remove(path);
}

TEST(filefilter_ext, fileTransfer_fileSchemePrefix)
{
    FileFilter *ff = FileFilter::instance();

    // Existing file addressed with a file:// prefix should be canonicalized.
    QString path = QDir::tempPath() + "/filefilter_ext_scheme.mp4";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("x");
            f.close();
        }
    }

    QUrl url = ff->fileTransfer(QStringLiteral("file://") + path);
    EXPECT_TRUE(url.isLocalFile());
    EXPECT_EQ(url.toLocalFile(), path);

    QFile::remove(path);
}

TEST(filefilter_ext, fileTransfer_percentEncodedInFileScheme)
{
    FileFilter *ff = FileFilter::instance();

    // A path with a space, given via file:// with percent-encoding, must round-trip
    // to the literal filesystem path (regression guard for the '#'-stripping fix).
    QString path = QDir::tempPath() + "/filefilter_ext with space.mp4";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("y");
            f.close();
        }
    }
    QString encoded = QUrl::toPercentEncoding(path, "/");
    QUrl url = ff->fileTransfer(QStringLiteral("file://") + encoded);
    EXPECT_EQ(url.toLocalFile(), path);

    QFile::remove(path);
}

TEST(filefilter_ext, fileTransfer_nonexistentPathRemainsRaw)
{
    FileFilter *ff = FileFilter::instance();

    QUrl url = ff->fileTransfer("/definitely/not/here.mp4");
    // Falls into the else-branch: QUrl(strFile), not fromLocalFile.
    EXPECT_FALSE(url.isLocalFile());
}

TEST(filefilter_ext, fileTransfer_networkUrl)
{
    FileFilter *ff = FileFilter::instance();

    QUrl url = ff->fileTransfer("http://example.com/stream.mp4");
    EXPECT_FALSE(url.isLocalFile());
    EXPECT_EQ(url.scheme(), QStringLiteral("http"));
}

TEST(filefilter_ext, fileTransfer_emptyString)
{
    FileFilter *ff = FileFilter::instance();

    QUrl url = ff->fileTransfer(QString());
    // QFileInfo("") is neither file nor dir -> QUrl("") which is empty/invalid.
    EXPECT_TRUE(url.isEmpty());
}

// ===========================================================================
// isMediaFile: non-local URLs short-circuit to true (network path).
// Local-file probing depends on mpv/gst backends, so we only exercise the
// non-local fast path here.
// ===========================================================================
TEST(filefilter_ext, isMediaFile_nonLocalReturnsTrue)
{
    FileFilter *ff = FileFilter::instance();

    EXPECT_TRUE(ff->isMediaFile(QUrl("http://example.com/stream.mp4")));
    EXPECT_TRUE(ff->isMediaFile(QUrl("rtmp://example.com/live")));
    EXPECT_TRUE(ff->isMediaFile(QUrl("https://example.com/clip.mp4")));
}

TEST(filefilter_ext, isMediaFile_emptyNonLocalReturnsTrue)
{
    FileFilter *ff = FileFilter::instance();

    // QUrl() with no scheme is not a local file, so the early return fires.
    EXPECT_TRUE(ff->isMediaFile(QUrl()));
}

// ===========================================================================
// filterDir: recursively collects file URLs. Drives real QDir traversal on a
// synthetic tree on disk; stopThread controls early abort.
// ===========================================================================
TEST(filefilter_ext, filterDir_collectsFilesRecursively)
{
    FileFilter *ff = FileFilter::instance();

    QDir base = filefilter_ext_makeTempTree();
    QVERIFY(!base.absolutePath().isEmpty());

    // Tree:
    //   base/
    //     a.mp4
    //     sub/
    //       b.mp3
    //       deep/
    //         c.ass
    base.mkpath("sub/deep");
    QFile(base.absoluteFilePath("a.mp4")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/b.mp3")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/deep/c.ass")).open(QIODevice::WriteOnly);

    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_EQ(urls.size(), 3);

    // All entries must be local-file URLs.
    for (const QUrl &u : urls) {
        EXPECT_TRUE(u.isLocalFile());
    }

    base.removeRecursively();
}

TEST(filefilter_ext, filterDir_emptyDirectory)
{
    FileFilter *ff = FileFilter::instance();

    QDir base = filefilter_ext_makeTempTree();
    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_TRUE(urls.isEmpty());

    base.removeRecursively();
}

TEST(filefilter_ext, filterDir_nonexistentDirectory)
{
    FileFilter *ff = FileFilter::instance();

    QDir bogus("/no/such/dir/xyz_filefilter_ext");
    QList<QUrl> urls = ff->filterDir(bogus);
    EXPECT_TRUE(urls.isEmpty());
}

TEST(filefilter_ext, filterDir_stopThreadReturnsEmptyOnSubdir)
{
    FileFilter *ff = FileFilter::instance();

    QDir base = filefilter_ext_makeTempTree();
    base.mkpath("sub");
    QFile(base.absoluteFilePath("a.mp4")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/b.mp3")).open(QIODevice::WriteOnly);

    // Request a stop BEFORE entering the first subdirectory; the recursion
    // must bail out and return an empty list.
    ff->stopThread();
    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_TRUE(urls.isEmpty());

    base.removeRecursively();
}

// ===========================================================================
// instance: singleton identity.
// ===========================================================================
TEST(filefilter_ext, instance_returnsSamePointer)
{
    FileFilter *a = FileFilter::instance();
    FileFilter *b = FileFilter::instance();
    EXPECT_EQ(a, b);
    EXPECT_NE(a, nullptr);
}

// ===========================================================================
// Static GStreamer callbacks: finished() quits the loop, discovered() handles
// the error/OK branches. We do not own a live GstDiscovererInfo in tests, so
// we exercise finished() (which only needs the loop pointer).
// ===========================================================================
TEST(filefilter_ext, finished_quitsLoop)
{
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    ASSERT_NE(loop, nullptr);

    // Run the loop on a background thread so finished() can quit it.
    std::thread t([loop]() {
        // Give the main thread a moment to enter g_main_loop_run().
        usleep(20000);
        FileFilter::finished(nullptr, loop);
    });
    g_main_loop_run(loop);   // returns once finished() quits it
    t.join();

    EXPECT_FALSE(g_main_loop_is_running(loop));
    g_main_loop_unref(loop);
}

// ===========================================================================
// isFormatSupported: file-open failure must yield false (defensive, no real
// media decoding involved).
// ===========================================================================
TEST(filefilter_ext, isFormatSupported_nonexistentFileReturnsFalse)
{
    FileFilter *ff = FileFilter::instance();

    EXPECT_FALSE(ff->isFormatSupported(QUrl::fromLocalFile("/no/such/file_xyz.mp4")));
}

TEST(filefilter_ext, isFormatSupported_emptyUrlReturnsFalse)
{
    FileFilter *ff = FileFilter::instance();

    // Empty local file path -> avformat_open_input fails -> false.
    EXPECT_FALSE(ff->isFormatSupported(QUrl::fromLocalFile(QString())));
}

// ===========================================================================
// Type-judgement functions via FFmpeg path: stubbed to avoid real decoding.
// We stub the private typeJudgeByFFmpeg indirectly by stubbing
// CompositingManager::isMpvExists() to control which backend is taken, then
// exercise isAudio/isVideo/isSubtitle on a URL whose mime is not mpegurl.
// Because isMpvExists() is a static member, we stub it through ADDR on the
// static function symbol.
// ===========================================================================
static bool filefilter_ext_stub_isMpvExists_true()
{
    return true;
}

static bool filefilter_ext_stub_isMpvExists_false()
{
    return false;
}

TEST(filefilter_ext, compositingMpvFlagRouting_smoke)
{
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_true);
    EXPECT_TRUE(CompositingManager::isMpvExists());
    stub.reset(ADDR(CompositingManager, isMpvExists));

    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_false);
    EXPECT_FALSE(CompositingManager::isMpvExists());
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

// isAudio/isVideo/isSubtitle on a non-existent local file: the FFmpeg path
// fails to open and returns MediaType::Other, so all three return false.
// This exercises the cache-clear logic in isAudio as well.
TEST(filefilter_ext, isAudio_nonexistentLocalReturnsFalse)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/audio_xyz.mp3");

    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_true);

    EXPECT_FALSE(ff->isAudio(url));
    // Second call hits the cache populated by the first call.
    EXPECT_FALSE(ff->isAudio(url));

    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(filefilter_ext, isVideo_nonexistentLocalReturnsFalse)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/video_xyz.mp4");

    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_true);
    EXPECT_FALSE(ff->isVideo(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(filefilter_ext, isSubtitle_nonexistentLocalReturnsFalse)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/sub_xyz.ass");

    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_true);
    EXPECT_FALSE(ff->isSubtitle(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

// Gst backend path: typeJudgeByGst returns Other early when the mime is not
// audio/* or video/*, so isAudio/isVideo/isSubtitle must all be false for a
// non-media local file even without a real discoverer.
TEST(filefilter_ext, gstBackend_nonMediaLocalAllFalse)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/text_xyz.txt");

    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), filefilter_ext_stub_isMpvExists_false);
    EXPECT_FALSE(ff->isAudio(url));
    EXPECT_FALSE(ff->isVideo(url));
    EXPECT_FALSE(ff->isSubtitle(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

// Keep the QApplication / MainWindow alive across the suite; just touching the
// accessor ensures the runtime is initialized like the other suites.
TEST(filefilter_ext, mainWindowAccessible)
{
    MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    QTest::qWait(10);
}
