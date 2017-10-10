#include "playlist_model.h"
#include "player_engine.h"
#include "utils.h"
#include "dmr_settings.h"


#include <libffmpegthumbnailer/videothumbnailer.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/avutil.h>
}

#include <random>

static int open_codec_context(int *stream_idx,
        AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;
    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        qWarning() << "Could not find " << av_get_media_type_string(type)
            << " stream in input file";
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];
#if LIBAVFORMAT_VERSION_MAJOR >= 57 && LIBAVFORMAT_VERSION_MINOR <= 25
    *dec_ctx = st->codec;
    dec = avcodec_find_decoder((*dec_ctx)->codec_id);
#else
    /* find decoder for the stream */
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }
    /* Allocate a codec context for the decoder */
    *dec_ctx = avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        fprintf(stderr, "Failed to allocate the %s codec context\n",
                av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }
    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(type));
        return ret;
    }
#endif

    *stream_idx = stream_index;
    return 0;
}

namespace dmr {

struct MovieInfo MovieInfo::parseFromFile(const QFileInfo& fi, bool *ok)
{
    struct MovieInfo mi;
    mi.valid = false;
    AVFormatContext *av_ctx = NULL;
    int stream_id = -1;
    AVCodecContext *dec_ctx = NULL;

    auto ret = avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), NULL, NULL);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
        if (ok) *ok = false;
        return mi;
    }

    if (avformat_find_stream_info(av_ctx, NULL) < 0) {
        qWarning() << "av_find_stream_info failed";
        if (ok) *ok = false;
        return mi;
    }

    if (av_ctx->nb_streams == 0) {
        if (ok) *ok = false;
        return mi;
    } 
    if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        if (ok) *ok = false;
        return mi;
    }

    av_dump_format(av_ctx, 0, fi.fileName().toUtf8().constData(), 0);

    mi.width = dec_ctx->width;
    mi.height = dec_ctx->height;
    auto duration = av_ctx->duration == AV_NOPTS_VALUE ? 0 : av_ctx->duration;
    duration = duration + (duration <= INT64_MAX - 5000 ? 5000 : 0);
    mi.duration = duration / AV_TIME_BASE;
    mi.resolution = QString("%1x%2").arg(mi.width).arg(mi.height);
    mi.title = fi.fileName(); //FIXME this
    mi.filePath = fi.canonicalFilePath();
    mi.creation = fi.created().toString();
    mi.fileSize = fi.size();
    mi.fileType = fi.suffix();

    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(av_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        if (tag->key && strcmp(tag->key, "creation_time") == 0) {
            auto dt = QDateTime::fromString(tag->value, Qt::ISODate);
            mi.creation = dt.toString();
            qDebug() << __func__ << dt.toString();
        }
        qDebug() << "tag:" << tag->key << tag->value;
    }
    avformat_close_input(&av_ctx);
    mi.valid = true;

    if (ok) *ok = true;
    return mi;
}

bool PlayItemInfo::refresh()
{
    if (url.isLocalFile()) {
        //FIXME: it seems that info.exists always gets refreshed 
        auto o = this->info.exists();
        auto sz = this->info.size();

        this->info.refresh();
        this->valid = this->info.exists();

        return (o != this->info.exists()) || sz != this->info.size();
    } 
    return false;
}

PlaylistModel::PlaylistModel(PlayerEngine *e)
    :_engine(e)
{
    _thumbnailer.setThumbnailSize(400);
    av_register_all();

    connect(e, &PlayerEngine::stateChanged, [=]() {
        qDebug() << "model" << "_userRequestingItem" << _userRequestingItem << "state" << e->state();
        switch (e->state()) {
            case PlayerEngine::Playing:
            {
                auto& pif = currentInfo();
                if (!pif.url.isLocalFile() && !pif.loaded) {
                    pif.mi.width = e->videoSize().width();
                    pif.mi.height = e->videoSize().height();
                    pif.mi.duration = e->duration();
                    emit itemInfoUpdated(_current);
                }
                break;
            }
            case PlayerEngine::Paused:
                break;

            case PlayerEngine::Idle:
                if (!_userRequestingItem) {
                    stop();
                    playNext(false);
                }
                break;
        }
    });

    _jobWatcher = new QFutureWatcher<PlayItemInfo>();
    connect(_jobWatcher, &QFutureWatcher<PlayItemInfo>::finished,
            this, &PlaylistModel::onAsyncAppendFinished);

    stop();
    loadPlaylist();
}

