// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "online_sub.h"
#include "ddlog.h"
#include "dmr_settings.h"
#include "utils.h"

#include <functional>

// 在 Qt6 中，QTextCodec 类被移到了 QStringConverter 中
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#else
#include <QStringDecoder>
#endif

namespace dmr {
static OnlineSubtitle *_instance = nullptr;

using RequestFunc = QString (const QFileInfo &fi);

struct SubtitleProvider {
    QString apiurl;
    std::function<RequestFunc> reqfn;
};

static SubtitleProvider shooter;

static QString hash_file(const QFileInfo &fi)
{
    qCDebug(dmrLib) << "Calculating file hash for:" << fi.absoluteFilePath();
    auto sz = fi.size();
    QList<qint64> offsets = {
        4096,
        sz / 3 * 2,
        sz / 3,
        sz - 8192
    };

    QStringList mds;

    QFile f(fi.absoluteFilePath());
    if (!f.open(QFile::ReadOnly)) {
        qCWarning(dmrLib) << "Failed to open file for hashing:" << fi.absoluteFilePath();
        return QString();
    }

    std::for_each(offsets.begin(), offsets.end(), [&f, &mds](qint64 v) {
        f.seek(v);
        auto bytes = f.read(4096);

#if 1
        auto h = QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
        mds.append(h);
#else
        unsigned char out[16];
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, bytes.data(), bytes.size());
        MD5_Final(out, &ctx);

        char hex[] = "0123456789ABCDEF";
        char md5[32];
        for (int i = 0; i < 16; i++) {
            md5[i * 2] = hex[out[i] >> 4];
            md5[i * 2 + 1] = hex[out[i] & 0xf];
        }

        mds.append(QString::fromLatin1((const char *)md5, 32));
#endif
    });
    f.close();

    qCDebug(dmrLib) << "File hash calculated:" << mds.join(";");
    //Qt seems has a bug that ; will not be encoded as %3B in url query
    return mds.join("%3B");
}

OnlineSubtitle &OnlineSubtitle::get()
{
    if (_instance == nullptr) {
        qCDebug(dmrLib) << "Creating new OnlineSubtitle instance";
        _instance = new OnlineSubtitle;
    }
    return *_instance;
}

OnlineSubtitle::OnlineSubtitle()
{
    qCDebug(dmrLib) << "Initializing OnlineSubtitle";
    shooter.apiurl = "http://www.shooter.cn/api/subapi.php";
    shooter.reqfn = [](const QFileInfo & fi) {
        if (!fi.exists()) return "";
        return "";
    };

    _defaultLocation = QString("%1/%2/%3/subtitles")
                       .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                       .arg(qApp->organizationName())
                       .arg(qApp->applicationName());
    QDir d;
    d.mkpath(_defaultLocation);
    qCDebug(dmrLib) << "Subtitle storage location:" << _defaultLocation;

    _nam = new QNetworkAccessManager(this);
    connect(_nam, &QNetworkAccessManager::finished, this, &OnlineSubtitle::replyReceived);
    qCDebug(dmrLib) << "OnlineSubtitle initialization completed";
}

void OnlineSubtitle::subtitlesDownloadComplete()
{
    qCDebug(dmrLib) << "Subtitle download completed";
    QList<QString> files;
    for (auto &sub : _subs) {
        if (!sub.local.isEmpty())
            files.append(sub.local); // filter out some index files (idx e.g.)
    }

    qCDebug(dmrLib) << "Downloaded subtitle files:" << files;
    emit subtitlesDownloadedFor(QUrl::fromLocalFile(_lastReqVideo.absoluteFilePath()), files, _lastReason);
    _subs.clear();
    _lastReqVideo = QFileInfo();
    _lastReason = FailReason::NoError;
}

