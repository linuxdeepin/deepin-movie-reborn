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
#include "slider.h"

#include <dthememanager.h>
#include <DApplication>

#define TOOLBOX_TOP_EXTENT 12

DWIDGET_USE_NAMESPACE

namespace dmr
{
DMRSlider::DMRSlider(QWidget *parent): QSlider(parent) 
{
    setTracking(false);
    setMouseTracking(true);
    _indicator = new QWidget();
    _indicator->setWindowFlags(Qt::ToolTip);
    _indicator->setFixedSize(1, 6);

    auto updateTheme = [=]() {
        if (qApp->theme() == "dark") {
            _indicator->setStyleSheet(R"(
                background: #ffffff;
            )");
        } else {
            _indicator->setStyleSheet(R"(
                background: #303030;
            )");
        }
    };
    connect(DThemeManager::instance(), &DThemeManager::themeChanged, updateTheme);
    updateTheme();

    _indicator->hide();
}

void DMRSlider::setEnableIndication(bool on)
{
    if (_showIndicator != on) {
        _showIndicator = on;
        if (!on) {
            _indicator->hide();
        }
    }
}

DMRSlider::~DMRSlider()
{
    delete _indicator;
}

void DMRSlider::mouseReleaseEvent(QMouseEvent *e)
{
    if (_down) {
        //emit sliderMoved(sliderPosition());
        _down = false;
        QSlider::mouseReleaseEvent(e);
    }
}

int DMRSlider::position2progress(const QPoint& p)
{
    auto total = (maximum() - minimum());

    if (orientation() == Qt::Horizontal) {
        qreal span = (qreal)total / contentsRect().width();
        return span * (p.x()) + minimum();
    } else {
        qreal span = (qreal)total / contentsRect().height();
        return span * (height() - p.y()) + minimum();
    }
}

void DMRSlider::mousePressEvent(QMouseEvent *e)
{
    if (e->buttons() == Qt::LeftButton && isEnabled()) {
        QSlider::mousePressEvent(e);

        int v = position2progress(e->pos());;
        setSliderPosition(v);
        _down = true;
    }
}

void DMRSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (!isEnabled()) return;

    int v = position2progress(e->pos());;
    if (_down) {
        setSliderPosition(v);
        if (_showIndicator) {
            _indicator->show();
            _indicator->move(e->globalX(), mapToGlobal(pos()).y()+TOOLBOX_TOP_EXTENT-2);
        }
    } else {
        if (_lastHoverValue != v) {
            if (_showIndicator) {
                _indicator->show();
                _indicator->move(e->globalX(), mapToGlobal(pos()).y()+TOOLBOX_TOP_EXTENT-2);
            }
            emit hoverChanged(v);
        }

        _lastHoverValue = v;
    }
}

void DMRSlider::leaveEvent(QEvent *e)
{
    _lastHoverValue = 0;
    if (_down) _down = false;
    _indicator->hide();
    emit leave();
}

void DMRSlider::wheelEvent(QWheelEvent *e)
{
    if (e->buttons() == Qt::MiddleButton && e->modifiers() == Qt::NoModifier) {
        qDebug() << "angleDelta" << e->angleDelta();
    }
    e->accept();
}

}

