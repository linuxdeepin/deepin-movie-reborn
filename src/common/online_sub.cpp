#include "online_sub.h"
#include "dmr_settings.h"

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

void OnlineSubtitle::replyReceived(QNetworkReply* reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        qDebug() << reply->errorString();
        return;
    }

    if (reply->property("type") == "meta") {
        auto data = reply->readAll();
        qDebug() << "data size " << data.size() << (int)data[0];
        if (data.size() == 1 && (int)data[0] == -1) {
            qDebug() << "no subtitle found";
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
        auto data = reply->readAll();
        auto disposition = reply->header(QNetworkRequest::ContentDispositionHeader);
        qDebug() << disposition;
        reply->close();

        int id = reply->property("id").toInt();
        auto name = QString("%1%2.%3").arg(_lastReqVideo.completeBaseName())
            .arg(id).arg(_subs[id].ext);
        if (disposition.isValid()) {
            //set name to disposition filename
        }

        auto path = QString("%1/%2").arg(storeLocation()).arg(name);
        QFile f(path);
        if (f.open(QFile::WriteOnly)) {
            f.write(data);
        }
        f.flush();
        _subs[id].local = path;
        qDebug() << "save to " << path;

        _pendingDownloads--;
        if (_pendingDownloads <= 0) {
            QList<QString> files;
            for (auto& sub: _subs) {
                files.append(sub.local); // filter out some index files (idx e.g.)
            }
            emit subtitlesDownloadedFor(QUrl::fromLocalFile(_lastReqVideo.absoluteFilePath()), files);
        }
    }
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
