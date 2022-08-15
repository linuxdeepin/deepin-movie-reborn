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

#include "qhttpserver.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>
#include <QDebug>

#include "qhttpconnection.h"

QHash<int, QString> STATUS_CODES;

QHttpServer::QHttpServer(QObject *parent) : QObject(parent), m_tcpServer(0)
{
#define STATUS_CODE(num, reason) STATUS_CODES.insert(num, reason);
    // {{{
    STATUS_CODE(100, "Continue")
    STATUS_CODE(101, "Switching Protocols")
    STATUS_CODE(102, "Processing") // RFC 2518) obsoleted by RFC 4918
    STATUS_CODE(200, "OK")
    STATUS_CODE(201, "Created")
    STATUS_CODE(202, "Accepted")
    STATUS_CODE(203, "Non-Authoritative Information")
    STATUS_CODE(204, "No Content")
    STATUS_CODE(205, "Reset Content")
    STATUS_CODE(206, "Partial Content")
    STATUS_CODE(207, "Multi-Status") // RFC 4918
    STATUS_CODE(300, "Multiple Choices")
    STATUS_CODE(301, "Moved Permanently")
    STATUS_CODE(302, "Moved Temporarily")
    STATUS_CODE(303, "See Other")
    STATUS_CODE(304, "Not Modified")
    STATUS_CODE(305, "Use Proxy")
    STATUS_CODE(307, "Temporary Redirect")
    STATUS_CODE(400, "Bad Request")
    STATUS_CODE(401, "Unauthorized")
    STATUS_CODE(402, "Payment Required")
    STATUS_CODE(403, "Forbidden")
    STATUS_CODE(404, "Not Found")
    STATUS_CODE(405, "Method Not Allowed")
    STATUS_CODE(406, "Not Acceptable")
    STATUS_CODE(407, "Proxy Authentication Required")
    STATUS_CODE(408, "Request Time-out")
    STATUS_CODE(409, "Conflict")
    STATUS_CODE(410, "Gone")
    STATUS_CODE(411, "Length Required")
    STATUS_CODE(412, "Precondition Failed")
    STATUS_CODE(413, "Request Entity Too Large")
    STATUS_CODE(414, "Request-URI Too Large")
    STATUS_CODE(415, "Unsupported Media Type")
    STATUS_CODE(416, "Requested Range Not Satisfiable")
    STATUS_CODE(417, "Expectation Failed")
    STATUS_CODE(418, "I\"m a teapot")        // RFC 2324
    STATUS_CODE(422, "Unprocessable Entity") // RFC 4918
    STATUS_CODE(423, "Locked")               // RFC 4918
    STATUS_CODE(424, "Failed Dependency")    // RFC 4918
    STATUS_CODE(425, "Unordered Collection") // RFC 4918
    STATUS_CODE(426, "Upgrade Required")     // RFC 2817
    STATUS_CODE(500, "Internal Server Error")
    STATUS_CODE(501, "Not Implemented")
    STATUS_CODE(502, "Bad Gateway")
    STATUS_CODE(503, "Service Unavailable")
    STATUS_CODE(504, "Gateway Time-out")
    STATUS_CODE(505, "HTTP Version not supported")
    STATUS_CODE(506, "Variant Also Negotiates") // RFC 2295
    STATUS_CODE(507, "Insufficient Storage")    // RFC 4918
    STATUS_CODE(509, "Bandwidth Limit Exceeded")
    STATUS_CODE(510, "Not Extended") // RFC 2774
    // }}}
}

QHttpServer::~QHttpServer()
{
}

void QHttpServer::newConnection()
{
    Q_ASSERT(m_tcpServer);

    while (m_tcpServer->hasPendingConnections()) {
        QHttpConnection *connection =
            new QHttpConnection(m_tcpServer->nextPendingConnection(), this);
        connect(connection, SIGNAL(newRequest(QHttpRequest *, QHttpResponse *)), this,
                SIGNAL(newRequest(QHttpRequest *, QHttpResponse *)));
    }
}

bool QHttpServer::listen(const QHostAddress &address, quint16 port)
{
    Q_ASSERT(!m_tcpServer);
    m_tcpServer = new QTcpServer(this);

    bool couldBindToPort = m_tcpServer->listen(address, port);
    if (couldBindToPort) {
        connect(m_tcpServer, SIGNAL(newConnection()), this, SLOT(newConnection()));
    } else {
        delete m_tcpServer;
        m_tcpServer = NULL;
    }
    return couldBindToPort;
}

bool QHttpServer::listen(quint16 port)
{
    return listen(QHostAddress::Any, port);
}

void QHttpServer::close()
{
    if (m_tcpServer)
        m_tcpServer->close();
}
