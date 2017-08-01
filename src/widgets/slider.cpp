#include "slider.h"

namespace dmr
{
DMRSlider::DMRSlider(QWidget *parent): QSlider(parent) 
{
    setTracking(false);
}

void DMRSlider::mouseReleaseEvent(QMouseEvent *e)
{
    blockSignals(false);
    QSlider::mouseReleaseEvent(e);
    emit sliderMoved(sliderPosition());
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
}

void DMRSlider::mouseMoveEvent(QMouseEvent *e)
{
    int v = 0;
    if (orientation() == Qt::Horizontal) {
        v = (maximum() - minimum()) * e->x() / width() + minimum();
    } else {
        v = (maximum() - minimum()) * (height() - e->y()) / height() + minimum();
    }
    setSliderPosition(v);
}

void DMRSlider::wheelEvent(QWheelEvent *e)
{
    e->accept();
}

}

