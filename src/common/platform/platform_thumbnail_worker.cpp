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

