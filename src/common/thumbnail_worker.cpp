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
#include "thumbnail_worker.h"
#include <atomic>
#include <mutex>
#include "player_engine.h"

#define SIZE_THRESHOLD (10 * 1<<20)

namespace dmr {
static std::atomic<ThumbnailWorker *> _instance { nullptr };
static QMutex _instLock;

static QMutex _thumbLock;
static QWaitCondition cond;

ThumbnailWorker &ThumbnailWorker::get()
{
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        if (_instance == nullptr) {
            _instance = new ThumbnailWorker;
#ifndef __mips__
            (*_instance).start();
#endif
        }
    }

    return *_instance;
}

bool ThumbnailWorker::isThumbGenerated(const QUrl &url, int secs)
{
    QMutexLocker lock(&_thumbLock);
    if (!_cache.contains(url)) return false;

    const auto &l = _cache[url];
    return l.contains(secs);
}

QPixmap ThumbnailWorker::getThumb(const QUrl &url, int secs)
{
    QMutexLocker lock(&_thumbLock);
    QPixmap pm;

    if (_cache.contains(url)) {
//        auto p = _cache[url].find(secs);
//        pm = *p;
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
#ifndef __mips__
    if (_thumbLock.tryLock()) {
        _wq.push_front(qMakePair(url, secs));
        cond.wakeOne();
        _thumbLock.unlock();
    }
#else
    runSingle(qMakePair(url, secs));
#endif
}

ThumbnailWorker::ThumbnailWorker()
{
    thumber.setThumbnailSize(static_cast<int>(thumbSize().width() * qApp->devicePixelRatio()));
    thumber.setMaintainAspectRatio(true);
}

QPixmap ThumbnailWorker::genThumb(const QUrl &url, int secs)
{
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
    pm.setDevicePixelRatio(dpr);

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    try {
        auto e = QProcessEnvironment::systemEnvironment();
        QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
        QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

        if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
                WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            return pm;
        }

        std::vector<uint8_t> buf;
        thumber.generateThumbnail(file.toUtf8().toStdString(),
                                  ThumbnailerImageType::Png, buf);

        auto img = QImage::fromData(buf.data(), static_cast<int>(buf.size()), "png");

        pm = QPixmap::fromImage(img.scaled(thumbSize() * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
    } catch (const std::logic_error &e) {
    }

    return pm;
}

void ThumbnailWorker::run()
{
    setPriority(QThread::IdlePriority);
    while (!_quit.load()) {

        QPair<QUrl, int> w;
        {
            QMutexLocker lock(&_thumbLock);
            while (_wq.isEmpty() && !_quit.load()) {
                cond.wait(lock.mutex(), 40);
            }

            if (!_wq.isEmpty()) {
                w = _wq.takeFirst();
                _wq.clear();
            }
        }

        if (_quit.load()) break;

        {
            QMutexLocker lock(&_thumbLock);
            //TODO: optimize: need a lru map
            if (_cacheSize > SIZE_THRESHOLD) {
                qDebug() << "thumb cache size exceeds maximum, clean up";
                _cache.clear();
                _cacheSize = 0;
            }
        }

        if (!isThumbGenerated(w.first, w.second)) {
            auto pm = genThumb(w.first, w.second);

            QMutexLocker lock(&_thumbLock);
            _cache[w.first].insert(w.second, pm);
            _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);

            QTime d(0, 0, 0);
            d = d.addSecs(w.second);
            qDebug() << "thumb for " << w.first << d.toString("hh:mm:ss");
        }

        emit thumbGenerated(w.first, w.second);
    }

    _wq.clear();
}

void ThumbnailWorker::runSingle(QPair<QUrl, int> w)
{
    if (_cacheSize > SIZE_THRESHOLD) {
        qDebug() << "thumb cache size exceeds maximum, clean up";
        _cache.clear();
        _cacheSize = 0;
    }

    if (!isThumbGenerated(w.first, w.second)) {
        auto pm = genThumb(w.first, w.second);

        QMutexLocker lock(&_thumbLock);
        _cache[w.first].insert(w.second, pm);
        _cacheSize += pm.width() * pm.height() * (pm.hasAlpha() ? 4 : 3);

        QTime d(0, 0, 0);
        d = d.addSecs(w.second);
        qDebug() << "thumb for " << w.first << d.toString("hh:mm:ss");
    }

    emit thumbGenerated(w.first, w.second);
}

}

