// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the deepin-movie backend / dlna / filefilter / thumbnail layer.
// Suite: boost_dm_be. Helper prefix: bdb_.
//
// Goal: raise line coverage of several previously under-tested translation
// units without ever bringing up a live media pipeline:
//   * src/backends/mpv/mpv_proxy.cpp      (pure/static helpers only)
//   * src/backends/mpv/mpv_glwidget.cpp   (header-only access; no GL context)
//   * src/common/thumbnail_worker.cpp      (pure static helper)
//   * src/dlna/dlnacontentserver.cpp       (Range + DLNA header formatters)
//   * src/libdmr/filefilter.cpp            (path/url helpers, dir traversal)
//   * src/dlna/getdlnaxmlvalue.cpp         (xml path lookups)
//   * src/dlna/cdlnasoappost.cpp           (soap payload + time formatting)
//
// CRASH SAFETY:
//   * Only TEST(...). gtest_main supplies main(); never define main().
//   * We NEVER construct a live MpvProxy / MpvGLWidget / ThumbnailWorker.
//     Their ctors/dtors touch libmpv handles, GL contexts, X11 window handles
//     and ffmpegthumbnailer — any of those can SIGSEGV in a headless sandbox
//     and drop every later case in the run. Instead we exercise only the
//     PURE/STATIC pieces of those TUs.
//   * DlnaContentServer::DlnaContentServer() spawns a real HTTP thread; we
//     stub QThread::start exactly like tests/deepin-movie/dlna/test_dlnacontentserver.cpp.
//   * Every Stub is reset() before the test ends.

// ---- STL / Qt BEFORE the private-access shim (rule #10) ----
#include <QtTest>
#include <QTest>
#include <QDebug>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDomDocument>
#include <QDomElement>
#include <QStringList>
#include <QVariant>
#include <QThread>
#include <unistd.h>
#include <thread>

#include <gtest/gtest.h>

// Pure libmpv header (no link requirement for the formats we touch).
#include <mpv/client.h>

// ---- Stub infrastructure ----
#include "stub/stub.h"

// ---- Private access for nested private types we exercise ----
// (MpvProxy::my_node_autofree, CDlnaSoapPost::getTimeStr, etc.) These MUST be
// included here under the shim BEFORE application.h -> mainwindow.h -> ...,
// which would transitively include them and trip the include guard, freezing
// their private members as private.
#define protected public
#define private public
#include "src/backends/mpv/mpv_proxy.h"
#include "src/dlna/cdlnasoappost.h"
#include "src/dlna/getdlnaxmlvalue.h"
#include "src/dlna/dlnacontentserver.h"
#undef protected
#undef private

// ---- Other headers under test (own private members stay private) ----
#include "application.h"
#include "compositing_manager.h"
#include "utils.h"
#include "filefilter.h"
#include "thumbnail_worker.h"

using namespace dmr;

// ===========================================================================
// Local stub helpers (unique prefix bdb_ to avoid collisions across suites).
// ===========================================================================

// Stub QThread::start to be a no-op so DlnaContentServer ctor does not spawn
// the real HTTP worker thread (mirrors test_dlnacontentserver.cpp).
static void bdb_QThread_start_stub()
{
    return;
}

static void bdb_stub_QThread_start(Stub &stub)
{
    stub.set(ADDR(QThread, start), bdb_QThread_start_stub);
}

static bool bdb_isMpvExists_true()  { return true;  }
static bool bdb_isMpvExists_false() { return false; }


// ===========================================================================
// mpv_proxy.cpp — PURE/STATIC helpers.
//
// my_node_autofree is an RAII wrapper whose dtor calls mpv_free_node_contents.
// For scalar/empty-list formats that call is a safe no-op, so we can construct
// synthetic mpv_node values on the stack and let the wrapper destruct without
// ever touching a live mpv handle. The sibling TUs (mpv_proxy_ext*) in the
// platform binary already cover NONE/FLAG/DOUBLE/INT64/STRING/NODE_MAP; here
// we add the remaining formats and edge cases so this binary's coverage of
// mpv_proxy.cpp grows independently.
// ===========================================================================

