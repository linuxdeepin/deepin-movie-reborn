// Copyright (C) 2026 Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dedicated unit tests for src/widgets/tip.cpp (hover tooltip popup).
//
// Suite name "tip_ext"; static helpers use unique prefix "tip_".
//
// Method bodies verified against the worktree sources at base 4608a9f2:
//   * ctor (59-124): WA_DeleteOnClose, Qt::ToolTip flags, translucent
//     background, objectName "Tip", inner QFrame with iconLabel ("TipIcon",
//     hidden when the icon is null) + textLabel ("TipText", DLabel with the
//     given text), adjustSize, hide() at the end, bIsWM initialised from
//     DWindowManagerHelper::hasBlurWindow().
//   * setText (152-160): updates the label text + m_strText + update().
//   * setters/getters (145-193): radius (default 8), background,
//     borderColor (default QColor(0,0,0,51)).
//   * slotWMChanged (182-188): re-reads hasBlurWindow into bIsWM.
//   * pop (189-196): show() then move(center - size()/2).
//   * enterEvent (130-142): hides the tip on mouse enter.
//   * paintEvent (243-288): !bIsWM -> transparency 255; light/dark/other
//     theme styles; bIsWM -> rounded rect else plain rect.
//   * resizeEvent (291-296): delegates to QWidget::resizeEvent.
//   * resetSize (287-306): text wider than maxWidth-14 -> word wrap +
//     fixed widths (tip maxWidth, label maxWidth-14).
//
// External dependencies are stubbed, never touched for real:
//   * DWindowManagerHelper::hasBlurWindow (WM blur state -> bIsWM branches)
//   * DGuiApplicationHelper::themeType (theme branches)
//     NOTE: setPaletteType() is NOT used — in a live DTK application it
//     broadcasts an app-wide theme cascade that perturbs the shared
//     MainWindow (and destabilises the whole single-process run). The stubs
//     only affect code inside the guarded scope.
// Only Google Test (TEST(...)); gtest supplies main() via test_qtestmain.cpp.

#include "src/widgets/tip.h"
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QLabel>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QEnterEvent>

#include <DLabel>
#include <DWindowManagerHelper>
#include <DGuiApplicationHelper>
#include <DFontSizeManager>

#include "stub/stub.h"

DWIDGET_USE_NAMESPACE
using namespace dmr;

// ---------------------------------------------------------------------------
// Stub state + helpers (unique prefix tip_)
// ---------------------------------------------------------------------------

// Fake WM blur state returned by the stubbed hasBlurWindow.
static bool s_tip_hasBlur = true;

static bool tip_hasBlurWindow_stub()
{
    return s_tip_hasBlur;
}

// Theme state returned by the stubbed DGuiApplicationHelper::themeType.
static DGuiApplicationHelper::ColorType s_tip_themeType = DGuiApplicationHelper::LightType;

static DGuiApplicationHelper::ColorType tip_themeType_stub()
{
    return s_tip_themeType;
}

// Combined environment guard (scoped, no global cascade).
class TipEnvGuard
{
public:
    TipEnvGuard(bool hasBlur, DGuiApplicationHelper::ColorType theme)
    {
        s_tip_hasBlur = hasBlur;
        s_tip_themeType = theme;
        m_stub.set(
            static_cast<bool (DWindowManagerHelper::*)() const>(
                &DWindowManagerHelper::hasBlurWindow),
            &tip_hasBlurWindow_stub);
        m_stub.set(
            static_cast<DGuiApplicationHelper::ColorType (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::themeType),
            &tip_themeType_stub);
    }

private:
    Stub m_stub;
};

static void tip_wait(int ms)
{
    QTest::qWait(ms);
}

static QLabel *tip_iconLabel(Tip &t)
{
    return t.findChild<QLabel *>("TipIcon");
}

static DLabel *tip_textLabel(Tip &t)
{
    return t.findChild<DLabel *>("TipText");
}

