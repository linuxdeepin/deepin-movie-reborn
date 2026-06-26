// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for the low-coverage DLNA cluster (pure XML / HTTP /
// string logic). Suite name is "dlna_ext"; file-scope helpers / stubs use the
// "dx_" prefix. Drives many small cases with varied inputs to hit each branch:
//
//   * dlnacontentserver.cpp — Range::fromRange (every branch incl. start>end,
//     end>=length, start==length-1, open-ended clamping, negative length),
//     dlnaOrgPnFlags mime dispatch, dlnaContentFeaturesHeader flag/seek matrix,
//     dlnaOrgFlagsForFile/Streaming sprintf output, getters/setters,
//     null-guard early returns in stream*/sendEmptyResponse/seqWriteData,
//     initializeHttpServer on port 0 + idempotency.
//   * cdlnasoappost.cpp     — getTimeStr formatting via DLNA_Seek, every
//     SoapOperPost operation branch, the SetAVTransportURIResponse/
//     GetPositionInfoResponse reply-parsing paths (no network: empty URLs
//     fail fast, the internal 1500 ms QTimer bounds the event loop).
//   * getdlnaxmlvalue.cpp   — present / absent / malformed element branches,
//     null root, multi-service selection, getValueByPathValue selector split.
//   * cssdpsearch.cpp       — showDlnaCastAddr no-AVTransport / no-LOCATION /
//     LOCATION-without-space / well-formed branches, readMsg no-datagram path.
//
// Safety rules:
//   * GoogleTest only (TEST(...)). gtest_main supplies main(); never define
//     main() here.
//   * DlnaContentServer spawns a QThread on construction; we stub
//     QThread::start (mirroring test_dlnacontentserver.cpp) so the worker —
//     which would bind a real HTTP port and run an event loop — never starts.
//   * No outbound network: empty/routable-but-closed URLs fail the post fast;
//     the 1500 ms internal QTimer in SoapOperPost and QEventLoop::quit on
//     reply finished bound every loop.
//   * CSSDPSearch binds a real UDP socket; if construction fails (port busy)
//     we GTEST_SKIP the affected cases rather than crash.

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
// Qt headers
#include <QDomDocument>
#include <QDomElement>
#include <QHostAddress>
#include <QTcpServer>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QFile>
#include <QThread>
#include <QTime>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>
#include <QList>
#include <QObject>
#include <QEventLoop>
#include <QTimer>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#define protected public
#define private public
#include "src/dlna/dlnacontentserver.h"
#include "src/dlna/cdlnasoappost.h"
#include "src/dlna/getdlnaxmlvalue.h"
#include "src/dlna/cssdpsearch.h"
#undef protected
#undef private
#include "stub/stub.h"

// ===========================================================================
// Stub helpers — neutralise QThread::start so DlnaContentServer's worker
// thread (which binds a real HTTP port) never starts. The thread object still
// exists and is cleaned up in the destructor.
// ===========================================================================

static void dx_qThread_start_stub()
{
    return;
}

static void dx_stub_QThread_start(Stub &stub)
{
    stub.set(ADDR(QThread, start), dx_qThread_start_stub);
}

// ===========================================================================
// DlnaContentServer::Range::fromRange — exhaustive branch coverage
// ===========================================================================

TEST(dlna_ext, RangeFromRange_ClosedRangeUnknownLengthIsValid)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-100");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 100);
    EXPECT_EQ(r->rangeLength(), 91);
}

TEST(dlna_ext, RangeFromRange_OpenEndedUnknownLengthIsValid)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, -1);
}

TEST(dlna_ext, RangeFromRange_ZeroStartUnknownLengthIsValid)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=0-");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 0);
    EXPECT_EQ(r->end, -1);
}

TEST(dlna_ext, RangeFromRange_WithWhitespaceAroundEqualsIsValid)
{
    // The regex allows \s* on both sides of '='.
    auto r = DlnaContentServer::Range::fromRange("bytes  =  10-100");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 100);
}

TEST(dlna_ext, RangeFromRange_NoBytesPrefixRejected)
{
    auto r = DlnaContentServer::Range::fromRange("range=10-100");
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_EmptyStringRejected)
{
    auto r = DlnaContentServer::Range::fromRange("");
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_NonNumericStartRejected)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=abc-100");
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_StartGreaterThanEndUnknownLengthRejected)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=100-10");
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_WithinKnownLengthValid)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-50", 100);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 50);
}