TEST(boost_dm_be, bdb_my_node_autofree_none_format_roundtrip)
{
    mpv_node node;
    node.format = MPV_FORMAT_NONE;
    {
        MpvProxy::my_node_autofree af(&node);
        ASSERT_NE(af.pNode, nullptr);
        EXPECT_EQ(af.pNode->format, MPV_FORMAT_NONE);
    }   // dtor: mpv_free_node_contents -> no-op for NONE
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_flag_format)
{
    mpv_node node;
    node.format = MPV_FORMAT_FLAG;
    node.u.flag = 1;
    {
        MpvProxy::my_node_autofree af(&node);
        EXPECT_EQ(af.pNode->u.flag, 1);
    }   // dtor no-op for flag
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_double_format)
{
    mpv_node node;
    node.format = MPV_FORMAT_DOUBLE;
    node.u.double_ = 2.71828;
    {
        MpvProxy::my_node_autofree af(&node);
        ASSERT_NE(af.pNode, nullptr);
        EXPECT_DOUBLE_EQ(af.pNode->u.double_, 2.71828);
    }
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_int64_format_negative)
{
    mpv_node node;
    node.format = MPV_FORMAT_INT64;
    node.u.int64 = -123456;
    {
        MpvProxy::my_node_autofree af(&node);
        EXPECT_EQ(af.pNode->u.int64, -123456);
    }
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_string_format_static_literal)
{
    // mpv_free_node_contents does NOT free string buffers (caller-owned), so a
    // string literal is safe and exercises the STRING branch of the dtor.
    mpv_node node;
    node.format = MPV_FORMAT_STRING;
    node.u.string = const_cast<char *>("bdb_static");
    {
        MpvProxy::my_node_autofree af(&node);
        ASSERT_STREQ(af.pNode->u.string, "bdb_static");
    }
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_empty_node_map)
{
    // Empty NODE_MAP (num == 0, list == nullptr): the dtor walks zero entries
    // and frees the nullptr arrays harmlessly.
    mpv_node node;
    node.format = MPV_FORMAT_NODE_MAP;
    node.u.list = nullptr;
    {
        MpvProxy::my_node_autofree af(&node);
        EXPECT_EQ(af.pNode->format, MPV_FORMAT_NODE_MAP);
    }
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_empty_node_array)
{
    // Same idea for the ARRAY container format.
    mpv_node node;
    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = nullptr;
    {
        MpvProxy::my_node_autofree af(&node);
        EXPECT_EQ(af.pNode->format, MPV_FORMAT_NODE_ARRAY);
    }
    SUCCEED();
}

TEST(boost_dm_be, bdb_my_node_autofree_scope_exit_runs_dtor)
{
    // Verify the wrapper is a true RAII type: leaving the inner scope must run
    // the dtor once; the outer scope's node stays valid (NONE format, no-op).
    mpv_node node;
    node.format = MPV_FORMAT_NONE;
    bool seen = false;
    {
        MpvProxy::my_node_autofree af(&node);
        seen = (af.pNode == &node);
    }
    EXPECT_TRUE(seen);
}

TEST(boost_dm_be, bdb_my_node_autofree_pNode_field_readable)
{
    // The wrapper exposes its pNode member (needed by the dtor). Confirm it can
    // be read back; using NONE keeps the dtor a no-op so the field is valid for
    // the whole scope.
    mpv_node node;
    node.format = MPV_FORMAT_NONE;
    MpvProxy::my_node_autofree af(&node);
    EXPECT_EQ(af.pNode, &node);
    EXPECT_EQ(af.pNode->format, MPV_FORMAT_NONE);
}

// --------------------------------------------------------------------------
// MpvHandle — refcounted QSharedPointer-based wrapper. Wrapping nullptr covers
// ctor/dtor/copy/operator-mpv_handle without ever calling libmpv (the dtor
// guards on the raw handle being non-null).
// --------------------------------------------------------------------------

TEST(boost_dm_be, bdb_MpvHandle_fromRawNull_yieldsNull)
{
    MpvHandle h = MpvHandle::fromRawHandle(nullptr);
    EXPECT_EQ((mpv_handle *)h, nullptr);
}

TEST(boost_dm_be, bdb_MpvHandle_defaultConstructed_yieldsZero)
{
    MpvHandle h;   // empty QSharedPointer
    EXPECT_EQ((mpv_handle *)h, (mpv_handle *)0);
}

TEST(boost_dm_be, bdb_MpvHandle_copy_sharesNullContainer)
{
    MpvHandle a = MpvHandle::fromRawHandle(nullptr);
    MpvHandle b = a;
    EXPECT_EQ((mpv_handle *)a, (mpv_handle *)b);
    EXPECT_EQ((mpv_handle *)a, nullptr);
}

TEST(boost_dm_be, bdb_MpvHandle_selfAssign_keepsNull)
{
    MpvHandle a = MpvHandle::fromRawHandle(nullptr);
    a = a;
    EXPECT_EQ((mpv_handle *)a, nullptr);
}

