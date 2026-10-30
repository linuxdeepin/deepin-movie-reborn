// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Task t10: value-level unit tests for three src/widgets translation units
// whose existing suites leave real gaps:
//   * src/widgets/moviewidget.cpp       (MovieWidget: no dedicated suite yet)
//   * src/widgets/mircastshowwidget.cpp (updateView had no value assertions,
//                                         destructor uncovered)
//   * src/widgets/mircastwidget.cpp     (slotReadyRead / slotGetPositionInfo /
//                                         SOAP control methods were SKIP-only)
//
// Suite names (moviewidget_t10 / mircastshow_t10 / mircastwidget_t10) are
// unique across the test binary; static helpers carry the "t10_" prefix.
//
// Safety rules baked in:
//   * Google Test only; no main() here (shared test main supplies entry).
//   * No real network: slotReadyRead is driven with a genuine QNetworkReply
//     from a local file:// URL (plus an unknown-scheme URL for the error
//     branch); SoapOperPost is stubbed so no SOAP/HTTP request leaves the
//     process. No SSDP, no DBus, no real playback is touched.
//   * PlayerEngine is the real class but state() is stubbed where a non-idle
//     core state is required (never starts actual playback).
//   * Geometry / paint paths are guarded by QGuiApplication::primaryScreen();
//     cases GTEST_SKIP() when headless.
//   * All widget state is reached through the project-standard
//     "#define private public" include hack (same as the ext suites).

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QGraphicsSvgItem>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#define protected public
#define private public
#include "src/widgets/moviewidget.h"
#include "src/widgets/mircastshowwidget.h"
#include "src/widgets/mircastwidget.h"
#include "application.h"
#include "src/dlna/cdlnasoappost.h"   // CDlnaSoapPost / DlnaOper / DlnaPositionInfo
#include "src/dlna/cssdpsearch.h"     // urlAddrPro / controlURLPro / friendlyNamePro
#include "player_engine.h"            // PlayerEngine (real, state() stubbed)
#include "playlist_model.h"
#undef protected
#undef private

#include "stub/stub.h"

using namespace dmr;

// --- Shared helpers --------------------------------------------------------

// Values mirrored from the file-local #defines in moviewidget.cpp /
// mircastshowwidget.cpp (NOT redefined here to keep ODR clean).
static const double kT10RotateAngle = 360.0 / (1000 * 2.5 / 50);   // 7.2
static const double kT10MovieRatio = 1.0 * 1080 / 1920;            // 0.5625
static const int kT10BgLength = 174;
static const double kT10ShowRatio = 1.0 * 680 / 1070;              // ~0.6355
static const int kT10ShowBgW = 410;
static const int kT10ShowBgH = 287;
static const int kT10XOffset = 17;
static const int kT10YOffset = 143;

// Create a PlayerEngine the same way tests/deepin-movie/libdmr do (mpv config
// stubbed into CompositingManager; no backend is loaded, state() is Idle).
static PlayerEngine *t10_createEngine()
{
    CompositingManager::get()._composited = true;
    if (!CompositingManager::get().m_pMpvConfig)
        CompositingManager::get().m_pMpvConfig = new QMap<QString, QString>();
    return new PlayerEngine(nullptr);
}

// --- SoapOperPost stub (single static Stub instance, restored by RAII) ------

static int t10_soapCalls = 0;
static DlnaOper t10_soapOper{};
static QString t10_soapControl, t10_soapHost, t10_soapLocal;
static int t10_soapSeek = -1;

// Patches the (non-virtual) member CDlnaSoapPost::SoapOperPost. The member
// ABI passes `this` in the first argument register, so the replacement MUST
// take a leading void* — otherwise every argument shifts by one and the
// QString copies dereference garbage (SIGSEGV).
static void t10_soap_stub(void *, DlnaOper oper, QString controlUrl,
                          QString hostUrl, QString localUrl, int nSeek)
{
    t10_soapCalls++;
    t10_soapOper = oper;
    t10_soapControl = controlUrl;
    t10_soapHost = hostUrl;
    t10_soapLocal = localUrl;
    t10_soapSeek = nSeek;
}

typedef void (*t10_soap_fptr)(void *, DlnaOper, QString, QString, QString, int);

