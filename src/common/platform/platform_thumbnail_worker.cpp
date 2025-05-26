// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_thumbnail_worker.h"
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
    qDebug() << "Destroying thumbnail worker";
    free(m_pCharTime);
    if (m_video_thumbnailer) {
        qDebug() << "Destroying video thumbnailer";
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
    }
}

Platform_ThumbnailWorker &Platform_ThumbnailWorker::get()
{
    if (m_instance == nullptr) {
        QMutexLocker lock(&m_instLock);
        qDebug() << "Creating new thumbnail worker instance";
        m_instance = new Platform_ThumbnailWorker;
        if(CompositingManager::get().platform() != Platform::Mips) {
            qDebug() << "Starting thumbnail worker thread";
            (*m_instance).start();
        } else {
            qDebug() << "Skipping thumbnail worker thread start on MIPS platform";
        }
    }
    return *m_instance;
}

bool Platform_ThumbnailWorker::isThumbGenerated(const QUrl &url, int secs)
{
    QMutexLocker lock(&m_thumbLock);
    if (!_cache.contains(url)) return false;

    const auto &l = _cache[url];
    return l.contains(secs);
}

QPixmap Platform_ThumbnailWorker::getThumb(const QUrl &url, int secs)
{
    QMutexLocker lock(&m_thumbLock);
    QPixmap pm;

    if (_cache.contains(url)) {
        pm = _cache[url].value(secs);
        qDebug() << "Retrieved cached thumbnail for" << url << "at" << secs << "seconds";
    } else {
        qDebug() << "No cached thumbnail found for" << url << "at" << secs << "seconds";
    }

    return pm;
}

void Platform_ThumbnailWorker::setPlayerEngine(PlayerEngine *pPlayerEngline)
{
    _engine = pPlayerEngline;
}

void Platform_ThumbnailWorker::requestThumb(const QUrl &url, int secs)
{
    qDebug() << "Requesting thumbnail for" << url << "at" << secs << "seconds";
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
        qDebug() << "Running thumbnail generation synchronously on MIPS platform";
        runSingle(qMakePair(url, secs));
    }
}

Platform_ThumbnailWorker::Platform_ThumbnailWorker()
{
    initThumb();
    m_video_thumbnailer->thumbnail_size = m_video_thumbnailer->thumbnail_size * qApp->devicePixelRatio();

    m_pCharTime = (char *)malloc(20);
}


void Platform_ThumbnailWorker::initThumb()
{
    qDebug() << "Initializing thumbnail generation library";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");
    
    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr) {
        qCritical() << "Failed to resolve required thumbnail generation functions";
        return;
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    if (m_video_thumbnailer == nullptr) {
        qCritical() << "Failed to create video thumbnailer instance";
        return;
    }
    qInfo() << "Thumbnail generation library initialized successfully";
}

QPixmap Platform_ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    qInfo() << "Generating thumbnail for" << url << "at" << secs << "seconds";
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
    pm.setDevicePixelRatio(dpr);

    if (m_image_data == nullptr) {
        qDebug() << "Creating new image data buffer";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    }

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    strcpy(m_pCharTime, d.toString("hh:mm:ss").toLatin1().data());
    m_video_thumbnailer->seek_time = m_pCharTime;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    
    try {
        qDebug() << "Generating thumbnail for file:" << file;
        m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
        auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");

        pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        qInfo() << "Successfully generated thumbnail" << pm.size();
    } catch (const std::logic_error &e) {
        qWarning() << "Failed to generate thumbnail:" << e.what();
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

    return pm;
}

void Platform_ThumbnailWorker::run()
{
    qInfo() << "Starting thumbnail worker thread";
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
                qDebug() << "Processing thumbnail request from queue";
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
                qInfo() << "Thumbnail cache size" << _cacheSize << "exceeds threshold" << SIZE_THRESHOLD << "- clearing cache";
                _cache.clear();
                _cacheSize = 0;
            }
        }

        if (!isThumbGenerated(w.first, w.second)) {
            qInfo() << "Generating new thumbnail for" << w.first << "at" << w.second << "seconds";
            auto pm = genThumb(w.first, w.second);

            QMutexLocker lock(&m_thumbLock);
            _cache[w.first].insert(w.second, pm);
            _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
            qInfo() << "Cache size after insertion:" << _cacheSize;

            QTime d(0, 0, 0);
            d = d.addSecs(w.second);
            qInfo() << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
        }

        emit thumbGenerated(w.first, w.second);
    }

    qInfo() << "Thumbnail worker thread stopping";
    _wq.clear();
}

void Platform_ThumbnailWorker::runSingle(QPair<QUrl, int> w)
{
    qInfo() << "Running single thumbnail generation for" << w.first << "at" << w.second << "seconds";
    
    if (_cacheSize > SIZE_THRESHOLD) {
        qInfo() << "Thumbnail cache size" << _cacheSize << "exceeds threshold" << SIZE_THRESHOLD << "- clearing cache";
        _cache.clear();
        _cacheSize = 0;
    }

    if (!isThumbGenerated(w.first, w.second)) {
        qInfo() << "Generating new thumbnail for" << w.first << "at" << w.second << "seconds";
        auto pm = genThumb(w.first, w.second);

        QMutexLocker lock(&m_thumbLock);
        _cache[w.first].insert(w.second, pm);
        _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);
        qInfo() << "Cache size after insertion:" << _cacheSize;

        QTime d(0, 0, 0);
        d = d.addSecs(w.second);
        qInfo() << "Generated thumbnail for" << w.first << "at" << d.toString("hh:mm:ss");
    }

    emit thumbGenerated(w.first, w.second);
}
}

