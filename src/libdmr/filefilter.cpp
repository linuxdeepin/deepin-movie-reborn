// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filefilter.h"
#include "compositing_manager.h"
#include "sysutils.h"

#include <iostream>
#include <functional>
using namespace std;

FileFilter* FileFilter::m_pFileFilter = new FileFilter;

static mvideo_gst_discoverer_info_get_uri g_mvideo_gst_discoverer_info_get_uri = nullptr;
static mvideo_gst_discoverer_info_get_result g_mvideo_gst_discoverer_info_get_result = nullptr;
static mvideo_gst_discoverer_info_get_misc g_mvideo_gst_discoverer_info_get_misc = nullptr;
static mvideo_gst_structure_to_string g_mvideo_gst_structure_to_string = nullptr;
static mvideo_gst_discoverer_info_get_video_streams g_mvideo_gst_discoverer_info_get_video_streams = nullptr;
static mvideo_gst_discoverer_info_get_audio_streams g_mvideo_gst_discoverer_info_get_audio_streams = nullptr;
static mvideo_gst_discoverer_info_get_subtitle_streams g_mvideo_gst_discoverer_info_get_subtitle_streams = nullptr;

FileFilter::FileFilter()
{
    qDebug() << "Initializing FileFilter";
    m_bMpvExists = dmr::CompositingManager::isMpvExists();
    qInfo() << "MPV exists:" << m_bMpvExists;
    m_stopRunningThread = false;
    m_pDiscoverer = nullptr;
    m_pLoop = nullptr;
    m_miType = MediaType::Other;

    QLibrary avformatLibrary(SysUtils::libPath("libavformat.so"));

    g_mvideo_avformat_open_input = (mvideo_avformat_open_input) avformatLibrary.resolve("avformat_open_input");
    g_mvideo_avformat_find_stream_info = (mvideo_avformat_find_stream_info) avformatLibrary.resolve("avformat_find_stream_info");
    g_mvideo_avformat_close_input = (mvideo_avformat_close_input) avformatLibrary.resolve("avformat_close_input");

    QLibrary gstreamerLibrary(SysUtils::libPath("libgstreamer-1.0.so"));
    QLibrary gstpbutilsLibrary(SysUtils::libPath("libgstpbutils-1.0.so"));

    g_mvideo_gst_init = (mvideo_gst_init) gstreamerLibrary.resolve("gst_init");
    g_mvideo_gst_discoverer_new = (mvideo_gst_discoverer_new) gstpbutilsLibrary.resolve("gst_discoverer_new");
    g_mvideo_gst_discoverer_start = (mvideo_gst_discoverer_start) gstpbutilsLibrary.resolve("gst_discoverer_start");
    g_mvideo_gst_discoverer_stop = (mvideo_gst_discoverer_stop) gstpbutilsLibrary.resolve("gst_discoverer_stop");
    g_mvideo_gst_discoverer_discover_uri_async = (mvideo_gst_discoverer_discover_uri_async) gstpbutilsLibrary.resolve("gst_discoverer_discover_uri_async");

    g_mvideo_gst_discoverer_info_get_uri = (mvideo_gst_discoverer_info_get_uri) gstpbutilsLibrary.resolve("gst_discoverer_info_get_uri");
    g_mvideo_gst_discoverer_info_get_result = (mvideo_gst_discoverer_info_get_result) gstpbutilsLibrary.resolve("gst_discoverer_info_get_result");
    g_mvideo_gst_discoverer_info_get_misc = (mvideo_gst_discoverer_info_get_misc) gstpbutilsLibrary.resolve("gst_discoverer_info_get_misc");
    g_mvideo_gst_structure_to_string = (mvideo_gst_structure_to_string) gstreamerLibrary.resolve("gst_structure_to_string");
    g_mvideo_gst_discoverer_info_get_video_streams = (mvideo_gst_discoverer_info_get_video_streams) gstpbutilsLibrary.resolve("gst_discoverer_info_get_video_streams");
    g_mvideo_gst_discoverer_info_get_audio_streams = (mvideo_gst_discoverer_info_get_audio_streams) gstpbutilsLibrary.resolve("gst_discoverer_info_get_audio_streams");
    g_mvideo_gst_discoverer_info_get_subtitle_streams = (mvideo_gst_discoverer_info_get_subtitle_streams) gstpbutilsLibrary.resolve("gst_discoverer_info_get_subtitle_streams");

    g_mvideo_gst_init(nullptr, nullptr);

    GError *pGErr = nullptr;
    m_pDiscoverer = g_mvideo_gst_discoverer_new(5 * GST_SECOND, &pGErr);
    m_pLoop = g_main_loop_new(nullptr, FALSE);

    if (!m_pDiscoverer) {
        qCritical() << "Failed to create discoverer instance:" << pGErr->message;
        g_clear_error (&pGErr);
    }

    g_signal_connect_data(m_pDiscoverer, "discovered", (GCallback)(discovered), &m_miType, nullptr, GConnectFlags(0));
    g_signal_connect_data(m_pDiscoverer, "finished",  (GCallback)(finished), m_pLoop, nullptr, GConnectFlags(0));

    g_mvideo_gst_discoverer_start(m_pDiscoverer);
    qDebug() << "FileFilter initialization completed";
}