PlaylistModel::~PlaylistModel()
{
    qDebug() << __func__;
    delete _jobWatcher;

    if (Settings::get().isSet(Settings::ClearWhenQuit)) {
        clearPlaylist();
    } else {
        //persistantly save current playlist 
        savePlaylist();
    }
}

void PlaylistModel::clearPlaylist()
{
    auto fileName = QString("%1/%2/%3/playlist")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    QSettings cfg(fileName, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");
    cfg.endGroup();
}

void PlaylistModel::savePlaylist()
{
    auto fileName = QString("%1/%2/%3/playlist")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    QSettings cfg(fileName, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");

    for (int i = 0; i < count(); ++i) {
        const auto& pif = _infos[i];
        cfg.setValue(QString::number(i), pif.url);
        qDebug() << "save " << pif.url;
    }
    cfg.endGroup();
    cfg.sync();
}

void PlaylistModel::loadPlaylist()
{
    auto fileName = QString("%1/%2/%3/playlist")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());

    QList<QUrl> urls;

    QSettings cfg(fileName, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    auto keys = cfg.childKeys();
    for (int i = 0; i < keys.size(); ++i) {
        auto url = cfg.value(QString::number(i)).toUrl();
        if (indexOf(url) >= 0) continue;

        if (url.isLocalFile()) {
            urls.append(url);

        } else {
            PlayItemInfo pif {
                true,
                false,
                url,
            };
            _infos.append(pif);
        }
    }
    cfg.endGroup();

    if (urls.size() == 0) {
        _firstLoad = false;
        reshuffle();
        emit countChanged();
    }

    QTimer::singleShot(0, [=]() { delayedAppendAsync(urls); });
}


PlaylistModel::PlayMode PlaylistModel::playMode() const
{
    return _playMode;
}

void PlaylistModel::setPlayMode(PlaylistModel::PlayMode pm)
{
    if (_playMode != pm) {
        _playMode = pm;
        reshuffle();
        emit playModeChanged(pm);
    }
}

void PlaylistModel::reshuffle()
{
    if (_playMode != PlayMode::ShufflePlay || _infos.size() == 0) {
        return;
    }

    _shufflePlayed = 0;
    _playOrder.clear();
    for (int i = 0, sz = _infos.size(); i < sz; ++i) {
        _playOrder.append(i);
    }

    std::random_device rd;
    std::mt19937 g(rd());

    std::shuffle(_playOrder.begin(), _playOrder.end(), g);
    qDebug() << _playOrder;
}

void PlaylistModel::clear()
{
    _infos.clear();
    _engine->stop();
    _engine->waitLastEnd();

    _current = -1;
    _last = -1;
    emit emptied();
    emit currentChanged();
    emit countChanged();
}

void PlaylistModel::remove(int pos)
{
    if (pos < 0 || pos >= count()) return;

    _userRequestingItem = true;

    _infos.removeAt(pos);
    reshuffle();

    _last = _current;
    if (_engine->state() != PlayerEngine::Idle) {
        if (_current == pos) {
            _last = _current;
            _current = -1;
            _engine->stop();
            _engine->waitLastEnd();

        } else if (pos < _current) {
            _current--;
            _last = _current;
        }
    }

    if (_last >= count())
        _last = -1;

    emit itemRemoved(pos);
    if (_last != _current)
        emit currentChanged();
    emit countChanged();


    qDebug() << _last << _current;
    _userRequestingItem = false;
}

void PlaylistModel::stop()
{
    _current = -1;
    emit currentChanged();
}

void PlaylistModel::tryPlayCurrent(bool next)
{
    auto& pif = _infos[_current];
    if (pif.refresh()) {
        qDebug() << pif.url.fileName() << "changed";
    }
    emit itemInfoUpdated(_current);
    if (pif.valid) {
        _engine->requestPlay(_current);
        emit currentChanged();
    } else {
        _current = -1;
        emit currentChanged();
        if (next) playNext(false);
        else playPrev(false);
    }
}

void PlaylistModel::playNext(bool fromUser)
{
    if (count() == 0) return;
    qDebug() << "playmode" << _playMode << "fromUser" << fromUser
        << "last" << _last << "current" << _current;

    _userRequestingItem = fromUser;

    switch (_playMode) {
        case SinglePlay:
            if (fromUser) {
                if (_last + 1 >= count()) {
                    _last = -1;
                }
                _engine->waitLastEnd();
                _current = _last + 1;
                _last = _current;
                tryPlayCurrent(true);
            }
            break;

        case SingleLoop:
            if (fromUser) {
                if (_engine->state() == PlayerEngine::Idle) {
                    _last = _last == -1 ? 0: _last;
                    _current = _last;
                    tryPlayCurrent(true);

                } else {
                    if (_last + 1 < count()) {
                        _engine->waitLastEnd();
                        _current = _last + 1;
                        _last = _current;
                        tryPlayCurrent(true);
                    } else {
                        _engine->stop();
                    }
                }
            } else {
                if (_engine->state() == PlayerEngine::Idle) {
                    _last = _last < 0 ? 0 : _last;
                    _current = _last;
                    tryPlayCurrent(true);
                } else {
                    // replay current
                    tryPlayCurrent(true);
                }
            }
            break;

        case ShufflePlay: {
            if (_shufflePlayed >= _playOrder.size()) {
                _shufflePlayed = 0;
                reshuffle();
            }
            _shufflePlayed++;
            qDebug() << "shuffle next " << _shufflePlayed-1;
            _engine->waitLastEnd();
            _last = _current = _playOrder[_shufflePlayed-1];
            tryPlayCurrent(true);
            break;
        }

        case OrderPlay:
            _last++;
            if (_last == count()) {
                if (fromUser) 
                    _last = 0;
                else {
                    _last--;
                    break;
                }
            }

            _engine->waitLastEnd();
            _current = _last;
            tryPlayCurrent(true);
            break;

        case ListLoop:
            _last++;
            if (_last == count()) {
                _loopCount++;
                _last = 0;
            }

            _engine->waitLastEnd();
            _current = _last;
            tryPlayCurrent(true);
            break;
    }

    _userRequestingItem = false;
}

void PlaylistModel::playPrev(bool fromUser)
{
    if (count() == 0) return;
    qDebug() << "playmode" << _playMode << "fromUser" << fromUser
        << "last" << _last << "current" << _current;

    _userRequestingItem = fromUser;

    switch (_playMode) {
        case SinglePlay:
            if (fromUser) {
                if (_last - 1 < 0) {
                    _last = count();
                }
                _engine->waitLastEnd();
                _current = _last - 1;
                _last = _current;
                tryPlayCurrent(false);
            }
            break;

        case SingleLoop:
            if (fromUser) {
                if (_engine->state() == PlayerEngine::Idle) {
                    _last = _last == -1 ? 0: _last;
                    _current = _last;
                    tryPlayCurrent(false);

                } else {
                    if (_last - 1 >= 0) {
                        _engine->waitLastEnd();
                        _current = _last - 1;
                        _last = _current;
                        tryPlayCurrent(false);
                    } else {
                        _engine->stop();
                    }
                }
            } else {
                if (_engine->state() == PlayerEngine::Idle) {
                    _last = _last < 0 ? 0 : _last;
                    _current = _last;
                    tryPlayCurrent(false);
                } else {
                    // replay current
                    tryPlayCurrent(false);
                }
            }
            break;

        case ShufflePlay: { // this must comes from user
            if (_shufflePlayed <= 1) {
                reshuffle();
                _shufflePlayed = _playOrder.size();
            }
            _shufflePlayed--;
            qDebug() << "shuffle prev " << _shufflePlayed-1;
            _engine->waitLastEnd();
            _last = _current = _playOrder[_shufflePlayed-1];
            tryPlayCurrent(false);
            break;
        }

        case OrderPlay:
            _last--;
            if (_last < 0) {
                _last = count()-1;
            }

            _engine->waitLastEnd();
            _current = _last;
            tryPlayCurrent(false);
            break;

        case ListLoop:
            _last--;
            if (_last < 0) {
                _loopCount++;
                _last = count()-1;
            }

            _engine->waitLastEnd();
            _current = _last;
            tryPlayCurrent(false);
            break;
    }

    _userRequestingItem = false;

}

static QDebug operator<<(QDebug s, const QFileInfoList& v)
{
    std::for_each(v.begin(), v.end(), [&](const QFileInfo& fi) {s << fi.fileName();});
    return s;
}

void PlaylistModel::appendSingle(const QUrl& url)
{
    if (indexOf(url) >= 0) return;

    if (url.isLocalFile()) {
        QFileInfo fi(url.toLocalFile());
        if (!fi.exists()) return;
        auto pif = calculatePlayInfo(url, fi);
        if (!pif.valid) return;
        _infos.append(pif);

        if (Settings::get().isSet(Settings::AutoSearchSimilar)) {
            auto fil = utils::FindSimilarFiles(fi);
            qDebug() << "auto search similar files" << fil;
            std::for_each(fil.begin(), fil.end(), [=](const QFileInfo& fi) {
                auto url = QUrl::fromLocalFile(fi.absoluteFilePath());
                if (indexOf(url) < 0 && _engine->isPlayableFile(fi.fileName())) {
                    auto pif = calculatePlayInfo(url, fi);
                    if (pif.valid) _infos.append(pif);
                }
            });
        }
    } else {
        PlayItemInfo pif {
            true,
            false,
            url,
        };
        _infos.append(pif);
    }
}

void PlaylistModel::collectionJob(const QList<QUrl>& urls)
{
    for (const auto& url: urls) {
        if (!url.isValid() || indexOf(url) >= 0 || !url.isLocalFile() || _urlsInJob.contains(url))
            continue;

        QFileInfo fi(url.toLocalFile());
        if (!_firstLoad && (!fi.exists() || !fi.isFile())) continue;

        _pendingJob.append(qMakePair(url, fi));
        _urlsInJob.insert(url);
        qDebug() << "append " << url.fileName();

        if (!_firstLoad && Settings::get().isSet(Settings::AutoSearchSimilar)) {
            auto fil = utils::FindSimilarFiles(fi);
            qDebug() << "auto search similar files" << fil;
            std::for_each(fil.begin(), fil.end(), [=](const QFileInfo& fi) {
                if (fi.isFile()) {
                    auto url = QUrl::fromLocalFile(fi.absoluteFilePath());

                    if (!_urlsInJob.contains(url) && indexOf(url) < 0 &&
                            _engine->isPlayableFile(fi.fileName())) {
                        _pendingJob.append(qMakePair(url, fi));
                        _urlsInJob.insert(url);
                    }
                }
            });
        }
    }
}

void PlaylistModel::appendAsync(const QList<QUrl>& urls)
{
    QTimer::singleShot(10, [=]() { delayedAppendAsync(urls); });
}

void PlaylistModel::delayedAppendAsync(const QList<QUrl>& urls)
{
    if (_pendingJob.size() > 0) {
        //TODO: may be automatically schedule later
        qWarning() << "there is a pending append going on, enqueue";
        _pendingAppendReq.enqueue(urls);
        return;
    }

    collectionJob(urls);
    if (!_pendingJob.size()) return;

    struct MapFunctor {
        PlaylistModel *_model = 0;
        using result_type = PlayItemInfo;
        MapFunctor(PlaylistModel* model): _model(model) {}

        struct PlayItemInfo operator()(const AppendJob& a) {
            qDebug() << "mapping " << a.first.fileName();
            return _model->calculatePlayInfo(a.first, a.second);
        };
    };

    auto future = QtConcurrent::mapped(_pendingJob, MapFunctor(this));
    _jobWatcher->setFuture(future);
}

static QList<PlayItemInfo>& SortSimilarFiles(QList<PlayItemInfo>& fil)
{
    //sort names by digits inside, take care of such a possible:
    //S01N04, S02N05, S01N12, S02N04, etc...
    struct {
        bool operator()(const PlayItemInfo& fi1, const PlayItemInfo& fi2) const {
            if (!fi1.valid) 
                return true;
            if (!fi2.valid)
                return false;

            QString fileName1 = fi1.url.fileName();
            QString fileName2 = fi2.url.fileName();

            if (utils::IsNamesSimilar(fileName1, fileName2)) {
                return utils::CompareNames(fileName1, fileName2);
            }
            return fileName1.localeAwareCompare(fileName2) < 0;
        }
    } SortByDigits;
    std::sort(fil.begin(), fil.end(), SortByDigits);
    
    return fil;
}

void PlaylistModel::onAsyncAppendFinished()
{
    auto f = _jobWatcher->future();
    _pendingJob.clear();
    _urlsInJob.clear();

    auto fil = f.results();
    if (!_firstLoad) {
        //since _infos are modified only at the same thread, the lock is not necessary
        auto last = std::remove_if(fil.begin(), fil.end(), [](const PlayItemInfo& pif) {
                return !pif.mi.valid;
            });
        fil.erase(last, fil.end());
    }

    qDebug() << "collected items" << fil.count();
    if (fil.size()) {
        if (!_firstLoad)
            _infos += SortSimilarFiles(fil);
        else
            _infos += fil;
        reshuffle();
        _firstLoad = false;
        emit itemsAppended();
        emit countChanged();
    }
    _firstLoad = false;
    emit asyncAppendFinished(fil);

    QTimer::singleShot(0, [&]() {
        if (_pendingAppendReq.size()) {
            auto job = _pendingAppendReq.dequeue();
            delayedAppendAsync(job);
        }
    });
}

bool PlaylistModel::hasPendingAppends()
{
    return _pendingAppendReq.size() > 0 || _pendingJob.size() > 0;
}

//TODO: what if loadfile failed
void PlaylistModel::append(const QUrl& url)
{
    if (!url.isValid()) return;

    appendSingle(url);
    reshuffle();
    emit itemsAppended();
    emit countChanged();
}

void PlaylistModel::changeCurrent(int pos)
{
    if (pos < 0 || pos >= count()) return;

    _userRequestingItem = true;

    _engine->waitLastEnd();
    _current = pos;
    _last = _current;
    tryPlayCurrent(true);
    _userRequestingItem = false;
}

void PlaylistModel::switchPosition(int p1, int p2)
{
    Q_ASSERT_X(0, "playlist", "not implemented");
}

PlayItemInfo& PlaylistModel::currentInfo()
{
    //Q_ASSERT (_infos.size() > 0 && _current >= 0);
    Q_ASSERT (_infos.size() > 0);
    
    if (_current >= 0)
        return _infos[_current];
    if (_last >= 0)
        return _infos[_last];
    return _infos[0];
}

const PlayItemInfo& PlaylistModel::currentInfo() const
{
    Q_ASSERT (_infos.size() > 0 && _current >= 0);
    return _infos[_current];
}

int PlaylistModel::count() const
{
    return _infos.count();
}

int PlaylistModel::current() const
{
    return _current;
}

struct PlayItemInfo PlaylistModel::calculatePlayInfo(const QUrl& url, const QFileInfo& fi)
{
    bool ok = false;
    auto mi = MovieInfo::parseFromFile(fi, &ok);

    QPixmap pm;
    if (ok) {
        try {
            std::vector<uint8_t> buf;
            _thumbnailer.generateThumbnail(fi.canonicalFilePath().toUtf8().toStdString(),
                    ThumbnailerImageType::Png, buf);

            auto img = QImage::fromData(buf.data(), buf.size(), "png");
            pm = QPixmap::fromImage(img);
        } catch (const std::logic_error&) {
        }
    }

    PlayItemInfo pif { fi.exists(), ok, url, fi, pm, mi };

    return pif;
}

int PlaylistModel::indexOf(const QUrl& url)
{
    auto p = std::find_if(_infos.begin(), _infos.end(), [&](const PlayItemInfo& pif) {
        return pif.url == url;
    });

    if (p == _infos.end()) return -1;
    return std::distance(_infos.begin(), p);
}

}


