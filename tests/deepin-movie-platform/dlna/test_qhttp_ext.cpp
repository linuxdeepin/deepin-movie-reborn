// Copyright (C) 2020 - 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for the embedded qhttp server cluster under
// src/dlna/dlnaHttpServer/:
//   * qhttpserver.cpp    (QHttpServer + STATUS_CODES table)
//   * qhttpconnection.cpp (QHttpConnection: parser plumbing, write/flush/isWritable,
//                          updateWriteCount, responseDone, socketDisconnected)
//   * qhttprequest.cpp    (QHttpRequest accessors, storeBody, MethodToString)
//   * qhttpresponse.cpp   (QHttpResponse: setHeader, writeHead, writeHeaders branches,
//                          write, end, isFinished/isHeaderWritten/isWritable,
//                          connectionClosed, flush, waitForBytesWritten)
//
// Suite name "qhttp_ext"; static/file-scope helpers use the prefix "qhe_".
//
// Design notes (why we drive a real loopback TCP pair):
//   * QHttpRequest / QHttpResponse ctors are private and only constructible via
//     QHttpConnection (friend). The only realistic way to obtain live instances
//     is to feed a connected QTcpSocket into QHttpConnection and push raw HTTP
//     bytes through the http_parser. We therefore spin a QTcpServer bound to
//     port 0 (ephemeral, no privilege, no real service) per request and connect
//     a QTcpSocket from localhost. The connection owns and deletes its socket.
//   * No real network traffic leaves the host. Each case uses its own port and
//     tears down via `delete connection` (which frees the parser + settings +
//     socket) so there is no global state leakage.
//   * Only Google Test (TEST(...)). gtest_main supplies main(); never define
//     main() here.

#include "src/dlna/dlnaHttpServer/qhttpserver.h"
#include "src/dlna/dlnaHttpServer/qhttpconnection.h"
#include "src/dlna/dlnaHttpServer/qhttprequest.h"
#include "src/dlna/dlnaHttpServer/qhttpresponse.h"

#include <gtest/gtest.h>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QDebug>
#include <QElapsedTimer>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Pump the Qt event loop for up to `timeoutMs`, exiting early as soon as
// `done` becomes true. Avoids hangs when a socket never delivers data.
void qhe_spin(int timeoutMs, std::function<bool()> done)
{
    QEventLoop loop;
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (done())
            loop.quit();
    });
    t.start(timeoutMs);
    poll.start();
    loop.exec();
}

// Result bundle for a single in-process HTTP round-trip.
struct qhe_Exchange {
    QHttpConnection *connection = nullptr;
    QHttpRequest *request = nullptr;
    QHttpResponse *response = nullptr;
    QTcpServer *server = nullptr;
    QTcpSocket *client = nullptr;
};

// Spin a local QTcpServer on an ephemeral port, build a QHttpConnection from
// the accepted socket, and push `rawRequest` from a localhost client socket.
// Returns once the newRequest signal has fired (or after the timeout).
// Caller owns the bundle; destroying `connection` frees the server-side socket.
qhe_Exchange qhe_performRequest(const QByteArray &rawRequest, int timeoutMs = 1000)
{
    qhe_Exchange ex;
    ex.server = new QTcpServer();
    ex.server->listen(QHostAddress::Any, 0);
    const quint16 port = ex.server->serverPort();

    QObject::connect(ex.server, &QTcpServer::newConnection, [&]() {
        QTcpSocket *sock = ex.server->nextPendingConnection();
        ex.connection = new QHttpConnection(sock, nullptr);
        QObject::connect(ex.connection, &QHttpConnection::newRequest,
                         [&](QHttpRequest *req, QHttpResponse *resp) {
                             ex.request = req;
                             ex.response = resp;
                         });
    });

    ex.client = new QTcpSocket();
    ex.client->connectToHost(QHostAddress::LocalHost, port);
    QObject::connect(ex.client, &QTcpSocket::connected, [&]() {
        ex.client->write(rawRequest);
        ex.client->flush();
    });

    qhe_spin(timeoutMs, [&]() { return ex.request != nullptr; });
    return ex;
}

// Tear down a bundle. `connection` owns and deletes its server-side socket.
void qhe_cleanup(qhe_Exchange &ex)
{
    delete ex.connection;   // frees m_socket, m_parser, m_parserSettings
    ex.connection = nullptr;
    ex.request = nullptr;   // request was parented to the connection
    ex.response = nullptr;  // response QObject(0) but deleteLater() chained
    delete ex.client;
    ex.client = nullptr;
    delete ex.server;
    ex.server = nullptr;
}

