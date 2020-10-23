/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#ifndef _DMR_PLAYLIST_MODEL_H
#define _DMR_PLAYLIST_MODEL_H

#include <QtWidgets>
//#include <QtConcurrent>
#include <DApplicationHelper>
#include <libffmpegthumbnailer/videothumbnailerc.h>

#include "utils.h"
#include <QNetworkReply>
#include <QMutex>

#include <libffmpegthumbnailer/videothumbnailerc.h>

typedef video_thumbnailer *(*mvideo_thumbnailer)();
typedef void (*mvideo_thumbnailer_destroy)(video_thumbnailer *thumbnailer);
/* create image_data structure */
typedef image_data *(*mvideo_thumbnailer_create_image_data)(void);
/* destroy image_data structure */
typedef void (*mvideo_thumbnailer_destroy_image_data)(image_data *data);
typedef int (*mvideo_thumbnailer_generate_thumbnail_to_buffer)(video_thumbnailer *thumbnailer, const char *movie_filename, image_data *generated_image_data);

namespace dmr {
class PlayerEngine;
class LoadThread;
class GetThumanbil;

struct MovieInfo {
    bool valid;
    QString title;
    QString fileType;
    QString resolution;
    QString filePath;
    QString creation;

    // rotation in metadata, this affects width/height
    int raw_rotate;
    qint64 fileSize;
    qint64 duration;
    int width, height;

    //3.4添加视频信息
    //视频流信息
    int vCodecID;
    qint64 vCodeRate;
    int fps;
    float proportion;
    //音频流信息
    int aCodeID;
    qint64 aCodeRate;
    int aDigit;
    int channels;
    int sampling;

    static struct MovieInfo parseFromFile(const QFileInfo &fi, bool *ok = nullptr);
    QString durationStr() const
    {
        return utils::Time2str(duration);
    }

    QString videoCodec() const
    {
        return  utils::videoIndex2str(vCodecID);
    }

    QString audioCodec() const
    {
        return utils::audioIndex2str(aCodeID);
    }

    //获取字幕编码格式（备用）
    /*QString subtitleCodec() const
    {
        return utils::subtitleIndex2str();
    }*/

    QString sizeStr() const
    {
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
    QPixmap thumbnail_dark;
    struct MovieInfo mi;

    bool refresh();
};

using AppendJob = QPair<QUrl, QFileInfo>; // async job
using PlayItemInfoList = QList<PlayItemInfo>;
using UrlList = QList<QUrl>;


class PlaylistModel: public QObject
{
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

    explicit PlaylistModel(PlayerEngine *engine);
    ~PlaylistModel();

    qint64 getUrlFileTotalSize(QUrl url, int tryTimes) const;

    void clear();
    void remove(int pos);
    void append(const QUrl &);

    void appendAsync(const QList<QUrl> &);
    void collectionJob(const QList<QUrl> &, QList<QUrl> &);

    void playNext(bool fromUser);
    void playPrev(bool fromUser);

    int count() const;
    const QList<PlayItemInfo> &items() const
    {
        return _infos;
    }
    QList<PlayItemInfo> &items()
    {
        return _infos;
    }

    int current() const;
    const PlayItemInfo &currentInfo() const;
    PlayItemInfo &currentInfo();
    int indexOf(const QUrl &url);

    void switchPosition(int p1, int p2);

    bool hasPendingAppends();
    void handleAsyncAppendResults(QList<PlayItemInfo> &pil);
    struct PlayItemInfo calculatePlayInfo(const QUrl &, const QFileInfo &fi, bool isDvd = false);
    bool getthreadstate();
    void savePlaylist();
    void clearPlaylist();
    QList<QUrl> getLoadList()
    {
        return m_loadFile;
    };
    void loadPlaylist();

public slots:
    void changeCurrent(int);
    void delayedAppendAsync(const QList<QUrl> &);
    void deleteThread();
    void clearLoad();

private slots:
    void onAsyncAppendFinished();
    void onAsyncFinished();
    void onAsyncUpdate(PlayItemInfo);
    //把lambda表达式改为槽函数，modify by myk
    void slotStateChanged();


signals:
    void countChanged();
    void currentChanged();
    void itemRemoved(int);
    void itemsAppended();
    void emptied();
    void playModeChanged(PlayMode);
    void asyncAppendFinished(const QList<PlayItemInfo> &);
    void itemInfoUpdated(int id);

private:
    void initThumb();
    void initFFmpeg();
    bool getMusicPix(const QFileInfo &fi, QPixmap &rImg);
    struct MovieInfo parseFromFile(const QFileInfo &fi, bool *ok = nullptr);
    QString libPath(const QString &strlib);
    // when app starts, and the first time to load playlist
    bool _firstLoad {true};
    int _count {0};
    int _current {-1};
    int _last {-1};
    bool _hasNormalVideo{false};
    PlayMode _playMode {PlayMode::OrderPlay};
    QList<PlayItemInfo> _infos;