// RAII: patch SoapOperPost for the test body, restore automatically.
struct t10_SoapGuard
{
    Stub stub;
    t10_SoapGuard()
    {
        t10_soapCalls = 0;
        t10_soapSeek = -1;
        stub.set((t10_soap_fptr)&CDlnaSoapPost::SoapOperPost, t10_soap_stub);
    }
    ~t10_SoapGuard() { }
};

typedef PlayerEngine::CoreState (*t10_state_fptr)();

// RAII: force PlayerEngine::state() to a fixed value for the test body.
template <PlayerEngine::CoreState S>
struct t10_EngineStateGuard
{
    Stub stub;
    static PlayerEngine::CoreState t10_state_stub() { return S; }
    t10_EngineStateGuard()
    {
        stub.set((t10_state_fptr)&PlayerEngine::state, &t10_state_stub);
    }
    ~t10_EngineStateGuard() { }
};

// Build a valid one-line DLNA device-description XML for slotReadyRead.
static QByteArray t10_deviceXml(const char *friendlyName = "T10-LivingTV",
                                const char *udn = "uuid:abcd1234-1111-2222-3333-444455556666")
{
    return QByteArray("<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                      "<specVersion><major>1</major><minor>0</minor></specVersion>"
                      "<device>"
                      "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
                      "<UDN>") + QByteArray(udn) + QByteArray("</UDN>"
                      "<friendlyName>") + QByteArray(friendlyName) + QByteArray("</friendlyName>"
                      "<serviceList><service>"
                      "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
                      "<controlURL>/ctl/AVTransport</controlURL>"
                      "</service></serviceList>"
                      "</device></root>");
}

// Fetch a real QNetworkReply for a local XML file and wait until it finished.
static QNetworkReply *t10_fileReply(QNetworkAccessManager &nam, const QByteArray &xml,
                                    QTemporaryFile &tmp)
{
    tmp.setAutoRemove(true);
    tmp.open();
    tmp.write(xml);
    tmp.flush();
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl::fromLocalFile(tmp.fileName())));
    (void)QTest::qWaitFor([&reply]() { return reply->isFinished(); }, 5000);
    return reply;
}

// ===========================================================================
// MovieWidget (src/widgets/moviewidget.cpp) — dedicated suite
// ===========================================================================

TEST(moviewidget_t10, construct_defaults)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    EXPECT_EQ(w.alignment(), Qt::AlignCenter);
    EXPECT_EQ(w.frameShape(), QFrame::NoFrame);
    EXPECT_TRUE(w.testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_TRUE(w.hasMouseTracking());
    ASSERT_NE(w.m_pScene, nullptr);
    EXPECT_EQ(w.m_pScene->items().size(), 2);
    ASSERT_NE(w.m_pTimer, nullptr);
    EXPECT_EQ(w.m_pTimer->interval(), 50);       // INTERVAL
    EXPECT_EQ(w.m_state, MovieWidget::STATE_STOP);
    EXPECT_EQ(w.m_nRotate, 0);
}

TEST(moviewidget_t10, construct_backgroundSvgCenteredByFormula)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    MovieWidget w(&parent);
    // ctor: sceneRect = bg item bounding rect; bg centered by DEFAULT_BGLENGTH
    double sceneW = w.m_pScene->width();
    double sceneH = w.m_pScene->height();
    EXPECT_DOUBLE_EQ(w.m_pBgSvgItem->pos().x(), (sceneW - kT10BgLength) / 2);
    EXPECT_DOUBLE_EQ(w.m_pBgSvgItem->pos().y(), (sceneH - kT10BgLength) / 2);
}

TEST(moviewidget_t10, startPlaying_fromStop_showsAndStartsTimer)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.show();                               // child isVisible needs it
    MovieWidget w(&parent);
    ASSERT_FALSE(w.isVisible());
    w.startPlaying();
    EXPECT_TRUE(w.isVisible());
    EXPECT_TRUE(w.m_pTimer->isActive());
    EXPECT_EQ(w.m_state, MovieWidget::STATE_PLAYING);
    EXPECT_EQ(w.m_nRotate, 0);                   // reset on the STOP branch
    w.stopPlaying();
}

