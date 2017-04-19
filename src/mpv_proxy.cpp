#include "mpv_proxy.h"
#include <mpv/client.h>

namespace dmr {
using namespace mpv::qt;

static void mpv_callback(void *d)
{
    MpvProxy *mpv = static_cast<MpvProxy*>(d);
    emit mpv->has_mpv_events();
}

MpvProxy::MpvProxy(QWidget *parent)
    :QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);

    qDebug() << "proxy hook winId " << this->winId();
    _handle = Handle::FromRawHandle(mpv_init());
    connect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events, Qt::QueuedConnection);
}

MpvProxy::~MpvProxy()
{
}

mpv_handle* MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();
    /*        - config
     *        - config-dir
     *        - input-conf
     *        - load-scripts
     *        - script
     *        - player-operation-mode
     *        - input-app-events (OSX)
     *      - all encoding mode options
     */

    set_property(h, "vo", "opengl");
    set_property(h, "wid", this->winId());
    mpv_initialize(h);

    mpv_set_wakeup_callback(h, mpv_callback, this);

    mpv_observe_property(h, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(h, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "chapter-list", MPV_FORMAT_NODE);

    mpv_observe_property(h, 0, "pause", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "core-idle", MPV_FORMAT_NODE);

    mpv_request_log_messages(h, "info");
    return h;
}

void MpvProxy::handle_mpv_events()
{
    while (1) {
        mpv_event* ev = mpv_wait_event(_handle, 0);
        if (ev->event_id == MPV_EVENT_NONE) 
            break;

        switch (ev->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                process_log_message((mpv_event_log_message*)ev);
                break;

            case MPV_EVENT_PROPERTY_CHANGE:
                process_property_change((mpv_event_property*)ev);
                break;


            default:
                break;
        }
    }
}

void MpvProxy::process_log_message(mpv_event_log_message* ev)
{
    qDebug() << QString("%1:%2: %3").arg(ev->prefix).arg(ev->level).arg(ev->text);
}

void MpvProxy::process_property_change(mpv_event_property* ev)
{
    if (ev->format == MPV_FORMAT_NONE || ev->data == NULL)
        return;

    QString name = QString::fromUtf8(ev->name);
    if (name == "time-pos") {
        double time = *(double *)ev->data;
    }
}

void MpvProxy::play()
{
    if (_playlist.size()) {
        QList<QVariant> args = { "loadfile", _playlist[0].canonicalFilePath() };
        qDebug () << args;
        command(_handle, args);
    }
}

void MpvProxy::pause()
{
}

void MpvProxy::stop()
{
}

void MpvProxy::seekForward(int secs)
{
    QList<QVariant> args = { "seek", QVariant(secs) };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::seekBackward(int secs)
{
    if (secs > 0) secs = -secs;
    QList<QVariant> args = { "seek", QVariant(secs) };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::addPlayFile(const QFileInfo& fi)
{
    if (fi.exists()) _playlist.append(fi);
}

qint64 MpvProxy::duration() const
{
    return get_property(_handle, "duration").value<qint64>();
}


qint64 MpvProxy::ellapsed() const
{
    return get_property(_handle, "time-pos").value<qint64>();
}

} // end of namespace dmr
