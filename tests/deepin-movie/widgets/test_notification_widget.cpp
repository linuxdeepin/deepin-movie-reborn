// Copyright (C) 2026 Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dedicated unit tests for src/widgets/notification_widget.cpp (the
// non-platform variant of the top-left message popup).
//
// Suite name "notif_ext"; static helpers use unique prefix "nwx_".
//
// Method bodies verified against the worktree sources at base 4608a9f2:
//   * ctor (24-67): objectName "NotificationFrame", theme-dependent
//     foreground role, single-shot QTimer (2000ms non-wayland / 500ms
//     wayland) connected to hide().
//   * popup (154-178): margins 14/4/14/4, fixed height 30 (20 in compact
//     mode), flag=true starts the auto-hide timer, then show() + raise().
//   * syncPosition() (99-121) / syncPosition(rect) (126-148): anchor math,
//     ANCHOR_NORTH_WEST moves to m_anchorPoint directly (no rect offset,
//     unlike the platform variant).
//   * updateWithMessage (184-214): elide to
//     m_pMainWindow->width() - 12 - 12 - 60, resize when visible else popup.
//   * showEvent (72-84): resize to label sizeHint + layout margins.
//   * paintEvent (254-265): light/dark background styles.
//
// No stubs required for popup(): with flag=false the call never leaves the
// widget; with flag=true it only starts an internal QTimer. Tests never
// touch the shared application MainWindow — a fresh local QWidget plays the
// role of m_pMainWindow. Theme/mode branches use scoped stubs:
//   * DGuiApplicationHelper::themeType / sizeMode
//     NOTE: setPaletteType()/setSizeMode() are NOT used — in a live DTK
//     application they broadcast an app-wide theme cascade that perturbs
//     the shared MainWindow (and destabilises the whole single-process run).
// Only Google Test (TEST(...)); gtest supplies main() via test_qtestmain.cpp.

#include "src/widgets/notification_widget.h"
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
#include <DGuiApplicationHelper>

#include "stub/stub.h"

DWIDGET_USE_NAMESPACE
using namespace dmr;

// ---------------------------------------------------------------------------
// Stub state + helpers (unique prefix nwx_)
// ---------------------------------------------------------------------------

// Theme state returned by the stubbed DGuiApplicationHelper accessors.
// NOTE: DGuiApplicationHelper::setPaletteType() is NOT used — in a live DTK
// application it broadcasts an app-wide theme cascade that perturbs the
// shared MainWindow (and destabilises the whole single-process run). The
// stubs only affect code inside the guarded scope.
static DGuiApplicationHelper::ColorType s_nwx_themeType = DGuiApplicationHelper::LightType;
static DGuiApplicationHelper::SizeMode s_nwx_sizeMode = DGuiApplicationHelper::NormalMode;

static DGuiApplicationHelper::ColorType nwx_themeType_stub()
{
    return s_nwx_themeType;
}

static DGuiApplicationHelper::SizeMode nwx_sizeMode_stub()
{
    return s_nwx_sizeMode;
}

// Theme/sizemode stub guard (scoped, no global cascade).
class NotifThemeGuard
{
public:
    explicit NotifThemeGuard(DGuiApplicationHelper::ColorType theme,
                             DGuiApplicationHelper::SizeMode size = DGuiApplicationHelper::NormalMode)
    {
        s_nwx_themeType = theme;
        s_nwx_sizeMode = size;
        m_stub.set(
            static_cast<DGuiApplicationHelper::ColorType (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::themeType),
            &nwx_themeType_stub);
        m_stub.set(
            static_cast<DGuiApplicationHelper::SizeMode (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::sizeMode),
            &nwx_sizeMode_stub);
    }

private:
    Stub m_stub;
};

static void nwx_wait(int ms)
{
    QTest::qWait(ms);
}

// Fresh local parent, sized like the real main window.
static QWidget *nwx_makeParent(bool showIt = false)
{
    QWidget *parent = new QWidget;
    parent->resize(400, 300);
    if (showIt && QGuiApplication::primaryScreen()) {
        parent->show();
        nwx_wait(20);
    }
    return parent;
}

// The single DLabel child (m_pMsgLabel). NOTE: the ctor creates the label
// parentless; it only becomes a findable child once popup() adds it to the
// layout.
static DLabel *nwx_msgLabel(NotificationWidget &nw)
{
    return nw.findChild<DLabel *>();
}

static void nwx_sendPaint(NotificationWidget &nw)
{
    QPaintEvent pe(nw.rect());
    QApplication::sendEvent(&nw, &pe);
}

// ---------------------------------------------------------------------------
// Constructor / initMember (via observable effects)
// ---------------------------------------------------------------------------

TEST(notif_ext, construct_objectNameSet)
{
    QWidget parent;
    NotificationWidget nw(&parent);
    EXPECT_STREQ(nw.objectName().toStdString().c_str(), "NotificationFrame");
}

