#include "mpv_proxy.h"
#include "mpv_glwidget.h"
#include "compositing_manager.h"
#include <mpv/client.h>

#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>
#include <QX11Info>

namespace dmr {
using namespace mpv::qt;

class EventRelayer2: public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    EventRelayer2(QWindow* src, QWindow *dest)
        :QObject(), QAbstractNativeEventFilter(), _source(src), _target(dest) {
            xcb_connection_t *conn = QX11Info::connection();
            int screen = 0;
            xcb_screen_t *s = xcb_aux_get_screen(conn, screen);

            auto cookie = xcb_get_window_attributes(conn, _source->winId());
            auto reply = xcb_get_window_attributes_reply(conn, cookie, NULL);

            const uint32_t data[] = { 
                reply->your_event_mask |
                    XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                        XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_BUTTON_PRESS |
                        XCB_EVENT_MASK_POINTER_MOTION

            };
            xcb_change_window_attributes (QX11Info::connection(), _source->winId(),
                    XCB_CW_EVENT_MASK, data);

            xcb_aux_sync(QX11Info::connection());
            qApp->installNativeEventFilter(this);
        }

    ~EventRelayer2() {
        qApp->removeNativeEventFilter(this);
    }

    QWindow *_source, *_target;

signals:
    void subwindowCreated(xcb_window_t winid);
    void subwindowMapped(xcb_window_t winid);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *data) {
        if(Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t* event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
                case XCB_CREATE_NOTIFY:
                    {
                        xcb_create_notify_event_t *cne = (xcb_create_notify_event_t*)event;
                        if (cne->parent == _source->winId()) {
                            qDebug() << "create notify " 
                                << QString("0x%1").arg(cne->window, 0, 16)
                                << QString("0x%1").arg(cne->parent, 0, 16);
                            emit subwindowCreated(cne->window);
                        }

                        break;
                    }
                case XCB_MAP_NOTIFY:
                    {
                        xcb_map_notify_event_t *mne = (xcb_map_notify_event_t*)event;
                        if (mne->event == _source->winId()) {
                            qDebug() << "map notify " 
                                << QString("0x%1").arg(mne->window, 0, 16)
                                << QString("0x%1").arg(mne->event, 0, 16);
                            emit subwindowMapped(mne->window);
                        }

                        break;
                    }
                case XCB_MOTION_NOTIFY:
                    {
                        xcb_motion_notify_event_t *cne = (xcb_motion_notify_event_t*)event;
                        qDebug() << "motion notify " 
                            << QString("0x%1").arg(cne->event, 0, 16);

                        break;
                    }
                case XCB_BUTTON_PRESS:
                    {
                        xcb_button_press_event_t *cne = (xcb_button_press_event_t*)event;
                        qDebug() << "btn press " 
                            << QString("0x%1").arg(cne->event, 0, 16);

                        break;
                    }

                default:
                    break;
            }
        }

        return false;
    }
};

static void mpv_callback(void *d)
{
    MpvProxy *mpv = static_cast<MpvProxy*>(d);
    emit mpv->has_mpv_events();
}

MpvProxy::MpvProxy(QWidget *parent)
    :QWidget(parent)
{
    if (!CompositingManager::get().composited()) {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_NativeWindow);
        qDebug() << "proxy hook winId " << this->winId();

        //auto evRelay = new EventRelayer2(windowHandle(), nullptr);
        //connect(evRelay, &EventRelayer2::subwindowCreated, this, &MpvProxy::onSubwindowCreated);
        //connect(evRelay, &EventRelayer2::subwindowMapped, this, &MpvProxy::onSubwindowMapped);
    }

    _handle = Handle::FromRawHandle(mpv_init());
    if (CompositingManager::get().composited()) {
        _gl_widget = new MpvGLWidget(this, _handle);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(_gl_widget);
        setLayout(layout);
    }
    connect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events, Qt::QueuedConnection);
}

MpvProxy::~MpvProxy()
{
    if (CompositingManager::get().composited()) {
        delete _gl_widget;
    }
}

void MpvProxy::onSubwindowMapped(xcb_window_t winid)
{
    qDebug() << __func__;
    auto l = qApp->allWindows();
    auto it = std::find_if(l.begin(), l.end(), [=](QWindow* w) { return w->winId() == winid; });
    if (it != l.end()) {
        qDebug() << "------- found child window";
    }
}

void MpvProxy::onSubwindowCreated(xcb_window_t winid)
{
    auto l = qApp->allWindows();
    auto it = std::find_if(l.begin(), l.end(), [=](QWindow* w) { return w->winId() == winid; });
    if (it == l.end()) {
        qDebug() << __func__ << QString("wrap 0x%1 into QWindow").arg(winid, 0, 16);
        auto *w = QWindow::fromWinId(winid);
        w->setParent(windowHandle());
        new EventRelayer2(w, nullptr);
    }
}

mpv_handle* MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();

    bool composited = CompositingManager::get().composited();
    
    //set_property(h, "terminal", "yes");
    //set_property(h, "msg-level", "all=v");

    if (mpv_initialize(h) < 0) {
        std::runtime_error("mpv init failed");
    }

    if (composited) {
        set_property(h, "vo", "opengl-cb");
        set_property(h, "hwdec", "auto");

    } else {
        set_property(h, "vo", "opengl");
        set_property(h, "hwdec", "auto");
        set_property(h, "wid", this->winId());
    }

    mpv_observe_property(h, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(h, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "chapter-list", MPV_FORMAT_NODE);

    mpv_observe_property(h, 0, "pause", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "core-idle", MPV_FORMAT_NODE);

    //mpv_request_log_messages(h, "info");
    mpv_set_wakeup_callback(h, mpv_callback, this);
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
        qDebug() << "time pos " << time;
    } else if (name == "pause") {
        //this->setProperty("paused", ev->data)
    } else if (name == "core-idle") {
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
    QList<QVariant> args = { "pause" };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::stop()
{
    QList<QVariant> args = { "stop" };
    qDebug () << args;
    command(_handle, args);
}

bool MpvProxy::paused()
{
    return get_property(_handle, "pause").toBool();
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
    if (fi.exists()) {
        if (!_playlist.isEmpty()) _playlist.removeFirst();
        _playlist.prepend(fi);
    }
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

#include "mpv_proxy.moc"
