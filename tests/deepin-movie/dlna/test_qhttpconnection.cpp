// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_QHttpConnection — Google Test suite for
// src/dlna/dlnaHttpServer/qhttpconnection.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method: ctor/dtor, parseRequest, write, flush,
//      isWritable, waitForBytesWritten, socketDisconnected (slot),
//      updateWriteCount (slot), responseDone (slot, via QHttpResponse
//      end flow), and all seven http_parser callbacks exercised
//      through real socket bytes                                       -> OK
//  [2] equivalence classes: complete GET/POST; fragmented URL / header
//      value / body; header with empty value; garbage request line;
//      disconnect before completion                                    -> OK
//  [3] boundary values: body split across two TCP writes, header value
//      accumulated from fragments, empty header value, garbage bytes   -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: fragmentation cases differ in
//      which parser callback accumulates (URL vs header vs body) and
//      in their observables — individual cases; documented as N/A      -> N/A
//  [5] branch list (socketDisconnected, updateWriteCount, responseDone)
//      mapped to case names                                            -> OK
//  [6] all if/early-return branches covered: see branch table below
//      (nextPendingConnection null-break lives in QHttpServer and needs
//      non-stubbable virtuals — documented N/C there)                  -> OK
//  [7] EXPECT_THROW exact type+message: no throwing APIs; malformed
//      input is answered by parser errors / no newRequest signal       -> N/A
//  [8] negative cases with strong-exception-safety checks: garbage
//      request and premature disconnect leave no crash and the
//      connection is cleaned up via deleteLater                        -> OK
//  [9] stubs (decision matrix): none — the parser cluster is exercised
//      with real bytes over real loopback TCP (the QHttpRequest/
//      QHttpResponse ctors are friend-only, so this is also the only
//      way to obtain them). Q_ASSERT-guarded paths (double listen,
//      malformed URL parse) are deliberately NOT triggered.            -> OK
// [10] custom metatypes: QHttpRequest*/QHttpResponse* pointers on
//      newRequest are auto-registered; QSignalSpy verified             -> OK
//
// Branch table (source ground truth, qhttpconnection.cpp):
//  ctor: parser/settings init + socket connects -> Ctor_* (implicit in all)
//  dtor: socket delete + parser free + settings delete -> Dtor_DeletesOwnedSocket
//  parseRequest: while bytesAvailable loop -> ParseRequest_FragmentedUrl,
//        ParseRequest_HeaderValueConcatenation, ParseRequest_PostBody,
//        ParseRequest_BodyFragments, ParseRequest_GarbageRequestLine
//  MessageBegin: new QHttpRequest per message  -> (implicit: newRequest emitted)
//  Url: m_currentUrl accumulation              -> ParseRequest_FragmentedUrl
//  HeaderField: flush previous pair (field+value non-empty gate)
//        -> ParseRequest_HeaderWithoutValue (pins the empty-value
//           name-merge defect), ParsedRequest_HeaderLookup* (request suite)
//  HeaderValue: value accumulation             -> ParseRequest_HeaderValueConcatenation
//  HeadersComplete: method/version/url + newRequest + done-connect
//        -> ParseRequest_PostBody, ParsedRequest_* (request suite)
//  Body: data(QByteArray) emission             -> ParseRequest_PostBody/_BodyFragments
//  MessageComplete: successful + end emission  -> ParseRequest_PostBody
//  socketDisconnected: deleteLater + unfinished request end
//        -> SocketDisconnected_UnfinishedRequest_CleansUp,
//           ParseRequest_GarbageRequestLine
//  updateWriteCount: pos==len -> allBytesWritten + counter reset
//        -> WriteAndWait_AllBytesWrittenTwice
//  write: socket write + transmitLen           -> Write_SendsBytesToPeer
//  flush/isWritable/waitForBytesWritten        -> FlushAndIsWritable
//  responseDone: m_last -> disconnect          -> Headers_ConnectionClose_ClosesSocket
//        (response suite)
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

class UT_QHttpConnection : public QObject, public ::testing::Test
{
protected:
    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;   // kept alive for the whole test
    QHttpConnection *m_conn = nullptr;
    QHttpRequest *m_req = nullptr;
    QHttpResponse *m_resp = nullptr;

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
        QSignalSpy reqSpy(m_conn, &QHttpConnection::newRequest);
        QObject::connect(m_conn, &QHttpConnection::newRequest, [&](QHttpRequest *r, QHttpResponse *s) {
            m_req = r;
            m_resp = s;
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
        delete m_conn;   // dtor deletes the owned socket; the response is its child
        delete m_client;
        delete m_req;    // requests are not parented by the library
        m_client = nullptr;
        m_conn = nullptr;
        m_req = nullptr;
        m_resp = nullptr;
    }
};

// --- ctor / dtor -----------------------------------------------------------------

TEST_F(UT_QHttpConnection, Dtor_DeletesOwnedSocket)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));

    delete m_conn;   // ~QHttpConnection deletes m_socket
    m_conn = nullptr;

    EXPECT_TRUE(m_client->waitForDisconnected(3000));
}

// --- parseRequest: URL / header / body callback cluster -----------------------------