// The message label is created parentless; popup() parents it into the
// layout, so the defaults are verified through a popup first.
TEST(notif_ext, construct_msgLabelDefaults_viaPopup)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("hello popup"), false);
    nwx_wait(20);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // initMember defaults: popup text set, no frame shape.
    EXPECT_EQ(label->frameShape(), QFrame::NoFrame);
}

TEST(notif_ext, construct_themeForegroundRole_light)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    NotifThemeGuard themeGuard(DGuiApplicationHelper::LightType);
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("light"), false);
    nwx_wait(20);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // themeType() != 2 (light): ToolTipText role.
    EXPECT_EQ(label->foregroundRole(), QPalette::ToolTipText);
}

TEST(notif_ext, construct_themeForegroundRole_dark)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    NotifThemeGuard themeGuard(DGuiApplicationHelper::DarkType);
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("dark"), false);
    nwx_wait(20);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // The ctor's dark branch (themeType() == 2) sets foregroundRole to
    // DPalette::TextLively (ColorType 5, which maps to QPalette::Mid); DTK's
    // label machinery re-adjusts the role during the subsequent show/polish,
    // so only the difference from the light branch is asserted here.
    EXPECT_NE(label->foregroundRole(), QPalette::ToolTipText);
}

// NOTE: the ctor's themeTypeChanged lambda cannot be triggered safely from
// tests — the only in-process emitter is DGuiApplicationHelper::setPaletteType,
// whose app-wide cascade would perturb the shared MainWindow.

// ---------------------------------------------------------------------------
// Inline setters
// ---------------------------------------------------------------------------

TEST(notif_ext, setters_anchorVariants)
{
    QWidget parent;
    NotificationWidget nw(&parent);
    nw.setAnchor(NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(42);
    nw.setAnchorPoint(QPoint(7, 9));
    nw.setWM(true);
    nw.setWM(false);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// popup
// ---------------------------------------------------------------------------

TEST(notif_ext, popup_showsWithMessage)
{
    QWidget *parent = nwx_makeParent(true);   // child visibility needs a shown parent
    NotificationWidget nw(parent);
    nw.popup(QString("hello popup"), false);
    nwx_wait(20);

    EXPECT_TRUE(nw.isVisible());
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("hello popup"));
    // popup fixes the height to 30 in normal mode.
    EXPECT_EQ(nw.height(), 30);
    // margins applied in popup.
    QHBoxLayout *lay = qobject_cast<QHBoxLayout *>(nw.layout());
    ASSERT_NE(lay, nullptr);
    EXPECT_EQ(lay->contentsMargins(), QMargins(14, 4, 14, 4));
}

TEST(notif_ext, popup_compactMode_height20)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    NotifThemeGuard themeGuard(DGuiApplicationHelper::LightType,
                               DGuiApplicationHelper::CompactMode);
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("compact"), false);
    nwx_wait(20);
    EXPECT_EQ(nw.height(), 20);
}

TEST(notif_ext, popup_twice_labelAddedOnce)
{
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("first"), false);
    nw.popup(QString("second"), false);
    nwx_wait(20);
    // indexOf(m_pMsgLabel) != -1 the second time: label not duplicated.
    QHBoxLayout *lay = qobject_cast<QHBoxLayout *>(nw.layout());
    ASSERT_NE(lay, nullptr);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(lay->indexOf(label), 0);
    EXPECT_EQ(label->text(), QString("second"));
}

// flag=true starts the single-shot auto-hide timer: after its interval
// (2000ms non-wayland) the widget must hide itself.
TEST(notif_ext, popup_flagTrue_autoHidesAfterTimer)
{
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("auto hide"), true);
    nwx_wait(20);
    ASSERT_TRUE(nw.isVisible());
    // Wait long enough for the 2000ms single-shot timer to fire.
    nwx_wait(2300);
    EXPECT_FALSE(nw.isVisible());
}

// ---------------------------------------------------------------------------
// updateWithMessage
// ---------------------------------------------------------------------------

TEST(notif_ext, updateWithMessage_hidden_delegatesToPopup)
{
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    EXPECT_FALSE(nw.isVisible());
    nw.updateWithMessage(QString("first show"), false);
    nwx_wait(20);
    EXPECT_TRUE(nw.isVisible());
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("first show"));
}

TEST(notif_ext, updateWithMessage_visible_resizesKeepsText)
{
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    nw.popup(QString("short"), false);
    nwx_wait(20);
    ASSERT_TRUE(nw.isVisible());
    int oldWidth = nw.width();

    nw.updateWithMessage(QString("a much longer message text than before"), false);
    nwx_wait(20);
    // Visible branch: label updated, width recomputed, syncPosition called.
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("a much longer message text than before"));
    EXPECT_GE(nw.width(), oldWidth);
    EXPECT_TRUE(nw.isVisible());
}

