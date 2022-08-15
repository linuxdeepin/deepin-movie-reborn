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
#ifndef DLNACONTENTSERVER_H
#define DLNACONTENTSERVER_H

#include <QObject>
#include <QFile>
#include <dlna/dlnaHttpServer/qhttpserver.h>
#include <dlna/dlnaHttpServer/qhttpresponse.h>
#include <dlna/dlnaHttpServer/qhttprequest.h>
#include <memory>
#include <optional>
#include <QThread>

class QHttpServer;
class QHttpRequest;
class QHttpResponse;
class DlnaContentServer : public QObject
{
    Q_OBJECT
public:
    struct Range {
        qint64 start;
        qint64 end;
        qint64 length;
        inline bool full() const {
            return start == 0 && (end == -1 || end == length - 1);
        }
        inline qint64 rangeLength() const { return end - start + 1; }
        inline bool operator==(const Range &rv) const {
            return start == rv.start && end == rv.end;
        }
        void updateLength(const int length);
        static std::optional<DlnaContentServer::Range> fromRange(const QString &rangeHeader,
                                              qint64 length = -1);
    };
    enum DLNA_ORG_FLAGS {
        DLNA_ORG_FLAG_NONE = 0U,
        DLNA_ORG_FLAG_SENDER_PACED = (1U << 31),
        DLNA_ORG_FLAG_TIME_BASED_SEEK = (1U << 30),
        DLNA_ORG_FLAG_BYTE_BASED_SEEK = (1U << 29),
        DLNA_ORG_FLAG_PLAY_CONTAINER = (1U << 28),
        DLNA_ORG_FLAG_S0_INCREASE = (1U << 27),
        DLNA_ORG_FLAG_SN_INCREASE = (1U << 26),
        DLNA_ORG_FLAG_RTSP_PAUSE = (1U << 25),
        DLNA_ORG_FLAG_STREAMING_TRANSFER_MODE = (1U << 24),
        DLNA_ORG_FLAG_INTERACTIVE_TRANSFERT_MODE = (1U << 23),
        DLNA_ORG_FLAG_BACKGROUND_TRANSFER_MODE = (1U << 22),
        DLNA_ORG_FLAG_CONNECTION_STALL = (1U << 21),
        DLNA_ORG_FLAG_DLNA_V15 = (1U << 20)
    };
public:

    DlnaContentServer(QObject *parent = nullptr, int nPort = 8080);
    ~DlnaContentServer();
    void requestHandler(QHttpRequest *req, QHttpResponse *resp);
    void streamFile(const QString &path, const QString &mime,
                                         QHttpRequest *req, QHttpResponse *resp);
    void streamFileRange(std::shared_ptr<QFile> file, QHttpRequest *req,
                         QHttpResponse *resp);
    void streamFileNoRange(std::shared_ptr<QFile> file, QHttpRequest *req,
                           QHttpResponse *resp);

    void sendEmptyResponse(QHttpResponse *resp, int code);
    QString dlnaContentFeaturesHeader(const QString& mime,
                                      bool seek = true,
                                      bool flags = true);
    QString dlnaOrgPnFlags(const QString &mime);
    QString dlnaOrgFlagsForFile();
    QString dlnaOrgFlagsForStreaming();
    void setBaseUrl(const QString &baseUrl);
    void setDlnaFileName(const QString &fileName);
    QString getBaseUrl() const;
    bool getIsStartHttpServer();
private:
    QString m_sDlnaFileName;
    QHttpServer *m_httpServer;
    QString m_sBaseUrl;
    int m_nNum;
    bool m_bStartHttpServer = false;
    QThread *m_pThread;
signals:
    void contSeqWriteData(std::shared_ptr<QFile> file, qint64 size,
                          QHttpResponse *resp);
    void closeServer();
public slots:
    void seqWriteData(std::shared_ptr<QFile> file, qint64 size,
                      QHttpResponse *resp);
    bool initializeHttpServer(int port);
};

#endif // DLNACONTENTSERVER_H
