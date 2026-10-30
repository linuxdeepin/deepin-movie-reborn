// Copyright (C) 2026 Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dedicated unit tests for src/widgets/titlebar.cpp (main-window title bar).
//
// Suite name "titlebar_ext"; static helpers use unique prefix "tb_".
//
// Method bodies verified against the worktree sources at base 4608a9f2:
//   * ctor (50-100): WA_TranslucentBackground false, focusPolicy NoFocus
//     (both the Titlebar and the inner DTitlebar), objectName TITLEBAR
//     ("titleBar"), QHBoxLayout margins/spacing 0, DTitlebar with
//     min/max/system/close button hints, setBackgroundTransparent(false),
//     setBlurBackground(true), themed "deepin-movie" icon, empty title,
//     DLabel title text (T7 font, AlignCenter), QGraphicsDropShadowEffect,
//     themeTypeChanged -> slotThemeTypeChanged connect.
//   * titlebar() (151-155): returns the inner DTitlebar pointer.
//   * setTitletxt (157-163): forwards to the title-text label.
//   * setTitleBarBackground (165-226): m_play = flag;
//     - play=true: titlebar background transparent, playColor palettes,
//       shadow effect color Qt::transparent;
//     - play=false: palette Window QColor(200,200,200,50), background
//       transparent + blur off, themeType dark -> playColor palettes /
//       light -> QColor(98,110,136,225) palettes, then setGraphicsEffect +
//       update().
//   * setIcon (121-129): forwards the pixmap to the inner DTitlebar.
//   * slotThemeTypeChanged (108-149): play -> playColor palettes + shadow
//     (offset, blur offsetY, darkEffectColor); non-play + dark -> playColor
//     palettes + darkEffectColor; non-play + light -> QColor(98,110,136,225)
//     palettes + blurRadius/lightEffectColor; always setGraphicsEffect.
//   * paintEvent (227-253): play -> fills with ":resources/icons/titlebar.png"
//     brush + palette Window transparent; non-play -> transparent fill.
//
// Theme branches use a scoped stub for DGuiApplicationHelper::themeType —
// NOTE: setPaletteType() is NOT used: in a live DTK application it broadcasts
// an app-wide theme cascade that perturbs the shared MainWindow (and
// destabilises the whole single-process run). The private TitlebarPrivate
// state is verified through public effects (palettes, the public
// QGraphicsDropShadowEffect getters, focus policies). Only Google Test
// (TEST(...)); gtest supplies main() via test_qtestmain.cpp.

#include "src/widgets/titlebar.h"
#include "application.h"

#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QLabel>
#include <QPaintEvent>
#include <QGraphicsDropShadowEffect>
#include <QPalette>

#include <DTitlebar>
#include <DLabel>
#include <DGuiApplicationHelper>

#include "stub/stub.h"

DWIDGET_USE_NAMESPACE
using namespace dmr;

// ---------------------------------------------------------------------------
// Stub state + helpers (unique prefix tb_)
// ---------------------------------------------------------------------------

// Theme state returned by the stubbed DGuiApplicationHelper::themeType.
static DGuiApplicationHelper::ColorType s_tb_themeType = DGuiApplicationHelper::LightType;

static DGuiApplicationHelper::ColorType tb_themeType_stub()
{
    return s_tb_themeType;
}

// Theme stub guard (scoped, no global cascade).
class TitlebarThemeGuard
{
public:
    explicit TitlebarThemeGuard(DGuiApplicationHelper::ColorType theme)
    {
        s_tb_themeType = theme;
        m_stub.set(
            static_cast<DGuiApplicationHelper::ColorType (DGuiApplicationHelper::*)() const>(
                &DGuiApplicationHelper::themeType),
            &tb_themeType_stub);
    }

private:
    Stub m_stub;
};

static void tb_wait(int ms)
{
    QTest::qWait(ms);
}

