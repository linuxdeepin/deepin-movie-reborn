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
#include "filefilter.h"
#include "compositing_manager.h"

FileFilter* FileFilter::m_pFileFilter = new FileFilter;

FileFilter::FileFilter()
{
    m_bMpvExists = dmr::CompositingManager::isMpvExists();
    m_stopRunningThread = false;
    m_pDiscoverer = nullptr;
    m_pLoop = nullptr;
    m_miType = MediaType::Other;

    QLibrary avformatLibrary(libPath("libavformat.so"));

    g_mvideo_avformat_open_input = (mvideo_avformat_open_input) avformatLibrary.resolve("avformat_open_input");
    g_mvideo_avformat_find_stream_info = (mvideo_avformat_find_stream_info) avformatLibrary.resolve("avformat_find_stream_info");
    g_mvideo_avformat_close_input = (mvideo_avformat_close_input) avformatLibrary.resolve("avformat_close_input");

    gst_init(nullptr, nullptr);

    GError *pGErr = nullptr;
    m_pDiscoverer = gst_discoverer_new(5 * GST_SECOND, &pGErr);
    m_pLoop = g_main_loop_new(nullptr, FALSE);

    if (!m_pDiscoverer) {
        qInfo() << "Error creating discoverer instance: " << pGErr->message;
        g_clear_error (&pGErr);
    }

    g_signal_connect_data(m_pDiscoverer, "discovered", (GCallback)discovered, &m_miType, nullptr, GConnectFlags(0));
    g_signal_connect_data(m_pDiscoverer, "finished",  (GCallback)(finished), m_pLoop, nullptr, GConnectFlags(0));

    gst_discoverer_start(m_pDiscoverer);
}

QString FileFilter::libPath(const QString &strlib)
{
    QDir  dir;
    QString path  = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    dir.setPath(path);
    QStringList list = dir.entryList(QStringList() << (strlib + "*"), QDir::NoDotAndDotDot | QDir::Files); //filter name with strlib
    if (list.contains(strlib)) {
        return strlib;
    } else {
        list.sort();
    }

    if(list.size() > 0)
        return list.last();
    else
        return QString();
}

FileFilter::~FileFilter()
{
    gst_discoverer_stop(m_pDiscoverer);
    g_object_unref(m_pDiscoverer);
    g_main_loop_unref(m_pLoop);
}

FileFilter *FileFilter::instance()
{
    if (nullptr == m_pFileFilter)
    {
        m_pFileFilter = new FileFilter();
    }
    return m_pFileFilter;
}

bool FileFilter::isMediaFile(QUrl url)
{
    MediaType miType;
    bool bMedia = false;

    if(!url.isLocalFile()) {   // url 文件不做判断,默认可以播放
        return true;
    }

    if (m_bMpvExists) {
        miType = typeJudgeByFFmpeg(url);
    } else {
        miType = typeJudgeByGst(url);
    }

    if (miType == MediaType::Audio || miType == MediaType::Video) {
        bMedia = true;
    }

    return bMedia;
}

QList<QUrl> FileFilter::filterDir(QDir dir)
{
    QList<QUrl> lstUrl;
    QDir di(dir);

    di.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    for (QFileInfo fileInfo : di.entryInfoList()) {
        if (fileInfo.isFile()) {
            lstUrl.append(fileTransfer(fileInfo.filePath()));
        } else if (fileInfo.isDir()) {
            if (m_stopRunningThread)
                return QList<QUrl>();
            lstUrl << filterDir(fileInfo.absoluteFilePath());
        }
    }
    return lstUrl;
}

QUrl FileFilter::fileTransfer(QString strFile)
{
    QUrl realUrl;
    bool bLocalFile = false;

    bLocalFile = QUrl(strFile).isLocalFile();

    if (bLocalFile)
    {
        strFile = QUrl(strFile).toLocalFile();
    }

    if (QFileInfo(strFile).isFile() || QFileInfo(strFile).isDir()) {      // 如果是软链接则需要找到真实路径
        while (QFileInfo(strFile).isSymLink()) {
            strFile = QFileInfo(strFile).symLinkTarget();
        }
        realUrl = QUrl::fromLocalFile(strFile);
    }
    else {
        realUrl = QUrl(strFile);
    }

    return realUrl;
}

bool FileFilter::isAudio(QUrl url)
{
    bool bAudio = false;

    if(m_mapCheckAudio.contains(url)) {
        return m_mapCheckAudio.value(url);
    } else {
        m_mapCheckAudio.clear();
    }

    if (m_bMpvExists) {
        bAudio = typeJudgeByFFmpeg(url) == MediaType::Audio ? true : false;
    } else {
        bAudio = typeJudgeByGst(url) == MediaType::Audio ? true : false;
    }

    m_mapCheckAudio[url] = bAudio;

    return bAudio;
}

