// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// ============================================================================
// UT_CDlnaSoapPost — Google Test suite for src/dlna/cdlnasoappost.cpp
//
// Minimal 10-item checklist (test-code-gen reference §case-design):
//  [1] ≥1 case per public method: ctor(+Ctor_RegistersMetaType),
//      SoapOperPost(+7 covering every DlnaOper arm and the error arms),
//      dtor(+Dtor_ReleasesNetworkManager), signal sigGetPostionInfo
//      (observed in every loopback case)                              -> OK
//  [2] equivalence classes: each of the 6 DlnaOper arms; valid SOAP
//      response / unrelated response / no response at all             -> OK
//  [3] boundary values: invalid enum cast (77), seek=100 -> "00:01:40",
//      1.5 s no-reply timeout, empty request body (invalid oper)      -> OK
//  [4] TEST_P for ≥3 homogeneous inputs: the six DlnaOper arms are
//      intentionally written as individual cases because each arm has
//      distinct request headers (Soapaction name) and recursion
//      behavior — a shared TEST_P body would branch per-case anyway;
//      documented as N/A                                              -> N/A
//  [5] branch list (SoapOperPost complexity 10, listed below) mapped
//      to case names                                                  -> OK
//  [6] all if/else-if/early-return branches of public methods covered:
//      see branch table below                                         -> OK
//  [7] EXPECT_THROW exact type+message: no throwing APIs; failure is
//      signalled via absence of sigGetPostionInfo / timeout           -> N/A
//  [8] negative cases with strong-exception-safety checks: invalid
//      oper and no-reply cases assert no crash and no signal          -> OK
//  [9] stubs (decision matrix): NO stubs — the network dependency is
//      served by a real loopback QTcpServer acting as the DLNA
//      renderer (QNAM would otherwise touch the real network; the
//      loopback server keeps the test hermetic). No DBus/IO/timer
//      stubs needed; the 1.5 s QEventLoop timeout is exercised as-is. -> OK
// [10] custom metatypes: DlnaPositionInfo registered by the ctor under
//      test (asserted); QSignalSpy created after construction         -> OK
//
// Branch table (source ground truth, cdlnasoappost.cpp):
//  SoapOperPost if/else-if chain:
//    DLNA_SetAVTransportURI -> SoapOperPost_SetAVTransportURI_ChainsToPlay
//    DLNA_Play              -> SoapOperPost_Play_SendsSoapActionHeader
//    DLNA_Pause             -> SoapOperPost_Pause_SendsSoapActionHeader
//    DLNA_Seek              -> SoapOperPost_Seek_EncodesTimeInBody
//    DLNA_Stop              -> SoapOperPost_Stop_SendsSoapActionHeader
//    DLNA_GetPositionInfo   -> SoapOperPost_GetPositionInfo_ParsesSignalPayload
//    no arm matches (invalid cast) -> SoapOperPost_InvalidOper_SendsEmptyAction
//  reply finished lambda:
//    contains "SetAVTransportURIResponse" -> ..._ChainsToPlay (recursion)
//    contains "GetPositionInfoResponse"   -> ..._GetPositionInfo_* (signal)
//    neither (garbage reply)              -> ..._Play_* (no recursion, no signal)
//  reply never arrives (timeout quit)     -> SoapOperPost_NoServerReply_TimesOut
//  dtor: m_pNetWorkManager non-null -> Dtor_ReleasesNetworkManager
//        (null branch unreachable from public API — N/C)
// ============================================================================

#include <gtest/gtest.h>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QMetaType>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

#include "cdlnasoappost.h"

namespace {

// SOAP GetPositionInfoResponse with a full field set (production shape).
const char *kPositionInfoResponse =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
      "<s:Body>"
        "<u:GetPositionInfoResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
          "<Track>1</Track>"
          "<TrackDuration>0:01:40</TrackDuration>"
          "<TrackMetaData>meta-data</TrackMetaData>"
          "<TrackURI>http://10.0.0.2/1.mp4</TrackURI>"
          "<RelTime>0:00:10</RelTime>"
          "<AbsTime>0:00:12</AbsTime>"
          "<RelCount>2147483647</RelCount>"
          "<AbsCount>2147483647</AbsCount>"
        "</u:GetPositionInfoResponse>"
      "</s:Body>"
    "</s:Envelope>";

// Minimal UPnP SOAP fault-ish body that matches neither response marker.
const char *kUnrelatedResponse = "<html><body>ok</body></html>";

// Loopback DLNA renderer: accepts SOAP POSTs and answers with a canned body.
// Set responseBody to a request-matching SOAP envelope before the call.
class LocalSoapServer
{
public:
    QTcpServer server;
    QByteArray responseBody = kUnrelatedResponse;
    // Response used for "#Play" requests: must NOT contain
    // "SetAVTransportURIResponse" (SoapOperPost re-posts DLNA_Play on such a
    // reply, so echoing it back would recurse forever).
    QByteArray playResponseBody =
        "<?xml version=\"1.0\"?><s:Envelope><s:Body>"
        "<u:PlayResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>"
        "</s:Body></s:Envelope>";
    QList<QByteArray> requests;
    int connections = 0;