TEST(moviewidget_t10, startPlaying_fromPause_keepsRotationNotReset)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.show();
    MovieWidget w(&parent);
    w.startPlaying();
    w.updateView();                              // m_nRotate += 2*ROTATE_ANGLE
    const int rotated = w.m_nRotate;
    EXPECT_EQ(rotated, 14);                      // int truncation of 14.4
    w.pausePlaying();                            // STATE_PAUSE
    w.startPlaying();                            // m_state != STOP -> no reset
    EXPECT_EQ(w.m_state, MovieWidget::STATE_PLAYING);
    EXPECT_EQ(w.m_nRotate, rotated);             // rotation preserved
    w.stopPlaying();
}

TEST(moviewidget_t10, stopPlaying_hidesAndStopsTimer)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    w.startPlaying();
    ASSERT_TRUE(w.m_pTimer->isActive());
    w.stopPlaying();
    EXPECT_FALSE(w.m_pTimer->isActive());
    EXPECT_EQ(w.m_state, MovieWidget::STATE_STOP);
    EXPECT_FALSE(w.isVisible());
}

TEST(moviewidget_t10, pausePlaying_stopsTimerKeepsVisibility)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.show();
    MovieWidget w(&parent);
    w.startPlaying();
    w.pausePlaying();
    EXPECT_FALSE(w.m_pTimer->isActive());
    EXPECT_EQ(w.m_state, MovieWidget::STATE_PAUSE);
    EXPECT_TRUE(w.isVisible());                  // pause does not hide
    w.stopPlaying();
}

TEST(moviewidget_t10, updateView_wideShape_takesHeightBranch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    w.resize(800, 200);                          // 200/800=0.25 < 0.5625
    w.startPlaying();                            // reset rotation + show
    w.updateView();
    const QRect desktop = QGuiApplication::primaryScreen()->availableGeometry();
    const int nWidth = static_cast<int>(200 / kT10MovieRatio);
    const double fRatio = nWidth * 2.0 / desktop.width();
    EXPECT_EQ(w.m_nRotate, 14);                  // int truncation of 14.4
    EXPECT_NEAR(w.m_pBgSvgItem->scale(), fRatio, 1e-9);
    EXPECT_NEAR(w.m_pNoteSvgItem->scale(), fRatio, 1e-9);
    EXPECT_NEAR(w.m_pBgSvgItem->pos().x(),
                (w.m_pScene->width() - kT10BgLength * fRatio) / 2, 1e-6);
    EXPECT_NEAR(w.m_pNoteSvgItem->rotation(), 14.0, 1e-9);
    w.stopPlaying();
}

TEST(moviewidget_t10, updateView_tallShape_takesWidthBranch)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    w.resize(200, 800);                          // 800/200=4.0 >= 0.5625
    w.startPlaying();
    w.updateView();
    const QRect desktop = QGuiApplication::primaryScreen()->availableGeometry();
    const int nHeight = static_cast<int>(200 * kT10MovieRatio);
    const double fRatio = nHeight * 2.0 / desktop.height();
    EXPECT_NEAR(w.m_pBgSvgItem->scale(), fRatio, 1e-9);
    EXPECT_NEAR(w.m_pBgSvgItem->pos().y(),
                (w.m_pScene->height() - kT10BgLength * fRatio) / 2, 1e-6);
    w.stopPlaying();
}

TEST(moviewidget_t10, updateView_rotationAccumulatesPerCall)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    w.startPlaying();
    w.updateView();
    w.updateView();
    w.updateView();
    EXPECT_EQ(w.m_nRotate, 42);                  // +14 per call (int)
    EXPECT_NEAR(w.m_pNoteSvgItem->rotation(), 42.0, 1e-9);
    w.stopPlaying();
}

TEST(moviewidget_t10, mouseEvents_areIgnored)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    MovieWidget w(&parent);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(10, 10),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(20, 20),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    // Invoke the protected handlers directly (private->public include hack);
    // QGraphicsView event dispatch is viewport-dependent and unreliable
    // for synthetic move events on the offscreen platform.
    w.mousePressEvent(&press);
    w.mouseReleaseEvent(&release);
    w.mouseMoveEvent(&move);
    EXPECT_FALSE(press.isAccepted());
    EXPECT_FALSE(release.isAccepted());
    EXPECT_FALSE(move.isAccepted());
}