bool FileFilter::isSubtitle(QUrl url)
{
    if (m_bMpvExists) {
        return typeJudgeByFFmpeg(url) == MediaType::Subtitle ? true : false;
    } else {
        return typeJudgeByGst(url) == MediaType::Subtitle ? true : false;
    }
}

bool FileFilter::isVideo(QUrl url)
{
    if (m_bMpvExists) {
        return typeJudgeByFFmpeg(url) == MediaType::Video ? true : false;
    } else {
        return typeJudgeByGst(url) == MediaType::Video ? true : false;
    }
}

FileFilter::MediaType FileFilter::typeJudgeByFFmpeg(const QUrl &url)
{
    int nRet;
    QString strFormatName;
    bool bVCodec = false;
    bool bACodec = false;
    bool bSCodec = false;
    MediaType miType = MediaType::Other;

    QString strMimeType = m_mimeDB.mimeTypeForUrl(url).name();

    AVFormatContext *av_ctx = nullptr;

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toString().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return MediaType::Other;
    }
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return MediaType::Other;
    }

    strFormatName = av_ctx->iformat->long_name;

    for (int i = 0; i < static_cast<int>(av_ctx->nb_streams); i++)
     {
         AVStream *in_stream = av_ctx->streams[i];

         if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
             bVCodec = true;
         } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
             bACodec = true;
         } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
             bSCodec = true;
         }
    }

    if (bVCodec) {
        miType = MediaType::Video;
    } else if (bACodec) {
        miType = MediaType::Audio;
    } else if (bSCodec) {
        miType = MediaType::Subtitle;
    } else {
        miType = MediaType::Other;
    }

    if(strFormatName.contains("Tele-typewriter") || strMimeType.startsWith("image/"))       // 排除文本文件，如果只用mimetype判断会遗漏部分原始格式文件如：h264裸流
        miType = MediaType::Other;

    g_mvideo_avformat_close_input(&av_ctx);

    return  miType;
}

FileFilter::MediaType FileFilter::typeJudgeByGst(const QUrl &url)
{
    char *uri = nullptr;
    uri = new char[200];

    m_miType = MediaType::Other;

    QString strMimeType = m_mimeDB.mimeTypeForUrl(url).name();

    if (!strMimeType.startsWith("audio/") && !strMimeType.startsWith("video/")) {
        delete []uri;
        return MediaType::Other;
    }

    uri = strcpy(uri, url.toString().toUtf8().constData());

    if (!gst_discoverer_discover_uri_async (m_pDiscoverer, uri)) {
      qInfo() << "Failed to start discovering URI " << uri;
      g_object_unref (m_pDiscoverer);
    }

    g_main_loop_run(m_pLoop);

    delete []uri;

    return m_miType;
}

void FileFilter::stopThread()
{
    m_stopRunningThread = true;
}

void FileFilter::discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, MediaType *miType)
{
    Q_UNUSED(discoverer);

    GstDiscovererResult result;
    const gchar *uri;
    bool bVideo = false;
    bool bAudio = false;
    bool bSubtitle = false;

    uri = gst_discoverer_info_get_uri (info);
    result = gst_discoverer_info_get_result (info);

    switch (result) {
      case GST_DISCOVERER_URI_INVALID:
        qInfo() << "Invalid URI " << uri;
        break;
      case GST_DISCOVERER_ERROR:
        qInfo() << "Discoverer error: " << err->message;
        break;
      case GST_DISCOVERER_TIMEOUT:
        qInfo() << "Timeout";
        break;
      case GST_DISCOVERER_BUSY:
        qInfo() << "Busy";
        break;
      case GST_DISCOVERER_MISSING_PLUGINS:{
        const GstStructure *s;
        gchar *str;

        s = gst_discoverer_info_get_misc (info);
        str = gst_structure_to_string (s);

        qInfo() << "Missing plugins: " << str;
        g_free (str);
        break;
      }
      case GST_DISCOVERER_OK:
        qInfo() << "Discovered " << uri;
        break;
    }

    if (result != GST_DISCOVERER_OK) {
      qInfo() << "This URI cannot be played";
      return;
    }

    GList *list;
    list = gst_discoverer_info_get_video_streams(info);
    if (list) {
       bVideo = true;
    }
    list = gst_discoverer_info_get_audio_streams(info);
    if (list) {
        bAudio = true;
    }
    list = gst_discoverer_info_get_subtitle_streams(info);
    if(list) {
        bSubtitle = true;
    }

    if (bVideo) {
        *miType = MediaType::Video;
    } else if (bAudio) {
        *miType = MediaType::Audio;
    } else if (bSubtitle) {
        *miType = MediaType::Subtitle;
    } else {
        *miType = MediaType::Other;
    }
}

void FileFilter::finished(GstDiscoverer *discoverer, GMainLoop *loop)
{
    Q_UNUSED(discoverer);

    g_main_loop_quit(loop);
}