TEST(dlna_ext, RangeFromRange_OpenEndedClampsEndToLastByte)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-", 100);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->start, 10);
    EXPECT_EQ(r->end, 99);
}

TEST(dlna_ext, RangeFromRange_ZeroToLastByteIsFull)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=0-99", 100);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->full());
}

TEST(dlna_ext, RangeFromRange_StartAtLastByteRejected)
{
    // length > 0 path requires start < length - 1.
    auto r = DlnaContentServer::Range::fromRange("bytes=99-100", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_StartBeyondLengthRejected)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=200-300", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_EndBeyondLengthRejected)
{
    auto r = DlnaContentServer::Range::fromRange("bytes=10-200", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_EndEqualsLengthRejected)
{
    // end must be < length; end == length (100) is rejected.
    auto r = DlnaContentServer::Range::fromRange("bytes=10-100", 100);
    EXPECT_FALSE(r.has_value());
}

TEST(dlna_ext, RangeFromRange_ZeroLengthTreatedAsUnknown)
{
    // length <= 0 is collapsed to -1; closed range still valid.
    auto r = DlnaContentServer::Range::fromRange("bytes=10-50", 0);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->end, 50);
    EXPECT_EQ(r->length, -1);
}

TEST(dlna_ext, Range_FullPredicateCoversOpenEndedAndClosed)
{
    DlnaContentServer::Range open{0, -1, 100};
    EXPECT_TRUE(open.full());
    DlnaContentServer::Range closed{0, 99, 100};
    EXPECT_TRUE(closed.full());
    DlnaContentServer::Range partial{5, 50, 100};
    EXPECT_FALSE(partial.full());
}

TEST(dlna_ext, Range_EqualityAndRangeLengthArithmetic)
{
    DlnaContentServer::Range a{10, 50, 100};
    DlnaContentServer::Range b{10, 50, 100};
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.rangeLength(), 41);
    DlnaContentServer::Range c{10, 49, 100};
    EXPECT_FALSE(a == c);
}

// ===========================================================================
// DlnaContentServer — dlnaOrgPnFlags mime dispatch (every return branch)
// ===========================================================================

TEST(dlna_ext, DlnaOrgPnFlags_MsvideoReturnsAvi)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("video/x-msvideo"), QStringLiteral("DLNA.ORG_PN=AVI"));
}

TEST(dlna_ext, DlnaOrgPnFlags_MsvideoCaseInsensitive)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("VIDEO/X-MSVIDEO"), QStringLiteral("DLNA.ORG_PN=AVI"));
}

TEST(dlna_ext, DlnaOrgPnFlags_AacReturnsAac)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("audio/aac"), QStringLiteral("DLNA.ORG_PN=AAC"));
}

TEST(dlna_ext, DlnaOrgPnFlags_AacpReturnsAac)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("audio/aacp"), QStringLiteral("DLNA.ORG_PN=AAC"));
}

TEST(dlna_ext, DlnaOrgPnFlags_MpegReturnsMp3)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("audio/mpeg"), QStringLiteral("DLNA.ORG_PN=MP3"));
}

TEST(dlna_ext, DlnaOrgPnFlags_WavReturnsLpcm)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("audio/vnd.wav"), QStringLiteral("DLNA.ORG_PN=LPCM"));
}

TEST(dlna_ext, DlnaOrgPnFlags_L16ReturnsLpcm)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("audio/L16"), QStringLiteral("DLNA.ORG_PN=LPCM"));
}

TEST(dlna_ext, DlnaOrgPnFlags_MatroskaReturnsMkv)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_EQ(srv.dlnaOrgPnFlags("video/x-matroska"), QStringLiteral("DLNA.ORG_PN=MKV"));
}

TEST(dlna_ext, DlnaOrgPnFlags_UnknownMimeReturnsEmpty)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_TRUE(srv.dlnaOrgPnFlags("application/octet-stream").isEmpty());
    EXPECT_TRUE(srv.dlnaOrgPnFlags("").isEmpty());
}

// ===========================================================================
// DlnaContentServer — dlnaContentFeaturesHeader flag/seek matrix
// (pnFlags empty × flags true/false × seek true/false, same for non-empty pn)
// ===========================================================================

