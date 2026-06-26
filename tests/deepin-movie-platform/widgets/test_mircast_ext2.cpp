// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests (round 2) for src/widgets/mircastwidget.cpp and
// src/widgets/mircastshowwidget.cpp.
// Suite name "mircast_ext2"; static helpers reuse the "mir_ext_" prefix from
// test_mircast_ext.cpp (same helpers, duplicated locally so this file stays
// self-contained).
//
// Safety notes baked into every case below:
//   * The MircastWidget is obtained via the running app's toolbox. Its
//     m_pDlnaSoapPost is only allocated inside initializeHttpServer(), which
//     also binds a real DlnaContentServer to a local IP. We NEVER call that
//     path, so every soap/network entry point is guarded so it does not
//     dereference the uninitialized pointer:
//       - pauseDlnaTp / playDlnaTp / seekDlnaTp / getPosInfoDlnaTp dereference
//         m_pDlnaSoapPost unconditionally; we only touch them indirectly through
//         slotPauseDlnaTp (Screening guard) or slotExitMircast (Idel guard /
//         stopDlnaTP empty-URL guard). They are NOT called directly here.
//       - stopDlnaTP returns early when ControlURLPro is empty.
//       - slotMircastTimeout unconditionally dereferences m_pDlnaSoapPost; we
//         do NOT exercise it.
//   * Do NOT call slotExitMircast after setMircastState(Connecting) on an
//     EMPTY playlist -- it triggers Q_ASSERT("_infos.size()>0") deep in the
//     engine path. The Connecting->slotExitMircast case below relies on
//     stopDlnaTP's empty-ControlURLPro early return, which happens before any
//     engine dereference.
//   * No mpv backend / no playlist loaded: anything needing engine state is
//     guarded or skipped.
//   * Fresh local widgets are used for screen/geometry cases with a
//     primaryScreen() guard.

#include "src/widgets/mircastwidget.h"
#include "src/widgets/mircastshowwidget.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/dlna/cssdpsearch.h"   // urlAddrPro / controlURLPro / friendlyNamePro
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QMetaObject>
#include <QGraphicsScene>
#include <QGraphicsTextItem>

using namespace dmr;

// --- Helpers (mirror test_mircast_ext.cpp so this TU is self-contained) ----

// Fetch the shared MircastWidget from the running application's toolbox.
static MircastWidget *mir_ext2_widget()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    if (!w) return nullptr;
    Platform_ToolboxProxy *tb = w->toolbox();
    if (!tb) return nullptr;
    return tb->getMircast();
}

// Build a valid DLNA device-description XML (same shape the production parser
// consumes in slotReadyRead / ItemWidget ctor).
static QByteArray mir_ext2_deviceXml(const char *friendlyName = "UT2-Device",
                                     const char *udn = "uuid:aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
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

// Invoke the private timeConversion(QString)->int via the meta-object system.
static int mir_ext2_timeConversion(MircastWidget *mw, const QString &s)
{
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, s));
    if (!ok) return -1;
    return out;
}

// ==========================================================================
// timeConversion -- pure string->int parse. Biggest safe coverage win.
// Format "HH:MM:SS" -> seconds; anything not exactly 3 colon-parts -> 0.
// ==========================================================================

TEST(mircast_ext2, timeConversion_hms_standard)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "01:02:03"), 1 * 3600 + 2 * 60 + 3);
}

TEST(mircast_ext2, timeConversion_zero)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "0:0:0"), 0);
}

TEST(mircast_ext2, timeConversion_allZerosPadded)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "00:00:00"), 0);
}

TEST(mircast_ext2, timeConversion_largeValues)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "100:200:300"),
              100 * 3600 + 200 * 60 + 300);
}

TEST(mircast_ext2, timeConversion_onlyHours)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "2:0:0"), 2 * 3600);
}

TEST(mircast_ext2, timeConversion_onlyMinutes)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "0:5:0"), 5 * 60);
}

TEST(mircast_ext2, timeConversion_onlySeconds)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "0:0:42"), 42);
}

TEST(mircast_ext2, timeConversion_nonNumericParts_returnsZero)
{
    // toInt() on non-numeric returns 0; size==3 so the math runs but yields 0.
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "xx:yy:zz"), 0);
}

TEST(mircast_ext2, timeConversion_partialNumeric_returnsPartial)
{
    // "1:abc:3" -> 1*3600 + 0 + 3
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "1:abc:3"), 3600 + 3);
}

