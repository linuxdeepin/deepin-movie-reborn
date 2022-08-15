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

#ifndef Q_HTTP_SERVER_FWD
#define Q_HTTP_SERVER_FWD

#include <QHash>
#include <QString>

/*!
 * A map of request or response headers
 */
typedef QHash<QString, QString> HeaderHash;

// QHttpServer
class QHttpServer;
class QHttpConnection;
class QHttpRequest;
class QHttpResponse;

// Qt
class QTcpServer;
class QTcpSocket;

// http_parser
struct http_parser_settings;
struct http_parser;

#endif
