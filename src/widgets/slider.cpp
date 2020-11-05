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

#include <DThemeManager>
#include <DApplication>
#include <QProcess>

#define TOOLBOX_TOP_EXTENT 12

DWIDGET_USE_NAMESPACE

namespace dmr {
static auto light_style = R"(
    #MovieProgress[Hover="true"]::groove:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%1 transparent,
            stop:%2  rgba(252, 252, 252, 0.88),  stop:0.50000 rgba(252, 252, 252, 0.88),
            stop:0.50001  rgba(0, 0, 0, 0.0), stop:1 rgba(0, 0, 0, 0.0)
        );
        position: absolute;
        left: 0px; right: 0px;
    }
    
    #MovieProgress[Hover="true"]::add-page:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%3 transparent,
            stop:%4  rgba(0, 0, 0, 0.1),   stop:%5 rgba(0, 0, 0, 0.1),
            stop:%6  rgba(252, 252, 252, 0.08),  stop:%7 rgba(252, 252, 252, 0.08),
            stop:%8  rgba(0, 0, 0, 0.0),   stop:1 rgba(0, 0, 0, 0.0)
        );
    }
    
    #MovieProgress[Hover="true"]::sub-page:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%1 transparent,
            stop:%2  #2eacff,              stop:0.58333 #2eacff,
            stop:0.58334  rgba(0, 0, 0, 0.0), stop:1 rgba(0, 0, 0, 0.0)
        );
    }
    )";

static auto dark_style = R"(
    #MovieProgress[Hover="true"]::groove:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%1 transparent,
            stop:%2  rgba(16, 16, 16, 0.8),  stop:0.50000 rgba(16, 16, 16, 0.8),
            stop:0.50001  rgba(0, 0, 0, 0.0), stop:1 rgba(0, 0, 0, 0.0)
        );
        position: absolute;
        left: 0px; right: 0px;
    }
    
    #MovieProgress[Hover="true"]::add-page:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%3 transparent,
            stop:%4  rgba(0, 0, 0, 0.5),   stop:%5 rgba(0, 0, 0, 0.5),
            stop:%6  rgba(0, 0, 0, 0.03),  stop:%7 rgba(0, 0, 0, 0.03),
            stop:%8  rgba(0, 0, 0, 0.0),   stop:1 rgba(0, 0, 0, 0.0)
        );
    }
    
    #MovieProgress[Hover="true"]::sub-page:horizontal {
        background-color: qlineargradient(x1:0 y1:0, x2:0 y2:1,
            stop:0.00000  transparent,          stop:%1 transparent,
            stop:%2  #2eacff,              stop:0.58333 #2eacff,
            stop:0.58334  rgba(0, 0, 0, 0.0), stop:1 rgba(0, 0, 0, 0.0)
        );
    }
    )";

DMRSlider::DMRSlider(QWidget *parent): DSlider(Qt::Horizontal, parent)
{
    slider()->setTracking(false);
    slider()->setMouseTracking(true);
    setMouseTracking(true);

//    auto updateTheme = [=]() {
//        if (qApp->theme() == "dark") {
//            _indicatorColor = QColor("#ffffff");
//            _style_tmpl = dark_style;
//        } else {
//            _indicatorColor = QColor("#303030");
//            _style_tmpl = light_style;
//        }
//    };
//    connect(DThemeManager::instance(), &DThemeManager::themeChanged, updateTheme);
//    updateTheme();

//    setProperty("Hover", "false");
//    setStyleSheet(styleSheet());
}

void DMRSlider::setEnableIndication(bool on)
{
    if (_indicatorEnabled != on) {
        _indicatorEnabled = on;
        update();
    }
}

DMRSlider::~DMRSlider()
{
}

void DMRSlider::mouseReleaseEvent(QMouseEvent *e)
{
    if (_down) {
        //emit sliderMoved(sliderPosition());
        _down = false;
        QWidget::mouseReleaseEvent(e);
    }
}

int DMRSlider::position2progress(const QPoint &p)
{
    auto total = (maximum() - minimum());

    if (orientation() == Qt::Horizontal) {
        qreal span = static_cast<qreal>(total) / contentsRect().width();
        return static_cast<int>(span * (p.x()) + minimum());
    } else {
        qreal span = static_cast<qreal>(total) / contentsRect().height();
        return static_cast<int>(span * (height() - p.y()) + minimum());
    }
}

void DMRSlider::mousePressEvent(QMouseEvent *e)
{
    auto systemEnv = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));

    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
        return ;
    }

    if (e->buttons() == Qt::LeftButton && isEnabled()) {
        QWidget::mousePressEvent(e);

        int v = position2progress(e->pos());;
        //wayland 此处注释
        //slider()->setSliderPosition(v);
        emit sliderMoved(v);
        _down = true;
    }
}

void DMRSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (!isEnabled()) return;

    int v = position2progress(e->pos());
    if (_down) {
       //wayland 此处注释
       // slider()->setSliderPosition(v);
        if (_showIndicator) {
            _indicatorPos = {e->x(), pos().y() + TOOLBOX_TOP_EXTENT - 4};
            update();
        }
    } else {
        // a mouse enter from previewer happens
        if (_indicatorEnabled && !property("Hover").toBool()) {
            setProperty("Hover", "true");
//            startAnimation(false);
            _showIndicator = true;
            update();
        }
        emit enter();

        if (_lastHoverValue != v) {
            if (_showIndicator) {
                _indicatorPos = {e->x(), pos().y() + TOOLBOX_TOP_EXTENT - 4};
                update();
            }

            emit hoverChanged(v);
        }

        _lastHoverValue = v;
    }
    e->accept();
}

void DMRSlider::leaveEvent(QEvent *e)
{
    if (_indicatorEnabled) {
//        startAnimation(true);

        _showIndicator = false;
        update();
    }

    //HACK: workaround problem that preview will make slider leave
    auto pos = mapFromGlobal(QCursor::pos());
    if (pos.y() > 0 && pos.y() < 6) {
        // preview may popup
        return;
    }

    _lastHoverValue = 0;
    if (_down) _down = false;

    emit leave();
    if (e) e->accept();
}

void DMRSlider::forceLeave()
{
    leaveEvent(nullptr);
}

/*not used yet*/
/*void DMRSlider::onAnimationStopped()
{
    // need to clear stylesheet when leave slider, since the generated sheet is a
    // little weird.
    if (_hoverAni && _hoverAni->state() == QVariantAnimation::Stopped) {
        setProperty("Hover", "false");
//        setStyleSheet("");
        update();
    }
}*/

/*not used yet*/
/*void DMRSlider::onValueChanged(const QVariant &v)
{
    // see dmr--ToolProxy.theme to find out the meaning of these values
    // v1 is for groove and sub-page
    // v2 is for add-page
    double v1 = (1.0 - v.toDouble()) * 0.500000 + v.toDouble() * (1 / 3.0);

    double v2 = (1.0 - v.toDouble()) * 0.500000 + v.toDouble() * (1 / 3.0);
    double v3 = v2 + (1.0 / 24.0);
    double v4 = v2 + (2.0 / 24.0);

    auto s = QString::fromUtf8(_style_tmpl)
             .arg(v1).arg(v1 + 0.000001)
             .arg(v2).arg(v2 + 0.000001)
             .arg(v3).arg(v3 + 0.000001)
             .arg(v4).arg(v4 + 0.000001);
    //qDebug() << "-------- interpolate " << v1 << v2 << v3 << v4;
//    setStyleSheet(s);
    update();

}*/

/*void DMRSlider::startAnimation(bool reverse)
{
    if (_hoverAni) {
        _hoverAni->stop();
        _hoverAni.clear();
    }
    _hoverAni = new QVariantAnimation(this);
    if (reverse) {
        _hoverAni->setStartValue(1.0);
        _hoverAni->setEndValue(0.0);
        _hoverAni->setEasingCurve(QEasingCurve::InCubic);
        connect(_hoverAni, &QVariantAnimation::stateChanged, this, &DMRSlider::onAnimationStopped);
    } else {
        _hoverAni->setStartValue(0.0);
        _hoverAni->setEndValue(1.0);
        _hoverAni->setEasingCurve(QEasingCurve::OutCubic);
    }
    connect(_hoverAni, &QVariantAnimation::valueChanged, this, &DMRSlider::onValueChanged);
    _hoverAni->setDuration(150);
    _hoverAni->start(QVariantAnimation::DeleteWhenStopped);
}*/

void DMRSlider::enterEvent(QEvent *e)
{
    if (_indicatorEnabled) {
        if (property("Hover") != "true") {
            setProperty("Hover", "true");
//            startAnimation(false);
            _showIndicator = true;
            update();
        }
    }
    emit enter();
    e->accept();
}

void DMRSlider::wheelEvent(QWheelEvent *e)
{
    if (e->buttons() == Qt::MiddleButton && e->modifiers() == Qt::NoModifier) {
        qDebug() << "angleDelta" << e->angleDelta();
    }
    e->accept();
}

void DMRSlider::paintEvent(QPaintEvent *e)
{
    QWidget::paintEvent(e);

//    if (_indicatorEnabled && _showIndicator) {
//        QPainter p(this);
//        QRect r(_indicatorPos, QSize{1, 6});
//        p.fillRect(r, QBrush(_indicatorColor));
//    }
}

}