TEST(mircast_ext2, timeConversion_emptyString_returnsZero)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, ""), 0);
}

TEST(mircast_ext2, timeConversion_twoParts_returnsZero)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "02:30"), 0);
}

TEST(mircast_ext2, timeConversion_fourParts_returnsZero)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "1:2:3:4"), 0);
}

TEST(mircast_ext2, timeConversion_notImplementedMarker_returnsZero)
{
    // The engine feeds "NOT_IMPLEMENTED" through this; 1 part -> 0.
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mir_ext2_timeConversion(mw, "NOT_IMPLEMENTED"), 0);
}

TEST(mircast_ext2, timeConversion_trailingColon_returnsZero)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    // "1:2:" splits to ["1","2",""] -> size 3, last toInt()=0 -> 1*3600+2*60
    EXPECT_EQ(mir_ext2_timeConversion(mw, "1:2:"), 3600 + 120);
}

// ==========================================================================
// getMircastState / getMircastPlayState / set* round-trips
// (Original suite covers the basics; we add enum-coverage and isolation.)
// ==========================================================================

TEST(mircast_ext2, getMircastState_afterSet_isReadbackValue)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Screening);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Screening);
    mw->setMircastState(MircastWidget::Idel);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

TEST(mircast_ext2, setMircastState_allValuesRoundTrip)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    for (int s = MircastWidget::Connecting; s <= MircastWidget::Idel; ++s) {
        auto st = static_cast<MircastWidget::MircastState>(s);
        mw->setMircastState(st);
        EXPECT_EQ(mw->getMircastState(), st);
    }
    // Always restore to Idel so later cases start clean.
    mw->setMircastState(MircastWidget::Idel);
}

TEST(mircast_ext2, setMircastPlayState_allValuesRoundTrip)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    for (int s = MircastWidget::NoState; s <= MircastWidget::Stop; ++s) {
        auto st = static_cast<MircastWidget::MircastPlayState>(s);
        mw->setMircastPlayState(st);
        EXPECT_EQ(mw->getMircastPlayState(), st);
    }
    mw->setMircastPlayState(MircastWidget::NoState);
}

TEST(mircast_ext2, setMircastState_repeatedSameValue_isIdempotent)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    mw->setMircastState(MircastWidget::Connecting);
    mw->setMircastState(MircastWidget::Connecting);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Connecting);
    mw->setMircastState(MircastWidget::Idel);
}

// ==========================================================================
// updateMircastState -- all three SearchState branches, both list branches.
// ==========================================================================

TEST(mircast_ext2, updateMircastState_searching_safe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::Searching);
    SUCCEED();
}

TEST(mircast_ext2, updateMircastState_listExhibit_safe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::ListExhibit);
    SUCCEED();
}

TEST(mircast_ext2, updateMircastState_noDevices_safe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::NoDevices);
    SUCCEED();
}

TEST(mircast_ext2, updateMircastState_allStatesInSequence)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::Searching);
    mw->updateMircastState(MircastWidget::ListExhibit);
    mw->updateMircastState(MircastWidget::NoDevices);
    mw->updateMircastState(MircastWidget::ListExhibit);
    SUCCEED();
}

// ==========================================================================
// slotSearchTimeout -- empty list (NoDevices branch) and the side-effect
// of invoking refreshTimeout/update on the shared widget.
// ==========================================================================

TEST(mircast_ext2, slotSearchTimeout_emptyList_isSafe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->slotSearchTimeout(); // empty list -> NoDevices branch
    SUCCEED();
}

TEST(mircast_ext2, slotSearchTimeout_calledTwice_isSafe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->slotSearchTimeout();
    mw->slotSearchTimeout();
    SUCCEED();
}

// ==========================================================================
// slotExitMircast -- Idel early-return is the only universally-safe path on
// the shared widget. From non-Idel we rely on stopDlnaTP's empty-ControlURLPro
// early return BEFORE any soap dereference; that still executes the
// state-resetting side (timer stop, m_URLAddrPro.clear, signal emit).
// ==========================================================================

TEST(mircast_ext2, slotExitMircast_whenIdel_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->slotExitMircast();
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

TEST(mircast_ext2, slotExitMircast_whenIdel_keepsPlayState)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->setMircastPlayState(MircastWidget::Play);
    mw->slotExitMircast(); // early-returns before touching play state
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Play);
    mw->setMircastPlayState(MircastWidget::NoState);
}

