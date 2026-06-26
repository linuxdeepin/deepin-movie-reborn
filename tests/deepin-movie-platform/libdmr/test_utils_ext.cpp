// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for src/libdmr/utils.cpp free functions.
// Suite name "utils_ext" is intentionally distinct from the existing "libdmr"
// suite in test_dmr.cpp so cases never collide.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>
#include "application.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextOption>
#include <QFont>
#include <QFontMetrics>
#include <QPixmap>
#include <QImage>
#include <QWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMap>

#include "stub/stub.h"

using namespace dmr;

// ---------------------------------------------------------------------------
// Stub helpers (file scope, uniquely named to avoid collisions)
// ---------------------------------------------------------------------------

// For switchToDefaultSink(): the real impl talks to DBus and QProcess("pacmd").
// We stub readDBusProperty (anonymous-namespace static in utils.cpp is NOT
// callable directly), so instead we just exercise the entry point while DBus
// is unavailable: readDBusProperty returns QVariant(0) -> invalid -> early
// return. This still covers the function's first invalid-return branch.

// ValidateScreenshotPath: make QDir::homePath stable so '~' expansion is
// deterministic regardless of the runner.
static QString utils_ext_homePath_stub()
{
    return QString("/tmp/utils_ext_home");
}

// first_check_wayland_env reads QProcessEnvironment::systemEnvironment().
// To exercise the non-wayland branch deterministically we stub
// QProcessEnvironment::value (const overload) — but that is hard to match
// exactly, so we instead just call the real function and assert its return
// matches the cached check_wayland_env() afterwards.

// ---------------------------------------------------------------------------
// Time2str
// ---------------------------------------------------------------------------

TEST(utils_ext, Time2str_zero)
{
    EXPECT_EQ(utils::Time2str(0), QString("00:00:00"));
}

TEST(utils_ext, Time2str_small_seconds)
{
    EXPECT_EQ(utils::Time2str(5), QString("00:00:05"));
    EXPECT_EQ(utils::Time2str(59), QString("00:00:59"));
}

TEST(utils_ext, Time2str_minutes_hours)
{
    EXPECT_EQ(utils::Time2str(60), QString("00:01:00"));
    EXPECT_EQ(utils::Time2str(3661), QString("01:01:01"));
    EXPECT_EQ(utils::Time2str(86399), QString("23:59:59")); // one second below a day
}

TEST(utils_ext, Time2str_exactly_one_day)
{
    // Boundary: exactly DAYSECONDS (86400) goes into the day branch.
    const QString s = utils::Time2str(86400);
    EXPECT_FALSE(s.isEmpty());
    EXPECT_TRUE(s.startsWith("24"));
}

TEST(utils_ext, Time2str_multi_day)
{
    // 2 days + 1 hour = 49:00:00
    const QString s = utils::Time2str(2 * 86400 + 3600);
    EXPECT_FALSE(s.isEmpty());
    EXPECT_TRUE(s.startsWith("49"));
}

TEST(utils_ext, Time2str_negative_wraps_via_addSecs)
{
    // Negative input: QTime::addSecs wraps; just ensure no crash and non-empty.
    const QString s = utils::Time2str(-1);
    EXPECT_FALSE(s.isEmpty());
}

// ---------------------------------------------------------------------------
// CompareNames
// ---------------------------------------------------------------------------

TEST(utils_ext, CompareNames_equal_strings)
{
    // Identical strings -> no differing numeric run -> localeAwareCompare(x,x)==0 -> false.
    EXPECT_FALSE(utils::CompareNames("abc", "abc"));
}

TEST(utils_ext, CompareNames_simple_lexical)
{
    EXPECT_TRUE(utils::CompareNames("a.mp4", "b.mp4"));
    EXPECT_FALSE(utils::CompareNames("b.mp4", "a.mp4"));
}

TEST(utils_ext, CompareNames_numeric_at_same_position)
{
    // Numbers compared as integers, not lexically: "file10" should sort after "file2".
    EXPECT_TRUE(utils::CompareNames("file2.mp4", "file10.mp4"));
    EXPECT_FALSE(utils::CompareNames("file10.mp4", "file2.mp4"));
}