static void tip_sendPaint(Tip &t)
{
    QPaintEvent pe(t.rect());
    QApplication::sendEvent(&t, &pe);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TEST(tip_ext, construct_objectNameAndFlags)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("hello"), nullptr);
    EXPECT_STREQ(t.objectName().toStdString().c_str(), "Tip");
    EXPECT_TRUE(t.windowFlags() & Qt::ToolTip);
    EXPECT_TRUE(t.testAttribute(Qt::WA_TranslucentBackground));
    // The ctor hides the tip at the end.
    EXPECT_FALSE(t.isVisible());
}

TEST(tip_ext, construct_textShownInLabel)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("hello"), nullptr);
    DLabel *label = tip_textLabel(t);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("hello"));
    EXPECT_FALSE(label->wordWrap());
    EXPECT_EQ(label->foregroundRole(), DPalette::ToolTipText);
}

TEST(tip_ext, construct_iconNull_hidesIcon)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("hello"), nullptr);
    QLabel *icon = tip_iconLabel(t);
    ASSERT_NE(icon, nullptr);
    EXPECT_TRUE(icon->isHidden());
}

TEST(tip_ext, construct_iconSet_showsPixmap)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    QPixmap pm(16, 16);
    pm.fill(Qt::red);
    Tip t(pm, QString("with icon"), nullptr);
    QLabel *icon = tip_iconLabel(t);
    ASSERT_NE(icon, nullptr);
    EXPECT_FALSE(icon->isHidden());
    // Qt6: QLabel::pixmap() returns QPixmap by value.
    EXPECT_FALSE(icon->pixmap().isNull());
    EXPECT_EQ(icon->pixmap().size(), QSize(16, 16));
}

TEST(tip_ext, construct_defaultPropertyValues)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("defaults"), nullptr);
    // TipPrivate field defaults.
    EXPECT_EQ(t.radius(), 8);
    EXPECT_EQ(t.borderColor(), QColor(0, 0, 0, static_cast<int>(0.2 * 255)));
    EXPECT_EQ(t.background(), QBrush());
}

// ---------------------------------------------------------------------------
// setText / setters + getters
// ---------------------------------------------------------------------------

TEST(tip_ext, setText_updatesLabel)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("before"), nullptr);
    t.setText(QString("after"));
    DLabel *label = tip_textLabel(t);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QString("after"));
}

TEST(tip_ext, setters_getters_roundtrip)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("roundtrip"), nullptr);
    t.setRadius(12);
    EXPECT_EQ(t.radius(), 12);
    QBrush brush(QColor(10, 20, 30));
    t.setBackground(brush);
    EXPECT_EQ(t.background(), brush);
    QColor color(1, 2, 3);
    t.setBorderColor(color);
    EXPECT_EQ(t.borderColor(), color);
}

// ---------------------------------------------------------------------------
// pop
// ---------------------------------------------------------------------------

TEST(tip_ext, pop_showsAndCentersOnPoint)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("pop me"), nullptr);
    int w = t.width();
    int h = t.height();
    t.pop(QPoint(100, 100));
    // pop() shows and moves; a synthetic enter event (offscreen mouse) hides
    // the tip once the event loop runs, so visibility is checked immediately.
    EXPECT_TRUE(t.isVisible());
    EXPECT_EQ(t.pos(), QPoint(100 - w / 2, 100 - h / 2));
}

// ---------------------------------------------------------------------------
// slotWMChanged (bIsWM re-read from the stubbed helper)
// ---------------------------------------------------------------------------

TEST(tip_ext, slotWMChanged_blurFalse_thenTrue)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("wm"), nullptr);
    // hasBlurWindow now reports false; slotWMChanged must pick it up.
    s_tip_hasBlur = false;
    t.slotWMChanged();
    // Paint with the updated internal flag: covers the !bIsWM branch.
    t.resize(200, 60);
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));

    // And back to true: covers the bIsWM rounded-rect branch.
    s_tip_hasBlur = true;
    t.slotWMChanged();
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));
}

// ---------------------------------------------------------------------------
// enterEvent / resizeEvent
// ---------------------------------------------------------------------------

