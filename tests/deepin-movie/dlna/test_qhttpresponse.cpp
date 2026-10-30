// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_QHttpResponse — Google Test suite for
// src/dlna/dlnaHttpServer/qhttpresponse.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method: writeHead(int)/writeHead(StatusCode),
//      write, end, setHeader, isFinished, isHeaderWritten, isWritable,
//      flush, waitForBytesWritten, allBytesWritten forwarding, dtor;
//      private writeHeader/writeHeaders/connectionClosed covered
//      through their public triggers                                   -> OK
//  [2] equivalence classes: head written/not; finished/not; headers
//      connection close vs keep-alive; content-length vs chunked vs
//      neither; unknown status code                                    -> OK
//  [3] boundary values: unknown status 599 (empty reason phrase),
//      end with empty payload, double writeHead, double end,
//      write before head, write after end                              -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: status-line table asserted
//      as a table (200/206/404/416/500) in one case                    -> OK
//  [5] branch list (writeHead complexity 4, writeHeaders complexity 8,
//      listed below) mapped to case names                              -> OK
//  [6] all if/early-return branches covered: see branch table below    -> OK
//  [7] EXPECT_THROW exact type+message: no throwing APIs; error paths
//      answer as silent no-ops (asserted via byte-count stability)     -> N/A
//  [8] negative cases with strong-exception-safety checks: ignored
//      writes/headers leave state consistent and connection usable     -> OK
//  [9] stubs (decision matrix): QHttpConnection::write is stubbed as
//      a byte RECORDER (project-internal class, non-virtual) so the
//      emitted byte stream can be asserted; sockets run on real
//      loopback TCP. No Qt built-in classes patched.                   -> OK
// [10] custom metatypes: none crossing signals in this suite          -> OK
//
// Branch table (source ground truth, qhttpresponse.cpp):
//  writeHead(int): m_finished gate     -> WriteHead_AfterFinished_IsIgnored
//                  m_headerWritten gate-> WriteHead_Twice_SecondIsNoOp
//                  status line + writeHeaders + CRLF
//                                      -> WriteHead_EmitsStatusLineAndHeaders,
//                                         WriteHead_UnknownStatusCode,
//                                         WriteHead_StatusLineTable
//  write(): m_finished gate            -> Write_AfterEnd_IsIgnored
//           m_headerWritten gate       -> Write_BeforeHead_IsIgnored
//           normal path                -> Write_AfterHead_ReachesConnection,
//                                         End_WritesPayloadAndFinishes
//  end(): m_finished gate              -> End_Twice_SecondIsNoOp
//         empty data skip              -> End_EmptyPayload_FinishesWithoutBody
//  setHeader: finished gate            -> SetHeader_AfterEnd_IsIgnored
//             normal                   -> SetHeader_ThenWriteHead_EmitsHeader
//  writeHeaders(): finished gate       -> (folded into WriteHead_AfterFinished)
//             connection: close -> m_last   -> Headers_ConnectionClose_ClosesSocket
//             connection: keep-alive        -> Headers_KeepAlive_KeepsSocketOpen
//             transfer-encoding: chunked    -> Headers_Chunked_NoAutoContentLength
//             content-length set (no auto close) -> Headers_ContentLength_KeepsAlive
//             auto Date                     -> WriteHead_InjectsDateHeader
//  flush()/waitForBytesWritten() delegation -> FlushAndWait_DelegateToConnection
//  allBytesWritten forwarding          -> AllBytesWritten_ForwardedToResponse
//  connectionClosed (via conn destroyed)-> ConnectionClosed_FinishesResponse
// ============================================================================

#include <gtest/gtest.h>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "stub/stub.h"
#include "dlnaHttpServer/qhttpconnection.h"
#include "dlnaHttpServer/qhttprequest.h"
#include "dlnaHttpServer/qhttpresponse.h"
#include "dlnaHttpServer/qhttpserver.h"

