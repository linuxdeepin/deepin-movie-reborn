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
    _icon->setPixmap(QPixmap(":/resources/icons/success.png"));
    hl->addWidget(_icon);

    _msgLabel = new QLabel();
    hl->addWidget(_msgLabel);

}

void NotificationWidget::popup(const QString& msg, bool success)
{
    _msgLabel->setText(msg);

    setBlendMode(DBlurEffectWidget::BehindWindowBlend);
    setRadius(30);
    setFixedSize(300, 43);
    setBlurRectXRadius(4);
    setBlurRectYRadius(4);
    setMaskColor(Qt::black);

    _icon->setPixmap(QPixmap(QString(":/resources/icons/%1.png").arg(success?"success":"fail")));

    auto geom = _mw->frameGeometry();
    move(geom.center().x() - size().width()/2, geom.bottom() - 110);
    QTimer::singleShot(2000, [=]() {this->deleteLater();});
    show();
    Utility::setStayOnTop(this, true);
}

}
