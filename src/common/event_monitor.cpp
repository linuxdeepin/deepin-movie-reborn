/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#undef Bool
#include <QCursor>

#include "event_monitor.h"
#define Bool int
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/record.h>
#undef Bool

//cppcheck 修改
void callback(XPointer ptr, XRecordInterceptData *pData)
{
    (reinterpret_cast<dmr::EventMonitor *>(ptr))->handleRecordEvent(pData);
}

namespace dmr {

EventMonitor::EventMonitor(QObject *parent) : QThread(parent)
{
    m_bIsPress = false;
}

//cppcheck 误报
void EventMonitor::run()
{
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        fprintf(stderr, "unable to open display\n");
        return;
    }
    // Receive from ALL clients, including future clients.
    //XRecordClientSpec clients = XRecordAllClients;
    XRecordClientSpec clients = XRecordCurrentClients;
    XRecordRange *pRange = XRecordAllocRange();
    if (pRange == nullptr) {
        fprintf(stderr, "unable to allocate XRecordRange\n");
        return;
    }

    memset(pRange, 0, sizeof(XRecordRange));
    pRange->device_events.first = ButtonPress;
    pRange->device_events.last  = MotionNotify;

    // And create the XRECORD context.
    XRecordContext context = XRecordCreateContext(display, 0, &clients, 1, &pRange, 1);
    if (context == 0) {
        fprintf(stderr, "XRecordCreateContext failed\n");
        return;
    }
    XFree(pRange);

    XSync(display, True);

    Display *display_datalink = XOpenDisplay(nullptr);
    if (display_datalink == nullptr) {
        fprintf(stderr, "unable to open second display\n");
        return;
    }

    if (!XRecordEnableContext(display_datalink, context,  callback, (XPointer) this)) {
        fprintf(stderr, "XRecordEnableContext() failed\n");
        return;
    }
}

void EventMonitor::handleRecordEvent(void *pValue)
{
    XRecordInterceptData *pData = (XRecordInterceptData *)pValue;
    if (!m_recording) {
        XRecordFreeData(pData);
        return;
    }

    if (pData->category == XRecordFromServer) {
        xEvent *event = (xEvent *)pData->data;
        switch (event->u.u.type) {
        case ButtonPress:
            if (event->u.u.detail != WheelUp &&
                    event->u.u.detail != WheelDown &&
                    event->u.u.detail != WheelLeft &&
                    event->u.u.detail != WheelRight) {
                m_bIsPress = true;
                emit buttonedPress(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            }
            break;
        case MotionNotify:
            if (m_bIsPress) {
                emit buttonedDrag(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            }
            break;
        case ButtonRelease:
            if (event->u.u.detail != WheelUp &&
                    event->u.u.detail != WheelDown &&
                    event->u.u.detail != WheelLeft &&
                    event->u.u.detail != WheelRight) {
                m_bIsPress = false;
                emit buttonedRelease(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            }
            break;
        default:
            break;
        }
    }

    XRecordFreeData(pData);
}

void EventMonitor::resumeRecording()
{
    if (!m_recording) {
        m_recording = 1;
    }
}

void EventMonitor::suspendRecording()
{
    if (m_recording) {
        if (m_bIsPress) {
            m_bIsPress = false;
            QPoint pos = QCursor::pos();
            emit buttonedRelease(pos.x(), pos.y());
        }
        m_recording = 0;
    }
}

}

