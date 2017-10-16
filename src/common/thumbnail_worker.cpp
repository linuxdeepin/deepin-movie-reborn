#include "thumbnail_worker.h"
#include <atomic>
#include <mutex>

#define SIZE_THRESHOLD (10 * 1<<20)

namespace dmr {
static std::atomic<ThumbnailWorker*> _instance { nullptr };
static QMutex _instLock;

static QMutex _thumbLock;
static QWaitCondition cond;

ThumbnailWorker& ThumbnailWorker::get()
{
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        if (_instance == nullptr) {
            _instance = new ThumbnailWorker;
            (*_instance).start();
        }
    }

    return *_instance;
}

bool ThumbnailWorker::isThumbGenerated(const QUrl& url, int secs)
{
    QMutexLocker lock(&_thumbLock);
    if (!_cache.contains(url)) return false;

    const auto& l = _cache[url];
    return l.contains(secs);
}

QPixmap ThumbnailWorker::getThumb(const QUrl& url, int secs)
{
    QMutexLocker lock(&_thumbLock);
    QPixmap pm;

    if (_cache.contains(url)) {
        auto p = _cache[url].find(secs);
        pm = *p;
    }

    return pm;
}


void ThumbnailWorker::requestThumb(const QUrl& url, int secs)
{
    QMutexLocker lock(&_thumbLock);
    _wq.push_front(qMakePair(url, secs));
    cond.wakeOne();
}

ThumbnailWorker::ThumbnailWorker()
{
    thumber.setThumbnailSize(thumbSize().width());
}

QPixmap ThumbnailWorker::genThumb(const QUrl& url, int secs)
{
    QPixmap pm;
    pm.fill(Qt::transparent);

    QTime d(0, 0, 0);
    d = d.addSecs(secs);
    thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
    try {
        std::vector<uint8_t> buf;
        thumber.generateThumbnail(file.toUtf8().toStdString(),
                ThumbnailerImageType::Png, buf);

        auto img = QImage::fromData(buf.data(), buf.size(), "png");

        pm = QPixmap::fromImage(img.scaled(thumbSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    } catch (const std::logic_error&) {
    }

    return pm;
}

void ThumbnailWorker::run()
{
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

}

