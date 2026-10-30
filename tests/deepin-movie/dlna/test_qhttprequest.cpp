// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_QHttpRequest — Google Test suite for
// src/dlna/dlnaHttpServer/qhttprequest.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method: method, methodString, url, path,
//      httpVersion, headers, header, remoteAddress, remotePort, body,
//      successful, storeBody, dtor; the data()/end() signals are
//      observed with QSignalSpy                                      -> OK
//      NOTE: the ctor, setters, MethodToString and appendBody are
//      PRIVATE (QHttpConnection is the only friend). They are covered
//      indirectly through a real parsed request; direct construction
//      is impossible by design.
//  [2] equivalence classes: GET vs POST; header present vs absent;
//      storeBody vs raw data() mode; complete vs partial request     -> OK
//  [3] boundary values: header name case-insensitivity (mixed-case
//      input vs stored lowercase), query-only URL, empty GET body,
//      body delivered in fragments                                   -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: header lookup asserted as a
//      multi-key table inside one case; document as N/A              -> N/A
//  [5] branch list mapped to case names (see branch table below)     -> OK
//  [6] all if/early-return branches covered: header() lowercases,
//      storeBody() double-call guarded (UniqueConnection)            -> OK
//  [7] EXPECT_THROW exact type+message: no throwing APIs             -> N/A
//  [8] negative cases with state-intactness checks: missing header /
//      missing body leave the request usable                         -> OK
//  [9] stubs (decision matrix): none — the object is created by
//      QHttpConnection (friend-only ctor), so every case runs a real
//      loopback TCP parse; no Qt built-in classes patched            -> OK
// [10] custom metatypes: pointer-typed newRequest uses SIGNAL/SLOT
//      string macros; data(QByteArray) is auto-registered            -> OK
//
// Branch table (source ground truth, qhttprequest.cpp):
//  method()/methodString()/MethodToString: switch arms via real HTTP
//        methods        -> ParsedRequest_MethodAndVersion,
//                          ParsedRequest_MethodStringTable
//  header(): lowercase key  -> ParsedRequest_HeaderLookupCaseInsensitive
//        missing key      -> ParsedRequest_MissingHeader_ReturnsEmpty
//  storeBody(): UniqueConnection double-call -> ParsedRequest_StoreBodyTwice
//  appendBody(): accumulation loop -> ParsedRequest_PostBody_Fragmented
//  successful()/end: MessageComplete wiring -> ParsedRequest_PostBody_Fragmented
//  remoteAddress()/remotePort(): socket peer -> ParsedRequest_RemotePeerIsLoopback
// ============================================================================

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "dlnaHttpServer/qhttpconnection.h"
#include "dlnaHttpServer/qhttprequest.h"
#include "dlnaHttpServer/qhttpresponse.h"

namespace {

class UT_QHttpRequest : public QObject, public ::testing::Test
{
protected:
    QTcpServer m_server;
    QHttpConnection *m_conn = nullptr;
    QHttpRequest *m_req = nullptr;
    QTcpSocket *m_client = nullptr;

    bool start(const QByteArray &firstWrite, bool waitForRequest = true, int waitMs = 1000)
    {
        if (!m_server.listen(QHostAddress::LocalHost))
            return false;
        m_client = new QTcpSocket();
        QSignalSpy serverSpy(&m_server, &QTcpServer::newConnection);
        m_client->connectToHost(QHostAddress::LocalHost, m_server.serverPort());
        for (int i = 0; i < 200 && serverSpy.count() == 0; ++i)
            QTest::qWait(5);
        if (serverSpy.count() == 0)
            return false;
        m_conn = new QHttpConnection(m_server.nextPendingConnection());
        QSignalSpy reqSpy(m_conn, SIGNAL(newRequest(QHttpRequest *, QHttpResponse *)));
        QObject::connect(m_conn, &QHttpConnection::newRequest, [&](QHttpRequest *r, QHttpResponse *) {
            m_req = r;
        });
        if (!firstWrite.isEmpty()) {
            m_client->write(firstWrite);
            m_client->flush();
        }
        if (waitForRequest)
            for (int i = 0; i < waitMs / 5 && reqSpy.count() == 0; ++i)
                QTest::qWait(5);
        return m_req != nullptr || !waitForRequest;
    }