FileFilter::~FileFilter()
{
    qDebug() << "Destroying FileFilter";
    g_mvideo_gst_discoverer_stop(m_pDiscoverer);
    g_object_unref(m_pDiscoverer);
    g_main_loop_unref(m_pLoop);
}

FileFilter *FileFilter::instance()
{
    if (nullptr == m_pFileFilter) {
        qDebug() << "Creating new FileFilter instance";
        m_pFileFilter = new FileFilter();
    }
    return m_pFileFilter;
}

bool FileFilter::isMediaFile(QUrl url)
{
    qDebug() << "Checking if file is media:" << url.toString();
    MediaType miType;
    bool bMedia = false;

    if(!url.isLocalFile()) {   // url 文件不做判断,默认可以播放
        qInfo() << "Non-local URL, assuming media file";
        return true;
    }

    qDebug() << "URL is local. Determining media type.";
    if (m_bMpvExists) {
        qDebug() << "MPV exists. Judging type by FFmpeg.";
        miType = typeJudgeByFFmpeg(url);
    } else {
        qDebug() << "MPV does not exist. Judging type by Gst.";
        miType = typeJudgeByGst(url);
    }

    if (miType == MediaType::Audio || miType == MediaType::Video) {
        qDebug() << "Media type is Audio or Video. Setting bMedia to true.";
        bMedia = true;
    }

    qDebug() << "Media type:" << (bMedia ? "Media" : "Not media");
    return bMedia;
}

QList<QUrl> FileFilter::filterDir(QDir dir)
{
    qDebug() << "Filtering directory:" << dir.absolutePath();
    QList<QUrl> lstUrl;
    QDir di(dir);
    qDebug() << "QDir object created for:" << di.absolutePath();

    di.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    qDebug() << "Directory filter set.";

    for (QFileInfo fileInfo : di.entryInfoList()) {
        qDebug() << "Processing entry:" << fileInfo.absoluteFilePath();
        if (fileInfo.isFile()) {
            qDebug() << "Entry is a file. Transferring file:" << fileInfo.filePath();
            lstUrl.append(fileTransfer(fileInfo.filePath()));
            qDebug() << "File transferred and added to list. Current list size:" << lstUrl.size();
        } else if (fileInfo.isDir()) {
            qDebug() << "Entry is a directory:" << fileInfo.absoluteFilePath();
            if (m_stopRunningThread) {
                qDebug() << "Thread stopped, returning empty list";
                return QList<QUrl>();
            }
            lstUrl << filterDir(fileInfo.absoluteFilePath());
        }
    }
    qDebug() << "Found" << lstUrl.size() << "files in directory";
    return lstUrl;
}

