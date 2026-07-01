// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <DSlider>
#include <DListWidget>
#include <QMenu>
#include <gtest/gtest.h>

// Expose ToolboxProxy private/protected members + slots so the coverage tests
// below can drive them directly. Qt/dtk headers are included above so they are
// parsed before the macro takes effect; only toolbox_proxy.h is affected.
// (Same pattern test_mainwindow.cpp uses for MainWindow.)
#define protected public
#define private public
#include "src/widgets/toolbox_proxy.h"
#undef protected
#undef private

#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/notification_widget.h"
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include "presenter.h"
#include "titlebar.h"
#include "src/widgets/tip.h"

using namespace dmr;
/*TEST(ToolBox, buttonBoxButton)
{
    MainWindow* w = dApp->getMainWindow();
    ButtonBoxButton *btn = new ButtonBoxButton("test", w);

    btn->show();
    QTest::qWait(400);
    QTest::mouseMove(btn);

    btn->deleteLater();
}*/

TEST(ToolBox, buttonTooltip)
{
    MainWindow* w = dApp->getMainWindow();
    ButtonToolTip *tip = new ButtonToolTip(w);

    tip->setText("123");
    tip->show();
    tip->changeTheme(darkTheme);
    tip->show();

    tip->deleteLater();
}

TEST(ToolBox, notificationWidget)
{
    MainWindow* w = dApp->getMainWindow();
    NotificationWidget *nwBottom = new NotificationWidget(w);
    NotificationWidget *nwNone = new NotificationWidget(w);

    nwBottom->setAnchor(NotificationWidget::ANCHOR_BOTTOM);
    nwNone->setAnchor(NotificationWidget::ANCHOR_NONE);
    nwBottom->show();
    nwNone->show();
    nwBottom->syncPosition(w->geometry());
    nwNone->syncPosition(w->geometry());
}

TEST(ToolBox, tip)
{
    MainWindow* w = dApp->getMainWindow();
    Tip *tip = new Tip(QPixmap(), "", w);

    tip->setText("test");
    tip->setBackground(QBrush(QColor(Qt::white)));
    tip->setRadius(2);
    tip->setBorderColor(QColor(Qt::blue));
    tip->pop(QPoint(200, 300));
    QColor color = tip->borderColor();
    QBrush brush = tip->background();
    tip->deleteLater();
}

TEST(ToolBox, animationLabel)
{
    MainWindow *mw = new MainWindow();
    AnimationLabel *aLabel = new AnimationLabel(mw, mw);
    aLabel->show();

    QEvent moveEvent(QEvent::Move);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPointF(0,0), QPointF(0,0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier, QPointingDevice::primaryPointingDevice());
    QApplication::sendEvent(aLabel, &moveEvent);
    QApplication::sendEvent(aLabel, &releaseEvent);
}

// ---- ToolboxProxy member coverage (src/widgets/toolbox_proxy.cpp) ----
// The existing tests above only touch auxiliary widgets (Tip/NotificationWidget/
// AnimationLabel). These tests drive ToolboxProxy's own methods directly. A fresh
// ToolboxProxy is used for layout-mutating cases so the shared main-window toolbox
// is left untouched; the shared toolbox is used only for playlist-aware reads.

// updateProgress (progBar accumulate/flush + viewProgBar branch), updateSlider,
// updateSliderPoint (toolbox_proxy.cpp ~3408-3485).
TEST(ToolBox, progressAndSliderPaths)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine = w->engine();
    ToolboxProxy *tb = new ToolboxProxy(w, engine);
    tb->show();
    QTest::qWait(30);

    // viewProgBar (film) branch: currentIndex != 1 -> no division by width.
    tb->m_pProgBar_Widget->setCurrentIndex(0);
    tb->updateProgress(8);
    tb->updateSlider();   // viewProgBar branch -> seekAbsolute (idle engine returns early)

    // progBar branch (currentIndex == 1). Guard the integer division: give the
    // slider a non-zero width so nValue*nDuration/width can't SIGFPE.
    tb->m_pProgBar_Widget->setCurrentIndex(1);
    tb->m_pProgBar->setFixedWidth(200);
    tb->m_processAdd = 0.0f;
    tb->updateProgress(0);   // value in [-1,1], processAdd in (-1,1) -> accumulate, return
    tb->m_processAdd = 5.0f;
    tb->updateProgress(0);   // processAdd out of (-1,1) -> flush + setSliderPosition/setValue
    tb->updateSlider();      // progBar branch

    QPoint origin(0, 0);
    tb->updateSliderPoint(origin);

    QTest::qWait(20);
    delete tb;
}