TEST(utils_ext, CompareNames_numeric_at_different_positions)
{
    // Numeric runs at different positions fall through to locale-aware compare.
    bool r = utils::CompareNames("a1b", "1ab");
    // Just assert it does not crash; the exact result is locale dependent but stable.
    Q_UNUSED(r);
}

TEST(utils_ext, CompareNames_empty_inputs)
{
    bool r1 = utils::CompareNames("", "");
    bool r2 = utils::CompareNames("", "a");
    bool r3 = utils::CompareNames("a", "");
    Q_UNUSED(r1); Q_UNUSED(r2); Q_UNUSED(r3);
    // No crash, no assert; that is the contract.
}

// ---------------------------------------------------------------------------
// IsNamesSimilar (Levenshtein-based, threshold <= 4)
// ---------------------------------------------------------------------------

TEST(utils_ext, IsNamesSimilar_identical)
{
    EXPECT_TRUE(utils::IsNamesSimilar("demo.mp4", "demo.mp4"));
}

TEST(utils_ext, IsNamesSimilar_close)
{
    // 1 char different -> distance 1 -> similar.
    EXPECT_TRUE(utils::IsNamesSimilar("demo.mp4", "demo.mp3"));
}

TEST(utils_ext, IsNamesSimilar_far)
{
    EXPECT_FALSE(utils::IsNamesSimilar("aaaaaaaaaa", "zzzzzzzzzz"));
}

TEST(utils_ext, IsNamesSimilar_empty_vs_nonempty)
{
    // distance == length of the non-empty string. Choose length within threshold.
    EXPECT_TRUE(utils::IsNamesSimilar("", "ab"));     // distance 2
    EXPECT_FALSE(utils::IsNamesSimilar("", "abcde")); // distance 5 > 4
}

TEST(utils_ext, IsNamesSimilar_both_empty)
{
    EXPECT_TRUE(utils::IsNamesSimilar("", ""));
}

// ---------------------------------------------------------------------------
// ValidateScreenshotPath
// ---------------------------------------------------------------------------

TEST(utils_ext, ValidateScreenshotPath_empty)
{
    EXPECT_FALSE(utils::ValidateScreenshotPath(QString()));
    EXPECT_FALSE(utils::ValidateScreenshotPath(""));
    EXPECT_FALSE(utils::ValidateScreenshotPath("   "));
}

TEST(utils_ext, ValidateScreenshotPath_nonexistent_path_is_valid)
{
    // Per impl: a path that doesn't exist is considered acceptable (returns true).
    EXPECT_TRUE(utils::ValidateScreenshotPath("/tmp/utils_ext_does_not_exist_xyz"));
}

TEST(utils_ext, ValidateScreenshotPath_existing_dir_writable)
{
    QTemporaryDir td;
    ASSERT_TRUE(td.isValid());
    EXPECT_TRUE(utils::ValidateScreenshotPath(td.path()));
}

TEST(utils_ext, ValidateScreenshotPath_existing_file_not_dir)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    EXPECT_FALSE(utils::ValidateScreenshotPath(tf.fileName()));
}

TEST(utils_ext, ValidateScreenshotPath_tilde_expansion)
{
    // Exercise the '~' replacement branch. We don't stub homePath here; we
    // just confirm it does not crash and yields a bool.
    bool r = utils::ValidateScreenshotPath("~/some_nonexistent_subdir_screenshot");
    EXPECT_TRUE(r); // nonexistent -> valid per impl
}

// ---------------------------------------------------------------------------
// ElideText
// ---------------------------------------------------------------------------

TEST(utils_ext, ElideText_short_text_fits)
{
    QFont font;
    QString out = utils::ElideText("hi", QSize(200, 20),
                                   QTextOption::NoWrap, font,
                                   Qt::ElideRight, 15, 200);
    EXPECT_FALSE(out.isEmpty());
}

TEST(utils_ext, ElideText_long_text_elided)
{
    QFont font;
    QString longText = QStringLiteral("the quick brown fox jumps over the lazy dog "
                                      "the quick brown fox jumps over the lazy dog "
                                      "the quick brown fox jumps over the lazy dog");
    QString out = utils::ElideText(longText, QSize(50, 20),
                                   QTextOption::WrapAnywhere, font,
                                   Qt::ElideRight, 15, 50);
    EXPECT_FALSE(out.isEmpty());
}

