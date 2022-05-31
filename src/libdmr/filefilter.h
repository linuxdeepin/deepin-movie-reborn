/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#ifndef FILEFILTER_H
#define FILEFILTER_H

#include <QObject>
#include <QUrl>
#include <QDir>
#include <QLibrary>
#include <QLibraryInfo>
#include <QFileInfo>
#include <QMap>
#include <QMimeDatabase>
#include <QMimeType>

extern "C" {
#include <libavformat/avformat.h>
}

extern "C" {
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
}

typedef int (*mvideo_avformat_open_input)(AVFormatContext **ps, const char *url, AVInputFormat *fmt, AVDictionary **options);
typedef int (*mvideo_avformat_find_stream_info)(AVFormatContext *ic, AVDictionary **options);
typedef void (*mvideo_avformat_close_input)(AVFormatContext **s);

typedef void (*mvideo_gst_init)(int *argc, char **argv[]);
typedef GstDiscoverer* (*mvideo_gst_discoverer_new)(GstClockTime timeout, GError **err);
typedef void (*mvideo_gst_discoverer_start) (GstDiscoverer *discoverer);
typedef void (*mvideo_gst_discoverer_stop) (GstDiscoverer *discoverer);
typedef gboolean (*mvideo_gst_discoverer_discover_uri_async) (GstDiscoverer *discoverer, const gchar *uri);
typedef gchar* (*mvideo_gst_discoverer_info_get_uri) (const GstDiscovererInfo* info);
typedef GstDiscovererResult (*mvideo_gst_discoverer_info_get_result) (const GstDiscovererInfo* info);
typedef const GstStructure* (*mvideo_gst_discoverer_info_get_misc) (const GstDiscovererInfo* info);
typedef gchar* (*mvideo_gst_structure_to_string) (const GstStructure * structure);
typedef GList* (*mvideo_gst_discoverer_info_get_video_streams) (GstDiscovererInfo *info);
typedef GList* (*mvideo_gst_discoverer_info_get_audio_streams) (GstDiscovererInfo *info);
typedef GList* (*mvideo_gst_discoverer_info_get_subtitle_streams) (GstDiscovererInfo *info);

/**
 * @file 处理输入文件的公共类，对输入文件的路径做转换
 * 避免文件路径出现多钟形式如：软连接、本地url、网络url等
 */
class FileFilter:public QObject
{
    Q_OBJECT

    enum MediaType
    {
        Audio = 0,
        Video,
        Subtitle,
        Other
    };

public:
    ~FileFilter();

    static FileFilter* instance();
    /**
     * @brief 判断是否是多媒体文件
     * @param 文件路径
     */
    bool isMediaFile(QUrl url);
    /**
     * @brief 取出文件夹下所有文件路径
     * @param 文件夹
     * @return 返回url路径集合
     */
    QList<QUrl> filterDir(QDir dir);
    /**
     * @brief 转化文件字符路径为url
     * @param 文件路径
     * @return 返回url路径
     */
    QUrl fileTransfer(QString strFile);
    /**
     * @brief 判断是否是音频
     * @param 文件路径
     * @return 是否是音频
     */
    bool isAudio(QUrl url);
    /**
     * @brief 判断是否是字幕
     * @param 文件路径
     * @return 是否是字幕
     */
    bool isSubtitle(QUrl url);
    /**
     * @brief 判断是否是视频
     * @param 文件路径
     * @return 是否是视频
     */
    bool isVideo(QUrl url);
    /**
     * @brief 通过ffmpeg库判断文件类型
     * @param 文件路径
     * @return 类型
     */
    MediaType typeJudgeByFFmpeg(const QUrl& url);
    /**
     * @brief 通过Qt判断文件类型
     * @param 文件路径
     * @return 类型
     */
    MediaType typeJudgeByGst(const QUrl& url);

    static void discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, MediaType *miType);

    static void finished(GstDiscoverer *discoverer, GMainLoop *loop);

    void stopThread();
private:
    FileFilter();

    QString libPath(const QString &strlib);

private:
    static FileFilter* m_pFileFilter;
    QMap<QUrl, bool> m_mapCheckAudio;//检测播放文件中的音视频信息
    mvideo_avformat_open_input g_mvideo_avformat_open_input = nullptr;
    mvideo_avformat_find_stream_info g_mvideo_avformat_find_stream_info = nullptr;
    mvideo_avformat_close_input g_mvideo_avformat_close_input = nullptr;

    mvideo_gst_init g_mvideo_gst_init = nullptr;
    mvideo_gst_discoverer_new g_mvideo_gst_discoverer_new = nullptr;
    mvideo_gst_discoverer_start g_mvideo_gst_discoverer_start = nullptr;
    mvideo_gst_discoverer_stop g_mvideo_gst_discoverer_stop = nullptr;
    mvideo_gst_discoverer_discover_uri_async g_mvideo_gst_discoverer_discover_uri_async = nullptr;

    QMimeDatabase m_mimeDB;
    bool m_bMpvExists;
    bool m_stopRunningThread;
    GstDiscoverer* m_pDiscoverer;
    GMainLoop* m_pLoop;
    MediaType m_miType;
};

#endif // FILEFILTER_H
