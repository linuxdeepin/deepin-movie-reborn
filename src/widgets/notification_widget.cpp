/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
#include "notification_widget.h"
#include "utility.h"
#include "event_relayer.h"

#include <DPlatformWindowHandle>
#include <dthememanager.h>
#include <dapplication.h>

namespace dmr {

NotificationWidget::NotificationWidget(QWidget *parent)
    :QFrame(parent), _mw(parent)
{
    DThemeManager::instance()->registerWidget(this);

    //setFrameShape(QFrame::NoFrame);
    setObjectName("NotificationFrame");

    _layout = new QHBoxLayout;
    _layout->setContentsMargins(0, 0, 0, 0);
    setLayout(_layout);

    _msgLabel = new QLabel();
    _msgLabel->setFrameShape(QFrame::NoFrame);

    _timer = new QTimer(this);
    _timer->setInterval(2000);
    _timer->setSingleShot(true);
    connect(_timer, &QTimer::timeout, [=]() {this->hide();});

}

void NotificationWidget::showEvent(QShowEvent *event)
{
    ensurePolished();
    if (_layout->indexOf(_icon) == -1) {
        resize(_msgLabel->sizeHint().width() + _layout->contentsMargins().left() 
                + _layout->contentsMargins().right(), height());
        adjustSize();
    }
    syncPosition();
}

void NotificationWidget::resizeEvent(QResizeEvent *re)
{
}

void NotificationWidget::syncPosition()
{
    auto geom = _mw->geometry();
    switch (_anchor) {
        case AnchorBottom:
            move(geom.center().x() - size().width()/2, geom.bottom() - _anchorDist - height());
            break;

        case AnchorNorthWest:
            move(_anchorPoint);
            break;

        case AnchorNone:
            move(geom.center().x() - size().width()/2, geom.center().y() - size().height()/2);
            break;
    }
}

void NotificationWidget::popupWithIcon(const QString& msg, const QPixmap& pm)
{
    if (!_icon) {
        _icon = new QLabel;
        _icon->setFrameShape(QFrame::NoFrame);
    }
    _icon->setPixmap(pm);

    _layout->setContentsMargins(12, 6, 12, 6);
    if (_layout->indexOf(_icon) == -1)
        _layout->addWidget(_icon);
    if (_layout->indexOf(_msgLabel) == -1)
        _layout->addWidget(_msgLabel, 1);

    setFixedHeight(40);
    _layout->update();
    _msgLabel->setText(msg);
    show();
    raise();
    _timer->start();
}

void NotificationWidget::popup(const QString& msg)
{
    _layout->setContentsMargins(14, 4, 14, 4);
    if (_layout->indexOf(_msgLabel) == -1) {
        _layout->addWidget(_msgLabel);
    }
    setFixedHeight(30);
    _msgLabel->setText(msg);
    show();
    raise();
    _timer->start();
}

void NotificationWidget::updateWithMessage(const QString& newMsg)
{
    QFont ft;
    ft.setPixelSize(12);
    QFontMetrics fm(ft);
    auto msg = fm.elidedText(newMsg, Qt::ElideMiddle, _mw->width() - 12 - 12 - 60);

    if (isVisible()) {
        _msgLabel->setText(msg);
        resize(_msgLabel->sizeHint().width() + _layout->contentsMargins().left() 
                + _layout->contentsMargins().right(), height());
        adjustSize();
        _timer->start();

    } else {
        popup(msg);
    }
}

void NotificationWidget::paintEvent(QPaintEvent* pe)
{
    float RADIUS = 4;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    bool light = ("light" == qApp->theme());
    auto bg_clr = QColor(23, 23, 23, 255 * 8 / 10);
    auto border_clr = QColor(255, 255, 255, 25);
    if (light) {
        bg_clr = QColor(252, 252, 252, 255 * 8 / 10);
        border_clr = QColor(0, 0, 0, 25);
    }

    p.fillRect(rect(), Qt::transparent);
    {
        QPainterPath pp;
        pp.addRoundedRect(rect(), RADIUS, RADIUS);
        p.setPen(border_clr);
        p.drawPath(pp);
    }

    auto view_rect = rect().marginsRemoved(QMargins(1, 1, 1, 1));
    QPainterPath pp;
    pp.addRoundedRect(view_rect, RADIUS, RADIUS);
    p.fillPath(pp, bg_clr);
}

}