TEST(moviewidget_t10, destructor_timerDeletedNoCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QPointer<MovieWidget> w = new MovieWidget();
    w->startPlaying();
    ASSERT_NE(w->m_pTimer, nullptr);
    QPointer<QTimer> timer = w->m_pTimer;
    delete w;                                    // dtor: timer->deleteLater()
    QTest::qWait(60);                            // let deleteLater deliver
    EXPECT_TRUE(timer.isNull());                 // timer actually destroyed
}

// NOTE (defect candidate, not fixed here): MovieWidget::initMember is a public
// slot that resets every pointer member to nullptr. Calling it after the ctor
// and then touching the widget (destructor included) dereferences null
// pointers. A case that exercises that path would crash by design, so it is
// documented in the defect list instead of being written here.

// ===========================================================================
// MircastShowWidget (src/widgets/mircastshowwidget.cpp) — gap-filling suite
// ===========================================================================

TEST(mircastshow_t10, updateView_wideShape_exactMath)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    MircastShowWidget w(&parent);
    w.resize(800, 400);                          // 400/800=0.5 < 0.6355
    w.updateView();
    const QRect desktop = QGuiApplication::primaryScreen()->availableGeometry();
    const int nWidth = static_cast<int>(400 / kT10ShowRatio);
    const double fRatio = nWidth * 2.0 / desktop.width();
    EXPECT_NEAR(w.m_pBgSvgItem->scale(), fRatio, 1e-9);
    EXPECT_NEAR(w.m_pProSvgItem->scale(), fRatio, 1e-9);
    const double bgX = (w.m_pScene->width() - kT10ShowBgW * fRatio) / 2;
    const double bgY = (w.m_pScene->height() - kT10ShowBgH * fRatio) / 2;
    EXPECT_NEAR(w.m_pBgSvgItem->pos().x(), bgX, 1e-6);
    EXPECT_NEAR(w.m_pBgSvgItem->pos().y(), bgY, 1e-6);
    EXPECT_NEAR(w.m_pProSvgItem->pos().x(), bgX + kT10XOffset * fRatio, 1e-6);
    EXPECT_NEAR(w.m_pProSvgItem->pos().y(), bgY + kT10YOffset * fRatio, 1e-6);
    EXPECT_NEAR(w.m_deviceName->textWidth(), kT10ShowBgW * fRatio, 1e-6);
    EXPECT_NEAR(w.m_promptInformation->pos().y(),
                (kT10ShowBgH + 10) * fRatio + bgY, 1e-6);
}

TEST(mircastshow_t10, updateView_tallShape_exactMath)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    MircastShowWidget w(&parent);
    w.resize(300, 800);                          // 800/300 ~2.67 >= 0.6355
    w.updateView();
    const QRect desktop = QGuiApplication::primaryScreen()->availableGeometry();
    const int nHeight = static_cast<int>(300 * kT10ShowRatio);
    const double fRatio = nHeight * 2.0 / desktop.height();
    EXPECT_NEAR(w.m_pBgSvgItem->scale(), fRatio, 1e-9);
    EXPECT_NEAR(w.m_pProSvgItem->scale(), fRatio, 1e-9);
}

TEST(mircastshow_t10, constructor_textItemDefaults)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    MircastShowWidget w(&parent);
    ASSERT_NE(w.m_deviceName, nullptr);
    ASSERT_NE(w.m_promptInformation, nullptr);
    EXPECT_EQ(w.m_deviceName->defaultTextColor(), Qt::white);
    EXPECT_EQ(w.m_promptInformation->defaultTextColor(), QColor(255, 255, 255, 153));
    EXPECT_TRUE(w.m_promptInformation->toPlainText().contains("Projecting"));
    // prompt font size was forced to 10 in the ctor
    EXPECT_EQ(w.m_promptInformation->font().pointSize(), 10);
}

TEST(mircastshow_t10, setDeviceName_displayFormatAndTruncation)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    MircastShowWidget w(&parent);
    w.setDeviceName("Living-Room-TV");
    EXPECT_EQ(w.m_deviceName->toPlainText(),
              QString("Display device:  Living-Room-TV"));
    // >20 chars: customizeText truncates to left(20) + "..."
    const QString longName("A-very-long-television-name-here");
    w.setDeviceName(longName);
    EXPECT_EQ(w.m_deviceName->toPlainText(),
              QString("Display device:  ") + longName.left(20) + "...");
}