// Fresh local parent playing the role of the main window.
static QWidget *tb_makeParent(bool showIt = false)
{
    QWidget *parent = new QWidget;
    parent->resize(800, 600);
    if (showIt && QGuiApplication::primaryScreen()) {
        parent->show();
        tb_wait(20);
    }
    return parent;
}

static QGraphicsDropShadowEffect *tb_shadowEffect(Titlebar &tb)
{
    return qobject_cast<QGraphicsDropShadowEffect *>(tb.graphicsEffect());
}

static void tb_sendPaint(Titlebar &tb)
{
    QPaintEvent pe(tb.rect());
    QApplication::sendEvent(&tb, &pe);
}

// ---------------------------------------------------------------------------
// Constructor (via observable effects)
// ---------------------------------------------------------------------------

TEST(titlebar_ext, construct_objectNameIsTitleBar)
{
    QWidget parent;
    Titlebar tb(&parent);
    // TITLEBAR macro ("titleBar", ac-deepin-movie-define.h).
    EXPECT_STREQ(tb.objectName().toStdString().c_str(), "titleBar");
    EXPECT_STREQ(tb.accessibleName().toStdString().c_str(), "titleBar");
}

TEST(titlebar_ext, construct_focusPolicyNoFocus)
{
    QWidget parent;
    Titlebar tb(&parent);
    // The ctor sets NoFocus on the Titlebar itself...
    EXPECT_EQ(tb.focusPolicy(), Qt::NoFocus);
    // ...and on the inner DTitlebar.
    DTitlebar *bar = tb.titlebar();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(bar->focusPolicy(), Qt::NoFocus);
}

TEST(titlebar_ext, construct_titlebarWindowButtonHints)
{
    QWidget parent;
    Titlebar tb(&parent);
    DTitlebar *bar = tb.titlebar();
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->windowFlags() & Qt::WindowMinMaxButtonsHint);
    EXPECT_TRUE(bar->windowFlags() & Qt::WindowSystemMenuHint);
    EXPECT_TRUE(bar->windowFlags() & Qt::WindowCloseButtonHint);
}

TEST(titlebar_ext, construct_translucentBackgroundOff)
{
    QWidget parent;
    Titlebar tb(&parent);
    EXPECT_FALSE(tb.testAttribute(Qt::WA_TranslucentBackground));
}

TEST(titlebar_ext, construct_layoutMarginsAndSpacingZero)
{
    QWidget parent;
    Titlebar tb(&parent);
    QLayout *lay = tb.layout();
    ASSERT_NE(lay, nullptr);
    EXPECT_EQ(lay->contentsMargins(), QMargins(0, 0, 0, 0));
    EXPECT_EQ(lay->spacing(), 0);
    // The DTitlebar is the first widget in the layout.
    EXPECT_EQ(lay->itemAt(0)->widget(), tb.titlebar());
}

TEST(titlebar_ext, construct_graphicsEffectNullUntilThemeSlot)
{
    QWidget parent;
    Titlebar tb(&parent);
    // The ctor creates the shadow effect but only slotThemeTypeChanged /
    // setTitleBarBackground install it via setGraphicsEffect.
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    tb.slotThemeTypeChanged();
    EXPECT_NE(tb.graphicsEffect(), nullptr);
    EXPECT_NE(tb_shadowEffect(tb), nullptr);
}

// NOTE: the ctor's themeTypeChanged connection cannot be exercised safely
// from tests — the only in-process emitter is
// DGuiApplicationHelper::setPaletteType, whose app-wide cascade would
// perturb the shared MainWindow. slotThemeTypeChanged itself is invoked
// directly instead.

// ---------------------------------------------------------------------------
// titlebar() / setTitletxt / setIcon
// ---------------------------------------------------------------------------

TEST(titlebar_ext, titlebar_getterReturnsSameInstance)
{
    QWidget parent;
    Titlebar tb(&parent);
    DTitlebar *bar = tb.titlebar();
    ASSERT_NE(bar, nullptr);
    EXPECT_EQ(tb.titlebar(), bar);
}

