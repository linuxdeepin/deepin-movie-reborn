// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "playlist_model.h"
#include "player_engine.h"
#include "utils.h"
#ifndef _LIBDMR_
#include "dmr_settings.h"
#endif
#include "dvd_utils.h"
#include "compositing_manager.h"
#include "gstutils.h"
#include "sysutils.h"

#include <QSvgRenderer>

#include <random>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/avutil.h>
}

typedef int (*mvideo_avformat_open_input)(AVFormatContext **ps, const char *url, AVInputFormat *fmt, AVDictionary **options);
typedef int (*mvideo_avformat_find_stream_info)(AVFormatContext *ic, AVDictionary **options);
typedef int (*mvideo_av_find_best_stream)(AVFormatContext *ic, enum AVMediaType type, int wanted_stream_nb, int related_stream, AVCodec **decoder_ret, int flags);
typedef void (*mvideo_avformat_close_input)(AVFormatContext **s);
typedef AVDictionaryEntry *(*mvideo_av_dict_get)(const AVDictionary *m, const char *key, const AVDictionaryEntry *prev, int flags);
typedef void (*mvideo_av_dump_format)(AVFormatContext *ic,int index, const char *url, int is_output);
typedef AVCodec *(*mvideo_avcodec_find_decoder)(enum AVCodecID id);
typedef const char *(*mvideo_av_get_media_type_string)(enum AVMediaType media_type);
typedef AVCodecContext *(*mvideo_avcodec_alloc_context3)(const AVCodec *codec);
typedef int (*mvideo_avcodec_parameters_to_context)(AVCodecContext *codec, const AVCodecParameters *par);
typedef int (*mvideo_avcodec_open2)(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);
typedef void (*mvideo_avcodec_free_context)(AVCodecContext **avctx);


mvideo_avformat_open_input g_mvideo_avformat_open_input = nullptr;
mvideo_avformat_find_stream_info g_mvideo_avformat_find_stream_info = nullptr;
mvideo_av_find_best_stream g_mvideo_av_find_best_stream = nullptr;
mvideo_avformat_close_input g_mvideo_avformat_close_input = nullptr;
mvideo_av_dict_get g_mvideo_av_dict_get = nullptr;
mvideo_av_dump_format g_mvideo_av_dump_format = nullptr;
mvideo_avcodec_find_decoder g_mvideo_avcodec_find_decoder = nullptr;
mvideo_av_get_media_type_string g_mvideo_av_get_media_type_string = nullptr;
mvideo_avcodec_alloc_context3 g_mvideo_avcodec_alloc_context3 = nullptr;
mvideo_avcodec_parameters_to_context g_mvideo_avcodec_parameters_to_context = nullptr;
mvideo_avcodec_open2 g_mvideo_avcodec_open2 = nullptr;
mvideo_avcodec_free_context g_mvideo_avcodec_free_context = nullptr;

namespace dmr {
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
#ifdef _MOVIE_USE_
    st << mi.strFmtName;
#endif
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
#ifdef _MOVIE_USE_
    st >> mi.strFmtName;
#endif
    return st;
}

static class PersistentManager *_persistentManager = nullptr;

static QString hashUrl(const QUrl &url)
{
    qDebug() << "Entering hashUrl() with URL:" << url.toString();
    QString ret = QString(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
    qDebug() << "Exiting hashUrl() with hashed URL:" << ret;
    return ret;
}

//TODO: clean cache periodically
class PersistentManager: public QObject
{
    Q_OBJECT
public:
    static PersistentManager &get()
    {
        if (!_persistentManager) {
            qDebug() << "_persistentManager is null, creating new instance.";
            _persistentManager = new PersistentManager;
        }
        return *_persistentManager;
    }

    struct CacheInfo {
        struct MovieInfo mi;
        QPixmap thumb;
        QPixmap thumb_dark;
        bool mi_valid {false};
        bool thumb_valid {false};
//        char m_padding [6];//占位符
        CacheInfo() {
            qDebug() << "Entering PersistentManager::CacheInfo::CacheInfo() constructor.";
            thumb = QPixmap();
            thumb_dark = QPixmap();
            mi_valid = false;
            thumb_valid = false;
            qDebug() << "Exiting PersistentManager::CacheInfo::CacheInfo() constructor.";
        }
    };

    CacheInfo loadFromCache(const QUrl &url)
    {
        qDebug() << "Entering PersistentManager::loadFromCache() with URL:" << url.toString();
        auto h = hashUrl(url);
        CacheInfo ci;

        {
            auto filename = QString("%1/%2").arg(_cacheInfoPath).arg(h);
            QFile f(filename);
            if (!f.exists()) {
                qDebug() << "Cache info file does not exist:" << filename << ", returning empty CacheInfo.";
                return ci;
            }

            if (f.open(QIODevice::ReadOnly)) {
                qDebug() << "Cache info file opened for reading:" << filename;
                QDataStream ds(&f);
                ds >> ci.mi;
                ci.mi_valid = ci.mi.valid;
            } else {
                qWarning() << f.errorString();
            }
            f.close();
            qDebug() << "Cache info file closed.";
        }

        if (ci.mi_valid) {
            qDebug() << "MovieInfo is valid, checking pixmap cache.";
            auto filename = QString("%1/%2").arg(_pixmapCachePath).arg(h);
            QFile f(filename);
            if (!f.exists()) {
                qDebug() << "Pixmap cache file does not exist:" << filename << ", returning CacheInfo.";
                return ci;
            }

            if (f.open(QIODevice::ReadOnly)) {
                qDebug() << "Pixmap cache file opened for reading:" << filename;
                QDataStream ds(&f);
                ds >> ci.thumb;
                ds >> ci.thumb_dark;
                ci.thumb.setDevicePixelRatio(qApp->devicePixelRatio());
                ci.thumb_dark.setDevicePixelRatio(qApp->devicePixelRatio());
                if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
                    qDebug() << "Dark theme detected, setting thumb_valid based on thumb_dark.";
                    ci.thumb_valid = !ci.thumb_dark.isNull();
                } else {
                    qDebug() << "Light theme detected, setting thumb_valid based on thumb.";
                    ci.thumb_valid = !ci.thumb.isNull();
                }
            } else {
                qWarning() << f.errorString();
            }
            f.close();
            qDebug() << "Pixmap cache file closed.";
        }

        qDebug() << "Exiting PersistentManager::loadFromCache() with CacheInfo. mi_valid:" << ci.mi_valid << ", thumb_valid:" << ci.thumb_valid;
        return ci;
    }

    void save(const PlayItemInfo &pif)
    {
        qDebug() << "Entering PersistentManager::save() with PlayItemInfo.";
        auto h = hashUrl(pif.url);

        bool mi_saved = false;

        {
            auto filename = QString("%1/%2").arg(_cacheInfoPath).arg(h);
            QFile f(filename);
            if (f.open(QIODevice::WriteOnly)) {
                QDataStream ds(&f);
                ds << pif.mi;
                mi_saved = true;
                qInfo() << "cache" << pif.url << "->" << h;
            } else {
                qWarning() << f.errorString();
            }
            f.close();
        }

        if (mi_saved) {
            qDebug() << "MovieInfo saved successfully.";
            auto filename = QString("%1/%2").arg(_pixmapCachePath).arg(h);
            QFile f(filename);
            if (f.open(QIODevice::WriteOnly)) {
                QDataStream ds(&f);
                ds << pif.thumbnail;
                ds << pif.thumbnail_dark;
            } else {
                qWarning() << f.errorString();
            }
            f.close();
        }
        qDebug() << "Exiting PersistentManager::save() with PlayItemInfo.";
    }

private:
    PersistentManager()
    {
        qDebug() << "Entering PersistentManager constructor.";
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
        qDebug() << "Exiting PersistentManager constructor.";
    }

    QString _pixmapCachePath;
    QString _cacheInfoPath;

};

