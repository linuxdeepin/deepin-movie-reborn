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
    blockSignals(false);
    QSlider::mouseReleaseEvent(e);
    emit sliderMoved(sliderPosition());
    _down = false;
}

void DMRSlider::mousePressEvent(QMouseEvent *e)
{
    QSlider::mousePressEvent(e);
    blockSignals(true);

    int v = 0;
    if (orientation() == Qt::Horizontal) {
        v = (maximum() - minimum()) * e->x() / width() + minimum();
    } else {
        v = (maximum() - minimum()) * (height() - e->y()) / height() + minimum();
    }
    setSliderPosition(v);
    _down = true;
}

void DMRSlider::mouseMoveEvent(QMouseEvent *e)
{
    int v = 0;
    if (orientation() == Qt::Horizontal) {
        v = (maximum() - minimum()) * e->x() / width() + minimum();
    } else {
        v = (maximum() - minimum()) * (height() - e->y()) / height() + minimum();
    }

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
    e->accept();
}

}