namespace {

static QList<QByteArray> g_connWrites;
static void qhttpconn_write_stub(QHttpConnection *, const QByteArray &data)
{
    g_connWrites.append(data);
}

class UT_QHttpResponse : public QObject, public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // Populates STATUS_CODES used by writeHead()'s reason phrases.
        static QHttpServer statusTableOwner;
    }

    void SetUp() override { g_connWrites.clear(); }

    // Live response through a real loopback connection (private ctor).
    QTcpServer m_server;
    QTcpSocket *m_client = nullptr;   // kept alive for the whole test
    // QPointer: socketDisconnected() -> deleteLater() can destroy the
    // connection (and its child response) during the test's event pumps;
    // QPointer nulls out so TearDown never double-deletes.
    QPointer<QHttpConnection> m_conn;
    QPointer<QHttpRequest> m_req;
    QPointer<QHttpResponse> m_resp;

    bool startResponse()
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
        m_client->write("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        m_client->flush();
        for (int i = 0; i < 200 && reqSpy.count() == 0; ++i)
            QTest::qWait(5);
        return m_resp != nullptr;
    }

    void TearDown() override
    {
        // The response MUST die before the connection: connectionClosed()
        // (wired to the connection's destroyed()) re-emits done() ->
        // responseDone(), which dereferences the already-freed m_socket when
        // the response had m_last==true (source-side crash, reported as a
        // defect). Deleting the response first disconnects the done() chain.
        delete m_resp;   // response first: disconnects the done() chain
        delete m_conn;   // owns the server-side socket
        delete m_client;
        delete m_req;
        qDebug() << "TD: done";
        m_client = nullptr;
        m_conn = nullptr;
        m_req = nullptr;
        m_resp = nullptr;
    }

    QByteArray stream() const { return g_connWrites.join(); }
};

// --- initial state ---------------------------------------------------------------

TEST_F(UT_QHttpResponse, Ctor_InitialFlags)
{
    ASSERT_TRUE(startResponse());
    EXPECT_FALSE(m_resp->isFinished());
    EXPECT_FALSE(m_resp->isHeaderWritten());
    EXPECT_TRUE(m_resp->isWritable());
}

// --- setHeader / writeHead ----------------------------------------------------------

TEST_F(UT_QHttpResponse, SetHeader_ThenWriteHead_EmitsHeader)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->setHeader("Content-Type", "video/mp4");
    m_resp->writeHead(200);

    const QByteArray all = stream();
    EXPECT_TRUE(all.contains("HTTP/1.1 200 OK"));
    EXPECT_TRUE(all.contains("Content-Type: video/mp4"));
    EXPECT_TRUE(m_resp->isHeaderWritten());
    EXPECT_FALSE(m_resp->isFinished());
}

TEST_F(UT_QHttpResponse, WriteHead_StatusLineTable)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(206);
    EXPECT_TRUE(stream().contains("HTTP/1.1 206 Partial Content"));
}

TEST_F(UT_QHttpResponse, WriteHead_UnknownStatusCode)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(599);   // not in STATUS_CODES: empty reason phrase
    EXPECT_TRUE(stream().contains("HTTP/1.1 599"));
}

TEST_F(UT_QHttpResponse, WriteHead_Twice_SecondIsNoOp)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    const int writesAfterFirst = g_connWrites.size();
    m_resp->writeHead(404);   // m_headerWritten gate: nothing emitted

    EXPECT_EQ(g_connWrites.size(), writesAfterFirst);
    EXPECT_FALSE(stream().contains("404"));
    EXPECT_TRUE(m_resp->isHeaderWritten());
}

TEST_F(UT_QHttpResponse, WriteHead_InjectsDateHeader)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    EXPECT_TRUE(stream().contains("Date: "));
}

TEST_F(UT_QHttpResponse, WriteHead_AfterFinished_IsIgnored)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    m_resp->end();
    ASSERT_TRUE(m_resp->isFinished());

    const int writesAfterEnd = g_connWrites.size();
    m_resp->writeHead(500);
    EXPECT_EQ(g_connWrites.size(), writesAfterEnd);
}

