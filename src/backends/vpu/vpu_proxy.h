#ifndef _DMR_VPU_PROXY_H
#define _DMR_VPU_PROXY_H 

#include "player_backend.h"
#include "player_engine.h"
#include "vpu_decoder.h"

namespace dmr {

class VpuMainThread;

class VpuProxy: public Backend {
    Q_OBJECT
public:
    VpuProxy(QWidget *parent = 0);
    virtual ~VpuProxy();

    const PlayingMovieInfo& playingMovieInfo() override;
    virtual void setPlayFile(const QFileInfo& fi) override;
    bool isPlayable() const override { return true; }

    qint64 duration() const override;
    qint64 elapsed() const override;

    void loadSubtitle(const QFileInfo& fi) override;
    void toggleSubtitle() override;
    bool isSubVisible() override;

    int volume() const override;
    bool muted() const override;

    QImage takeScreenshot() override;
    void burstScreenshot() override; //initial the start of burst screenshotting
    void stopBurstScreenshot() override;

public slots:
    void play() override;
    void pauseResume() override;
    void stop() override;

    void seekForward(int secs) override;
    void seekBackward(int secs) override;
    void volumeUp() override;
    void volumeDown() override;
    void changeVolume(int val) override;
    void toggleMute() override;

protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

    void video_refresh_timer();

private:
    VideoFrame _lastFrame {0};

    VpuMainThread *_d {0};
    bool _reqQuit {false};

    int64_t _elapsed {0};
    double _frameLastPts {-1.0};
    double _frameLastDelay {0.0};
    double _frameTimer {0.0};

    PlayingMovieInfo _pmf;
    void updatePlayingMovieInfo();
    void setState(PlayState s);
};
}

#endif /* ifndef _DMR_VPU_PROXY_H */




