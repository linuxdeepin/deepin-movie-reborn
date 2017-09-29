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
    if (isVisible()) {
        _msgLabel->setText(newMsg);
        resize(_msgLabel->sizeHint().width() + _layout->contentsMargins().left() 
                + _layout->contentsMargins().right(), height());
        adjustSize();
        _timer->start();

    } else {
        popup(newMsg);
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