TEST(notif_ext, updateWithMessage_elidesLongTextToMainWindowWidth)
{
    QWidget *parent = nwx_makeParent(true);  // width 400 -> elide threshold 400-12-12-60
    NotificationWidget nw(parent);
    nw.popup(QString("start"), false);
    nwx_wait(20);
    QString longMsg(200, QLatin1Char('x'));
    nw.updateWithMessage(longMsg, false);
    nwx_wait(20);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    // ElideMiddle inserts the ellipsis character.
    EXPECT_TRUE(label->text().contains(QString(QChar(0x2026))));
    EXPECT_LT(label->text().size(), longMsg.size());
}

// ---------------------------------------------------------------------------
// syncPosition (no-arg: uses m_pMainWindow->geometry())
// ---------------------------------------------------------------------------

TEST(notif_ext, syncPosition_noArg_bottom)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(5);
    nw.syncPosition();
    QRect geom = parent->geometry();
    QPoint expected(geom.center().x() - 60, geom.bottom() - 5 - 30);
    EXPECT_EQ(nw.pos(), expected);
}

TEST(notif_ext, syncPosition_noArg_northWest_anchorPoint)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_NORTH_WEST);
    nw.setAnchorPoint(QPoint(33, 44));
    nw.syncPosition();
    EXPECT_EQ(nw.pos(), QPoint(33, 44));
}

TEST(notif_ext, syncPosition_noArg_none_centers)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_NONE);
    nw.syncPosition();
    QRect geom = parent->geometry();
    QPoint expected(geom.center().x() - 60, geom.center().y() - 15);
    EXPECT_EQ(nw.pos(), expected);
}

// ---------------------------------------------------------------------------
// syncPosition (rect overload)
// ---------------------------------------------------------------------------

TEST(notif_ext, syncPosition_rect_bottom)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_BOTTOM);
    nw.setAnchorDistance(10);
    QRect rect(0, 0, 800, 600);
    nw.syncPosition(rect);
    QPoint expected(rect.center().x() - 60, rect.bottom() - 10 - 30);
    EXPECT_EQ(nw.pos(), expected);
}

TEST(notif_ext, syncPosition_rect_northWest_anchorPointOnly)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_NORTH_WEST);
    nw.setAnchorPoint(QPoint(10, 20));
    QRect rect(5, 5, 800, 600);
    nw.syncPosition(rect);
    // Non-platform variant moves to the anchor point directly, ignoring
    // rect.topLeft() (that offset exists only in the platform variant).
    EXPECT_EQ(nw.pos(), QPoint(10, 20));
}

TEST(notif_ext, syncPosition_rect_none_centers)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(120, 30);
    nw.setAnchor(NotificationWidget::ANCHOR_NONE);
    QRect rect(0, 0, 800, 600);
    nw.syncPosition(rect);
    QPoint expected(rect.center().x() - 60, rect.center().y() - 15);
    EXPECT_EQ(nw.pos(), expected);
}

// ---------------------------------------------------------------------------
// showEvent / resizeEvent
// ---------------------------------------------------------------------------

TEST(notif_ext, showEvent_resizesToLabelSizeHint)
{
    QWidget *parent = nwx_makeParent(true);
    NotificationWidget nw(parent);
    // popup() first: label added to layout and margins set to 14/4/14/4,
    // so the showEvent recompute is deterministic.
    nw.popup(QString("shown"), false);
    nwx_wait(20);
    DLabel *label = nwx_msgLabel(nw);
    ASSERT_NE(label, nullptr);
    QShowEvent se;
    QApplication::sendEvent(&nw, &se);
    // showEvent: width = label sizeHint + margins(14, 14).
    EXPECT_EQ(nw.width(), label->sizeHint().width() + 28);
}

TEST(notif_ext, resizeEvent_doesNotCrash)
{
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    QResizeEvent re(QSize(100, 40), nw.size());
    QApplication::sendEvent(&nw, &re);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// paintEvent (light / dark / other themes; theme via stub, no cascade)
// ---------------------------------------------------------------------------

TEST(notif_ext, paintEvent_lightTheme)
{
    NotifThemeGuard themeGuard(DGuiApplicationHelper::LightType);
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(nwx_sendPaint(nw));
}

TEST(notif_ext, paintEvent_darkTheme)
{
    NotifThemeGuard themeGuard(DGuiApplicationHelper::DarkType);
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(nwx_sendPaint(nw));
}

TEST(notif_ext, paintEvent_unknownTheme)
{
    NotifThemeGuard themeGuard(DGuiApplicationHelper::UnknownType);
    QWidget *parent = nwx_makeParent();
    NotificationWidget nw(parent);
    nw.resize(80, 30);
    EXPECT_NO_FATAL_FAILURE(nwx_sendPaint(nw));
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

TEST(notif_ext, destroy_whileVisible_cleansUp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget *parent = nwx_makeParent(true);
    {
        NotificationWidget nw(parent);
        nw.popup(QString("bye"), true);
        nwx_wait(20);
        EXPECT_TRUE(nw.isVisible());
    }
    EXPECT_NO_FATAL_FAILURE(nwx_wait(30));
}
