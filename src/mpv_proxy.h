#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include <QtWidgets>
#include <xcb/xproto.h>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;

struct MovieInfo {
    QString title;
    QString fileType;
    QString fileSize;
    QString resolution;
    QString duration;
    QString filePath;
    QString creation;

    int width, height;
};

class MpvProxy: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration)
    Q_PROPERTY(qint64 ellapsed READ ellapsed NOTIFY ellapsedChanged)
    Q_PROPERTY(bool paused READ paused)
    Q_PROPERTY(CoreState state READ state WRITE setState NOTIFY stateChanged)
public:
    enum CoreState {
        Idle,
        Playing,
        Paused,
    };
    Q_ENUM(CoreState)

    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    void addPlayFile(const QFileInfo& fi);

    qint64 duration() const;
    qint64 ellapsed() const;
    const struct MovieInfo& movieInfo(); 

    bool paused();
    CoreState state() const { return _state; }
    void setState(CoreState s);

signals:
    void has_mpv_events();
    void ellapsedChanged();
    void stateChanged();
    void fileLoaded();

    //emit during burst screenshotting
    void notifyScreenshot(const QPixmap& frame);

public slots:
    void play();
    void pauseResume();
    void stop();
    void seekForward(int secs);
    void seekBackward(int secs);
    void takeScreenshot();
    void burstScreenshot(); //initial the start of burst screenshotting
    void stopBurstScreenshot();

protected slots:
    void handle_mpv_events();
    void onSubwindowCreated(xcb_window_t winid);
    void onSubwindowMapped(xcb_window_t winid);
    void stepBurstScreenshot();

private:
    Handle _handle;
    MpvGLWidget *_gl_widget{nullptr};

    QList<QFileInfo> _playlist;
    CoreState _state { CoreState::Idle };
    struct MovieInfo _movieInfo;
    bool _movieInfoNeedsUpdate {true};

    bool _inBurstShotting {false};
    QTimer *_burstScreenshotTimer {nullptr};

    mpv_handle* mpv_init();
    void process_property_change(mpv_event_property* ev);
    void process_log_message(mpv_event_log_message* ev);
};
}

#endif /* ifndef _MAIN_WINDOW_H */