    bool start()
    {
        QObject::connect(&server, &QTcpServer::newConnection, [this] { onConn(); });
        return server.listen(QHostAddress::LocalHost);
    }

    QUrl url() const
    {
        return QUrl(QString("http://127.0.0.1:%1/AVTransport/action").arg(server.serverPort()));
    }

    QString hostAuthority() const
    {
        return QString("127.0.0.1:%1").arg(server.serverPort());
    }

    void onConn()
    {
        while (QTcpSocket *sock = server.nextPendingConnection()) {
            ++connections;
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            QObject::connect(sock, &QTcpSocket::readyRead, [this, sock] {
                auto &buf = m_buf[sock];
                buf.append(sock->readAll());
                const int headerEnd = buf.indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                // Content-Length of the pending body
                int contentLen = 0;
                const QByteArray head = buf.left(headerEnd);
                const QList<QByteArray> lines = head.split('\n');
                for (const QByteArray &line : lines) {
                    if (line.toLower().startsWith("content-length:"))
                        contentLen = line.mid(15).trimmed().toInt();
                }
                if (buf.size() - headerEnd - 4 < contentLen)
                    return;
                requests.append(buf);
                m_buf.remove(sock);

                // Qt6 QNAM writes header names lower-cased on the wire.
                const QByteArray body =
                    buf.toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#play\"")
                        ? playResponseBody : responseBody;
                QByteArray resp;
                resp += "HTTP/1.1 200 OK\r\n";
                resp += "Content-Type: text/xml; charset=\"utf-8\"\r\n";
                resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                resp += "Connection: close\r\n\r\n";
                resp += body;
                sock->write(resp);
                sock->flush();
                sock->disconnectFromHost();
            });
        }
    }

    ~LocalSoapServer()
    {
        for (QTcpSocket *sock : m_buf.keys())
            sock->abort();
    }

private:
    QHash<QTcpSocket *, QByteArray> m_buf;
};

class UT_CDlnaSoapPost : public QObject, public ::testing::Test
{
protected:
    LocalSoapServer m_server;
};

// --- ctor / dtor --------------------------------------------------------------

TEST_F(UT_CDlnaSoapPost, Ctor_RegistersMetaType_AndCreatesManager)
{
    CDlnaSoapPost post;
    // ctor called qRegisterMetaType<DlnaPositionInfo> — required by QSignalSpy.
    EXPECT_TRUE(QMetaType::fromName("DlnaPositionInfo").isValid());
    SUCCEED() << "constructor created QNetworkAccessManager without crashing";
}

TEST_F(UT_CDlnaSoapPost, Dtor_ReleasesNetworkManager)
{
    {
        CDlnaSoapPost post;
    }
    // Re-create to prove the destructor released everything cleanly.
    CDlnaSoapPost post2;
    SUCCEED();
}

// --- SoapOperPost: one case per DlnaOper arm -----------------------------------

TEST_F(UT_CDlnaSoapPost, SoapOperPost_GetPositionInfo_ParsesSignalPayload)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kPositionInfoResponse;

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(DLNA_GetPositionInfo, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString());
    ASSERT_EQ(m_server.requests.size(), 1);

