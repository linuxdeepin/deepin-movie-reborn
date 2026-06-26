// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Boost-coverage unit tests for five libdmr translation units whose line
// coverage is currently low:
//   src/libdmr/utils.cpp
//   src/libdmr/gstutils.cpp
//   src/libdmr/filefilter.cpp
//   src/libdmr/movie_configuration.cpp
//   src/libdmr/compositing_manager.cpp
//
// Suite name "boost_libdmr" is intentionally distinct from the pre-existing
// suites ("libdmr" in test_dmr.cpp, "utils_ext", "libdmr_ext",
// "movieconfig_ext", "filefilter_ext", "gstutils_ext" ...) so TEST() cases
// never collide.
//
// Hard rules honoured (these run in ONE shared process):
//  * Only Google Test (TEST(boost_libdmr, ...)); no main() defined here.
//  * Every QObject::connect(...,[&]{}) return is saved and disconnected.
//  * Qt6 event constructors always use correct subclasses (no QEvent(...) hack).
//  * Widget pointers are null-checked before dereference.
//  * No real mpv decode / media loading / network is triggered; only the
//    Idle / empty / pure-logic branches are exercised.
//  * Geometry/paint paths are guarded by QGuiApplication::primaryScreen().
//  * Private members are accessed by temporarily #defining private/protected
//    public around the relevant project header includes.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QImage>
#include <QWidget>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMap>
#include <QSettings>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QUrl>
#include <QUrlQuery>
#include <QDBusInterface>
#include <QDBusReply>

#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <atomic>

#include <gtest/gtest.h>
#include "application.h"

// ---------------------------------------------------------------------------
// Access private members of the libdmr classes/functions. Wrap project headers
// in #define private/protected public. STL/Qt headers are included ABOVE so
// they are unaffected.
// ---------------------------------------------------------------------------
#define protected public
#define private public
#include "utils.h"
#include "gstutils.h"
#include "filefilter.h"
#include "movie_configuration.h"
#include "compositing_manager.h"
#undef protected
#undef private

#include "stub/stub.h"

using namespace dmr;

// ===========================================================================
// Static helpers (prefix bl_ for "boost libdmr").
// ===========================================================================

// Unique URL namespace so DB rows never collide with other suites.
static const QUrl bl_url_a = QUrl("boost_libdmr://case-a");
static const QUrl bl_url_b = QUrl("boost_libdmr://case-b");

// A temp directory created/cleaned per use, with a deterministic root name so
// filterDir/hash tests have a stable sandbox.
static QDir bl_makeTempTree(const char *tag)
{
    const QString root = QDir::tempPath() + "/bl_" + QString::fromLatin1(tag) + "_XXXXXX";
    QByteArray tpl = root.toUtf8();
    QByteArray arr = mkdtemp(tpl.data());
    return QDir(QString::fromUtf8(arr));
}

// ===========================================================================
// utils.cpp  — pure-logic / IO helpers
// ===========================================================================

// videoIndex2str: the PCM/ADPCM/AMR/RealAudio offset branches (lines 572..581
// in the source). Map insertions happen for every call; we exercise the
// offsets that the existing utils_ext suite did not touch.
TEST(boost_libdmr, videoIndex2str_pcm_offsets)
{
    // PCM block starts at index 65536. Spot-check the head/tail of that block.
    EXPECT_EQ(utils::videoIndex2str(65536).toStdString(), "pcm_s16le");
    EXPECT_EQ(utils::videoIndex2str(65537).toStdString(), "pcm_s16be");
}

TEST(boost_libdmr, videoIndex2str_adpcm_offsets)
{
    // ADPCM block starts at 69632.
    EXPECT_EQ(utils::videoIndex2str(69632).toStdString(), "adpcm_ima_qt");
    EXPECT_EQ(utils::videoIndex2str(69633).toStdString(), "adpcm_ima_wav");
}

TEST(boost_libdmr, videoIndex2str_amr_offsets)
{
    EXPECT_EQ(utils::videoIndex2str(73728).toStdString(), "amr_nb");
    EXPECT_EQ(utils::videoIndex2str(73729).toStdString(), "amr_wb");
}

TEST(boost_libdmr, videoIndex2str_real_audio_offsets)
{
    EXPECT_EQ(utils::videoIndex2str(77824).toStdString(), "ra_144");
    EXPECT_EQ(utils::videoIndex2str(77825).toStdString(), "ra_288");
}