struct MovieInfo PlaylistModel::parseFromFile(const QFileInfo &fi, bool *ok)
{
    qDebug() << "Entering PlaylistModel::parseFromFile() with file:" << fi.filePath();
    struct MovieInfo mi;
    mi.valid = false;
    AVFormatContext *av_ctx = nullptr;
    AVCodecParameters *video_dec_ctx = nullptr;
    AVCodecParameters *audio_dec_ctx = nullptr;

    if (!CompositingManager::isMpvExists()) {
        qDebug() << "MPV does not exist, parsing with Qt.";
        return parseFromFileByQt(fi, ok);
    }

    if (!fi.exists()) {
        qDebug() << "File does not exist:" << fi.filePath() << ", returning invalid MovieInfo.";
        if (ok) *ok = false;
        return mi;
    }

    auto ret = g_mvideo_avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
        if (ok) *ok = false;
        return mi;
    }

    if (g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0) {
        qWarning() << "av_find_stream_info failed";
        if (ok) *ok = false;
        return mi;
    }

    if (av_ctx->nb_streams == 0) {
        qDebug() << "No streams found in AVFormat context, returning invalid MovieInfo.";
        if (ok) *ok = false;
        return mi;
    }

    int videoRet = -1;
    int audioRet = -1;
    AVStream *videoStream = nullptr;
    AVStream *audioStream = nullptr;
    videoRet = g_mvideo_av_find_best_stream(av_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioRet = g_mvideo_av_find_best_stream(av_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (videoRet < 0 && audioRet < 0) {
        qDebug() << "No video or audio stream found, returning invalid MovieInfo.";
        if (ok) *ok = false;
        return mi;
    }

    if (videoRet >= 0) {
        qDebug() << "Video stream found at index:" << videoRet;
        int video_stream_index = -1;
        video_stream_index = videoRet;
        videoStream = av_ctx->streams[video_stream_index];
        video_dec_ctx = videoStream->codecpar;

        mi.width = video_dec_ctx->width;
        mi.height = video_dec_ctx->height;
        mi.vCodecID = video_dec_ctx->codec_id;
        mi.vCodeRate = video_dec_ctx->bit_rate;
#ifdef _MOVIE_USE_
        mi.strFmtName = av_ctx->iformat->long_name;
#endif

        if (videoStream->avg_frame_rate.den != 0) {
            qDebug() << "Video frame rate denominator is not zero, calculating fps.";
            mi.fps = static_cast<int>(round(static_cast<double>(videoStream->avg_frame_rate.num) / videoStream->avg_frame_rate.den));
        } else {
            qDebug() << "Video frame rate denominator is zero, setting fps to 0.";
            mi.fps = 0;
        }
        if (mi.height != 0) {
            qDebug() << "Video height is not zero, calculating proportion.";
            mi.proportion = static_cast<float>(mi.width) / static_cast<float>(mi.height);
        } else {
            qDebug() << "Video height is zero, setting proportion to 0.";
            mi.proportion = 0;
        }

        AVCodecContext *codec_context = g_mvideo_avcodec_alloc_context3(NULL);
        g_mvideo_avcodec_parameters_to_context(codec_context, video_dec_ctx);
        AVCodec *videoCodec = g_mvideo_avcodec_find_decoder(video_dec_ctx->codec_id);
        if (g_mvideo_avcodec_open2(codec_context, videoCodec, 0) > 0) {
            qDebug() << "Video codec opened successfully, setting property.";
            //用唯一的文件名绑定对应视频的对应pix_fmt值
            setProperty(fi.filePath().toUtf8(), codec_context->pix_fmt);
        } else {
            qDebug() << "Failed to open video codec.";
        }
        g_mvideo_avcodec_free_context(&codec_context);
    }
    if (audioRet >= 0) {
        qDebug() << "Audio stream found at index:" << audioRet;
        int audio_stream_index = -1;
        audio_stream_index = audioRet;
        audioStream = av_ctx->streams[audio_stream_index];
        audio_dec_ctx = audioStream->codecpar;

        mi.aCodeID = audio_dec_ctx->codec_id;
        mi.aCodeRate = audio_dec_ctx->bit_rate;
        mi.aDigit = audio_dec_ctx->format;
        mi.channels = audio_dec_ctx->channels;
        mi.sampling = audio_dec_ctx->sample_rate;

#ifdef USE_TEST
        QPixmap musicimage;
        getMusicPix(fi, musicimage);
        qDebug() << "Music image retrieved for testing.";
#endif
    }

    auto duration = av_ctx->duration == AV_NOPTS_VALUE ? 0 : av_ctx->duration;
    qDebug() << "Calculated duration:" << duration;
    duration = duration + (duration <= INT64_MAX - 5000 ? 5000 : 0);
    mi.duration = duration / AV_TIME_BASE;
    mi.resolution = QString("%1x%2").arg(mi.width).arg(mi.height);
    mi.title = fi.fileName(); //FIXME this
    mi.filePath = fi.canonicalFilePath();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qInfo() << __func__ << "created:" << fi.created() << "       toString:" << fi.created().toString();
    mi.creation = fi.created().toString();
#else
    qInfo() << __func__ << "created:" << fi.birthTime() << "       toString:" << fi.birthTime().toString();
    mi.creation = fi.birthTime().toString();
#endif
    mi.fileSize = fi.size();
    mi.fileType = fi.suffix();
#ifdef _MOVIE_USE_
    mi.strFmtName = av_ctx->iformat->long_name;
#endif
    AVDictionaryEntry *tag = nullptr;
    while ((tag = g_mvideo_av_dict_get(av_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        qDebug() << "Processing metadata tag:" << tag->key;
        if (tag->key && strcmp(tag->key, "creation_time") == 0) {
            qDebug() << "Found creation_time tag.";
            auto dt = QDateTime::fromString(tag->value, Qt::ISODate);
            mi.creation = dt.toString();
            qInfo() << __func__ << dt.toString();
            break;
        }
        qInfo() << "tag:" << tag->key << tag->value;
    }

    g_mvideo_avformat_close_input(&av_ctx);
    mi.valid = true;
    qDebug() << "MovieInfo is valid, closing AVFormat context.";

    if (ok) {
        qDebug() << "ok pointer is not null, setting to true.";
        *ok = true;
    } else {
        qDebug() << "ok pointer is null.";
    }
    qDebug() << "Exiting PlaylistModel::parseFromFile() with valid:" << mi.valid;
    return mi;
}

MovieInfo PlaylistModel::parseFromFileByQt(const QFileInfo &fi, bool *ok)
{
    qDebug() << "Entering PlaylistModel::parseFromFileByQt() with file:" << fi.filePath();
    struct MovieInfo mi;

    mi = GstUtils::get()->parseFileByGst(fi);

    if (fi.exists()) {
        qDebug() << "File exists, setting ok to true.";
        *ok = true;
    }

    qDebug() << "Exiting PlaylistModel::parseFromFileByQt().";
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

void PlaylistModel::slotStateChanged()
{
    qDebug() << "Entering PlaylistModel::slotStateChanged().";
    PlayerEngine *e = dynamic_cast<PlayerEngine *>(sender());
    if (!e) {
        qDebug() << "Sender is not a PlayerEngine instance, returning.";
        return;
    }
    qInfo() << "model" << "_userRequestingItem" << _userRequestingItem << "state" << e->state();
    switch (e->state()) {
    case PlayerEngine::Playing: {
        qDebug() << "Player state changed to Playing.";
        auto &pif = currentInfo();
        if (!pif.url.isLocalFile() && !pif.loaded) {
            qDebug() << "Not a local file or not loaded, updating MovieInfo.";
            pif.mi.width = e->videoSize().width();
            pif.mi.height = e->videoSize().height();
            pif.mi.duration = e->duration();
            pif.loaded = true;
            emit itemInfoUpdated(_current);
        }
        _userRequestingItem = false;
        break;
    }
    case PlayerEngine::Paused:
        qDebug() << "Player state changed to Paused.";
        break;

    case PlayerEngine::Idle:
        qDebug() << "Player state changed to Idle.";
        if (!_userRequestingItem) {
            qDebug() << "Not a user requesting item, stopping and playing next.";
            stop();
            //WINID方式渲染结束时，保证gpu渲染资源的正常释放与切换，延时5ms执行下部视频的播放
            if(!CompositingManager::get().composited()) {
                qDebug() << "Compositing manager not composited, delaying playNext.";
                QTimer::singleShot(5, [=]() {
                    playNext(false);
                });
            } else {
                qDebug() << "Compositing manager composited, playing next immediately.";
                playNext(false);
            }
        }
        break;
    }
    qDebug() << "Exiting PlaylistModel::slotStateChanged().";
}

PlaylistModel::PlaylistModel(PlayerEngine *e)
    : _engine(e)
{
    qDebug() << "Initializing PlaylistModel";
    m_pdataMutex = new QMutex();
    m_ploadThread = nullptr;
    m_brunning = false;

    _playlistFile = QString("%1/%2/%3/playlist")
                    .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                    .arg(qApp->organizationName())
                    .arg(qApp->applicationName());

    connect(e, &PlayerEngine::stateChanged, this, &PlaylistModel::slotStateChanged);

    stop();

#ifdef _LIBDMR_
    qDebug() << "Initializing thumbnail and FFmpeg in LIBDMR mode";
    initThumb();
    initFFmpeg();
#endif
#ifndef _LIBDMR_
    qDebug() << "Running in non-LIBDMR mode";
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        int restore_pos = Settings::get().internalOption("playlist_pos").toInt();
        _last = restore_pos;
        qInfo() << "Restoring last position:" << restore_pos;
    }
#endif
    qDebug() << "PlaylistModel initialization completed";
}


void PlaylistModel::initThumb()
{
    qDebug() << "Entering PlaylistModel::initThumb()";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");

    if (m_mvideo_thumbnailer == nullptr
            || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr
            || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr) {
        qWarning() << "Failed to resolve thumbnailer functions, thumbnail generation will be disabled";
        return;
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();

    m_image_data = m_mvideo_thumbnailer_create_image_data();
    m_video_thumbnailer->thumbnail_size = 400 * qApp->devicePixelRatio();
    m_bInitThumb = true;
    qDebug() << "Exiting PlaylistModel::initThumb(), thumbnail initialized successfully";
}

void PlaylistModel::initFFmpeg()
{
    qDebug() << "Entering PlaylistModel::initFFmpeg()";
    QLibrary avcodecLibrary(SysUtils::libPath("libavcodec.so"));
    QLibrary avformatLibrary(SysUtils::libPath("libavformat.so"));
    QLibrary avutilLibrary(SysUtils::libPath("libavutil.so"));

    g_mvideo_avformat_open_input = (mvideo_avformat_open_input) avformatLibrary.resolve("avformat_open_input");
    g_mvideo_avformat_find_stream_info = (mvideo_avformat_find_stream_info) avformatLibrary.resolve("avformat_find_stream_info");
    g_mvideo_av_find_best_stream = (mvideo_av_find_best_stream) avformatLibrary.resolve("av_find_best_stream");
    g_mvideo_avformat_close_input = (mvideo_avformat_close_input) avformatLibrary.resolve("avformat_close_input");
    g_mvideo_av_dict_get = (mvideo_av_dict_get) avutilLibrary.resolve("av_dict_get");
    g_mvideo_av_dump_format = (mvideo_av_dump_format) avformatLibrary.resolve("av_dump_format");
    g_mvideo_avcodec_find_decoder = (mvideo_avcodec_find_decoder) avcodecLibrary.resolve("avcodec_find_decoder");
    g_mvideo_av_get_media_type_string = (mvideo_av_get_media_type_string) avutilLibrary.resolve("av_get_media_type_string");
    g_mvideo_avcodec_alloc_context3 = (mvideo_avcodec_alloc_context3) avcodecLibrary.resolve("avcodec_alloc_context3");
    g_mvideo_avcodec_parameters_to_context = (mvideo_avcodec_parameters_to_context) avcodecLibrary.resolve("avcodec_parameters_to_context");
    g_mvideo_avcodec_open2 = (mvideo_avcodec_open2)(avcodecLibrary.resolve("avcodec_open2"));
    g_mvideo_avcodec_free_context = (mvideo_avcodec_free_context)(avcodecLibrary.resolve("avcodec_free_context"));

    m_initFFmpeg = true;
    qDebug() << "Exiting PlaylistModel::initFFmpeg()";
}

qint64 PlaylistModel::getUrlFileTotalSize(QUrl url, int tryTimes) const
{
    qDebug() << "Entering PlaylistModel::getUrlFileTotalSize()";
    qint64 size = -1;

    if (tryTimes <= 0) {
        qDebug() << "tryTimes <= 0, set to 1";
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
            qInfo() << reply->errorString();
            continue;
        }
        QVariant var = reply->header(QNetworkRequest::ContentLengthHeader);
        size = var.toLongLong();
        reply->deleteLater();
        break;
    } while (tryTimes--);

    qDebug() << "Exiting PlaylistModel::getUrlFileTotalSize(), size:" << size;
    return size;
}

void PlaylistModel::clearPlaylist()
{
    qDebug() << "Entering PlaylistModel::clearPlaylist()";

    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");
    cfg.endGroup();
}

void PlaylistModel::savePlaylist()
{
    qDebug() << "Entering PlaylistModel::savePlaylist()";

    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    cfg.remove("");
    qDebug() << "Cleared existing playlist entries";

    for (int i = 0; i < count(); ++i) {
        const auto &pif = _infos[i];
        cfg.setValue(QString::number(i), pif.url);
        qInfo() << "save " << pif.url;
    }
    cfg.endGroup();
    cfg.sync();
    qDebug() << "Exiting PlaylistModel::savePlaylist()";
}

void PlaylistModel::loadPlaylist()
{
    qDebug() << "Entering PlaylistModel::loadPlaylist()";

    if (!m_initFFmpeg) {
        qDebug() << "FFmpeg not initialized, initializing now";
        initFFmpeg();
    }
    if (!m_bInitThumb) {
        qDebug() << "Thumbnail generator not initialized, initializing now";
        initThumb();
    }
    QList<QUrl> urls;

    QSettings cfg(_playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    auto keys = cfg.childKeys();
    qDebug() << "Found" << keys.size() << "items in saved playlist";

    for (int i = 0; i < keys.size(); ++i) {
        auto url = cfg.value(QString::number(i)).toUrl();
        if (indexOf(url) >= 0) continue;
        urls.append(url);
        qDebug() << "Added URL from saved playlist:" << url.toString();
    }
    cfg.endGroup();
    delayedAppendAsync(urls);
    qDebug() << "Exiting PlaylistModel::loadPlaylist()";
}

bool PlaylistModel::getThumbnailRunning()
{
    qDebug() << "Entering PlaylistModel::getThumbnailRunning()";
    if (m_getThumbnail) {
        if (m_getThumbnail->isRunning()) {
            qDebug() << "m_getThumbnail is running";
            return true;
        } else {
            qDebug() << "m_getThumbnail is not running";
            return false;
        }
    } else {
        qDebug() << "m_getThumbnail is null";
        return false;
    }
    qDebug() << "Exiting PlaylistModel::getThumbnailRunning()";
}

MovieInfo PlaylistModel::getMovieInfo(const QUrl &url, bool *is)
{
    qDebug() << "Entering PlaylistModel::getMovieInfo()";
    if (url.isLocalFile()) {
        qDebug() << "url is local file";
        QFileInfo fi(url.toLocalFile());
        if (fi.exists()) {
            qDebug() << "fi exists";
            return parseFromFile(fi, is);
        } else {
            qDebug() << "fi not exists";
            *is = false;
            return MovieInfo();
        }
    } else {
        qDebug() << "url is not local file";
        *is = false;
        return MovieInfo();
    }
    qDebug() << "Exiting PlaylistModel::getMovieInfo()";
}

QImage PlaylistModel::getMovieCover(const QUrl &url)
{
    qDebug() << "Entering PlaylistModel::getMovieCover()";
    if (!m_bInitThumb) {
        qDebug() << "m_bInitThumb is false, initializing now";
        initThumb();
        m_image_data = nullptr;
    }

    if (m_mvideo_thumbnailer == nullptr
            || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr
            || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr
            || m_video_thumbnailer == nullptr) {
        return QImage();
    }

    m_video_thumbnailer->thumbnail_size = static_cast<int>(THUMBNAIL_SIZE);
    m_video_thumbnailer->seek_time = const_cast<char*>(SEEK_TIME);
    m_image_data = m_mvideo_thumbnailer_create_image_data();
    QString file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(), m_image_data);
    QImage img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;
    qDebug() << "Exiting PlaylistModel::getMovieCover()";
    return img;
}


PlaylistModel::PlayMode PlaylistModel::playMode() const
{
    qDebug() << "Entering PlaylistModel::playMode()";
    return _playMode;
}

void PlaylistModel::setPlayMode(PlaylistModel::PlayMode pm)
{
    qDebug() << "Entering PlaylistModel::setPlayMode()";
    if (_playMode != pm) {
        _playMode = pm;
        reshuffle();
        emit playModeChanged(pm);
    }
    qDebug() << "Exiting PlaylistModel::setPlayMode()";
}

void PlaylistModel::reshuffle()
{
    qDebug() << "Entering PlaylistModel::reshuffle()";
    if (_playMode != PlayMode::ShufflePlay || _infos.size() == 0) {
        qDebug() << "playMode is not ShufflePlay or infos.size() is 0, return";
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
    qDebug() << "Exiting PlaylistModel::reshuffle()";
}

void PlaylistModel::clear()
{
    qDebug() << "Entering PlaylistModel::clear()";

    _infos.clear();
    qDebug() << "Cleared playlist items";

    _engine->stop();
    qDebug() << "Stopped playback engine";

    _engine->waitLastEnd();
    qDebug() << "Waited for engine to finish";

    _current = -1;
    _last = -1;
    qDebug() << "Reset current and last positions";

    savePlaylist();
    qDebug() << "Saved empty playlist to disk";

    emit emptied();
    emit currentChanged();
    emit countChanged();

    qDebug() << "Exiting PlaylistModel::clear()";
}

void PlaylistModel::remove(int pos)
{
    qDebug() << "Entering PlaylistModel::remove() - position:" << pos;
    
    if (pos < 0 || pos >= count()) {
        qWarning() << "Invalid position to remove:" << pos << "playlist size:" << count();
        return;
    }

    _userRequestingItem = true;
    qDebug() << "Set userRequestingItem flag to prevent auto-advance";

    m_loadFile.removeOne(_infos[pos].url);
    _infos.removeAt(pos);
    reshuffle();
    qDebug() << "Reshuffled playlist after removal";

    _last = _current;
    if (_engine->state() != PlayerEngine::Idle) {
        qDebug() << "Engine is active, adjusting current position";
        if (_current == pos) {
            qDebug() << "Removed currently playing item, resetting position";
            _current = -1;
            _last = _current;
            _engine->waitLastEnd();

        } else if (pos < _current) {
            qDebug() << "Removed item before current, adjusting current from" << _current << "to" << (_current-1);
            _current--;
            _last = _current;
        }
    } else {
        qDebug() << "Engine is idle";
        if (_current == pos) {
            qDebug() << "Removed current item, resetting position";
            _current = -1;
            _last = _current;
            _engine->waitLastEnd();
        }
    }

    if (_last >= count()) {
        qDebug() << "Last position" << _last << "is now out of bounds, resetting";
        _last = -1;
    }

    emit itemRemoved(pos);
    if (_last != _current) {
        qDebug() << "Current position changed, emitting signal";
        emit currentChanged();
    }
    emit countChanged();


    qInfo() << _last << _current;
    _userRequestingItem = false;
    savePlaylist();

    qDebug() << "Exiting PlaylistModel::remove()";
}

void PlaylistModel::stop()
{
    qDebug() << "Entering PlaylistModel::stop()";
    _current = -1;
    emit currentChanged();
    qDebug() << "Exiting PlaylistModel::stop()";
}

void PlaylistModel::tryPlayCurrent(bool next)
{
    qInfo() << "Attempting to play current item";
    auto &pif = _infos[_current];
    if (pif.refresh()) {
        qInfo() << "File changed:" << pif.url.fileName();
    }
    emit itemInfoUpdated(_current);
    if (pif.valid) {
        qDebug() << "Playing valid item:" << pif.url.fileName();
        _engine->requestPlay(_current);
        emit currentChanged();
    } else {
        qWarning() << "Current item is invalid:" << pif.url.fileName();
        _current = -1;
        bool canPlay = false;
        //循环播放时，无效文件播放闪退
        if (_playMode == PlayMode::SingleLoop) {
            qDebug() << "SingleLoop mode - adjusting last position for invalid file";
            if ((_last < count() - 1) && next) {
                _last++;
                qDebug() << "Moving to next item, new last:" << _last;
            } else if ((_last > 0) && !next) {
                _last--;
                qDebug() << "Moving to previous item, new last:" << _last;
            } else if (next) {
                _last = 0;
                qDebug() << "Wrapping to first item";
            } else if (!next) {
                _last = count() - 1;
                qDebug() << "Wrapping to last item";
            }
        }

        qDebug() << "Checking if any valid items exist in playlist";
        bool result = std::any_of(_infos.begin(), _infos.end(), [](const PlayItemInfo & info) {
            return info.valid;
        });
        if (result) {
            canPlay = true;
            qDebug() << "Found at least one valid item in playlist";
        } else {
            qDebug() << "No valid items found in playlist";
        }

        if (canPlay) {
            qDebug() << "Found valid item to play";
            emit currentChanged();
            if (next) playNext(false);
            else playPrev(false);
        } else {
            qWarning() << "No valid items found to play";
        }
    }

    qDebug() << "Exiting PlaylistModel::tryPlayCurrent()";
}

void PlaylistModel::clearLoad()
{
    m_loadFile.clear();
}

void PlaylistModel::playNext(bool fromUser)
{
    qDebug() << "Entering PlaylistModel::playNext() - fromUser:" << fromUser;

    if (count() == 0) {
        qDebug() << "Playlist is empty, cannot play next";
        return;
    }
    qInfo() << "Playing next - mode:" << _playMode << "fromUser:" << fromUser
            << "last:" << _last << "current:" << _current;

    _userRequestingItem = true;

    switch (_playMode) {
    case SinglePlay:
        qDebug() << "Processing SinglePlay mode";
        if (fromUser) {
            if (_last + 1 >= count()) {
                qDebug() << "Reached end of playlist, resetting to beginning";
                _last = -1;
            }
            _engine->waitLastEnd();
            _current = _last + 1;
            _last = _current;
            qDebug() << "Playing next item at position:" << _current;
            tryPlayCurrent(true);
        } else {
            qDebug() << "Auto-advance in SinglePlay mode ignored";
        }
        break;

    case SingleLoop:
        qDebug() << "Processing SingleLoop mode";
        if (fromUser) {
            if (_engine->state() == PlayerEngine::Idle) {
                _last = _last == -1 ? 0 : _last;
                _current = _last;
                qDebug() << "Engine idle, playing current item at position:" << _current;
                tryPlayCurrent(true);
            } else {
                if (_last + 1 >= count()) {
                    qDebug() << "Reached end of playlist in single loop mode";
                    _last = -1;
                }
                _engine->waitLastEnd();
                _current = _last + 1;
                _last = _current;
                qDebug() << "Playing next item at position:" << _current;
                tryPlayCurrent(true);
            }
        } else {
            if (_engine->state() == PlayerEngine::Idle) {
                _last = _last < 0 ? 0 : _last;
                _current = _last;
                qDebug() << "Engine idle, playing current item at position:" << _current;
                tryPlayCurrent(true);
            } else {
                // 等待播放状态的真正结束，避免时序问题
                _engine->waitLastEnd();
                qDebug() << "Replaying current item in single loop mode";
                tryPlayCurrent(true);
            }
        }
        break;

    case ShufflePlay: {
        qDebug() << "Processing ShufflePlay mode";
        if (_shufflePlayed >= _playOrder.size()) {
            qDebug() << "Reshuffling playlist";
            _shufflePlayed = 0;
            reshuffle();
        }
        _shufflePlayed++;
        qInfo() << "Playing shuffled item" << _shufflePlayed - 1;
        _engine->waitLastEnd();
        _last = _current = _playOrder[_shufflePlayed - 1];
        tryPlayCurrent(true);
        break;
    }

    case OrderPlay:
        qDebug() << "Processing OrderPlay mode";
        _last++;
        if (_last == count()) {
            if (fromUser) {
                qDebug() << "Reached end of playlist in order play mode, restarting from beginning";
                _last = 0;
            } else {
                qDebug() << "Reached end of playlist in order play mode";
                _last--;
                break;
            }
        }

        _engine->waitLastEnd();
        _current = _last;
        qDebug() << "Playing next item at position:" << _current;
        tryPlayCurrent(true);
        break;

    case ListLoop:
        qDebug() << "Processing ListLoop mode";
        _last++;
        if (_last == count()) {
            _loopCount++;
            qDebug() << "Completed playlist loop" << _loopCount;
            _last = 0;
        }

        _engine->waitLastEnd();
        _current = _last;
        qDebug() << "Playing next item at position:" << _current;
        tryPlayCurrent(true);
        break;
    }

    qDebug() << "Exiting PlaylistModel::playNext()";
}

void PlaylistModel::playPrev(bool fromUser)
{
    qDebug() << "Entering PlaylistModel::playPrev()";
    if (count() == 0) {
        qDebug() << "Playlist is empty, cannot play previous";
        return;
    }
    qInfo() << "playmode" << _playMode << "fromUser" << fromUser
            << "last" << _last << "current" << _current;

    _userRequestingItem = true;

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
        qInfo() << "shuffle prev " << _shufflePlayed - 1;
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
    qDebug() << "Exiting PlaylistModel::playPrev()";
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
    qInfo() << "Appending single file:" << url.toString();
    if (indexOf(url) >= 0) {
        qDebug() << "File already exists in playlist, skipping";
        return;
    }

    if (url.isLocalFile()) {
        QFileInfo fi(url.toLocalFile());
        if (!fi.exists()) {
            qWarning() << "File does not exist:" << url.toString();
            return;
        }
        auto pif = calculatePlayInfo(url, fi);
        if (!pif.valid) {
            qWarning() << "Invalid play info for file:" << url.toString();
            return;
        }
        _infos.append(pif);
        qDebug() << "Added local file to playlist:" << fi.fileName();

#ifndef _LIBDMR_
        if (Settings::get().isSet(Settings::AutoSearchSimilar)) {
            QFileInfoList fil = utils::FindSimilarFiles(fi);
            qInfo() << "Found" << fil.size() << "similar files";
            std::for_each(fil.begin(), fil.end(), [ = ](const QFileInfo & fi) {
                auto url = QUrl::fromLocalFile(fi.absoluteFilePath());
                if (indexOf(url) < 0 && _engine->isPlayableFile(fi.absoluteFilePath())) {
                    auto playitem_info = calculatePlayInfo(url, fi);
                    if (playitem_info.valid) {
                        _infos.append(playitem_info);
                        qDebug() << "Added similar file to playlist:" << fi.fileName();
                    }
                }
            });
        }
#endif
    } else {
        qDebug() << "Adding non-local file to playlist";
        auto pif = calculatePlayInfo(url, QFileInfo(), true);
        _infos.append(pif);
    }
}

void PlaylistModel::collectionJob(const QList<QUrl> &urls, QList<QUrl> &inputUrls)
{
    qDebug() << "Entering PlaylistModel::collectionJob()";
    for (const auto &url : urls) {
        int aa = indexOf(url);
        if (m_loadFile.contains(url))
            continue;
        if (!url.isValid() || indexOf(url) >= 0 || _urlsInJob.contains(url.toLocalFile()))
            continue;

        m_loadFile.append(url);
        qInfo() << __func__ << _infos.size() << "index is" << aa << url;

        if(url.isLocalFile()) {
            QFileInfo fi(url.toLocalFile());
            if (!_firstLoad && (!fi.exists() || !fi.isFile())) continue;
            _pendingJob.append(qMakePair(url, fi));
            _urlsInJob.insert(url.toLocalFile());
            inputUrls.append(url);
            qInfo() << "append " << url.fileName();

    #ifndef _LIBDMR_
            //去除加载多个文件是自动加载相似文件功能
            //fix: 101698
            //powered by xxxxp
            if (!_firstLoad && Settings::get().isSet(Settings::AutoSearchSimilar) && (urls.size() == 1)) {
                QFileInfoList fil = utils::FindSimilarFiles(fi);
                //NOTE: The searched files are out of order, so they are sorted here
                struct {
                    bool operator()(const QFileInfo& fi1, const QFileInfo& fi2) const {
                        return utils::CompareNames(fi1.fileName(), fi2.fileName());
                    }
                } SortByDigits;
                std::sort(fil.begin(), fil.end(), SortByDigits);
                qInfo() << "auto search similar files" << fil;

                for (const QFileInfo &fileinfo : fil) {
                    if (fileinfo.isFile()) {
                        auto file_url = QUrl::fromLocalFile(fileinfo.absoluteFilePath());

                        if (!_urlsInJob.contains(file_url.toLocalFile()) && indexOf(file_url) < 0 &&
                                _engine->isPlayableFile(fileinfo.absoluteFilePath())) {
                            _pendingJob.append(qMakePair(file_url, fileinfo));
                            _urlsInJob.insert(file_url.toLocalFile());
                            inputUrls.append(file_url);
                            //handleAsyncAppendResults(QList<PlayItemInfo>()<<calculatePlayInfo(url,fi));
                        }
                    }
                }
            }
    #endif
        } else {
            _pendingJob.append(qMakePair(url, QFileInfo()));
            _urlsInJob.insert(url.toString());
            inputUrls.append(url);
        }
    }

    qInfo() << "input size" << urls.size() << "output size" << _urlsInJob.size()
            << "_pendingJob: " << _pendingJob.size();
}

void PlaylistModel::appendAsync(const QList<QUrl> &urls)
{
    qDebug() << "Entering PlaylistModel::appendAsync()";
    if (!m_initFFmpeg) {
        qDebug() << "Initializing FFmpeg";
        initFFmpeg();
    }
    if (!m_bInitThumb) {
        qDebug() << "Initializing thumbnail";
        initThumb();
    }
    qDebug() << "Exiting PlaylistModel::appendAsync()";
    delayedAppendAsync(urls);
    qDebug() << "Exiting PlaylistModel::appendAsync()";
}

void PlaylistModel::delayedAppendAsync(const QList<QUrl> &urls)
{
    qDebug() << "Entering PlaylistModel::delayedAppendAsync()";
    if (_pendingJob.size() > 0) {
        //TODO: may be automatically schedule later
        qWarning() << "there is a pending append going on, enqueue";
        m_pdataMutex->lock();
        _pendingAppendReq.enqueue(urls);
        m_pdataMutex->unlock();
        qDebug() << "_pendingJob > 0, return";
        return;
    }

    QList<QUrl> t_urls;
    m_pdataMutex->lock();
    collectionJob(urls, t_urls);
    m_pdataMutex->unlock();

    if (!_pendingJob.size()) {
        qDebug() << "_pendingJob.size() is 0, return";
        return;
    }

    struct MapFunctor {
        PlaylistModel *_model = nullptr;
        using result_type = PlayItemInfo;
        explicit MapFunctor(PlaylistModel *model): _model(model) {}

        struct PlayItemInfo operator()(const AppendJob &a)
        {
            qDebug() << "mapping " << a.first.fileName();
            return _model->calculatePlayInfo(a.first, a.second);
        }
    };

    qInfo() << "not wayland";
    if (QThread::idealThreadCount() > 1) {
        if (!m_getThumbnail) {
            m_getThumbnail = new GetThumbnail(this, t_urls);
            connect(m_getThumbnail, &GetThumbnail::finished, this, &PlaylistModel::onAsyncFinished);
            connect(m_getThumbnail, &GetThumbnail::updateItem, this, &PlaylistModel::onAsyncUpdate, Qt::BlockingQueuedConnection);
            m_getThumbnail->start();
        } else {
            if (m_getThumbnail->isRunning()) {
                m_tempList.append(t_urls);
            } else {
                m_getThumbnail->setUrls(t_urls);
                m_getThumbnail->start();
            }
        }
        _pendingJob.clear();
        _urlsInJob.clear();
    } else {
        PlayItemInfoList pil;
        for (const auto &a : _pendingJob) {
            qInfo() << "sync mapping " << a.first.fileName();
            pil.append(calculatePlayInfo(a.first, a.second));
            if (m_ploadThread && m_ploadThread->isRunning()) {
                m_ploadThread->msleep(10);
            }
        }
        _pendingJob.clear();
        _urlsInJob.clear();
        handleAsyncAppendResults(pil);
    }
    qDebug() << "Exiting PlaylistModel::delayedAppendAsync()";
}

static QList<PlayItemInfo> &SortSimilarFiles(QList<PlayItemInfo> &fil)
{
    qDebug() << "Entering PlaylistModel::SortSimilarFiles()";
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
    qDebug() << "Exiting PlaylistModel::SortSimilarFiles()";
    return fil;
}

/*not used yet*/
/*void PlaylistModel::onAsyncAppendFinished()
{
    qInfo() << __func__;
//    auto f = _jobWatcher->future();
    _pendingJob.clear();
    _urlsInJob.clear();

    //auto fil = f.results();
    //handleAsyncAppendResults(fil);
}*/

void PlaylistModel::onAsyncFinished()
{
    qDebug() << "Entering PlaylistModel::onAsyncFinished()";
    //qInfo() << fil.size();
    m_getThumbnail->clearItem();
    //handleAsyncAppendResults(fil);
    if (!m_tempList.isEmpty()) {
        qDebug() << "m_tempList is not empty, set urls and start";
        m_getThumbnail->setUrls(m_tempList);
        m_tempList.clear();
        m_getThumbnail->start();
    }
    qDebug() << "Exiting PlaylistModel::onAsyncFinished()";
}

void PlaylistModel::onAsyncUpdate(PlayItemInfo fil)
{
    qDebug() << "Entering PlaylistModel::onAsyncUpdate()";
    QList<PlayItemInfo> fils;
    fils.append(fil);
    //since _infos are modified only at the same thread, the lock is not necessary
    auto last = std::remove_if(fils.begin(), fils.end(), [](const PlayItemInfo & pif) {
        return !pif.mi.valid;
    });
    fils.erase(last, fils.end());

    if (!_firstLoad)
        _infos += SortSimilarFiles(fils);
    else
        _infos += fils;
    reshuffle();
    _firstLoad = false;
    emit itemsAppended();
    emit countChanged();
    _firstLoad = false;
    emit asyncAppendFinished(fils);

    if (_pendingAppendReq.size()) {
        qDebug() << "_pendingAppendReq.size() is not 0, dequeue";
        auto job = _pendingAppendReq.dequeue();
        delayedAppendAsync(job);
    }
    savePlaylist();
    qDebug() << "Exiting PlaylistModel::onAsyncUpdate()";
}

void PlaylistModel::handleAsyncAppendResults(QList<PlayItemInfo> &fil)
{
    qInfo() << __func__ << fil.size();
    if (!fil.size())
        return;
    //since _infos are modified only at the same thread, the lock is not necessary
    auto last = std::remove_if(fil.begin(), fil.end(), [](const PlayItemInfo & pif) {
        return !pif.mi.valid;
    });
    fil.erase(last, fil.end());

    qInfo() << "collected items" << fil.count();
    if (fil.size()) {
        if (!_firstLoad)
            _infos += SortSimilarFiles(fil);
        else
            _infos += fil;
        reshuffle();
        savePlaylist();
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
    qDebug() << "Exiting PlaylistModel::onAsyncUpdate()";
}

/*bool PlaylistModel::hasPendingAppends()
{
    return _pendingAppendReq.size() > 0 || _pendingJob.size() > 0;
}*/

//TODO: what if loadfile failed
void PlaylistModel::append(const QUrl &url)
{
    qDebug() << "Entering PlaylistModel::append()";
    if (!url.isValid()) return;

    appendSingle(url);
    reshuffle();
    savePlaylist();
    emit itemsAppended();
    emit countChanged();
    qDebug() << "Exiting PlaylistModel::append()";
}

void PlaylistModel::changeCurrent(int pos)
{
    qInfo() << __func__ << pos;
    if (pos < 0 || pos >= count()) return;
    auto mi = items().at(pos).mi;
    if (mi.fileType == "webm") {
        auto pif = calculatePlayInfo(items().at(pos).url, items().at(pos).info);
        items().removeAt(pos);
        items().insert(pos, pif);
        emit updateDuration();
    } else {
        if (_current == pos) {
            return;
        }
    }

    _userRequestingItem = true;

    _engine->waitLastEnd();
    _current = pos;
    _last = _current;
    tryPlayCurrent(true);
    emit currentChanged();
    qDebug() << "Exiting PlaylistModel::changeCurrent()";
}

void PlaylistModel::switchPosition(int src, int target)
{
    qDebug() << "Entering PlaylistModel::switchPosition()";
    //Q_ASSERT_X(0, "playlist", "not implemented");
    Q_ASSERT(src < _infos.size() && target < _infos.size());
    _infos.move(src, target);

    int min = qMin(src, target);
    int max = qMax(src, target);
    if (_current >= min && _current <= max) {
        qDebug() << "_current is between min and max";
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
    qDebug() << "Exiting PlaylistModel::switchPosition()";
}

PlayItemInfo &PlaylistModel::currentInfo()
{
    qDebug() << "Entering PlaylistModel::currentInfo()";
    //Q_ASSERT (_infos.size() > 0 && _current >= 0);
    Q_ASSERT(_infos.size() > 0);

    if (_current >= 0)
        return _infos[_current];
    if (_last >= 0 && _last < _infos.size())
        return _infos[_last];
    return _infos[0];
}

int PlaylistModel::size() const
{
    return _infos.size();
}

const PlayItemInfo &PlaylistModel::currentInfo() const
{
    Q_ASSERT(_infos.size() > 0 && _current >= 0);
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

//获取音乐缩略图
bool PlaylistModel::getMusicPix(const QFileInfo &fi, QPixmap &rImg)
{
    qDebug() << "Entering PlaylistModel::getMusicPix()";
    AVFormatContext *av_ctx = nullptr;
    //AVCodecContext *dec_ctx = nullptr;

    if (!fi.exists()) {
        qWarning() << "File does not exist";
        return false;
    }

    QLibrary library(SysUtils::libPath("libavformat.so"));
    mvideo_avformat_open_input g_mvideo_avformat_open_input_temp = (mvideo_avformat_open_input) library.resolve("avformat_open_input");
    mvideo_avformat_close_input g_mvideo_avformat_close_input = (mvideo_avformat_close_input) library.resolve("avformat_close_input");
    mvideo_avformat_find_stream_info g_mvideo_avformat_find_stream_info_temp = (mvideo_avformat_find_stream_info) library.resolve("avformat_find_stream_info");

    auto ret = g_mvideo_avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
        return false;
    }

    if (g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0) {
        qWarning() << "av_find_stream_info failed";
        return false;
    }

    for (unsigned int i = 0; i < av_ctx->nb_streams; i++) {
        if (av_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket pkt = av_ctx->streams[i]->attached_pic;
            //使用QImage读取完整图片数据（注意，图片数据是为解析的文件数据，需要用QImage::fromdata来解析读取）
            //rImg = QImage::fromData((uchar *)pkt.data, pkt.size);
            return rImg.loadFromData(static_cast<uchar *>(pkt.data), static_cast<uint>(pkt.size));
        }
    }
    g_mvideo_avformat_close_input(&av_ctx);
    qDebug() << "Exiting PlaylistModel::getMusicPix() return false";
    return false;
}

struct PlayItemInfo PlaylistModel::calculatePlayInfo(const QUrl &url, const QFileInfo &fi, bool isDvd)
{
    qDebug() << "Entering PlaylistModel::calculatePlayInfo()";
    bool ok = false;
    struct MovieInfo mi;
    auto ci = PersistentManager::get().loadFromCache(url);

    mi = parseFromFile(fi, &ok);
    if (isDvd && url.scheme().startsWith("dvd")) {
        qDebug() << "isDvd and url.scheme().startsWith(\"dvd\")";
        QString dev = url.path();
        if (dev.isEmpty()) dev = "/dev/sr0";
#ifdef heyi
        dmr::dvd::RetrieveDvdThread::get()->startDvd(dev);
#endif
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

    QPixmap pm = QPixmap();
    QPixmap dark_pm = QPixmap();
    if (ci.thumb_valid) {
        pm = ci.thumb;
        dark_pm = ci.thumb_dark;

        qInfo() << "load cached thumb" << url;
    } else if (ok) {
        try {
            //如果打开的是音乐就读取音乐缩略图
            bool isMusic = false;
            foreach (QString sf, _engine->audio_filetypes) {
                if (sf.right(sf.size() - 2) == mi.fileType) {
                    isMusic = true;
                }
            }

            //此处判断导致非播放状态下导入无视频流视频加载缩略图错误
            //暂时去掉，后期如有异常请排查此处逻辑
            //by xxxx
            if (/*_engine->state() != dmr::PlayerEngine::Idle && */mi.width < 0 || mi.height < 0) { //如果没有视频流，就当做音乐播放
                isMusic = true;
            }

            if (isMusic == false && m_mvideo_thumbnailer_generate_thumbnail_to_buffer) {
                m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, fi.canonicalFilePath().toUtf8().data(),  m_image_data);
                auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
                pm = QPixmap::fromImage(img);
                dark_pm = pm;
            }
            pm.setDevicePixelRatio(qApp->devicePixelRatio());
            dark_pm.setDevicePixelRatio(qApp->devicePixelRatio());
        } catch (const std::logic_error &) {
        }
    }

    PlayItemInfo pif { fi.exists() || !url.isLocalFile(), ok, url, fi, pm, dark_pm, mi };

    if (ok && url.isLocalFile() && (!ci.mi_valid || !ci.thumb_valid)) {
        PersistentManager::get().save(pif);
    }
    if (!url.isLocalFile() && !url.scheme().startsWith("dvd") && CompositingManager::isMpvExists()) {
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
    qDebug() << "Exiting PlaylistModel::calculatePlayInfo()";
    return pif;
}

int PlaylistModel::indexOf(const QUrl &url)
{
    qDebug() << "Entering PlaylistModel::indexOf()";
    auto p = std::find_if(_infos.begin(), _infos.end(), [&](const PlayItemInfo & pif) {
        return pif.url == url;
    });

    if (p == _infos.end()) return -1;
    return static_cast<int>(std::distance(_infos.begin(), p));
}

PlaylistModel::~PlaylistModel()
{
    qDebug() << "Entering PlaylistModel destructor";

    delete m_pdataMutex;
    qDebug() << "Deleted data mutex";

#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ClearWhenQuit)) {
        qDebug() << "ClearWhenQuit is set, clearing playlist";
        clearPlaylist();
    } else {
        qDebug() << "Preserving playlist on exit";
    }
#endif
    if (m_getThumbnail) {
        if (m_getThumbnail->isRunning()) {
            qDebug() << "Stopping running thumbnail generation thread";
            m_getThumbnail->stop();
        }
        m_getThumbnail->deleteLater();
        m_getThumbnail = nullptr;
        qDebug() << "Thumbnail generator scheduled for deletion";
    }
    if (m_video_thumbnailer != nullptr) {
        qDebug() << "Destroying video thumbnailer";
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
        m_video_thumbnailer = nullptr;
    }
    if (m_image_data != nullptr) {
        qDebug() << "Destroying image data";
        m_mvideo_thumbnailer_destroy_image_data(m_image_data);
        m_image_data = nullptr;
    }

    qDebug() << "Exiting PlaylistModel destructor";
}

LoadThread::LoadThread(PlaylistModel *model, const QList<QUrl> &urls): _urls(urls)
{
    qDebug() << "Entering LoadThread constructor";
    _pModel = nullptr;
    _pModel = model;
//    _urls = urls;
}
LoadThread::~LoadThread()
{
    qDebug() << "Exiting LoadThread destructor";
    _pModel = nullptr;
}

void LoadThread::run()
{
    qDebug() << "Entering LoadThread run";
    if (_pModel) {
        _pModel->delayedAppendAsync(_urls);
    }
}
#ifdef _LIBDMR_
static int open_codec_context(int *stream_idx,
                              AVCodecParameters **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    qDebug() << "Entering open_codec_context";
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;
    ret = g_mvideo_av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        qWarning() << "Could not find " << g_mvideo_av_get_media_type_string(type)
                   << " stream in input file";
        return ret;
    }

    stream_index = ret;
    st = fmt_ctx->streams[stream_index];
#if LIBAVFORMAT_VERSION_MAJOR >= 57
    *dec_ctx = st->codecpar;
    dec = g_mvideo_avcodec_find_decoder((*dec_ctx)->codec_id);
#else
    /* find decoder for the stream */
    dec = g_mvideo_avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        qWarning() << "Failed to find" << g_mvideo_av_get_media_type_string(type) << "codec";
        fprintf(stderr, "Failed to find %s codec\n",
                g_mvideo_av_get_media_type_string(type));
        return AVERROR(EINVAL);
    }
    /* Allocate a codec context for the decoder */
    *dec_ctx = g_mvideo_avcodec_alloc_context3(dec);
    if (!*dec_ctx) {
        qWarning() << "Failed to allocate the" << g_mvideo_av_get_media_type_string(type) << "codec context";
        fprintf(stderr, "Failed to allocate the %s codec context\n",
                g_mvideo_av_get_media_type_string(type));
        return AVERROR(ENOMEM);
    }
    /* Copy codec parameters from input stream to output codec context */
    if ((ret = g_mvideo_avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
        qWarning() << "Failed to copy" << g_mvideo_av_get_media_type_string(type) << "codec parameters to decoder context";
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                g_mvideo_av_get_media_type_string(type));
        return ret;
    }
#endif

    *stream_idx = stream_index;
    qDebug() << "Exiting open_codec_context";
    return 0;
}
MovieInfo MovieInfo::parseFromFile(const QFileInfo &fi, bool *ok)
{
    qDebug() << "Entering MovieInfo::parseFromFile";
    struct MovieInfo mi;
    mi.valid = false;
    AVFormatContext *av_ctx = NULL;
    int stream_id = -1;
    AVCodecParameters *dec_ctx = NULL;
    AVStream *av_stream = nullptr;

    if (!fi.exists()) {
        qDebug() << "File does not exist";
        if (ok) *ok = false;
        return mi;
    }
    if (!CompositingManager::isMpvExists()) {
        qDebug() << "CompositingManager is not exists, parse file by Gst";
        mi = GstUtils::get()->parseFileByGst(fi);
        if(mi.valid){
            *ok = true;
        }
        return mi;
    }

    auto ret = g_mvideo_avformat_open_input(&av_ctx, fi.filePath().toUtf8().constData(), NULL, NULL);
    if (ret < 0) {
        qWarning() << "avformat: could not open input";
        if (ok) *ok = false;
        return mi;
    }

    if (g_mvideo_avformat_find_stream_info(av_ctx, NULL) < 0) {
        qWarning() << "av_find_stream_info failed";
        if (ok) *ok = false;
        return mi;
    }

    if (av_ctx->nb_streams == 0) {
        qDebug() << "No streams found in the file";
        if (ok) *ok = false;
        return mi;
    }
    if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
        if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_AUDIO) < 0) {
            qDebug() << "Failed to open audio codec context";
            if (ok) *ok = false;
            return mi;
        }
    }

    for (int i = 0; i < av_ctx->nb_streams; i++) {
        av_stream = av_ctx->streams[i];
        if (av_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            break;
        }
    }

    g_mvideo_av_dump_format(av_ctx, 0, fi.fileName().toUtf8().constData(), 0);

    mi.width = dec_ctx->width;
    mi.height = dec_ctx->height;
    auto duration = av_ctx->duration == AV_NOPTS_VALUE ? 0 : av_ctx->duration;
    duration = duration + (duration <= INT64_MAX - 5000 ? 5000 : 0);
    mi.duration = duration / AV_TIME_BASE;
    mi.resolution = QString("%1x%2").arg(mi.width).arg(mi.height);
    mi.title = fi.fileName(); //FIXME this
    mi.filePath = fi.canonicalFilePath();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qInfo() << __func__ << "created:" << fi.created() << "       toString:" << fi.created().toString();
    mi.creation = fi.created().toString();
#else
    qInfo() << __func__ << "created:" << fi.birthTime() << "       toString:" << fi.birthTime().toString();
    mi.creation = fi.birthTime().toString();
#endif
    mi.fileSize = fi.size();
    mi.fileType = fi.suffix();

    mi.vCodecID = dec_ctx->codec_id;
    mi.vCodeRate = dec_ctx->bit_rate;
#ifdef _MOVIE_USE_
    mi.strFmtName = av_ctx->iformat->long_name;
#endif
    if (av_stream->avg_frame_rate.den != 0) {
        mi.fps = static_cast<int>(round(static_cast<double>(av_stream->avg_frame_rate.num) / av_stream->avg_frame_rate.den));
    } else {
        mi.fps = 0;
    }
    if (mi.height != 0) {
        qDebug() << "mi.height is not 0:" << mi.height;
        mi.proportion = mi.width / mi.height;
    } else {
        qDebug() << "mi.height is 0, set mi.proportion to 0";
        mi.proportion = 0;
    }

    if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_AUDIO) < 0) {
        qDebug() << "Failed to open audio codec context";
        if (open_codec_context(&stream_id, &dec_ctx, av_ctx, AVMEDIA_TYPE_VIDEO) < 0) {
            if (ok) *ok = false;
            return mi;
        }
    }


    mi.aCodeID = dec_ctx->codec_id;
    mi.aCodeRate = dec_ctx->bit_rate;
    mi.aDigit = dec_ctx->format;
    mi.channels = dec_ctx->channels;
    mi.sampling = dec_ctx->sample_rate;

    AVDictionaryEntry *tag = NULL;
    while ((tag = g_mvideo_av_dict_get(av_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
        if (tag->key && strcmp(tag->key, "creation_time") == 0) {
            auto dt = QDateTime::fromString(tag->value, Qt::ISODate);
            mi.creation = dt.toString();
            qInfo() << __func__ << dt.toString();
            break;
        }
        qInfo() << "tag:" << tag->key << tag->value;
    }

    tag = NULL;
    AVStream *st = av_ctx->streams[stream_id];
    while ((tag = g_mvideo_av_dict_get(st->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) != NULL) {
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
        qInfo() << "tag:" << tag->key << tag->value;
    }


    g_mvideo_avformat_close_input(&av_ctx);
    mi.valid = true;

    if (ok) *ok = true;
    qDebug() << "Exiting MovieInfo::parseFromFile";
    return mi;
}
//#else
//MovieInfo MovieInfo::parseFromFile(const QFileInfo &fi, bool *ok)
//{
//    MovieInfo info;
//    return info;
//}
#endif
}

#include "playlist_model.moc"

