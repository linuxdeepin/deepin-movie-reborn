// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filefilter.h"
#include "ddlog.h"
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
    qCDebug(dmrLib) << "Initializing FileFilter";
    m_bMpvExists = dmr::CompositingManager::isMpvExists();
    qCInfo(dmrLib) << "MPV exists:" << m_bMpvExists;
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
        qCCritical(dmrLib) << "Failed to create discoverer instance:" << pGErr->message;
        g_clear_error (&pGErr);
    }

    g_signal_connect_data(m_pDiscoverer, "discovered", (GCallback)(discovered), &m_miType, nullptr, GConnectFlags(0));
    g_signal_connect_data(m_pDiscoverer, "finished",  (GCallback)(finished), m_pLoop, nullptr, GConnectFlags(0));

    g_mvideo_gst_discoverer_start(m_pDiscoverer);
    qCDebug(dmrLib) << "FileFilter initialization completed";
}

FileFilter::~FileFilter()
{
    qCDebug(dmrLib) << "Destroying FileFilter";
    g_mvideo_gst_discoverer_stop(m_pDiscoverer);
    g_object_unref(m_pDiscoverer);
    g_main_loop_unref(m_pLoop);
}

FileFilter *FileFilter::instance()
{
    if (nullptr == m_pFileFilter) {
        qCDebug(dmrLib) << "Creating new FileFilter instance";
        m_pFileFilter = new FileFilter();
    }
    return m_pFileFilter;
}

bool FileFilter::isMediaFile(QUrl url)
{
    qCDebug(dmrLib) << "Checking if file is media:" << url.toString();
    MediaType miType;
    bool bMedia = false;

    if(!url.isLocalFile()) {   // url 文件不做判断,默认可以播放
        qCInfo(dmrLib) << "Non-local URL, assuming media file";
        return true;
    }

    qCDebug(dmrLib) << "URL is local. Determining media type.";
    if (m_bMpvExists) {
        qCDebug(dmrLib) << "MPV exists. Judging type by FFmpeg.";
        miType = typeJudgeByFFmpeg(url);
    } else {
        qCDebug(dmrLib) << "MPV does not exist. Judging type by Gst.";
        miType = typeJudgeByGst(url);
    }

    if (miType == MediaType::Audio || miType == MediaType::Video) {
        qCDebug(dmrLib) << "Media type is Audio or Video. Setting bMedia to true.";
        bMedia = true;
    }

    qCDebug(dmrLib) << "Media type:" << (bMedia ? "Media" : "Not media");
    return bMedia;
}

QList<QUrl> FileFilter::filterDir(QDir dir)
{
    qCDebug(dmrLib) << "Filtering directory:" << dir.absolutePath();
    QList<QUrl> lstUrl;
    QDir di(dir);
    qCDebug(dmrLib) << "QDir object created for:" << di.absolutePath();

    di.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    qCDebug(dmrLib) << "Directory filter set.";

    for (QFileInfo fileInfo : di.entryInfoList()) {
        qCDebug(dmrLib) << "Processing entry:" << fileInfo.absoluteFilePath();
        if (fileInfo.isFile()) {
            qCDebug(dmrLib) << "Entry is a file. Transferring file:" << fileInfo.filePath();
            lstUrl.append(fileTransfer(fileInfo.filePath()));
            qCDebug(dmrLib) << "File transferred and added to list. Current list size:" << lstUrl.size();
        } else if (fileInfo.isDir()) {
            qCDebug(dmrLib) << "Entry is a directory:" << fileInfo.absoluteFilePath();
            if (m_stopRunningThread) {
                qCDebug(dmrLib) << "Thread stopped, returning empty list";
                return QList<QUrl>();
            }
            lstUrl << filterDir(fileInfo.absoluteFilePath());
        }
    }
    qCDebug(dmrLib) << "Found" << lstUrl.size() << "files in directory";
    return lstUrl;
}

// file:// URLs must not be parsed with QUrl(str): '#' would be treated as a fragment
// delimiter, silently truncating paths that contain '#' in the filename (legal on Linux).
// Strip the scheme prefix and percent-decode instead, so QUrl::fromLocalFile() always
// receives a plain filesystem path and encodes it correctly.
static const QString s_fileSchemePrefix = QStringLiteral("file://");

