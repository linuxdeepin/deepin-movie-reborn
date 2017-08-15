#include "notification_widget.h"
#include "utility.h"

#include <DPlatformWindowHandle>

namespace dmr {

NotificationWidget::NotificationWidget(QWidget *parent)
    :DBlurEffectWidget(NULL), _mw(parent)
{
    setWindowFlags(Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground, true);

    _frame = new QFrame(this);
    _frame->setObjectName("NotificationFrame");
    setStyleSheet(R"( 
        #NotificationFrame {
            background-color: rgba(23, 23, 23, 0.8);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 4px;
        })");
    _layout = new QHBoxLayout;
    _frame->setLayout(_layout);

    auto hl = new QHBoxLayout;
    hl->setContentsMargins(0, 0, 0, 0);
    setLayout(hl);
    hl->addWidget(_frame);

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

void NotificationWidget::showEvent(QShowEvent *event)
{
    auto geom = _mw->frameGeometry();
    if (_anchor == AnchorBottom) {
        move(geom.center().x() - size().width()/2, geom.bottom() - _anchorDist - height());
    } else {
        move(geom.center().x() - size().width()/2, geom.center().y() - size().height()/2);
    }
}

void NotificationWidget::popupWithIcon(const QString& msg, const QPixmap& pm)
{
    _msgLabel->setText(msg);
    _icon->setPixmap(pm);

    _layout->setContentsMargins(12, 8, 12, 8);
    if (_layout->indexOf(_icon) == -1)
        _layout->addWidget(_icon);
    if (_layout->indexOf(_msgLabel) == -1)
        _layout->addWidget(_msgLabel);

    setFixedSize(300, 43);
    
    show();
    _timer->start();
    Utility::setStayOnTop(this, true);
}

void NotificationWidget::popup(const QString& msg)
{
    _msgLabel->setStyleSheet("font-size: 14px");
    _msgLabel->setText(msg);
    _layout->setContentsMargins(16, 9, 16, 9);
    if (_layout->indexOf(_msgLabel) == -1)
        _layout->addWidget(_msgLabel);

    this->ensurePolished();

    show();
    _timer->start();
    Utility::setStayOnTop(this, true);
}

void NotificationWidget::updateWithMessage(const QString& newMsg)
{
    if (isVisible()) {
        _timer->start();
        _msgLabel->setText(newMsg);

        auto geom = _mw->frameGeometry();
        //if (_anchor == AnchorBottom) {
            //move(geom.center().x() - size().width()/2, geom.bottom() - _anchorDist);
        //} else {
            //move(geom.center().x() - size().width()/2, geom.center().y() - size().height()/2);
        //}
    } else {
        popup(newMsg);
    }
}

}