TEST(dlna_ext, DlnaContentFeaturesHeader_EmptyPnFlagsTrueSeekTrue)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("application/octet-stream", true, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    EXPECT_TRUE(h.contains("DLNA.ORG_CI=0"));
    EXPECT_TRUE(h.contains("DLNA.ORG_FLAGS"));
    EXPECT_FALSE(h.contains("DLNA.ORG_PN"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_EmptyPnFlagsTrueSeekFalse)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("application/octet-stream", false, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    EXPECT_TRUE(h.contains("DLNA.ORG_FLAGS"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_EmptyPnFlagsFalseSeekTrue)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("application/octet-stream", true, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    EXPECT_FALSE(h.contains("DLNA.ORG_FLAGS"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_EmptyPnFlagsFalseSeekFalse)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("application/octet-stream", false, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    EXPECT_FALSE(h.contains("DLNA.ORG_FLAGS"));
    EXPECT_FALSE(h.contains("DLNA.ORG_PN"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_NonEmptyPnFlagsTrueSeekTrue)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("video/x-msvideo", true, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=AVI"));
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=01"));
    EXPECT_TRUE(h.contains("DLNA.ORG_FLAGS"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_NonEmptyPnFlagsTrueSeekFalse)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("audio/aac", false, true);
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=AAC"));
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
}

TEST(dlna_ext, DlnaContentFeaturesHeader_NonEmptyPnFlagsFalseSeekFalse)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString h = srv.dlnaContentFeaturesHeader("audio/mpeg", false, false);
    EXPECT_TRUE(h.contains("DLNA.ORG_PN=MP3"));
    EXPECT_TRUE(h.contains("DLNA.ORG_OP=00"));
    EXPECT_FALSE(h.contains("DLNA.ORG_FLAGS"));
}

// ===========================================================================
// DlnaContentServer — flag sprintf builders + getters/setters
// ===========================================================================

TEST(dlna_ext, DlnaOrgFlagsForFile_HasPrefixAndHexPayload)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString f = srv.dlnaOrgFlagsForFile();
    EXPECT_TRUE(f.startsWith("DLNA.ORG_FLAGS="));
    EXPECT_GT(f.length(), static_cast<int>(strlen("DLNA.ORG_FLAGS=")));
    // File flags carry byte-based-seek + streaming-transfer-mode + DLNA_V15.
    EXPECT_TRUE(f.contains("DLNA.ORG_FLAGS=2"));
}

TEST(dlna_ext, DlnaOrgFlagsForStreaming_HasPrefixAndHexPayload)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    QString s = srv.dlnaOrgFlagsForStreaming();
    EXPECT_TRUE(s.startsWith("DLNA.ORG_FLAGS="));
    EXPECT_GT(s.length(), static_cast<int>(strlen("DLNA.ORG_FLAGS=")));
}

TEST(dlna_ext, DlnaContentServer_SettersAndGettersRoundTrip)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    srv.setBaseUrl("http://127.0.0.1:8080/");
    srv.setDlnaFileName("clip.mkv");
    EXPECT_EQ(srv.getBaseUrl(), QStringLiteral("http://127.0.0.1:8080/"));
    // Thread is stubbed so the worker never sets m_bStartHttpServer.
    EXPECT_FALSE(srv.getIsStartHttpServer());
}

TEST(dlna_ext, DlnaContentServer_InitialStateBaseUrlEmpty)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    EXPECT_TRUE(srv.getBaseUrl().isEmpty());
}

// ===========================================================================
// DlnaContentServer — initializeHttpServer on port 0 (loopback) + idempotency
// ===========================================================================

TEST(dlna_ext, InitializeHttpServer_OnEphemeralPortSucceeds)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    bool ok = srv.initializeHttpServer(0);
    if (!ok) {
        GTEST_SKIP() << "Could not bind an ephemeral HTTP port on loopback";
    }
    EXPECT_TRUE(ok);
}

TEST(dlna_ext, InitializeHttpServer_SecondCallIsIdempotent)
{
    // The guard `if(!m_httpServer)` short-circuits the second call: listen is
    // never re-run so bServer stays false.
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    bool first = srv.initializeHttpServer(0);
    if (!first) {
        GTEST_SKIP() << "Could not bind an ephemeral HTTP port on loopback";
    }
    bool second = srv.initializeHttpServer(0);
    EXPECT_FALSE(second);
}

// ===========================================================================
// DlnaContentServer — null-guard early returns (no real request/response)
// ===========================================================================

TEST(dlna_ext, StreamFile_NullRequestAndResponseSafe)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    srv.streamFile("/nonexistent", "video/mp4", nullptr, nullptr);
    SUCCEED();
}

TEST(dlna_ext, StreamFileRange_NullRequestAndResponseSafe)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    auto file = std::make_shared<QFile>("/tmp/does-not-exist");
    srv.streamFileRange(file, nullptr, nullptr);
    SUCCEED();
}

TEST(dlna_ext, StreamFileNoRange_NullRequestAndResponseSafe)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    auto file = std::make_shared<QFile>("/tmp/does-not-exist");
    srv.streamFileNoRange(file, nullptr, nullptr);
    SUCCEED();
}