QUrl FileFilter::fileTransfer(QString strFile)
{
    qCDebug(dmrLib) << "Entering FileFilter::fileTransfer() with strFile:" << strFile;
    QUrl realUrl;

    if (strFile.startsWith(s_fileSchemePrefix)) {
        strFile = QUrl::fromPercentEncoding(strFile.mid(s_fileSchemePrefix.size()).toUtf8());
        qCDebug(dmrLib) << "Extracted path from file URL:" << strFile;
    }
    // Without "file://" prefix, strFile is already a filesystem path; do not URL-decode it.

    QFileInfo fileInfo(strFile);
    if (fileInfo.isFile() || fileInfo.isDir()) {
        // canonicalFilePath() resolves all symlinks in one step and returns empty string
        // for broken or circular symlinks, avoiding an infinite loop.
        const QString canonical = fileInfo.canonicalFilePath();
        realUrl = QUrl::fromLocalFile(canonical.isEmpty() ? strFile : canonical);
        qCDebug(dmrLib) << "realUrl set from local file:" << realUrl.toString();
    } else {
        // Non-existent local path or a non-file:// network URL (http://, rtmp://, etc.)
        realUrl = QUrl(strFile);
        qCDebug(dmrLib) << "realUrl set from raw string:" << realUrl.toString();
    }

    qCDebug(dmrLib) << "Exiting FileFilter::fileTransfer() with realUrl:" << realUrl.toString();
    return realUrl;
}

bool FileFilter::isAudio(QUrl url)
{
    qCDebug(dmrLib) << "Entering FileFilter::isAudio() with URL:" << url.toString();
    bool bAudio = false;

    if(m_mapCheckAudio.contains(url)) {
        qCDebug(dmrLib) << "m_mapCheckAudio contains URL. Returning cached value.";
        bool result = m_mapCheckAudio.value(url);
        qCDebug(dmrLib) << "Exiting FileFilter::isAudio() (cached) with result:" << result;
        return result;
    } else {
        qCDebug(dmrLib) << "m_mapCheckAudio does not contain URL. Clearing map.";
        m_mapCheckAudio.clear();
    }

    if (m_bMpvExists) {
        qCDebug(dmrLib) << "MPV exists. Judging type by FFmpeg.";
        bAudio = typeJudgeByFFmpeg(url) == MediaType::Audio ? true : false;
    } else {
        qCDebug(dmrLib) << "MPV does not exist. Judging type by Gst.";
        bAudio = typeJudgeByGst(url) == MediaType::Audio ? true : false;
    }

    qCDebug(dmrLib) << "Storing result in m_mapCheckAudio. bAudio:" << bAudio;
    m_mapCheckAudio[url] = bAudio;

    qCDebug(dmrLib) << "Exiting FileFilter::isAudio() with result:" << bAudio;
    return bAudio;
}

bool FileFilter::isSubtitle(QUrl url)
{
    qCDebug(dmrLib) << "Entering FileFilter::isSubtitle() with URL:" << url.toString();
    bool result;
    if (m_bMpvExists) {
        qCDebug(dmrLib) << "MPV exists. Judging type by FFmpeg.";
        result = typeJudgeByFFmpeg(url) == MediaType::Subtitle ? true : false;
    } else {
        qCDebug(dmrLib) << "MPV does not exist. Judging type by Gst.";
        result = typeJudgeByGst(url) == MediaType::Subtitle ? true : false;
    }
    qCDebug(dmrLib) << "Exiting FileFilter::isSubtitle() with result:" << result;
    return result;
}

bool FileFilter::isVideo(QUrl url)
{
    qCDebug(dmrLib) << "Entering FileFilter::isVideo() with URL:" << url.toString();
    bool result;
    if (m_bMpvExists) {
        qCDebug(dmrLib) << "MPV exists. Judging type by FFmpeg.";
        result = typeJudgeByFFmpeg(url) == MediaType::Video ? true : false;
    } else {
        qCDebug(dmrLib) << "MPV does not exist. Judging type by Gst.";
        result = typeJudgeByGst(url) == MediaType::Video ? true : false;
    }
    qCDebug(dmrLib) << "Exiting FileFilter::isVideo() with result:" << result;
    return result;
}

