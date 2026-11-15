// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_thumbnail_worker.h"
#include "ddlog.h"
#include "player_engine.h"
#include <QLibrary>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "compositing_manager.h"
#include "sysutils.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>

#define SIZE_THRESHOLD (10 * 1<<20)

namespace dmr {
std::atomic<Platform_ThumbnailWorker *> Platform_ThumbnailWorker::m_instance(nullptr);
QMutex Platform_ThumbnailWorker::m_instLock;
QMutex Platform_ThumbnailWorker::m_thumbLock;
QWaitCondition Platform_ThumbnailWorker::m_cond;

Platform_ThumbnailWorker::~Platform_ThumbnailWorker()
{
    qCDebug(appMovie) << "Destroying thumbnail worker";
    free(m_pCharTime);
    if (m_video_thumbnailer) {
        qCDebug(appMovie) << "Destroying video thumbnailer";
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
    } else {
        qCDebug(appMovie) << "Video thumbnailer is null, no need to destroy.";
    }
    qCDebug(appMovie) << "Exiting Platform_ThumbnailWorker destructor.";
}

Platform_ThumbnailWorker &Platform_ThumbnailWorker::get()
{
    if (m_instance == nullptr) {
        QMutexLocker lock(&m_instLock);
        qCDebug(appMovie) << "Creating new thumbnail worker instance";
        m_instance = new Platform_ThumbnailWorker;
        if(CompositingManager::get().platform() != Platform::Mips) {
            qCDebug(appMovie) << "Starting thumbnail worker thread";
            (*m_instance).start();
        } else {
            qCDebug(appMovie) << "Skipping thumbnail worker thread start on MIPS platform";
        }
    }
    return *m_instance;
}

bool Platform_ThumbnailWorker::isThumbGenerated(const QUrl &url, int secs)
{
    qCDebug(appMovie) << "Entering Platform_ThumbnailWorker::isThumbGenerated. URL:" << url << ", Seconds:" << secs;
    QMutexLocker lock(&m_thumbLock);
    if (!_cache.contains(url)) {
        qCDebug(appMovie) << "Cache does not contain URL. Returning false.";
        return false;
    }

    const auto &l = _cache[url];
    bool result = l.contains(secs);
    qCDebug(appMovie) << "Cache contains URL. Checking if seconds is present. Result:" << result;
    qCDebug(appMovie) << "Exiting Platform_ThumbnailWorker::isThumbGenerated.";
    return result;
}

QPixmap Platform_ThumbnailWorker::getThumb(const QUrl &url, int secs)
{
    qCDebug(appMovie) << "Entering Platform_ThumbnailWorker::getThumb. URL:" << url << ", Seconds:" << secs;
    QMutexLocker lock(&m_thumbLock);
    QPixmap pm;

    if (_cache.contains(url)) {
        pm = _cache[url].value(secs);
        qCDebug(appMovie) << "Retrieved cached thumbnail for" << url << "at" << secs << "seconds";
    } else {
        qCDebug(appMovie) << "No cached thumbnail found for" << url << "at" << secs << "seconds";
    }
    qCDebug(appMovie) << "Exiting Platform_ThumbnailWorker::getThumb.";
    return pm;
}

void Platform_ThumbnailWorker::setPlayerEngine(PlayerEngine *pPlayerEngline)
{
    qCDebug(appMovie) << "Entering Platform_ThumbnailWorker::setPlayerEngine. PlayerEngine pointer:" << pPlayerEngline;
    _engine = pPlayerEngline;
    qCDebug(appMovie) << "Exiting Platform_ThumbnailWorker::setPlayerEngine. Engine set.";
}

void Platform_ThumbnailWorker::requestThumb(const QUrl &url, int secs)
{
    qCDebug(appMovie) << "Requesting thumbnail for" << url << "at" << secs << "seconds";
    if(CompositingManager::get().platform() != Platform::Mips) {
        qCDebug(appMovie) << "Platform is not MIPS. Attempting to add thumbnail request to queue.";
        if (m_thumbLock.tryLock()) {
            _wq.push_front(qMakePair(url, secs));
            m_cond.wakeOne();
            m_thumbLock.unlock();
            qCDebug(appMovie) << "Added thumbnail request to queue";
        } else {
            qCWarning(appMovie) << "Failed to acquire lock for thumbnail request";
        }
    } else {
        qCDebug(appMovie) << "Running thumbnail generation synchronously on MIPS platform";
        runSingle(qMakePair(url, secs));
    }
    qCDebug(appMovie) << "Exiting Platform_ThumbnailWorker::requestThumb.";
}

Platform_ThumbnailWorker::Platform_ThumbnailWorker()
{
    initThumb();
    m_video_thumbnailer->thumbnail_size = m_video_thumbnailer->thumbnail_size * qApp->devicePixelRatio();

    m_pCharTime = (char *)malloc(20);
}


void Platform_ThumbnailWorker::initThumb()
{
    qCDebug(appMovie) << "Initializing thumbnail generation library";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");
    
    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr) {
        qCCritical(appMovie) << "Failed to resolve required thumbnail generation functions";
        return;
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    if (m_video_thumbnailer == nullptr) {
        qCCritical(appMovie) << "Failed to create video thumbnailer instance";
        return;
    }
    qCInfo(appMovie) << "Thumbnail generation library initialized successfully";
}

QPixmap Platform_ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    qCInfo(appMovie) << "Generating thumbnail for" << url << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
    pm.setDevicePixelRatio(dpr);

