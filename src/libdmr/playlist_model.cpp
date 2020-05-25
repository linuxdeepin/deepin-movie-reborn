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
#include "playlist_model.h"
#include "player_engine.h"
#include "utils.h"
#ifndef _LIBDMR_
#include "dmr_settings.h"
#endif
#include "dvd_utils.h"

#include <libffmpegthumbnailer/videothumbnailer.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/avutil.h>
}

#include <random>

static bool check_wayland()
{
    auto e = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

    //start...
    //solv problem that drop movie fast will update ui error,
    //get env var config item : QT_QPA_PLATFORM=xcb
    QString QT_QPA_PLATFORM = e.value(QStringLiteral("QT_QPA_PLATFORM"));
    if (QT_QPA_PLATFORM == QLatin1String("dxcb") || QT_QPA_PLATFORM == QLatin1String("xcb") ) {
        qDebug() << "QT_QPA_PLATFORM type is : " << QT_QPA_PLATFORM;
        return false;
    }
    //end...

    if (XDG_SESSION_TYPE == QLatin1String("wayland") || WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive))
        return true;
    else {
        return false;
    }

}

//获取音乐缩略图
static bool getMusicPix(const QFileInfo &fi, QPixmap &rImg)
{

    AVFormatContext *av_ctx = NULL;
    AVCodecContext *dec_ctx = NULL;

    if (!fi.exists()) {
        return false;
    }

    auto ret = avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), NULL, NULL);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
        return false;
    }

    if (avformat_find_stream_info(av_ctx, NULL) < 0) {
        qWarning() << "av_find_stream_info failed";
        return false;
    }

    // read the format headers  comment by thx , 这里会导致一些音乐 奔溃
    //if (av_ctx->iformat->read_header(av_ctx) < 0) {
    //    printf("No header format");
    //    return false;
    //}

    for (int i = 0; i < av_ctx->nb_streams; i++) {
        if (av_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket pkt = av_ctx->streams[i]->attached_pic;
            //使用QImage读取完整图片数据（注意，图片数据是为解析的文件数据，需要用QImage::fromdata来解析读取）
            //rImg = QImage::fromData((uchar *)pkt.data, pkt.size);
            return rImg.loadFromData((uchar *)pkt.data, pkt.size);
        }
    }
    return false;
}

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
QDebug operator<<(QDebug debug, const struct MovieInfo &mi)
{
    debug << "MovieInfo{"
          << mi.valid
          << mi.title
          << mi.fileType
          << mi.resolution
          << mi.filePath
          << mi.creation
          << mi.raw_rotate
          << mi.fileSize
          << mi.duration
          << mi.width
          << mi.height
          << mi.vCodecID
          << mi.vCodeRate
          << mi.fps
          << mi.proportion
          << mi.aCodeID
          << mi.aCodeRate
          << mi.aDigit
          << mi.channels
          << mi.sampling
          << "}";
    return debug;
}

QDataStream &operator<< (QDataStream &st, const MovieInfo &mi)
{
    st << mi.valid;
    st << mi.title;
    st << mi.fileType;
    st << mi.resolution;
    st << mi.filePath;
    st << mi.creation;
    st << mi.raw_rotate;
    st << mi.fileSize;
    st << mi.duration;
    st << mi.width;
    st << mi.height;
    st << mi.vCodecID;
    st << mi.vCodeRate;
    st << mi.fps;
    st << mi.proportion;
    st << mi.aCodeID;
    st << mi.aCodeRate;
    st << mi.aDigit;
    st << mi.channels;
    st << mi.sampling;
    return st;
}

QDataStream &operator>> (QDataStream &st, MovieInfo &mi)
{
    st >> mi.valid;
    st >> mi.title;
    st >> mi.fileType;
    st >> mi.resolution;
    st >> mi.filePath;
    st >> mi.creation;
    st >> mi.raw_rotate;
    st >> mi.fileSize;
    st >> mi.duration;
    st >> mi.width;
    st >> mi.height;
    st >> mi.vCodecID;
    st >> mi.vCodeRate;
    st >> mi.fps;
    st >> mi.proportion;
    st >> mi.aCodeID;
    st >> mi.aCodeRate;
    st >> mi.aDigit;
    st >> mi.channels;
    st >> mi.sampling;
    return st;
}