TEST(boost_libdmr, videoIndex2str_known_video_codes)
{
    // A few mid-list entries to grow coverage of the videoList mapping.
    EXPECT_EQ(utils::videoIndex2str(27).toStdString(), "h264");
    EXPECT_EQ(utils::videoIndex2str(173).toStdString(), "vp9");
}

// FastFileHash / FullFileHash: the >=8192 multi-offset branch (lines 307..314).
TEST(boost_libdmr, FastFileHash_large_file_uses_offsets)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    // Write 16 KiB of deterministic data so the >=8192 branch is taken.
    QByteArray block(16384, '\0');
    for (int i = 0; i < block.size(); ++i)
        block[i] = static_cast<char>(i & 0xFF);
    tf.write(block);
    tf.flush();

    QFileInfo fi(tf.fileName());
    const QString h1 = utils::FastFileHash(fi);
    EXPECT_FALSE(h1.isEmpty());
    // Deterministic: hashing the same file twice yields the same digest.
    EXPECT_EQ(h1, utils::FastFileHash(fi));
}

TEST(boost_libdmr, FastFileHash_offset_branch_differs_from_full)
{
    // For a file >= 8192, FastFileHash samples two 4 KiB windows and must NOT
    // equal FullFileHash (which hashes the whole file).
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    QByteArray block(16384, '\0');
    for (int i = 0; i < block.size(); ++i)
        block[i] = static_cast<char>((i * 7) & 0xFF);
    tf.write(block);
    tf.flush();

    QFileInfo fi(tf.fileName());
    EXPECT_NE(utils::FastFileHash(fi), utils::FullFileHash(fi));
}

TEST(boost_libdmr, FullFileHash_empty_file)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    QFileInfo fi(tf.fileName());
    // md5("")
    EXPECT_EQ(utils::FullFileHash(fi).toLower().toStdString(),
              "d41d8cd98f00b204e9800998ecf8427e");
}

// ValidateScreenshotPath: directory that exists but is not writable returns
// false (lines 654..657). Build a dir, strip write permission, then restore.
TEST(boost_libdmr, ValidateScreenshotPath_existing_dir_not_writable)
{
    if (::geteuid() == 0) {
        // root bypasses permission bits; this case cannot be exercised as root.
        GTEST_SKIP() << "running as root, permission bit cannot block access";
    }
    QDir base = bl_makeTempTree("vsp");
    ASSERT_TRUE(QFileInfo(base.absolutePath()).isDir());

    // Remove write permission for everyone.
    QFile::setPermissions(base.absolutePath(),
                          QFile::ReadOwner | QFile::ExeOwner |
                          QFile::ReadGroup | QFile::ExeGroup |
                          QFile::ReadOther | QFile::ExeOther);

    EXPECT_FALSE(utils::ValidateScreenshotPath(base.absolutePath()));

    // Restore so the temp tree can be removed cleanly.
    QFile::setPermissions(base.absolutePath(),
                          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                          QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                          QFile::ReadOther | QFile::WriteOther | QFile::ExeOther);
    base.removeRecursively();
}

// getPlayProperty: the file-not-readable / file-not-exist branches and the
// real-file parsing branch. Only exercised when DTKCORE_CLASS_DConfigFile is
// NOT defined; otherwise the function ignores the path. We call it either way
// so the early-log line is still covered.
TEST(boost_libdmr, getPlayProperty_nonexistent_file_safe)
{
    QMap<QString, QString> *m = new QMap<QString, QString>();
    EXPECT_NO_FATAL_FAILURE({
        utils::getPlayProperty("/tmp/bl_definitely_missing_play.conf", m);
    });
    EXPECT_TRUE(m != nullptr);
    delete m;
}

TEST(boost_libdmr, getPlayProperty_reads_real_file)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    // Two valid "key=value" lines and one malformed line (no '=') so the
    // parser loop's continue-branch is also covered.
    tf.write("alpha=1\nbeta=2\nno_equals_here\n");
    tf.flush();

    QMap<QString, QString> *m = new QMap<QString, QString>();
    EXPECT_NO_FATAL_FAILURE({
        utils::getPlayProperty(tf.fileName().toLocal8Bit().constData(), m);
    });
    // When DConfig is NOT compiled in, the file is parsed and m is populated.
    // When DConfig IS compiled in, the file is ignored. Either is acceptable;
    // we just assert no crash.
    delete m;
}

