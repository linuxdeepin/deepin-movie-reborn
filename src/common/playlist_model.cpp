#include "playlist_model.h"
#include "player_engine.h"

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

struct MovieInfo MovieInfo::parseFromFile(const QFileInfo& fi)
{
    struct MovieInfo mi;
    AVFormatContext *av_ctx = NULL;
    int stream_id = -1;
    AVCodecContext *dec_ctx = NULL;

    auto ret = avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), NULL, NULL);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
    }

    if (avformat_find_stream_info(av_ctx, NULL) < 0) {
        qWarning() << "av_find_stream_info failed";
    }

    if (av_ctx->nb_streams == 0) return mi;
    if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
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

    return mi;
}

PlaylistModel::PlaylistModel(PlayerEngine *e)
    :_engine(e)
{
    _thumbnailer.setThumbnailSize(44);
    av_register_all();

    connect(e, &PlayerEngine::stateChanged, [=]() {
        qDebug() << "model" << "_userRequestedItem" << _userRequestedItem;
        switch (e->state()) {
            case PlayerEngine::Playing:
                break;
            case PlayerEngine::Paused:
                break;
            case PlayerEngine::Idle:
                stop();
                if (!_userRequestedItem) {
                    playNext(false);
                } else {
                    _userRequestedItem = false;
                }
                break;
        }
    });

    stop();
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

    _current = -1;
    _last = -1;
    emit currentChanged();
    emit countChanged();
}

void PlaylistModel::remove(int pos)
{
    if (pos < 0 || pos >= count()) return;

    _infos.removeAt(pos);
    reshuffle();

    if (_current == pos) {
        _last = _current;
        _current = -1;
        _engine->stop();

    } else if (pos < _current) {
        _current--;
        _last = _current;
    }

    emit itemRemoved(pos);
    emit currentChanged();
    emit countChanged();
}

void PlaylistModel::stop()
{
    _current = -1;
    emit currentChanged();
}

void PlaylistModel::firstPlay()
{
    playNext(false);
}

void PlaylistModel::playNext(bool fromUser)
{
    if (count() == 0) return;
    qDebug() << "playmode" << _playMode << "fromUser" << fromUser
        << "last" << _last << "current" << _current;

    _userRequestedItem = fromUser;

    switch (_playMode) {
        case SinglePlay:
            if (fromUser) {
                if (_last + 1 < count()) {
                    _current = _last + 1;
                    _last = _current;
                    _engine->requestPlay(_current);
                    emit currentChanged();
                } else {
                    //ignore
                }
            } else {
                clear();
            }
            break;

        case SingleLoop:
            if (fromUser) {
                if (_engine->state() == PlayerEngine::Idle) {
                    Q_ASSERT(_last != -1);
                    _current = _last;
                    _engine->requestPlay(_current);

                } else {
                    if (_last + 1 < count()) {
                        _current = _last + 1;
                        _last = _current;
                        _engine->requestPlay(_current);
                        emit currentChanged();
                    } else {
                        _engine->stop();
                    }
                }
            } else {
                if (_engine->state() == PlayerEngine::Idle) {
                    _current = _last < 0 ? 0 : _last;
                    _engine->requestPlay(_current);
                    emit currentChanged();
                } else {
                    // replay current
                    _engine->requestPlay(_current);
                }
            }
            break;

        case ShufflePlay: {
            int i;
            for (i = 0; i < _playOrder.size(); ++i) {
                if (_playOrder[i] == _last) {
                    i = i + 1;
                    break;
                }
            }
            if (i < _playOrder.size()) {
                _last = _current = _playOrder[i];
                _engine->requestPlay(_current);
                emit currentChanged();
            } else {
                _engine->stop();
            }
            break;
        }

        case OrderPlay:
        case ListLoop:
            _last++;
            if (_last == count()) {
                if (_playMode == OrderPlay) {
                    clear();
                    break;

                } else {
                    _loopCount++;
                    _last = 0;
                } 
            }

            _current = _last;
            _engine->requestPlay(_current);
            emit currentChanged();
            break;
    }
}

void PlaylistModel::playPrev(bool fromUser)
{
    if (count() == 0) return;

    if (count() && _current > 0) {
        _current = _current - 1;
        _engine->requestPlay(_current);
        emit currentChanged();
    }
}

//TODO: what if loadfile failed
void PlaylistModel::append(const QFileInfo& fi)
{
    if (!fi.exists()) return;

    _infos.append(calculatePlayInfo(fi));
    emit countChanged();
}

void PlaylistModel::changeCurrent(int pos)
{
    if (pos < 0 || pos >= count()) return;

    _last = _current;
    _current = pos;
    Q_ASSERT_X(0, "playlist", "not implemented");
    emit currentChanged();
}

void PlaylistModel::switchPosition(int p1, int p2)
{
    Q_ASSERT_X(0, "playlist", "not implemented");
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

struct PlayItemInfo PlaylistModel::calculatePlayInfo(const QFileInfo& fi)
{
    std::vector<uint8_t> buf;
    _thumbnailer.generateThumbnail(fi.canonicalFilePath().toUtf8().toStdString(),
            ThumbnailerImageType::Png, buf);

    auto img = QImage::fromData(buf.data(), buf.size(), "png");

    QPixmap pm = QPixmap::fromImage(img);

    auto mi = MovieInfo::parseFromFile(fi);

    PlayItemInfo pif = {
        .info = fi,
        .thumbnail = pm.scaled(24, 44),
        .mi = mi
    };

    Q_ASSERT(!pif.thumbnail.isNull());

    return pif;
}

}


