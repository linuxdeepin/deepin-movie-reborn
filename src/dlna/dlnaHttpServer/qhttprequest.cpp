// Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qhttprequest.h"

#include "qhttpconnection.h"

QHttpRequest::QHttpRequest(QHttpConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_url("http://localhost/"), m_success(false)
{
    qDebug() << "Entering QHttpRequest constructor. Connection:" << connection << ", Parent:" << parent;
    qDebug() << "Initialized m_url to:" << m_url.toString() << ", m_success to:" << m_success;
    qDebug() << "Exiting QHttpRequest constructor.";
}

QHttpRequest::~QHttpRequest()
{
    qDebug() << "Entering QHttpRequest destructor.";
    qDebug() << "Exiting QHttpRequest destructor.";
}

QString QHttpRequest::header(const QString &field)
{
    qDebug() << "Entering header function";
    return m_headers.value(field.toLower(), "");
}

const HeaderHash &QHttpRequest::headers() const
{
    qDebug() << "Entering headers function";
    return m_headers;
}

const QString &QHttpRequest::httpVersion() const
{
    qDebug() << "Entering httpVersion:" << m_version;
    return m_version;
}

const QUrl &QHttpRequest::url() const
{
    qDebug() << "Entering url:" << m_url.toString();
    return m_url;
}

const QString QHttpRequest::path() const
{
    qDebug() << "Entering path:" << m_url.path();
    return m_url.path();
}

const QString QHttpRequest::methodString() const
{
    qDebug() << "Entering methodString";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return MethodToString(method());
#else
    return MethodToString(m_method);
#endif
}

QHttpRequest::HttpMethod QHttpRequest::method() const
{
    qDebug() << "Entering method:" << m_method;
    return m_method;
}

const QString &QHttpRequest::remoteAddress() const
{
    qDebug() << "Entering remoteAddress:" << m_remoteAddress;
    return m_remoteAddress;
}

quint16 QHttpRequest::remotePort() const
{
    qDebug() << "Entering remotePort:" << m_remotePort;
    return m_remotePort;
}

void QHttpRequest::storeBody()
{
    qDebug() << "Entering storeBody function";
    connect(this, SIGNAL(data(const QByteArray &)), this, SLOT(appendBody(const QByteArray &)),
            Qt::UniqueConnection);
}

QString QHttpRequest::MethodToString(HttpMethod method)
{
    qDebug() << "Entering MethodToString function";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    int index = staticMetaObject.indexOfEnumerator("HttpMethod");
    return staticMetaObject.enumerator(index).valueToKey(method);
#endif
}

void QHttpRequest::appendBody(const QByteArray &body)
{
    qDebug() << "Entering appendBody function";
    m_body.append(body);
}