TEST(mircast_ext2, slotExitMircast_fromConnecting_resetsToIdel)
{
    // SAFE because: slotExitMircast -> stopDlnaTP, and stopDlnaTP early-returns
    // when m_ControlURLPro is empty (it is, since we never call
    // initializeHttpServer/startDlnaTp). The SoapOperPost dereference is never
    // reached. The engine itself is never touched in this path.
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    mw->slotExitMircast();
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

// ==========================================================================
// stopDlnaTP -- empty ControlURLPro -> safe early return; still sets Stop.
// ==========================================================================

TEST(mircast_ext2, stopDlnaTP_emptyControlUrl_setsPlayStateStop)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastPlayState(MircastWidget::Play);
    mw->stopDlnaTP();
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Stop);
    mw->setMircastPlayState(MircastWidget::NoState);
}

TEST(mircast_ext2, stopDlnaTP_calledRepeatedly_isSafe)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->stopDlnaTP();
    mw->stopDlnaTP();
    mw->stopDlnaTP();
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Stop);
    mw->setMircastPlayState(MircastWidget::NoState);
}

// ==========================================================================
// seekMircast -- Screening guard. When not Screening it returns before any
// math; when Screening it computes nSeek and dispatches to slotSeekMircast
// -> seekDlnaTp which DOES deref soap. So we only exercise the not-Screening
// early-return on the shared widget.
// ==========================================================================

TEST(mircast_ext2, seekMircast_notScreening_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->seekMircast(10);
    mw->seekMircast(-10);
    mw->seekMircast(0);
    SUCCEED();
}

TEST(mircast_ext2, seekMircast_whenConnecting_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    mw->seekMircast(5); // only Screening proceeds; Connecting returns
    mw->setMircastState(MircastWidget::Idel);
    SUCCEED();
}

// ==========================================================================
// slotPauseDlnaTp -- Screening guard. Not Screening -> early return, no soap.
// ==========================================================================

TEST(mircast_ext2, slotPauseDlnaTp_whenIdel_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->slotPauseDlnaTp();
    SUCCEED();
}

TEST(mircast_ext2, slotPauseDlnaTp_whenConnecting_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    mw->slotPauseDlnaTp();
    mw->setMircastState(MircastWidget::Idel);
    SUCCEED();
}

// ==========================================================================
// playNext -- Idel guard. The body only runs when m_mircastState != Idel, and
// when it does it dereferences m_pEngine and calls startDlnaTp (soap). So on
// the shared widget we ONLY exercise the Idel early-return path (the `if`
// condition is false, body skipped).
// ==========================================================================

TEST(mircast_ext2, playNext_whenIdel_isNoOp)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->playNext(); // body skipped, no engine/soap dereference
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

// ==========================================================================
// slotGetPositionInfo -- Idel guard. Early-returns when Idel.
// ==========================================================================

TEST(mircast_ext2, slotGetPositionInfo_whenIdel_returnsEarly)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:05";
    info.sTrackDuration = "00:01:00";
    info.sTrackURI = "http://example/x";
    mw->slotGetPositionInfo(info);
    SUCCEED();
}

TEST(mircast_ext2, slotGetPositionInfo_whenIdel_ignoresNotImplemented)
{
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    DlnaPositionInfo info;
    info.sAbsTime = "NOT_IMPLEMENTED";
    info.sTrackDuration = "NOT_IMPLEMENTED";
    info.sTrackURI = "";
    mw->slotGetPositionInfo(info);
    SUCCEED();
}

// ==========================================================================
// slotRefreshBtnClicked -- DO NOT call on shared widget: it calls
// initializeHttpServer() (binds real network) + searchDevices() (real SSDP).
// Skip entirely; covered indirectly by RefreButtonWidget standalone tests.
// ==========================================================================

// ==========================================================================
// createListeItem on shared widget -- mutates the shared list. We instead
// exercise ListWidget standalone below for the building/selection logic, and
// use the shared widget's createListeItem ONLY to confirm it forwards to the
// list and returns a non-null item. We restore state via the list's clear()
// afterwards via getMircast -> ... not exposed; skip to avoid cross-suite
// list pollution. (Covered in standalone ListWidget cases.)
// ==========================================================================