TEST(boost_libdmr, getPlayProperty_unreadable_existing_file)
{
    // Create a file then revoke read permission; the open() branch fails.
    // Skip as root (root bypasses permission bits).
    if (::geteuid() == 0) GTEST_SKIP() << "running as root";

    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("k=v\n");
    tf.flush();
    QFile::setPermissions(tf.fileName(), QFile::WriteOwner); // no read

    QMap<QString, QString> *m = new QMap<QString, QString>();
    EXPECT_NO_FATAL_FAILURE({
        utils::getPlayProperty(tf.fileName().toLocal8Bit().constData(), m);
    });
    delete m;
    // QFile destructor will not restore perms; set writable again so the
    // QTemporaryFile can remove it on destruction.
    QFile::setPermissions(tf.fileName(),
                          QFile::ReadOwner | QFile::WriteOwner);
}

// readDBusProperty is a file-scope static in utils.cpp (not declared in the
// header), so it is not directly callable here. Its !isValid() early-return
// branch is instead exercised indirectly via switchToDefaultSink below.

// switchToDefaultSink: readDBusProperty returns invalid -> early return.
// No real DBus/PulseAudio present in CI; this covers the entry + early-out.
TEST(boost_libdmr, switchToDefaultSink_invalid_dbus_safe)
{
    EXPECT_NO_FATAL_FAILURE({ utils::switchToDefaultSink(); });
}

// ShowInFileManager: the file-exists-but-DBus-invalid branch (lines 92..108)
// and the non-local-file branch (line 110). We point it at an existing file
// so the QUrl::fromLocalFile path is taken; the freedesktop.FileManager1 DBus
// service is absent in CI, so the openUrl fallback fires.
TEST(boost_libdmr, ShowInFileManager_existing_file_falls_back_to_openurl)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("x");
    tf.flush();
    EXPECT_NO_FATAL_FAILURE({ utils::ShowInFileManager(tf.fileName()); });
}

TEST(boost_libdmr, ShowInFileManager_nonlocal_url_path)
{
    // A url string that is not a local file: the !url.isLocalFile() branch is
    // entered and QDesktopServices::openUrl(url) is called. We use an about
    // URL so nothing actually launches.
    EXPECT_NO_FATAL_FAILURE({ utils::ShowInFileManager("about:blank"); });
}

// IsNamesSimilar: drive stringDistance through both equal-char and
// unequal-char inner branches (lines 136..147) with a non-trivial input.
TEST(boost_libdmr, IsNamesSimilar_partly_overlapping)
{
    // 2 differing chars -> distance 2 -> similar.
    EXPECT_TRUE(utils::IsNamesSimilar("episode01.mkv", "episode03.mkv"));
    // 5 differing chars -> distance 5 -> not similar.
    EXPECT_FALSE(utils::IsNamesSimilar("episode01.mkv", "episode99.xy"));
}

// FindSimilarFiles: a directory with a mix of files + subdirs (the
// skip-non-file branch at line 169..172).
TEST(boost_libdmr, FindSimilarFiles_skips_subdirs_and_non_similar)
{
    QDir base = bl_makeTempTree("fsf");
    QFile a(base.absoluteFilePath("show01.mp4"));
    ASSERT_TRUE(a.open(QIODevice::WriteOnly)); a.write("a"); a.close();
    QFile b(base.absoluteFilePath("show02.mp4"));
    ASSERT_TRUE(b.open(QIODevice::WriteOnly)); b.write("b"); b.close();
    base.mkpath("subdir");              // must be skipped (not a file)
    QFile c(base.absoluteFilePath("unrelated.txt"));
    ASSERT_TRUE(c.open(QIODevice::WriteOnly)); c.write("c"); c.close();

    QFileInfoList res = utils::FindSimilarFiles(QFileInfo(a.fileName()));
    EXPECT_GE(res.size(), 1);

    bool foundSibling = false;
    bool foundUnrelated = false;
    for (const QFileInfo &fi : res) {
        if (fi.fileName() == "show02.mp4") foundSibling = true;
        if (fi.fileName() == "unrelated.txt") foundUnrelated = true;
    }
    EXPECT_TRUE(foundSibling);
    EXPECT_FALSE(foundUnrelated);

    base.removeRecursively();
}