static class PersistentManager *_persistentManager = nullptr;

static QString hashUrl(const QUrl &url)
{
    return QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
}

//TODO: clean cache periodically
class PersistentManager: public QObject
{
    Q_OBJECT
public:
    static PersistentManager &get()
    {
        if (!_persistentManager) {
            _persistentManager = new PersistentManager;
        }
        return *_persistentManager;
    }

    struct CacheInfo {
        struct MovieInfo mi;
        QPixmap thumb;
        bool mi_valid {false};
        bool thumb_valid {false};
    };

    CacheInfo loadFromCache(const QUrl &url)
    {
        auto h = hashUrl(url);
        CacheInfo ci;

        {
            auto filename = QString("%1/%2").arg(_cacheInfoPath).arg(h);
            QFile f(filename);
            if (!f.exists()) return ci;

            if (f.open(QIODevice::ReadOnly)) {
                QDataStream ds(&f);
                ds >> ci.mi;
                ci.mi_valid = ci.mi.valid;
            } else {
                qWarning() << f.errorString();
            }
        }

        if (ci.mi_valid) {
            auto filename = QString("%1/%2").arg(_pixmapCachePath).arg(h);
            QFile f(filename);
            if (!f.exists()) return ci;

            if (f.open(QIODevice::ReadOnly)) {
                QDataStream ds(&f);
                ds >> ci.thumb;
                ci.thumb.setDevicePixelRatio(qApp->devicePixelRatio());
                ci.thumb_valid = !ci.thumb.isNull();
            } else {
                qWarning() << f.errorString();
            }
        }

        return ci;
    }

    void save(const PlayItemInfo &pif)
    {
        auto h = hashUrl(pif.url);

        bool mi_saved = false;

        {
            auto filename = QString("%1/%2").arg(_cacheInfoPath).arg(h);
            QFile f(filename);
            if (f.open(QIODevice::WriteOnly)) {
                QDataStream ds(&f);
                ds << pif.mi;
                mi_saved = true;
                qDebug() << "cache" << pif.url << "->" << h;
            } else {
                qWarning() << f.errorString();
            }
        }

        if (mi_saved) {
            auto filename = QString("%1/%2").arg(_pixmapCachePath).arg(h);
            QFile f(filename);
            if (f.open(QIODevice::WriteOnly)) {
                QDataStream ds(&f);
                ds << pif.thumbnail;
            } else {
                qWarning() << f.errorString();
            }
        }
    }

    bool cacheExists(const QUrl &url)
    {
        auto h = hashUrl(url);
        auto filename = QString("%1/%2").arg(_cacheInfoPath).arg(h);
        return QFile::exists(filename);
    }

private:
    PersistentManager()
    {
        auto tmpl = QString("%1/%2/%3/%4")
                    .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                    .arg(qApp->organizationName())
                    .arg(qApp->applicationName());
        {
            _cacheInfoPath = tmpl.arg("cacheinfo");
            QDir d;
            d.mkpath(_cacheInfoPath);
        }
        {
            _pixmapCachePath = tmpl.arg("thumbs");
            QDir d;
            d.mkpath(_pixmapCachePath);
        }
    }

    QString _pixmapCachePath;
    QString _cacheInfoPath;

};

struct MovieInfo MovieInfo::parseFromFile(const QFileInfo &fi, bool *ok)
{
    struct MovieInfo mi;
    mi.valid = false;
    AVFormatContext *av_ctx = NULL;
    int stream_id = -1;
    AVCodecContext *dec_ctx = NULL;