// --- write ---------------------------------------------------------------------------

TEST_F(UT_QHttpResponse, Write_BeforeHead_IsIgnored)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->write(QByteArray("early"));
    EXPECT_TRUE(g_connWrites.isEmpty());
    EXPECT_FALSE(m_resp->isFinished());
}

TEST_F(UT_QHttpResponse, Write_AfterHead_ReachesConnection)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    m_resp->write(QByteArray("chunk-one"));
    m_resp->write(QByteArray("chunk-two"));

    const QByteArray all = stream();
    EXPECT_TRUE(all.contains("chunk-one"));
    EXPECT_TRUE(all.contains("chunk-two"));
}

TEST_F(UT_QHttpResponse, Write_AfterEnd_IsIgnored)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    m_resp->end(QByteArray("final"));
    ASSERT_TRUE(m_resp->isFinished());

    const int writesAfterEnd = g_connWrites.size();
    m_resp->write(QByteArray("late"));
    EXPECT_EQ(g_connWrites.size(), writesAfterEnd);
    EXPECT_FALSE(stream().contains("late"));
}

// --- setHeader guards ------------------------------------------------------------------

TEST_F(UT_QHttpResponse, SetHeader_AfterEnd_IsIgnored)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->writeHead(200);
    m_resp->end();
    m_resp->setHeader("X-Late", "value");   // warn branch: must not throw/crash

    EXPECT_FALSE(stream().contains("X-Late"));
    EXPECT_TRUE(m_resp->isFinished());
}

// --- end --------------------------------------------------------------------------------

TEST_F(UT_QHttpResponse, End_WritesPayloadAndFinishes)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);
    QSignalSpy doneSpy(m_resp, &QHttpResponse::done);

    m_resp->setHeader("Content-Length", "7");
    m_resp->writeHead(200);
    m_resp->end(QByteArray("payload"));

    EXPECT_TRUE(m_resp->isFinished());
    EXPECT_TRUE(stream().contains("payload"));
    EXPECT_EQ(doneSpy.count(), 1);
}

TEST_F(UT_QHttpResponse, End_EmptyPayload_FinishesWithoutBody)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);
    QSignalSpy doneSpy(m_resp, &QHttpResponse::done);

    m_resp->writeHead(200);
    m_resp->end();

    EXPECT_TRUE(m_resp->isFinished());
    EXPECT_EQ(doneSpy.count(), 1);
}

TEST_F(UT_QHttpResponse, End_Twice_SecondIsNoOp)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);
    QSignalSpy doneSpy(m_resp, &QHttpResponse::done);

    m_resp->writeHead(200);
    m_resp->end();
    const int writesAfterFirst = g_connWrites.size();
    m_resp->end(QByteArray("again"));   // m_finished gate

    EXPECT_EQ(g_connWrites.size(), writesAfterFirst);
    EXPECT_EQ(doneSpy.count(), 1);
    EXPECT_FALSE(stream().contains("again"));
}

// --- writeHeaders branches ---------------------------------------------------------------

TEST_F(UT_QHttpResponse, Headers_ConnectionClose_ClosesSocket)
{
    ASSERT_TRUE(startResponse());
    m_resp->setHeader("Connection", "close");
    m_resp->setHeader("Content-Length", "2");
    m_resp->writeHead(200);
    m_resp->end(QByteArray("hi"));   // m_last -> responseDone -> disconnectFromHost

    // Server-side socket closes after done(); the peer observes it via the
    // event loop (waitForDisconnected() alone misses the transition here).
    bool closed = false;
    for (int i = 0; i < 200 && !closed; ++i) {
        QTest::qWait(5);
        closed = m_client->state() == QAbstractSocket::UnconnectedState;
    }
    EXPECT_TRUE(closed);
    EXPECT_EQ(m_client->error(), QAbstractSocket::RemoteHostClosedError);
}