FileFilter::MediaType FileFilter::typeJudgeByFFmpeg(const QUrl &url)
{
    qCDebug(dmrLib) << "Judging media type by FFmpeg:" << url.toString();
    int nRet;
    QString strFormatName;
    bool bVCodec = false;
    bool bACodec = false;
    bool bSCodec = false;
    MediaType miType = MediaType::Other;

    QString strMimeType = m_mimeDB.mimeTypeForUrl(url).name();
    qCDebug(dmrLib) << "MIME type:" << strMimeType;

    if (strMimeType.contains("mpegurl")) {
        qCDebug(dmrLib) << "Mpegurl format detected, returning Other";
        return MediaType::Other;
    }

    AVFormatContext *av_ctx = nullptr;

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toLocalFile().toUtf8().constData(), nullptr, nullptr);
    qCDebug(dmrLib) << "avformat_open_input returned:" << nRet;

    if(nRet < 0) {
        qCWarning(dmrLib) << "Failed to open input file:" << url.toString();
        return MediaType::Other;
    }
    qCDebug(dmrLib) << "Input file opened successfully.";
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        qCDebug(dmrLib) << "Failed to find stream info. Returning MediaType::Other.";
        return MediaType::Other;
    }
    qCDebug(dmrLib) << "Stream info found successfully.";

    strFormatName = av_ctx->iformat->long_name;
    qCDebug(dmrLib) << "Format name:" << strFormatName;

    for (int i = 0; i < static_cast<int>(av_ctx->nb_streams); i++) {
        qCDebug(dmrLib) << "Processing stream index:" << i;
        AVStream *in_stream = av_ctx->streams[i];

        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            qCDebug(dmrLib) << "Video stream detected.";
            bVCodec = true;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            qCDebug(dmrLib) << "Audio stream detected.";
            bACodec = true;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            qCDebug(dmrLib) << "Subtitle stream detected.";
            bSCodec = true;
        } else {
            qCDebug(dmrLib) << "Other stream type detected:" << in_stream->codecpar->codec_type;
        }
    }
    qCDebug(dmrLib) << "Finished processing all streams. bVCodec:" << bVCodec << ", bACodec:" << bACodec << ", bSCodec:" << bSCodec;

    if (bVCodec) {
        qCDebug(dmrLib) << "Video codec found. Setting miType to Video.";
        miType = MediaType::Video;
    } else if (bACodec) {
        qCDebug(dmrLib) << "Audio codec found. Setting miType to Audio.";
        miType = MediaType::Audio;
    } else if (bSCodec) {
        qCDebug(dmrLib) << "Subtitle codec found. Setting miType to Subtitle.";
        miType = MediaType::Subtitle;
    } else {
        qCDebug(dmrLib) << "No video, audio, or subtitle codecs found. Setting miType to Other.";
        miType = MediaType::Other;
    }

    if (strMimeType.contains("x-7z")) { //7z压缩包中会检测出音频流
        qCDebug(dmrLib) << "7z archive detected, returning Other";
        miType = MediaType::Other;
    }

    if(strFormatName.contains("Tele-typewriter") || strMimeType.startsWith("image/")) {   // 排除文本文件，如果只用mimetype判断会遗漏部分原始格式文件如：h264裸流
        qCDebug(dmrLib) << "Text file or image detected, returning Other";
        miType = MediaType::Other;
    }

    g_mvideo_avformat_close_input(&av_ctx);
    qCDebug(dmrLib) << "Media type determined:" << (miType == MediaType::Video ? "Video" : 
                                           miType == MediaType::Audio ? "Audio" : 
                                           miType == MediaType::Subtitle ? "Subtitle" : "Other");
    return miType;
}