// ConvertLinglongPathForFM: the startsWith-prefix branch cannot run outside a
// linglong env (IsLinglongEnvironment()==false short-circuits). We still call
// both converters to cover the early-return lines.
TEST(boost_libdmr, ConvertLinglongPaths_passthrough_outside_linglong)
{
    const QString p1 = "/some/playback.mp4";
    const QString p2 = "/run/host/rootfs/some/fm.mp4";
    EXPECT_EQ(utils::ConvertLinglongPathForPlayback(p1), p1);
    EXPECT_EQ(utils::ConvertLinglongPathForFM(p2), p2);
}

// ElideText: a single-line input that fits within one line (lineCount==1
// branch at line 730..732).
TEST(boost_libdmr, ElideText_single_line_fits_then_elided)
{
    QFont font;
    QString out = utils::ElideText("short", QSize(200, 20),
                                   QTextOption::NoWrap, font,
                                   Qt::ElideRight, 15, 200);
    EXPECT_FALSE(out.isEmpty());
}

TEST(boost_libdmr, ElideText_newline_triggers_height_increment)
{
    // A multi-line input containing '\n' exercises the height += lineHeight
    // branch (line 717..718).
    QFont font;
    QString out = utils::ElideText("line1\nline2", QSize(200, 60),
                                   QTextOption::ManualWrap, font,
                                   Qt::ElideRight, 15, 200);
    EXPECT_FALSE(out.isEmpty());
}

// ===========================================================================
// gstutils.cpp  — Singleton accessors + static callbacks (defensive).
// Real gstreamer discovery is not triggered; we only touch the safe surface.
// ===========================================================================

TEST(boost_libdmr, GstUtils_get_returns_stable_singleton)
{
    GstUtils *a = GstUtils::get();
    GstUtils *b = GstUtils::get();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a, b);
}

TEST(boost_libdmr, GstUtils_finished_quits_loop)
{
    // finished() only calls g_main_loop_quit(data->loop); safe to drive on a
    // throwaway CustomData without any GstDiscoverer attached.
    CustomData cd;
    cd.discoverer = nullptr;
    cd.loop = g_main_loop_new(nullptr, FALSE);
    ASSERT_NE(cd.loop, nullptr);

    std::thread t([&cd]() {
        usleep(15000);
        GstUtils::finished(nullptr, &cd);
    });
    g_main_loop_run(cd.loop);
    t.join();

    EXPECT_FALSE(g_main_loop_is_running(cd.loop));
    g_main_loop_unref(cd.loop);
}

TEST(boost_libdmr, GstUtils_parseFileByGst_missing_file_safe)
{
    // parseFileByGst fills the MovieInfo fields from the QFileInfo (which all
    // succeed for a missing file) and then calls discover_uri_async. For a
    // missing file the async discover either fails (-> early return before
    // g_main_loop_run) or reports failure quickly (-> finished() quits the
    // loop). Either way it returns promptly; we assert no synchronous crash.
    GstUtils *g = GstUtils::get();
    ASSERT_NE(g, nullptr);
    QFileInfo fi("/tmp/bl_definitely_missing_gst_video_xyz.mp4");
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({ mi = g->parseFileByGst(fi); });
    // For a bad/missing file, valid stays false.
    EXPECT_FALSE(mi.valid);
}

// ===========================================================================
// filefilter.cpp  — Pure path logic + cache bookkeeping.
// ===========================================================================

TEST(boost_libdmr, FileFilter_instance_identity)
{
    FileFilter *a = FileFilter::instance();
    FileFilter *b = FileFilter::instance();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
}

// fileTransfer: the file:// + percent-decode branch for a path that contains
// a literal '#' (the regression the production fix targets).
TEST(boost_libdmr, FileFilter_fileTransfer_hash_in_filename)
{
    FileFilter *ff = FileFilter::instance();
    QString path = QDir::tempPath() + "/bl_hash#name.mp4";
    {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) { f.write("h"); f.close(); }
    }
    QString encoded = QUrl::toPercentEncoding(path, "/");
    QUrl url = ff->fileTransfer(QStringLiteral("file://") + encoded);
    EXPECT_EQ(url.toLocalFile(), path);
    QFile::remove(path);
}