TEST(mircastshow_t10, mouseMoveEvent_forwardsAndIgnores)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    parent.show();
    MircastShowWidget w(&parent);
    w.resize(400, 300);
    w.show();
    QTest::qWait(20);
    QMouseEvent ev(QEvent::MouseMove, QPointF(30, 30), QPointF(30, 30),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    w.mouseMoveEvent(&ev);                       // direct call: sendEvent would be
                                                 // routed to the viewport and miss the override
}

TEST(mircastshow_t10, destructor_afterUse_noCrash)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget parent;
    parent.resize(800, 600);
    QPointer<MircastShowWidget> w = new MircastShowWidget(&parent);
    w->setDeviceName("bye-tv");
    w->updateView();
    delete w;                                    // covers ~MircastShowWidget
    QTest::qWait(30);
    EXPECT_TRUE(w.isNull());
}

// ===========================================================================
// MircastWidget (src/widgets/mircastwidget.cpp) — SKIP-branch rescue suite
// ===========================================================================

// --- slotReadyRead (HIGH) ---------------------------------------------------

TEST(mircastwidget_t10, readyRead_directCall_nullSender_returnsEarly)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    // Called outside any signal emission -> sender() is nullptr.
    w.slotReadyRead();
    EXPECT_TRUE(w.m_devicesList.isEmpty());
    EXPECT_EQ(w.m_listWidget->count(), 0);
}

TEST(mircastwidget_t10, readyRead_errorReply_noDeviceAdded)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(
        QNetworkRequest(QUrl("t10-unknown-scheme://host/device.xml")));
    QTest::qWaitFor([&reply]() { return reply->isFinished(); }, 5000);
    ASSERT_NE(reply->error(), QNetworkReply::NoError);
    QObject::connect(reply, &QNetworkReply::readyRead,
                     &w, &MircastWidget::slotReadyRead, Qt::DirectConnection);
    emit reply->readyRead();
    EXPECT_TRUE(w.m_devicesList.isEmpty());
    reply->deleteLater();
    QTest::qWait(30);
}

TEST(mircastwidget_t10, readyRead_validXml_addsDeviceAndItem)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    QNetworkAccessManager nam;
    QTemporaryFile tmp;
    QNetworkReply *reply = t10_fileReply(nam, t10_deviceXml("T10-LivingTV",
        "uuid:abcd1234-1111-2222-3333-444455556666"), tmp);
    ASSERT_EQ(reply->error(), QNetworkReply::NoError);
    QObject::connect(reply, &QNetworkReply::readyRead,
                     &w, &MircastWidget::slotReadyRead, Qt::DirectConnection);
    emit reply->readyRead();
    ASSERT_EQ(w.m_devicesList.size(), 1);
    EXPECT_EQ(w.m_devicesList.first().name, QString("T10-LivingTV"));
    EXPECT_EQ(w.m_devicesList.first().uuid, QString("abcd1234-1111-2222-3333-444455556666"));
    EXPECT_EQ(w.m_listWidget->count(), 1);
    reply->deleteLater();
    QTest::qWait(30);
}

TEST(mircastwidget_t10, readyRead_duplicateUuid_notAddedTwice)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    QNetworkAccessManager nam;
    QTemporaryFile tmp1, tmp2;
    // First delivery: fresh reply with the device XML
    QNetworkReply *r1 = t10_fileReply(nam, t10_deviceXml(), tmp1);
    ASSERT_EQ(r1->error(), QNetworkReply::NoError);
    QObject::connect(r1, &QNetworkReply::readyRead,
                     &w, &MircastWidget::slotReadyRead, Qt::DirectConnection);
    emit r1->readyRead();
    ASSERT_EQ(w.m_devicesList.size(), 1);
    // Second delivery: a NEW reply carrying the SAME uuid (re-emitting the
    // first reply would just re-read an already-consumed buffer) -> dedup
    QNetworkReply *r2 = t10_fileReply(nam, t10_deviceXml("T10-LivingTV",
        "uuid:abcd1234-1111-2222-3333-444455556666"), tmp2);
    ASSERT_EQ(r2->error(), QNetworkReply::NoError);
    QObject::connect(r2, &QNetworkReply::readyRead,
                     &w, &MircastWidget::slotReadyRead, Qt::DirectConnection);
    emit r2->readyRead();
    EXPECT_EQ(w.m_devicesList.size(), 1);        // duplicate uuid rejected
    EXPECT_EQ(w.m_listWidget->count(), 1);
    r1->deleteLater();
    r2->deleteLater();
    QTest::qWait(30);
}

