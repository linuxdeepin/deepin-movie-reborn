/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
#include "online_sub.h"
#include "dmr_settings.h"
#include "utils.h"

#include <functional>
#include <openssl/md5.h>


namespace dmr {
static OnlineSubtitle *_instance = nullptr;

using RequestFunc = QString (const QFileInfo& fi);

struct SubtitleProvider {
    QString apiurl;
    std::function<RequestFunc> reqfn;
};

static SubtitleProvider shooter;

static QString hash_file(const QFileInfo& fi)
{
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
            md5[i*2] = hex[out[i] >> 4];
            md5[i*2+1] = hex[out[i] & 0xf];
        }

        mds.append(QString::fromLatin1((const char*)md5, 32));
#endif
    });

    qDebug() << mds.join(";");
    //Qt seems has a bug that ; will not be encoded as %3B in url query
    return mds.join("%3B");
}

OnlineSubtitle& OnlineSubtitle::get()
{
    if (_instance == nullptr) {
        _instance = new OnlineSubtitle;
    }

    return *_instance;
}

OnlineSubtitle::OnlineSubtitle()
{
    shooter.apiurl = "http://www.shooter.cn/api/subapi.php";
    shooter.reqfn = [](const QFileInfo& fi) {
        if (!fi.exists()) return "";
        return "";
    };

    _defaultLocation = QString("%1/%2/%3/subtitles")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    QDir d;
    d.mkpath(_defaultLocation);

    _nam = new QNetworkAccessManager(this);
    connect(_nam, &QNetworkAccessManager::finished, this, &OnlineSubtitle::replyReceived);
}

void OnlineSubtitle::subtitlesDownloadComplete()
{
    QList<QString> files;
    for (auto& sub: _subs) {
        if (!sub.local.isEmpty())
            files.append(sub.local); // filter out some index files (idx e.g.)
    }

    emit subtitlesDownloadedFor(QUrl::fromLocalFile(_lastReqVideo.absoluteFilePath()), files, _lastReason);
    _subs.clear();
    _lastReqVideo = QFileInfo();
    _lastReason = FailReason::NoError;
}

QString OnlineSubtitle::findAvailableName(const QString& tmpl, int id)
{
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
            return path;
        }
        c++;
    } while (c < (1<<16));
    return tmpl;
}

void OnlineSubtitle::replyReceived(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->property("type") == "sub") {
            _pendingDownloads--;
            if (_pendingDownloads <= 0) {
                _lastReason = FailReason::NetworkError;
                subtitlesDownloadComplete();
            }
        }
        qDebug() << reply->errorString();
        return;
    }

    if (reply->property("type") == "meta") {
        auto data = reply->readAll();
        qDebug() << "data size " << data.size() << (int)data[0];
        if (data.size() == 1 && (int)data[0] == -1) {
            qDebug() << "no subtitle found";
            _lastReason = FailReason::NoSubFound;
            subtitlesDownloadComplete();
            return;
        }

        auto json = QJsonDocument::fromJson(data);
        if (json.isArray()) {
            qDebug() << json;
            _subs.clear();

            for (auto v: json.array()) {
                if (v.isObject()) {
                    auto obj = v.toObject();
                    for (auto f: obj["Files"].toArray()) {
                        auto fi = f.toObject();

                        ShooterSubtitleMeta meta;
                        meta.id = _subs.size();
                        meta.desc = obj["Desc"].toString();
                        meta.delay = obj["Delay"].toInt();
                        meta.ext = fi["Ext"].toString();
                        meta.link = fi["Link"].toString();
                        _subs.append(meta);
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
            qDebug() << disposition;
        } else if (reply->hasRawHeader("Content-Disposition")) {
            QByteArray name;
            auto bytes = reply->rawHeader("Content-Disposition");
            for (auto h: bytes.split(';')) {
                auto kv = h.split('=');
                if (kv.size() == 2 && kv[0].trimmed() == "filename") {
                    name = kv[1].trimmed();
                    break;
                }
            }
            if (!name.isEmpty()) {
                auto codec = QTextCodec::codecForName("UTF-8");
                name_tmpl = codec->toUnicode(name);
            }
        } else {
            int id = reply->property("id").toInt();
            name_tmpl = QString("%1.%2").arg(_lastReqVideo.completeBaseName())
                .arg(_subs[id].ext);
        }
        reply->close();

        int id = reply->property("id").toInt();
        path = findAvailableName(name_tmpl, id);
        {
            QFile f(path);
            if (f.open(QFile::WriteOnly)) {
                f.write(data);
            }
            f.flush();
        }

        _pendingDownloads--;
        if (hasHashConflict(path, name_tmpl)) {
            _lastReason = FailReason::Duplicated;
            QFile::remove(path);
        } else {
            _subs[id].local = path;
            qDebug() << "save to " << path;
        }

        if (_pendingDownloads <= 0) {
            subtitlesDownloadComplete();
        }
    }
}

bool OnlineSubtitle::hasHashConflict(const QString& path, const QString& tmpl)
{
    QFileInfo fi(path);
    auto md5 = utils::FullFileHash(fi);
    
    QDirIterator di(fi.path());
    while (di.hasNext()) {
        di.next();
        auto s = di.fileName();
        if (fi.fileName() == di.fileName()) 
            continue;

        s = s.replace(QRegExp("\\[\\d+\\]"), "");
        if (tmpl == s) {
            auto h = utils::FullFileHash(di.fileInfo());
            qDebug() << "found " << di.fileName() << h;
            if (h == md5) {
                return true;
            }
        }
    }

    return false;
}

void OnlineSubtitle::downloadSubtitles()
{
    _pendingDownloads = _subs.size();

    for (auto& sub: _subs) {
        QNetworkRequest req;
        //QUrl url(sub.link.toUtf8());
        auto s = sub.link;
        s.replace("https://", "http://");
        QUrl url(s);
        url.setScheme("http");
        req.setUrl(url);

        auto *reply = _nam->get(req);
        //qDebug() << __func__ << sub.link << url;
        reply->setProperty("type", "sub");
        reply->setProperty("id", sub.id);
    }
}

QString OnlineSubtitle::storeLocation()
{
    return _defaultLocation;
}

void OnlineSubtitle::requestSubtitle(const QUrl& url)
{
    QFileInfo fi(url.toLocalFile());
    QString h = hash_file(fi);
    _lastReqVideo = fi;

    QUrl req_url;
    req_url.setUrl(shooter.apiurl);

    QUrlQuery q;
    q.addQueryItem("filehash", h);
    //q.addQueryItem("pathinfo", fi.absoluteFilePath());
    q.addQueryItem("pathinfo", fi.fileName());
    q.addQueryItem("format", "json");
    //q.addQueryItem("lang", "chn");

    QUrl params;
    params.setQuery(q);
    auto data = params.query(QUrl::FullyEncoded).toUtf8();

    //qDebug() << req_url << params.query(QUrl::FullyEncoded);

    QNetworkRequest req;
    req.setUrl(shooter.apiurl);
    req.setHeader(QNetworkRequest::ContentLengthHeader, data.length());
    req.setRawHeader("Content-Type", "application/x-www-form-urlencoded; charset=utf-8");

    auto reply = _nam->post(req, data);
    reply->setProperty("type", "meta");
}

}