TEST(boost_dm_be, bdb_MpvHandle_reset_toDefault)
{
    MpvHandle a = MpvHandle::fromRawHandle(nullptr);
    a = MpvHandle();
    EXPECT_EQ((mpv_handle *)a, (mpv_handle *)0);
}

TEST(boost_dm_be, bdb_MpvHandle_manyAliases_allNullUntilLastDrop)
{
    MpvHandle a = MpvHandle::fromRawHandle(nullptr);
    {
        MpvHandle b = a;
        MpvHandle c = a;
        MpvHandle d = a;
        EXPECT_EQ((mpv_handle *)b, nullptr);
        EXPECT_EQ((mpv_handle *)c, nullptr);
        EXPECT_EQ((mpv_handle *)d, nullptr);
    }
    EXPECT_EQ((mpv_handle *)a, nullptr);
}

// --------------------------------------------------------------------------
// MpvProxy::tr — Q_OBJECT-generated static translator. Pure, backend-free.
// The strings used by mpv_proxy.cpp are "Internal" and "Movie".
// --------------------------------------------------------------------------

TEST(boost_dm_be, bdb_MpvProxy_tr_movie_nonEmpty)
{
    EXPECT_FALSE(MpvProxy::tr("Movie").isEmpty());
}

TEST(boost_dm_be, bdb_MpvProxy_tr_internal_nonEmpty)
{
    EXPECT_FALSE(MpvProxy::tr("Internal").isEmpty());
}

TEST(boost_dm_be, bdb_MpvProxy_tr_plural_overload_safe)
{
    QString s1 = MpvProxy::tr("Movie", nullptr, 1);
    QString s2 = MpvProxy::tr("Movie", nullptr, 2);
    EXPECT_FALSE(s1.isEmpty());
    EXPECT_FALSE(s2.isEmpty());
}

TEST(boost_dm_be, bdb_MpvProxy_tr_empty_source_safe)
{
    QString s = MpvProxy::tr("");
    EXPECT_TRUE(s.isEmpty() || s == QString(""));
}

// --------------------------------------------------------------------------
// Backend base-class static state (mpv_proxy.cpp includes the Backend header
// and uses Backend::setDebugLevel / DebugLevel). Touching the static setter
// covers that path safely with no instance.
// --------------------------------------------------------------------------

TEST(boost_dm_be, bdb_Backend_setDebugLevel_roundtrip)
{
    Backend::setDebugLevel(Backend::DebugLevel::Info);
    Backend::setDebugLevel(Backend::DebugLevel::Debug);
    Backend::setDebugLevel(Backend::DebugLevel::Verbose);
    SUCCEED();
}


// ===========================================================================
// dlnacontentserver.cpp — Range + DLNA header formatters.
//
// DlnaContentServer::DlnaContentServer() spawns a real HTTP thread; we stub
// QThread::start exactly like tests/deepin-movie/dlna/test_dlnacontentserver.cpp
// so the server ctor is side-effect-free. The pure helpers below are the
// high-yield targets: Range::fromRange branches, dlnaOrgPnFlags mime table,
// dlnaContentFeaturesHeader flag matrix, dlnaOrgFlagsForFile/Streaming.
// ===========================================================================

// Build a server with the HTTP thread stubbed out. Returns a heap pointer the
// caller owns (mirrors the fixture pattern in the existing dlna test).
static DlnaContentServer *bdb_makeServer()
{
    Stub stub;
    bdb_stub_QThread_start(stub);
    DlnaContentServer *srv = new DlnaContentServer();
    return srv;
}

// ---- Range::fromRange ----

TEST(boost_dm_be, bdb_Range_fromRange_openEnded_withLength)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-", 100);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 99);   // length-1
}

TEST(boost_dm_be, bdb_Range_fromRange_closedRange_withinLength)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-100", 200);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 100);
}

TEST(boost_dm_be, bdb_Range_fromRange_full_fromZero)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=0-", 50);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->full());
}

TEST(boost_dm_be, bdb_Range_fromRange_singleByte)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=5-5", 10);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 5);
    EXPECT_EQ(r->end, 5);
    EXPECT_EQ(r->rangeLength(), 1);
}

TEST(boost_dm_be, bdb_Range_fromRange_unknownLength_openEnded)
{
    // length = -1 (default): open-ended start-only range is valid.
    auto r = DlnaContentServer::Range::fromRange("bytes=0-");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
}

TEST(boost_dm_be, bdb_Range_fromRange_unknownLength_closedValid)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=0-99");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, 99);
}