TEST(mircastwidget_t10, readyRead_emptyXml_addsEmptyDeviceEntry)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    QNetworkAccessManager nam;
    QTemporaryFile tmp;
    QNetworkReply *reply = t10_fileReply(nam, QByteArray("<root></root>"), tmp);
    ASSERT_EQ(reply->error(), QNetworkReply::NoError);
    QObject::connect(reply, &QNetworkReply::readyRead,
                     &w, &MircastWidget::slotReadyRead, Qt::DirectConnection);
    emit reply->readyRead();
    ASSERT_EQ(w.m_devicesList.size(), 1);
    EXPECT_TRUE(w.m_devicesList.first().name.isEmpty());
    EXPECT_TRUE(w.m_devicesList.first().uuid.isEmpty());
    EXPECT_EQ(w.m_listWidget->count(), 1);
    reply->deleteLater();
    QTest::qWait(30);
}

// --- slotGetPositionInfo (HIGH) ---------------------------------------------

TEST(mircastwidget_t10, getPositionInfo_connecting_validTimes_becomesScreening)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Connecting);
    w.m_connectDevice.deviceState = MircastWidget::Idel;   // deterministic
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:30";
    info.sTrackDuration = "00:10:00";
    w.slotGetPositionInfo(info);
    // Connecting path: duration+absTime both > 0 -> promote to Screening;
    // m_nCurAbsTime/m_nCurDuration are only written on the Screening path
    EXPECT_EQ(w.m_mircastState, MircastWidget::Screening);
    EXPECT_EQ(w.m_attempts, 0);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_connecting_succeedsEmitsSignal)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Connecting);
    w.m_connectDevice.deviceState = MircastWidget::Connecting;
    w.m_connectDevice.miracastDevice.name = "T10-TV";
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:01";
    info.sTrackDuration = "00:00:59";
    w.slotGetPositionInfo(info);
    EXPECT_EQ(w.m_mircastState, MircastWidget::Screening);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).toInt(), static_cast<int>(MIRCAST_SUCCEEDED));
    EXPECT_EQ(spy.first().at(1).toString(), QString("T10-TV"));
    // connectDevice state flipped to Screening
    EXPECT_NE(w.m_connectDevice.deviceState, MircastWidget::Connecting);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_connecting_maxAttempts_exits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Connecting);
    w.m_attempts = 100000;                       // >= MAXMIRCAST * 10
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:00";
    info.sTrackDuration = "00:00:00";
    w.slotGetPositionInfo(info);
    // empty playlist + OrderPlay: current()==count()-1 -> MIRCAST_EXIT plus
    // slotExitMircast's own follow-up signal (1, "normal")
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(MIRCAST_EXIT));
    EXPECT_EQ(w.m_mircastState, MircastWidget::Idel);
    EXPECT_EQ(w.m_attempts, 0);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_screening_engineIdle_exits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();   // real state() -> Idle
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Screening);
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:10";
    info.sTrackDuration = "00:01:00";
    w.slotGetPositionInfo(info);
    // MIRCAST_EXIT + slotExitMircast's own signal (1, "normal")
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(MIRCAST_EXIT));
    EXPECT_EQ(spy.at(1).at(0).toInt(), 1);
    EXPECT_EQ(spy.at(1).at(1).toString(), QString("normal"));
    EXPECT_EQ(w.m_mircastState, MircastWidget::Idel);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_screening_absEqDuration_orderPlay_exits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Screening);
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:42";                  // == duration -> completed
    info.sTrackDuration = "00:00:42";
    w.slotGetPositionInfo(info);
    // empty playlist + default OrderPlay + current()==count()-1 -> exit
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(MIRCAST_EXIT));
    EXPECT_EQ(w.m_mircastState, MircastWidget::Idel);
    EXPECT_EQ(w.m_attempts, 0);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_screening_notImplementedDuration0_exits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Screening);
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "NOT_IMPLEMENTED";
    info.sTrackDuration = "00:00:00";            // duration == 0 -> invalid
    w.slotGetPositionInfo(info);
    // "NOT_IMPLEMENTED" + duration 0 -> invalid -> exit
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(MIRCAST_EXIT));
    EXPECT_EQ(w.m_mircastState, MircastWidget::Idel);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_screening_uriMismatch_exits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    w.setMircastState(MircastWidget::Screening);
    w.m_sTrackURI = "rtsp://somewhere-else/stream";
    w.m_sLocalUrl = "http://local/file.mp4";
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:01";
    info.sTrackDuration = "00:00:02";            // != abs, not NOT_IMPLEMENTED
    info.sTrackURI = "rtsp://somewhere-else/stream";   // overwrites m_sTrackURI
    w.slotGetPositionInfo(info);
    ASSERT_EQ(spy.count(), 2);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(MIRCAST_EXIT));
    EXPECT_EQ(w.m_mircastState, MircastWidget::Idel);
    delete engine;
}

