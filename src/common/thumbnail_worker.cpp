// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_worker.h"
#include "ddlog.h"
#include "player_engine.h"
#include <QLibrary>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "compositing_manager.h"
#include "sysutils.h"

#define SIZE_THRESHOLD (10 * 1<<20)

namespace dmr {
std::atomic<ThumbnailWorker *> ThumbnailWorker::m_instance(nullptr);
QMutex ThumbnailWorker::m_instLock;
QMutex ThumbnailWorker::m_thumbLock;
QWaitCondition ThumbnailWorker::m_cond;

ThumbnailWorker::~ThumbnailWorker()
{
    qCDebug(appMovie) << "Destroying ThumbnailWorker instance";
    free(m_pCharTime);
    if (m_video_thumbnailer) {
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
        qCDebug(appMovie) << "Video thumbnailer destroyed";
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
        qCDebug(appMovie) << "No thumbnail cache found for:" << url.toString();
        return false;
    }

    const auto &l = _cache[url];
    bool exists = l.contains(secs);
    qCDebug(appMovie) << "Thumbnail exists for" << url.toString() << "at" << secs << "seconds:" << exists;
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
    qCDebug(appMovie) << "Requesting thumbnail for:" << url.toString() << "at" << secs << "seconds";
    if(CompositingManager::get().platform() != Platform::Mips) {
        if (m_thumbLock.tryLock()) {
            _wq.push_front(qMakePair(url, secs));
            m_cond.wakeOne();
            m_thumbLock.unlock();
            qCDebug(appMovie) << "Added thumbnail request to queue";
        } else {
            qCWarning(appMovie) << "Failed to acquire lock for thumbnail request";
        }
    } else {
        qCDebug(appMovie) << "Running thumbnail generation in single thread mode";
        runSingle(qMakePair(url, secs));
    }
}

ThumbnailWorker::ThumbnailWorker()
{
    qCDebug(appMovie) << "Initializing ThumbnailWorker";
    initThumb();
    m_video_thumbnailer->thumbnail_size = m_video_thumbnailer->thumbnail_size * qApp->devicePixelRatio();
    qCDebug(appMovie) << "Thumbnail size set based on device pixel ratio:" << m_video_thumbnailer->thumbnail_size;

    m_pCharTime = (char *)malloc(20);

    qCDebug(appMovie) << "Exiting ThumbnailWorker constructor.";
}


void ThumbnailWorker::initThumb()
{
    qCDebug(appMovie) << "Initializing thumbnail library";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");
    qCDebug(appMovie) << "Attempted to resolve thumbnail functions.";

    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr) {
        qCCritical(appMovie) << "Failed to resolve required thumbnail functions from library";
        return;
    } else {
        qCDebug(appMovie) << "All required thumbnail functions resolved successfully.";
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    if (!m_video_thumbnailer) {
        qCCritical(appMovie) << "Failed to create video thumbnailer instance";
    } else {
        qCInfo(appMovie) << "Successfully initialized thumbnail library";
    }
    qCDebug(appMovie) << "Exiting ThumbnailWorker::initThumb().";
}

QPixmap ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    qCDebug(appMovie) << "Generating thumbnail for:" << url.toString() << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();

    // Try precise thumbnail first (decode to exact frame), fall back to ffmpegthumbnailer
    QPixmap pm = PreciseThumbnail::generate(url, secs, thumbSize(), dpr);
    if (!pm.isNull()) {
        qDebug() << "Precise thumbnail generated successfully, size:" << pm.size();
        return pm;
    }

    qWarning() << "Precise thumbnail failed, falling back to ffmpegthumbnailer";
    pm.setDevicePixelRatio(dpr);
    qCDebug(appMovie) << "Device pixel ratio:" << dpr << ", Pixmap device pixel ratio set.";

    if (m_image_data == nullptr) {
        qCDebug(appMovie) << "m_image_data is nullptr, creating new image data buffer.";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    } else {
        qCDebug(appMovie) << "m_image_data already exists.";
    }

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    strncpy(m_pCharTime, d.toString("hh:mm:ss").toLatin1().data(), 19);
    m_pCharTime[19] = '\0';
    m_video_thumbnailer->seek_time = m_pCharTime;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    qCDebug(appMovie) << "Seek time set. File path for thumbnail generation:" << file;
    