    QPixmap precisePm = genThumbPrecise(url, secs);
    if (!precisePm.isNull()) {
        qCInfo(appMovie) << "Using precise seek thumbnail for" << url << "at" << secs << "seconds";
        return precisePm;
    }
    qCWarning(appMovie) << "Precise seek failed, falling back to libffmpegthumbnailer for" << url;

    if (m_image_data == nullptr) {
        qCDebug(appMovie) << "Creating new image data buffer";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    }

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    strncpy(m_pCharTime, d.toString("hh:mm:ss").toLatin1().data(), 19);
    m_pCharTime[19] = '\0';
    m_video_thumbnailer->seek_time = m_pCharTime;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    
    try {
        qCDebug(appMovie) << "Generating thumbnail for file:" << file;
        m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
        auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");

        pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        qCInfo(appMovie) << "Successfully generated thumbnail" << pm.size();
    } catch (const std::logic_error &e) {
        qCWarning(appMovie) << "Failed to generate thumbnail:" << e.what();
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

    return pm;
}

QPixmap Platform_ThumbnailWorker::genThumbPrecise(const QUrl &url, int secs)
{
    qCInfo(appMovie) << "Precise seek thumbnail for" << url << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext *codecCtx = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;
    struct SwsContext *swsCtx = nullptr;
    QPixmap result;

    if (avformat_open_input(&fmtCtx, file.toUtf8().constData(), nullptr, nullptr) != 0) {
        qCWarning(appMovie) << "avformat_open_input failed for" << file;
        return result;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        qCWarning(appMovie) << "avformat_find_stream_info failed for" << file;
        avformat_close_input(&fmtCtx);
        return result;
    }

    int videoStreamIdx = -1;
    const AVCodec *codec = nullptr;
    videoStreamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (videoStreamIdx < 0 || !codec) {
        qCWarning(appMovie) << "No video stream found for" << file;
        avformat_close_input(&fmtCtx);
        return result;
    }

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        qCWarning(appMovie) << "avcodec_alloc_context3 failed";
        avformat_close_input(&fmtCtx);
        return result;
    }

    if (avcodec_parameters_to_context(codecCtx, fmtCtx->streams[videoStreamIdx]->codecpar) < 0) {
        qCWarning(appMovie) << "avcodec_parameters_to_context failed";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        qCWarning(appMovie) << "avcodec_open2 failed";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    int64_t seekTarget = static_cast<int64_t>(secs) * AV_TIME_BASE;
    if (avformat_seek_file(fmtCtx, -1, INT64_MIN, seekTarget, INT64_MAX, AVSEEK_FLAG_ANY) < 0) {
        qCWarning(appMovie) << "avformat_seek_file failed at" << secs << "seconds";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }
    avcodec_flush_buffers(codecCtx);

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    bool gotFrame = false;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index != videoStreamIdx) {
            av_packet_unref(pkt);
            continue;
        }

