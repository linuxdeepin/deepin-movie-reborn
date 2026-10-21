// Copyright (C) 2020 ~ 2020 Deepin Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qhttprequest.h"
#include "ddlog.h"

#include "qhttpconnection.h"

#include <QDebug>

QHttpRequest::QHttpRequest(QHttpConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_url("http://localhost/"), m_success(false)
{
    qCDebug(dmrDlna) << "Entering QHttpRequest constructor. Connection:" << connection << ", Parent:" << parent;
    qCDebug(dmrDlna) << "Initialized m_url to:" << m_url.toString() << ", m_success to:" << m_success;
    qCDebug(dmrDlna) << "Exiting QHttpRequest constructor.";
}

QHttpRequest::~QHttpRequest()
{
    qCDebug(dmrDlna) << "Entering QHttpRequest destructor.";
    qCDebug(dmrDlna) << "Exiting QHttpRequest destructor.";
}

QString QHttpRequest::header(const QString &field)
{
    qCDebug(dmrDlna) << "Entering header function";
    return m_headers.value(field.toLower(), "");
}

const HeaderHash &QHttpRequest::headers() const
{
    qCDebug(dmrDlna) << "Entering headers function";
    return m_headers;
}

const QString &QHttpRequest::httpVersion() const
{
    qCDebug(dmrDlna) << "Entering httpVersion:" << m_version;
    return m_version;
}

const QUrl &QHttpRequest::url() const
{
    qCDebug(dmrDlna) << "Entering url:" << m_url.toString();
    return m_url;
}

const QString QHttpRequest::path() const
{
    qCDebug(dmrDlna) << "Entering path:" << m_url.path();
    return m_url.path();
}

const QString QHttpRequest::methodString() const
{
    qCDebug(dmrDlna) << "Entering methodString";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return MethodToString(method());
#else
    return MethodToString(m_method);
#endif
}

QHttpRequest::HttpMethod QHttpRequest::method() const
{
    qCDebug(dmrDlna) << "Entering method:" << m_method;
    return m_method;
}

const QString &QHttpRequest::remoteAddress() const
{
    qCDebug(dmrDlna) << "Entering remoteAddress:" << m_remoteAddress;
    return m_remoteAddress;
}

quint16 QHttpRequest::remotePort() const
{
    qCDebug(dmrDlna) << "Entering remotePort:" << m_remotePort;
    return m_remotePort;
}

void QHttpRequest::storeBody()
{
    qCDebug(dmrDlna) << "Entering storeBody function";
    connect(this, SIGNAL(data(const QByteArray &)), this, SLOT(appendBody(const QByteArray &)),
            Qt::UniqueConnection);
}

QString QHttpRequest::MethodToString(HttpMethod method)
{
    qCDebug(dmrDlna) << "Entering MethodToString function";
    int index = staticMetaObject.indexOfEnumerator("HttpMethod");
    return staticMetaObject.enumerator(index).valueToKey(method);
}

void QHttpRequest::appendBody(const QByteArray &body)
{
    qCDebug(dmrDlna) << "Entering appendBody function";
    m_body.append(body);
}
