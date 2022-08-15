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

#include "qhttprequest.h"

#include "qhttpconnection.h"

QHttpRequest::QHttpRequest(QHttpConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_url("http://localhost/"), m_success(false)
{
}

QHttpRequest::~QHttpRequest()
{
}

QString QHttpRequest::header(const QString &field)
{
    return m_headers.value(field.toLower(), "");
}

const HeaderHash &QHttpRequest::headers() const
{
    return m_headers;
}

const QString &QHttpRequest::httpVersion() const
{
    return m_version;
}

const QUrl &QHttpRequest::url() const
{
    return m_url;
}

const QString QHttpRequest::path() const
{
    return m_url.path();
}

const QString QHttpRequest::methodString() const
{
    return MethodToString(method());
}

QHttpRequest::HttpMethod QHttpRequest::method() const
{
    return m_method;
}

const QString &QHttpRequest::remoteAddress() const
{
    return m_remoteAddress;
}

quint16 QHttpRequest::remotePort() const
{
    return m_remotePort;
}

void QHttpRequest::storeBody()
{
    connect(this, SIGNAL(data(const QByteArray &)), this, SLOT(appendBody(const QByteArray &)),
            Qt::UniqueConnection);
}

QString QHttpRequest::MethodToString(HttpMethod method)
{
    int index = staticMetaObject.indexOfEnumerator("HttpMethod");
    return staticMetaObject.enumerator(index).valueToKey(method);
}

void QHttpRequest::appendBody(const QByteArray &body)
{
    m_body.append(body);
}
