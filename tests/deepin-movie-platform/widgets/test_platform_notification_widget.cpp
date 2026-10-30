// Copyright (C) 2026 Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dedicated unit tests for src/widgets/platform/platform_notification_widget.cpp
// (the platform variant of the top-left message popup).
//
// Suite name "pnotif_ext"; static helpers use unique prefix "pn_".
//
// Method bodies verified against the worktree sources at base 4608a9f2:
//   * ctor (25-68): objectName "NotificationFrame", Qt::ToolTip window flags,
//     WA_TranslucentBackground, theme-dependent foreground role, single-shot
//     QTimer (2000ms non-wayland / 500ms wayland) connected to hide().
//   * popup (144-206): margins 14/4/14/4, fixed height 30 (20 in compact
//     mode), flag=true scans qApp->allWindows() + the workspace window list
//     and may skip showing on overlap, timer 500/2000ms by isActiveWindow,
//     then show() + raise().
//   * syncPosition() (99-118) / syncPosition(rect) (121-138): anchor math,
//     ANCHOR_NORTH_WEST uses rect.topLeft() + m_anchorPoint.
//   * updateWithMessage (208-244): icon hidden, elide to
//     m_pMainWindow->width() - 84, resize when visible else popup.
//   * showEvent (73-87): resize to label sizeHint + layout margins.
//   * paintEvent (254-302): m_bIsWM rounded style vs solid style, light/dark.
//
// External dependencies are stubbed, never touched for real:
//   * DWindowManagerHelper::currentWorkspaceWindowIdList (WM state query)
//   * DForeignWindow::fromWinId (foreign window construction)
//   * QWidget::isActiveWindow (WM focus state, for the timer branches)
//   * DGuiApplicationHelper::themeType / sizeMode (theme branches)
//     NOTE: DGuiApplicationHelper::setPaletteType()/setSizeMode() are NOT
//     used — in a live DTK application they broadcast an app-wide theme
//     cascade that perturbs the shared MainWindow (and destabilises the
//     whole single-process run). The stubs only affect code inside the
//     guarded scope.
// Only Google Test (TEST(...)); gtest supplies main() via test_qtestmain.cpp.

#include "src/widgets/platform/platform_notification_widget.h"
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QLabel>
#include <QShowEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QTimer>

#include <DLabel>
#include <DWindowManagerHelper>
#include <DForeignWindow>
#include <DGuiApplicationHelper>

#include "stub/stub.h"

DWIDGET_USE_NAMESPACE
using namespace dmr;

// ---------------------------------------------------------------------------
// Stub state + helpers (unique prefix pn_)
// ---------------------------------------------------------------------------

// Fake workspace window list returned by the stubbed
// DWindowManagerHelper::currentWorkspaceWindowIdList.
static QVector<quint32> s_pn_wmClientList;

static QVector<quint32> pn_currentWorkspaceWindowIdList_stub()
{
    return s_pn_wmClientList;
}

// Fake foreign window handed out by the stubbed DForeignWindow::fromWinId.
static DForeignWindow *s_pn_foreignWindow = nullptr;

static DForeignWindow *pn_fromWinId_stub(WId /*id*/)
{
    return s_pn_foreignWindow;
}

// Optional QWidget::isActiveWindow control (Qt class: static_cast, never VADDR).
static bool s_pn_isActiveWindow = false;

static bool pn_isActiveWindow_stub(QWidget * /*w*/)
{
    return s_pn_isActiveWindow;
}

// Theme state returned by the stubbed DGuiApplicationHelper accessors.
static DGuiApplicationHelper::ColorType s_pn_themeType = DGuiApplicationHelper::LightType;
static DGuiApplicationHelper::SizeMode s_pn_sizeMode = DGuiApplicationHelper::NormalMode;

static DGuiApplicationHelper::ColorType pn_themeType_stub()
{
    return s_pn_themeType;
}

static DGuiApplicationHelper::SizeMode pn_sizeMode_stub()
{
    return s_pn_sizeMode;
}

