/*
 * Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "dlnacontentserver.h"
#include <QtDebug>
#include <QHostAddress>
#include <QThreadPool>
#include <QTimer>

static const qint64 qlen = 2048;
const QString dlnaOrgOpFlagsSeekBytes{"DLNA.ORG_OP=01"};
const QString dlnaOrgOpFlagsNoSeek{"DLNA.ORG_OP=00"};
const QString dlnaOrgCiFlags{"DLNA.ORG_CI=0"};


DlnaContentServer::DlnaContentServer(QObject *parent, int nPort) : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<QFile>>("std::shared_ptr<QFile>");
    m_httpServer = NULL;
    m_nNum = 0;
    m_pThread = new QThread;
    moveToThread(m_pThread);
    qInfo() << "main thread:" << QThread::currentThreadId();
    connect(m_pThread, &QThread::finished, this, &QObject::deleteLater);
    connect(m_pThread, &QThread::started, [=](){
        m_bStartHttpServer = initializeHttpServer(nPort);
        if(!m_bStartHttpServer) {
            qInfo() << "http Server start failed!";
        }
    });
    m_pThread->start();
}

DlnaContentServer::~DlnaContentServer()
{
    if(m_pThread) {
        m_pThread->quit();
        m_pThread->wait();
        m_pThread->deleteLater();
        m_pThread = NULL;
    }
}

bool DlnaContentServer::initializeHttpServer(int port)
{
    qInfo() << "Worker()" << "thread:" << QThread::currentThreadId();
    bool bServer = false;
    if(!m_httpServer) {
        m_httpServer = new QHttpServer(parent());
        connect(m_httpServer, &QHttpServer::newRequest, this,
                &DlnaContentServer::requestHandler);
        bServer = m_httpServer->listen(QHostAddress::Any, port);
        if(!bServer) {
            m_httpServer->deleteLater();
            m_httpServer = NULL;
            return bServer;
        }
        connect(this, &DlnaContentServer::contSeqWriteData, this,
                &DlnaContentServer::seqWriteData, Qt::QueuedConnection);
        connect(this, &DlnaContentServer::closeServer, [=](){
            m_httpServer->close();
            m_httpServer->deleteLater();
            m_httpServer = NULL;
        });
    }
    return bServer;
}

void DlnaContentServer::requestHandler(QHttpRequest *req, QHttpResponse *resp)
{
    streamFile(m_sDlnaFileName, "", req, resp);
    connect(this, &DlnaContentServer::closeServer, [=](){
        req->end();
    });
}
void DlnaContentServer::streamFile(const QString &path, const QString &mime,
                                     QHttpRequest *req, QHttpResponse *resp) {
    auto file = std::make_shared<QFile>(path);

    if (!file->open(QFile::ReadOnly)) {
        qWarning() << "Unable to open file:" << file->fileName();
        sendEmptyResponse(resp, 500);
        return;
    }

    const auto &headers = req->headers();

    resp->setHeader("Content-Type", mime);
    resp->setHeader("Accept-Ranges", "bytes");
    resp->setHeader("Connection", "close");
    resp->setHeader("Cache-Control", "no-cache");
    resp->setHeader("TransferMode.DLNA.ORG", "Streaming");
    resp->setHeader("contentFeatures.DLNA.ORG",
                    dlnaContentFeaturesHeader(mime));

    if (headers.contains("range")) {
        streamFileRange(file, req, resp);
    } else {
        streamFileNoRange(file, req, resp);
    }
}
void DlnaContentServer::streamFileRange(std::shared_ptr<QFile> file,
                                          QHttpRequest *req,
                                          QHttpResponse *resp) {
    const auto length = file->bytesAvailable();
    const auto range = Range::fromRange(req->headers().value("range"), length);
    if (!range) {
        qWarning() << "Unable to read on invalid Range header";
        sendEmptyResponse(resp, 416);
    }

    resp->setHeader("Content-Length", QString::number(range->rangeLength()));
    resp->setHeader("Content-Range", "bytes " + QString::number(range->start) +
                                         "-" + QString::number(range->end) +
                                         "/" + QString::number(length));

    resp->writeHead(206);
    file->seek(range->start);
    seqWriteData(file, range->rangeLength(), resp);
}

void DlnaContentServer::streamFileNoRange(std::shared_ptr<QFile> file,
                                            QHttpRequest *,
                                            QHttpResponse *resp) {
    const auto length = file->bytesAvailable();

    resp->setHeader("Content-Length", QString::number(length));

    resp->writeHead(200);
    seqWriteData(file, length, resp);
}

std::optional<DlnaContentServer::Range> DlnaContentServer::Range::fromRange(
    const QString &rangeHeader, qint64 length) {
    QRegExp rx{"bytes[\\s]*=[\\s]*([\\d]+)-([\\d]*)"};

    if (rx.indexIn(rangeHeader) >= 0) {
        Range range{rx.cap(1).toLongLong(), rx.cap(2).toLongLong(), length};
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
void DlnaContentServer::seqWriteData(std::shared_ptr<QFile> file, qint64 size,
                                       QHttpResponse *resp) {
//    qInfo() << "Worker()" << "thread:" << QThread::currentThreadId();
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

    resp->end();
}

QString DlnaContentServer::dlnaContentFeaturesHeader(const QString& mime, bool seek, bool flags)
{
    QString pnFlags = dlnaOrgPnFlags(mime);
    if (pnFlags.isEmpty()) {
        if (flags)
            return QString("%1;%2;%3").arg(
                        seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags,
                        seek ? dlnaOrgFlagsForFile() : dlnaOrgFlagsForStreaming());
        else
            return QString("%1;%2").arg(seek ?
                        dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags);
    } else {
        if (flags)
            return QString("%1;%2;%3;%4").arg(
                        pnFlags, seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags,
                        seek ? dlnaOrgFlagsForFile() : dlnaOrgFlagsForStreaming());
        else
            return QString("%1;%2;%3").arg(
                        pnFlags, seek ? dlnaOrgOpFlagsSeekBytes : dlnaOrgOpFlagsNoSeek,
                        dlnaOrgCiFlags);
    }
}
QString DlnaContentServer::dlnaOrgFlagsForFile()
{
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
QString DlnaContentServer::dlnaOrgFlagsForStreaming()
{
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

void DlnaContentServer::setBaseUrl(const QString &baseUrl)
{
    m_sBaseUrl = baseUrl;
}

void DlnaContentServer::setDlnaFileName(const QString &fileName)
{
    m_sDlnaFileName = fileName;
}

QString DlnaContentServer::getBaseUrl() const
{
    return m_sBaseUrl;
}


bool DlnaContentServer::getIsStartHttpServer()
{
    return m_bStartHttpServer;
}

QString DlnaContentServer::dlnaOrgPnFlags(const QString &mime)
{
    if (mime.contains("video/x-msvideo", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=AVI";
    /*if (mime.contains(image/jpeg"))
        return "DLNA.ORG_PN=JPEG_LRG";*/
    if (mime.contains("audio/aac", Qt::CaseInsensitive) ||
            mime.contains("audio/aacp", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=AAC";
    if (mime.contains("audio/mpeg", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=MP3";
    if (mime.contains("audio/vnd.wav", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=LPCM";
    if (mime.contains("audio/L16", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=LPCM";
    if (mime.contains("video/x-matroska", Qt::CaseInsensitive))
        return "DLNA.ORG_PN=MKV";
    return QString();
}

void DlnaContentServer::sendEmptyResponse(QHttpResponse *resp, int code) {
    qDebug() << "sendEmptyResponse:" << resp << code;
    resp->setHeader("Content-Length", "0");
    resp->writeHead(code);
    resp->end();
}