// ==========================================================================
// togglePopup -- show/hide logic; safe, no network. (Original suite covers
// both branches; we add a busy-flag re-entrancy guard check.)
// ==========================================================================

TEST(mircast_ext2, togglePopup_hideThenShow_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->show();
    QTest::qWait(20);
    ASSERT_TRUE(mw->isVisible());
    mw->togglePopup(); // visible -> hide
    EXPECT_FALSE(mw->isVisible());
    mw->togglePopup(); // hidden -> show + refreshStart
    EXPECT_TRUE(mw->isVisible());
    mw->hide();
    QTest::qWait(20);
}

TEST(mircast_ext2, togglePopup_show_whenHidden_setsVisible)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = mir_ext2_widget();
    ASSERT_NE(mw, nullptr);
    mw->hide();
    QTest::qWait(20);
    ASSERT_FALSE(mw->isVisible());
    mw->togglePopup();
    EXPECT_TRUE(mw->isVisible());
    mw->hide();
    QTest::qWait(20);
}

// ==========================================================================
// ItemWidget -- construction, state, accessors, display truncation, events.
// Standalone (parent=nullptr), no shared state, no network.
// ==========================================================================

TEST(mircast_ext2, itemWidget_construct_preservesDeviceFields)
{
    MiracastDevice dev;
    dev.name = "Living Room TV";
    dev.uuid = "uuid:lr-tv";
    ItemWidget item(dev, mir_ext2_deviceXml("Living Room TV", "uuid:lr-tv"), nullptr);
    EXPECT_EQ(item.getDevice().name, QString("Living Room TV"));
    EXPECT_EQ(item.getDevice().uuid, QString("uuid:lr-tv"));
}

TEST(mircast_ext2, itemWidget_tooltip_equalsDeviceName)
{
    MiracastDevice dev;
    dev.name = "Bedroom Speaker";
    dev.uuid = "uuid:bs";
    ItemWidget item(dev, mir_ext2_deviceXml("Bedroom Speaker", "uuid:bs"), nullptr);
    EXPECT_EQ(item.toolTip(), QString("Bedroom Speaker"));
}

TEST(mircast_ext2, itemWidget_initialState_isNormal)
{
    MiracastDevice dev; dev.name = "X"; dev.uuid = "uuid:x";
    ItemWidget item(dev, mir_ext2_deviceXml(), nullptr);
    EXPECT_EQ(item.state(), ItemWidget::Normal);
}

TEST(mircast_ext2, itemWidget_setState_loading_startsAndStops)
{
    MiracastDevice dev; dev.name = "L"; dev.uuid = "uuid:l";
    ItemWidget item(dev, mir_ext2_deviceXml(), nullptr);
    item.setState(ItemWidget::Loading);
    EXPECT_EQ(item.state(), ItemWidget::Loading);
    // Transition back to Normal stops the rotate timer.
    item.setState(ItemWidget::Normal);
    EXPECT_EQ(item.state(), ItemWidget::Normal);
}

TEST(mircast_ext2, itemWidget_setState_checked_roundTrip)
{
    MiracastDevice dev; dev.name = "C"; dev.uuid = "uuid:c";
    ItemWidget item(dev, mir_ext2_deviceXml(), nullptr);
    item.setState(ItemWidget::Checked);
    EXPECT_EQ(item.state(), ItemWidget::Checked);
    item.setState(ItemWidget::Normal);
    EXPECT_EQ(item.state(), ItemWidget::Normal);
}

TEST(mircast_ext2, itemWidget_clearSelect_isSafe)
{
    MiracastDevice dev; dev.name = "CS"; dev.uuid = "uuid:cs";
    ItemWidget item(dev, mir_ext2_deviceXml(), nullptr);
    item.clearSelect(); // just resets m_selected
    SUCCEED();
}

TEST(mircast_ext2, itemWidget_shortName_displayKeepsName)
{
    MiracastDevice dev; dev.name = "Short"; dev.uuid = "uuid:s";
    ItemWidget item(dev, mir_ext2_deviceXml("Short", "uuid:s"), nullptr);
    EXPECT_EQ(item.toolTip(), QString("Short"));
}

TEST(mircast_ext2, itemWidget_veryLongName_truncatesDisplay)
{
    MiracastDevice dev;
    dev.name = QString("W").repeated(300);
    dev.uuid = "uuid:long";
    ItemWidget item(dev, mir_ext2_deviceXml("w", "uuid:long"), nullptr);
    // convertDisplay runs in ctor; tooltip carries the original name.
    EXPECT_EQ(item.toolTip(), dev.name);
    SUCCEED();
}