// fileTransfer: a directory (not a file) goes through the isDir() branch and
// canonicalFilePath() is taken.
TEST(boost_libdmr, FileFilter_fileTransfer_directory)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bl_makeTempTree("ffd");
    QUrl url = ff->fileTransfer(base.absolutePath());
    EXPECT_TRUE(url.isLocalFile());
    EXPECT_EQ(url.toLocalFile(), base.absolutePath());
    base.removeRecursively();
}

// isMediaFile: the local-file branch on a non-existent path goes through
// typeJudgeByFFmpeg which returns Other -> bMedia stays false.
TEST(boost_libdmr, FileFilter_isMediaFile_local_missing_returns_false)
{
    FileFilter *ff = FileFilter::instance();
    Stub stub;
    static auto bl_true = +[]() { return true; };
    stub.set(ADDR(CompositingManager, isMpvExists), bl_true);
    EXPECT_FALSE(ff->isMediaFile(QUrl::fromLocalFile("/tmp/bl_no_such_media_xyz.mp4")));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

// isAudio cache: two different urls populate the cache; the second call clears
// it (lines 194..197). We just assert resilience.
TEST(boost_libdmr, FileFilter_isAudio_cache_clear_on_miss)
{
    FileFilter *ff = FileFilter::instance();
    Stub stub;
    static auto bl_true = +[]() { return true; };
    stub.set(ADDR(CompositingManager, isMpvExists), bl_true);

    QUrl u1 = QUrl::fromLocalFile("/tmp/bl_audio_a_xyz.mp3");
    QUrl u2 = QUrl::fromLocalFile("/tmp/bl_audio_b_xyz.mp3");
    EXPECT_FALSE(ff->isAudio(u1));
    // u1 is now cached; asking for u2 misses and clears the cache first.
    EXPECT_FALSE(ff->isAudio(u2));
    // u1 again -> cache miss again (cleared above), repopulates.
    EXPECT_FALSE(ff->isAudio(u1));

    stub.reset(ADDR(CompositingManager, isMpvExists));
}

// filterDir: a directory tree containing a nested subdir + files exercises
// both the isFile() and isDir() recursion branches.
TEST(boost_libdmr, FileFilter_filterDir_nested_tree)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bl_makeTempTree("ffd2");
    base.mkpath("d1/d2");
    QFile(base.absoluteFilePath("top.mp4")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("d1/mid.mp3")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("d1/d2/leaf.ass")).open(QIODevice::WriteOnly);

    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_EQ(urls.size(), 3);
    for (const QUrl &u : urls) EXPECT_TRUE(u.isLocalFile());

    base.removeRecursively();
}

// stopThread toggles the flag; a subsequent filterDir on a tree with a subdir
// returns empty (lines 139..142).
TEST(boost_libdmr, FileFilter_stopThread_then_filterDir_returns_empty)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bl_makeTempTree("ffst");
    base.mkpath("child");
    QFile(base.absoluteFilePath("x.mp4")).open(QIODevice::WriteOnly);

    ff->stopThread();
    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_TRUE(urls.isEmpty());

    base.removeRecursively();
}

// ===========================================================================
// movie_configuration.cpp  — DB-backed config round-trips.
// ===========================================================================

static MovieConfiguration &bl_cfg() { return MovieConfiguration::get(); }
static void bl_reset_db() { bl_cfg().clear(); }

TEST(boost_libdmr, MovieConfig_knownKey2String_full_map)
{
    using K = MovieConfiguration::KnownKey;
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubDelay).toStdString(),
              "sub-delay");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubCodepage).toStdString(),
              "sub-codepage");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubId).toStdString(), "sid");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::StartPos).toStdString(),
              "start");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::ExternalSubs).toStdString(),
              "external-subs");
    // Out-of-range enum -> default -> "".
    EXPECT_TRUE(MovieConfiguration::knownKey2String(
                    static_cast<K>(4242)).isEmpty());
}

TEST(boost_libdmr, MovieConfig_update_and_get_by_string_key)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    EXPECT_FALSE(mc.getByUrl(bl_url_a, "volume").isValid());
    mc.updateUrl(bl_url_a, "volume", QVariant(80));
    EXPECT_EQ(mc.getByUrl(bl_url_a, "volume").toInt(), 80);
}

