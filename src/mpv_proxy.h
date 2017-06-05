#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include <QtWidgets>
#include <xcb/xproto.h>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;

class MpvProxy: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 ellapsed READ ellapsed NOTIFY ellapsedChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pauseChanged)
public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    void addPlayFile(const QFileInfo& fi);
    qint64 duration() const;
    qint64 ellapsed() const;
    bool paused();

signals:
    void has_mpv_events();
    void pauseChanged();
    void durationChanged();
    void ellapsedChanged();

public slots:
    void play();
    void pauseResume();
    void stop();
    void seekForward(int secs);
    void seekBackward(int secs);
    void takeScreenshot();
    void burstScreenshot();

protected slots:
    void handle_mpv_events();
    void onSubwindowCreated(xcb_window_t winid);
    void onSubwindowMapped(xcb_window_t winid);

private:
    Handle _handle;
    MpvGLWidget *_gl_widget{nullptr};

    QList<QFileInfo> _playlist;

    mpv_handle* mpv_init();
    void process_property_change(mpv_event_property* ev);
    void process_log_message(mpv_event_log_message* ev);
};
}

#endif /* ifndef _MAIN_WINDOW_H */



