// Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dlnacontentserver.h"
#include <QtDebug>
#include <QHostAddress>
#include <QThreadPool>
#include <QTimer>
#include <QRegularExpression>

static const qint64 qlen = 2048;
const QString dlnaOrgOpFlagsSeekBytes{"DLNA.ORG_OP=01"};
const QString dlnaOrgOpFlagsNoSeek{"DLNA.ORG_OP=00"};
const QString dlnaOrgCiFlags{"DLNA.ORG_CI=0"};


DlnaContentServer::DlnaContentServer(QObject *parent, int nPort) : QObject(parent)
{
    qDebug() << "Entering DlnaContentServer constructor. Port:" << nPort;
    qRegisterMetaType<std::shared_ptr<QFile>>("std::shared_ptr<QFile>");
    m_httpServer = NULL;
    m_pThread = new QThread;
    moveToThread(m_pThread);
    qDebug() << "Creating DLNA content server on port" << nPort;
    connect(m_pThread, &QThread::finished, this, &QObject::deleteLater);
    connect(m_pThread, &QThread::started, [=](){
        qDebug() << "Thread started. Initializing HTTP server.";
        m_bStartHttpServer = initializeHttpServer(nPort);
        if(!m_bStartHttpServer) {
            qWarning() << "HTTP server failed to start on port" << nPort;
        } else {
            qDebug() << "HTTP server started successfully on port" << nPort;
        }
        qDebug() << "HTTP server initialization complete.";
    });
    m_pThread->start();
    qDebug() << "Thread started.";
    qDebug() << "Exiting DlnaContentServer constructor.";
}

DlnaContentServer::~DlnaContentServer()
{
    qDebug() << "Entering DlnaContentServer destructor.";
    qDebug() << "Shutting down DLNA content server";
    if(m_pThread) {
        qDebug() << "Quitting and waiting for thread to finish.";
        m_pThread->quit();
        m_pThread->wait();
        m_pThread->deleteLater();
        m_pThread = NULL;
        qDebug() << "Thread cleaned up.";
    } else {
        qDebug() << "m_pThread is null, no cleanup needed.";
    }
    qDebug() << "Exiting DlnaContentServer destructor.";
}
/**
 * @brief initializeHttpServer 初始化HttpServer
 * @param port Http服务端口
 */
bool DlnaContentServer::initializeHttpServer(int port)
{
    qDebug() << "Initializing HTTP server on port" << port;
    bool bServer = false;
    if(!m_httpServer) {
        m_httpServer = new QHttpServer(parent());
        connect(m_httpServer, &QHttpServer::newRequest, this,
                &DlnaContentServer::requestHandler);
        bServer = m_httpServer->listen(QHostAddress::Any, port);
        if(!bServer) {
            qWarning() << "Failed to start HTTP server on port" << port;
            m_httpServer->deleteLater();
            m_httpServer = NULL;
            return bServer;
        }
        connect(this, &DlnaContentServer::contSeqWriteData, this,
                &DlnaContentServer::seqWriteData, Qt::QueuedConnection);
        connect(this, &DlnaContentServer::closeServer, [=](){
            qDebug() << "Closing HTTP server";
            m_httpServer->close();
            m_httpServer->deleteLater();
            m_httpServer = NULL;
        });
    }
    return bServer;
}
/**
 * @brief slotBaseMuteChanged 请求传输文件数据
 * @param req Http请求
 * @param resp Http应答
 */
void DlnaContentServer::requestHandler(QHttpRequest *req, QHttpResponse *resp)
{
    qDebug() << "Received request for file:" << m_sDlnaFileName;
    streamFile(m_sDlnaFileName, "", req, resp);
    connect(this, &DlnaContentServer::closeServer, [=](){
        qDebug() << "Ending request due to server close";
        req->end();
    });
}
/**
 * @brief streamFile 传输文件流数据
 * @param file Http传输文件
 * @param mime 视频格式
 * @param req Http请求
 * @param resp Http应答
 */