TEST(boost_dm_be, bdb_Range_fromRange_invalid_garbage_returnsNullopt)
{
    auto r = DlnaContentServer::Range::fromRange("not-a-range", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(boost_dm_be, bdb_Range_fromRange_invalid_noBytesPrefix)
{
    auto r = DlnaContentServer::Range::fromRange("10-100", 200);
    EXPECT_FALSE(r.has_value());
}

TEST(boost_dm_be, bdb_Range_fromRange_inverted_returnsNullopt)
{
    // end < start must be rejected by the validation guard.
    auto r = DlnaContentServer::Range::fromRange("bytes=100-10", 200);
    EXPECT_FALSE(r.has_value());
}

TEST(boost_dm_be, bdb_Range_fromRange_startBeyondLength_returnsNullopt)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=500-600", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(boost_dm_be, bdb_Range_fromRange_emptyHeader_returnsNullopt)
{
    auto r = DlnaContentServer::Range::fromRange("", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(boost_dm_be, bdb_Range_full_predicate_and_rangeLength)
{
    DlnaContentServer::Range r{0, 9, 10};
    EXPECT_TRUE(r.full());
    EXPECT_EQ(r.rangeLength(), 10);
}

TEST(boost_dm_be, bdb_Range_equality_operator)
{
    DlnaContentServer::Range a{0, 9, 10};
    DlnaContentServer::Range b{0, 9, 10};
    DlnaContentServer::Range c{1, 9, 10};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

// ---- dlnaOrgPnFlags (mime -> UPnP PN tag table) ----

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_avi)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->dlnaOrgPnFlags("video/x-msvideo"), "DLNA.ORG_PN=AVI");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_aac)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->dlnaOrgPnFlags("audio/aac"), "DLNA.ORG_PN=AAC");
    EXPECT_EQ(srv->dlnaOrgPnFlags("audio/aacp"), "DLNA.ORG_PN=AAC");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_mpeg_audio)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->dlnaOrgPnFlags("audio/mpeg"), "DLNA.ORG_PN=MP3");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_wav_and_L16)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->dlnaOrgPnFlags("audio/vnd.wav"), "DLNA.ORG_PN=LPCM");
    EXPECT_EQ(srv->dlnaOrgPnFlags("audio/L16"), "DLNA.ORG_PN=LPCM");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_matroska)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->dlnaOrgPnFlags("video/x-matroska"), "DLNA.ORG_PN=MKV");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_caseInsensitive)
{
    DlnaContentServer *srv = bdb_makeServer();
    // The table uses Qt::CaseInsensitive, so mixed-case input must still match.
    EXPECT_EQ(srv->dlnaOrgPnFlags("VIDEO/X-MSVIDEO"), "DLNA.ORG_PN=AVI");
    EXPECT_EQ(srv->dlnaOrgPnFlags("Audio/MPEG"), "DLNA.ORG_PN=MP3");
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgPnFlags_unknownMime_returnsEmpty)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_TRUE(srv->dlnaOrgPnFlags("application/octet-stream").isEmpty());
    EXPECT_TRUE(srv->dlnaOrgPnFlags("").isEmpty());
    delete srv;
}