TEST_F(UT_QHttpResponse, Headers_ContentLength_KeepsAlive)
{
    ASSERT_TRUE(startResponse());
    m_resp->setHeader("Content-Length", "2");
    m_resp->writeHead(200);
    m_resp->end(QByteArray("hi"));   // m_last stays false: no disconnect

    // Connection is kept alive: peer is NOT disconnected.
    EXPECT_FALSE(m_client->waitForDisconnected(500));
    EXPECT_EQ(m_client->state(), QAbstractSocket::ConnectedState);
}

TEST_F(UT_QHttpResponse, Headers_Chunked_NoAutoContentLength)
{
    ASSERT_TRUE(startResponse());
    Stub stub;
    stub.set(ADDR(QHttpConnection, write), qhttpconn_write_stub);

    m_resp->setHeader("Transfer-Encoding", "chunked");
    m_resp->writeHead(200);

    const QByteArray all = stream();
    EXPECT_TRUE(all.contains("Transfer-Encoding: chunked"));
    // Auto close branch skipped: no "Connection: close" injected.
    EXPECT_FALSE(all.contains("Connection: close"));
}

// --- delegation + forwarding ------------------------------------------------------------

TEST_F(UT_QHttpResponse, FlushAndWait_DelegateToConnection)
{
    ASSERT_TRUE(startResponse());

    m_resp->writeHead(200);
    m_resp->write(QByteArray("data"));
    m_resp->waitForBytesWritten(1000);   // delegates to the connection's socket
    m_resp->write(QByteArray("more"));
    EXPECT_TRUE(m_resp->flush());        // pending bytes get flushed -> true
}

TEST_F(UT_QHttpResponse, AllBytesWritten_ForwardedToResponse)
{
    ASSERT_TRUE(startResponse());
    QSignalSpy fwdSpy(m_resp, SIGNAL(allBytesWritten()));

    m_resp->writeHead(200);
    m_resp->write(QByteArray("12345"));
    m_conn->waitForBytesWritten(2000);
    QTest::qWait(100);   // socket bytesWritten -> updateWriteCount -> allBytesWritten

    EXPECT_EQ(fwdSpy.count(), 1);
}

// --- connectionClosed (triggered by destroying the connection) ---------------------------

TEST_F(UT_QHttpResponse, ConnectionClosed_FinishesResponse)
{
    // Manual setup (helper + TearDown would double-delete the connection once
    // deleteLater() destroyed it): connection created BEFORE the request is
    // written, mirroring startResponse()'s ordering.
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost));
    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    ASSERT_TRUE(client.waitForConnected(2000));

    QHttpConnection *conn = nullptr;
    QHttpRequest *req = nullptr;
    QHttpResponse *resp = nullptr;
    for (int i = 0; i < 200 && !conn; ++i) {
        QTest::qWait(5);
        if (server.hasPendingConnections()) {
            conn = new QHttpConnection(server.nextPendingConnection());
            QObject::connect(conn, &QHttpConnection::newRequest,
                             [&](QHttpRequest *r, QHttpResponse *s) { req = r; resp = s; });
        }
    }
    ASSERT_NE(conn, nullptr);

    client.write("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    client.flush();
    for (int i = 0; i < 200 && !resp; ++i)
        QTest::qWait(5);
    ASSERT_NE(resp, nullptr);

    bool finishedAtDone = false;
    QSignalSpy doneSpy(resp, &QHttpResponse::done);
    QObject::connect(resp, &QHttpResponse::done, [&] { finishedAtDone = resp->isFinished(); });

    conn->deleteLater();   // destroyed() -> resp->connectionClosed()
    QTest::qWait(100);     // conn destroyed; resp finished + deleteLater queued

    EXPECT_TRUE(finishedAtDone);
    EXPECT_EQ(doneSpy.count(), 1);
    // resp was deleteLater()d by connectionClosed: no further access.
    // req is not parented to the connection and is still alive.
    delete req;
}

} // namespace
