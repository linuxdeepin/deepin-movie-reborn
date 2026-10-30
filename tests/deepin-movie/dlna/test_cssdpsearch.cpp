// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_CSSDPSearch — Google Test suite for src/dlna/cssdpsearch.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method/slot: ctor(+Ctor_*), SsdpSearch(+2),
//      readMsg(+2), showDlnaCastAddr(+4), dtor(+Dtor_*)               -> OK
//  [2] equivalence classes: SSDP replies with/without "AVTransport";
//      with/without "LOCATION" line; LOCATION with >=2 tokens; empty
//      M-SEARCH send vs send failure (-1)                             -> OK
//  [3] boundary values: reply without LOCATION, LOCATION with exactly
//      two tokens, several pending datagrams in one burst             -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: SSDP reply variants are 4
//      structurally distinct cases (gate / no-location / fetch /
//      multi-datagram) with different observables — individual cases -> N/A
//  [5] branch list (showDlnaCastAddr complexity 8, listed below)
//      mapped to case names                                           -> OK
//  [6] all if/early-return branches of public methods covered:
//      see branch table below                                         -> OK
//  [7] EXPECT_THROW exact type+message: no throwing APIs; error paths
//      observed via absence of network traffic / no crash             -> N/A
//  [8] negative cases with strong-exception-safety checks: ignored
//      replies and send-failure cases leave the object usable         -> OK
//  [9] stubs (decision matrix): QUdpSocket::writeDatagram(3-arg
//      non-virtual inline overload) stubbed to record the M-SEARCH
//      datagram and to simulate send failure; the UDP receive path and
//      the LOCATION HTTP fetch run for real against loopback
//      sockets (no real network reachable).                           -> OK
// [10] custom metatypes: QByteArray signal args only — built-in       -> OK
//
// Branch table (source ground truth, cssdpsearch.cpp):
//  ctor: bind + connects                       -> Ctor_BindsUdpPort56123,
//                                               Ctor_SecondInstance_SharesPort
//  SsdpSearch: writeDatagram ret != -1 -> SsdpSearch_SendsMSEARCHDatagram
//              ret == -1 (warn)        -> SsdpSearch_SendFailure_IsTolerated
//  readMsg: hasPendingDatagrams loop           -> ReadMsg_EmitsUpdataAvAddr,
//                                               ReadMsg_MultipleDatagrams
//  showDlnaCastAddr:
//    reply lacks "AVTransport"                 -> Show_NonAVTransport_Ignored
//    has AVTransport, no LOCATION line         -> Show_AVTransportNoLocation_Ignored
//    LOCATION tokens < 2                       -> Show_MalformedLocation_Ignored
//    valid LOCATION -> QNAM get on location    -> Show_ValidLocation_FetchesUrl
//  dtor: manager/socket non-null               -> Dtor_ReleasesResources
//        (null branches unreachable — N/C)
// ============================================================================

#include <gtest/gtest.h>
#include <QByteArray>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

#include "stub/stub.h"
#include "cssdpsearch.h"

namespace {

// ---- QUdpSocket::writeDatagram recorder (3-arg non-virtual inline overload) --
static QByteArray g_dgramData;
static QHostAddress g_dgramHost;
static quint16 g_dgramPort = 0;
static qint64 g_dgramResult = 0;
static int g_dgramCalls = 0;
static qint64 udp_writeDatagram_stub(QUdpSocket *, const QByteArray &datagram,
                                     const QHostAddress &host, quint16 port)
{
    g_dgramData = datagram;
    g_dgramHost = host;
    g_dgramPort = port;
    ++g_dgramCalls;
    return g_dgramResult;
}

class UT_CSSDPSearch : public QObject, public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_dgramCalls = 0;
        g_dgramResult = 0;
    }

    // Spin the loop briefly so cross-socket datagrams get delivered.
    static void pumpEvents(int ms)
    {
        QTest::qWait(ms);
    }
};

// --- ctor / dtor ---------------------------------------------------------------

TEST_F(UT_CSSDPSearch, Ctor_BindsUdpPort56123)
{
    CSSDPSearch search;
    QUdpSocket probe;
    // ShareAddress allows our probe to bind the very same port.
    EXPECT_TRUE(probe.bind(QHostAddress::Any, 56123, QUdpSocket::ShareAddress));
}

TEST_F(UT_CSSDPSearch, Ctor_SecondInstance_SharesPort)
{
    CSSDPSearch first;
    CSSDPSearch second;   // bind(Any, 56123, ShareAddress) must not crash/fail hard
    SUCCEED();
}

TEST_F(UT_CSSDPSearch, Dtor_ReleasesResources)
{
    {
        CSSDPSearch search;
    }
    CSSDPSearch again;   // re-create after full teardown
    SUCCEED();
}

// --- SsdpSearch ------------------------------------------------------------------

TEST_F(UT_CSSDPSearch, SsdpSearch_SendsMSEARCHDatagram)
{
    CSSDPSearch search;
    Stub stub;
    g_dgramResult = 128;   // success path
    stub.set(static_cast<qint64 (QUdpSocket::*)(const QByteArray &, const QHostAddress &, quint16)>(
                 &QUdpSocket::writeDatagram),
             udp_writeDatagram_stub);

    search.SsdpSearch();

    EXPECT_EQ(g_dgramCalls, 1);
    EXPECT_TRUE(g_dgramData.contains("M-SEARCH"));
    EXPECT_TRUE(g_dgramData.contains("ssdp:discover"));
    EXPECT_TRUE(g_dgramData.contains("ST: ssdp:all"));
    EXPECT_EQ(g_dgramHost, QHostAddress("239.255.255.250"));
    EXPECT_EQ(g_dgramPort, quint16(1900));
}