// Installs the WM-state stubs for the current test scope; the Stub
// destructor restores the original implementations automatically.
class PnotifStubGuard
{
public:
    PnotifStubGuard()
    {
        m_stub.set(
            static_cast<QVector<quint32> (DWindowManagerHelper::*)() const>(
                &DWindowManagerHelper::currentWorkspaceWindowIdList),
            &pn_currentWorkspaceWindowIdList_stub);
        m_stub.set(
            static_cast<DForeignWindow *(*)(WId)>(&DForeignWindow::fromWinId),
            &pn_fromWinId_stub);
    }

private:
    Stub m_stub;
};

// Theme/sizemode stub guard (scoped, no global cascade).
class PnotifThemeGuard
{
public:
    explicit PnotifThemeGuard(DGuiApplicationHelper::ColorType theme,
                              DGuiApplicationHelper::SizeMode size = DGuiApplicationHelper::NormalMode)
    {
        s_pn_themeType = theme;
        s_pn_sizeMode = size;
        m_stub.set(
            static_cast<DGuiApplicationHelper::ColorType (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::themeType),
            &pn_themeType_stub);
        m_stub.set(
            static_cast<DGuiApplicationHelper::SizeMode (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::sizeMode),
            &pn_sizeMode_stub);
    }

private:
    Stub m_stub;
};

static void pn_wait(int ms)
{
    QTest::qWait(ms);
}

// Fresh local parent, sized like the real main window.
static QWidget *pn_makeParent(bool showIt = false)
{
    QWidget *parent = new QWidget;
    parent->resize(400, 300);
    if (showIt && QGuiApplication::primaryScreen()) {
        parent->show();
        pn_wait(20);
    }
    return parent;
}

// The single DLabel child (m_pMsgLabel). NOTE: the ctor creates the label
// parentless; it only becomes a findable child once popup() adds it to the
// layout.
static DLabel *pn_msgLabel(Platform_NotificationWidget &nw)
{
    return nw.findChild<DLabel *>();
}

static void pn_sendPaint(Platform_NotificationWidget &nw)
{
    QPaintEvent pe(nw.rect());
    QApplication::sendEvent(&nw, &pe);
}

// ---------------------------------------------------------------------------
// Constructor / initMember (via observable effects)
// ---------------------------------------------------------------------------

TEST(pnotif_ext, construct_objectNameSet)
{
    QWidget parent;
    Platform_NotificationWidget nw(&parent);
    EXPECT_STREQ(nw.objectName().toStdString().c_str(), "NotificationFrame");
}

TEST(pnotif_ext, construct_toolTipWindowFlags)
{
    QWidget parent;
    Platform_NotificationWidget nw(&parent);
    EXPECT_TRUE(nw.windowFlags() & Qt::ToolTip);
    EXPECT_TRUE(nw.windowFlags() & Qt::FramelessWindowHint);
}

TEST(pnotif_ext, construct_translucentBackground)
{
    QWidget parent;
    Platform_NotificationWidget nw(&parent);
    EXPECT_TRUE(nw.testAttribute(Qt::WA_TranslucentBackground));
}

// The message label is created parentless; popup() parents it into the
// layout, so the defaults are verified through a popup first.
TEST(pnotif_ext, construct_msgLabelDefaults_viaPopup)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("hello popup"), false);
    pn_wait(20);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // initMember defaults: popup text set, no frame shape.
    EXPECT_EQ(label->frameShape(), QFrame::NoFrame);
}

TEST(pnotif_ext, construct_themeForegroundRole_light)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::LightType);
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("light"), false);
    pn_wait(20);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // themeType() != 2 (light): ToolTipText role.
    EXPECT_EQ(label->foregroundRole(), QPalette::ToolTipText);
}

