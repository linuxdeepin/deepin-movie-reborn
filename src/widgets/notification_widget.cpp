#include "notification_widget.h"
#include "utility.h"
#include "event_relayer.h"

#include <DPlatformWindowHandle>
#include <dthememanager.h>
#include <dapplication.h>

namespace dmr {

NotificationWidget::NotificationWidget(QWidget *parent)
    :QWidget(nullptr), _mw(parent)
{
    setWindowFlags(Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground, true);

    DThemeManager::instance()->registerWidget(this);

    _blur = new DBlurEffectWidget(this);

    _frame = new QFrame(this);
    _layout = new QHBoxLayout;
    _frame->setObjectName("NotificationFrame");
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

    _blur->setBlurRectXRadius(4);
    _blur->setBlurRectYRadius(4);
    _blur->setBlendMode(DBlurEffectWidget::BehindWindowBlend);

    connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
            this, &NotificationWidget::updateBg);
    updateBg();

    winId();
    auto relay = new EventRelayer(_mw->windowHandle(), windowHandle());
    connect(relay, &EventRelayer::targetNeedsUpdatePosition, 
            this, &NotificationWidget::onMainWindowMoved);
}

void NotificationWidget::onMainWindowMoved(const QPoint& p)
{
    if (isVisible()) {
        syncPosition();
    }
}

void NotificationWidget::updateBg() 
{
    if (qApp->theme() == "dark") {
        _blur->setMaskColor(DBlurEffectWidget::DarkColor);
    } else {
        _blur->setMaskColor(DBlurEffectWidget::LightColor);
    }
}

void NotificationWidget::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    _blur->resize(size());
}

void NotificationWidget::showEvent(QShowEvent *event)
{
    ensurePolished();
    updateGeometry();
    syncPosition();
}

void NotificationWidget::syncPosition()
{
    auto geom = _mw->frameGeometry();
    switch (_anchor) {
        case AnchorBottom:
            move(geom.center().x() - size().width()/2, geom.bottom() - _anchorDist - height());
            break;

        case AnchorNorthWest:
            move(_mw->mapToGlobal(_anchorPoint));
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
    _msgLabel->setText(msg);
    _layout->setContentsMargins(16, 9, 16, 9);
    if (_layout->indexOf(_msgLabel) == -1) {
        _layout->addWidget(_msgLabel);
        _msgLabel->setStyleSheet("font-size: 14px");
    }

    show();
    _timer->start();
    Utility::setStayOnTop(this, true);
}

void NotificationWidget::updateWithMessage(const QString& newMsg)
{
    if (isVisible()) {
        _timer->start();
        resize(1, size().height());
        _msgLabel->setText(newMsg);

        auto geom = _mw->frameGeometry();
    } else {
        popup(newMsg);
    }
}

}
