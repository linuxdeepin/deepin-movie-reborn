#ifndef _DMR_PLAYER_ENINE_H
#define _DMR_PLAYER_ENINE_H 

#include "config.h"

#include <QtWidgets>
#include "playlist_model.h"
#include "player_backend.h"
#include "online_sub.h"

namespace dmr {
class PlaylistModel;

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
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(bool paused READ paused)

    Q_PROPERTY(CoreState state READ state NOTIFY stateChanged)
public:
    enum CoreState {
        Idle,
        Playing,
        Paused,
    };
    Q_ENUM(CoreState)

        // filetypes supported by mpv: https://github.com/mpv-player/mpv/blob/master/player/external_files.c
    const QStringList audio_filetypes {"*.mp3","*.ogg","*.wav","*.wma","*.m4a","*.aac","*.ac3","*.ape","*.flac","*.ra","*.mka","*.dts","*.opus"};
    const QStringList video_filetypes {"*.avi","*.divx","*.mpg","*.mpeg","*.m1v","*.m2v","*.mpv","*.dv","*.3gp","*.mov","*.mp4","*.m4v","*.mqv","*.dat","*.vcd","*.ogm","*.ogv","*.asf","*.wmv","*.vob","*.mkv","*.ram","*.flv","*.rm","*.ts","*.rmvb","*.dvr-ms","*.m2t","*.m2ts","*.rec","*.f4v","*.hdmov","*.webm","*.vp8","*.letv","*.hlv"};
    const QStringList subtitle_filetypes {"*.sub","*.srt","*.ass","*.ssa","*.smi","*.rt","*.txt","*.mks","*.vtt","*.sup"};

    /* backend like mpv will asynchronously report end of playback. 
     * there are situations when we need to see the end-event before 
     * proceed (e.g playlist next)
     */
    void waitLastEnd();

    friend class PlaylistModel;

    PlayerEngine(QWidget *parent = 0);
    virtual ~PlayerEngine();

    void addPlayFile(const QUrl& url);
    QList<QUrl> addPlayDir(const QDir& dir); // return collected valid urls
    //returned list contains only accepted valid items
    QList<QUrl> addPlayFiles(const QList<QUrl>& urls);

    bool isPlayableFile(const QUrl& url);
    bool isPlayableFile(const QString& name);

    // only supports (+/-) 0, 90, 180, 270
    int videoRotation() const;
    void setVideoRotation(int degree);

    void setVideoAspect(double r);
    double videoAspect() const;

    qint64 duration() const;
    qint64 elapsed() const;
    QSize videoSize() const;
    const struct MovieInfo& movieInfo(); 

    bool paused();
    CoreState state() const { return _state; }
    const PlayingMovieInfo& playingMovieInfo();
    void setPlaySpeed(double times);

    void loadOnlineSubtitle(const QUrl& url);
    bool loadSubtitle(const QFileInfo& fi);
    void toggleSubtitle();
    bool isSubVisible();
    void selectSubtitle(int id); // id into PlayingMovieInfo.subs
    int sid() const;
    void setSubDelay(double secs);
    double subDelay() const;
    void updateSubStyle(const QString& font, int sz);
    void setSubCodepage(const QString& cp);
    QString subCodepage();
    void addSubSearchPath(const QString& path);

    void selectTrack(int id); // id into PlayingMovieInfo.audios
    int aid() const;

    void changeSoundMode(Backend::SoundMode sm);
    int volume() const;
    bool muted() const;

    PlaylistModel& playlist() const { return *_playlist; }

    QImage takeScreenshot();
    void burstScreenshot(); //initial the start of burst screenshotting
    void stopBurstScreenshot();

    void savePlaybackPosition();

signals:
    void tracksChanged();
    void elapsedChanged();
    void videoSizeChanged();
    void stateChanged();
    void fileLoaded();
    void muteChanged();
    void volumeChanged();
    void sidChanged();
    void aidChanged();
    void subCodepageChanged();

    void loadOnlineSubtitlesFinished(const QUrl& url, bool success);

    //emit during burst screenshotting
    void notifyScreenshot(const QImage& frame);

    void playlistChanged();

public slots:
    void play();
    void pauseResume();
    void stop();

    void prev();
    void next();
    void playSelected(int id); // id as in playlist indexes
    void playByName(const QUrl& url);
    void clearPlaylist();

    void seekForward(int secs);
    void seekBackward(int secs);
    void seekAbsolute(int pos);

    void volumeUp();
    void volumeDown();
    void changeVolume(int val);
    void toggleMute();

protected slots:
    void onBackendStateChanged();
    void requestPlay(int id);
    void updateSubStyles();
    void onSubtitlesDownloaded(const QUrl& url, const QList<QString>& filenames,
            OnlineSubtitle::FailReason);
    void onPlaylistAsyncAppendFinished(const QList<PlayItemInfo>&);

protected:
    PlaylistModel *_playlist {nullptr};
    CoreState _state { CoreState::Idle };
    Backend *_current {nullptr};

    QUrl _pendingPlayReq;

    QList<QUrl> collectPlayFiles(const QList<QUrl>& urls);
    QList<QUrl> collectPlayDir(const QDir& dir);

    void resizeEvent(QResizeEvent* re) override;
    void savePreviousMovieState();
};
}

#endif /* ifndef _DMR_PLAYER_ENINE_H */
