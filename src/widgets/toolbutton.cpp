#include "toolbutton.h"
#include <dthememanager.h>
#include <DApplication>

namespace dmr {
VolumeButton::VolumeButton(QWidget* parent)
    : DImageButton(parent) 
{
    changeLevel(Level::High);
}

void VolumeButton::changeLevel(Level lv)
{
    if (_lv != lv) {
        switch (lv) {
            case Level::Off:
                setObjectName("VolOff"); break;
            case Level::Mute:
                setObjectName("VolMute"); break;
            case Level::Low:
                setObjectName("VolLow"); break;
            case Level::Mid:
                setObjectName("VolMid"); break;
            case Level::High:
                setObjectName("VolHigh"); break;
        }
        setStyleSheet(styleSheet());
        _lv = lv;
    }
}

void VolumeButton::enterEvent(QEvent *ev)
{
    emit entered();
}

void VolumeButton::leaveEvent(QEvent *ev)
{
    emit leaved();
}

void VolumeButton::wheelEvent(QWheelEvent* we)
{
    //qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        if (we->angleDelta().y() > 0) {
            emit requestVolumeUp();
        } else {
            emit requestVolumeDown();
        }
    }
}

}

