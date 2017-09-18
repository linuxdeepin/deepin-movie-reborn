#include "slider.h"

namespace dmr
{
DMRSlider::DMRSlider(QWidget *parent): QSlider(parent) 
{
    setTracking(false);
    setMouseTracking(true);
}

void DMRSlider::mouseReleaseEvent(QMouseEvent *e)
{
    if (_down) {
        blockSignals(false);
        emit sliderMoved(sliderPosition());
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
        blockSignals(true);

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
    }

    if (_lastHoverValue != v)
        emit hoverChanged(v);
    _lastHoverValue = v;
}

void DMRSlider::leaveEvent(QEvent *e)
{
    _lastHoverValue = 0;
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

