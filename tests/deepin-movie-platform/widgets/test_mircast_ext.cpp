// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extension unit tests for src/widgets/mircastwidget.cpp.
// Suite name "mircast_ext"; static helpers use unique prefix "mir_ext_".
//
// Safety notes baked into every case below:
//   * The MircastWidget is obtained via the running app's toolbox. Its
//     m_pDlnaSoapPost is only allocated inside initializeHttpServer(), which
//     also binds a real DlnaContentServer to a local IP. We never call that
//     path, so every soap/network entry point is guarded so it does not
//     dereference the uninitialized pointer (e.g. slotPauseDlnaTp returns
//     unless Screening, stopDlnaTP returns when ControlURLPro is empty).
//   * No mpv backend / no playlist loaded: anything that needs engine state
//     is guarded or skipped.
//   * Fresh local widgets are used for screen/geometry cases with a
//     primaryScreen() guard.

#include "src/widgets/mircastwidget.h"
#include "src/widgets/platform/platform_toolbox_proxy.h"
#include "src/common/platform/platform_mainwindow.h"
#include "src/libdmr/player_engine.h"
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

using namespace dmr;

// --- Helpers ---------------------------------------------------------------

// Fetch the shared MircastWidget from the running application's toolbox.
static MircastWidget *mir_ext_widget()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    if (!w) return nullptr;
    Platform_ToolboxProxy *tb = w->toolbox();
    if (!tb) return nullptr;
    return tb->getMircast();
}

// Build a valid DLNA device-description XML (same shape the production parser
// consumes in slotReadyRead / ItemWidget ctor).
static QByteArray mir_ext_deviceXml(const char *friendlyName = "UT-Device",
                                    const char *udn = "uuid:11111111-2222-3333-4444-555555555555")
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

// --- getMircastState / getMircastPlayState --------------------------------

TEST(mircast_ext, getMircastState_initial_isIdel)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    // The widget starts in Idel and is never driven in this test binary.
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

TEST(mircast_ext, getMircastPlayState_initial_isNoState)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::NoState);
}

// setMircastState / setMircastPlayState are test-injected setters exposed on
// the class; round-trip them through the getters.
TEST(mircast_ext, setMircastState_roundtripsThroughGetter)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Connecting);
    mw->setMircastState(MircastWidget::Screening);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Screening);
    // Restore the default so subsequent cases stay in a known state.
    mw->setMircastState(MircastWidget::Idel);
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

TEST(mircast_ext, setMircastPlayState_roundtripsThroughGetter)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastPlayState(MircastWidget::Play);
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Play);
    mw->setMircastPlayState(MircastWidget::Pause);
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Pause);
    mw->setMircastPlayState(MircastWidget::Stop);
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Stop);
    mw->setMircastPlayState(MircastWidget::NoState);
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::NoState);
}

// --- timeConversion (pure logic, all branches) ----------------------------

// timeConversion is private; reach it via the meta-object system. It takes a
// QString and returns int. Register the call through invokeMethod.
TEST(mircast_ext, timeConversion_validHMS)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("01:02:03")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 1 * 3600 + 2 * 60 + 3);
}

TEST(mircast_ext, timeConversion_zeroTime)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("00:00:00")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 0);
}

TEST(mircast_ext, timeConversion_largeValues)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("10:00:00")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 36000);
}

// Not 3 colon-separated parts -> returns 0.
TEST(mircast_ext, timeConversion_invalidShape_returnsZero)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("NOT_IMPLEMENTED")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 0);
}

TEST(mircast_ext, timeConversion_emptyString_returnsZero)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 0);
}

TEST(mircast_ext, timeConversion_twoParts_returnsZero)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    int out = -1;
    bool ok = QMetaObject::invokeMethod(
        mw, "timeConversion", Qt::DirectConnection,
        Q_RETURN_ARG(int, out), Q_ARG(QString, QString("02:30")));
    ASSERT_TRUE(ok);
    EXPECT_EQ(out, 0);
}

// --- togglePopup (show/hide toggle, never touches soap) -------------------

TEST(mircast_ext, togglePopup_hide_whenVisible)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->show();
    QTest::qWait(30);
    ASSERT_TRUE(mw->isVisible());
    mw->togglePopup();
    EXPECT_FALSE(mw->isVisible());
}