QUrl FileFilter::fileTransfer(QString strFile)
{
    qDebug() << "Entering FileFilter::fileTransfer() with strFile:" << strFile;
    QUrl realUrl;
    bool bLocalFile = false;

    bLocalFile = QUrl(strFile).isLocalFile();
    qDebug() << "strFile is local file:" << bLocalFile;

    if (bLocalFile)
    {
        qDebug() << "strFile is a local file. Converting to local file path.";
        strFile = QUrl(strFile).toLocalFile();
    }

    if (QFileInfo(strFile).isFile() || QFileInfo(strFile).isDir()) {      // 如果是软链接则需要找到真实路径
        qDebug() << "strFile is a file or directory. Checking for symlink.";
        while (QFileInfo(strFile).isSymLink()) {
            strFile = QFileInfo(strFile).symLinkTarget();
            qDebug() << "Resolved symlink target:" << strFile;
        }
        realUrl = QUrl::fromLocalFile(strFile);
        qDebug() << "realUrl set from local file:" << realUrl.toString();
    }
    else {
        qDebug() << "strFile is neither a file nor a directory. Setting realUrl from raw string.";
        realUrl = QUrl(strFile);
        qDebug() << "realUrl set from raw string:" << realUrl.toString();
    }

    qDebug() << "Exiting FileFilter::fileTransfer() with realUrl:" << realUrl.toString();
    return realUrl;
}

bool FileFilter::isAudio(QUrl url)
{
    qDebug() << "Entering FileFilter::isAudio() with URL:" << url.toString();
    bool bAudio = false;

    if(m_mapCheckAudio.contains(url)) {
        qDebug() << "m_mapCheckAudio contains URL. Returning cached value.";
        bool result = m_mapCheckAudio.value(url);
        qDebug() << "Exiting FileFilter::isAudio() (cached) with result:" << result;
        return result;
    } else {
        qDebug() << "m_mapCheckAudio does not contain URL. Clearing map.";
        m_mapCheckAudio.clear();
    }

    if (m_bMpvExists) {
        qDebug() << "MPV exists. Judging type by FFmpeg.";
        bAudio = typeJudgeByFFmpeg(url) == MediaType::Audio ? true : false;
    } else {
        qDebug() << "MPV does not exist. Judging type by Gst.";
        bAudio = typeJudgeByGst(url) == MediaType::Audio ? true : false;
    }

    qDebug() << "Storing result in m_mapCheckAudio. bAudio:" << bAudio;
    m_mapCheckAudio[url] = bAudio;

    qDebug() << "Exiting FileFilter::isAudio() with result:" << bAudio;
    return bAudio;
}

bool FileFilter::isSubtitle(QUrl url)
{
    qDebug() << "Entering FileFilter::isSubtitle() with URL:" << url.toString();
    bool result;
    if (m_bMpvExists) {
        qDebug() << "MPV exists. Judging type by FFmpeg.";
        result = typeJudgeByFFmpeg(url) == MediaType::Subtitle ? true : false;
    } else {
        qDebug() << "MPV does not exist. Judging type by Gst.";
        result = typeJudgeByGst(url) == MediaType::Subtitle ? true : false;
    }
    qDebug() << "Exiting FileFilter::isSubtitle() with result:" << result;
    return result;
}

bool FileFilter::isVideo(QUrl url)
{
    qDebug() << "Entering FileFilter::isVideo() with URL:" << url.toString();
    bool result;
    if (m_bMpvExists) {
        qDebug() << "MPV exists. Judging type by FFmpeg.";
        result = typeJudgeByFFmpeg(url) == MediaType::Video ? true : false;
    } else {
        qDebug() << "MPV does not exist. Judging type by Gst.";
        result = typeJudgeByGst(url) == MediaType::Video ? true : false;
    }
    qDebug() << "Exiting FileFilter::isVideo() with result:" << result;
    return result;
}

