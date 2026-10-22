// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#undef Bool
#include <QCursor>
#include "ddlog.h"
#include <QDebug>

#include "event_monitor.h"
#define Bool int
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/extensions/record.h>
#undef Bool

//cppcheck 修改
void callback(XPointer ptr, XRecordInterceptData *pData)
{
    qCDebug(appMovie) << "Entering global callback function.";
    (reinterpret_cast<dmr::EventMonitor *>(ptr))->handleRecordEvent(pData);
    qCDebug(appMovie) << "Exiting global callback function.";
}

namespace dmr {

EventMonitor::EventMonitor(QObject *parent) : QThread(parent)
{
    qCDebug(appMovie) << "Entering EventMonitor constructor.";
    m_bIsPress = false;
}

//cppcheck 误报
void EventMonitor::run()
{
    qCDebug(appMovie) << "EventMonitor::run() started. Attempting to open X display.";
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        qCWarning(appMovie) << "Error: unable to open display in EventMonitor::run().";
        fprintf(stderr, "unable to open display\n");
        return;
    }
    // Receive from ALL clients, including future clients.
    //XRecordClientSpec clients = XRecordAllClients;
    XRecordClientSpec clients = XRecordCurrentClients;
    qCDebug(appMovie) << "XRecordClientSpec set to XRecordCurrentClients.";
    XRecordRange *pRange = XRecordAllocRange();
    if (pRange == nullptr) {
        qCWarning(appMovie) << "Error: unable to allocate XRecordRange in EventMonitor::run().";
        fprintf(stderr, "unable to allocate XRecordRange\n");
        return;
    }
    qCDebug(appMovie) << "XRecordRange allocated.";

    memset(pRange, 0, sizeof(XRecordRange));
    pRange->device_events.first = ButtonPress;
    pRange->device_events.last  = MotionNotify;
    qCDebug(appMovie) << "XRecordRange configured for ButtonPress to MotionNotify.";

    // And create the XRECORD context.
    XRecordContext context = XRecordCreateContext(display, 0, &clients, 1, &pRange, 1);
    if (context == 0) {
        qCWarning(appMovie) << "Error: XRecordCreateContext failed in EventMonitor::run().";
        fprintf(stderr, "XRecordCreateContext failed\n");
        return;
    }
    qCDebug(appMovie) << "XRecordContext created successfully.";
    XFree(pRange);

    XSync(display, True);
    qCDebug(appMovie) << "X display synchronized.";

    Display *display_datalink = XOpenDisplay(nullptr);
    if (display_datalink == nullptr) {
        qCWarning(appMovie) << "Error: unable to open second display for datalink in EventMonitor::run().";
        fprintf(stderr, "unable to open second display\n");
        return;
    }
    qCDebug(appMovie) << "Second X display for datalink opened successfully.";

    if (!XRecordEnableContext(display_datalink, context,  callback, (XPointer) this)) {
        qCWarning(appMovie) << "Error: XRecordEnableContext() failed in EventMonitor::run().";
        fprintf(stderr, "XRecordEnableContext() failed\n");
        return;
    }

    qCDebug(appMovie) << "EventMonitor::run() finished.";
}

void EventMonitor::handleRecordEvent(void *pValue)
{
    qCDebug(appMovie) << "Entering EventMonitor::handleRecordEvent. pValue:" << pValue;
    XRecordInterceptData *pData = (XRecordInterceptData *)pValue;
    if (!m_recording) {
        qCDebug(appMovie) << "EventMonitor: Recording suspended. Freeing data.";
        XRecordFreeData(pData);
        return;
    }

    if (pData->category == XRecordFromServer) {
        xEvent *event = (xEvent *)pData->data;
        qCDebug(appMovie) << "EventMonitor: Received event from server. Type:" << event->u.u.type;
        switch (event->u.u.type) {
        case ButtonPress:
            qCDebug(appMovie) << "EventMonitor: ButtonPress event. Detail:" << event->u.u.detail;
            if (event->u.u.detail != WheelUp &&
                    event->u.u.detail != WheelDown &&
                    event->u.u.detail != WheelLeft &&
                    event->u.u.detail != WheelRight) {
                m_bIsPress = true;
                qCDebug(appMovie) << "EventMonitor: ButtonPress detected at (" << event->u.keyButtonPointer.rootX << "," << event->u.keyButtonPointer.rootY << "). Emitting buttonedPress.";
                emit buttonedPress(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            } else {
                qCDebug(appMovie) << "EventMonitor: Ignoring wheel button press:" << event->u.u.detail;
            }
            break;
        case MotionNotify:
            qCDebug(appMovie) << "EventMonitor: MotionNotify event.";
            if (m_bIsPress) {
                qCDebug(appMovie) << "EventMonitor: MotionNotify detected at (" << event->u.keyButtonPointer.rootX << "," << event->u.keyButtonPointer.rootY << "). Emitting buttonedDrag.";
                emit buttonedDrag(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            } else {
                qCDebug(appMovie) << "EventMonitor: Ignoring MotionNotify (no button pressed).";
            }
            break;
        case ButtonRelease:
            qCDebug(appMovie) << "EventMonitor: ButtonRelease event. Detail:" << event->u.u.detail;
            if (event->u.u.detail != WheelUp &&
                    event->u.u.detail != WheelDown &&
                    event->u.u.detail != WheelLeft &&
                    event->u.u.detail != WheelRight) {
                m_bIsPress = false;
                qCDebug(appMovie) << "EventMonitor: ButtonRelease detected at (" << event->u.keyButtonPointer.rootX << "," << event->u.keyButtonPointer.rootY << "). Emitting buttonedRelease.";
                emit buttonedRelease(event->u.keyButtonPointer.rootX, event->u.keyButtonPointer.rootY);
            } else {
                qCDebug(appMovie) << "EventMonitor: Ignoring wheel button release:" << event->u.u.detail;
            }
            break;
        default:
            qCDebug(appMovie) << "EventMonitor: Unhandled event type:" << event->u.u.type;
            break;
        }
    } else {
        qCDebug(appMovie) << "EventMonitor: Received event with category:" << pData->category << ". Not XRecordFromServer, skipping event processing logic.";
    }

    XRecordFreeData(pData);
    qCDebug(appMovie) << "EventMonitor: XRecord data freed.";
}

void EventMonitor::resumeRecording()
{
    qCDebug(appMovie) << "EventMonitor: Attempting to resume recording. Current m_recording:" << m_recording;
    if (!m_recording) {
        m_recording = 1;
        qCDebug(appMovie) << "EventMonitor: Recording resumed.";
    } else {
        qCDebug(appMovie) << "EventMonitor: Recording already active.";
    }
    qCDebug(appMovie) << "Exiting EventMonitor::resumeRecording. New m_recording:" << m_recording;
}

void EventMonitor::suspendRecording()
{
    qCDebug(appMovie) << "EventMonitor: Attempting to suspend recording. Current m_recording:" << m_recording;
    if (m_recording) {
        if (m_bIsPress) {
            m_bIsPress = false;
            QPoint pos = QCursor::pos();
            qCDebug(appMovie) << "EventMonitor: Button was pressed when suspending. Emitting buttonedRelease at (" << pos.x() << "," << pos.y() << ").";
            emit buttonedRelease(pos.x(), pos.y());
        }
        m_recording = 0;
        qCDebug(appMovie) << "EventMonitor: Recording suspended.";
    } else {
        qCDebug(appMovie) << "EventMonitor: Recording already suspended.";
    }
    qCDebug(appMovie) << "Exiting EventMonitor::suspendRecording. New m_recording:" << m_recording;
}

}

