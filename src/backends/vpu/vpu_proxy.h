#ifndef _DMR_VPU_PROXY_H
#define _DMR_VPU_PROXY_H 

#include "player_backend.h"
#include "player_engine.h"

namespace dmr {

class VpuDecoder;

class VpuProxy: public Backend {
    Q_OBJECT
public:
    VpuProxy(QWidget *parent = 0);
    virtual ~VpuProxy();

    const PlayingMovieInfo& playingMovieInfo() override;
    virtual void setPlayFile(const QFileInfo& fi) override;
    bool isPlayable() const override { return true; }

    qint64 duration() const override;
    qint64 ellapsed() const override;

    void loadSubtitle(const QFileInfo& fi) override;
    void toggleSubtitle() override;
    bool isSubVisible() override;

    int volume() const override;
    bool muted() const override;

    QImage takeScreenshot() override;
    void burstScreenshot() override; //initial the start of burst screenshotting
    void stopBurstScreenshot() override;

public slots:
    //void play(const QString& filename);
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

private:
    QLabel *_canvas {0};
    QImage _img;
    VpuDecoder *_d {0};

    PlayingMovieInfo _pmf;
    void updatePlayingMovieInfo();
    void setState(PlayState s);
};
}

#endif /* ifndef _DMR_VPU_PROXY_H */