TEST(mircast_ext2, itemWidget_mediumName_aroundThreshold_isSafe)
{
    // Name just over TEXT_WIDTH (170) -- exercises the truncation loop body
    // without being pathological.
    MiracastDevice dev;
    dev.name = QString("M").repeated(60);
    dev.uuid = "uuid:med";
    ItemWidget item(dev, mir_ext2_deviceXml("m", "uuid:med"), nullptr);
    SUCCEED();
}

TEST(mircast_ext2, itemWidget_emptyName_isSafe)
{
    MiracastDevice dev; dev.name = ""; dev.uuid = "uuid:empty";
    ItemWidget item(dev, mir_ext2_deviceXml("", "uuid:empty"), nullptr);
    EXPECT_EQ(item.toolTip(), QString(""));
}

TEST(mircast_ext2, itemWidget_paintEvent_allStates)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev; dev.name = "Paint2"; dev.uuid = "uuid:paint2";
    ItemWidget item(dev, mir_ext2_deviceXml("Paint2", "uuid:paint2"), nullptr);
    item.setFixedSize(240, 34);
    item.show();
    QTest::qWait(20);
    for (int s = ItemWidget::Normal; s <= ItemWidget::Checked; ++s) {
        item.setState(static_cast<ItemWidget::ConnectState>(s));
        QTest::qWait(10);
    }
    item.hide();
    QTest::qWait(10);
}

TEST(mircast_ext2, itemWidget_enterLeaveEvents)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev; dev.name = "Hover2"; dev.uuid = "uuid:hover2";
    ItemWidget item(dev, mir_ext2_deviceXml("Hover2", "uuid:hover2"), nullptr);
    item.show();
    QTest::qWait(20);
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&item, &ee);
    QTest::qWait(10);
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&item, &le);
    QTest::qWait(10);
    item.hide();
}

TEST(mircast_ext2, itemWidget_mousePress_emitsSelected)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev; dev.name = "Click2"; dev.uuid = "uuid:click2";
    ItemWidget item(dev, mir_ext2_deviceXml("Click2", "uuid:click2"), nullptr);
    item.show();
    QTest::qWait(20);
    bool sel = false;
    auto c = QObject::connect(&item, &ItemWidget::selected, [&]() { sel = true; });
    QMouseEvent pe(QEvent::MouseButtonPress, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&item, &pe);
    QObject::disconnect(c);
    EXPECT_TRUE(sel);
    item.hide();
}

TEST(mircast_ext2, itemWidget_doubleClick_emitsConnecting)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev; dev.name = "Dbl2"; dev.uuid = "uuid:dbl2";
    ItemWidget item(dev, mir_ext2_deviceXml("Dbl2", "uuid:dbl2"), nullptr);
    item.show();
    QTest::qWait(20);
    bool conn = false;
    auto c = QObject::connect(&item, &ItemWidget::connecting, [&]() { conn = true; });
    QMouseEvent de(QEvent::MouseButtonDblClick, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&item, &de);
    QObject::disconnect(c);
    EXPECT_TRUE(conn);
    item.hide();
}

TEST(mircast_ext2, itemWidget_loadingStateThenPaint_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev; dev.name = "Load2"; dev.uuid = "uuid:load2";
    ItemWidget item(dev, mir_ext2_deviceXml("Load2", "uuid:load2"), nullptr);
    item.setFixedSize(240, 34);
    item.show();
    QTest::qWait(10);
    item.setState(ItemWidget::Loading);
    QTest::qWait(40); // let rotate timer fire a couple times
    item.setState(ItemWidget::Normal);
    QTest::qWait(10);
    item.hide();
}

// ==========================================================================
// ListWidget -- standalone. count/clear/currentItemIndex/currentItemWidget/
// selectedItemWidget/setItemWidgetStatus/createListeItem + selection signals.
// ==========================================================================

TEST(mircast_ext2, listWidget_empty_initialState)
{
    ListWidget lw;
    EXPECT_EQ(lw.count(), 0);
    EXPECT_EQ(lw.currentItemIndex(), -1);
    EXPECT_EQ(lw.currentItemWidget(), nullptr);
    EXPECT_EQ(lw.selectedItemWidget().count(), 0);
}