    try {
        qCDebug(appMovie) << "Generating thumbnail for file:" << file;
        m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
        auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
        qCDebug(appMovie) << "Image data converted to QImage. Image size:" << img.size();

        pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        qCDebug(appMovie) << "Successfully generated thumbnail, size:" << pm.size();
    } catch (const std::logic_error &e) {
        qCWarning(appMovie) << "Failed to generate thumbnail:" << e.what();
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

    qCDebug(appMovie) << "Exiting ThumbnailWorker::genThumb(). Returning pixmap.";
    return pm;
}

void ThumbnailWorker::run()
{
    qCDebug(appMovie) << "Starting thumbnail worker thread";
    setPriority(QThread::IdlePriority);
    qCDebug(appMovie) << "Thread priority set to IdlePriority.";
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
            qCDebug(appMovie) << "Acquired m_thumbLock for queue processing.";
            while (_wq.isEmpty() && 
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                   !_quit.load()
#else
                   !_quit
#endif
            ) {
                qCDebug(appMovie) << "Waiting for thumbnail requests or quit signal. Queue is empty.";
                m_cond.wait(lock.mutex(), 40);
            }

            if (!_wq.isEmpty()) {
                w = _wq.takeFirst();
                _wq.clear();
                qCDebug(appMovie) << "Processing thumbnail request from queue";
            }
        }

        if (
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            _quit.load()
#else
            _quit
#endif
        ) {
            qCDebug(appMovie) << "Quit signal detected in run loop. Breaking.";
            break;
        }

        {
            QMutexLocker lock(&m_thumbLock);
            qCDebug(appMovie) << "Acquired m_thumbLock for cache management.";
            //TODO: optimize: need a lru map
            if (_cacheSize > SIZE_THRESHOLD) {
                qCInfo(appMovie) << "Thumbnail cache size exceeds maximum threshold, cleaning up cache";
                _cache.clear();
                _cacheSize = 0;
                qCDebug(appMovie) << "Cache cleared. New cache size:" << _cacheSize;
            } else {
                qCDebug(appMovie) << "Cache size within threshold:" << _cacheSize;
            }
        }

        if (!isThumbGenerated(w.first, w.second)) {
            qCDebug(appMovie) << "Thumbnail not generated for current request. Generating now.";
            auto pm = genThumb(w.first, w.second);

            QMutexLocker lock(&m_thumbLock);
            qCDebug(appMovie) << "Acquired m_thumbLock for cache insertion.";
            _cache[w.first].insert(w.second, pm);
            _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
            qCDebug(appMovie) << "Thumbnail inserted into cache. New cache size:" << _cacheSize;

            QTime d(0, 0, 0);
            d = d.addSecs(w.second);
            qCInfo(appMovie) << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
        } else {
            qCDebug(appMovie) << "Thumbnail already generated for" << w.first.toString() << "at" << w.second << "seconds. Skipping generation.";
        }

        emit thumbGenerated(w.first, w.second);
        qCDebug(appMovie) << "Emitted thumbGenerated signal.";
    }

    qCDebug(appMovie) << "Clearing thumbnail request queue";
    _wq.clear();
    qCDebug(appMovie) << "Exiting ThumbnailWorker::run(). Thread stopped.";
}

void ThumbnailWorker::runSingle(QPair<QUrl, int> w)
{
    qCDebug(appMovie) << "Running single thumbnail generation for:" << w.first.toString() << "at" << w.second << "seconds";
    
    if (_cacheSize > SIZE_THRESHOLD) {
        qCInfo(appMovie) << "Thumbnail cache size exceeds maximum threshold, cleaning up cache";
        _cache.clear();
        _cacheSize = 0;
        qCDebug(appMovie) << "Cache cleared in single run mode. New cache size:" << _cacheSize;
    } else {
        qCDebug(appMovie) << "Cache size within threshold in single run mode:" << _cacheSize;
    }

    if (!isThumbGenerated(w.first, w.second)) {
        qCDebug(appMovie) << "Thumbnail not generated for current request in single run mode. Generating now.";
        auto pm = genThumb(w.first, w.second);

        QMutexLocker lock(&m_thumbLock);
        qCDebug(appMovie) << "Acquired m_thumbLock for cache insertion in single run mode.";
        _cache[w.first].insert(w.second, pm);
        _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
        qCDebug(appMovie) << "Thumbnail inserted into cache in single run mode. New cache size:" << _cacheSize;

        QTime d(0, 0, 0);
        d = d.addSecs(w.second);
        qCInfo(appMovie) << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
    }

    emit thumbGenerated(w.first, w.second);
}
}