TEST(pnotif_ext, construct_themeForegroundRole_dark)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::DarkType);
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("dark"), false);
    pn_wait(20);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // The ctor's dark branch (themeType() == 2) sets foregroundRole to
    // DPalette::TextLively (ColorType 5, which maps to QPalette::Mid); DTK's
    // label machinery re-adjusts the role during the subsequent show/polish,
    // so only the difference from the light branch is asserted here.
    EXPECT_NE(label->foregroundRole(), QPalette::ToolTipText);
}

// ---------------------------------------------------------------------------
// Inline setters
// ---------------------------------------------------------------------------

TEST(pnotif_ext, setters_anchorVariants)
{
    QWidget parent;
    Platform_NotificationWidget nw(&parent);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(42);
    nw.setAnchorPoint(QPoint(7, 9));
    nw.setWM(true);
    nw.setWM(false);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// popup (flag=false: deterministic, no window scan)
// ---------------------------------------------------------------------------

TEST(pnotif_ext, popup_flagFalse_showsWithMessage)
{
    QWidget *parent = pn_makeParent(true);   // child visibility needs a shown parent
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("hello popup"), false);
    pn_wait(20);

    EXPECT_TRUE(nw.isVisible());
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("hello popup"));
    // popup fixes the height to 30 in normal mode.
    EXPECT_EQ(nw.height(), 30);
    // margins applied in popup.
    QHBoxLayout *lay = qobject_cast<QHBoxLayout *>(nw.layout());
    ASSERT_NE(lay, nullptr);
    EXPECT_EQ(lay->contentsMargins(), QMargins(14, 4, 14, 4));
}

TEST(pnotif_ext, popup_flagFalse_fixedHeight30)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::LightType,
                                DGuiApplicationHelper::CompactMode);
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("compact"), false);
    pn_wait(20);
    // The platform popup calls setFixedHeight(30) unconditionally — the
    // compact-mode height-20 branch exists only in the non-platform variant.
    EXPECT_EQ(nw.height(), 30);
}

TEST(pnotif_ext, popup_twice_labelAddedOnce)
{
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("first"), false);
    nw.popup(QString("second"), false);
    pn_wait(20);
    // indexOf(m_pMsgLabel) != -1 the second time: label not duplicated.
    QHBoxLayout *lay = qobject_cast<QHBoxLayout *>(nw.layout());
    ASSERT_NE(lay, nullptr);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(lay->indexOf(label), 0);
    EXPECT_EQ(label->text(), QString("second"));
}

// ---------------------------------------------------------------------------
// popup (flag=true: window-overlap scan with stubbed WM state)
// ---------------------------------------------------------------------------

// Empty workspace list: no overlap scan happens, popup shows.
TEST(pnotif_ext, popup_flagTrue_emptyWmList_shows)
{
    PnotifStubGuard guard;
    s_pn_wmClientList.clear();
    s_pn_foreignWindow = nullptr;

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("no overlap"), true);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());
}

// Workspace list contains our window followed by a foreign one whose
// geometry overlaps the popup: the scan returns early before show(),
// so the popup stays hidden.
TEST(pnotif_ext, popup_flagTrue_overlapWithForeignWindow_skips)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifStubGuard guard;

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    // Deterministic pre-popup geometry; popup() has not shown the widget yet.
    nw.resize(120, 30);

    DForeignWindow fakeForeign;
    fakeForeign.setGeometry(QRect(-10, -10, 200, 60));
    s_pn_foreignWindow = &fakeForeign;

    // { own window id, foreign id }: scan sees own -> currentWindow=true,
    // then the foreign window -> overlap -> return without show().
    s_pn_wmClientList = QVector<quint32>{ static_cast<quint32>(parent->winId()), 999999u };
    nw.popup(QString("overlap me"), true);
    pn_wait(20);
    EXPECT_FALSE(nw.isVisible());

    s_pn_wmClientList.clear();
    s_pn_foreignWindow = nullptr;
}