TEST(dlna_ext, SeqWriteData_NullResponseSafe)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    auto file = std::make_shared<QFile>("/tmp/does-not-exist");
    srv.seqWriteData(file, 0, nullptr);
    SUCCEED();
}

TEST(dlna_ext, SendEmptyResponse_NullResponseSafe)
{
    Stub stub;
    dx_stub_QThread_start(stub);
    DlnaContentServer srv;
    srv.sendEmptyResponse(nullptr, 404);
    SUCCEED();
}

// ===========================================================================
// GetDlnaXmlValue — present / absent / malformed branches
// ===========================================================================

TEST(dlna_ext, XmlValue_EmptyDataYieldsEmptyForAnyPath)
{
    GetDlnaXmlValue xml("");
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_SimpleLeafValue)
{
    GetDlnaXmlValue xml("<root><device><friendlyName>TV</friendlyName></device></root>");
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QStringLiteral("TV"));
}

TEST(dlna_ext, XmlValue_LeafValueIsTrimmed)
{
    GetDlnaXmlValue xml("<root><device><friendlyName>  TV  </friendlyName></device></root>");
    EXPECT_EQ(xml.getValueByPath("device/friendlyName"), QStringLiteral("TV"));
}

TEST(dlna_ext, XmlValue_MissingIntermediateSegmentYieldsEmpty)
{
    GetDlnaXmlValue xml("<root><device><modelName>X</modelName></device></root>");
    EXPECT_EQ(xml.getValueByPath("device/missing"), QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_MissingRootFirstChildFallsToSibling)
{
    // A leading text/processing-instruction node makes firstChild().toElement()
    // null; the code then walks to nextSibling(). We feed a comment node first
    // to exercise that fallback path.
    GetDlnaXmlValue xml("<!-- c --><root><a>1</a></root>");
    EXPECT_EQ(xml.getValueByPath("a"), QStringLiteral("1"));
}

TEST(dlna_ext, XmlValue_NoMatchingChildYieldsEmpty)
{
    GetDlnaXmlValue xml("<root><a>1</a></root>");
    EXPECT_EQ(xml.getValueByPath("a/b"), QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_MalformedSelectorYieldsEmpty)
{
    // sValue without "=" -> split size != 2 -> null element -> "".
    QByteArray data =
        "<root><device><serviceList>"
        "<service><serviceType>A</serviceType><controlURL>/a</controlURL></service>"
        "</serviceList></device></root>";
    GetDlnaXmlValue xml(data);
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList", "no-equals", "controlURL"),
              QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_NoMatchYieldsEmpty)
{
    QByteArray data =
        "<root><device><serviceList>"
        "<service><serviceType>A</serviceType><controlURL>/a</controlURL></service>"
        "</serviceList></device></root>";
    GetDlnaXmlValue xml(data);
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList",
                                      "serviceType=Z", "controlURL"),
              QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_NullParentYieldsEmpty)
{
    GetDlnaXmlValue xml("<root/>");
    EXPECT_EQ(xml.getValueByPathValue("missing/path", "k=v", "controlURL"),
              QStringLiteral(""));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_FirstMatchingServiceReturned)
{
    QByteArray data =
        "<root><device><serviceList>"
        "<service><serviceType>AVTransport:1</serviceType>"
        "  <controlURL>/avt</controlURL></service>"
        "</serviceList></device></root>";
    GetDlnaXmlValue xml(data);
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList",
                                      "serviceType=AVTransport:1", "controlURL"),
              QStringLiteral("/avt"));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_SecondOfTwoServicesSelected)
{
    // The loop must skip the non-matching first service and pick the second.
    QByteArray data =
        "<root><device><serviceList>"
        "<service><serviceType>RenderingControl:1</serviceType>"
        "  <controlURL>/rc</controlURL></service>"
        "<service><serviceType>AVTransport:1</serviceType>"
        "  <controlURL>/avt</controlURL></service>"
        "</serviceList></device></root>";
    GetDlnaXmlValue xml(data);
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList",
                                      "serviceType=AVTransport:1", "controlURL"),
              QStringLiteral("/avt"));
}