QString OnlineSubtitle::findAvailableName(const QString &tmpl, int id)
{
    qCDebug(dmrLib) << "Finding available name for template:" << tmpl << "ID:" << id;
    QString name_tmpl = tmpl;
    int i = tmpl.lastIndexOf('.');
    if (i >= 0) {
        name_tmpl.replace(i, 1, "[%1].");
    } else {
        name_tmpl = name_tmpl.append("[%1]");
    }
    auto c = id;
    do {
        auto name = name_tmpl.arg(c);
        auto path = QString("%1/%2").arg(storeLocation()).arg(name);
        if (!QFile::exists(path)) {
            qCDebug(dmrLib) << "Found available name:" << path;
            return path;
        }
        c++;
    } while (c < (1 << 16));
    qCWarning(dmrLib) << "No available name found for template:" << tmpl;
    return tmpl;
}

void OnlineSubtitle::replyReceived(QNetworkReply *reply)
{
    qCDebug(dmrLib) << "Received network reply for type:" << reply->property("type").toString();
    
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->property("type") == "sub") {
            _pendingDownloads--;
            if (_pendingDownloads <= 0) {
                _lastReason = FailReason::NetworkError;
                qCWarning(dmrLib) << "Network error occurred, marking download as failed";
                subtitlesDownloadComplete();
            }
        }
        qCWarning(dmrLib) << "Network error:" << reply->errorString();
        reply->deleteLater();
        return;
    }

    if (reply->property("type") == "meta") {
        auto data = reply->readAll();
        qCDebug(dmrLib) << "Received metadata response, size:" << data.size();
        // fix bug 24817 by ZhuYuliang
        if ((0 == data.size()) || (((data.size() == 1) && (static_cast<int>(data[0]) == -1)) || (static_cast<int>(data[0]) == 255))) {
            qCInfo(dmrLib) << "No subtitle found for the video";
            _lastReason = FailReason::NoSubFound;
            emit onlineSubtitleStateChanged(_lastReason);
            reply->deleteLater();
            return;
        }

        auto json = QJsonDocument::fromJson(data);
        if (json.isArray()) {
            qCDebug(dmrLib) << "Processing subtitle metadata";
            _subs.clear();

            for (auto v : json.array()) {
                if (v.isObject()) {
                    auto obj = v.toObject();
                    for (auto f : obj["Files"].toArray()) {
                        auto fi = f.toObject();

                        ShooterSubtitleMeta meta;
                        meta.id = _subs.size();
                        meta.desc = obj["Desc"].toString();
                        meta.delay = obj["Delay"].toInt();
                        meta.ext = fi["Ext"].toString();
                        meta.link = fi["Link"].toString();
                        _subs.append(meta);
                        qCDebug(dmrLib) << "Found subtitle:" << meta.desc << "Extension:" << meta.ext;
                    }
                }
            }

            downloadSubtitles();
        }

        reply->close();

    } else if (reply->property("type") == "sub") {
        QString path;
        QString name_tmpl;

        auto data = reply->readAll();
        auto disposition = reply->header(QNetworkRequest::ContentDispositionHeader);
        if (disposition.isValid()) {
            //set name to disposition filename
            qCDebug(dmrLib) << "Using Content-Disposition header for filename:" << disposition;
        } else if (reply->hasRawHeader("Content-Disposition")) {
            QByteArray name;
            auto bytes = reply->rawHeader("Content-Disposition");
            for (auto h : bytes.split(';')) {
                auto kv = h.split('=');
                if (kv.size() == 2 && kv[0].trimmed() == "filename") {
                    name = kv[1].trimmed();
                    break;
                }
            }
            if (!name.isEmpty()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                auto codec = QTextCodec::codecForName("UTF-8");
                name_tmpl = codec->toUnicode(name);
#else           
                // Qt6 中使用 QStringDecoder 来解码字符串
                name_tmpl = QStringDecoder(QStringDecoder::Utf8)(name);
#endif
                qCDebug(dmrLib) << "Extracted filename from Content-Disposition:" << name_tmpl;
            }
        } else {
            int id = reply->property("id").toInt();
            name_tmpl = QString("%1.%2").arg(_lastReqVideo.completeBaseName())
                        .arg(_subs[id].ext);
            qCDebug(dmrLib) << "Generated filename from video name:" << name_tmpl;
        }
        reply->close();

        int id = reply->property("id").toInt();
        path = findAvailableName(name_tmpl, id);
        {
            QFile f(path);
            if (f.open(QFile::WriteOnly)) {
                f.write(data);
                qCDebug(dmrLib) << "Saved subtitle to:" << path;
            } else {
                qCWarning(dmrLib) << "Failed to save subtitle to:" << path;
            }
            f.flush();
        }

        _pendingDownloads--;
        QString conflictPath;
        if (hasHashConflict(path, name_tmpl, conflictPath)) {
            qCInfo(dmrLib) << "Found duplicate subtitle, using existing file:" << conflictPath;
            _lastReason = FailReason::Duplicated;
            _subs[id].local = conflictPath;
            QFile::remove(path);
        } else {
            _subs[id].local = path;
            qCDebug(dmrLib) << "Subtitle saved successfully to:" << path;
        }

        if (_pendingDownloads <= 0) {
            subtitlesDownloadComplete();
        }
    }
    reply->deleteLater();
}