// volumeUp/Down, calculationStep, changeMuteState (enabled + disabled-slider
// emit-sigUnsupported paths), setVolSliderHide, setButtonTooltipHide (~2451-3255).
TEST(ToolBox, volumeAndMutePaths)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine = w->engine();
    ToolboxProxy *tb = new ToolboxProxy(w, engine);
    tb->show();
    QTest::qWait(30);

    // enabled slider -> real volume/mute paths.
    tb->m_pVolSlider->setEnabled(true);
    tb->volumeUp();
    tb->volumeDown();
    tb->calculationStep(120);
    tb->changeMuteState();

    // disabled slider -> emit sigUnsupported early-return paths.
    tb->m_pVolSlider->setEnabled(false);
    tb->volumeUp();
    tb->volumeDown();

    tb->setVolSliderHide();
    tb->setButtonTooltipHide();
    QTest::qWait(20);
    delete tb;
}

// anyPopupShown/closeAnyPopup + private preview slots + waitPlay (~1684-1819, 1963-2004).
TEST(ToolBox, popupAndSlotPaths)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine = w->engine();
    ToolboxProxy *tb = new ToolboxProxy(w, engine);
    tb->show();
    QTest::qWait(30);

    (void)tb->anyPopupShown();
    tb->closeAnyPopup();

    // private/protected slots, exposed via the macro above.
    tb->slotLeavePreview();
    tb->slotHidePreviewTime();
    tb->slotSliderPressed();

    // waitPlay disables buttons then re-enables via a 500ms singleShot; let it fire.
    tb->waitPlay();
    QTest::qWait(650);

    delete tb;
}

// isInMircastWidget (hidden + contains branches), updateMircastWidget, hideMircastWidget (~2425-2447).
TEST(ToolBox, mircastWidgetPaths)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine = w->engine();
    ToolboxProxy *tb = new ToolboxProxy(w, engine);
    tb->show();
    QTest::qWait(30);

    tb->m_mircastWidget->hide();
    EXPECT_FALSE(tb->isInMircastWidget(QPoint(0, 0)));   // !isVisible early return

    // Visible widget: exercise the geometry().contains() return line with a point
    // inside and outside the rect. (Result depends on the WM-managed geometry, so
    // we don't assert it — coverage of the contains line is what matters here.)
    tb->m_mircastWidget->show();
    tb->m_mircastWidget->setGeometry(0, 0, 100, 100);
    (void)tb->isInMircastWidget(QPoint(50, 50));
    (void)tb->isInMircastWidget(QPoint(500, 500));

    tb->updateMircastWidget(QPoint(300, 300));
    tb->hideMircastWidget();
    QTest::qWait(20);
    delete tb;
}

// Playlist-aware methods need a toolbox whose m_pPlaylist is wired up — only the
// shared main-window toolbox has that (MainWindow::loadPlayList -> setPlaylist).
// clearPlayListFocus / setBtnFocusSign / getListBtnFocus / getMouseTime (~2403-2423).
TEST(ToolBox, playlistFocusAndFlags)
{
    MainWindow *w = dApp->getMainWindow();
    ToolboxProxy *tb = w->toolbox();
    ASSERT_TRUE(tb);

    tb->setBtnFocusSign(true);
    EXPECT_TRUE(tb->m_bSetListBtnFocus);
    tb->clearPlayListFocus();      // reads isFocusInPlaylist, resets flag
    EXPECT_FALSE(tb->m_bSetListBtnFocus);

    (void)tb->getListBtnFocus();
    (void)tb->getMouseTime();
    QTest::qWait(10);
}