    if (!fi.exists()) {
        if (ok) *ok = false;
        return mi;
    }

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
        if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_AUDIO) < 0) {
            if (ok) *ok = false;
            return mi;
        }
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

    mi.vCodecID = dec_ctx->codec_id;
    mi.vCodeRate = dec_ctx->bit_rate;
    if (dec_ctx->framerate.den != 0) {
        mi.fps = dec_ctx->framerate.num / dec_ctx->framerate.den;
    } else {
        mi.fps = 0;
    }
    if (mi.height != 0) {
        mi.proportion = mi.width / mi.height;
    } else {
        mi.proportion = 0;
    }

    if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_AUDIO) < 0) {
        if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
            if (ok) *ok = false;
            return mi;
        }
    }


    mi.aCodeID = dec_ctx->codec_id;
    mi.aCodeRate = dec_ctx->bit_rate;
    mi.aDigit = dec_ctx->sample_fmt;
    mi.channels = dec_ctx->channels;
    mi.sampling = dec_ctx->sample_rate;

    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(av_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        if (tag->key && strcmp(tag->key, "creation_time") == 0) {
            auto dt = QDateTime::fromString(tag->value, Qt::ISODate);
            mi.creation = dt.toString();
            qDebug() << __func__ << dt.toString();
            break;
        }
        qDebug() << "tag:" << tag->key << tag->value;
    }

    tag = NULL;
    AVStream *st = av_ctx->streams[stream_id];
    while ((tag = av_dict_get(st->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        if (tag->key && strcmp(tag->key, "rotate") == 0) {
            mi.raw_rotate = QString(tag->value).toInt();
            auto vr = (mi.raw_rotate + 360) % 360;
            if (vr == 90 || vr == 270) {
                auto tmp = mi.height;
                mi.height = mi.width;
                mi.width = tmp;
            }
            break;
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
    : _engine(e)
{
    m_pdataMutex = new QMutex();
    m_ploadThread = nullptr;
    m_brunning = false;
    _thumbnailer.setThumbnailSize(400 * qApp->devicePixelRatio());
    av_register_all();

    _playlistFile = QString("%1/%2/%3/playlist")
                    .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                    .arg(qApp->organizationName())
                    .arg(qApp->applicationName());

    connect(e, &PlayerEngine::stateChanged, [ = ]() {
        qDebug() << "model" << "_userRequestingItem" << _userRequestingItem << "state" << e->state();
        switch (e->state()) {
        case PlayerEngine::Playing: {
            auto &pif = currentInfo();
            if (!pif.url.isLocalFile() && !pif.loaded) {
                pif.mi.width = e->videoSize().width();
                pif.mi.height = e->videoSize().height();
                pif.mi.duration = e->duration();
                pif.loaded = true;
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

#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        int restore_pos = Settings::get().internalOption("playlist_pos").toInt();
        _last = restore_pos;
    }
#endif
}

PlaylistModel::~PlaylistModel()
{
    qDebug() << __func__;
    delete _jobWatcher;

    delete m_pdataMutex;

#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ClearWhenQuit)) {
        clearPlaylist();
    } else {
        //persistently save current playlist
        savePlaylist();
    }
#endif
}

qint64 PlaylistModel::getUrlFileTotalSize(QUrl url, int tryTimes) const
{
    qint64 size = -1;

    if (tryTimes <= 0) {
        tryTimes = 1;
    }

    do {
        QNetworkAccessManager manager;
        // 事件循环，等待请求文件头信息结束;
        QEventLoop loop;
        // 超时，结束事件循环;
        QTimer timer;

        //发出请求，获取文件地址的头部信息;
        QNetworkReply *reply = manager.head(QNetworkRequest(QUrl(url)));//QNetworkRequest(url)
        if (!reply)
            continue;

        QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
        QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));

        timer.start(5000);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << reply->errorString();
            continue;
        }
        QVariant var = reply->header(QNetworkRequest::ContentLengthHeader);
        size = var.toLongLong();
        reply->deleteLater();
//        qDebug() << reply->hasRawHeader("Content-Encoding ");
//        qDebug() << reply->hasRawHeader("Content-Language");
//        qDebug() << reply->hasRawHeader("Content-Length");
//        qDebug() << reply->hasRawHeader("Content-Type");
//        qDebug() << reply->hasRawHeader("Last-Modified");
//        qDebug() << reply->hasRawHeader("Expires");

        break;


    } while (tryTimes--);



    return size;
}

void PlaylistModel::clearPlaylist()
{
    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");
    cfg.endGroup();
}

void PlaylistModel::savePlaylist()
{
    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");

    for (int i = 0; i < count(); ++i) {
        const auto &pif = _infos[i];
        cfg.setValue(QString::number(i), pif.url);
        qDebug() << "save " << pif.url;
    }
    cfg.endGroup();
    cfg.sync();
}

void PlaylistModel::loadPlaylist()
{
    QList<QUrl> urls;

    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    auto keys = cfg.childKeys();
    for (int i = 0; i < keys.size(); ++i) {
        auto url = cfg.value(QString::number(i)).toUrl();
        if (indexOf(url) >= 0) continue;

        if (url.isLocalFile()) {
            urls.append(url);

        } else {
            auto pif = calculatePlayInfo(url, QFileInfo());
            _infos.append(pif);
        }
    }
    cfg.endGroup();

    if (urls.size() == 0) {
        _firstLoad = false;
        reshuffle();
        emit countChanged();
        return;
    }

    QTimer::singleShot(0, [ = ]() {
        delayedAppendAsync(urls);
    });
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
            _engine->waitLastEnd();

        } else if (pos < _current) {
            _current--;
            _last = _current;
        }
    } else {
        if (_current == pos) {
            _last = _current;
            _current = -1;
            _engine->waitLastEnd();
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
    savePlaylist();
}

void PlaylistModel::stop()
{
    _current = -1;
    emit currentChanged();
}

void PlaylistModel::tryPlayCurrent(bool next)
{
    auto &pif = _infos[_current];
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
                _last = _last == -1 ? 0 : _last;
                _current = _last;
                tryPlayCurrent(true);

            } else {
                if (_last + 1 >= count()) {
                    _last = -1;
                }
                _engine->waitLastEnd();
                _current = _last + 1;
                _last = _current;
                tryPlayCurrent(true);
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
        qDebug() << "shuffle next " << _shufflePlayed - 1;
        _engine->waitLastEnd();
        _last = _current = _playOrder[_shufflePlayed - 1];
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
                _last = _last == -1 ? 0 : _last;
                _current = _last;
                tryPlayCurrent(false);

            } else {
                if (_last - 1 < 0) {
                    _last = count();
                }
                _engine->waitLastEnd();
                _current = _last - 1;
                _last = _current;
                tryPlayCurrent(false);
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
        qDebug() << "shuffle prev " << _shufflePlayed - 1;
        _engine->waitLastEnd();
        _last = _current = _playOrder[_shufflePlayed - 1];
        tryPlayCurrent(false);
        break;
    }

    case OrderPlay:
        _last--;
        if (_last < 0) {
            _last = count() - 1;
        }

        _engine->waitLastEnd();
        _current = _last;
        tryPlayCurrent(false);
        break;

    case ListLoop:
        _last--;
        if (_last < 0) {
            _loopCount++;
            _last = count() - 1;
        }

        _engine->waitLastEnd();
        _current = _last;
        tryPlayCurrent(false);
        break;
    }

    _userRequestingItem = false;

}

static QDebug operator<<(QDebug s, const QFileInfoList &v)
{
    std::for_each(v.begin(), v.end(), [&](const QFileInfo & fi) {
        s << fi.fileName();
    });
    return s;
}

void PlaylistModel::appendSingle(const QUrl &url)
{
    if (indexOf(url) >= 0) return;

    if (url.isLocalFile()) {
        QFileInfo fi(url.toLocalFile());
        if (!fi.exists()) return;
        auto pif = calculatePlayInfo(url, fi);
        if (!pif.valid) return;
        _infos.append(pif);

#ifndef _LIBDMR_
        if (Settings::get().isSet(Settings::AutoSearchSimilar)) {
            auto fil = utils::FindSimilarFiles(fi);
            qDebug() << "auto search similar files" << fil;
            std::for_each(fil.begin(), fil.end(), [ = ](const QFileInfo & fi) {
                auto url = QUrl::fromLocalFile(fi.absoluteFilePath());
                if (indexOf(url) < 0 && _engine->isPlayableFile(fi.fileName())) {
                    auto pif = calculatePlayInfo(url, fi);
                    if (pif.valid) _infos.append(pif);
                }
            });
        }
#endif
    } else {
        auto pif = calculatePlayInfo(url, QFileInfo(), true);
        _infos.append(pif);
    }
}

void PlaylistModel::collectionJob(const QList<QUrl> &urls)
{
    for (const auto &url : urls) {
        if (!url.isValid() || indexOf(url) >= 0 || !url.isLocalFile() || _urlsInJob.contains(url.toLocalFile()))
            continue;

        QFileInfo fi(url.toLocalFile());
        if (!_firstLoad && (!fi.exists() || !fi.isFile())) continue;

        _pendingJob.append(qMakePair(url, fi));
        _urlsInJob.insert(url.toLocalFile());
        qDebug() << "append " << url.fileName();

#ifndef _LIBDMR_
        if (!_firstLoad && Settings::get().isSet(Settings::AutoSearchSimilar)) {
            auto fil = utils::FindSimilarFiles(fi);
            qDebug() << "auto search similar files" << fil;
            std::for_each(fil.begin(), fil.end(), [ = ](const QFileInfo & fi) {
                if (fi.isFile()) {
                    auto url = QUrl::fromLocalFile(fi.absoluteFilePath());

                    if (!_urlsInJob.contains(url.toLocalFile()) && indexOf(url) < 0 &&
                            _engine->isPlayableFile(fi.fileName())) {
                        _pendingJob.append(qMakePair(url, fi));
                        _urlsInJob.insert(url.toLocalFile());
                    }
                }
            });
        }
#endif
    }

    qDebug() << "input size" << urls.size() << "output size" << _urlsInJob.size()
             << "_pendingJob: " << _pendingJob.size();
}

void PlaylistModel::appendAsync(const QList<QUrl> &urls)
{
    if (check_wayland()) {
        if (m_ploadThread == nullptr) {
            m_ploadThread = new LoadThread(this, urls);
            connect(m_ploadThread, &QThread::finished, this, &PlaylistModel::deleteThread);
        }
        if (!m_ploadThread->isRunning()) {
            m_ploadThread->start();
            m_brunning = m_ploadThread->isRunning();
        }
    } else {
        QTimer::singleShot(10, [ = ]() {
            delayedAppendAsync(urls);
        });
    }
}

void PlaylistModel::deleteThread()
{
    if (check_wayland()) {
        if (m_ploadThread == nullptr)
            return ;
        if (m_ploadThread->isRunning()) {
            m_ploadThread->wait();
        }
        delete m_ploadThread;
        m_ploadThread = nullptr;
        m_brunning = false;
    }
}

void PlaylistModel::delayedAppendAsync(const QList<QUrl> &urls)
{
    if (_pendingJob.size() > 0) {
        //TODO: may be automatically schedule later
        qWarning() << "there is a pending append going on, enqueue";
        m_pdataMutex->lock();
        _pendingAppendReq.enqueue(urls);
        m_pdataMutex->unlock();
        return;
    }

    m_pdataMutex->lock();
    collectionJob(urls);
    m_pdataMutex->unlock();

    if (!_pendingJob.size()) return;

    struct MapFunctor {
        PlaylistModel *_model = 0;
        using result_type = PlayItemInfo;
        MapFunctor(PlaylistModel *model): _model(model) {}

        struct PlayItemInfo operator()(const AppendJob &a)
        {
            qDebug() << "mapping " << a.first.fileName();
            return _model->calculatePlayInfo(a.first, a.second);
        };
    };

    if (check_wayland()) {
        m_pdataMutex->lock();
        PlayItemInfoList pil;
        for (const auto &a : _pendingJob) {
            qDebug() << "sync mapping " << a.first.fileName();
            pil.append(calculatePlayInfo(a.first, a.second));
            if (m_ploadThread && m_ploadThread->isRunning()) {
                m_ploadThread->msleep(10);
            }
        }
        _pendingJob.clear();
        _urlsInJob.clear();

        m_pdataMutex->unlock();

        handleAsyncAppendResults(pil);
    } else {
        if (QThread::idealThreadCount() > 1) {
            auto future = QtConcurrent::mapped(_pendingJob, MapFunctor(this));
            _jobWatcher->setFuture(future);
        } else {
            PlayItemInfoList pil;
            for (const auto &a : _pendingJob) {
                qDebug() << "sync mapping " << a.first.fileName();
                pil.append(calculatePlayInfo(a.first, a.second));
                if (m_ploadThread && m_ploadThread->isRunning()) {
                    m_ploadThread->msleep(10);
                }
            }
            _pendingJob.clear();
            _urlsInJob.clear();
            handleAsyncAppendResults(pil);
        }
    }

}

static QList<PlayItemInfo> &SortSimilarFiles(QList<PlayItemInfo> &fil)
{
    //sort names by digits inside, take care of such a possible:
    //S01N04, S02N05, S01N12, S02N04, etc...
    struct {
        bool operator()(const PlayItemInfo &fi1, const PlayItemInfo &fi2) const
        {
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
    qDebug() << __func__;
    auto f = _jobWatcher->future();
    _pendingJob.clear();
    _urlsInJob.clear();

    auto fil = f.results();
    handleAsyncAppendResults(fil);
}

void PlaylistModel::handleAsyncAppendResults(QList<PlayItemInfo> &fil)
{
    qDebug() << __func__ << fil.size();
    if (!_firstLoad) {
        //since _infos are modified only at the same thread, the lock is not necessary
        auto last = std::remove_if(fil.begin(), fil.end(), [](const PlayItemInfo & pif) {
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
    savePlaylist();
}

bool PlaylistModel::hasPendingAppends()
{
    return _pendingAppendReq.size() > 0 || _pendingJob.size() > 0;
}

//TODO: what if loadfile failed
void PlaylistModel::append(const QUrl &url)
{
    if (!url.isValid()) return;

    appendSingle(url);
    reshuffle();
    emit itemsAppended();
    emit countChanged();
}

void PlaylistModel::changeCurrent(int pos)
{
    if (pos < 0 || pos >= count() || _current == pos) return;

    _userRequestingItem = true;

    _engine->waitLastEnd();
    _current = pos;
    _last = _current;
    tryPlayCurrent(true);
    _userRequestingItem = false;
    emit currentChanged();
}

void PlaylistModel::switchPosition(int src, int target)
{
    //Q_ASSERT_X(0, "playlist", "not implemented");
    Q_ASSERT (src < _infos.size() && target < _infos.size());
    _infos.move(src, target);

    int min = qMin(src, target);
    int max = qMax(src, target);
    if (_current >= min && _current <= max) {
        if (_current == src) {
            _current = target;
            _last = _current;
        } else {
            if (src < target) {
                _current--;
                _last = _current;
            } else if (src > target) {
                _current++;
                _last = _current;
            }
        }
        emit currentChanged();
    }
}

PlayItemInfo &PlaylistModel::currentInfo()
{
    //Q_ASSERT (_infos.size() > 0 && _current >= 0);
    Q_ASSERT (_infos.size() > 0);

    if (_current >= 0)
        return _infos[_current];
    if (_last >= 0)
        return _infos[_last];
    return _infos[0];
}

const PlayItemInfo &PlaylistModel::currentInfo() const
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

bool PlaylistModel::getthreadstate()
{
    if (m_ploadThread) {
        m_brunning = m_ploadThread->isRunning();
    } else {
        m_brunning = false;
    }
    return m_brunning;
}

struct PlayItemInfo PlaylistModel::calculatePlayInfo(const QUrl &url, const QFileInfo &fi, bool isDvd)
{
    bool ok = false;
    struct MovieInfo mi;
    auto ci = PersistentManager::get().loadFromCache(url);
    if (ci.mi_valid && url.isLocalFile()) {
        mi = ci.mi;
        ok = true;
        qDebug() << "load cached MovieInfo" << mi;
    } else {

        mi = MovieInfo::parseFromFile(fi, &ok);
        if (isDvd && url.scheme().startsWith("dvd")) {
            QString dev = url.path();
            if (dev.isEmpty()) dev = "/dev/sr0";
            dmr::dvd::RetrieveDvdThread::get()->startDvd(dev);
//            mi.title = dmr::dvd::RetrieveDVDTitle(dev);
//            if (mi.title.isEmpty()) {
//              mi.title = "DVD";
//            }
//            mi.valid = true;
        } else if (!url.isLocalFile()) {
            QString msg = url.fileName();
            if (msg != "sr0" || msg != "cdrom") {
                if (msg.isEmpty()) msg = url.path();
                mi.title = msg;
                mi.valid = true;
            }
        } else {
            mi.title = fi.fileName();
        }
    }

    QPixmap pm;
    if (ci.thumb_valid) {
        pm = ci.thumb;
        qDebug() << "load cached thumb" << url;
    } else if (ok) {
        try {
            //如果打开的是音乐就读取音乐缩略图
            bool isMusic = false;
            foreach (QString sf, _engine->audio_filetypes) {
                if (sf.right(sf.size() - 2) == mi.fileType) {
                    isMusic = true;
                }
            }
            if (isMusic == false) {
                std::vector<uint8_t> buf;
                _thumbnailer.generateThumbnail(fi.canonicalFilePath().toUtf8().toStdString(),
                                               ThumbnailerImageType::Png, buf);

                auto img = QImage::fromData(buf.data(), buf.size(), "png");
                pm = QPixmap::fromImage(img);
            } else {
                if (getMusicPix(fi, pm) == false) {
                    pm.load(":/resources/icons/logo-big.svg");
                }
            }
            pm.setDevicePixelRatio(qApp->devicePixelRatio());
        } catch (const std::logic_error &) {
        }
    }

    PlayItemInfo pif { fi.exists() || !url.isLocalFile(), ok, url, fi, pm, mi };
    if (ok && url.isLocalFile() && (!ci.mi_valid || !ci.thumb_valid)) {
        PersistentManager::get().save(pif);
    }
    if (!url.isLocalFile() && !url.scheme().startsWith("dvd")) {
        pif.mi.filePath = pif.url.path();

        pif.mi.width = _engine->_current->width();
        pif.mi.height = _engine->_current->height();
        pif.mi.resolution = QString::number(_engine->_current->width()) + "x"
                            + QString::number(_engine->_current->height());

        pif.mi.duration = _engine->_current->duration();
        auto suffix = pif.mi.title.mid(pif.mi.title.lastIndexOf('.'));
        suffix.replace(QString("."), QString(""));
        pif.mi.fileType = suffix;
        pif.mi.fileSize = getUrlFileTotalSize(url, 3);
        pif.mi.filePath = url.toDisplayString();
    }
    return pif;
}

int PlaylistModel::indexOf(const QUrl &url)
{
    auto p = std::find_if(_infos.begin(), _infos.end(), [&](const PlayItemInfo & pif) {
        return pif.url == url;
    });

    if (p == _infos.end()) return -1;
    return std::distance(_infos.begin(), p);
}


LoadThread::LoadThread(PlaylistModel *model, const QList<QUrl> &urls)
{
    _pModel = nullptr;
    _pModel = model;
    _urls = urls;
}
LoadThread::~LoadThread()
{
    _pModel = nullptr;
}

void LoadThread::run()
{
    if (_pModel) {
        _pModel->delayedAppendAsync(_urls);
    }
}

}

#include "playlist_model.moc"