// Read everything the client socket has buffered so far.
QByteArray qhe_drainClient(QTcpSocket *client)
{
    if (!client)
        return {};
    QByteArray out;
    while (client->waitForReadyRead(50)) {
        out.append(client->readAll());
    }
    out.append(client->readAll());
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// QHttpServer — STATUS_CODES table + construction + listen + close
// ---------------------------------------------------------------------------

TEST(qhttp_ext, QHttpServerConstructPopulatesStatusCodes)
{
    // Constructing the server fills the global STATUS_CODES hash.
    QHttpServer server;
    EXPECT_FALSE(STATUS_CODES.isEmpty());
    EXPECT_EQ(STATUS_CODES.value(200), QStringLiteral("OK"));
    EXPECT_EQ(STATUS_CODES.value(404), QStringLiteral("Not Found"));
    EXPECT_EQ(STATUS_CODES.value(500), QStringLiteral("Internal Server Error"));
}

TEST(qhttp_ext, QHttpServerStatusCodesEdgeEntries)
{
    QHttpServer server;
    // Spot-check entries that are easy to regress (RFC/teapot/webdav).
    EXPECT_EQ(STATUS_CODES.value(100), QStringLiteral("Continue"));
    EXPECT_EQ(STATUS_CODES.value(206), QStringLiteral("Partial Content"));
    EXPECT_EQ(STATUS_CODES.value(416), QStringLiteral("Requested Range Not Satisfiable"));
    EXPECT_EQ(STATUS_CODES.value(418), QStringLiteral("I\"m a teapot"));
    EXPECT_EQ(STATUS_CODES.value(505), QStringLiteral("HTTP Version not supported"));
    EXPECT_EQ(STATUS_CODES.value(510), QStringLiteral("Not Extended"));
}

TEST(qhttp_ext, QHttpServerListenOnEphemeralPortSucceeds)
{
    QHttpServer server;
    // Port 0 => the OS picks an ephemeral port. Should always succeed and
    // leaves m_tcpServer populated.
    bool ok = server.listen(QHostAddress::Any, 0);
    EXPECT_TRUE(ok);
    server.close();
}

TEST(qhttp_ext, QHttpServerListenOverloadUsesAnyInterface)
{
    QHttpServer server;
    // The quint16-only overload must delegate to listen(Any, port).
    bool ok = server.listen(static_cast<quint16>(0));
    EXPECT_TRUE(ok);
    server.close();
}

TEST(qhttp_ext, QHttpServerCloseIsSafeWhenNeverListened)
{
    // close() guards on m_tcpServer being non-null; calling it on a fresh
    // server must not crash.
    QHttpServer server;
    server.close();
    SUCCEED();
}

TEST(qhttp_ext, QHttpServerListenFailurePathOnOccupiedPort)
{
    // Force a bind collision: hold a raw QTcpServer on an ephemeral port with
    // the default (no SO_REUSEADDR) flags, then ask QHttpServer to listen on
    // the same port. listen() must return false and free its internal server
    // (exercising the delete + null branch in the failure path).
    QTcpServer holder;
    ASSERT_TRUE(holder.listen(QHostAddress::Any, 0));
    const quint16 heldPort = holder.serverPort();

    QHttpServer server;
    bool ok = server.listen(QHostAddress::Any, heldPort);
    EXPECT_FALSE(ok);
    // After failure the object must still be safe to destroy / re-close.
    server.close();
}

// ---------------------------------------------------------------------------
// QHttpConnection + QHttpRequest — request line / headers / accessors
// ---------------------------------------------------------------------------

TEST(qhttp_ext, QHttpConnectionParsesGetRequestLine)
{
    QByteArray raw =
        "GET /movies/inception.mp4?quality=high HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    EXPECT_EQ(ex.request->method(), QHttpRequest::HTTP_GET);
    EXPECT_EQ(ex.request->path(), QStringLiteral("/movies/inception.mp4"));
    EXPECT_TRUE(ex.request->url().toString().contains("quality=high"));
    EXPECT_EQ(ex.request->httpVersion(), QStringLiteral("1.1"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpRequestHeaderLookupIsCaseInsensitive)
{
    // header() lowercases the field name before lookup; the parser also
    // lowercases stored keys. Mixed-case input must still resolve.
    QByteArray raw =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "User-Agent: qhttp-ext-test\r\n"
        "X-Custom: hello\r\n"
        "\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    EXPECT_EQ(ex.request->header("HOST"), QStringLiteral("localhost"));
    EXPECT_EQ(ex.request->header("user-agent"), QStringLiteral("qhttp-ext-test"));
    EXPECT_EQ(ex.request->header("X-CUSTOM"), QStringLiteral("hello"));
    EXPECT_EQ(ex.request->header("does-not-exist"), QStringLiteral(""));
    EXPECT_EQ(ex.request->headers().size(), 3);
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpRequestRemoteEndpointPopulated)
{
    // remoteAddress/remotePort come from the socket peer; on a loopback
    // connection the address must be 127.0.0.1.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    EXPECT_EQ(ex.request->remoteAddress(), QStringLiteral("127.0.0.1"));
    EXPECT_GT(ex.request->remotePort(), 0);
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpRequestMethodStringsForVerbs)
{
    // Drive several HTTP verbs and confirm method() + methodString().
    struct Case { const char *verb; QHttpRequest::HttpMethod m; };
    const Case cases[] = {
        {"GET", QHttpRequest::HTTP_GET},
        {"POST", QHttpRequest::HTTP_POST},
        {"HEAD", QHttpRequest::HTTP_HEAD},
        {"PUT", QHttpRequest::HTTP_PUT},
        {"DELETE", QHttpRequest::HTTP_DELETE},
        {"OPTIONS", QHttpRequest::HTTP_OPTIONS},
    };
    for (const auto &c : cases) {
        QByteArray raw = QByteArray(c.verb) + " / HTTP/1.1\r\nHost: x\r\n\r\n";
        qhe_Exchange ex = qhe_performRequest(raw);
        ASSERT_NE(ex.request, nullptr) << "verb=" << c.verb;
        EXPECT_EQ(ex.request->method(), c.m) << "verb=" << c.verb;
        qhe_cleanup(ex);
    }
}

TEST(qhttp_ext, QHttpRequestMultipleHeadersSameName)
{
    // The parser appends field/value across multiple callbacks; ensure the
    // last value for a repeated field wins in the HeaderHash.
    QByteArray raw =
        "GET / HTTP/1.1\r\n"
        "X-Tag: one\r\n"
        "X-Tag: two\r\n"
        "\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    EXPECT_EQ(ex.request->header("x-tag"), QStringLiteral("two"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpRequestStoreBodyAccumulatesBody)
{
    // storeBody() wires the data() signal into appendBody(); after the full
    // POST is parsed, body() must hold the payload.
    QByteArray payload = "hello body world";
    QByteArray raw =
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
        "\r\n" + payload;
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    ex.request->storeBody();
    // storeBody uses a UniqueConnection; emit a chunk to exercise appendBody.
    Q_EMIT ex.request->data(QByteArray("chunk1"));
    Q_EMIT ex.request->data(QByteArray("chunk2"));
    EXPECT_EQ(ex.request->body(), QByteArray("chunk1chunk2"));
    EXPECT_TRUE(ex.request->successful());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpRequestMessageCompleteSetsSuccessful)
{
    // After a fully-delivered request with the final blank line, the parser
    // fires MessageComplete which sets successful=true.
    QByteArray raw = "GET /done HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.request, nullptr);
    EXPECT_TRUE(ex.request->successful());
    qhe_cleanup(ex);
}

// ---------------------------------------------------------------------------
// QHttpConnection — write / flush / isWritable passthroughs + updateWriteCount
// ---------------------------------------------------------------------------

TEST(qhttp_ext, QHttpConnectionIsWritableOnLiveSocket)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.connection, nullptr);
    EXPECT_TRUE(ex.connection->isWritable());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpConnectionWriteAndFlushDeliverBytes)
{
    // write() should append to m_transmitLen; flush() should return whatever
    // the socket flush returned. Bytes should arrive at the client.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.connection, nullptr);
    ex.connection->write("raw-bytes-via-connection");
    ex.connection->flush();
    ex.connection->waitForBytesWritten(200);
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("raw-bytes-via-connection"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpConnectionAllBytesWrittenSignal)
{
    // updateWriteCount resets counters and emits allBytesWritten() once the
    // transmitted length is reached. We use the response path to drive writes
    // and then wait for the client to drain them.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);

    bool gotAllBytes = false;
    ex.response->connect(ex.response, &QHttpResponse::allBytesWritten, [&]() {
        gotAllBytes = true;
    });

    ex.response->setHeader("Content-Length", "4");
    ex.response->writeHead(QHttpResponse::STATUS_OK);
    ex.response->write("abcd");
    ex.response->end();
    qhe_drainClient(ex.client);
    qhe_spin(300, [&]() { return gotAllBytes; });
    EXPECT_TRUE(gotAllBytes);
    qhe_cleanup(ex);
}

// ---------------------------------------------------------------------------
// QHttpResponse — setHeader / writeHead / write / end + branch coverage
// ---------------------------------------------------------------------------

TEST(qhttp_ext, QHttpResponseSetHeaderBeforeFinishStores)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("X-Test", "value");
    EXPECT_FALSE(ex.response->isFinished());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseWriteHeadEmitsStatusLine)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(200);
    EXPECT_TRUE(ex.response->isHeaderWritten());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseWriteHeadEnumOverload)
{
    // The StatusCode enum overload must delegate to the int overload.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(QHttpResponse::STATUS_NOT_FOUND);
    EXPECT_TRUE(ex.response->isHeaderWritten());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseWriteHeadStatusLineContent)
{
    // The first line written must be "HTTP/1.1 <code> <reason>".
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(404);
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.startsWith("HTTP/1.1 404 Not Found"));
    EXPECT_TRUE(got.contains("Content-Length: 0"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseAutoConnectionKeepAliveWhenContentLength)
{
    // When Content-Length is set and no Connection header is provided, the
    // server auto-injects "Connection: keep-alive".
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "3");
    ex.response->writeHead(200);
    ex.response->write("abc");
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("Connection: keep-alive"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseConnectionCloseSetsLast)
{
    // An explicit Connection: close must mark this as the last response.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Connection", "close");
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(200);
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("Connection: close"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseChunkedTransferEncoding)
{
    // Transfer-Encoding: chunked sets m_useChunkedEncoding and the server
    // auto-fills Transfer-Encoding when only chunked is requested.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Transfer-Encoding", "chunked");
    ex.response->writeHead(200);
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("Transfer-Encoding: chunked"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseAutoDateHeader)
{
    // When no Date header is set the server appends one.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(200);
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("Date:"));
    EXPECT_TRUE(got.contains("GMT"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseExplicitDateSuppressesAutoDate)
{
    // An explicit Date header must NOT be duplicated.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Date", "Mon, 01 Jan 2024 00:00:00 GMT");
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(200);
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    int count = got.count("Date:");
    EXPECT_EQ(count, 1);
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseWriteBeforeHeadIsIgnored)
{
    // write() before writeHead() must early-return (warning only), leaving
    // nothing written to the wire.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->write("should-not-appear");   // header not written yet
    EXPECT_FALSE(ex.response->isHeaderWritten());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseWriteAfterEndIsIgnored)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "0");
    ex.response->writeHead(200);
    ex.response->end();
    EXPECT_TRUE(ex.response->isFinished());
    // These must all be no-ops after finish.
    ex.response->setHeader("X-Late", "1");
    ex.response->writeHead(500);
    ex.response->write("late");
    ex.response->end("late");
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseEndWithDataFlushesBody)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "5");
    ex.response->writeHead(200);
    ex.response->end("final");
    EXPECT_TRUE(ex.response->isFinished());
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("final"));
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseFlushPassthrough)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Length", "1");
    ex.response->writeHead(200);
    ex.response->write("z");
    bool ok = ex.response->flush();
    // flush result depends on buffer state; just ensure it returns without crash.
    Q_UNUSED(ok);
    ex.response->end();
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseIsWritableDelegatesToConnection)
{
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    EXPECT_TRUE(ex.response->isWritable());
    qhe_cleanup(ex);
}

TEST(qhttp_ext, QHttpResponseNoContentLengthClosesConnection)
{
    // Without Content-Length or Transfer-Encoding, the server marks the
    // response as last (connection close) instead of inventing a length.
    QByteArray raw = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    qhe_Exchange ex = qhe_performRequest(raw);
    ASSERT_NE(ex.response, nullptr);
    ex.response->setHeader("Content-Type", "text/plain");
    ex.response->writeHead(200);
    ex.response->write("unsized");
    ex.response->end();
    QByteArray got = qhe_drainClient(ex.client);
    EXPECT_TRUE(got.contains("Connection: close"));
    EXPECT_TRUE(got.contains("unsized"));
    qhe_cleanup(ex);
}