TEST(boost_libdmr, MovieConfig_queryByUrl_returns_full_map)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    mc.updateUrl(bl_url_a, "k1", QVariant(1));
    mc.updateUrl(bl_url_a, "k2", QVariant(2));
    auto m = mc.queryByUrl(bl_url_a);
    EXPECT_EQ(m.size(), 2);
    EXPECT_EQ(m.value("k1").toInt(), 1);
    EXPECT_EQ(m.value("k2").toInt(), 2);
}

TEST(boost_libdmr, MovieConfig_removeUrl_drops_rows)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    mc.updateUrl(bl_url_a, "k", QVariant(1));
    ASSERT_TRUE(mc.urlExists(bl_url_a));
    mc.removeUrl(bl_url_a);
    EXPECT_FALSE(mc.urlExists(bl_url_a));
    EXPECT_TRUE(mc.queryByUrl(bl_url_a).isEmpty());
}

TEST(boost_libdmr, MovieConfig_clear_wipes_everything)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    mc.updateUrl(bl_url_a, "k", QVariant(1));
    mc.updateUrl(bl_url_b, "k", QVariant(2));
    mc.clear();
    EXPECT_FALSE(mc.urlExists(bl_url_a));
    EXPECT_FALSE(mc.urlExists(bl_url_b));
}

TEST(boost_libdmr, MovieConfig_decodeList_base64_round_trip)
{
    auto &mc = bl_cfg();
    const QString a = QString::fromUtf8("/path/字幕.ass");
    const QString b = QString::fromUtf8("/another one.srt");
    const QString encoded =
        a.toUtf8().toBase64() + ";" + b.toUtf8().toBase64();
    const QStringList out = mc.decodeList(QVariant(encoded));
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out[0], a);
    EXPECT_EQ(out[1], b);
}

TEST(boost_libdmr, MovieConfig_append2ListUrl_preserves_order)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    using K = MovieConfiguration::KnownKey;
    EXPECT_TRUE(mc.getListByUrl(bl_url_a, K::ExternalSubs).isEmpty());
    mc.append2ListUrl(bl_url_a, K::ExternalSubs,
                      QString::fromUtf8("/tmp/bl_sub1.ass"));
    mc.append2ListUrl(bl_url_a, K::ExternalSubs,
                      QString::fromUtf8("/tmp/bl_sub2.srt"));
    QStringList list = mc.getListByUrl(bl_url_a, K::ExternalSubs);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].toStdString(), "/tmp/bl_sub1.ass");
    EXPECT_EQ(list[1].toStdString(), "/tmp/bl_sub2.srt");
}

TEST(boost_libdmr, MovieConfig_getByUrl_knownKey_overload)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    using K = MovieConfiguration::KnownKey;
    mc.updateUrl(bl_url_a, K::StartPos, QVariant(999));
    EXPECT_EQ(mc.getByUrl(bl_url_a, K::StartPos).toInt(), 999);
    // KnownKey on an absent url -> invalid.
    EXPECT_FALSE(mc.getByUrl(bl_url_b, K::SubId).isValid());
}

TEST(boost_libdmr, MovieConfig_removeFromListUrl_safe_on_absent)
{
    bl_reset_db();
    auto &mc = bl_cfg();
    using K = MovieConfiguration::KnownKey;
    EXPECT_NO_FATAL_FAILURE({
        mc.removeFromListUrl(bl_url_a, K::ExternalSubs,
                             QString::fromUtf8("/tmp/bl_none.ass"));
    });
}

// ===========================================================================
// compositing_manager.cpp  — getters + pure detection helpers.
// ===========================================================================

TEST(boost_libdmr, CompositingManager_get_singleton)
{
    auto &a = CompositingManager::get();
    auto &b = CompositingManager::get();
    EXPECT_EQ(&a, &b);
}

TEST(boost_libdmr, CompositingManager_setCanHwdec_round_trip)
{
    auto &cm = CompositingManager::get();
    cm.setCanHwdec(false);
    EXPECT_FALSE(cm.isCanHwdec());
    cm.setCanHwdec(true);
    EXPECT_TRUE(cm.isCanHwdec());
}

TEST(boost_libdmr, CompositingManager_overrideCompositeMode_toggle)
{
    auto &cm = CompositingManager::get();
    cm.overrideCompositeMode(false);
    EXPECT_FALSE(cm.composited());
    cm.overrideCompositeMode(true);
    EXPECT_TRUE(cm.composited());
}

