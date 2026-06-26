// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 3) for src/widgets/mircastwidget.cpp.
// Suite name "mircastwidget_ext"; static helpers use unique prefix "mce_".
//
// What this file adds that test_mircast_ext.cpp / test_mircast_ext2.cpp do not:
//   * Constructs MircastWidget STANDALONE (parent=nullptr, engine=nullptr).
//     The earlier suites only ever fetch the shared widget from the live app's
//     toolbox, so the constructor body (lines 32-166) is never directly
//     measured. Constructing standalone also gives every case a clean,
//     isolated state so we can mutate private members without leaking into
//     later suites.
//   * Reaches branches the shared-widget suites cannot, because they always
//     operate with an empty m_devicesList and pristine flags:
//       - togglePopup re-entrancy guard (m_bIsToggling == true -> no-op).
//       - updateMircastState(Searching) with a NON-empty device list: the
//         `if (!m_devicesList.isEmpty()) return;` early return.
//       - slotSearchTimeout with a NON-empty device list -> ListExhibit branch
//         (the earlier suites only hit the empty -> NoDevices branch).
//       - stopDlnaTP with a NON-empty ControlURLPro but mismatched track URI:
//         the soap call is skipped, so m_pDlnaSoapPost (uninitialized on a
//         standalone widget) is never dereferenced.
//       - MircastWidget::createListeItem wrapper (forwards to ListWidget and
//         applies the Screening/URL-equality guard).
//       - slotExitMircast from Screening (standalone, empty ControlURLPro ->
//         stopDlnaTP early-returns before any soap deref).
//
// Safety notes baked into every case:
//   * On a standalone MircastWidget, m_pDlnaSoapPost and m_search are NEVER
//     initialized by the constructor (only initializeHttpServer() assigns
//     them). They therefore hold garbage. We NEVER call any path that
//     dereferences them: initializeHttpServer, searchDevices,
//     slotRefreshBtnClicked, slotMircastTimeout, slotConnectDevice,
//     startDlnaTp, pauseDlnaTp, playDlnaTp, seekDlnaTp, getPosInfoDlnaTp, and
//     the Screening branches of seekMircast / slotPauseDlnaTp are all avoided
//     or GTEST_SKIP'd. stopDlnaTP is only driven through the track-mismatch
//     branch that skips the SoapOperPost call (and we additionally null the
//     pointer defensively).
//   * No mpv backend / no playlist: anything needing engine state is skipped.
//   * Paint / show paths are guarded by QGuiApplication::primaryScreen().
//   * Mutated private state is restored at the end of each case.

#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QMetaObject>

#include <gtest/gtest.h>

// Pre-include the heavy transitive dependencies of mircastwidget.h so that the
// `#define private public` block below only re-parses the target header (its
// includes are now guarded and skipped). Mirrors the pattern used by the
// platform_mainwindow extension tests.
#include <DWidget>
#include <DFloatingWidget>
#include <DLabel>
#include <DSpinner>
#include <QTimer>
#include <QScrollArea>
#include <QIcon>
#include "src/dlna/cdlnasoappost.h"

#define protected public
#define private public
#include "src/widgets/mircastwidget.h"
#undef protected
#undef private

#include "src/dlna/cssdpsearch.h"   // urlAddrPro / controlURLPro / friendlyNamePro
#include "application.h"

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// Build a valid DLNA device-description XML (same shape the production parser
// consumes in slotReadyRead / ItemWidget ctor).
static QByteArray mce_deviceXml(const char *friendlyName = "MCE-Device",
                                const char *udn = "uuid:00000000-1111-2222-3333-444444444444")
{
    return QByteArray(" \
        <root xmlns=\"urn:schemas-upnp-org:device-1-0\"> \
        <specVersion><major>1</major><minor>0</minor></specVersion> \
        <device> \
        <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType> \
        <UDN>") + QByteArray(udn) + QByteArray("</UDN> \
        <friendlyName>") + QByteArray(friendlyName) + QByteArray("</friendlyName> \
        <serviceList> \
        <service> \
        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType> \
        <controlURL>/ctl/AVTransport</controlURL> \
        </service> \
        </serviceList> \
        </device> \
        </root>");
}