        int ret = avcodec_send_packet(codecCtx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            continue;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }
            gotFrame = true;
            break;
        }
        if (gotFrame) {
            break;
        }
    }

    if (!gotFrame) {
        qCWarning(appMovie) << "Failed to decode target frame for" << file;
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    int srcW = frame->width;
    int srcH = frame->height;
    QSize target = thumbSize() * dpr;
    int dstW = target.width();
    int dstH = target.height();

    swsCtx = sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(frame->format),
                            dstW, dstH, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        qCWarning(appMovie) << "sws_getContext failed";
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    uint8_t *dstData[4] = {nullptr};
    int dstLinesize[4] = {0};
    if (av_image_alloc(dstData, dstLinesize, dstW, dstH, AV_PIX_FMT_RGB24, 1) < 0) {
        qCWarning(appMovie) << "av_image_alloc failed";
        sws_freeContext(swsCtx);
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return result;
    }

    sws_scale(swsCtx, frame->data, frame->linesize, 0, srcH, dstData, dstLinesize);

    QImage img(dstData[0], dstW, dstH, dstLinesize[0], QImage::Format_RGB888);
    result = QPixmap::fromImage(img.copy());
    result.setDevicePixelRatio(dpr);

    qCInfo(appMovie) << "Precise seek thumbnail generated" << result.size();

    av_freep(&dstData[0]);
    sws_freeContext(swsCtx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    return result;
}

void Platform_ThumbnailWorker::run()
{
    qCInfo(appMovie) << "Starting thumbnail worker thread";
    setPriority(QThread::IdlePriority);
    while (
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        !_quit.load()
#else
        !_quit
#endif
    ) {
        QPair<QUrl, int> w;
        {
            QMutexLocker lock(&m_thumbLock);
            while (_wq.isEmpty() && 
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                   !_quit.load()
#else
                   !_quit
#endif
            ) {
                m_cond.wait(lock.mutex(), 40);
            }

            if (!_wq.isEmpty()) {
                w = _wq.takeFirst();
                _wq.clear();
                qCDebug(appMovie) << "Processing thumbnail request from queue";
            }
        }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (_quit.load()) break;
#else
        if (_quit) break;
#endif

        {
            QMutexLocker lock(&m_thumbLock);
            //TODO: optimize: need a lru map
            if (_cacheSize > SIZE_THRESHOLD) {
                qCInfo(appMovie) << "Thumbnail cache size" << _cacheSize << "exceeds threshold" << SIZE_THRESHOLD << "- clearing cache";
                _cache.clear();
                _cacheSize = 0;
            }
        }

        if (!isThumbGenerated(w.first, w.second)) {
            qCInfo(appMovie) << "Generating new thumbnail for" << w.first << "at" << w.second << "seconds";
            auto pm = genThumb(w.first, w.second);

            QMutexLocker lock(&m_thumbLock);
            _cache[w.first].insert(w.second, pm);
            _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
            qCInfo(appMovie) << "Cache size after insertion:" << _cacheSize;

            QTime d(0, 0, 0);
            d = d.addSecs(w.second);
            qCInfo(appMovie) << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
        }

        emit thumbGenerated(w.first, w.second);
    }

    qCInfo(appMovie) << "Thumbnail worker thread stopping";
    _wq.clear();
}

void Platform_ThumbnailWorker::runSingle(QPair<QUrl, int> w)
{
    qCInfo(appMovie) << "Running single thumbnail generation for" << w.first << "at" << w.second << "seconds";
    
    if (_cacheSize > SIZE_THRESHOLD) {
        qCInfo(appMovie) << "Thumbnail cache size" << _cacheSize << "exceeds threshold" << SIZE_THRESHOLD << "- clearing cache";
        _cache.clear();
        _cacheSize = 0;
    }

    if (!isThumbGenerated(w.first, w.second)) {
        qCInfo(appMovie) << "Generating new thumbnail for" << w.first << "at" << w.second << "seconds";
        auto pm = genThumb(w.first, w.second);

        QMutexLocker lock(&m_thumbLock);
        _cache[w.first].insert(w.second, pm);
        _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
        qCInfo(appMovie) << "Cache size after insertion:" << _cacheSize;

        QTime d(0, 0, 0);
        d = d.addSecs(w.second);
        qCInfo(appMovie) << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
    }

    emit thumbGenerated(w.first, w.second);
}
}
