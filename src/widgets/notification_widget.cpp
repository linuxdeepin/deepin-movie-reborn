#include "notification_widget.h"
#include "utility.h"
#include "event_relayer.h"

#include <DPlatformWindowHandle>
#include <dthememanager.h>
#include <dapplication.h>

namespace dmr {

NotificationWidget::NotificationWidget(QWidget *parent)
    :QWidget(parent), _mw(parent)
{
    DThemeManager::instance()->registerWidget(this);

    _frame = new QFrame(this);
    _frame->setFrameShape(QFrame::NoFrame);
    _layout = new QHBoxLayout;
    _frame->setObjectName("NotificationFrame");
    _frame->setLayout(_layout);

    auto hl = new QHBoxLayout;
    hl->setContentsMargins(0, 0, 0, 0);
    setLayout(hl);
    hl->addWidget(_frame);

    _icon = new QLabel;
    _icon->setFrameShape(QFrame::NoFrame);
    _msgLabel = new QLabel();

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
    }
    adjustSize();
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
    _msgLabel->setText(msg);
    _icon->setPixmap(pm);

    _layout->setContentsMargins(12, 6, 12, 6);
    if (_layout->indexOf(_icon) == -1)
        _layout->addWidget(_icon);
    if (_layout->indexOf(_msgLabel) == -1)
        _layout->addWidget(_msgLabel);

    setFixedSize(300, 40);
    
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

}
