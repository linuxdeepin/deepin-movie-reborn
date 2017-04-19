#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>

#include <QtCore>
#include <QX11Info>

#include "event_relayer.h"

namespace dmr {

EventRelayer::EventRelayer(QWindow* src, QWindow *dest)
    :QObject(), QAbstractNativeEventFilter(), _source(src), _target(dest) {
    int screen = 0;
    xcb_screen_t *s = xcb_aux_get_screen (QX11Info::connection(), screen);
    const uint32_t data[] = { 
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };
    xcb_change_window_attributes (QX11Info::connection(), _source->winId(),
            XCB_CW_EVENT_MASK, data);

    qApp->installNativeEventFilter(this);
}

EventRelayer::~EventRelayer() {
    qApp->removeNativeEventFilter(this);
}

bool EventRelayer::nativeEventFilter(const QByteArray &eventType, void *message, long *) {
    if(Q_LIKELY(eventType == "xcb_generic_event_t")) {
        xcb_generic_event_t* event = static_cast<xcb_generic_event_t *>(message);
        switch (event->response_type & ~0x80) {
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *cne = (xcb_configure_notify_event_t*)event;
                if (cne->window != _source->winId())
                    return false;

                QPoint p(cne->x, cne->y);
                if (p != _target->framePosition()) {
                    qDebug() << "cne: " << QRect(cne->x, cne->y, cne->width, cne->height)
                        << "origin: " << _source->framePosition()
                        << "dest: " << _target->framePosition();
                    emit targetNeedsUpdatePosition(QPoint(cne->x, cne->y));
                }
                break;
            }
            default:
                break;
        }
    }

    return false;
}

}