FileFilter::MediaType FileFilter::typeJudgeByFFmpeg(const QUrl &url)
{
    qDebug() << "Judging media type by FFmpeg:" << url.toString();
    int nRet;
    QString strFormatName;
    bool bVCodec = false;
    bool bACodec = false;
    bool bSCodec = false;
    MediaType miType = MediaType::Other;

    QString strMimeType = m_mimeDB.mimeTypeForUrl(url).name();
    qDebug() << "MIME type:" << strMimeType;

    if (strMimeType.contains("mpegurl")) {
        qDebug() << "Mpegurl format detected, returning Other";
        return MediaType::Other;
    }

    AVFormatContext *av_ctx = nullptr;

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toLocalFile().toUtf8().constData(), nullptr, nullptr);
    qDebug() << "avformat_open_input returned:" << nRet;

    if(nRet < 0) {
        qWarning() << "Failed to open input file:" << url.toString();
        return MediaType::Other;
    }
    qDebug() << "Input file opened successfully.";
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        qDebug() << "Failed to find stream info. Returning MediaType::Other.";
        return MediaType::Other;
    }
    qDebug() << "Stream info found successfully.";

    strFormatName = av_ctx->iformat->long_name;
    qDebug() << "Format name:" << strFormatName;

    for (int i = 0; i < static_cast<int>(av_ctx->nb_streams); i++) {
        qDebug() << "Processing stream index:" << i;
        AVStream *in_stream = av_ctx->streams[i];

        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            qDebug() << "Video stream detected.";
            bVCodec = true;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            qDebug() << "Audio stream detected.";
            bACodec = true;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            qDebug() << "Subtitle stream detected.";
            bSCodec = true;
        } else {
            qDebug() << "Other stream type detected:" << in_stream->codecpar->codec_type;
        }
    }
    qDebug() << "Finished processing all streams. bVCodec:" << bVCodec << ", bACodec:" << bACodec << ", bSCodec:" << bSCodec;

    if (bVCodec) {
        qDebug() << "Video codec found. Setting miType to Video.";
        miType = MediaType::Video;
    } else if (bACodec) {
        qDebug() << "Audio codec found. Setting miType to Audio.";
        miType = MediaType::Audio;
    } else if (bSCodec) {
        qDebug() << "Subtitle codec found. Setting miType to Subtitle.";
        miType = MediaType::Subtitle;
    } else {
        qDebug() << "No video, audio, or subtitle codecs found. Setting miType to Other.";
        miType = MediaType::Other;
    }

    if (strMimeType.contains("x-7z")) { //7z压缩包中会检测出音频流
        qDebug() << "7z archive detected, returning Other";
        miType = MediaType::Other;
    }

    if(strFormatName.contains("Tele-typewriter") || strMimeType.startsWith("image/")) {   // 排除文本文件，如果只用mimetype判断会遗漏部分原始格式文件如：h264裸流
        qDebug() << "Text file or image detected, returning Other";
        miType = MediaType::Other;
    }

    g_mvideo_avformat_close_input(&av_ctx);
    qDebug() << "Media type determined:" << (miType == MediaType::Video ? "Video" : 
                                           miType == MediaType::Audio ? "Audio" : 
                                           miType == MediaType::Subtitle ? "Subtitle" : "Other");
    return miType;
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

    if (!g_mvideo_gst_discoverer_discover_uri_async (m_pDiscoverer, uri)) {
      qInfo() << "Failed to start discovering URI " << uri;
      g_object_unref (m_pDiscoverer);
    }

    g_main_loop_run(m_pLoop);

    delete []uri;

    return m_miType;
}

bool FileFilter::isFormatSupported(const QUrl &url)
{
    AVFormatContext *av_ctx = nullptr;

    auto nRet = g_mvideo_avformat_open_input(&av_ctx, url.toLocalFile().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return false;
    }
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return false;
    }