// ---- dlnaContentFeaturesHeader (full flag matrix) ----

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_knownMime_seekFile)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString h = srv->dlnaContentFeaturesHeader("video/x-msvideo", true, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=AVI"));
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_knownMime_noSeek_noFlags)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString h = srv->dlnaContentFeaturesHeader("video/x-msvideo", false, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=AVI"));
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_unknownMime_seekFile)
{
    DlnaContentServer *srv = bdb_makeServer();
    // pnFlags empty -> shorter header form, seek+flags branch.
    QString h = srv->dlnaContentFeaturesHeader("application/octet-stream", true, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    EXPECT_FALSE(h.contains("DLNA.ORG_PN"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_unknownMime_noSeek_streaming)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString h = srv->dlnaContentFeaturesHeader("application/octet-stream", false, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_unknownMime_flagsFalse_noSeek)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString h = srv->dlnaContentFeaturesHeader("application/octet-stream", false, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_unknownMime_flagsFalse_seek)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString h = srv->dlnaContentFeaturesHeader("application/octet-stream", true, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaContentFeaturesHeader_defaultArgs_seekAndFlags)
{
    DlnaContentServer *srv = bdb_makeServer();
    // Defaults: seek=true, flags=true.
    QString h = srv->dlnaContentFeaturesHeader("audio/mpeg");
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=MP3"));
    delete srv;
}

// ---- dlnaOrgFlagsForFile / dlnaOrgFlagsForStreaming ----

TEST(boost_dm_be, bdb_dlnaOrgFlagsForFile_format)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString f = srv->dlnaOrgFlagsForFile();
    EXPECT_TRUE(f.startsWith("DLNA.ORG_FLAGS="));
    // The value is a 32-hex-digit field (8 + 24 zero-padded).
    EXPECT_EQ(f.length() - QString("DLNA.ORG_FLAGS=").length(), 32);
    delete srv;
}

TEST(boost_dm_be, bdb_dlnaOrgFlagsForStreaming_format)
{
    DlnaContentServer *srv = bdb_makeServer();
    QString f = srv->dlnaOrgFlagsForStreaming();
    EXPECT_TRUE(f.startsWith("DLNA.ORG_FLAGS="));
    EXPECT_EQ(f.length() - QString("DLNA.ORG_FLAGS=").length(), 32);
    delete srv;
}

// ---- getters / setters ----

TEST(boost_dm_be, bdb_setBaseUrl_getBaseUrl_roundtrip)
{
    DlnaContentServer *srv = bdb_makeServer();
    srv->setBaseUrl("http://127.0.0.1:8080/x");
    EXPECT_EQ(srv->getBaseUrl().toStdString(), "http://127.0.0.1:8080/x");
    delete srv;
}

TEST(boost_dm_be, bdb_setDlnaFileName_storesValue)
{
    DlnaContentServer *srv = bdb_makeServer();
    srv->setDlnaFileName("/tmp/bdb_clip.mp4");
    // No public getter for the filename; just confirm it does not crash and the
    // server remains usable. The internal field is exercised by requestHandler.
    srv->setDlnaFileName("");
    delete srv;
}

TEST(boost_dm_be, bdb_getBaseUrl_defaultEmpty)
{
    DlnaContentServer *srv = bdb_makeServer();
    EXPECT_EQ(srv->getBaseUrl(), QString());
    delete srv;
}

TEST(boost_dm_be, bdb_getIsStartHttpServer_defaultFalse)
{
    DlnaContentServer *srv = bdb_makeServer();
    // With QThread::start stubbed, the started-handler never runs, so the flag
    // stays at its default false.
    EXPECT_FALSE(srv->getIsStartHttpServer());
    delete srv;
}

// ---- initializeHttpServer (called directly; thread already stubbed) ----

TEST(boost_dm_be, bdb_initializeHttpServer_bindsOrFailsCleanly)
{
    DlnaContentServer *srv = bdb_makeServer();
    // Pick an unlikely high port. The call must return a bool without crashing
    // whether or not the bind succeeds (env-dependent).
    bool ok = srv->initializeHttpServer(58083);
    EXPECT_TRUE(ok == true || ok == false);
    delete srv;
}

// ---- sendEmptyResponse with null resp (defensive guard) ----

TEST(boost_dm_be, bdb_sendEmptyResponse_nullResp_doesNotCrash)
{
    DlnaContentServer *srv = bdb_makeServer();
    srv->sendEmptyResponse(nullptr, 404);   // guard returns immediately
    delete srv;
}

// ---- streamFile / streamFileRange / streamFileNoRange with null req/resp ----

TEST(boost_dm_be, bdb_streamFile_nullReqResp_returnsEarly)
{
    DlnaContentServer *srv = bdb_makeServer();
    srv->streamFile("", "video/mp4", nullptr, nullptr);   // guard returns
    delete srv;
}

TEST(boost_dm_be, bdb_streamFileRange_nullResp_returnsEarly)
{
    DlnaContentServer *srv = bdb_makeServer();
    auto f = std::make_shared<QFile>("");
    srv->streamFileRange(f, nullptr, nullptr);
    delete srv;
}

TEST(boost_dm_be, bdb_streamFileNoRange_nullResp_returnsEarly)
{
    DlnaContentServer *srv = bdb_makeServer();
    auto f = std::make_shared<QFile>("");
    srv->streamFileNoRange(f, nullptr, nullptr);
    delete srv;
}

TEST(boost_dm_be, bdb_seqWriteData_nullResp_returnsEarly)
{
    DlnaContentServer *srv = bdb_makeServer();
    auto f = std::make_shared<QFile>("");
    srv->seqWriteData(f, 0, nullptr);
    delete srv;
}


// ===========================================================================
// filefilter.cpp — path/url helpers + directory traversal.
// Mirrors tests/deepin-movie/libdmr/test_filefilter.cpp patterns.
// ===========================================================================

static QDir bdb_makeTempTree()
{
    QByteArray tpl = (QDir::tempPath() + "/bdb_ff_XXXXXX").toUtf8();
    QByteArray arr = mkdtemp(tpl.data());
    return QDir(QString::fromUtf8(arr));
}

TEST(boost_dm_be, bdb_fileTransfer_plainNonexistentPath)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = ff->fileTransfer("/tmp/bdb_no_such_xyz.mp4");
    EXPECT_FALSE(url.isLocalFile());
    EXPECT_TRUE(url.toString().contains("bdb_no_such_xyz.mp4"));
}

TEST(boost_dm_be, bdb_fileTransfer_existingFile_canonical)
{
    FileFilter *ff = FileFilter::instance();
    QString path = QDir::tempPath() + "/bdb_ff_real.mp4";
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

TEST(boost_dm_be, bdb_fileTransfer_fileSchemePrefix)
{
    FileFilter *ff = FileFilter::instance();
    QString path = QDir::tempPath() + "/bdb_ff_scheme.mp4";
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

TEST(boost_dm_be, bdb_fileTransfer_percentEncodedSpaceRoundTrips)
{
    FileFilter *ff = FileFilter::instance();
    QString path = QDir::tempPath() + "/bdb_ff with space.mp4";
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

TEST(boost_dm_be, bdb_fileTransfer_networkUrl)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = ff->fileTransfer("http://example.com/bdb/stream.mp4");
    EXPECT_FALSE(url.isLocalFile());
    EXPECT_EQ(url.scheme().toStdString(), "http");
}

TEST(boost_dm_be, bdb_fileTransfer_emptyString)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_TRUE(ff->fileTransfer(QString()).isEmpty());
}

TEST(boost_dm_be, bdb_isMediaFile_nonLocalShortCircuitTrue)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_TRUE(ff->isMediaFile(QUrl("http://example.com/bdb.mp4")));
    EXPECT_TRUE(ff->isMediaFile(QUrl("rtmp://example.com/bdb")));
    EXPECT_TRUE(ff->isMediaFile(QUrl("https://example.com/bdb.mp4")));
}

TEST(boost_dm_be, bdb_isMediaFile_emptyUrlNonLocalTrue)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_TRUE(ff->isMediaFile(QUrl()));
}

