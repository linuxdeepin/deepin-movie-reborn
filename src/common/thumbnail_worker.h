// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

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
#ifndef _DMR_THUMBNAIL_WORKER_H
#define _DMR_THUMBNAIL_WORKER_H

#include <QtWidgets>
#include <libffmpegthumbnailer/videothumbnailerc.h>
#include <atomic>
#include <mutex>


typedef video_thumbnailer *(*mvideo_thumbnailer)();
typedef void (*mvideo_thumbnailer_destroy)(video_thumbnailer *thumbnailer);
/* create image_data structure */
typedef image_data *(*mvideo_thumbnailer_create_image_data)(void);
/* destroy image_data structure */
typedef void (*mvideo_thumbnailer_destroy_image_data)(image_data *data);
typedef int (*mvideo_thumbnailer_generate_thumbnail_to_buffer)(video_thumbnailer *thumbnailer, const char *movie_filename, image_data *generated_image_data);

namespace dmr {
//using namespace ffmpegthumbnailer;

class PlayerEngine;

class ThumbnailWorker: public QThread
{
    Q_OBJECT
public:
    ~ThumbnailWorker();
    static ThumbnailWorker &get();
    // expected size for ui
    static QSize thumbSize()
    {
        return {178, 101};
    }
    bool isThumbGenerated(const QUrl &url, int secs);
    QPixmap getThumb(const QUrl &url, int secs);
    void stop()
    {
        _quit.store(1);
        quit();
    }
    void setPlayerEngine(PlayerEngine *pPlayerEngline);
public slots:
    void requestThumb(const QUrl &url, int secs);

signals:
    void thumbGenerated(const QUrl &url, int secs);

private:
    QList<QPair<QUrl, int>> _wq;
    QHash<QUrl, QMap<int, QPixmap>> _cache;
    QAtomicInt _quit{0};
    qint64 _cacheSize {0};
    video_thumbnailer *m_video_thumbnailer = nullptr;
    image_data *m_image_data = nullptr;
    PlayerEngine *_engine {nullptr};
    mvideo_thumbnailer m_mvideo_thumbnailer = nullptr;
    mvideo_thumbnailer_destroy m_mvideo_thumbnailer_destroy = nullptr;
    mvideo_thumbnailer_create_image_data m_mvideo_thumbnailer_create_image_data = nullptr;
    mvideo_thumbnailer_destroy_image_data m_mvideo_thumbnailer_destroy_image_data = nullptr;
    mvideo_thumbnailer_generate_thumbnail_to_buffer m_mvideo_thumbnailer_generate_thumbnail_to_buffer = nullptr;
    char *m_pCharTime;

    ThumbnailWorker();
    void initThumb();
    void run() override;
    void runSingle(QPair<QUrl, int> w);
    QPixmap genThumb(const QUrl &url, int secs);
    QString libPath(const QString &strlib);

private:
    static std::atomic<ThumbnailWorker *> m_instance;
    static QMutex m_instLock;
    static QMutex m_thumbLock;
    static QWaitCondition m_cond;
};

}

#endif /* ifndef _DMR_THUMBNAIL_WORKER_H */