TEST(dlna_ext, XmlValue_GetValueByPathValue_MatchButMissingTargetElement)
{
    // serviceType matches but the requested controlURL element is absent.
    QByteArray data =
        "<root><device><serviceList>"
        "<service><serviceType>AVTransport:1</serviceType></service>"
        "</serviceList></device></root>";
    GetDlnaXmlValue xml(data);
    EXPECT_EQ(xml.getValueByPathValue("device/serviceList",
                                      "serviceType=AVTransport:1", "controlURL"),
              QStringLiteral(""));
}

// ===========================================================================
// CDlnaSoapPost — getTimeStr formatting (private) via DLNA_Seek
// ===========================================================================

TEST(dlna_ext, SoapPost_GetTimeStrViaSeekFormatsAsHours)
{
    // DLNA_Seek formats the target via getTimeStr(); 3661s -> 01:01:01.
    // Empty URLs make the post fail fast; the internal 1500 ms timer bounds the
    // event loop, so this case cannot hang.
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Seek, "", "", "", 3661);
    SUCCEED();
}

TEST(dlna_ext, SoapPost_GetTimeStrZeroSeek)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Seek, "", "", "", 0);
    SUCCEED();
}

// ===========================================================================
// CDlnaSoapPost — every SoapOperPost operation branch (no network)
// ===========================================================================

TEST(dlna_ext, SoapPost_SetAVTransportURIBranch)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_SetAVTransportURI, "", "", "http://127.0.0.1:9/file");
    SUCCEED();
}

TEST(dlna_ext, SoapPost_PlayBranch)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Play, "", "", "");
    SUCCEED();
}

TEST(dlna_ext, SoapPost_PauseBranch)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Pause, "", "", "");
    SUCCEED();
}

TEST(dlna_ext, SoapPost_StopBranch)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_Stop, "", "", "");
    SUCCEED();
}

TEST(dlna_ext, SoapPost_GetPositionInfoBranch)
{
    CDlnaSoapPost post;
    post.SoapOperPost(DLNA_GetPositionInfo, "", "", "");
    SUCCEED();
}

// ===========================================================================
// CSSDPSearch — showDlnaCastAddr branch coverage (no outbound network)
// ===========================================================================

TEST(dlna_ext, SsdpSearch_ShowDlnaCastAddr_NoAvTransportIsIgnored)
{
    CSSDPSearch s;
    s.showDlnaCastAddr(QByteArray("NOTIFY * HTTP/1.1\r\nLOCATION: http://x\r\n"));
    SUCCEED();
}

TEST(dlna_ext, SsdpSearch_ShowDlnaCastAddr_AvTransportButNoLocation)
{
    CSSDPSearch s;
    s.showDlnaCastAddr(QByteArray("AVTransport here\r\nSome-Header: x\r\n"));
    SUCCEED();
}

TEST(dlna_ext, SsdpSearch_ShowDlnaCastAddr_LocationWithoutSpaceIsIgnored)
{
    // LOCATION line split by ' ' yields a 1-element list -> size < 2 -> skipped.
    CSSDPSearch s;
    s.showDlnaCastAddr(QByteArray("AVTransport\r\nLOCATION:http://no-space\r\n"));
    SUCCEED();
}

TEST(dlna_ext, SsdpSearch_ShowDlnaCastAddr_LocationWithSpaceStartsFetch)
{
    // Well-formed LOCATION triggers a QNetworkAccessManager::get; the request
    // target is unroutable (port 1) so it fails, and the inner QEventLoop is
    // quit by the reply's finished signal (no hang).
    CSSDPSearch s;
    s.showDlnaCastAddr(QByteArray("AVTransport\r\nLOCATION: http://127.0.0.1:1/desc.xml\r\n"));
    SUCCEED();
}

TEST(dlna_ext, SsdpSearch_ReadMsgNoDatagramsIsSafe)
{
    CSSDPSearch s;
    s.readMsg();
    SUCCEED();
}
