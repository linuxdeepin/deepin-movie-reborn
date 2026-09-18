// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_worker.h"
#include "player_engine.h"
#include <QLibrary>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "compositing_manager.h"
#include "sysutils.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#define SIZE_THRESHOLD (10 * 1<<20)

namespace dmr {
std::atomic<ThumbnailWorker *> ThumbnailWorker::m_instance(nullptr);
QMutex ThumbnailWorker::m_instLock;
QMutex ThumbnailWorker::m_thumbLock;
QWaitCondition ThumbnailWorker::m_cond;

ThumbnailWorker::~ThumbnailWorker()
{
    qDebug() << "Destroying ThumbnailWorker instance";
    free(m_pCharTime);
    if (m_video_thumbnailer) {
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
        qDebug() << "Video thumbnailer destroyed";
    }
}

ThumbnailWorker &ThumbnailWorker::get()
{
    if (m_instance == nullptr) {
        QMutexLocker lock(&m_instLock);
        m_instance = new ThumbnailWorker;
        if(CompositingManager::get().platform() != Platform::Mips) {
            (*m_instance).start();
        }
    }
    return *m_instance;
}

bool ThumbnailWorker::isThumbGenerated(const QUrl &url, int secs)
{
    QMutexLocker lock(&m_thumbLock);
    if (!_cache.contains(url)) {
        qDebug() << "No thumbnail cache found for:" << url.toString();
        return false;
    }

    const auto &l = _cache[url];
    bool exists = l.contains(secs);
    qDebug() << "Thumbnail exists for" << url.toString() << "at" << secs << "seconds:" << exists;
    return exists;
}

QPixmap ThumbnailWorker::getThumb(const QUrl &url, int secs)
{
    QMutexLocker lock(&m_thumbLock);
    QPixmap pm;

    if (_cache.contains(url)) {
        pm = _cache[url].value(secs);
    }

    return pm;
}

void ThumbnailWorker::setPlayerEngine(PlayerEngine *pPlayerEngline)
{
    _engine = pPlayerEngline;
}

void ThumbnailWorker::requestThumb(const QUrl &url, int secs)
{
    qDebug() << "Requesting thumbnail for:" << url.toString() << "at" << secs << "seconds";
    if(CompositingManager::get().platform() != Platform::Mips) {
        if (m_thumbLock.tryLock()) {
            _wq.push_front(qMakePair(url, secs));
            m_cond.wakeOne();
            m_thumbLock.unlock();
            qDebug() << "Added thumbnail request to queue";
        } else {
            qWarning() << "Failed to acquire lock for thumbnail request";
        }
    } else {
        qDebug() << "Running thumbnail generation in single thread mode";
        runSingle(qMakePair(url, secs));
    }
}

ThumbnailWorker::ThumbnailWorker()
{
    qDebug() << "Initializing ThumbnailWorker";
    initThumb();
    m_video_thumbnailer->thumbnail_size = m_video_thumbnailer->thumbnail_size * qApp->devicePixelRatio();
    qDebug() << "Thumbnail size set based on device pixel ratio:" << m_video_thumbnailer->thumbnail_size;

    m_pCharTime = (char *)malloc(20);

    qDebug() << "Exiting ThumbnailWorker constructor.";
}


void ThumbnailWorker::initThumb()
{
    qDebug() << "Initializing thumbnail library";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");
    qDebug() << "Attempted to resolve thumbnail functions.";

    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr) {
        qCritical() << "Failed to resolve required thumbnail functions from library";
        return;
    } else {
        qDebug() << "All required thumbnail functions resolved successfully.";
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    if (!m_video_thumbnailer) {
        qCritical() << "Failed to create video thumbnailer instance";
    } else {
        qInfo() << "Successfully initialized thumbnail library";
    }
    qDebug() << "Exiting ThumbnailWorker::initThumb().";
}

QPixmap ThumbnailWorker::genThumbPrecise(const QUrl &url, int secs)
{
    qDebug() << "Generating precise thumbnail for:" << url.toString() << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
    pm.setDevicePixelRatio(dpr);

    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, file.toUtf8().constData(), nullptr, nullptr) != 0) {
        qWarning() << "genThumbPrecise: failed to open input" << file;
        return pm;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        qWarning() << "genThumbPrecise: failed to find stream info";
        avformat_close_input(&fmtCtx);
        return pm;
    }

    int videoStream = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = static_cast<int>(i);
            break;
        }
    }
    if (videoStream < 0) {
        qWarning() << "genThumbPrecise: no video stream found";
        avformat_close_input(&fmtCtx);
        return pm;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(nullptr);
    if (!codecCtx) {
        avformat_close_input(&fmtCtx);
        return pm;
    }
    avcodec_parameters_to_context(codecCtx, fmtCtx->streams[videoStream]->codecpar);

    const AVCodec *codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!codec || avcodec_open2(codecCtx, codec, nullptr) < 0) {
        qWarning() << "genThumbPrecise: failed to open codec";
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return pm;
    }

    int64_t seekTarget = static_cast<int64_t>(secs) * AV_TIME_BASE;
    if (av_seek_frame(fmtCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
        qWarning() << "genThumbPrecise: seek failed for" << secs << "seconds";
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool found = false;
    int64_t targetUs = static_cast<int64_t>(secs) * AV_TIME_BASE;
    AVRational timeBase = fmtCtx->streams[videoStream]->time_base;

    while (!found && av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == videoStream) {
            int ret = avcodec_send_packet(codecCtx, pkt);
            while (ret >= 0) {
                ret = avcodec_receive_frame(codecCtx, frame);
                if (ret < 0) {
                    break;
                }
                int64_t frameUs = av_rescale_q(frame->pts, timeBase, (AVRational){1, AV_TIME_BASE});
                if (frameUs >= targetUs) {
                    found = true;
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (found) {
        int width = codecCtx->width;
        int height = codecCtx->height;
        struct SwsContext *swsCtx = sws_getContext(
            width, height, codecCtx->pix_fmt,
            width, height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (swsCtx) {
            uint8_t *rgbData[1] = {static_cast<uint8_t *>(av_malloc(static_cast<size_t>(width) * height * 3))};
            int rgbLinesize[1] = {width * 3};
            sws_scale(swsCtx, frame->data, frame->linesize, 0, height, rgbData, rgbLinesize);

            QImage img(rgbData[0], width, height, width * 3, QImage::Format_RGB888);
            pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            pm.setDevicePixelRatio(dpr);

            av_free(rgbData[0]);
            sws_freeContext(swsCtx);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    qDebug() << "genThumbPrecise: done, found =" << found << "pm isNull =" << pm.isNull();
    return pm;
}

QPixmap ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    qDebug() << "Generating thumbnail for:" << url.toString() << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();

    QPixmap pm = genThumbPrecise(url, secs);
    if (!pm.isNull()) {
        qDebug() << "Precise thumbnail generated successfully, size:" << pm.size();
        return pm;
    }

    qWarning() << "genThumbPrecise failed, falling back to ffmpegthumbnailer";
    pm.setDevicePixelRatio(dpr);
    qDebug() << "Device pixel ratio:" << dpr << ", Pixmap device pixel ratio set.";

    if (m_image_data == nullptr) {
        qDebug() << "m_image_data is nullptr, creating new image data buffer.";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    } else {
        qDebug() << "m_image_data already exists.";
    }

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    strcpy(m_pCharTime, d.toString("hh:mm:ss").toLatin1().data());
    m_video_thumbnailer->seek_time = m_pCharTime;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    qDebug() << "Seek time set. File path for thumbnail generation:" << file;
    
    try {
        qDebug() << "Generating thumbnail for file:" << file;
        m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
        auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
        qDebug() << "Image data converted to QImage. Image size:" << img.size();

        pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        qDebug() << "Successfully generated thumbnail, size:" << pm.size();
    } catch (const std::logic_error &e) {
        qWarning() << "Failed to generate thumbnail:" << e.what();
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

    qDebug() << "Exiting ThumbnailWorker::genThumb(). Returning pixmap.";
    return pm;
}

void ThumbnailWorker::run()
{
    qDebug() << "Starting thumbnail worker thread";
    setPriority(QThread::IdlePriority);
    qDebug() << "Thread priority set to IdlePriority.";
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
            qDebug() << "Acquired m_thumbLock for queue processing.";
            while (_wq.isEmpty() && 
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                   !_quit.load()
#else
                   !_quit
#endif
            ) {
                qDebug() << "Waiting for thumbnail requests or quit signal. Queue is empty.";
                m_cond.wait(lock.mutex(), 40);
            }

            if (!_wq.isEmpty()) {
                w = _wq.takeFirst();
                _wq.clear();
                qDebug() << "Processing thumbnail request from queue";
            }
        }

        if (
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            _quit.load()
#else
            _quit
#endif
        ) {
            qDebug() << "Quit signal detected in run loop. Breaking.";
            break;
        }

        {
            QMutexLocker lock(&m_thumbLock);
            qDebug() << "Acquired m_thumbLock for cache management.";
            //TODO: optimize: need a lru map
            if (_cacheSize > SIZE_THRESHOLD) {
                qInfo() << "Thumbnail cache size exceeds maximum threshold, cleaning up cache";
                _cache.clear();
                _cacheSize = 0;
                qDebug() << "Cache cleared. New cache size:" << _cacheSize;
            } else {
                qDebug() << "Cache size within threshold:" << _cacheSize;
            }
        }

        if (!isThumbGenerated(w.first, w.second)) {
            qDebug() << "Thumbnail not generated for current request. Generating now.";
            auto pm = genThumb(w.first, w.second);

            QMutexLocker lock(&m_thumbLock);
            qDebug() << "Acquired m_thumbLock for cache insertion.";
            _cache[w.first].insert(w.second, pm);
            _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
            qDebug() << "Thumbnail inserted into cache. New cache size:" << _cacheSize;

            QTime d(0, 0, 0);
            d = d.addSecs(w.second);
            qInfo() << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
        } else {
            qDebug() << "Thumbnail already generated for" << w.first.toString() << "at" << w.second << "seconds. Skipping generation.";
        }

        emit thumbGenerated(w.first, w.second);
        qDebug() << "Emitted thumbGenerated signal.";
    }

    qDebug() << "Clearing thumbnail request queue";
    _wq.clear();
    qDebug() << "Exiting ThumbnailWorker::run(). Thread stopped.";
}

void ThumbnailWorker::runSingle(QPair<QUrl, int> w)
{
    qDebug() << "Running single thumbnail generation for:" << w.first.toString() << "at" << w.second << "seconds";
    
    if (_cacheSize > SIZE_THRESHOLD) {
        qInfo() << "Thumbnail cache size exceeds maximum threshold, cleaning up cache";
        _cache.clear();
        _cacheSize = 0;
        qDebug() << "Cache cleared in single run mode. New cache size:" << _cacheSize;
    } else {
        qDebug() << "Cache size within threshold in single run mode:" << _cacheSize;
    }

    if (!isThumbGenerated(w.first, w.second)) {
        qDebug() << "Thumbnail not generated for current request in single run mode. Generating now.";
        auto pm = genThumb(w.first, w.second);

        QMutexLocker lock(&m_thumbLock);
        qDebug() << "Acquired m_thumbLock for cache insertion in single run mode.";
        _cache[w.first].insert(w.second, pm);
        _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
        qDebug() << "Thumbnail inserted into cache in single run mode. New cache size:" << _cacheSize;

        QTime d(0, 0, 0);
        d = d.addSecs(w.second);
        qInfo() << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
    }

    emit thumbGenerated(w.first, w.second);
}
}