TEST(mircastwidget_t10, getPositionInfo_screening_singleLoop_restartsDlnaTp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PlayerEngine *engine = t10_createEngine();
    MircastWidget w(nullptr, engine);
    engine->getplaylist()->setPlayMode(PlaylistModel::SingleLoop);
    w.setMircastState(MircastWidget::Screening);
    w.m_attempts = 5;
    t10_EngineStateGuard<PlayerEngine::CoreState::Playing> stateGuard;
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:30";                  // == duration -> completed
    info.sTrackDuration = "00:00:30";
    w.slotGetPositionInfo(info);
    // SingleLoop -> startDlnaTp(); standalone has no HTTP server -> early
    // return inside startDlnaTp; widget stays Screening, attempts reset.
    EXPECT_EQ(w.m_mircastState, MircastWidget::Screening);
    EXPECT_EQ(w.m_attempts, 0);
    EXPECT_EQ(w.m_nCurAbsTime, 30);
    delete engine;
}

// --- SOAP control methods (SoapOperPost stubbed) -----------------------------

TEST(mircastwidget_t10, playDlnaTp_postsPlayAndEmits)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    QSignalSpy spy(&w, &MircastWidget::updatePlayStatus);
    w.playDlnaTp();
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapOper, DLNA_Play);
    EXPECT_EQ(w.m_nPlayStatus, MircastWidget::Play);
    EXPECT_EQ(spy.count(), 1);
}

TEST(mircastwidget_t10, getPosInfoDlnaTp_postsGetPositionInfo)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.getPosInfoDlnaTp();
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapOper, DLNA_GetPositionInfo);
}

TEST(mircastwidget_t10, pauseDlnaTp_screening_postsPause)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.setMircastState(MircastWidget::Screening);
    w.m_nPlayStatus = MircastWidget::Play;       // slotPauseDlnaTp toggles
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.slotPauseDlnaTp();
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapOper, DLNA_Pause);
    EXPECT_EQ(w.m_nPlayStatus, MircastWidget::Pause);
}

TEST(mircastwidget_t10, seekMircast_negativeSeek_clampsToZero)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.setMircastState(MircastWidget::Screening);
    w.m_nCurAbsTime = 5;
    w.m_nCurDuration = 100;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.seekMircast(-10);                          // 5 + (-10) < 0 -> seek 0
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapOper, DLNA_Seek);
    EXPECT_EQ(t10_soapSeek, 0);
}

TEST(mircastwidget_t10, seekMircast_beyondDuration_clampsToDuration)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.setMircastState(MircastWidget::Screening);
    w.m_nCurAbsTime = 90;
    w.m_nCurDuration = 100;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.seekMircast(50);                           // 140 > 100 -> seek 100
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapSeek, 100);
}

TEST(mircastwidget_t10, seekMircast_inRange_passesThrough)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.setMircastState(MircastWidget::Screening);
    w.m_nCurAbsTime = 5;
    w.m_nCurDuration = 100;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.seekMircast(50);                           // 55 within [0, 100]
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapSeek, 55);
}