// Invoke the private timeConversion(QString)->int via the meta-object system
// (it is a private slot; reached reflectively so we never need friend access).
static int mce_timeConversion(MircastWidget *mw, const QString &s)
{
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, s));
    if (!ok) return -1;
    return out;
}

// ==========================================================================
// Standalone construction. The earlier suites never build a MircastWidget
// directly, so the constructor body (state init, layout, hint widget, scroll
// area, refresh button, DSizeMode wiring) is uncovered. We build one fresh
// per case and assert the post-conditions the constructor establishes.
// ==========================================================================

TEST(mircastwidget_ext, construct_standalone_initialState_isIdel)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    EXPECT_EQ(mw.getMircastState(), MircastWidget::Idel);
    EXPECT_EQ(mw.getMircastPlayState(), MircastWidget::NoState);
    EXPECT_NE(mw.getRefreshBtn(), nullptr);
}

TEST(mircastwidget_ext, construct_standalone_appliesFixedSize)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    // MIRCASTWIDTH(240)+14 x MIRCASTHEIGHT(188)+12 == 254 x 200.
    EXPECT_EQ(mw.size().width(), 254);
    EXPECT_EQ(mw.size().height(), 200);
}

TEST(mircastwidget_ext, construct_standalone_hintAreaInitiallyHidden)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    // The constructor hides both m_hintWidget and m_mircastArea before any
    // state update; confirm neither is flagged hidden-after-show pending.
    EXPECT_TRUE(mw.m_hintWidget->isHidden());
    EXPECT_TRUE(mw.m_mircastArea->isHidden());
}

TEST(mircastwidget_ext, construct_standalone_attemptsAndTimeoutsZero)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    EXPECT_EQ(mw.m_attempts, 0);
    EXPECT_EQ(mw.m_connectTimeout, 0);
    EXPECT_EQ(mw.m_nCurDuration, -1);
    EXPECT_EQ(mw.m_nCurAbsTime, -1);
    EXPECT_TRUE(mw.m_ControlURLPro.isEmpty());
    EXPECT_TRUE(mw.m_URLAddrPro.isEmpty());
    EXPECT_TRUE(mw.m_sLocalUrl.isEmpty());
}

// ==========================================================================
// timeConversion on a standalone widget -- keeps the pure-logic parse covered
// without depending on the shared app widget. (Earlier suites cover this on
// the shared widget; we re-cover on standalone so the constructor + parse
// path are measured in one isolated process path.)
// ==========================================================================

TEST(mircastwidget_ext, timeConversion_onStandalone_hms)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    EXPECT_EQ(mce_timeConversion(&mw, "01:02:03"), 1 * 3600 + 2 * 60 + 3);
}

TEST(mircastwidget_ext, timeConversion_onStandalone_invalidShape_zero)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    EXPECT_EQ(mce_timeConversion(&mw, "NOT_IMPLEMENTED"), 0);
    EXPECT_EQ(mce_timeConversion(&mw, ""), 0);
    EXPECT_EQ(mce_timeConversion(&mw, "1:2"), 0);
}

// ==========================================================================
// togglePopup -- re-entrancy guard. When m_bIsToggling is true the function
// must return immediately, leaving visibility unchanged. The earlier suites
// only exercise the visible->hide and hidden->show branches.
// ==========================================================================

TEST(mircastwidget_ext, togglePopup_whenToggling_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.hide();
    QTest::qWait(20);
    ASSERT_FALSE(mw.isVisible());

    // Simulate an in-flight toggle. The function must bail out before the
    // show()/refreshStart() path, leaving the widget hidden.
    mw.m_bIsToggling = true;
    mw.togglePopup();
    EXPECT_FALSE(mw.isVisible());
    // Restore the guard so destruction / later cases are unaffected.
    mw.m_bIsToggling = false;
}