FileFilter::MediaType FileFilter::typeJudgeByGst(const QUrl &url)
{
    m_miType = MediaType::Other;

    QString strMimeType = m_mimeDB.mimeTypeForUrl(url).name();

    if (!strMimeType.startsWith("audio/") && !strMimeType.startsWith("video/")) {
        return MediaType::Other;
    }

    QByteArray urlBytes = url.toString().toUtf8();
    int urlLen = urlBytes.size();
    char *uri = new char[urlLen + 1];
    memcpy(uri, urlBytes.constData(), urlLen);
    uri[urlLen] = '\0';

    if (!g_mvideo_gst_discoverer_discover_uri_async (m_pDiscoverer, uri)) {
      qCInfo(dmrLib) << "Failed to start discovering URI " << uri;
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
    qCDebug(dmrLib) << "Entering FileFilter::stopThread()";
    m_stopRunningThread = true;
    qCDebug(dmrLib) << "m_stopRunningThread set to true. Exiting FileFilter::stopThread()";
}

void FileFilter::discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, MediaType *miType)
{
    qCDebug(dmrLib) << "Entering FileFilter::discovered()";
    Q_UNUSED(discoverer);

    GstDiscovererResult result;
    const gchar *uri;
    bool bVideo = false;
    bool bAudio = false;
    bool bSubtitle = false;

    uri = g_mvideo_gst_discoverer_info_get_uri (info);
    result = g_mvideo_gst_discoverer_info_get_result (info);

    qCDebug(dmrLib) << "Discoverer result:" << result;
    switch (result) {
      case GST_DISCOVERER_URI_INVALID:
        qCWarning(dmrLib) << "Invalid URI:" << uri;
        break;
      case GST_DISCOVERER_ERROR:
        qCWarning(dmrLib) << "Discoverer error:" << err->message;
        break;
      case GST_DISCOVERER_TIMEOUT:
        qCWarning(dmrLib) << "Discovery timeout for URI:" << uri;
        break;
      case GST_DISCOVERER_BUSY:
        qCWarning(dmrLib) << "Discoverer busy for URI:" << uri;
        break;
      case GST_DISCOVERER_MISSING_PLUGINS:{
        const GstStructure *s;
        gchar *str;

        s = g_mvideo_gst_discoverer_info_get_misc (info);
        str = g_mvideo_gst_structure_to_string (s);

        qCWarning(dmrLib) << "Missing plugins for URI:" << uri << "Details:" << str;
        g_free (str);
        break;
      }
      case GST_DISCOVERER_OK:
        qCDebug(dmrLib) << "Successfully discovered URI:" << uri;
        break;
    }

    if (result != GST_DISCOVERER_OK) {
        qCWarning(dmrLib) << "URI cannot be played:" << uri;
        return;
    }

    GList *list;
    list = g_mvideo_gst_discoverer_info_get_video_streams(info);
    if (list) {
        bVideo = true;
        qCDebug(dmrLib) << "Video stream found";
    }
    list = g_mvideo_gst_discoverer_info_get_audio_streams(info);
    if (list) {
        bAudio = true;
        qCDebug(dmrLib) << "Audio stream found";
    }
    list = g_mvideo_gst_discoverer_info_get_subtitle_streams(info);
    if(list) {
        bSubtitle = true;
        qCDebug(dmrLib) << "Subtitle stream found";
    }

    if (bVideo) {
        qCDebug(dmrLib) << "Setting miType to Video.";
        *miType = MediaType::Video;
    } else if (bAudio) {
        qCDebug(dmrLib) << "Setting miType to Audio.";
        *miType = MediaType::Audio;
    } else if (bSubtitle) {
        qCDebug(dmrLib) << "Setting miType to Subtitle.";
        *miType = MediaType::Subtitle;
    } else {
        qCDebug(dmrLib) << "Setting miType to Other.";
        *miType = MediaType::Other;
    }
    qCDebug(dmrLib) << "Media type determined:" << (*miType == MediaType::Video ? "Video" : 
                                           *miType == MediaType::Audio ? "Audio" : 
                                           *miType == MediaType::Subtitle ? "Subtitle" : "Other");
    qCDebug(dmrLib) << "Exiting FileFilter::discovered()";
}

void FileFilter::finished(GstDiscoverer *discoverer, GMainLoop *loop)
{
    qCDebug(dmrLib) << "Entering FileFilter::finished()";
    Q_UNUSED(discoverer);

    g_main_loop_quit(loop);
    qCDebug(dmrLib) << "Exiting FileFilter::finished()";
}