void DlnaContentServer::streamFile(const QString &path, const QString &mime,
                                     QHttpRequest *req, QHttpResponse *resp) {
    qDebug() << "Entering streamFile function. Path:" << path << "Mime:" << mime;
    if(!req || !resp) {
        qDebug() << "Invalid request or response";
        return;
    }
    qDebug() << "Streaming file:" << path << "with mime type:" << mime;
    
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QFile::ReadOnly)) {
        qWarning() << "Failed to open file:" << path << "-" << file->errorString();
        sendEmptyResponse(resp, 500);
        return;
    }

    const auto &headers = req->headers();
    qDebug() << "Request headers:" << headers;

    resp->setHeader("Content-Type", mime);
    resp->setHeader("Accept-Ranges", "bytes");
    resp->setHeader("Connection", "close");
    resp->setHeader("Cache-Control", "no-cache");
    resp->setHeader("TransferMode.DLNA.ORG", "Streaming");
    resp->setHeader("contentFeatures.DLNA.ORG",
                    dlnaContentFeaturesHeader(mime));

    if (headers.contains("range")) {
        qDebug() << "Range request detected:" << headers.value("range");
        streamFileRange(file, req, resp);
    } else {
        qDebug() << "Full file request";
        streamFileNoRange(file, req, resp);
    }
}
/**
 * @brief streamFileRange 断点续传流
 * @param file Http传输文件
 * @param req Http请求
 * @param resp Http应答
 */
void DlnaContentServer::streamFileRange(std::shared_ptr<QFile> file,
                                          QHttpRequest *req,
                                          QHttpResponse *resp) {
    qDebug() << "Entering streamFileRange function";
    if(!req || !resp) {
        qDebug() << "Invalid request or response";
        return;
    }
    const auto length = file->bytesAvailable();
    const auto range = Range::fromRange(req->headers().value("range"), length);
    if (!range) {
        qWarning() << "Invalid range request:" << req->headers().value("range");
        sendEmptyResponse(resp, 416);
        return;
    }

    qDebug() << "Streaming range:" << range->start << "-" << range->end << "of" << length << "bytes";
    resp->setHeader("Content-Length", QString::number(range->rangeLength()));
    resp->setHeader("Content-Range", "bytes " + QString::number(range->start) +
                                         "-" + QString::number(range->end) +
                                         "/" + QString::number(length));

    resp->writeHead(206);
    file->seek(range->start);
    seqWriteData(file, range->rangeLength(), resp);
}
/**
 * @brief streamFileNoRange 全部流
 * @param file Http传输文件
 * @param req Http请求
 * @param resp Http应答
 */
void DlnaContentServer::streamFileNoRange(std::shared_ptr<QFile> file,
                                            QHttpRequest *req,
                                            QHttpResponse *resp) {
    qDebug() << "Entering streamFileNoRange function";
    if(!req || !resp) {
        qDebug() << "Invalid request or response";
        return;
    }
    const auto length = file->bytesAvailable();
    qDebug() << "Streaming full file of" << length << "bytes";

    resp->setHeader("Content-Length", QString::number(length));
    resp->writeHead(200);
    seqWriteData(file, length, resp);
}