TEST(mircastwidget_ext, togglePopup_whenToggling_doesNotShowEvenIfVisible)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.show();
    QTest::qWait(20);
    ASSERT_TRUE(mw.isVisible());

    mw.m_bIsToggling = true;
    mw.togglePopup(); // must NOT hide because guard returns first.
    EXPECT_TRUE(mw.isVisible());
    mw.m_bIsToggling = false;
    mw.hide();
    QTest::qWait(20);
}

TEST(mircastwidget_ext, togglePopup_hideWhenVisible_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.show();
    QTest::qWait(20);
    ASSERT_TRUE(mw.isVisible());
    mw.togglePopup(); // visible -> hide branch (no refreshStart, no network)
    EXPECT_FALSE(mw.isVisible());
}

// NOTE: The hidden->show branch of togglePopup is intentionally NOT exercised
// here. That branch calls m_refreshBtn->refreshStart(), which emits
// buttonClicked -- and the constructor wires buttonClicked to
// slotRefreshBtnClicked -> initializeHttpServer() (binds a real
// DlnaContentServer) + searchDevices() (real SSDP). That is the network path
// this suite must avoid. The earlier suites cover it on the shared widget;
// repeating it on a standalone widget would only stack a second HTTP server
// on port 9999.

// ==========================================================================
// updateMircastState -- the Searching branch has an early return when the
// device list is non-empty. The earlier suites always have an empty list, so
// that return is never hit. We populate m_devicesList on a standalone widget.
// ==========================================================================

TEST(mircastwidget_ext, updateMircastState_searching_nonEmptyList_returnsEarly)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);

    // Pre-condition: show the hint area, then put a device in the list. The
    // Searching branch must return before hide()/show() calls, so the hint
    // area visibility must NOT change.
    mw.m_hintWidget->show();
    mw.m_mircastArea->hide();
    ASSERT_FALSE(mw.m_hintWidget->isHidden());

    MiracastDevice dev;
    dev.name = "MCE-TV";
    dev.uuid = "uuid:mce-tv";
    mw.m_devicesList.append(dev);
    mw.updateMircastState(MircastWidget::Searching);

    // Early return: hint widget still not hidden, area still hidden.
    EXPECT_FALSE(mw.m_hintWidget->isHidden());
    EXPECT_TRUE(mw.m_mircastArea->isHidden());

    // Restore.
    mw.m_devicesList.clear();
}

TEST(mircastwidget_ext, updateMircastState_searching_emptyList_setsHint_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.updateMircastState(MircastWidget::Searching);
    // Empty list -> full Searching body: hint shown, area hidden.
    EXPECT_FALSE(mw.m_hintWidget->isHidden());
    EXPECT_TRUE(mw.m_mircastArea->isHidden());
}

TEST(mircastwidget_ext, updateMircastState_listExhibit_showsArea_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.m_mircastArea->hide();
    ASSERT_TRUE(mw.m_mircastArea->isHidden());
    mw.updateMircastState(MircastWidget::ListExhibit);
    EXPECT_FALSE(mw.m_mircastArea->isHidden());
    EXPECT_TRUE(mw.m_hintWidget->isHidden());
}

TEST(mircastwidget_ext, updateMircastState_noDevices_hidesArea_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.m_mircastArea->show();
    ASSERT_FALSE(mw.m_mircastArea->isHidden());
    mw.updateMircastState(MircastWidget::NoDevices);
    EXPECT_TRUE(mw.m_mircastArea->isHidden());
}

// ==========================================================================
// slotSearchTimeout -- the non-empty-list branch routes to ListExhibit. The
// earlier suites only hit the empty -> NoDevices branch.
// ==========================================================================

TEST(mircastwidget_ext, slotSearchTimeout_nonEmptyList_marksListExhibit)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);

    MiracastDevice dev;
    dev.name = "MCE-Renderer";
    dev.uuid = "uuid:mce-renderer";
    mw.m_devicesList.append(dev);

    mw.m_mircastArea->hide();
    ASSERT_TRUE(mw.m_mircastArea->isHidden());

    mw.slotSearchTimeout(); // non-empty -> updateMircastState(ListExhibit)
    EXPECT_FALSE(mw.m_mircastArea->isHidden());

    // Restore.
    mw.m_devicesList.clear();
}

