#ifndef _DMR_PLAYLIST_MODEL_H
#define _DMR_PLAYLIST_MODEL_H 

#include <QtWidgets>
#include <QtConcurrent>
#include <libffmpegthumbnailer/videothumbnailer.h>

namespace dmr {
using namespace ffmpegthumbnailer;
class PlayerEngine;

struct MovieInfo {
    QUrl url;
    QString title;
    QString fileType;
    QString resolution;
    QString filePath;
    QString creation;

    qint64 fileSize;
    qint64 duration;
    int width, height;

    static struct MovieInfo parseFromFile(const QFileInfo& fi, bool *ok = nullptr);
    QString durationStr() const {
        QTime d(0, 0, 0);
        d = d.addSecs(duration);
        return d.toString("hh:mm:ss");
    }

    QString sizeStr() const {
        auto K = 1024;
        auto M = 1024 * K;
        auto G = 1024 * M;
        if (fileSize > G) {
            return QString(QT_TR_NOOP("%1G")).arg((double)fileSize / G, 0, 'f', 1);
        } else if (fileSize > M) {
            return QString(QT_TR_NOOP("%1M")).arg((double)fileSize / M, 0, 'f', 1);
        } else if (fileSize > K) {
            return QString(QT_TR_NOOP("%1K")).arg((double)fileSize / K, 0, 'f', 1);
        }
        return QString(QT_TR_NOOP("%1")).arg(fileSize);
    }
};


struct PlayItemInfo {
    bool valid;
    bool loaded;  // if url is network, this is false until playback started
    QUrl url;
    QFileInfo info;
    QPixmap thumbnail;
    struct MovieInfo mi;

    void refresh();
};

using AppendJob = QPair<QUrl, QFileInfo>; // async job
using PlayItemInfoList = QList<PlayItemInfo>;

class PlaylistModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ current WRITE changeCurrent NOTIFY currentChanged)

public:
    friend class PlayerEngine;
    enum PlayMode {
        OrderPlay,
        ShufflePlay,
        SinglePlay,
        SingleLoop,
        ListLoop,
    };

    void stop();

    PlayMode playMode() const;
    void setPlayMode(PlayMode pm);

    PlaylistModel(PlayerEngine* engine);
    ~PlaylistModel();

    void clear();
    void remove(int pos);
    void append(const QUrl&);

    void appendAsync(const QList<QUrl>&);
    void collectionJob(const QList<QUrl>&);

    void playNext(bool fromUser);
    void playPrev(bool fromUser);

    int count() const;
    const QList<PlayItemInfo>& items() const { return _infos; }
    QList<PlayItemInfo>& items() { return _infos; }

    int current() const;
    const PlayItemInfo& currentInfo() const;
    PlayItemInfo& currentInfo();
    int indexOf(const QUrl& url);

    void switchPosition(int p1, int p2);

public slots:
    void changeCurrent(int);

private slots:
    void onAsyncAppendFinished();

signals:
    void countChanged();
    void currentChanged();
    void itemRemoved(int);
    void itemsAppended();
    void emptied();
    void playModeChanged(PlayMode);
    void asyncAppendFinished(const QList<PlayItemInfo>&);
    void itemInfoUpdated(int id);

private:
    int _count {0};
    int _current {-1};
    int _last {-1};
    PlayMode _playMode {PlayMode::OrderPlay};
    QList<PlayItemInfo> _infos;

    QList<int> _playOrder; // for shuffle mode
    int _shufflePlayed {0}; // count currently played items in shuffle mode
    int _loopCount {0}; // loop count

    QList<AppendJob> _pendingJob; // async job
    QSet<QUrl> _urlsInJob;
    QFutureWatcher<PlayItemInfo> *_jobWatcher {nullptr};

    bool _userRequestingItem {false};

    VideoThumbnailer _thumbnailer;
    PlayerEngine *_engine {nullptr};

    struct PlayItemInfo calculatePlayInfo(const QUrl&, const QFileInfo& fi);
    void reshuffle();
    void savePlaylist();
    void loadPlaylist();
    void clearPlaylist();
    void appendSingle(const QUrl&);
    void tryPlayCurrent(bool next);
};

}

#endif /* ifndef _DMR_PLAYLIST_MODEL_H */

