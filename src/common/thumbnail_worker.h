#ifndef _DMR_THUMBNAIL_WORKER_H
#define _DMR_THUMBNAIL_WORKER_H 

#include <QtWidgets>
#include <libffmpegthumbnailer/videothumbnailer.h>

namespace dmr {
using namespace ffmpegthumbnailer;

class ThumbnailWorker: public QThread {
    Q_OBJECT
public:
    static ThumbnailWorker& get();

    // expected size for ui
    static QSize thumbSize() { return {158, 89}; }

    bool isThumbGenerated(const QUrl& url, int secs);
    QPixmap getThumb(const QUrl& url, int secs);

    void stop() { _quit.store(1); quit(); }

public slots:
    void requestThumb(const QUrl& url, int secs);

signals:
    void thumbGenerated(const QUrl& url, int secs);

private:
    QList<QPair<QUrl, int>> _wq;
    QHash<QUrl, QMap<int, QPixmap>> _cache;
    VideoThumbnailer thumber;
    QAtomicInt _quit{0};
    qint64 _cacheSize {0};

    ThumbnailWorker();
    void run() override;
    QPixmap genThumb(const QUrl& url, int secs);
};

}

#endif /* ifndef _DMR_THUMBNAIL_WORKER_H */
