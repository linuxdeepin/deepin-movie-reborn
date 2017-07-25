#ifndef _DMR_PLAYER_BACKEND_H
#define _DMR_PLAYER_BACKEND_H 

#include <QtWidgets>

namespace dmr {
class PlayingMovieInfo;

// Player backend base class
// There are only two backends: mpv and vpu
// mpv is the only and default on all platform except Sunway
// vpu is default for Sunway if media file can be hardware-decoded by coda vpu
class Backend: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(bool paused READ paused)

    Q_PROPERTY(PlayState state READ state NOTIFY stateChanged)
public:
    enum PlayState {
        Playing,
        Paused,
        Stopped
    };
    Q_ENUM(PlayState)

    Backend(QWidget *parent = 0) {}
    virtual ~Backend() {}

    virtual void setPlayFile(const QFileInfo& fi) { _file = fi; }

    // NOTE: need to check if file is playable by this backend, 
    // this is important especially for vpu
    virtual bool isPlayable() const = 0;

    virtual qint64 duration() const { return 0; }
    virtual qint64 elapsed() const { return 0; }

    virtual bool paused() { return _state == PlayState::Paused; }
    virtual PlayState state() const { return _state; }
    virtual const PlayingMovieInfo& playingMovieInfo() = 0;

    virtual void loadSubtitle(const QFileInfo& fi) = 0;
    virtual void toggleSubtitle() = 0;
    virtual bool isSubVisible() = 0;
    virtual void selectSubtitle(int id) = 0;
    virtual void selectTrack(int id) = 0;

    virtual int aid() const = 0;
    virtual int sid() const = 0;

    virtual int volume() const = 0;
    virtual bool muted() const = 0;

    virtual QImage takeScreenshot() = 0;
    virtual void burstScreenshot() = 0; //initial the start of burst screenshotting
    virtual void stopBurstScreenshot() = 0;

Q_SIGNALS:
    void tracksChanged();
    void elapsedChanged();
    void stateChanged();
    void fileLoaded();
    void muteChanged();
    void volumeChanged();
    void sidChanged();
    void aidChanged();

    //emit during burst screenshotting
    void notifyScreenshot(const QImage& frame);

public slots:
    virtual void play() = 0;
    virtual void pauseResume() = 0;
    virtual void stop() = 0;

    virtual void seekForward(int secs) = 0;
    virtual void seekBackward(int secs) = 0;
    virtual void volumeUp() = 0;
    virtual void volumeDown() = 0;
    virtual void changeVolume(int val) = 0;
    virtual void toggleMute() = 0;

protected:
    PlayState _state { PlayState::Stopped };
    QFileInfo _file;
};
}

#endif /* ifndef _DMR_PLAYER_BACKEND_H */