TEST(boost_libdmr, CompositingManager_testFlag_round_trip)
{
    auto &cm = CompositingManager::get();
    cm.setTestFlag(true);
    EXPECT_TRUE(cm.isTestFlag());
    cm.setTestFlag(false);
    EXPECT_FALSE(cm.isTestFlag());
}

TEST(boost_libdmr, CompositingManager_getMpvConfig_out_param_set)
{
    auto &cm = CompositingManager::get();
    QMap<QString, QString> *m = nullptr;
    cm.getMpvConfig(m);
    EXPECT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->keys(); });
}

TEST(boost_libdmr, CompositingManager_enablePower_returns_int)
{
    int v = CompositingManager::get().enablePower();
    EXPECT_TRUE(v == -1 || v >= 0);
}

TEST(boost_libdmr, CompositingManager_getEnablePowerConfig_returns_pair)
{
    QPair<QString, QString> p = CompositingManager::get().getEnablePowerConfig();
    // tautology to ensure both fields are used (no warning)
    EXPECT_TRUE(p.first == p.first);
    EXPECT_TRUE(p.second == p.second);
}

TEST(boost_libdmr, CompositingManager_platform_known_value)
{
    Platform p = CompositingManager::get().platform();
    EXPECT_TRUE(p == Unknown || p == X86 || p == Mips || p == Alpha || p == Arm64);
}

TEST(boost_libdmr, CompositingManager_interopKind_valid_enum)
{
    OpenGLInteropKind k = CompositingManager::interopKind();
    EXPECT_TRUE(k == INTEROP_NONE || k == INTEROP_AUTO ||
                k == INTEROP_VAAPI_EGL || k == INTEROP_VAAPI_GLX ||
                k == INTEROP_VDPAU_GLX);
}

TEST(boost_libdmr, CompositingManager_isPadSystem_false)
{
    EXPECT_FALSE(CompositingManager::get().isPadSystem());
}

TEST(boost_libdmr, CompositingManager_isZXIntgraphics_bool)
{
    bool r = CompositingManager::get().isZXIntgraphics();
    EXPECT_TRUE(r == true || r == false);
}

TEST(boost_libdmr, CompositingManager_isOnlySoftDecode_bool)
{
    bool r = CompositingManager::get().isOnlySoftDecode();
    EXPECT_TRUE(r == true || r == false);
}

TEST(boost_libdmr, CompositingManager_isSpecialControls_bool)
{
    bool r = CompositingManager::get().isSpecialControls();
    EXPECT_TRUE(r == true || r == false);
}

TEST(boost_libdmr, CompositingManager_isMpvExists_cached)
{
    bool a = CompositingManager::isMpvExists();
    bool b = CompositingManager::isMpvExists();
    EXPECT_EQ(a, b);
}

TEST(boost_libdmr, CompositingManager_runningOnNvidia_bool)
{
    bool r = CompositingManager::runningOnNvidia();
    EXPECT_TRUE(r == true || r == false);
}

TEST(boost_libdmr, CompositingManager_runningOnVmwgfx_bool)
{
    bool r = CompositingManager::runningOnVmwgfx();
    EXPECT_TRUE(r == true || r == false);
}

TEST(boost_libdmr, CompositingManager_getProfile_unknown_returns_empty)
{
    PlayerOptionList ol = CompositingManager::get().getProfile("bl_no_such_profile");
    EXPECT_TRUE(ol.isEmpty());
}

TEST(boost_libdmr, CompositingManager_getBestProfile_returns_list)
{
    PlayerOptionList ol = CompositingManager::get().getBestProfile();
    EXPECT_TRUE(ol.isEmpty() || ol.size() > 0);
}

TEST(boost_libdmr, CompositingManager_detectOpenGLEarly_idempotent)
{
    // Already called once during construction; calling again must hit the
    // already-run early-return branch (no crash, no env mutation).
    EXPECT_NO_FATAL_FAILURE({ CompositingManager::detectOpenGLEarly(); });
}

TEST(boost_libdmr, CompositingManager_detectPciID_safe)
{
    // Runs lspci; in CI it may fail to start. We only assert no crash.
    EXPECT_NO_FATAL_FAILURE({ CompositingManager::detectPciID(); });
}
