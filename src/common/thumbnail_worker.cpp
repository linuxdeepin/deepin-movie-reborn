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

QPixmap ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    qDebug() << "Generating thumbnail for:" << url.toString() << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
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