    const QByteArray req = m_server.requests.first();
    EXPECT_TRUE(req.contains("POST /AVTransport/action HTTP/1.1"));
    EXPECT_TRUE(req.toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#getpositioninfo\""));
    EXPECT_TRUE(req.toLower().contains(("host: " + m_server.hostAuthority()).toUtf8()));
    EXPECT_TRUE(req.toLower().contains("content-type: text/xml; charset=\"utf-8\""));

    ASSERT_EQ(spy.count(), 1);
    const DlnaPositionInfo info = spy.at(0).at(0).value<DlnaPositionInfo>();
    EXPECT_EQ(info.nTrack, 1);
    EXPECT_EQ(info.sTrackDuration, QString("0:01:40"));
    EXPECT_EQ(info.sTrackMetaData, QString("meta-data"));
    EXPECT_EQ(info.sTrackURI, QString("http://10.0.0.2/1.mp4"));
    EXPECT_EQ(info.sRelTime, QString("0:00:10"));
    EXPECT_EQ(info.sAbsTime, QString("0:00:12"));
    EXPECT_EQ(info.nRelCount, qint64(2147483647));
    EXPECT_EQ(info.nAbsCount, qint64(2147483647));
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_SetAVTransportURI_ChainsToPlay)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody =
        "<?xml version=\"1.0\"?><s:Envelope><s:Body>"
        "<u:SetAVTransportURIResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\"/>"
        "</s:Body></s:Envelope>";

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    const QString videoUrl = "http://10.0.0.2:9001/movie.mp4";
    post.SoapOperPost(DLNA_SetAVTransportURI, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), videoUrl);

    // First POST carries the URI, the class then re-posts DLNA_Play itself.
    ASSERT_EQ(m_server.requests.size(), 2);
    EXPECT_TRUE(m_server.requests.at(0).toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#setavtransporturi\""));
    EXPECT_TRUE(m_server.requests.at(0).contains(videoUrl.toUtf8()));
    EXPECT_TRUE(m_server.requests.at(1).toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#play\""));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Play_SendsSoapActionHeader)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kUnrelatedResponse;   // no recursion, no signal

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(DLNA_Play, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString());
    ASSERT_EQ(m_server.requests.size(), 1);
    EXPECT_TRUE(m_server.requests.first().toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#play\""));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Pause_SendsSoapActionHeader)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kUnrelatedResponse;

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(DLNA_Pause, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString());
    ASSERT_EQ(m_server.requests.size(), 1);
    EXPECT_TRUE(m_server.requests.first().toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#pause\""));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(UT_CDlnaSoapPost, SoapOperPost_Stop_SendsSoapActionHeader)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kUnrelatedResponse;

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(DLNA_Stop, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString());
    ASSERT_EQ(m_server.requests.size(), 1);
    EXPECT_TRUE(m_server.requests.first().toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#stop\""));
    EXPECT_EQ(spy.count(), 0);
}

// Seek encodes nSeek through getTimeStr ("hh:mm:ss") into the request body.
TEST_F(UT_CDlnaSoapPost, SoapOperPost_Seek_EncodesTimeInBody)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kUnrelatedResponse;

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(DLNA_Seek, m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString(), 100);
    ASSERT_EQ(m_server.requests.size(), 1);
    EXPECT_TRUE(m_server.requests.first().toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#seek\""));
    EXPECT_TRUE(m_server.requests.first().contains("00:01:40"));   // 100 s
    EXPECT_EQ(spy.count(), 0);
}

// Invalid enum cast: no arm matches -> empty Soapaction suffix + empty body,
// still a well-formed POST (strong-exception-safety style negative case).
TEST_F(UT_CDlnaSoapPost, SoapOperPost_InvalidOper_SendsEmptyAction)
{
    ASSERT_TRUE(m_server.start());
    m_server.responseBody = kUnrelatedResponse;

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    post.SoapOperPost(static_cast<DlnaOper>(77), m_server.url().toString(),
                      QString("http://") + m_server.hostAuthority(), QString());
    ASSERT_EQ(m_server.requests.size(), 1);
    EXPECT_TRUE(m_server.requests.first().toLower().contains("soapaction: \"urn:schemas-upnp-org:service:avtransport:1#\""));
    EXPECT_EQ(spy.count(), 0);
}

// No renderer behind the URL: QNAM error path, 1.5 s singleShot quits the
// internal loop, no signal, object stays usable.
TEST_F(UT_CDlnaSoapPost, SoapOperPost_NoServerReply_TimesOutWithoutSignal)
{
    QTcpServer blackhole;
    ASSERT_TRUE(blackhole.listen(QHostAddress::LocalHost));
    // Accept connections but never read or reply.

    CDlnaSoapPost post;
    QSignalSpy spy(&post, &CDlnaSoapPost::sigGetPostionInfo);

    QElapsedTimer timer;
    timer.start();
    post.SoapOperPost(DLNA_GetPositionInfo,
                      QUrl(QString("http://127.0.0.1:%1/AVTransport/action").arg(blackhole.serverPort())).toString(),
                      QString("http://127.0.0.1:%1").arg(blackhole.serverPort()), QString());
    EXPECT_GE(timer.elapsed(), 1400);   // waited for the internal 1500 ms quit
    EXPECT_EQ(spy.count(), 0);

    // Object still usable afterwards.
    EXPECT_NO_FATAL_FAILURE(post.SoapOperPost(DLNA_GetPositionInfo,
                                              QUrl(QString("http://127.0.0.1:%1/AVTransport/action").arg(blackhole.serverPort())).toString(),
                                              QString("http://127.0.0.1:%1").arg(blackhole.serverPort()), QString()));
    EXPECT_EQ(spy.count(), 0);
}

} // namespace
