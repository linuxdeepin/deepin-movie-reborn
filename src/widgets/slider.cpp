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