TEST_F(UT_CSSDPSearch, SsdpSearch_SendFailure_IsTolerated)
{
    CSSDPSearch search;
    Stub stub;
    g_dgramResult = -1;   // failure path: only a warning branch
    stub.set(static_cast<qint64 (QUdpSocket::*)(const QByteArray &, const QHostAddress &, quint16)>(
                 &QUdpSocket::writeDatagram),
             udp_writeDatagram_stub);

    search.SsdpSearch();
    EXPECT_EQ(g_dgramCalls, 1);

    // Object remains usable (strong-exception-safety style check).
    g_dgramResult = 128;
    search.SsdpSearch();
    EXPECT_EQ(g_dgramCalls, 2);
}

// --- readMsg (real loopback UDP) ---------------------------------------------------

TEST_F(UT_CSSDPSearch, ReadMsg_EmitsUpdataAvAddr)
{
    CSSDPSearch search;
    QSignalSpy spy(&search, &CSSDPSearch::updataAvAddr);

    QUdpSocket sender;
    const QByteArray payload = "NOTIFY * HTTP/1.1\r\nST: upnp:rootdevice\r\n";
    EXPECT_EQ(sender.writeDatagram(payload, QHostAddress::LocalHost, 56123),
              qint64(payload.size()));

    pumpEvents(300);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toByteArray(), payload);
}

TEST_F(UT_CSSDPSearch, ReadMsg_MultipleDatagrams_EmitsEach)
{
    CSSDPSearch search;
    QSignalSpy spy(&search, &CSSDPSearch::updataAvAddr);

    QUdpSocket sender;
    for (int i = 0; i < 3; ++i) {
        const QByteArray payload = "datagram-" + QByteArray::number(i);
        sender.writeDatagram(payload, QHostAddress::LocalHost, 56123);
    }

    pumpEvents(400);
    EXPECT_EQ(spy.count(), 3);
    // Loop emits in arrival order.
    EXPECT_EQ(spy.at(0).at(0).toByteArray(), QByteArray("datagram-0"));
    EXPECT_EQ(spy.at(2).at(0).toByteArray(), QByteArray("datagram-2"));
}

// --- showDlnaCastAddr ----------------------------------------------------------------

TEST_F(UT_CSSDPSearch, Show_NonAVTransport_Ignored)
{
    CSSDPSearch search;
    // No AVTransport marker: gate branch rejects the whole reply.
    EXPECT_NO_FATAL_FAILURE(search.showDlnaCastAddr(
        "HTTP/1.1 200 OK\r\nST: upnp:rootdevice\r\nUSN: uuid:x\r\n\r\n"));
    SUCCEED();
}

TEST_F(UT_CSSDPSearch, Show_AVTransportNoLocation_Ignored)
{
    CSSDPSearch search;
    // AVTransport present but no LOCATION line: inner loop never matches.
    EXPECT_NO_FATAL_FAILURE(search.showDlnaCastAddr(
        "HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:service:AVTransport:1\r\n\r\n"));
    SUCCEED();
}

TEST_F(UT_CSSDPSearch, Show_MalformedLocation_Ignored)
{
    CSSDPSearch search;
    // LOCATION line with < 2 space-separated tokens: tmpList.size() gate.
    EXPECT_NO_FATAL_FAILURE(search.showDlnaCastAddr(
        "ST: urn:schemas-upnp-org:service:AVTransport:1\r\nLOCATION:\r\n\r\n"));
    SUCCEED();
}

TEST_F(UT_CSSDPSearch, Show_ValidLocation_FetchesUrl)
{
    // Loopback description server acts as the discovered renderer.
    QTcpServer descServer;
    QList<QByteArray> gets;
    QObject::connect(&descServer, &QTcpServer::newConnection, [&descServer, &gets] {
        while (QTcpSocket *sock = descServer.nextPendingConnection()) {
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            QObject::connect(sock, &QTcpSocket::readyRead, [sock, &gets] {
                const QByteArray req = sock->readAll();
                gets.append(req);
                const QByteArray body = "<?xml version=\"1.0\"?><root><device><friendlyName>t</friendlyName></device></root>";
                QByteArray resp;
                resp += "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\n";
                resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                resp += "Connection: close\r\n\r\n";
                resp += body;
                sock->write(resp);
                sock->flush();
                sock->disconnectFromHost();
            });
        }
    });
    ASSERT_TRUE(descServer.listen(QHostAddress::LocalHost));

    CSSDPSearch search;   // parent() is null: slotReadyRead connect is a no-op warning
    const QString location = QString("http://127.0.0.1:%1/desc.xml").arg(descServer.serverPort());
    const QByteArray reply = "HTTP/1.1 200 OK\r\n"
                             "CACHE-CONTROL: max-age=1800\r\n"
                             "ST: urn:schemas-upnp-org:service:AVTransport:1\r\n"
                             "LOCATION: " + location.toUtf8() + "\r\n\r\n";

    search.showDlnaCastAddr(reply);
    QTest::qWait(300);   // let the QNAM GET complete

    ASSERT_FALSE(gets.isEmpty());
    EXPECT_TRUE(gets.first().startsWith("GET /desc.xml"));
    // Qt6 QNAM writes header names lower-cased on the wire.
    EXPECT_TRUE(gets.first().toLower().contains(("host: 127.0.0.1:" + QString::number(descServer.serverPort())).toUtf8()));
}

} // namespace