TEST(mircastwidget_ext, slotSearchTimeout_emptyList_marksNoDevices_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.m_mircastArea->show();
    ASSERT_FALSE(mw.m_mircastArea->isHidden());
    mw.slotSearchTimeout(); // empty -> NoDevices
    EXPECT_TRUE(mw.m_mircastArea->isHidden());
}

// ==========================================================================
// stopDlnaTP -- the non-empty-ControlURLPro / track-URI-mismatch branch skips
// the SoapOperPost call. On a standalone widget m_pDlnaSoapPost is garbage, so
// reaching the soap call would crash; we deliberately steer into the skip
// branch and additionally null the pointer as a safety net.
// ==========================================================================

TEST(mircastwidget_ext, stopDlnaTP_nonEmptyUrl_trackMismatch_skipsSoap)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);

    // Defensively null the uninitialized soap pointer (never dereferenced on
    // this branch, but hygienic).
    mw.m_pDlnaSoapPost = nullptr;
    mw.m_ControlURLPro = "http://127.0.0.1:9999/ctl/AVTransport";
    mw.m_sTrackURI = "http://host/a.mp4";   // different from local url
    mw.m_sLocalUrl = "http://127.0.0.1:9999/b.mp4";
    mw.setMircastPlayState(MircastWidget::Play);

    mw.stopDlnaTP(); // ControlURLPro non-empty, track mismatch -> soap skipped.

    EXPECT_EQ(mw.getMircastPlayState(), MircastWidget::Stop);
    EXPECT_TRUE(mw.m_ControlURLPro.isEmpty());
    EXPECT_TRUE(mw.m_URLAddrPro.isEmpty());
    EXPECT_TRUE(mw.m_sLocalUrl.isEmpty());
}

TEST(mircastwidget_ext, stopDlnaTP_emptyControlUrl_returnsEarly_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.m_pDlnaSoapPost = nullptr; // defensive
    mw.setMircastPlayState(MircastWidget::Play);
    mw.stopDlnaTP(); // empty ControlURLPro -> early return
    EXPECT_EQ(mw.getMircastPlayState(), MircastWidget::Stop);
}

// ==========================================================================
// MircastWidget::createListeItem wrapper -- forwards to ListWidget and applies
// the Screening/URL-equality guard. With m_mircastState == Idel the guard's
// setState is skipped, so we simply get back a constructed item. The earlier
// suites exercise ListWidget::createListeItem but NOT this wrapper.
// ==========================================================================

TEST(mircastwidget_ext, createListeItem_wrapper_returnsItem)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);

    MiracastDevice dev;
    dev.name = "Wrapper-TV";
    dev.uuid = "uuid:wrapper-tv";
    ItemWidget *item = mw.createListeItem(dev, mce_deviceXml("Wrapper-TV", "uuid:wrapper-tv"), nullptr);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->getDevice().name, QString("Wrapper-TV"));
    EXPECT_EQ(item->state(), ItemWidget::Normal); // guard skipped setState

    // Clean up the items the wrapper added to the owned list widget.
    mw.m_listWidget->clear();
}

TEST(mircastwidget_ext, createListeItem_wrapper_whenScreening_nonMatchingUrl_leavesNormal)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);

    // Put the widget into Screening with a URL that does NOT match the new
    // item's (empty) urlAddrPro property -> the Checked-state branch is
    // skipped, item stays Normal.
    mw.setMircastState(MircastWidget::Screening);
    mw.m_URLAddrPro = "http://1.2.3.4:9999/";

    MiracastDevice dev;
    dev.name = "Other-TV";
    dev.uuid = "uuid:other-tv";
    ItemWidget *item = mw.createListeItem(dev, mce_deviceXml("Other-TV", "uuid:other-tv"), nullptr);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->state(), ItemWidget::Normal);

    mw.setMircastState(MircastWidget::Idel);
    mw.m_URLAddrPro.clear();
    mw.m_listWidget->clear();
}