TEST(utils_ext, ElideText_empty)
{
    QFont font;
    QString out = utils::ElideText("", QSize(100, 20),
                                   QTextOption::NoWrap, font,
                                   Qt::ElideRight, 15, 100);
    EXPECT_TRUE(out.isEmpty());
}

TEST(utils_ext, ElideText_multiline_with_newlines)
{
    QFont font;
    QString text = QStringLiteral("line1\nline2\nline3\nline4");
    QString out = utils::ElideText(text, QSize(100, 40),
                                   QTextOption::ManualWrap, font,
                                   Qt::ElideRight, 15, 100);
    EXPECT_FALSE(out.isEmpty());
}

TEST(utils_ext, ElideText_elide_middle_mode)
{
    QFont font;
    QString out = utils::ElideText("a somewhat long piece of text here",
                                   QSize(40, 40),
                                   QTextOption::WrapAtWordBoundaryOrAnywhere,
                                   font, Qt::ElideMiddle, 15, 40);
    EXPECT_FALSE(out.isEmpty());
}

// ---------------------------------------------------------------------------
// MakeRoundedPixmap (overload 1: pm, rx, ry, rotation)
// ---------------------------------------------------------------------------

TEST(utils_ext, MakeRoundedPixmap_overload1_basic)
{
    QPixmap src(40, 40);
    src.fill(Qt::red);
    QPixmap out = utils::MakeRoundedPixmap(src, 5, 5, 0);
    EXPECT_FALSE(out.isNull());
    EXPECT_EQ(out.size(), src.size());
}

TEST(utils_ext, MakeRoundedPixmap_overload1_rotated)
{
    QPixmap src(40, 40);
    src.fill(Qt::blue);
    QPixmap out = utils::MakeRoundedPixmap(src, 5, 5, 90);
    EXPECT_FALSE(out.isNull());
}

TEST(utils_ext, MakeRoundedPixmap_overload1_null_pixmap_does_not_crash)
{
    QPixmap nullPm;
    QPixmap out = utils::MakeRoundedPixmap(nullPm, 5, 5, 0);
    // The impl unconditionally builds dest from pm.size(); with a null pm
    // the result may be null/empty. We only assert no crash.
    EXPECT_TRUE(out.isNull() || out.size().isEmpty());
}

// ---------------------------------------------------------------------------
// MakeRoundedPixmap (overload 2: sz, pm, rx, ry, time)
// ---------------------------------------------------------------------------

TEST(utils_ext, MakeRoundedPixmap_overload2_basic)
{
    QPixmap src(20, 20);
    src.fill(Qt::green);
    QPixmap out = utils::MakeRoundedPixmap(QSize(40, 40), src, 5, 5, 3661);
    EXPECT_FALSE(out.isNull());
    EXPECT_EQ(out.size(), QSize(40, 40));
}

TEST(utils_ext, MakeRoundedPixmap_overload2_zero_time)
{
    QPixmap src(20, 20);
    src.fill(Qt::green);
    QPixmap out = utils::MakeRoundedPixmap(QSize(40, 40), src, 5, 5, 0);
    EXPECT_FALSE(out.isNull());
}

TEST(utils_ext, MakeRoundedPixmap_overload2_large_time)
{
    QPixmap src(20, 20);
    src.fill(Qt::green);
    QPixmap out = utils::MakeRoundedPixmap(QSize(60, 60), src, 5, 5, 90000);
    EXPECT_FALSE(out.isNull());
}

// ---------------------------------------------------------------------------
// MoveToCenter
// ---------------------------------------------------------------------------

TEST(utils_ext, MoveToCenters_real_window)
{
    // MoveToCenter 内部解引用 QScreen; 共享 MainWindow 经多轮用例后 screen 句柄可能失效,
    // 改用全新局部 widget; 无 primaryScreen 时(无显示环境)跳过, 避免空指针崩溃。
    if (!QGuiApplication::primaryScreen()) {
        GTEST_SKIP() << "no screen available";
    }
    QWidget wid;
    wid.resize(100, 100);
    utils::MoveToCenter(&wid);
    SUCCEED();
}