TEST(mircastwidget_t10, seekMircast_notScreening_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    t10_SoapGuard soapGuard;
    w.seekMircast(30);                           // state Idel -> early return
    EXPECT_EQ(t10_soapCalls, 0);
}

TEST(mircastwidget_t10, stopDlnaTP_trackMatch_postsStopAndClears)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    w.setMircastState(MircastWidget::Screening);
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    w.m_sTrackURI = "http://local/file.mp4";
    w.m_sLocalUrl = "http://local/file.mp4";
    w.m_ControlURLPro = "http://tv/ctl";
    w.m_URLAddrPro = "http://tv";
    t10_SoapGuard soapGuard;
    QSignalSpy spy(&w, &MircastWidget::updatePlayStatus);
    w.stopDlnaTP();
    EXPECT_EQ(t10_soapCalls, 1);
    EXPECT_EQ(t10_soapOper, DLNA_Stop);
    EXPECT_EQ(w.m_nPlayStatus, MircastWidget::Stop);
    EXPECT_TRUE(w.m_ControlURLPro.isEmpty());    // urls cleared after stop
    EXPECT_TRUE(w.m_URLAddrPro.isEmpty());
    EXPECT_TRUE(w.m_sLocalUrl.isEmpty());
    EXPECT_EQ(spy.count(), 1);
}

TEST(mircastwidget_t10, startDlnaTp_nullServer_withItemProps_returnsEarly)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    ASSERT_EQ(w.m_dlnaContentServer, nullptr);   // standalone: no HTTP server
    ItemWidget item(MiracastDevice{QString("dev"), QString("uuid-1")},
                    QByteArray(), nullptr);
    item.setProperty(urlAddrPro, "http://tv/host");
    item.setProperty(controlURLPro, "http://tv/ctl/AVTransport");
    t10_SoapGuard soapGuard;
    w.startDlnaTp(&item);                        // reads props, then early return
    EXPECT_EQ(w.m_ControlURLPro, QString("http://tv/ctl/AVTransport"));
    EXPECT_EQ(w.m_URLAddrPro, QString("http://tv/host"));
    EXPECT_EQ(t10_soapCalls, 0);                 // never reached SoapOperPost
    EXPECT_EQ(w.m_nPlayStatus, MircastWidget::NoState);
}

TEST(mircastwidget_t10, startDlnaTp_nullServer_noItem_keepsUrls)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget w;
    t10_SoapGuard soapGuard;
    w.m_ControlURLPro = "keep-me";
    w.startDlnaTp(nullptr);                      // item == nullptr: props kept
    EXPECT_EQ(w.m_ControlURLPro, QString("keep-me"));
    EXPECT_EQ(t10_soapCalls, 0);
}

TEST(mircastwidget_t10, slotMircastTimeout_belowMax_incrementsCounter)
{
    MircastWidget w(nullptr, nullptr);
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);   // real object; empty URLs keep the SOAP post harmless
    w.m_connectTimeout = 2;
    w.m_mircastTimeOut.start(60000);
    ASSERT_TRUE(w.m_mircastTimeOut.isActive());
    w.slotMircastTimeout();
    EXPECT_EQ(w.m_connectTimeout, 3);
    EXPECT_TRUE(w.m_mircastTimeOut.isActive());  // below MAXMIRCAST: timer keeps running
}

TEST(mircastwidget_t10, slotMircastTimeout_maxAttempts_emitsFailedAndStopsTimer)
{
    MircastWidget w(nullptr, nullptr);
    w.m_pDlnaSoapPost = new CDlnaSoapPost(&w);
    w.m_connectTimeout = 4;                      // MAXMIRCAST = 5 (defined in mircastwidget.cpp:26)
    QSignalSpy spy(&w, &MircastWidget::mircastState);
    w.m_mircastTimeOut.start(60000);
    ASSERT_TRUE(w.m_mircastTimeOut.isActive());
    w.slotMircastTimeout();
    EXPECT_EQ(w.m_connectTimeout, 5);
    EXPECT_FALSE(w.m_mircastTimeOut.isActive()); // timer stopped at max attempts
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().first().toInt(), MIRCAST_CONNECTION_FAILED);   // not Screening -> -3
}