    QList<int> _playOrder; // for shuffle mode
    int _shufflePlayed {0}; // count currently played items in shuffle mode
    int _loopCount {0}; // loop count

    QList<AppendJob> _pendingJob; // async job
    QSet<QString> _urlsInJob;  // url list
    QFutureWatcher<PlayItemInfo> *_jobWatcher {nullptr};

    QQueue<UrlList> _pendingAppendReq;

    bool _userRequestingItem {false};

    video_thumbnailer *m_video_thumbnailer = nullptr;
    image_data *m_image_data = nullptr;

    mvideo_thumbnailer m_mvideo_thumbnailer = nullptr;
    mvideo_thumbnailer_destroy m_mvideo_thumbnailer_destroy = nullptr;
    mvideo_thumbnailer_create_image_data m_mvideo_thumbnailer_create_image_data = nullptr;
    mvideo_thumbnailer_destroy_image_data m_mvideo_thumbnailer_destroy_image_data = nullptr;
    mvideo_thumbnailer_generate_thumbnail_to_buffer m_mvideo_thumbnailer_generate_thumbnail_to_buffer = nullptr;

    PlayerEngine *_engine {nullptr};

    QString _playlistFile;

    LoadThread *m_ploadThread;
    GetThumanbil *m_getThumanbil {nullptr};
    QMutex *m_pdataMutex;
    bool m_brunning;
    QList<QUrl> m_tempList;
    QList<QUrl> m_loadFile;
    bool m_isLoadRunning {false};
    bool m_initFFmpeg {false};

    void reshuffle();
    void appendSingle(const QUrl &);
    void tryPlayCurrent(bool next);

};


class LoadThread: public QThread
{
    Q_OBJECT

public:
    LoadThread(PlaylistModel *model, const QList<QUrl> &urls);
    ~LoadThread();

public:
    void run();

private:
    PlaylistModel *_pModel;
    QList<QUrl> _urls;

    QList<AppendJob> _pendingJob; // async job
    QSet<QString> _urlsInJob;  // url list
};

class GetThumanbil : public QThread
{
    Q_OBJECT
public:
    GetThumanbil(PlaylistModel *model, const QList<QUrl> &urls):m_model(model), m_urls(urls)
    {
//        m_model = model;
//        m_urls = urls;
        m_mutex = new QMutex;
        m_itemMutex = new QMutex;
    };
    ~GetThumanbil()
    {
        m_stop = true;
        delete m_mutex;
        m_mutex = nullptr;
        delete m_itemMutex;
        m_itemMutex = nullptr;
    };
    //QList<PlayItemInfo> getInfoList() {return m_itemInfo;}
    void stop()
    {
        m_stop = true;
    };
    void setUrls(QList<QUrl> urls)
    {
        m_mutex->lock();
        m_urls = urls;
        m_mutex->unlock();
    };
    void clearItem()
    {
        m_itemMutex->lock();
        //m_itemInfo.clear();
        m_urls.clear();
        m_itemMutex->unlock();
    };

    void run()
    {
        m_mutex->lock();
        QList<QUrl> urls = m_urls;
        m_mutex->unlock();
        foreach (QUrl url, urls) {
            QFileInfo info(url.path());
            m_itemMutex->lock();
            //m_itemInfo.append();
            emit updateItem(m_model->calculatePlayInfo(url, info, false));
            m_itemMutex->unlock();
            m_isFinished = true;
            if (m_stop) break;
        }
    }

signals:
    void updateItem(PlayItemInfo);
private:
    PlaylistModel *m_model;
    QList<QUrl> m_urls;
    //QList<PlayItemInfo> m_itemInfo;
    QMutex *m_mutex;
    QMutex *m_itemMutex;
    bool m_isFinished {true};
    bool m_stop {false};
};


}

#endif /* ifndef _DMR_PLAYLIST_MODEL_H */