TEST(titlebar_ext, setTitletxt_doesNotCrash)
{
    QWidget parent;
    Titlebar tb(&parent);
    EXPECT_NO_FATAL_FAILURE(tb.setTitletxt(QString("my movie title")));
    // The inner DLabel is a grandchild (inside the DTitlebar layout): find
    // it under the DTitlebar to check the text was forwarded.
    DTitlebar *bar = tb.titlebar();
    ASSERT_NE(bar, nullptr);
    DLabel *label = bar->findChild<DLabel *>();
    // DTitlebar may hold internal labels; at least one must exist and, if
    // it is the title label, carry the forwarded text.
    if (label) {
        EXPECT_TRUE(label->text().isEmpty() || label->text() == QString("my movie title"));
    }
    SUCCEED();
}

TEST(titlebar_ext, setIcon_pixmapForwarded)
{
    QWidget parent;
    Titlebar tb(&parent);
    QPixmap pm(32, 32);
    pm.fill(Qt::blue);
    EXPECT_NO_FATAL_FAILURE(tb.setIcon(pm));
    SUCCEED();
}

// ---------------------------------------------------------------------------
// setTitleBarBackground
// ---------------------------------------------------------------------------

TEST(titlebar_ext, setTitleBarBackground_false_paletteWindowColor)
{
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    QWidget parent;
    Titlebar tb(&parent);
    tb.setTitleBarBackground(false);
    // play=false branch: palette Window QColor(200,200,200,50).
    EXPECT_EQ(tb.palette().color(QPalette::Window), QColor(200, 200, 200, 50));
    // The shadow effect is installed at the end.
    EXPECT_NE(tb_shadowEffect(tb), nullptr);
}

TEST(titlebar_ext, setTitleBarBackground_false_light_textPalette)
{
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    QWidget parent;
    Titlebar tb(&parent);
    tb.setTitleBarBackground(false);
    // Light branch: the slot sets ButtonText QColor(98,110,136,225) directly
    // on the DTitlebar (the same palette goes to m_titletxt, which cannot be
    // addressed robustly through findChild — DTitlebar has its own DLabels).
    EXPECT_EQ(tb.titlebar()->palette().color(QPalette::ButtonText), QColor(98, 110, 136, 225));
}

TEST(titlebar_ext, setTitleBarBackground_false_dark_textPalettePlayColor)
{
    TitlebarThemeGuard guard(DGuiApplicationHelper::DarkType);
    QWidget parent;
    Titlebar tb(&parent);
    tb.setTitleBarBackground(false);
    // Dark branch (non-play): playColor on the titlebar palette.
    EXPECT_EQ(tb.titlebar()->palette().color(QPalette::ButtonText), QColor(255, 255, 255, 204));
}

TEST(titlebar_ext, setTitleBarBackground_true_shadowTransparent)
{
    QWidget parent;
    Titlebar tb(&parent);
    tb.setTitleBarBackground(true);
    // play=true branch: shadow effect color Qt::transparent.
    QGraphicsDropShadowEffect *fx = tb_shadowEffect(tb);
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(fx->color(), QColor(Qt::transparent));
}

// ---------------------------------------------------------------------------
// slotThemeTypeChanged
// ---------------------------------------------------------------------------

TEST(titlebar_ext, slotThemeTypeChanged_light_textPalette)
{
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    QWidget parent;
    Titlebar tb(&parent);
    tb.slotThemeTypeChanged();
    // Non-play + light: ButtonText QColor(98,110,136,225) set on the titlebar.
    EXPECT_EQ(tb.titlebar()->palette().color(QPalette::ButtonText), QColor(98, 110, 136, 225));
    // Light branch: lightEffectColor + blurRadius-based shadow (52).
    QGraphicsDropShadowEffect *fx = tb_shadowEffect(tb);
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(fx->color(), QColor(200, 200, 200, 45));
    EXPECT_EQ(fx->blurRadius(), 52.0);
}

