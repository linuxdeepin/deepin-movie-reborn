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
#include "config.h"
#include "titlebar.h"

#include <QtGui>

#include <DThemeManager>

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

void Titlebar::paintEvent(QPaintEvent *pe)
{
    Q_D(const Titlebar);

    auto radius = RADIUS;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto titleBarHeight = this->height();
    QRectF r = rect();
    p.fillRect(r, Qt::transparent);

    QRectF topLeftRect(r.topLeft(), QSize(2 * radius, 2 * radius));
    QRectF topRightRect(QPoint(r.right() - 2 * radius, r.y()),
                        QSize(2 * radius, 2 * radius));

    QPainterPath titleBorder;
    titleBorder.moveTo(r.x() + radius, r.y());
    titleBorder.lineTo(r.x() + r.width() - radius, r.y());
    titleBorder.arcTo(topRightRect, 90.0, -90.0);
    titleBorder.lineTo(r.x() + r.width(), r.y() + radius);
    titleBorder.lineTo(r.x() + r.width(), r.y() + titleBarHeight);
    titleBorder.lineTo(r.x(), r.y() + titleBarHeight);
    titleBorder.lineTo(r.x() , r.y() + radius);
    titleBorder.arcTo(topLeftRect, 180.0, -90.0);
    titleBorder.closeSubpath();

    p.setClipPath(titleBorder);
    p.fillPath(titleBorder, QBrush(d->titleBackground));

    QLine line(r.topLeft().x(), r.y() + titleBarHeight,
               r.x() + r.width(), r.y() + titleBarHeight);
    p.setPen(QPen(d->borderBottom, 1.0));
    p.drawLine(line);

    QLine lineOut(r.topLeft().x()+radius, r.y(),
                  r.x() + r.width()-radius, r.y());
    p.setPen(QPen(d->borderShadowTop, 1.0));
    p.drawLine(lineOut);
}

}