std::optional<DlnaContentServer::Range> DlnaContentServer::Range::fromRange(
    const QString &rangeHeader, qint64 length) {
    qDebug() << "Entering Range::fromRange function";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRegExp rx{"bytes[\\s]*=[\\s]*([\\d]+)-([\\d]*)"};
    if (rx.indexIn(rangeHeader) >= 0) {
        Range range{rx.cap(1).toLongLong(), rx.cap(2).toLongLong(), length};
#else
    QRegularExpression rx{"bytes\\s*=\\s*([\\d]+)-([\\d]*)"};
    QRegularExpressionMatch match = rx.match(rangeHeader);
    if (match.hasMatch()) {
        Range range{match.captured(1).toLongLong(), match.captured(2).toLongLong(), length};
#endif
        if (range.length <= 0) range.length = -1;
        if (range.end <= 0) range.end = -1;
        if (length > 0) {
            if (range.end < 0) range.end = length - 1;
            if (range.start < length - 1 && range.end < length &&
                range.end > range.start && range.end > 0 && range.start >= 0) {
                return range;
            }
        } else {
            if ((range.end == -1 ||
                 (range.end > range.start && range.end > 0)) &&
                range.start >= 0) {
                return range;
            }
        }
    }

    qWarning() << "Invalid Range:" << rangeHeader;
    return std::nullopt;
}
/**
 * @brief seqWriteData 请求传输文件数据
 * @param file Http请求文件
 * @param size Http请求文件大小
 * @param resp Http应答
 */
void DlnaContentServer::seqWriteData(std::shared_ptr<QFile> file, qint64 size,
                                       QHttpResponse *resp) {
    qDebug() << "Entering seqWriteData function";
    if(!resp) {
        qDebug() << "Invalid response";
        return;
    }
    if (resp->isFinished()) {
        qWarning() << "Connection closed by server, so skiping data sending";
    } else {
        qint64 rlen = size;
        const qint64 len =
            rlen < qlen ? rlen : qlen;
        // qDebug() << "Sending" << len << "of data";
        QByteArray data;
        data.resize(static_cast<int>(len));
        auto cdata = data.data();
        const auto count = static_cast<int>(file->read(cdata, len));
        rlen = rlen - len;

        if (count > 0) {
            resp->write(data);
            resp->waitForBytesWritten(1000);
            if (rlen > 0) {
                emit contSeqWriteData(file, rlen, resp);
                return;
            }
        } else {
            qWarning() << "No more data to read";
        }

        qDebug() << "All data sent, so ending connection";
    }

    qDebug() << "File streaming completed";
    resp->end();
}
/**
 * @brief dlnaContentFeaturesHeader 填充dlna传输头
 * @param seek 是否seek传输
 * @param flags 文件或流标志
 */
QString DlnaContentServer::dlnaContentFeaturesHeader(const QString& mime, bool seek, bool flags)
{
    qDebug() << "Entering dlnaContentFeaturesHeader function";
    QString pnFlags = dlnaOrgPnFlags(mime);
    QString header;
    if (pnFlags.isEmpty()) {
        qDebug() << "pnFlags is empty";
        if (flags) {
            qDebug() << "flags is true";
            header = QString("%1;%2;%3").arg(
                        seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags,
                        seek ? dlnaOrgFlagsForFile() : dlnaOrgFlagsForStreaming());
        } else {
            qDebug() << "flags is false";
            header = QString("%1;%2").arg(seek ?
                        dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags);
        }
    } else {
        qDebug() << "pnFlags is not empty";
        if (flags) {
            qDebug() << "flags is true";
            header = QString("%1;%2;%3;%4").arg(
                        pnFlags, seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags,
                        seek ? dlnaOrgFlagsForFile() : dlnaOrgFlagsForStreaming());
        } else {
            qDebug() << "flags is false";
            header = QString("%1;%2;%3").arg(
                        pnFlags, seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags);
        }
    }
    qDebug() << "Generated DLNA content features header:" << header;
    return header;
}
/**
 * @brief dlnaOrgFlagsForFile dlna文件传输协议
 */
QString DlnaContentServer::dlnaOrgFlagsForFile()
{
    qDebug() << "Entering dlnaOrgFlagsForFile function";
    char flags[448];
    sprintf(flags, "%s=%.8x%.24x", "DLNA.ORG_FLAGS",
            DLNA_ORG_FLAG_BYTE_BASED_SEEK |
            DLNA_ORG_FLAG_STREAMING_TRANSFER_MODE |
            DLNA_ORG_FLAG_BACKGROUND_TRANSFER_MODE |
            DLNA_ORG_FLAG_CONNECTION_STALL |
            DLNA_ORG_FLAG_DLNA_V15, 0);
    QString f(flags);
    qDebug() << f;
    return f;
}
/**
 * @brief dlnaOrgFlagsForStreaming dlna流传输协议
 */
QString DlnaContentServer::dlnaOrgFlagsForStreaming()
{
    qDebug() << "Entering dlnaOrgFlagsForStreaming function";
    char flags[448];
    sprintf(flags, "%s=%.8x%.24x", "DLNA.ORG_FLAGS",
            DLNA_ORG_FLAG_S0_INCREASE |
            DLNA_ORG_FLAG_SN_INCREASE |
            DLNA_ORG_FLAG_STREAMING_TRANSFER_MODE |
            DLNA_ORG_FLAG_CONNECTION_STALL |
            DLNA_ORG_FLAG_DLNA_V15, 0);
    QString f(flags);
    qDebug() << f;
    return f;
}
/**
 * @brief setBaseUrl 设置Http视频连接地址
 * @param baseUrl Http视频连接地址
 */
void DlnaContentServer::setBaseUrl(const QString &baseUrl)
{
    qDebug() << "Entering setBaseUrl function";
    m_sBaseUrl = baseUrl;
}
/**
 * @brief setDlnaFileName 设置传输文件名
 * @param fileName 传输文件名
 */
void DlnaContentServer::setDlnaFileName(const QString &fileName)
{
    qDebug() << "Entering setDlnaFileName function";
    m_sDlnaFileName = fileName;
}
/**
 * @brief getBaseUrl 获取Http视频连接地址
 */
QString DlnaContentServer::getBaseUrl() const
{
    qDebug() << "Entering getBaseUrl:" << m_sBaseUrl;
    return m_sBaseUrl;
}
/**
 * @brief getIsStartHttpServer Http服务是否启动
 */
bool DlnaContentServer::getIsStartHttpServer()
{
    qDebug() << "Entering getIsStartHttpServer:" << m_bStartHttpServer;
    return m_bStartHttpServer;
}
/**
 * @brief dlnaOrgPnFlags 视频格式转换为upnp标准
 * @param mime 视频格式
 */
QString DlnaContentServer::dlnaOrgPnFlags(const QString &mime)
{
    qDebug() << "Entering dlnaOrgPnFlags function";
    if (mime.contains("video/x-msvideo", Qt::CaseInsensitive)) {
        qDebug() << "mime contains video/x-msvideo";
        return "DLNA.ORG_PN=AVI";
    }
    /*if (mime.contains(image/jpeg"))
        return "DLNA.ORG_PN=JPEG_LRG";*/
    if (mime.contains("audio/aac", Qt::CaseInsensitive) ||
            mime.contains("audio/aacp", Qt::CaseInsensitive)) {
        qDebug() << "mime contains audio/aac or audio/aacp";
        return "DLNA.ORG_PN=AAC";
    }
    if (mime.contains("audio/mpeg", Qt::CaseInsensitive)) {
        qDebug() << "mime contains audio/mpeg";
        return "DLNA.ORG_PN=MP3";
    }
    if (mime.contains("audio/vnd.wav", Qt::CaseInsensitive)) {
        qDebug() << "mime contains audio/vnd.wav";
        return "DLNA.ORG_PN=LPCM";
    }
    if (mime.contains("audio/L16", Qt::CaseInsensitive)) {
        qDebug() << "mime contains audio/L16";
        return "DLNA.ORG_PN=LPCM";
    }
    if (mime.contains("video/x-matroska", Qt::CaseInsensitive)) {
        qDebug() << "mime contains video/x-matroska";
        return "DLNA.ORG_PN=MKV";
    }
    qDebug() << "mime does not contain any of the above";
    return QString();
}
/**
 * @brief sendEmptyResponse 发送空应答
 * @param resp Http应答
 * @param code Http应答码
 */
void DlnaContentServer::sendEmptyResponse(QHttpResponse *resp, int code) {
    qDebug() << "Entering sendEmptyResponse function";
    if(!resp) {
        qDebug() << "Invalid response";
        return;
    }
    qDebug() << "Sending empty response with code:" << code;
    resp->setHeader("Content-Length", "0");
    resp->writeHead(code);
    resp->end();
}