bool OnlineSubtitle::hasHashConflict(const QString &path, const QString &tmpl, QString &conflictPath)
{
    qCDebug(dmrLib) << "Checking for hash conflicts for:" << path;
    QFileInfo fi(path);
    auto md5 = utils::FullFileHash(fi);

    QDirIterator di(fi.path());
    while (di.hasNext()) {
        di.next();
        auto s = di.fileName();
        if (fi.fileName() == di.fileName())
            continue;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        s = s.replace(QRegExp("\\[\\d+\\]"), "");
#else
        s = s.replace(QRegularExpression("\\[\\d+\\]"), "");
#endif
        if (tmpl == s) {
            auto h = utils::FullFileHash(di.fileInfo());
            qCDebug(dmrLib) << "Comparing with existing file:" << di.fileName() << "Hash:" << h;
            if (h == md5) {
                conflictPath = di.filePath();
                qCInfo(dmrLib) << "Found hash conflict with:" << conflictPath;
                return true;
            }
        }
    }

    return false;
}

void OnlineSubtitle::downloadSubtitles()
{
    qCDebug(dmrLib) << "Starting subtitle download for" << _subs.size() << "files";
    _pendingDownloads = _subs.size();

    for (auto &sub : _subs) {
        QNetworkRequest req;
        auto s = sub.link;
        s.replace("https://", "http://");
        QUrl url(s);
        url.setScheme("http");
        req.setUrl(url);

        auto *reply = _nam->get(req);
        qCDebug(dmrLib) << "Downloading subtitle from:" << url.toString();
        reply->setProperty("type", "sub");
        reply->setProperty("id", sub.id);
    }
}

QString OnlineSubtitle::storeLocation()
{
    return _defaultLocation;
}

void OnlineSubtitle::requestSubtitle(const QUrl &url)
{
    qCDebug(dmrLib) << "Requesting subtitle for:" << url.toString();
    QFileInfo fi(url.toLocalFile());
    QString h = hash_file(fi);
    _lastReqVideo = fi;

    QUrl req_url;
    req_url.setUrl(shooter.apiurl);

    QUrlQuery q;
    q.addQueryItem("filehash", h);
    q.addQueryItem("pathinfo", fi.fileName());
    q.addQueryItem("format", "json");

    QUrl params;
    params.setQuery(q);
    auto data = params.query(QUrl::FullyEncoded).toUtf8();

    QNetworkRequest req;
    req.setUrl(shooter.apiurl);
    req.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    req.setRawHeader("Content-Type", "application/x-www-form-urlencoded; charset=utf-8");

    qCDebug(dmrLib) << "Sending subtitle request to:" << shooter.apiurl;
    auto reply = _nam->post(req, data);
    reply->setProperty("type", "meta");
}

}