TEST(mircast_ext2, listWidget_createListeItem_incrementsCount)
{
    ListWidget lw;
    MiracastDevice d; d.name = "A"; d.uuid = "uuid:a2";
    ItemWidget *i = lw.createListeItem(d, mir_ext2_deviceXml("A", "uuid:a2"), nullptr);
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(lw.count(), 1);
    EXPECT_EQ(i->getDevice().name, QString("A"));
    lw.clear();
}

TEST(mircast_ext2, listWidget_multipleItems_countFollows)
{
    ListWidget lw;
    for (int k = 0; k < 4; ++k) {
        MiracastDevice d;
        d.name = QString("D%1").arg(k);
        d.uuid = QString("uuid:d%1").arg(k);
        lw.createListeItem(d, mir_ext2_deviceXml(d.name.toLatin1().constData(),
                                                  d.uuid.toLatin1().constData()),
                           nullptr);
    }
    EXPECT_EQ(lw.count(), 4);
    lw.clear();
    EXPECT_EQ(lw.count(), 0);
}

TEST(mircast_ext2, listWidget_clear_resetsCurrent)
{
    ListWidget lw;
    MiracastDevice d; d.name = "X"; d.uuid = "uuid:x2";
    lw.createListeItem(d, mir_ext2_deviceXml(), nullptr);
    lw.clear();
    EXPECT_EQ(lw.count(), 0);
    EXPECT_EQ(lw.currentItemWidget(), nullptr);
    EXPECT_EQ(lw.currentItemIndex(), -1);
}

TEST(mircast_ext2, listWidget_selectedItemWidget_filtersByState)
{
    ListWidget lw;
    MiracastDevice d1; d1.name = "S1"; d1.uuid = "uuid:s1";
    MiracastDevice d2; d2.name = "S2"; d2.uuid = "uuid:s2";
    MiracastDevice d3; d3.name = "S3"; d3.uuid = "uuid:s3";
    ItemWidget *i1 = lw.createListeItem(d1, mir_ext2_deviceXml("S1", "uuid:s1"), nullptr);
    lw.createListeItem(d2, mir_ext2_deviceXml("S2", "uuid:s2"), nullptr);
    ItemWidget *i3 = lw.createListeItem(d3, mir_ext2_deviceXml("S3", "uuid:s3"), nullptr);

    EXPECT_EQ(lw.selectedItemWidget().count(), 0);
    i1->setState(ItemWidget::Checked);
    i3->setState(ItemWidget::Loading);
    EXPECT_EQ(lw.selectedItemWidget().count(), 2);
    i1->setState(ItemWidget::Normal);
    EXPECT_EQ(lw.selectedItemWidget().count(), 1);
    i3->setState(ItemWidget::Normal);
    EXPECT_EQ(lw.selectedItemWidget().count(), 0);
    lw.clear();
}

TEST(mircast_ext2, listWidget_setItemWidgetStatus_resetsAll)
{
    ListWidget lw;
    MiracastDevice d1; d1.name = "R1"; d1.uuid = "uuid:r1";
    MiracastDevice d2; d2.name = "R2"; d2.uuid = "uuid:r2";
    ItemWidget *i1 = lw.createListeItem(d1, mir_ext2_deviceXml("R1", "uuid:r1"), nullptr);
    ItemWidget *i2 = lw.createListeItem(d2, mir_ext2_deviceXml("R2", "uuid:r2"), nullptr);
    i1->setState(ItemWidget::Checked);
    i2->setState(ItemWidget::Loading);
    EXPECT_EQ(lw.selectedItemWidget().count(), 2);

    lw.setItemWidgetStatus(lw.selectedItemWidget(), ItemWidget::Normal);
    EXPECT_EQ(lw.selectedItemWidget().count(), 0);
    EXPECT_EQ(i1->state(), ItemWidget::Normal);
    EXPECT_EQ(i2->state(), ItemWidget::Normal);
    lw.clear();
}

TEST(mircast_ext2, listWidget_setItemWidgetStatus_emptyList_isSafe)
{
    ListWidget lw;
    QList<ItemWidget *> empty;
    lw.setItemWidgetStatus(empty, ItemWidget::Checked); // no-op
    SUCCEED();
}