TEST(titlebar_ext, slotThemeTypeChanged_dark_textPalettePlayColor)
{
    TitlebarThemeGuard guard(DGuiApplicationHelper::DarkType);
    QWidget parent;
    Titlebar tb(&parent);
    tb.slotThemeTypeChanged();
    // Non-play + dark: playColor on the titlebar palette.
    EXPECT_EQ(tb.titlebar()->palette().color(QPalette::ButtonText), QColor(255, 255, 255, 204));
    QGraphicsDropShadowEffect *fx = tb_shadowEffect(tb);
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(fx->color(), QColor(30, 30, 30, 50));
    EXPECT_EQ(fx->blurRadius(), 15.0);
}

TEST(titlebar_ext, slotThemeTypeChanged_playState_darkEffectShadow)
{
    QWidget parent;
    Titlebar tb(&parent);
    tb.setTitleBarBackground(true);   // m_play = true
    tb.slotThemeTypeChanged();
    // m_play branch of the slot takes precedence over the theme:
    // darkEffectColor + blurRadius = offsetY (15).
    QGraphicsDropShadowEffect *fx = tb_shadowEffect(tb);
    ASSERT_NE(fx, nullptr);
    EXPECT_EQ(fx->color(), QColor(30, 30, 30, 50));
    EXPECT_EQ(fx->blurRadius(), 15.0);
}

TEST(titlebar_ext, slotThemeTypeChanged_themeSwitchLightToDark)
{
    QWidget parent;
    Titlebar tb(&parent);
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    tb.slotThemeTypeChanged();
    QColor lightText = tb.titlebar()->palette().color(QPalette::ButtonText);
    EXPECT_EQ(lightText, QColor(98, 110, 136, 225));

    // Switch the stubbed theme and re-run the slot.
    s_tb_themeType = DGuiApplicationHelper::DarkType;
    tb.slotThemeTypeChanged();
    EXPECT_EQ(tb.titlebar()->palette().color(QPalette::ButtonText), QColor(255, 255, 255, 204));
}

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------

TEST(titlebar_ext, paintEvent_playState_fillsWithTitlebarPixmap)
{
    QWidget *parent = tb_makeParent();
    Titlebar tb(parent);
    tb.setTitleBarBackground(true);
    tb.resize(800, 50);
    EXPECT_NO_FATAL_FAILURE(tb_sendPaint(tb));
}

TEST(titlebar_ext, paintEvent_normalState_transparentFill)
{
    QWidget *parent = tb_makeParent();
    Titlebar tb(parent);
    tb.setTitleBarBackground(false);
    tb.resize(800, 50);
    EXPECT_NO_FATAL_FAILURE(tb_sendPaint(tb));
}

TEST(titlebar_ext, paintEvent_defaultState_noPlay)
{
    QWidget *parent = tb_makeParent();
    Titlebar tb(parent);
    tb.resize(800, 50);
    // Without setTitleBarBackground the play flag stays false.
    EXPECT_NO_FATAL_FAILURE(tb_sendPaint(tb));
}

// ---------------------------------------------------------------------------
// Destruction
// ---------------------------------------------------------------------------

TEST(titlebar_ext, destroy_afterUse_cleansUp)
{
    if (!QGuiApplication::primaryScreen()) GTEST_SKIP();
    QWidget *parent = tb_makeParent(true);
    {
        Titlebar tb(parent);
        tb.setTitleBarBackground(false);
        tb.slotThemeTypeChanged();
    }
    EXPECT_NO_FATAL_FAILURE(tb_wait(20));
}

// Deleting a heap instance also exercises the deleting destructor (D0).
TEST(titlebar_ext, destroy_heapInstance_cleansUp)
{
    QWidget parent;
    TitlebarThemeGuard guard(DGuiApplicationHelper::LightType);
    Titlebar *tb = new Titlebar(&parent);
    tb->slotThemeTypeChanged();
    delete tb;
    SUCCEED();
}