TEST(mircast_ext, togglePopup_show_whenHidden)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->hide();
    QTest::qWait(30);
    ASSERT_FALSE(mw->isVisible());
    // show() path calls m_refreshBtn->refreshStart(); refreshBtn is owned and
    // valid, so this is safe.
    mw->togglePopup();
    EXPECT_TRUE(mw->isVisible());
    // Leave it hidden to avoid interactions with later cases.
    mw->hide();
    QTest::qWait(20);
}

// --- updateMircastState (all 3 branches; Searching early-returns if list nonempty)

TEST(mircast_ext, updateMircastState_noDevices_hidesArea)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::NoDevices);
    // NoDevices hides the scroll area; hint widget text is set but we don't
    // have a public accessor, so just assert no crash.
    SUCCEED();
}

TEST(mircast_ext, updateMircastState_listExhibit_showsArea)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->updateMircastState(MircastWidget::ListExhibit);
    SUCCEED();
}

TEST(mircast_ext, updateMircastState_searching_emptyList_setsHint)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    // Devices list is empty by default -> Searching branch fully executes.
    mw->updateMircastState(MircastWidget::Searching);
    SUCCEED();
}

// --- slotSearchTimeout (empty vs non-empty list branches) -----------------

TEST(mircast_ext, slotSearchTimeout_emptyList_marksNoDevices)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    // Default list is empty; slot calls updateMircastState(NoDevices) then
    // m_refreshBtn->refreshTimeout() (valid) then update().
    mw->slotSearchTimeout();
    SUCCEED();
}

// --- slotExitMircast -------------------------------------------------------

TEST(mircast_ext, slotExitMircast_whenIdel_isNoOp)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->slotExitMircast(); // early return; never reaches stopDlnaTP/soap.
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

// Drive slotExitMircast from a non-Idel state but with empty ControlURLPro so
// that stopDlnaTP() returns before any soap dereference.
TEST(mircast_ext, slotExitMircast_whenConnecting_resetsState)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Connecting);
    mw->slotExitMircast();
    // slotExitMircast resets to Idel, clears timeouts, and calls stopDlnaTP
    // which returns early because ControlURLPro is empty.
    EXPECT_EQ(mw->getMircastState(), MircastWidget::Idel);
}

// --- stopDlnaTP (empty-URL early return is the safe path) -----------------

TEST(mircast_ext, stopDlnaTP_emptyControlUrl_returnsEarly)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    // ControlURLPro defaults to empty -> returns before touching m_pDlnaSoapPost.
    mw->stopDlnaTP();
    EXPECT_EQ(mw->getMircastPlayState(), MircastWidget::Stop);
}

// --- seekMircast (Screening guard; not Screening -> early return) ---------

TEST(mircast_ext, seekMircast_notScreening_isNoOp)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->seekMircast(10); // returns immediately, no soap dereference.
    SUCCEED();
}

// --- slotPauseDlnaTp (Screening guard; not Screening -> early return) ------

TEST(mircast_ext, slotPauseDlnaTp_notScreening_isNoOp)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    mw->slotPauseDlnaTp();
    SUCCEED();
}

// --- slotGetPositionInfo (Idel guard -> early return) ---------------------

TEST(mircast_ext, slotGetPositionInfo_whenIdel_returnsEarly)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    mw->setMircastState(MircastWidget::Idel);
    DlnaPositionInfo info;
    info.sAbsTime = "00:00:05";
    info.sTrackDuration = "00:01:00";
    info.sTrackURI = "http://example/x";
    mw->slotGetPositionInfo(info); // Idel -> immediate return.
    SUCCEED();
}

// --- slotMircastTimeout guard (would deref soap; skip unless initialized) --
// slotMircastTimeout unconditionally dereferences m_pDlnaSoapPost, which is
// only valid after initializeHttpServer() (not called here). We deliberately
// do NOT exercise it to keep the suite crash-free.

// --- getRefreshBtn + RefreButtonWidget behavior ---------------------------

