/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/notification_widget.h"
#include <gtest/gtest.h>
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <DSlider>
#include <DListWidget>
#include <QMenu>
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

//TEST(ToolBox, animationLabel)
//{
//    MainWindow *mw = new MainWindow();
//    AnimationLabel *aLabel = new AnimationLabel(mw, mw);
//    aLabel->show();

//    QEvent moveEvent(QEvent::Move);
//    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, QPoint(0,0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
//    QApplication::sendEvent(aLabel, &moveEvent);
//    QApplication::sendEvent(aLabel, &releaseEvent);
//}

