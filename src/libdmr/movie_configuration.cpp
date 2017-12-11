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
#include "config.h"
#include "movie_configuration.h"
#include "utils.h"

#include <QtSql>
#include <atomic>

namespace dmr 
{
static std::atomic<MovieConfiguration*> _instance { nullptr };
static QMutex _instLock;

#define CHECKED_EXEC(q) do { \
    if (!(q).exec()) { \
        qCritical() << (q).lastError(); \
    } \
} while (0)

// storage as a database:
// table 1: urls
// url md5 timestamp
// md5 is local file's md5, if url is networked, md5 == 0
// table 2: infos (stores info about every url)
// url key value
class MovieConfigurationBackend: public QObject
{
public:
    MovieConfigurationBackend(MovieConfiguration* cfg): QObject(cfg)
    {
        auto db_dir = QString("%1/%2/%3")
            .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
            .arg(qApp->organizationName())
            .arg(qApp->applicationName());

        QDir d;
        d.mkpath(db_dir);

        auto db_path = QString("%1/movies.db").arg(db_dir);
        _db = QSqlDatabase::addDatabase("QSQLITE");
        _db.setDatabaseName(db_path);
        _db.open();

        auto ts = _db.tables(QSql::Tables);
        if (!ts.contains("urls") || !ts.contains("infos")) {
            QSqlQuery q(_db);
            if (!q.exec("create table if not exists urls (url TEXT primary key, "
                    "md5 TEXT, timestamp DATETIME)")) {
                qCritical() << q.lastError();
            }

            if (!q.exec("create table if not exists infos (url TEXT, "
                    "key TEXT, value BLOB, primary key (url, key))")) {
                qCritical() << q.lastError();
            }
        }
    }

    void deleteUrl(const QUrl& url)
    {
        _db.transaction();

        QSqlQuery q(_db);
        q.prepare("delete from infos where url = ?");
        q.addBindValue(url);
        if (!q.exec()) {
            _db.commit();
            return;
        }
        
        if (q.numRowsAffected() > 0) {
            QSqlQuery q(_db);
            q.prepare("delete from urls where url = ?");
            q.addBindValue(url);
            CHECKED_EXEC(q);
        }
    }

    bool urlExists(const QUrl& url)
    {
        QSqlQuery q(_db);
        q.prepare("select url from urls where url = ? limit 1");
        q.addBindValue(url);
        CHECKED_EXEC(q);

        return q.first();
    }

    void clear()
    {
        _db.transaction();

        QSqlQuery q(_db);
        if (q.exec("delete from infos")) {
            if (q.exec("delete from urls")) {
                _db.commit();
                return;
            }
        }

        _db.rollback();
    }

    void updateUrl(const QUrl& url, const QString& key, const QVariant& val)
    {
        qDebug() << url << key << val;

        _db.transaction();

        if (!urlExists(url)) {
            QString md5;
            if (url.isLocalFile()) {
                md5 = utils::FastFileHash(QFileInfo(url.toLocalFile()));
            } else {
                md5 = QString(QCryptographicHash::hash(url.toString().toUtf8(), QCryptographicHash::Md5).toHex());
            }

            QSqlQuery q(_db);
            q.prepare("insert into urls (url, md5, timestamp) values (?, ?, ?)");
            q.addBindValue(url);
            q.addBindValue(md5);
            q.addBindValue(QDateTime::currentDateTimeUtc());
            if (!q.exec()) {
                _db.rollback();
                return;
            }
        }

        QSqlQuery q(_db);
        q.prepare("replace into infos (url, key, value) values (?, ?, ?)");
        q.addBindValue(url);
        q.addBindValue(key);
        q.addBindValue(val);
        CHECKED_EXEC(q);
        _db.commit();
    }

    QVariant queryValueByUrlKey(const QUrl& url, const QString& key)
    {
        if (!urlExists(url))
            return {};

        QSqlQuery q(_db);
        q.prepare("select value from infos where url = ? and key = ?");
        q.addBindValue(url);
        q.addBindValue(key);
        CHECKED_EXEC(q);

        if (q.next()) {
            return q.value(0);
        }

        return QVariant();
    }