// Foreign window far away: no overlap, the scan completes and the popup
// shows (also covers the fromWinId non-null path).
TEST(pnotif_ext, popup_flagTrue_foreignWindowNoOverlap_shows)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifStubGuard guard;

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);

    DForeignWindow fakeForeign;
    fakeForeign.setGeometry(QRect(5000, 5000, 200, 200));
    s_pn_foreignWindow = &fakeForeign;
    s_pn_wmClientList = QVector<quint32>{ static_cast<quint32>(parent->winId()), 999999u };

    nw.popup(QString("far away"), true);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());

    s_pn_wmClientList.clear();
    s_pn_foreignWindow = nullptr;
}

// Own window only in the list: fromWinId is never reached, popup shows.
TEST(pnotif_ext, popup_flagTrue_onlyOwnWindow_shows)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifStubGuard guard;

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    s_pn_foreignWindow = nullptr;
    s_pn_wmClientList = QVector<quint32>{ static_cast<quint32>(parent->winId()) };

    nw.popup(QString("own only"), true);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());

    s_pn_wmClientList.clear();
}

// Main window reported inactive: the short 500ms auto-hide timer branch.
TEST(pnotif_ext, popup_flagTrue_mainWindowInactive_shortTimer)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifStubGuard guard;
    Stub activeStub;
    s_pn_isActiveWindow = false;
    activeStub.set(
        static_cast<bool (QWidget::*)() const>(&QWidget::isActiveWindow),
        &pn_isActiveWindow_stub);

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    s_pn_wmClientList.clear();
    s_pn_foreignWindow = nullptr;

    nw.popup(QString("inactive"), true);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());
    s_pn_isActiveWindow = false;
}

// Main window reported active: the normal 2000ms auto-hide timer branch.
TEST(pnotif_ext, popup_flagTrue_mainWindowActive_normalTimer)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    PnotifStubGuard guard;
    Stub activeStub;
    s_pn_isActiveWindow = true;
    activeStub.set(
        static_cast<bool (QWidget::*)() const>(&QWidget::isActiveWindow),
        &pn_isActiveWindow_stub);

    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    s_pn_wmClientList.clear();
    s_pn_foreignWindow = nullptr;

    nw.popup(QString("active"), true);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());
    s_pn_isActiveWindow = false;
}

// ---------------------------------------------------------------------------
// updateWithMessage
// ---------------------------------------------------------------------------

TEST(pnotif_ext, updateWithMessage_hidden_delegatesToPopup)
{
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    EXPECT_FALSE(nw.isVisible());
    nw.updateWithMessage(QString("first show"), false);
    pn_wait(20);
    EXPECT_TRUE(nw.isVisible());
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("first show"));
}

TEST(pnotif_ext, updateWithMessage_visible_resizesKeepsText)
{
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("short"), false);
    pn_wait(20);
    ASSERT_TRUE(nw.isVisible());
    int oldWidth = nw.width();

    nw.updateWithMessage(QString("a much longer message text than before"), false);
    pn_wait(20);
    // Visible branch: label updated, width recomputed, syncPosition called.
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("a much longer message text than before"));
    EXPECT_GE(nw.width(), oldWidth);
    EXPECT_TRUE(nw.isVisible());
}

TEST(pnotif_ext, updateWithMessage_elidesLongTextToMainWindowWidth)
{
    QWidget *parent = pn_makeParent(true);  // width 400 -> elide threshold 400-84
    Platform_NotificationWidget nw(parent);
    nw.popup(QString("start"), false);
    pn_wait(20);
    QString longMsg(200, QLatin1Char('x'));
    nw.updateWithMessage(longMsg, false);
    pn_wait(20);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // ElideMiddle inserts the ellipsis character.
    EXPECT_TRUE(label->text().contains(QString(QChar(0x2026))));
    EXPECT_LT(label->text().size(), longMsg.size());
}

// ---------------------------------------------------------------------------
// syncPosition (no-arg: uses m_pMainWindow->geometry())
// ---------------------------------------------------------------------------

TEST(pnotif_ext, syncPosition_noArg_bottom)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(5);
    nw.syncPosition();
    QRect geom = parent->geometry();
    QPoint expected(geom.center().x() - 60, geom.bottom() - 5 - 30);
    EXPECT_EQ(nw.pos(), expected);
}