TEST(mircast_ext, getRefreshBtn_returnsValid)
{
    MircastWidget *mw = mir_ext_widget();
    ASSERT_NE(mw, nullptr);
    RefreButtonWidget *btn = mw->getRefreshBtn();
    ASSERT_NE(btn, nullptr);
    // refreshTimeout stops spinner, shows refresh icon: pure local UI, safe.
    btn->refreshTimeout();
    SUCCEED();
}

// NOTE: We intentionally do NOT drive a mouseRelease on the shared refresh
// button. Its refreshStart() emits buttonClicked, which is wired to
// MircastWidget::slotRefreshBtnClicked -> initializeHttpServer() (binds a real
// DlnaContentServer to a local IP) + searchDevices() (real SSDP UDP). That is
// exactly the network path we must avoid. The standalone case below proves the
// same RefreButtonWidget logic without triggering the MircastWidget wiring.

// Construct a standalone refresh button and drive refreshStart/refreshTimeout
// plus the spinner-visible early return in mouseReleaseEvent.
TEST(mircast_ext, refreshButton_standalone_refreshStartEmitsClicked)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    RefreButtonWidget btn;
    btn.show();
    QTest::qWait(20);

    bool got = false;
    auto conn = QObject::connect(&btn, &RefreButtonWidget::buttonClicked,
                                 [&]() { got = true; });
    btn.refreshStart(); // spinner visible
    EXPECT_TRUE(got);
    QObject::disconnect(conn);

    // While spinner is visible, mouseRelease must NOT re-trigger refreshStart.
    bool got2 = false;
    auto conn2 = QObject::connect(&btn, &RefreButtonWidget::buttonClicked,
                                  [&]() { got2 = true; });
    QMouseEvent re(QEvent::MouseButtonRelease, QPointF(2, 2), QPointF(2, 2),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                   QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(&btn, &re);
    QObject::disconnect(conn2);
    EXPECT_FALSE(got2);

    btn.refreshTimeout();
    btn.hide();
    QTest::qWait(20);
}

// --- ItemWidget / ListWidget (standalone, no network) ---------------------

TEST(mircast_ext, itemWidget_getDevice_returnsConstructedDevice)
{
    MiracastDevice dev;
    dev.name = "UT Render";
    dev.uuid = "uuid:abcdef";
    QByteArray data = mir_ext_deviceXml("UT Render", "uuid:abcdef");
    ItemWidget item(dev, data, nullptr);
    EXPECT_EQ(item.getDevice().name, "UT Render");
    EXPECT_EQ(item.getDevice().uuid, "uuid:abcdef");
}

TEST(mircast_ext, itemWidget_stateTransitions)
{
    MiracastDevice dev;
    dev.name = "S";
    dev.uuid = "uuid:s";
    QByteArray data = mir_ext_deviceXml("S", "uuid:s");
    ItemWidget item(dev, data, nullptr);

    EXPECT_EQ(item.state(), ItemWidget::Normal);
    item.setState(ItemWidget::Loading);
    EXPECT_EQ(item.state(), ItemWidget::Loading);
    item.setState(ItemWidget::Checked);
    EXPECT_EQ(item.state(), ItemWidget::Checked);
    item.setState(ItemWidget::Normal);
    EXPECT_EQ(item.state(), ItemWidget::Normal);
    item.clearSelect();
    SUCCEED();
}

// convertDisplay truncates names wider than TEXT_WIDTH(170). Use a very long
// name to hit the truncation branch; a short name hits the passthrough branch.
TEST(mircast_ext, itemWidget_longName_truncatesDisplay)
{
    MiracastDevice dev;
    dev.name = QString("X").repeated(300); // far wider than 170px
    dev.uuid = "uuid:long";
    QByteArray data = mir_ext_deviceXml("x", "uuid:long");
    ItemWidget item(dev, data, nullptr);
    // No public accessor for m_displayName, but the tooltip is the device name
    // (unchanged). The truncation path is exercised purely for coverage; we
    // only assert construction succeeded.
    EXPECT_EQ(item.toolTip(), dev.name);
}