#ifdef __sw_64__
    for (int i = 0; i < static_cast<int>(av_ctx->nb_streams); i++)
    {
        AVStream *in_stream = av_ctx->streams[i];
        if (in_stream->codecpar->codec_id == AVCodecID::AV_CODEC_ID_AV1) {
            return false;
        }
    }
#endif

    return true;
}

void FileFilter::stopThread()
{
    qDebug() << "Entering FileFilter::stopThread()";
    m_stopRunningThread = true;
    qDebug() << "m_stopRunningThread set to true. Exiting FileFilter::stopThread()";
}

void FileFilter::discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, MediaType *miType)
{
    qDebug() << "Entering FileFilter::discovered()";
    Q_UNUSED(discoverer);

    GstDiscovererResult result;
    const gchar *uri;
    bool bVideo = false;
    bool bAudio = false;
    bool bSubtitle = false;

    uri = g_mvideo_gst_discoverer_info_get_uri (info);
    result = g_mvideo_gst_discoverer_info_get_result (info);

    qDebug() << "Discoverer result:" << result;
    switch (result) {
      case GST_DISCOVERER_URI_INVALID:
        qWarning() << "Invalid URI:" << uri;
        break;
      case GST_DISCOVERER_ERROR:
        qWarning() << "Discoverer error:" << err->message;
        break;
      case GST_DISCOVERER_TIMEOUT:
        qWarning() << "Discovery timeout for URI:" << uri;
        break;
      case GST_DISCOVERER_BUSY:
        qWarning() << "Discoverer busy for URI:" << uri;
        break;
      case GST_DISCOVERER_MISSING_PLUGINS:{
        const GstStructure *s;
        gchar *str;

        s = g_mvideo_gst_discoverer_info_get_misc (info);
        str = g_mvideo_gst_structure_to_string (s);

        qWarning() << "Missing plugins for URI:" << uri << "Details:" << str;
        g_free (str);
        break;
      }
      case GST_DISCOVERER_OK:
        qDebug() << "Successfully discovered URI:" << uri;
        break;
    }

    if (result != GST_DISCOVERER_OK) {
        qWarning() << "URI cannot be played:" << uri;
        return;
    }

    GList *list;
    list = g_mvideo_gst_discoverer_info_get_video_streams(info);
    if (list) {
        bVideo = true;
        qDebug() << "Video stream found";
    }
    list = g_mvideo_gst_discoverer_info_get_audio_streams(info);
    if (list) {
        bAudio = true;
        qDebug() << "Audio stream found";
    }
    list = g_mvideo_gst_discoverer_info_get_subtitle_streams(info);
    if(list) {
        bSubtitle = true;
        qDebug() << "Subtitle stream found";
    }

    if (bVideo) {
        qDebug() << "Setting miType to Video.";
        *miType = MediaType::Video;
    } else if (bAudio) {
        qDebug() << "Setting miType to Audio.";
        *miType = MediaType::Audio;
    } else if (bSubtitle) {
        qDebug() << "Setting miType to Subtitle.";
        *miType = MediaType::Subtitle;
    } else {
        qDebug() << "Setting miType to Other.";
        *miType = MediaType::Other;
    }
    qDebug() << "Media type determined:" << (*miType == MediaType::Video ? "Video" : 
                                           *miType == MediaType::Audio ? "Audio" : 
                                           *miType == MediaType::Subtitle ? "Subtitle" : "Other");
    qDebug() << "Exiting FileFilter::discovered()";
}

void FileFilter::finished(GstDiscoverer *discoverer, GMainLoop *loop)
{
    qDebug() << "Entering FileFilter::finished()";
    Q_UNUSED(discoverer);

    g_main_loop_quit(loop);
    qDebug() << "Exiting FileFilter::finished()";
}