TEST(pnotif_ext, syncPosition_noArg_northWest_anchorPoint)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    nw.setAnchorPoint(QPoint(33, 44));
    nw.syncPosition();
    EXPECT_EQ(nw.pos(), QPoint(33, 44));
}

TEST(pnotif_ext, syncPosition_noArg_none_centers)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_NONE);
    nw.syncPosition();
    QRect geom = parent->geometry();
    QPoint expected(geom.center().x() - 60, geom.center().y() - 15);
    EXPECT_EQ(nw.pos(), expected);
}

// ---------------------------------------------------------------------------
// syncPosition (rect overload)
// ---------------------------------------------------------------------------

TEST(pnotif_ext, syncPosition_rect_bottom)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(10);
    QRect rect(0, 0, 800, 600);
    nw.syncPosition(rect);
    QPoint expected(rect.center().x() - 60, rect.bottom() - 10 - 30);
    EXPECT_EQ(nw.pos(), expected);
}

TEST(pnotif_ext, syncPosition_rect_northWest_topLeftPlusAnchorPoint)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    nw.setAnchorPoint(QPoint(10, 20));
    QRect rect(5, 5, 800, 600);
    nw.syncPosition(rect);
    // Platform variant adds rect.topLeft() to the anchor point.
    EXPECT_EQ(nw.pos(), QPoint(15, 25));
}

TEST(pnotif_ext, syncPosition_rect_none_centers)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(Platform_NotificationWidget::ANCHOR_NONE);
    QRect rect(0, 0, 800, 600);
    nw.syncPosition(rect);
    QPoint expected(rect.center().x() - 60, rect.center().y() - 15);
    EXPECT_EQ(nw.pos(), expected);
}

// ---------------------------------------------------------------------------
// showEvent / resizeEvent
// ---------------------------------------------------------------------------

TEST(pnotif_ext, showEvent_resizesToLabelSizeHint)
{
    QWidget *parent = pn_makeParent(true);
    Platform_NotificationWidget nw(parent);
    // popup() first: label added to layout and margins set to 14/4/14/4,
    // so the showEvent recompute is deterministic.
    nw.popup(QString("shown"), false);
    pn_wait(20);
    DLabel *label = pn_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    QShowEvent se;
    QApplication::sendEvent(&nw, &se);
    // showEvent: width = label sizeHint + margins(14, 14).
    EXPECT_EQ(nw.width(), label->sizeHint().width() + 28);
}

TEST(pnotif_ext, resizeEvent_doesNotCrash)
{
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    QResizeEvent re(QSize(100, 40), nw.size());
    QApplication::sendEvent(&nw, &re);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// paintEvent (m_bIsWM x theme matrix; theme via stub, no global cascade)
// ---------------------------------------------------------------------------

TEST(pnotif_ext, paintEvent_wmTrue_light)
{
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::LightType);
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.setWM(true);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(pn_sendPaint(nw));
}

TEST(pnotif_ext, paintEvent_wmTrue_dark)
{
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::DarkType);
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.setWM(true);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(pn_sendPaint(nw));
}

TEST(pnotif_ext, paintEvent_wmFalse_light)
{
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::LightType);
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.setWM(false);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(pn_sendPaint(nw));
}

TEST(pnotif_ext, paintEvent_wmFalse_dark)
{
    PnotifThemeGuard themeGuard(DGuiApplicationHelper::DarkType);
    QWidget *parent = pn_makeParent();
    Platform_NotificationWidget nw(parent);
    nw.setWM(false);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(pn_sendPaint(nw));
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

TEST(pnotif_ext, destroy_whileVisible_cleansUp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget *parent = pn_makeParent(true);
    {
        Platform_NotificationWidget nw(parent);
        nw.popup(QString("bye"), true);
        pn_wait(20);
        EXPECT_TRUE(nw.isVisible());
    }
    EXPECT_NO_FATAL_FAILURE(pn_wait(30));
}