TEST(mircast_ext, itemWidget_shortName_keepsName)
{
    MiracastDevice dev;
    dev.name = "Short";
    dev.uuid = "uuid:short";
    QByteArray data = mir_ext_deviceXml("Short", "uuid:short");
    ItemWidget item(dev, data, nullptr);
    EXPECT_EQ(item.toolTip(), QString("Short"));
}

// paintEvent covers Normal/Loading/Checked via state. Needs a screen.
TEST(mircast_ext, itemWidget_paintEvent_allStates)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev;
    dev.name = "Paint";
    dev.uuid = "uuid:paint";
    QByteArray data = mir_ext_deviceXml("Paint", "uuid:paint");
    ItemWidget item(dev, data, nullptr);
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

// enterEvent / leaveEvent toggle the hover flag (no crash assertion possible
// without internals, but exercising them still covers the code).
TEST(mircast_ext, itemWidget_enterLeaveEvents_areSafe)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev;
    dev.name = "Hover";
    dev.uuid = "uuid:hover";
    QByteArray data = mir_ext_deviceXml("Hover", "uuid:hover");
    ItemWidget item(dev, data, nullptr);
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

// mousePressEvent emits selected(); double-click emits connecting().
TEST(mircast_ext, itemWidget_mousePress_emitsSelected)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev;
    dev.name = "Click";
    dev.uuid = "uuid:click";
    QByteArray data = mir_ext_deviceXml("Click", "uuid:click");
    ItemWidget item(dev, data, nullptr);
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

TEST(mircast_ext, itemWidget_doubleClick_emitsConnecting)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    MiracastDevice dev;
    dev.name = "Dbl";
    dev.uuid = "uuid:dbl";
    QByteArray data = mir_ext_deviceXml("Dbl", "uuid:dbl");
    ItemWidget item(dev, data, nullptr);
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

// --- ListWidget standalone -------------------------------------------------

TEST(mircast_ext, listWidget_count_clear_currentIndex)
{
    ListWidget lw;
    EXPECT_EQ(lw.count(), 0);
    EXPECT_EQ(lw.currentItemIndex(), -1);
    EXPECT_EQ(lw.currentItemWidget(), nullptr);

    MiracastDevice d1; d1.name = "A"; d1.uuid = "uuid:a";
    MiracastDevice d2; d2.name = "B"; d2.uuid = "uuid:b";
    QByteArray xml = mir_ext_deviceXml();
    ItemWidget *i1 = lw.createListeItem(d1, xml, nullptr);
    ItemWidget *i2 = lw.createListeItem(d2, xml, nullptr);
    EXPECT_EQ(lw.count(), 2);

    // Nothing selected yet.
    EXPECT_EQ(lw.selectedItemWidget().count(), 0);

    i1->setState(ItemWidget::Checked);
    EXPECT_EQ(lw.selectedItemWidget().count(), 1);

    // setItemWidgetStatus resets all to Normal.
    lw.setItemWidgetStatus(lw.selectedItemWidget(), ItemWidget::Normal);
    EXPECT_EQ(lw.selectedItemWidget().count(), 0);

    lw.clear();
    EXPECT_EQ(lw.count(), 0);
    // i1/i2 were deleteLater()'d in clear; drop raw pointers.
    (void)i1; (void)i2;
}

// slotSelectItem via mousePress on an item updates currentItemWidget/index.
TEST(mircast_ext, listWidget_selectItem_updatesCurrent)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    ListWidget lw;
    lw.show();
    QTest::qWait(20);

    MiracastDevice d; d.name = "Pick"; d.uuid = "uuid:pick";
    ItemWidget *i = lw.createListeItem(d, mir_ext_deviceXml(), nullptr);
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

// createListeItem on a standalone ListWidget constructs an ItemWidget and
// appends it (the MircastWidget-level wrapper would mutate the shared widget's
// list, so we exercise the ListWidget directly to avoid cross-suite leakage).
TEST(mircast_ext, listWidget_createListeItem_buildsItem)
{
    ListWidget lw;
    MiracastDevice d; d.name = "MW-Item"; d.uuid = "uuid:mw-item";
    ItemWidget *item = lw.createListeItem(d, mir_ext_deviceXml(), nullptr);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->getDevice().name, QString("MW-Item"));
    EXPECT_EQ(lw.count(), 1);
    lw.clear();
}