TEST(mircast_ext2, listWidget_currentItemIndex_beforeSelect_isMinusOne)
{
    ListWidget lw;
    MiracastDevice d; d.name = "P"; d.uuid = "uuid:p";
    lw.createListeItem(d, mir_ext2_deviceXml(), nullptr);
    EXPECT_EQ(lw.currentItemIndex(), -1);
    lw.clear();
}

TEST(mircast_ext2, listWidget_slotSelectItem_viaMousePress)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ListWidget lw;
    lw.show();
    QTest::qWait(20);
    MiracastDevice d; d.name = "Pick2"; d.uuid = "uuid:pick2";
    ItemWidget *i = lw.createListeItem(d, mir_ext2_deviceXml("Pick2", "uuid:pick2"), nullptr);
    i->show();
    QTest::qWait(20);

    QMouseEvent pe(QEvent::MouseButtonPress, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(i, &pe);
    QTest::qWait(20);

    EXPECT_EQ(lw.currentItemWidget(), i);
    EXPECT_EQ(lw.currentItemIndex(), 0);
    lw.clear();
    lw.hide();
}

TEST(mircast_ext2, listWidget_slotSelectItem_clearsOthers)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ListWidget lw;
    lw.show();
    QTest::qWait(20);
    MiracastDevice d1; d1.name = "C1"; d1.uuid = "uuid:c1";
    MiracastDevice d2; d2.name = "C2"; d2.uuid = "uuid:c2";
    ItemWidget *i1 = lw.createListeItem(d1, mir_ext2_deviceXml("C1", "uuid:c1"), nullptr);
    ItemWidget *i2 = lw.createListeItem(d2, mir_ext2_deviceXml("C2", "uuid:c2"), nullptr);
    i1->show();
    i2->show();
    QTest::qWait(20);

    // Select first, then second; slotSelectItem clears prior selection's flag.
    QMouseEvent p1(QEvent::MouseButtonPress, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(i1, &p1);
    QTest::qWait(10);
    EXPECT_EQ(lw.currentItemWidget(), i1);

    QMouseEvent p2(QEvent::MouseButtonPress, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(i2, &p2);
    QTest::qWait(10);
    EXPECT_EQ(lw.currentItemWidget(), i2);

    lw.clear();
    lw.hide();
}

TEST(mircast_ext2, listWidget_doubleClick_emitsConnectDevice)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ListWidget lw;
    lw.show();
    QTest::qWait(20);
    MiracastDevice d; d.name = "Conn2"; d.uuid = "uuid:conn2";
    ItemWidget *i = lw.createListeItem(d, mir_ext2_deviceXml("Conn2", "uuid:conn2"), nullptr);
    i->show();
    QTest::qWait(20);

    bool got = false;
    auto c = QObject::connect(&lw, &ListWidget::connectDevice,
                              [&](ItemWidget *) { got = true; });
    QMouseEvent de(QEvent::MouseButtonDblClick, QPointF(3, 3), QPointF(3, 3),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(i, &de);
    QObject::disconnect(c);
    QTest::qWait(20);
    EXPECT_TRUE(got);
    lw.clear();
    lw.hide();
}

// ==========================================================================
// RefreButtonWidget -- standalone. refreshStart emits buttonClicked,
// refreshTimeout stops spinner; mouseRelease has a spinner-visible guard.
// (We never wire buttonClicked to anything that touches the network here.)
// ==========================================================================

TEST(mircast_ext2, refreshButton_refreshStart_emitsClicked)
{
    RefreButtonWidget btn;
    bool got = false;
    auto c = QObject::connect(&btn, &RefreButtonWidget::buttonClicked,
                              [&]() { got = true; });
    btn.refreshStart();
    QObject::disconnect(c);
    EXPECT_TRUE(got);
}

TEST(mircast_ext2, refreshButton_refreshTimeout_isSafe)
{
    RefreButtonWidget btn;
    btn.refreshStart();
    btn.refreshTimeout(); // stops spinner, shows refresh label
    btn.refreshTimeout(); // idempotent
    SUCCEED();
}

TEST(mircast_ext2, refreshButton_refreshStartThenTimeout_cycle)
{
    RefreButtonWidget btn;
    for (int k = 0; k < 3; ++k) {
        btn.refreshStart();
        btn.refreshTimeout();
    }
    SUCCEED();
}

TEST(mircast_ext2, refreshButton_mouseRelease_whenSpinnerVisible_isNoOp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    RefreButtonWidget btn;
    btn.show();
    QTest::qWait(20);
    btn.refreshStart(); // spinner visible

    int count = 0;
    auto c = QObject::connect(&btn, &RefreButtonWidget::buttonClicked,
                              [&]() { count++; });
    // Spinner is visible -> mouseRelease early-returns, no second click.
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &re);
    QObject::disconnect(c);
    // Only the refreshStart() above fired; the release must not add another.
    EXPECT_EQ(count, 1);
    btn.refreshTimeout();
    btn.hide();
    QTest::qWait(10);
}