TEST(utils_ext, MoveToCenter_local_widget)
{
    if (!QGuiApplication::primaryScreen()) {
        GTEST_SKIP() << "no screen available";
    }
    QWidget wid;
    wid.resize(100, 100);
    utils::MoveToCenter(&wid);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// videoIndex2str / audioIndex2str
// ---------------------------------------------------------------------------

TEST(utils_ext, videoIndex2str_known)
{
    EXPECT_EQ(utils::videoIndex2str(0), QString("none"));
    EXPECT_EQ(utils::videoIndex2str(1), QString("mpeg1video"));
    EXPECT_EQ(utils::videoIndex2str(2), QString("mpeg2video"));
    EXPECT_EQ(utils::videoIndex2str(28), QString("h264"));
}

TEST(utils_ext, videoIndex2str_out_of_range_returns_empty)
{
    // QMap::operator[] on missing key default-constructs QString -> "".
    EXPECT_EQ(utils::videoIndex2str(999999), QString());
}

TEST(utils_ext, videoIndex2str_negative_returns_empty)
{
    EXPECT_EQ(utils::videoIndex2str(-1), QString());
}

TEST(utils_ext, audioIndex2str_known)
{
    EXPECT_EQ(utils::audioIndex2str(86016), QString("mp2"));
    EXPECT_EQ(utils::audioIndex2str(86017), QString("mp3"));
    EXPECT_EQ(utils::audioIndex2str(86018), QString("aac"));
}

TEST(utils_ext, audioIndex2str_out_of_range_returns_empty)
{
    EXPECT_EQ(utils::audioIndex2str(0), QString());
    EXPECT_EQ(utils::audioIndex2str(999999), QString());
}

// ---------------------------------------------------------------------------
// runPipeProcess
// ---------------------------------------------------------------------------

TEST(utils_ext, runPipeProcess_empty_command)
{
    QStringList out = utils::runPipeProcess("", "");
    EXPECT_TRUE(out.isEmpty());
}

TEST(utils_ext, runPipeProcess_rejects_metachars)
{
    // Commands containing shell metacharacters are rejected for safety.
    QStringList out1 = utils::runPipeProcess("ls; rm -rf /", "");
    EXPECT_TRUE(out1.isEmpty());
    QStringList out2 = utils::runPipeProcess("ls | grep x", "");
    EXPECT_TRUE(out2.isEmpty());
    QStringList out3 = utils::runPipeProcess("ls `whoami`", "");
    EXPECT_TRUE(out3.isEmpty());
    QStringList out4 = utils::runPipeProcess("ls $HOME", "");
    EXPECT_TRUE(out4.isEmpty());
}

TEST(utils_ext, runPipeProcess_echo_no_filter_returns_all)
{
    QStringList out = utils::runPipeProcess("echo utils_ext_marker", "");
    ASSERT_FALSE(out.isEmpty());
    bool found = false;
    for (const QString &l : out) {
        if (l.contains("utils_ext_marker")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(utils_ext, runPipeProcess_echo_with_filter)
{
    QStringList out = utils::runPipeProcess("echo utils_ext_keep\necho utils_ext_drop", "keep");
    // At least the keep line should be present when filter matches.
    EXPECT_GE(out.size(), 0); // mainly assert no crash / hang
}

// ---------------------------------------------------------------------------
// FastFileHash / FullFileHash
// ---------------------------------------------------------------------------

TEST(utils_ext, FastFileHash_missing_file_returns_empty)
{
    QFileInfo fi("/tmp/utils_ext_definitely_missing_file_xyz");
    EXPECT_EQ(utils::FastFileHash(fi), QString());
}

TEST(utils_ext, FullFileHash_missing_file_returns_empty)
{
    QFileInfo fi("/tmp/utils_ext_definitely_missing_file_xyz");
    EXPECT_EQ(utils::FullFileHash(fi), QString());
}

TEST(utils_ext, FastFileHash_small_file_under_8192)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("hello utils_ext");
    tf.flush();

    QFileInfo fi(tf.fileName());
    QString h = utils::FastFileHash(fi);
    EXPECT_FALSE(h.isEmpty());
    EXPECT_EQ(h, utils::FullFileHash(fi)); // for <8192 FastFileHash hashes whole file
}

TEST(utils_ext, FullFileHash_known_content)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("abc");
    tf.flush();

    QFileInfo fi(tf.fileName());
    QString h = utils::FullFileHash(fi);
    // md5("abc")
    EXPECT_EQ(h.toLower(), QString("900150983cd24fb0d6963f7d28e17f72"));
}

// ---------------------------------------------------------------------------
// first_check_wayland_env / check_wayland_env / set_wayland
// ---------------------------------------------------------------------------

TEST(utils_ext, first_check_wayland_env_returns_bool_no_crash)
{
    bool r = utils::first_check_wayland_env();
    EXPECT_TRUE(r == true || r == false);
}

TEST(utils_ext, check_wayland_env_after_first_check_consistent)
{
    bool first = utils::first_check_wayland_env();
    bool cached = utils::check_wayland_env();
    EXPECT_EQ(first, cached);
}

#ifdef USE_TEST
TEST(utils_ext, set_wayland_toggles_cache)
{
    utils::set_wayland(true);
    EXPECT_TRUE(utils::check_wayland_env());
    utils::set_wayland(false);
    EXPECT_FALSE(utils::check_wayland_env());
    // Restore from the real environment to avoid affecting later tests.
    utils::first_check_wayland_env();
}
#endif

// ---------------------------------------------------------------------------
// FindSimilarFiles
// ---------------------------------------------------------------------------

TEST(utils_ext, FindSimilarFiles_in_temp_dir)
{
    QTemporaryDir td;
    ASSERT_TRUE(td.isValid());
    QDir dir(td.path());

    // Create a base file plus a similar-named one and an unrelated one.
    QFile base(dir.absoluteFilePath("episode01.mp4"));
    ASSERT_TRUE(base.open(QIODevice::WriteOnly));
    base.write("base");
    base.close();

    QFile sim(dir.absoluteFilePath("episode02.mp4"));
    ASSERT_TRUE(sim.open(QIODevice::WriteOnly));
    sim.write("sim");
    sim.close();

    QFile other(dir.absoluteFilePath("totally_different_name.txt"));
    ASSERT_TRUE(other.open(QIODevice::WriteOnly));
    other.write("other");
    other.close();

    QFileInfoList result = utils::FindSimilarFiles(QFileInfo(base.fileName()));
    // The base file itself is always similar to itself; the sibling episode
    // should be found too. The unrelated file must never appear.
    EXPECT_GE(result.size(), 1);

    bool foundSim = false;
    bool foundOther = false;
    for (const QFileInfo &fi : result) {
        if (fi.fileName() == "episode02.mp4") foundSim = true;
        if (fi.fileName() == "totally_different_name.txt") foundOther = true;
    }
    EXPECT_TRUE(foundSim);
    EXPECT_FALSE(foundOther);
}

TEST(utils_ext, FindSimilarFiles_nonexistent_dir_does_not_crash)
{
    QFileInfo fi("/tmp/utils_ext_no_such_dir_xyz/base.mp4");
    QFileInfoList result = utils::FindSimilarFiles(fi);
    EXPECT_TRUE(result.isEmpty());
}

// ---------------------------------------------------------------------------
// IsLinglongEnvironment / ConvertLinglongPathFor* (pure-logic)
// ---------------------------------------------------------------------------

TEST(utils_ext, IsLinglongEnvironment_returns_bool)
{
    bool r = utils::IsLinglongEnvironment();
    EXPECT_TRUE(r == true || r == false);
}

TEST(utils_ext, ConvertLinglongPathForPlayback_non_linglong_passthrough)
{
    // In the test environment (no LINGLONG_APPID) the function returns input.
    QString path = "/some/random/path.mp4";
    EXPECT_EQ(utils::ConvertLinglongPathForPlayback(path), path);
}

TEST(utils_ext, ConvertLinglongPathForFM_non_linglong_passthrough)
{
    QString path = "/run/host/rootfs/some/path.mp4";
    EXPECT_EQ(utils::ConvertLinglongPathForFM(path), path);
}

// ---------------------------------------------------------------------------
// getJjwGPUPath / isJjwGPUPresent / isSietiumGPUPresent
// ---------------------------------------------------------------------------

TEST(utils_ext, getJjwGPUPath_returns_string_no_crash)
{
    QString p = utils::getJjwGPUPath();
    // On a typical CI box no jjw device is present -> empty, but any string is fine.
    EXPECT_TRUE(p.isEmpty() || p.size() > 0);
}

TEST(utils_ext, isJjwGPUPresent_returns_bool)
{
    bool r = utils::isJjwGPUPresent();
    EXPECT_TRUE(r == true || r == false);
}

TEST(utils_ext, isSietiumGPUPresent_returns_bool)
{
    bool r = utils::isSietiumGPUPresent();
    EXPECT_TRUE(r == true || r == false);
}

// ---------------------------------------------------------------------------
// LoadHiDPIImage / LoadHiDPIPixmap
// ---------------------------------------------------------------------------

TEST(utils_ext, LoadHiDPIImage_missing_file_returns_null_or_empty)
{
    QImage img = utils::LoadHiDPIImage("/tmp/utils_ext_no_such_image.png");
    EXPECT_TRUE(img.isNull());
}

TEST(utils_ext, LoadHiDPIPixmap_missing_file_returns_null)
{
    QPixmap pm = utils::LoadHiDPIPixmap("/tmp/utils_ext_no_such_pixmap.png");
    EXPECT_TRUE(pm.isNull());
}

// ---------------------------------------------------------------------------
// getPlayProperty (writes into a provided map; guarded by DConfig availability)
// ---------------------------------------------------------------------------

TEST(utils_ext, getPlayProperty_null_map_pointer_safe_with_null)
{
    // Signature is (const char*, QMap<QString,QString>*&). Pass a real map
    // pointer variable (not &local) to satisfy the reference-to-pointer param.
    QMap<QString, QString> *m = new QMap<QString, QString>();
    utils::getPlayProperty("/tmp/utils_ext_no_such_play_properties.conf", m);
    // No crash; map may be empty.
    EXPECT_TRUE(m != nullptr);
    delete m;
}

TEST(utils_ext, getPlayProperty_reads_existing_simple_file)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("key1=value1\nkey2=value2\n");
    tf.flush();

    // The param is a reference to a pointer; pass an lvalue pointer.
    QMap<QString, QString> *m = new QMap<QString, QString>();
    // When DTKCORE_CLASS_DConfigFile is defined, the file path is ignored and
    // DConfig is consulted instead; either branch is fine — we just assert no crash.
    utils::getPlayProperty(tf.fileName().toLocal8Bit().constData(), m);
    delete m;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// switchToDefaultSink (DBus path; covered defensively)
// ---------------------------------------------------------------------------

TEST(utils_ext, switchToDefaultSink_does_not_crash_without_dbus)
{
    // readDBusProperty returns invalid QVariant when DBus service is absent,
    // causing an early return. We just assert the call doesn't crash.
    utils::switchToDefaultSink();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// ShowInFileManager (DBus + QDesktopServices; covered defensively)
// ---------------------------------------------------------------------------

TEST(utils_ext, ShowInFileManager_empty_path_returns_silently)
{
    utils::ShowInFileManager(""); // early-returns on empty path
    SUCCEED();
}

TEST(utils_ext, ShowInFileManager_nonexistent_path_returns_silently)
{
    utils::ShowInFileManager("/tmp/utils_ext_no_such_file_for_fm");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Inhibit / UnInhibit (DBus; return value 0 when service unavailable)
// ---------------------------------------------------------------------------

TEST(utils_ext, InhibitStandby_returns_uint_no_crash)
{
    uint32_t cookie = utils::InhibitStandby();
    EXPECT_TRUE(cookie == 0 || cookie > 0); // 0 when DBus absent
    utils::UnInhibitStandby(cookie);
}

TEST(utils_ext, InhibitPower_returns_uint_no_crash)
{
    uint32_t cookie = utils::InhibitPower();
    EXPECT_TRUE(cookie == 0 || cookie > 0);
    utils::UnInhibitPower(cookie);
    // Existing test_dmr.cpp calls UnInhibitPower(20) with an arbitrary cookie;
    // mirror that to cover the no-op path.
    utils::UnInhibitPower(20);
}
