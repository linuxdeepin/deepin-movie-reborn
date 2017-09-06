#include "titlebar.h"

#include <QDebug>
#include <QPainter>

#include <dthememanager.h>

DWIDGET_USE_NAMESPACE

namespace dmr 
{
class TitlebarPrivate
{
public:
    TitlebarPrivate(Titlebar *parent) : q_ptr(parent)
    {
        QLinearGradient linearGradient(QPointF(0.0, 0.0), QPointF(0.0, 1.0));
        linearGradient.setColorAt(0.0, QColor(255, 255, 255, 255));
        linearGradient.setColorAt(1.0,  QColor(0xf8, 0xf8, 0xf8, 255));
        titleBackground = QBrush(linearGradient);

        borderShadowTop =  QColor(0.05 * 255,  0.05 * 255,  0.05 * 255);
        borderBottom = QColor(255, 0, 0);
    }

    QBrush          titleBackground;
    QColor          borderBottom;
    QColor          borderShadowTop;
    QString         viewname;

    Titlebar *q_ptr;
    Q_DECLARE_PUBLIC(Titlebar)
};

Titlebar::Titlebar(QWidget *parent) : DTitlebar(parent), d_ptr(new TitlebarPrivate(this))
{
    Q_D(Titlebar);
    DThemeManager::instance()->registerWidget(this);
}

Titlebar::~Titlebar()
{

}

QBrush Titlebar::background() const
{
    Q_D(const Titlebar);
    return d->titleBackground;
}

QColor Titlebar::borderBottom() const
{
    Q_D(const Titlebar);
    return d->borderBottom;
}

QColor Titlebar::borderShadowTop() const
{
    Q_D(const Titlebar);
    return d->borderShadowTop;
}

void Titlebar::setBackground(QBrush titleBackground)
{
    Q_D(Titlebar);
    d->titleBackground = titleBackground;
}

void Titlebar::setBorderBottom(QColor borderBottom)
{
    Q_D(Titlebar);
    d->borderBottom = borderBottom;
}

void Titlebar::setBorderShadowTop(QColor borderShadowTop)
{
    Q_D(Titlebar);
    d->borderShadowTop = borderShadowTop;
}

void Titlebar::paintEvent(QPaintEvent *)
{
    Q_D(const Titlebar);

    auto radius = 0;
    QPainter titlePainter(this);
    titlePainter.setRenderHint(QPainter::Antialiasing);
    titlePainter.setRenderHint(QPainter::HighQualityAntialiasing);

    auto titleBarHeight = this->height();
    QRectF r = rect();
    QPointF titleTopLeft(r.x(), r.y());

    titlePainter.fillRect(r, QBrush(d->titleBackground));

    QLine line(titleTopLeft.x(), r.y() + titleBarHeight,
               r.x() + r.width(), r.y() + titleBarHeight);

    titlePainter.setPen(QPen(d->borderBottom, 1.0));
    titlePainter.drawLine(line);

    QLine lineOut(titleTopLeft.x()+radius, r.y()+1,
                  r.x() + r.width()-radius, r.y()+1);
    titlePainter.setPen(QPen(d->borderShadowTop, 1.0));
    titlePainter.drawLine(lineOut);
}

}

