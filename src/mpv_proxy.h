#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include <QtWidgets>
#include <xcb/xproto.h>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;
class PlaylistModel;

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

    int volume() const;
    bool muted() const;

    const PlaylistModel& playlist() const { return *_playlist; }

    QPixmap takeScreenshot();
    void burstScreenshot(); //initial the start of burst screenshotting
    void stopBurstScreenshot();

signals:
    void has_mpv_events();

    void ellapsedChanged();
    void stateChanged();
    void fileLoaded();
    void muteChanged();
    void volumeChanged();

    //emit during burst screenshotting
    void notifyScreenshot(const QPixmap& frame);

    void playlistChanged();

public slots:
    void play();
    void pauseResume();
    void stop();
    void prev();
    void next();
    void seekForward(int secs);
    void seekBackward(int secs);
    void volumeUp();
    void volumeDown();
    void changeVolume(int val);
    void toggleMute();

protected slots:
    void handle_mpv_events();
    void onSubwindowCreated(xcb_window_t winid);
    void onSubwindowMapped(xcb_window_t winid);
    void stepBurstScreenshot();

private:
    Handle _handle;
    MpvGLWidget *_gl_widget{nullptr};

    //QList<QFileInfo> _playlist;
    PlaylistModel *_playlist {nullptr};
    CoreState _state { CoreState::Idle };
    struct MovieInfo _movieInfo;
    bool _movieInfoNeedsUpdate {true};

    bool _inBurstShotting {false};
    QTimer *_burstScreenshotTimer {nullptr};

    mpv_handle* mpv_init();
    void processPropertyChange(mpv_event_property* ev);
    void processLogMessage(mpv_event_log_message* ev);
    QPixmap takeOneScreenshot();
    void changeProperty(const QString& name, const QVariant& v);
};
}

#endif /* ifndef _MAIN_WINDOW_H */



