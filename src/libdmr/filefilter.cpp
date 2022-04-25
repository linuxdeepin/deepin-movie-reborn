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

#include <QMimeDatabase>
#include <QMimeType>

FileFilter* FileFilter::m_pFileFilter = nullptr;

FileFilter::FileFilter()
{
    m_stopRunningThread = false;
    QLibrary avformatLibrary(libPath("libavformat.so"));

    g_mvideo_avformat_open_input = (mvideo_avformat_open_input) avformatLibrary.resolve("avformat_open_input");
    g_mvideo_avformat_find_stream_info = (mvideo_avformat_find_stream_info) avformatLibrary.resolve("avformat_find_stream_info");
    g_mvideo_avformat_close_input = (mvideo_avformat_close_input) avformatLibrary.resolve("avformat_close_input");
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

    Q_ASSERT(list.size() > 0);
    return list.last();
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
    int nRet;
    QString strFormatName;
    int nStreamNum = 0;
    bool bMedia = false;
    QMimeDatabase db;
    QString strMimeType;

    strMimeType = db.mimeTypeForUrl(url).name();

    AVFormatContext *av_ctx = nullptr;

    if(!url.isLocalFile()) {   // url 文件不做判断,默认可以播放
        return true;
    }

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toString().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return false;
    }

    if(av_ctx->probe_score <= AVPROBE_SCORE_RETRY)  // format 匹配度不高
    {
        return false;
    }

    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return false;
    }

    nStreamNum = static_cast<int>(av_ctx->nb_streams);
    strFormatName = av_ctx->iformat->long_name;

    if(nStreamNum > 0 && !strFormatName.contains("Tele-typewriter") && !strFormatName.contains("subtitle")
            && !strMimeType.startsWith("image/"))       // 排除文本文件，如果只用mimetype判断会遗漏部分原始格式文件如：h264裸流
        bMedia = true;

    g_mvideo_avformat_close_input(&av_ctx);

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
    if(m_mapCheckAudio.contains(url)) {
        return m_mapCheckAudio.value(url);
    } else {
        m_mapCheckAudio.clear();
    }
    int nRet;
    QString strFormatName;
    bool bAudio = false;
    m_mapCheckAudio[url] = bAudio;

    AVFormatContext *av_ctx = nullptr;

    if(!url.isLocalFile()) {   // url 文件不做判断
        return false;
    }

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toString().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return false;
    }
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return false;
    }

    strFormatName = av_ctx->iformat->long_name;

    if (av_ctx->nb_streams == 1)
    {
         AVStream *in_stream = av_ctx->streams[0];

         if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
         {
             bAudio = true;
         }
    }
    else
    {
        if (strFormatName.contains("audio"))
            bAudio = true;
    }

    g_mvideo_avformat_close_input(&av_ctx);
    m_mapCheckAudio[url] = bAudio;
    return bAudio;
}

bool FileFilter::isSubtitle(QUrl url)
{
    int nRet;
    QString strFormatName;
    bool bSubtitle = false;

    AVFormatContext *av_ctx = nullptr;

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toString().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return false;
    }
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return false;
    }

    strFormatName = av_ctx->iformat->long_name;

    if(strFormatName.contains("subtitle"))
        bSubtitle = true;

    g_mvideo_avformat_close_input(&av_ctx);

    return bSubtitle;
}

bool FileFilter::isVideo(QUrl url)
{
    int nRet;
    QString strFormatName;
    bool bVideo = false;
    int nWidth = 0;
    int nHeight = 0;

    AVFormatContext *av_ctx = nullptr;

    nRet = g_mvideo_avformat_open_input(&av_ctx, url.toString().toUtf8().constData(), nullptr, nullptr);

    if(nRet < 0)
    {
        return false;
    }
    if(g_mvideo_avformat_find_stream_info(av_ctx, nullptr) < 0)
    {
        return false;
    }

    strFormatName = av_ctx->iformat->long_name;

    for (int i = 0; i < static_cast<int>(av_ctx->nb_streams); i++)
     {
         AVStream *in_stream = av_ctx->streams[i];

         if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
             nWidth = in_stream->codec->width;
             nHeight = in_stream->codec->height;
         }
    }

    if(!strFormatName.contains("audio") && nWidth > 0 && nHeight > 0)
           bVideo = true;

    g_mvideo_avformat_close_input(&av_ctx);

    return bVideo;
}


void FileFilter::stopThread()
{
    m_stopRunningThread = true;
}

