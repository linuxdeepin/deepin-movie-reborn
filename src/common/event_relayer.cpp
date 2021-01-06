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
#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>

#include <QtCore>
#include <QX11Info>

#include "event_relayer.h"

namespace dmr {

EventRelayer::EventRelayer(QWindow *src, QWindow *dest)
    : QObject(), QAbstractNativeEventFilter(), _source(src), _target(dest)
{
#if 0
    int screen = 0;
    xcb_screen_t *s = xcb_aux_get_screen (QX11Info::connection(), screen);
    const uint32_t data[] = {
        XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
        XCB_EVENT_MASK_EXPOSURE

    };
    xcb_change_window_attributes (QX11Info::connection(), _source->winId(),
                                  XCB_CW_EVENT_MASK, data);

#endif
    qApp->installNativeEventFilter(this);
}

EventRelayer::~EventRelayer()
{
    qApp->removeNativeEventFilter(this);
}

bool EventRelayer::nativeEventFilter(const QByteArray &eventType, void *message, long *)
{
    if (Q_LIKELY(eventType == "xcb_generic_event_t")) {
        xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
        switch (event->response_type & ~0x80) {
        case XCB_CONFIGURE_NOTIFY: {
            xcb_configure_notify_event_t *cne = reinterpret_cast<xcb_configure_notify_event_t *>(event);
            if (cne->window == _source->winId()) {
                QPoint p(cne->x, cne->y);
                if (p != _target->framePosition()) {
                    //qInfo() << "cne: " << QRect(cne->x, cne->y, cne->width, cne->height)
                    //<< "origin: " << _source->framePosition()
                    //<< "dest: " << _target->framePosition();
                    emit targetNeedsUpdatePosition(QPoint(cne->x, cne->y));
                }
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