// ==========================================================================
// slotExitMircast -- from Screening. The known-crashing case in the runner is
// mircast_ext.slotExitMircast_whenConnecting_resetsState (excluded); we avoid
// that exact path. From Screening on a standalone widget, stopDlnaTP early-
// returns (empty ControlURLPro) so no soap dereference occurs and no engine is
// touched. This covers the state-resetting side of the non-Idel path.
// ==========================================================================

TEST(mircastwidget_ext, slotExitMircast_fromScreening_resetsToIdel)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.m_pDlnaSoapPost = nullptr; // defensive; stopDlnaTP early-returns anyway
    mw.setMircastState(MircastWidget::Screening);
    mw.m_connectTimeout = 7;
    mw.m_URLAddrPro = "http://leftover/";

    mw.slotExitMircast();

    EXPECT_EQ(mw.getMircastState(), MircastWidget::Idel);
    EXPECT_EQ(mw.m_connectTimeout, 0);
    EXPECT_TRUE(mw.m_URLAddrPro.isEmpty());
}

TEST(mircastwidget_ext, slotExitMircast_fromIdel_isNoOp_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.setMircastState(MircastWidget::Idel);
    mw.m_URLAddrPro = "http://keep/";
    mw.slotExitMircast(); // early return; URL untouched.
    EXPECT_EQ(mw.getMircastState(), MircastWidget::Idel);
    EXPECT_EQ(mw.m_URLAddrPro, QString("http://keep/"));
}

// ==========================================================================
// Guarded early-return paths on a standalone widget (Idel / not-Screening).
// These mirror the shared-widget cases but exercise the freshly-constructed
// widget's state, adding constructor-adjacent line coverage.
// ==========================================================================

TEST(mircastwidget_ext, seekMircast_notScreening_standalone_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.setMircastState(MircastWidget::Idel);
    mw.seekMircast(10);  // returns before any soap dereference
    mw.seekMircast(-10);
    SUCCEED();
}

TEST(mircastwidget_ext, slotPauseDlnaTp_notScreening_standalone_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.setMircastState(MircastWidget::Idel);
    mw.slotPauseDlnaTp(); // Screening guard -> return
    SUCCEED();
}

TEST(mircastwidget_ext, slotGetPositionInfo_whenIdel_standalone_returnsEarly)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.setMircastState(MircastWidget::Idel);
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:05";
    info.sTrackDuration = "00:01:00";
    info.sTrackURI = "http://example/x";
    mw.slotGetPositionInfo(info); // Idel -> immediate return
    SUCCEED();
}

TEST(mircastwidget_ext, playNext_whenIdel_standalone_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    mw.setMircastState(MircastWidget::Idel);
    mw.playNext(); // body skipped (no engine/soap dereference)
    EXPECT_EQ(mw.getMircastState(), MircastWidget::Idel);
}

TEST(mircastwidget_ext, setMircastState_allValuesRoundTrip_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    for (int s = MircastWidget::Connecting; s <= MircastWidget::Idel; ++s) {
        auto st = static_cast<MircastWidget::MircastState>(s);
        mw.setMircastState(st);
        EXPECT_EQ(mw.getMircastState(), st);
    }
    mw.setMircastState(MircastWidget::Idel);
}

TEST(mircastwidget_ext, setMircastPlayState_allValuesRoundTrip_standalone)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    for (int s = MircastWidget::NoState; s <= MircastWidget::Stop; ++s) {
        auto st = static_cast<MircastWidget::MircastPlayState>(s);
        mw.setMircastPlayState(st);
        EXPECT_EQ(mw.getMircastPlayState(), st);
    }
    mw.setMircastPlayState(MircastWidget::NoState);
}

// ==========================================================================
// RefreButtonWidget -- exercise refreshStart/refreshTimeout through the
// widget's OWNED button, but FIRST disconnect the constructor's
// buttonClicked -> slotRefreshBtnClicked wiring so refreshStart()'s emit
// cannot reach initializeHttpServer()/searchDevices() (the network path).
// This covers the owned RefreButtonWidget's methods in isolation.
// ==========================================================================