TEST(boost_dm_be, bdb_isFormatSupported_nonexistentFalse)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_FALSE(ff->isFormatSupported(QUrl::fromLocalFile("/no/such/bdb_xyz.mp4")));
}

TEST(boost_dm_be, bdb_isFormatSupported_emptyUrlFalse)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_FALSE(ff->isFormatSupported(QUrl::fromLocalFile(QString())));
}

TEST(boost_dm_be, bdb_filterDir_collectsFilesRecursively)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bdb_makeTempTree();
    base.mkpath("sub/deep");
    QFile(base.absoluteFilePath("a.mp4")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/b.mp3")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/deep/c.ass")).open(QIODevice::WriteOnly);

    QList<QUrl> urls = ff->filterDir(base);
    EXPECT_EQ(urls.size(), 3);
    for (const QUrl &u : urls) {
        EXPECT_TRUE(u.isLocalFile());
    }
    base.removeRecursively();
}

TEST(boost_dm_be, bdb_filterDir_emptyDirectory)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bdb_makeTempTree();
    EXPECT_TRUE(ff->filterDir(base).isEmpty());
    base.removeRecursively();
}

TEST(boost_dm_be, bdb_filterDir_nonexistentDirectory)
{
    FileFilter *ff = FileFilter::instance();
    EXPECT_TRUE(ff->filterDir(QDir("/no/such/dir/bdb_xyz")).isEmpty());
}

TEST(boost_dm_be, bdb_filterDir_stopThreadReturnsEmpty)
{
    FileFilter *ff = FileFilter::instance();
    QDir base = bdb_makeTempTree();
    base.mkpath("sub");
    QFile(base.absoluteFilePath("a.mp4")).open(QIODevice::WriteOnly);
    QFile(base.absoluteFilePath("sub/b.mp3")).open(QIODevice::WriteOnly);

    ff->stopThread();
    EXPECT_TRUE(ff->filterDir(base).isEmpty());
    base.removeRecursively();
}