TEST_F(UT_QHttpConnection, ParseRequest_FragmentedUrl)
{
    ASSERT_TRUE(start("GET /fr", false));   // partial request line: no request yet

    m_client->write("ag HTTP/1.1\r\nHost: x\r\n\r\n");
    m_client->flush();
    QTest::qWait(200);

    // m_req was replaced by the second (complete) request.
    ASSERT_NE(m_req, nullptr);
    EXPECT_EQ(m_req->path(), QString("/frag"));
}

TEST_F(UT_QHttpConnection, ParseRequest_HeaderValueConcatenation)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nX-Long: 123", false));
    m_client->write("45\r\nX-Short: ab\r\n\r\n");
    m_client->flush();
    QTest::qWait(200);

    ASSERT_NE(m_req, nullptr);
    EXPECT_EQ(m_req->header("x-long"), QString("12345"));
    EXPECT_EQ(m_req->header("x-short"), QString("ab"));
}

TEST_F(UT_QHttpConnection, ParseRequest_HeaderWithoutValue)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nX-Empty:\r\nX-Real: v\r\n\r\n"));
    ASSERT_NE(m_req, nullptr);
    // ACTUAL behavior (pins a suspected source defect for the defect export):
    // HeaderField() only flushes the pending pair when BOTH the field and the
    // value are non-empty. An empty value ("X-Empty:") leaves the pending field
    // set, and the next HeaderField() call APPENDS its name to the pending
    // field instead of starting a new one -> "x-emptyx-real". "x-real" alone
    // is never registered. The final pair is inserted unconditionally by
    // HeadersComplete(), which is how the merged key reaches the map.
    EXPECT_EQ(m_req->header("x-emptyx-real"), QString("v"));
    EXPECT_TRUE(m_req->header("x-real").isEmpty());
}

TEST_F(UT_QHttpConnection, ParseRequest_PostBody_DataAndEndSignals)
{
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n"));
    ASSERT_NE(m_req, nullptr);

    // Spies are attached before the body arrives, so both signals are seen.
    QSignalSpy dataSpy(m_req, SIGNAL(data(QByteArray)));
    QSignalSpy endSpy(m_req, &QHttpRequest::end);
    m_client->write("hello");
    m_client->flush();
    QTest::qWait(300);

    ASSERT_EQ(dataSpy.count(), 1);
    EXPECT_EQ(dataSpy.at(0).at(0).toByteArray(), QByteArray("hello"));
    EXPECT_EQ(endSpy.count(), 1);
    EXPECT_TRUE(m_req->successful());
}

TEST_F(UT_QHttpConnection, ParseRequest_BodyFragments)
{
    ASSERT_TRUE(start("POST /upload HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\n"));
    ASSERT_NE(m_req, nullptr);

    // The collector is attached before any body byte arrives.
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
    EXPECT_EQ(m_req->body(), QByteArray());   // storeBody() not requested
    EXPECT_TRUE(m_req->successful());
}

TEST_F(UT_QHttpConnection, ParseRequest_GarbageRequestLine_NoNewRequest)
{
    ASSERT_TRUE(start(QByteArray(), false));   // connection only, no valid request yet
    QSignalSpy reqSpy(m_conn, &QHttpConnection::newRequest);
    QSignalSpy destroyedSpy(m_conn, &QObject::destroyed);

    m_client->write("GARBAGE\r\n\r\n");
    m_client->flush();
    m_client->disconnectFromHost();

    QTest::qWait(300);   // parser error -> no request; disconnect -> deleteLater

    EXPECT_EQ(reqSpy.count(), 0);
    EXPECT_EQ(destroyedSpy.count(), 1);   // socketDisconnected scheduled the cleanup
    m_conn = nullptr;                     // already destroyed
}

TEST_F(UT_QHttpConnection, SocketDisconnected_UnfinishedRequest_CleansUp)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n", false));   // headers not complete
    QSignalSpy reqSpy(m_conn, &QHttpConnection::newRequest);
    QSignalSpy destroyedSpy(m_conn, &QObject::destroyed);

    m_client->disconnectFromHost();

    QTest::qWait(300);
    EXPECT_EQ(reqSpy.count(), 0);          // never completed
    EXPECT_EQ(destroyedSpy.count(), 1);    // deleteLater ran
    m_conn = nullptr;
}

// --- write path ----------------------------------------------------------------------

TEST_F(UT_QHttpConnection, Write_SendsBytesToPeer)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));

    m_conn->write(QByteArray("hello-peer"));
    m_conn->flush();

    EXPECT_TRUE(m_client->waitForReadyRead(2000));
    EXPECT_EQ(m_client->readAll(), QByteArray("hello-peer"));
}

TEST_F(UT_QHttpConnection, WriteAndWait_AllBytesWrittenTwice)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    QSignalSpy allSpy(m_conn, &QHttpConnection::allBytesWritten);

    m_conn->write(QByteArray("abcdef"));
    m_conn->waitForBytesWritten(2000);
    QTest::qWait(100);
    ASSERT_EQ(allSpy.count(), 1);   // counter reset after full flush

    m_conn->write(QByteArray("xy"));
    m_conn->waitForBytesWritten(2000);
    QTest::qWait(100);
    EXPECT_EQ(allSpy.count(), 2);   // reset counters make the second emit possible
}

TEST_F(UT_QHttpConnection, FlushAndIsWritable)
{
    ASSERT_TRUE(start("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    EXPECT_TRUE(m_conn->isWritable());
    m_conn->write(QByteArray("pending"));
    EXPECT_TRUE(m_conn->flush());
}

} // namespace
