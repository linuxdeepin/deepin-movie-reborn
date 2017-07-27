#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include "player_backend.h"
#include "player_engine.h"
#include <xcb/xproto.h>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;

class MpvProxy: public Backend {
    Q_OBJECT

public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    const PlayingMovieInfo& playingMovieInfo() override;
    // mpv plays all files by default  (I hope)
    bool isPlayable() const override { return true; }

    // polling until current playback ended
    void pollingEndOfPlayback();

    qint64 duration() const override;
    qint64 elapsed() const override;
    QSize videoSize() const override;

    void loadSubtitle(const QFileInfo& fi) override;
    void toggleSubtitle() override;
    bool isSubVisible() override;
    void selectSubtitle(int id) override;
    int sid() const override;

    void selectTrack(int id) override;
    int aid() const override;

    void changeSoundMode(SoundMode sm) override;
    int volume() const override;
    bool muted() const override;

    void setVideoAspect(double r) override;
    double videoAspect() const override;
    int videoRotation() const override;
    void setVideoRotation(int degree) override;

    QImage takeScreenshot() override;
    void burstScreenshot() override; //initial the start of burst screenshotting
    void stopBurstScreenshot() override;

public slots:
    void playUrl(QUrl url);

    void play() override;
    void pauseResume() override;
    void stop() override;

    void seekForward(int secs) override;
    void seekBackward(int secs) override;
    void volumeUp() override;
    void volumeDown() override;
    void changeVolume(int val) override;
    void toggleMute() override;

protected slots:
    void handle_mpv_events();
    void stepBurstScreenshot();

signals:
    void has_mpv_events();

private:
    Handle _handle;
    MpvGLWidget *_gl_widget{nullptr};

    bool _inBurstShotting {false};
    QTimer *_burstScreenshotTimer {nullptr};
    bool _pendingSeek {false};
    PlayingMovieInfo _pmf;
    int _videoRotation {0};

    mpv_handle* mpv_init();
    void processPropertyChange(mpv_event_property* ev);
    void processLogMessage(mpv_event_log_message* ev);
    QImage takeOneScreenshot();
    void changeProperty(const QString& name, const QVariant& v);
    void updatePlayingMovieInfo();
    void setState(PlayState s);
};
}

#endif /* ifndef _DMR_MPV_PROXY_H */