TEST(tip_ext, enterEvent_hidesTip)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("hover"), nullptr);
    t.pop(QPoint(100, 100));
    // Visibility must be checked before any event-loop wait: the synthetic
    // enter event delivered by the platform would already hide the tip.
    ASSERT_TRUE(t.isVisible());

    QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), QPointF(105, 105));
    QApplication::sendEvent(&t, &enter);
    EXPECT_FALSE(t.isVisible());
}

TEST(tip_ext, resizeEvent_doesNotCrash)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("resize"), nullptr);
    QResizeEvent re(QSize(150, 50), t.size());
    QApplication::sendEvent(&t, &re);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// paintEvent (bIsWM x theme matrix; theme via stub, no global cascade)
// ---------------------------------------------------------------------------

TEST(tip_ext, paintEvent_wmTrue_light)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("paint"), nullptr);
    t.resize(200, 60);
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));
}

TEST(tip_ext, paintEvent_wmTrue_dark)
{
    TipEnvGuard env(true, DGuiApplicationHelper::DarkType);
    Tip t(QPixmap(), QString("paint"), nullptr);
    t.resize(200, 60);
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));
}

TEST(tip_ext, paintEvent_wmTrue_unknownTheme)
{
    TipEnvGuard env(true, DGuiApplicationHelper::UnknownType);
    Tip t(QPixmap(), QString("paint"), nullptr);
    t.resize(200, 60);
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));
}

TEST(tip_ext, paintEvent_wmFalse_lightAndDark)
{
    // hasBlurWindow false at construction: bIsWM == false branch.
    TipEnvGuard env(false, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("paint"), nullptr);
    t.resize(200, 60);
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));

    // Dark theme on the same !bIsWM instance.
    s_tip_themeType = DGuiApplicationHelper::DarkType;
    EXPECT_NO_FATAL_FAILURE(tip_sendPaint(t));
}

// ---------------------------------------------------------------------------
// resetSize
// ---------------------------------------------------------------------------

TEST(tip_ext, resetSize_shortText_noChanges)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("short"), nullptr);
    t.resetSize(500);
    DLabel *label = tip_textLabel(t);
    ASSERT_NE(label, nullptr);
    // Text narrower than maxWidth - 14: no wrap, no fixed width.
    EXPECT_FALSE(label->wordWrap());
    EXPECT_EQ(label->maximumWidth(), QWIDGETSIZE_MAX);
    EXPECT_EQ(t.maximumWidth(), QWIDGETSIZE_MAX);
}

TEST(tip_ext, resetSize_longText_wrapsAndFixedWidth)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip t(QPixmap(), QString("a fairly long tooltip message that must wrap"), nullptr);
    t.resetSize(100);
    DLabel *label = tip_textLabel(t);
    ASSERT_NE(label, nullptr);
    // Text wider than maxWidth - 14: wrap + fixed widths.
    EXPECT_TRUE(label->wordWrap());
    EXPECT_EQ(label->maximumWidth(), 86);
    EXPECT_EQ(t.maximumWidth(), 100);
}

TEST(tip_ext, resetSize_smallMaxWidth_wraps)
{
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    // w >= maxWidth - 14 is the wrap condition (>=, not >): verify with a
    // very long string at a small maxWidth.
    Tip t(QPixmap(), QString(80, QLatin1Char('w')), nullptr);
    t.resetSize(60);
    DLabel *label = tip_textLabel(t);
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->wordWrap());
    EXPECT_EQ(label->maximumWidth(), 46);
    EXPECT_EQ(t.maximumWidth(), 60);
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

TEST(tip_ext, destroy_afterPop_cleansUp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    {
        Tip t(QPixmap(), QString("bye"), nullptr);
        t.pop(QPoint(50, 50));
        // Check immediately — see the synthetic-enter note in pop tests.
        EXPECT_TRUE(t.isVisible());
    }
    SUCCEED();
}

// Deleting a heap instance also exercises the deleting destructor (D0).
TEST(tip_ext, destroy_heapInstance_cleansUp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    TipEnvGuard env(true, DGuiApplicationHelper::LightType);
    Tip *t = new Tip(QPixmap(), QString("heap"), nullptr);
    t->pop(QPoint(30, 30));
    EXPECT_TRUE(t->isVisible());
    delete t;
    SUCCEED();
}