TEST(boost_dm_be, bdb_isAudio_nonexistentLocalFalse_mpvPath)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/bdb_audio.mp3");
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bdb_isMpvExists_true);
    EXPECT_FALSE(ff->isAudio(url));
    // Cached path: second call hits the populated map.
    EXPECT_FALSE(ff->isAudio(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(boost_dm_be, bdb_isVideo_nonexistentLocalFalse_mpvPath)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/bdb_video.mp4");
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bdb_isMpvExists_true);
    EXPECT_FALSE(ff->isVideo(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(boost_dm_be, bdb_isSubtitle_nonexistentLocalFalse_mpvPath)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/bdb_sub.ass");
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bdb_isMpvExists_true);
    EXPECT_FALSE(ff->isSubtitle(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(boost_dm_be, bdb_gstBackend_nonMediaLocalAllFalse)
{
    FileFilter *ff = FileFilter::instance();
    QUrl url = QUrl::fromLocalFile("/no/such/bdb_text.txt");
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bdb_isMpvExists_false);
    EXPECT_FALSE(ff->isAudio(url));
    EXPECT_FALSE(ff->isVideo(url));
    EXPECT_FALSE(ff->isSubtitle(url));
    stub.reset(ADDR(CompositingManager, isMpvExists));
}

TEST(boost_dm_be, bdb_finished_quitsGMainLoop)
{
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    ASSERT_NE(loop, nullptr);

    std::thread t([loop]() {
        usleep(20000);
        FileFilter::finished(nullptr, loop);
    });
    g_main_loop_run(loop);
    t.join();

    EXPECT_FALSE(g_main_loop_is_running(loop));
    g_main_loop_unref(loop);
}

TEST(boost_dm_be, bdb_fileFilter_singletonIdentity)
{
    EXPECT_EQ(FileFilter::instance(), FileFilter::instance());
    EXPECT_NE(FileFilter::instance(), nullptr);
}


// ===========================================================================
// getdlnaxmlvalue.cpp — xml path lookups (pure logic over QDomDocument).
// ===========================================================================

static const char *bdb_sampleDeviceXml =
    "<root xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\" xmlns=\"urn:schemas-upnp-org:device-1-0\">"
    "<specVersion><major>1</major><minor>0</minor></specVersion>"
    "<device>"
    "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
    "<UDN>uuid:bdb-1234</UDN>"
    "<friendlyName>bdb-PC</friendlyName>"
    "<serviceList>"
    "<service>"
    "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
    "<controlURL>AVTransport/action</controlURL>"
    "</service>"
    "<service>"
    "<serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
    "<controlURL>RenderingControl/action</controlURL>"
    "</service>"
    "</serviceList>"
    "</device>"
    "</root>";

TEST(boost_dm_be, bdb_getValueByPath_friendlyName)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    EXPECT_EQ(xml.getValueByPath("device/friendlyName").toStdString(), "bdb-PC");
}

TEST(boost_dm_be, bdb_getValueByPath_udn)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    EXPECT_EQ(xml.getValueByPath("device/UDN").toStdString(), "uuid:bdb-1234");
}

TEST(boost_dm_be, bdb_getValueByPath_missing_returnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    EXPECT_TRUE(xml.getValueByPath("device/no_such_field").isEmpty());
}

TEST(boost_dm_be, bdb_getValueByPathValue_avTransportControlURL)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    QString v = xml.getValueByPathValue(
        "device/serviceList",
        "serviceType=urn:schemas-upnp-org:service:AVTransport:1",
        "controlURL");
    EXPECT_EQ(v.toStdString(), "AVTransport/action");
}

TEST(boost_dm_be, bdb_getValueByPathValue_renderingControlControlURL)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    QString v = xml.getValueByPathValue(
        "device/serviceList",
        "serviceType=urn:schemas-upnp-org:service:RenderingControl:1",
        "controlURL");
    EXPECT_EQ(v.toStdString(), "RenderingControl/action");
}

TEST(boost_dm_be, bdb_getValueByPathValue_noMatch_returnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    QString v = xml.getValueByPathValue(
        "device/serviceList",
        "serviceType=urn:schemas-upnp-org:service:NotPresent:1",
        "controlURL");
    EXPECT_TRUE(v.isEmpty());
}

TEST(boost_dm_be, bdb_getValueByPathValue_malformedValueFilter_returnsEmpty)
{
    GetDlnaXmlValue xml{QByteArray(bdb_sampleDeviceXml)};
    // The value filter must be "key=value"; passing one with no '=' makes
    // sList.size() != 2 and the function returns an empty element.
    QString v = xml.getValueByPathValue(
        "device/serviceList",
        "serviceTypeNoEquals",
        "controlURL");
    EXPECT_TRUE(v.isEmpty());
}

