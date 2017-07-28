#include "notification_widget.h"
#include "utility.h"

#include <DPlatformWindowHandle>

namespace dmr {

NotificationWidget::NotificationWidget(QWidget *parent)
    :DBlurEffectWidget(NULL), _mw(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto hl = new QHBoxLayout;
    hl->setContentsMargins(12, 8, 12, 8);
    setLayout(hl);

    _icon = new QLabel;

    _msgLabel = new QLabel();

    _timer = new QTimer(this);
    _timer->setInterval(2000);
    _timer->setSingleShot(true);
    connect(_timer, &QTimer::timeout, [=]() {this->hide();});

    setBlendMode(DBlurEffectWidget::BehindWindowBlend);
    setRadius(30);
    setBlurRectXRadius(4);
    setBlurRectYRadius(4);
    setMaskColor(Qt::black);
}

void NotificationWidget::popupWithIcon(const QString& msg, const QPixmap& pm)
{
    _msgLabel->setText(msg);

    _icon->setPixmap(pm);

    if (layout()->indexOf(_icon) == -1)
        layout()->addWidget(_icon);
    if (layout()->indexOf(_msgLabel) == -1)
    layout()->addWidget(_msgLabel);

    setFixedSize(300, 43);
    auto geom = _mw->frameGeometry();
    move(geom.center().x() - size().width()/2, geom.bottom() - 110);
    show();
    _timer->start();
    Utility::setStayOnTop(this, true);
}

void NotificationWidget::popup(const QString& msg)
{
    _msgLabel->setText(msg);
    if (layout()->indexOf(_msgLabel) == -1)
        layout()->addWidget(_msgLabel);

    //FIXME: update pos when mw moved
    auto geom = _mw->frameGeometry();
    move(geom.center().x() - size().width()/2, geom.center().y() - size().height()/2);
    show();
    _timer->start();
    Utility::setStayOnTop(this, true);
}

void NotificationWidget::updateWithMessage(const QString& newMsg)
{
    _timer->stop();
    _timer->start();
    _msgLabel->setText(newMsg);

    auto geom = _mw->frameGeometry();
    move(geom.center().x() - size().width()/2, geom.center().y() - size().height()/2);
}

}