    QMap<QString, QVariant> queryByUrl(const QUrl& url)
    {
        if (!urlExists(url))
            return {};

        QSqlQuery q(_db);
        q.prepare("select key, value from infos where url = ?");
        q.addBindValue(url);
        CHECKED_EXEC(q);

        QMap<QString, QVariant> res;
        while (q.next()) {
            res.insert(q.value(0).toString(), q.value(1));
        }

        return res;
    }

    ~MovieConfigurationBackend()
    {
        _db.close();
        QSqlDatabase::removeDatabase(_db.connectionName());
    }

private:
    QSqlDatabase _db;
};

MovieConfiguration& MovieConfiguration::get()
{
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        if (_instance == nullptr) {
            _instance = new MovieConfiguration;
        }
    }

    return *_instance;
}

void MovieConfiguration::removeUrl(const QUrl& url)
{
    _backend->deleteUrl(url);
}

bool MovieConfiguration::urlExists(const QUrl& url)
{
    return _backend->urlExists(url);
}

void MovieConfiguration::clear()
{
    _backend->clear();
}

void MovieConfiguration::updateUrl(const QUrl& url, const QString& key, const QVariant& val)
{
    _backend->updateUrl(url, key, val);
}

void MovieConfiguration::updateUrl(const QUrl& url, KnownKey key, const QVariant& val)
{
    updateUrl(url, knownKey2String(key), val);
}

void MovieConfiguration::append2ListUrl(const QUrl& url, KnownKey key, const QString& val)
{
    auto list = getByUrl(url, knownKey2String(key)).toString().split(';', QString::SkipEmptyParts);
    auto bytes = val.toUtf8().toBase64();
    list.append(bytes);
    updateUrl(url, key, list.join(';'));
}

void MovieConfiguration::removeFromListUrl(const QUrl& url, KnownKey key, const QString& val)
{
    auto list = getListByUrl(url, key);

}

QString MovieConfiguration::knownKey2String(KnownKey kk)
{
    switch (kk) {
        case KnownKey::SubDelay: return "sub-delay";
        case KnownKey::SubCodepage: return "sub-codepage";
        case KnownKey::SubId: return "sid";
        case KnownKey::StartPos: return "start";
        case KnownKey::ExternalSubs: return "external-subs";
        default: return "";
    }
}

QStringList MovieConfiguration::getListByUrl(const QUrl& url, KnownKey key)
{
    return decodeList(getByUrl(url, knownKey2String(key)));
}

QStringList MovieConfiguration::decodeList(const QVariant& val)
{
    auto list = val.toString().split(';', QString::SkipEmptyParts);
    std::transform(list.begin(), list.end(), list.begin(), [](const QString& s) {
        return QByteArray::fromBase64(s.toUtf8());
    });

    return list;
}

QVariant MovieConfiguration::getByUrl(const QUrl& url, const QString& key)
{
    return _backend->queryValueByUrlKey(url, key);
}

QVariant MovieConfiguration::getByUrl(const QUrl& url, KnownKey key)
{
    return getByUrl(url, knownKey2String(key));
}

QMap<QString, QVariant> MovieConfiguration::queryByUrl(const QUrl& url)
{
    return _backend->queryByUrl(url);
}

MovieConfiguration::~MovieConfiguration()
{
    delete _backend;
}

MovieConfiguration::MovieConfiguration()
    :QObject(0)
{
}

static void _backend_test()
{
    auto& mc = MovieConfiguration::get();
    mc.updateUrl(QUrl("movie1"), "sub-delay", -2.5);
    mc.updateUrl(QUrl("movie1"), "sub-delay", 1.5);
    mc.updateUrl(QUrl("movie2"), "sub-delay", 1.0);
    mc.updateUrl(QUrl("movie1"), "volume", 20);
    //mc.clear();
    auto res = mc.queryByUrl(QUrl("movie1"));
    Q_ASSERT (res.size() == 2);
    qDebug() << res;

    mc.removeUrl(QUrl("movie1"));
    mc.updateUrl(QUrl("movie1"), "volume", 30);
    mc.updateUrl(QUrl("movie2"), "volume", 40);

    res = mc.queryByUrl(QUrl("movie1"));
    Q_ASSERT (res.size() == 1);
    qDebug() << res;
    mc.clear();
}

void MovieConfiguration::init()
{
    _backend = new MovieConfigurationBackend(this);
#ifdef SQL_TEST
    _backend_test();
#endif
}

}