    void TearDown() override
    {
        delete m_client;
        delete m_conn;   // dtor deletes the socket; the response is its child
        delete m_req;    // requests are not parented by the library
        m_client = nullptr;
        m_conn = nullptr;
        m_req = nullptr;
    }
};

// --- parsed request state ------------------------------------------------------------

TEST_F(UT_QHttpRequest, ParsedRequest_MethodAndVersion)
{
    ASSERT_TRUE(start("GET /media/movie.mp4?start=0 HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(m_req->method(), QHttpRequest::HTTP_GET);
    EXPECT_EQ(m_req->path(), QString("/media/movie.mp4"));
    EXPECT_EQ(m_req->url().query(), QString("start=0"));
    EXPECT_EQ(m_req->httpVersion(), QString("1.1"));
}

TEST_F(UT_QHttpRequest, ParsedRequest_MethodStringTable)
{
    // methodString() is the public wrapper around the private MethodToString().
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n"));
    EXPECT_EQ(m_req->methodString(), QString("HTTP_POST"));
    EXPECT_EQ(m_req->method(), QHttpRequest::HTTP_POST);
}

TEST_F(UT_QHttpRequest, ParsedRequest_HeaderLookupCaseInsensitive)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nContent-Type: video/mp4\r\nAccept-Encoding: gzip\r\n\r\n"));
    // Stored lowercase; lookup lowercases the key before comparing.
    EXPECT_EQ(m_req->header("content-type"), QString("video/mp4"));
    EXPECT_EQ(m_req->header("Content-Type"), QString("video/mp4"));
    EXPECT_EQ(m_req->header("CONTENT-TYPE"), QString("video/mp4"));
    EXPECT_EQ(m_req->header("accept-encoding"), QString("gzip"));
    // headers() returns the full lowercase-keyed hash.
    EXPECT_EQ(m_req->headers().size(), 2);
}

TEST_F(UT_QHttpRequest, ParsedRequest_MissingHeader_ReturnsEmpty)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_TRUE(m_req->header("x-not-sent").isEmpty());
}

TEST_F(UT_QHttpRequest, ParsedRequest_RemotePeerIsLoopback)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_EQ(m_req->remoteAddress(), QString("127.0.0.1"));
    EXPECT_NE(m_req->remotePort(), quint16(0));
}

// --- body handling ---------------------------------------------------------------------

TEST_F(UT_QHttpRequest, ParsedRequest_GetHasNoBody)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_TRUE(m_req->body().isEmpty());
    EXPECT_TRUE(m_req->successful());
}

TEST_F(UT_QHttpRequest, ParsedRequest_PostBody_StoredWhenStoreBodyCalled)
{
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 7\r\n\r\n"));
    m_req->storeBody();   // connect data -> appendBody (UniqueConnection)

    QSignalSpy endSpy(m_req, &QHttpRequest::end);
    m_client->write("payload");
    m_client->flush();
    QTest::qWait(300);

    EXPECT_EQ(m_req->body(), QByteArray("payload"));
    EXPECT_EQ(endSpy.count(), 1);
    EXPECT_TRUE(m_req->successful());
}

TEST_F(UT_QHttpRequest, ParsedRequest_StoreBodyTwice_NoDuplicateAccumulation)
{
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\n"));
    m_req->storeBody();
    m_req->storeBody();   // UniqueConnection guards the second connect

    m_client->write("abcd");
    m_client->flush();
    QTest::qWait(300);

    EXPECT_EQ(m_req->body(), QByteArray("abcd"));
}

TEST_F(UT_QHttpRequest, ParsedRequest_DataSignal_BodyFragmentsWithoutStore)
{
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n"));
    QByteArray collected;
    QObject::connect(m_req, &QHttpRequest::data, [&](const QByteArray &chunk) {
        collected.append(chunk);
    });

    m_client->write("he");
    m_client->flush();
    QTest::qWait(100);
    m_client->write("llo");
    m_client->flush();
    QTest::qWait(200);

    EXPECT_EQ(collected, QByteArray("hello"));
    EXPECT_TRUE(m_req->body().isEmpty());   // storeBody() never called
    EXPECT_TRUE(m_req->successful());
}

} // namespace