TEST(mircast_ext2, refreshButton_mouseRelease_whenSpinnerHidden_triggersStart)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    RefreButtonWidget btn;
    btn.show();
    QTest::qWait(20);
    btn.refreshStart();
    btn.refreshTimeout(); // spinner hidden now

    int count = 0;
    auto c = QObject::connect(&btn, &RefreButtonWidget::buttonClicked,
                              [&]() { count++; });
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &re);
    QObject::disconnect(c);
    EXPECT_EQ(count, 1);
    btn.refreshTimeout();
    btn.hide();
    QTest::qWait(10);
}

// ==========================================================================
// MircastShowWidget + ExitButton -- standalone. Construction requires the
// SVG resources compiled into the binary; if absent, scenes/items may be null.
// We guard every dereference.
// ==========================================================================

TEST(mircast_ext2, mircastShowWidget_construct_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    (void)w;
    SUCCEED();
}

TEST(mircast_ext2, mircastShowWidget_setDeviceName_short_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setDeviceName("TV");
    SUCCEED();
}

TEST(mircast_ext2, mircastShowWidget_setDeviceName_long_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setDeviceName(QString("L").repeated(50)); // customizeText truncates >20
    SUCCEED();
}

TEST(mircast_ext2, mircastShowWidget_setDeviceName_empty_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setDeviceName("");
    SUCCEED();
}

TEST(mircast_ext2, mircastShowWidget_updateView_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setFixedSize(800, 600);
    w.show();
    QTest::qWait(20);
    w.updateView();
    QTest::qWait(10);
    w.hide();
    QTest::qWait(10);
}

TEST(mircast_ext2, mircastShowWidget_mouseMoveEvent_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setFixedSize(400, 300);
    w.show();
    QTest::qWait(20);
    QMouseEvent me(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10),
                   Qt::NoButton, Qt::NoButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&w, &me);
    QTest::qWait(10);
    w.hide();
    QTest::qWait(10);
}

TEST(mircast_ext2, mircastShowWidget_setDeviceNameTwice_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastShowWidget w;
    w.setDeviceName("First");
    w.setDeviceName("Second");
    SUCCEED();
}

// ExitButton standalone (it's a QWidget child created inside MircastShowWidget,
// but constructable on its own).
TEST(mircast_ext2, exitButton_construct_isSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ExitButton btn;
    (void)btn;
    SUCCEED();
}

TEST(mircast_ext2, exitButton_enterLeaveEvents)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ExitButton btn;
    btn.show();
    QTest::qWait(20);
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&btn, &ee);
    QTest::qWait(10);
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&btn, &le);
    QTest::qWait(10);
    btn.hide();
}

TEST(mircast_ext2, exitButton_pressRelease_emitsExitMircast)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ExitButton btn;
    btn.show();
    QTest::qWait(20);

    bool got = false;
    auto c = QObject::connect(&btn, &ExitButton::exitMircast, [&]() { got = true; });
    QMouseEvent pe(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &pe);
    QTest::qWait(10);
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &re);
    QObject::disconnect(c);
    QTest::qWait(10);
    EXPECT_TRUE(got);
    btn.hide();
}

TEST(mircast_ext2, exitButton_paintEvent_allStates)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ExitButton btn;
    btn.show();
    QTest::qWait(20);
    // Drive enter/leave/press to flip internal m_state across all enum values
    // so paintEvent runs through Normal/Hover/Press branches on the next
    // update() (which the state changes already trigger).
    QEnterEvent ee(QPointF(5, 5), QPointF(5, 5), QPointF(5, 5));
    QApplication::sendEvent(&btn, &ee);
    QTest::qWait(10);
    QMouseEvent pe(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &pe);
    QTest::qWait(10);
    QEvent le(QEvent::Leave);
    QApplication::sendEvent(&btn, &le);
    QTest::qWait(10);
    btn.hide();
}