TEST(mircastwidget_ext, refreshBtn_ownedByWidget_refreshCycle_disconnected_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    RefreButtonWidget *btn = mw.getRefreshBtn();
    ASSERT_NE(btn, nullptr);

    // Neutralize the network-triggering connection for the duration of the
    // case. refreshStart() emits buttonClicked; with the connection severed
    // it stays a pure local UI operation.
    QObject::disconnect(btn, &RefreButtonWidget::buttonClicked,
                        &mw, &MircastWidget::slotRefreshBtnClicked);

    int clicks = 0;
    auto c = QObject::connect(btn, &RefreButtonWidget::buttonClicked,
                              [&]() { clicks++; });
    btn->refreshStart();   // emits buttonClicked -> clicks=1, no network
    EXPECT_EQ(clicks, 1);
    btn->refreshTimeout();
    btn->refreshStart();   // clicks=2
    EXPECT_EQ(clicks, 2);
    btn->refreshTimeout();
    QObject::disconnect(c);
    SUCCEED();
}

TEST(mircastwidget_ext, refreshBtn_ownedByWidget_mouseRelease_spinnerGuard)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget mw(nullptr, nullptr);
    RefreButtonWidget *btn = mw.getRefreshBtn();
    ASSERT_NE(btn, nullptr);
    QObject::disconnect(btn, &RefreButtonWidget::buttonClicked,
                        &mw, &MircastWidget::slotRefreshBtnClicked);

    btn->show();
    QTest::qWait(20);
    btn->refreshStart(); // spinner visible -> mouseRelease must be a no-op
    int clicks = 0;
    auto c = QObject::connect(btn, &RefreButtonWidget::buttonClicked,
                              [&]() { clicks++; });
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(btn, &re);
    QObject::disconnect(c);
    EXPECT_EQ(clicks, 0); // spinner-visible guard short-circuits
    btn->refreshTimeout();
    btn->hide();
    QTest::qWait(10);
}

// ==========================================================================
// Deliberately-skipped risky paths. These are documented here so reviewers
// know they were considered. Each unconditionally dereferences m_pDlnaSoapPost
// and/or m_search (uninitialized on a standalone widget) or initiates a real
// network/miracast session, so driving them would crash the single test
// process and lose all later coverage.
// ==========================================================================

TEST(mircastwidget_ext, slotRefreshBtnClicked_INITIATES_NETWORK_SKIP)
{
    GTEST_SKIP() << "Calls initializeHttpServer() (binds DlnaContentServer to "
                    "a local IP) + searchDevices() (real SSDP UDP). Network.";
}

TEST(mircastwidget_ext, initializeHttpServer_BINDS_REAL_SERVER_SKIP)
{
    GTEST_SKIP() << "Allocates m_search/m_pDlnaSoapPost and binds a real "
                    "DlnaContentServer; network + side effects.";
}

TEST(mircastwidget_ext, searchDevices_REAL_SSDP_SKIP)
{
    GTEST_SKIP() << "Dereferences m_search (uninitialized on standalone) and "
                    "issues real SSDP multicast.";
}

TEST(mircastwidget_ext, slotMircastTimeout_DEREFS_SOAP_SKIP)
{
    GTEST_SKIP() << "Unconditionally dereferences m_pDlnaSoapPost.";
}

TEST(mircastwidget_ext, slotConnectDevice_DEREFS_ENGINE_AND_SOAP_SKIP)
{
    GTEST_SKIP() << "Dereferences m_pEngine and calls startDlnaTp (soap).";
}

TEST(mircastwidget_ext, seekMircast_whenScreening_DEREFS_SOAP_SKIP)
{
    GTEST_SKIP() << "Screening branch dispatches to slotSeekMircast/seekDlnaTp "
                    "which dereferences m_pDlnaSoapPost.";
}

TEST(mircastwidget_ext, slotPauseDlnaTp_whenScreening_DEREFS_SOAP_SKIP)
{
    GTEST_SKIP() << "Screening branch dispatches to pauseDlnaTp/playDlnaTp "
                    "which dereference m_pDlnaSoapPost.";
}

TEST(mircastwidget_ext, slotGetPositionInfo_nonIdel_DEREFS_ENGINE_SKIP)
{
    GTEST_SKIP() << "Non-Idel branch dereferences m_pEngine and playlist model.";
}
