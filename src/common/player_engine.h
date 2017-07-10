#ifndef _DMR_PLAYER_ENINE_H
#define _DMR_PLAYER_ENINE_H 

#include "config.h"

#include <QtWidgets>
#include "playlist_model.h"

namespace dmr {
class PlaylistModel;
class Backend;

using SubtitleInfo = QMap<QString, QVariant>;
using AudioInfo = QMap<QString, QVariant>;

struct PlayingMovieInfo 
{
    QList<SubtitleInfo> subs;
    QList<AudioInfo> audios;
};

class PlayerEngine: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration)
    Q_PROPERTY(qint64 ellapsed READ ellapsed NOTIFY ellapsedChanged)
    Q_PROPERTY(bool paused READ paused)

    Q_PROPERTY(CoreState state READ state NOTIFY stateChanged)
public:
    enum CoreState {
        Idle,
        Playing,
        Paused,
    };
    Q_ENUM(CoreState)

    friend class PlaylistModel;

    PlayerEngine(QWidget *parent = 0);
    virtual ~PlayerEngine();

    void addPlayFile(const QFileInfo& fi);

    qint64 duration() const;
    qint64 ellapsed() const;
    const struct MovieInfo& movieInfo(); 

    bool paused();
    CoreState state() const { return _state; }
    const PlayingMovieInfo& playingMovieInfo();

    void loadSubtitle(const QFileInfo& fi);
    void toggleSubtitle();
    bool isSubVisible();

    int volume() const;
    bool muted() const;

    PlaylistModel& playlist() const { return *_playlist; }

    QPixmap takeScreenshot();
    void burstScreenshot(); //initial the start of burst screenshotting
    void stopBurstScreenshot();

signals:
    void tracksChanged();
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
    void clearPlaylist();

    void seekForward(int secs);
    void seekBackward(int secs);
    void volumeUp();
    void volumeDown();
    void changeVolume(int val);
    void toggleMute();

protected slots:
    void onBackendStateChanged();
    void requestPlay(int id);

protected:
    PlaylistModel *_playlist {nullptr};
    CoreState _state { CoreState::Idle };
    Backend *_current {nullptr};
};
}

#endif /* ifndef _DMR_PLAYER_ENINE_H */