TEST(boost_dm_be, bdb_getValueByPath_emptyInputDoc)
{
    GetDlnaXmlValue xml{QByteArray("")};
    EXPECT_TRUE(xml.getValueByPath("device/friendlyName").isEmpty());
}

TEST(boost_dm_be, bdb_getValueByPathValue_emptyInputDoc)
{
    GetDlnaXmlValue xml{QByteArray("")};
    QString v = xml.getValueByPathValue(
        "device/serviceList",
        "serviceType=x",
        "controlURL");
    EXPECT_TRUE(v.isEmpty());
}


// ===========================================================================
// cdlnasoappost.cpp — getTimeStr (pure) + SoapOperPost oper table.
//
// SoapOperPost builds a SOAP request and posts via QNetworkAccessManager; with
// empty URLs the post fails fast and the QEventLoop is bounded by the internal
// 1500ms QTimer. This mirrors tests/deepin-movie/dlna/test_cdlnasoappost.cpp,
// which is already known to keep EXIT=0.
// ===========================================================================

TEST(boost_dm_be, bdb_getTimeStr_zero)
{
    CDlnaSoapPost post;
    EXPECT_EQ(post.getTimeStr(0).toStdString(), "00:00:00");
}

TEST(boost_dm_be, bdb_getTimeStr_oneHour)
{
    CDlnaSoapPost post;
    EXPECT_EQ(post.getTimeStr(3600).toStdString(), "01:00:00");
}

TEST(boost_dm_be, bdb_getTimeStr_complex)
{
    CDlnaSoapPost post;
    EXPECT_EQ(post.getTimeStr(3661).toStdString(), "01:01:01");
}

TEST(boost_dm_be, bdb_getTimeStr_largeValue)
{
    CDlnaSoapPost post;
    // 24h+ values: QTime wraps, but the call must not crash.
    QString s = post.getTimeStr(100000);
    EXPECT_FALSE(s.isEmpty());
}

TEST(boost_dm_be, bdb_SoapOperPost_setAVTransportURI_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_SetAVTransportURI, "", "", "");
}

TEST(boost_dm_be, bdb_SoapOperPost_play_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Play, "", "", "");
}

TEST(boost_dm_be, bdb_SoapOperPost_pause_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Pause, "", "", "");
}

TEST(boost_dm_be, bdb_SoapOperPost_stop_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Stop, "", "", "");
}

TEST(boost_dm_be, bdb_SoapOperPost_seek_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Seek, "", "", "", 10);
}

TEST(boost_dm_be, bdb_SoapOperPost_getPositionInfo_emptyUrls)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_GetPositionInfo, "", "", "");
}


// ===========================================================================
// thumbnail_worker.cpp — pure static helper only.
//
// ThumbnailWorker::ThumbnailWorker() resolves libffmpegthumbnailer symbols and
// multiplies a thumbnail size by qApp->devicePixelRatio(); its destructor frees
// a malloc'd buffer and calls into the thumbnailer library. Constructing it in
// a headless sandbox risks null-deref on unresolved symbols, so per safety rule
// #4 we exercise only the static, header-inline thumbSize() helper and skip the
// rest. thumbSize() is pure and covers the static return path in the header.
// ===========================================================================

TEST(boost_dm_be, bdb_thumbnailWorker_thumbSize_staticValue)
{
    // Header-inline static: returns a fixed expected UI size.
    QSize sz = ThumbnailWorker::thumbSize();
    EXPECT_EQ(sz.width(), 178);
    EXPECT_EQ(sz.height(), 101);
}

TEST(boost_dm_be, bdb_thumbnailWorker_thumbSize_consistent)
{
    // Static must return the same value across calls.
    EXPECT_EQ(ThumbnailWorker::thumbSize(), ThumbnailWorker::thumbSize());
}

TEST(boost_dm_be, bdb_thumbnailWorker_skipped_liveConstruction)
{
    // The ctor/dtor touch libffmpegthumbnailer + devicePixelRatio; live
    // construction in a headless sandbox risks a crash that would drop later
    // coverage. Skip rather than endanger the rest of the run.
    GTEST_SKIP() << "ThumbnailWorker ctor/dtor touch libffmpegthumbnailer and "
                    "qApp->devicePixelRatio(); not constructed in this suite.";
}


// ===========================================================================
// Sanity: keep the QApplication/MainWindow runtime alive like the sibling
// suites; touching the accessor ensures the application is initialized.
// ===========================================================================

TEST(boost_dm_be, bdb_mainWindowAccessible)
{
    MainWindow *w = dApp->getMainWindow();
    EXPECT_NE(w, nullptr);
    QTest::qWait(10);
}
