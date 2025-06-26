// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "movie_configuration.h"
#include "utils.h"

#include <QtSql>
#include <atomic>

namespace dmr {
static std::atomic<MovieConfiguration *> _instance { nullptr };
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
    explicit MovieConfigurationBackend(MovieConfiguration *cfg): QObject(cfg)
    {
        qDebug() << "Initializing MovieConfigurationBackend";
        auto db_dir = QString("%1/%2/%3")
                      .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                      .arg(qApp->organizationName())
                      .arg(qApp->applicationName());

        QDir d;
        d.mkpath(db_dir);
        qDebug() << "Database directory created:" << db_dir;

        auto db_path = QString("%1/movies.db").arg(db_dir);
        qDebug() << "Database path:" << db_path;
        
        _db = QSqlDatabase::addDatabase("QSQLITE");
        _db.setDatabaseName(db_path);
        if(!_db.open()) {
            qCritical() << "Failed to open movies database:" << _db.lastError().text();
            return;
        }
        qDebug() << "Database opened successfully";

        auto ts = _db.tables(QSql::Tables);
        if (!ts.contains("urls") || !ts.contains("infos")) {
            qDebug() << "Creating database tables";
            QSqlQuery q(_db);
            if (!q.exec("create table if not exists urls (url TEXT primary key, "
                        "md5 TEXT, timestamp DATETIME)")) {
                qCritical() << "Failed to create urls table:" << q.lastError().text();
            }

            if (!q.exec("create table if not exists infos (url TEXT, "
                        "key TEXT, value BLOB, primary key (url, key))")) {
                qCritical() << "Failed to create infos table:" << q.lastError().text();
            }
            qDebug() << "Database tables created";
        }
        qDebug() << "Exiting MovieConfigurationBackend::MovieConfigurationBackend()";
    }

    void deleteUrl(const QUrl &url)
    {
        qDebug() << "Deleting URL:" << url.toString();
        if(_db.transaction()) {
            qDebug() << "Transaction started for deleteUrl.";
            QSqlQuery q(_db);
            if(q.prepare("delete from infos where url = ?")) {
                q.addBindValue(url);
                if (!q.exec()) {
                    qWarning() << "Failed to delete from infos table:" << q.lastError().text();
                    if(!_db.commit()) {
                        qCritical() << "Failed to commit transaction:" << _db.lastError().text();
                    }
                    qDebug() << "Exiting MovieConfigurationBackend::deleteUrl() with infos delete failure.";
                    return;
                }
            }

            if (q.numRowsAffected() > 0) {
                qDebug() << "Rows affected in infos table > 0, attempting to delete from urls table.";
                QSqlQuery q_l(_db);
                if(q_l.prepare("delete from urls where url = ?")) {
                    q_l.addBindValue(url);
                    CHECKED_EXEC(q_l);
                }
            }
            qDebug() << "URL deleted successfully";
        }
    }

    bool urlExists(const QUrl &url)
    {
        qDebug() << "Checking if URL exists:" << url.toString();
        QSqlQuery q(_db);
        if(q.prepare("select url from urls where url = ? limit 1")) {
            q.addBindValue(url);
            CHECKED_EXEC(q);
        }

        bool exists = q.first();
        qDebug() << "URL exists:" << exists;
        return exists;
    }

    void clear()
    {
        qDebug() << "Clearing all database entries";
        if(_db.transaction()) {
            qDebug() << "Transaction started for clear.";
            QSqlQuery q(_db);
            if (q.exec("delete from infos")) {
                qDebug() << "Deleted from infos table.";
                if (q.exec("delete from urls")) {
                    qDebug() << "Deleted from urls table.";
                    if(!_db.commit()) {
                        qCritical() << "Failed to commit clear transaction:" << _db.lastError().text();
                    }
                    qDebug() << "Database cleared successfully";
                    return;
                }
            }

            if(!_db.rollback()) {
                qCritical() << "Failed to rollback clear transaction:" << _db.lastError().text();
            }
        }
        qDebug() << "Exiting MovieConfigurationBackend::clear() with potential failure or rollback.";
    }

    void updateUrl(const QUrl &url, const QString &key, const QVariant &val)
    {
        qDebug() << "Updating URL:" << url.toString() << "Key:" << key << "Value:" << val.toString();

        if(_db.transaction()) {
            qDebug() << "Transaction started for updateUrl.";
            if (!urlExists(url)) {
                qDebug() << "URL does not exist, inserting new URL.";
                QString md5;
                if (url.isLocalFile()) {
                    md5 = utils::FastFileHash(QFileInfo(url.toLocalFile()));
                    qDebug() << "Local file MD5:" << md5;
                } else {
                    md5 = QString(QCryptographicHash::hash(url.toString().toUtf8(), QCryptographicHash::Md5).toHex());
                    qDebug() << "Remote URL MD5:" << md5;
                }

                QSqlQuery q(_db);
                if(q.prepare("insert into urls (url, md5, timestamp) values (?, ?, ?)")) {
                    q.addBindValue(url);
                    q.addBindValue(md5);
                    q.addBindValue(QDateTime::currentDateTimeUtc());
                    if (!q.exec()) {
                        qWarning() << "Failed to insert URL:" << q.lastError().text();
                        if(!_db.rollback()) {
                            qCritical() << "Failed to rollback URL insert:" << _db.lastError().text();
                        }
                        qDebug() << "Exiting MovieConfigurationBackend::updateUrl() with URL insert failure.";
                        return;
                    }
                }
            } else {
                qDebug() << "URL already exists, skipping URL insert.";
            }

            QSqlQuery q(_db);
            if(q.prepare("replace into infos (url, key, value) values (?, ?, ?)")) {
                q.addBindValue(url);
                q.addBindValue(key);
                q.addBindValue(val);
                CHECKED_EXEC(q);
                if(!_db.commit()) {
                    qCritical() << "Failed to commit update transaction:" << _db.lastError().text();
                }
                qDebug() << "URL updated successfully";
            } else {
                qWarning() << "Failed to prepare replace infos query:" << q.lastError().text();
            }
        } else {
            qWarning() << "Failed to start transaction for updateUrl.";
        }
        qDebug() << "Exiting MovieConfigurationBackend::updateUrl()";
    }

    QVariant queryValueByUrlKey(const QUrl &url, const QString &key)
    {
        qDebug() << "Querying value for URL:" << url.toString() << "Key:" << key;
        if (!urlExists(url)) {
            qDebug() << "URL does not exist";
            return {};
        }

        QSqlQuery q(_db);
        if(q.prepare("select value from infos where url = ? and key = ?")) {
            q.addBindValue(url);
            q.addBindValue(key);
            CHECKED_EXEC(q);

            if (q.next()) {
                QVariant value = q.value(0);
                qDebug() << "Value found:" << value.toString();
                qDebug() << "Exiting MovieConfigurationBackend::queryValueByUrlKey() with value:" << value.toString();
                return value;
            } else {
                qDebug() << "No value found for URL and key.";
            }
        } else {
            qWarning() << "Failed to prepare queryValueByUrlKey query:" << q.lastError().text();
        }

        qDebug() << "No value found";
        qDebug() << "Exiting MovieConfigurationBackend::queryValueByUrlKey() with empty QVariant.";
        return QVariant();
    }

    QMap<QString, QVariant> queryByUrl(const QUrl &url)
    {
        qDebug() << "Querying all values for URL:" << url.toString();
        if (!urlExists(url)) {
            qDebug() << "URL does not exist, returning empty QMap.";
            return {};
        }

        QSqlQuery q(_db);
        if(q.prepare("select key, value from infos where url = ?")) {
            q.addBindValue(url);
            CHECKED_EXEC(q);
        } else {
            qWarning() << "Failed to prepare queryByUrl query:" << q.lastError().text();
        }

        QMap<QString, QVariant> res;
        while (q.next()) {
            res.insert(q.value(0).toString(), q.value(1));
            qDebug() << "Found key:" << q.value(0).toString() << "value:" << q.value(1).toString();
        }

        qDebug() << "Found" << res.size() << "values";
        return res;
    }

    ~MovieConfigurationBackend();

private:
    QSqlDatabase _db;
};

MovieConfigurationBackend::~MovieConfigurationBackend()
{
    qDebug() << "Destroying MovieConfigurationBackend";
    _db.close();
    QSqlDatabase::removeDatabase(_db.connectionName());
    qDebug() << "Exiting MovieConfigurationBackend::~MovieConfigurationBackend()";
}

MovieConfiguration &MovieConfiguration::get()
{
    qDebug() << "Entering MovieConfiguration::get()";
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        qDebug() << "_instance is nullptr, acquiring lock and creating new instance.";
        _instance = new MovieConfiguration;
        qDebug() << "Created new MovieConfiguration instance";
    } else {
        qDebug() << "_instance already exists.";
    }
    qDebug() << "Exiting MovieConfiguration::get() with instance:" << _instance;
    return *_instance;
}

void MovieConfiguration::removeUrl(const QUrl &url)
{
    qDebug() << "Entering MovieConfiguration::removeUrl() for URL:" << url.toString();
    _backend->deleteUrl(url);
    qDebug() << "Exiting MovieConfiguration::removeUrl()";
}

bool MovieConfiguration::urlExists(const QUrl &url)
{
    qDebug() << "Entering MovieConfiguration::urlExists() for URL:" << url.toString();
    bool exists = _backend->urlExists(url);
    qDebug() << "Exiting MovieConfiguration::urlExists() with result:" << exists;
    return exists;
}

void MovieConfiguration::clear()
{
    qDebug() << "Entering MovieConfiguration::clear()";
    _backend->clear();
    qDebug() << "Exiting MovieConfiguration::clear()";
}

void MovieConfiguration::updateUrl(const QUrl &url, const QString &key, const QVariant &val)
{
    qDebug() << "Entering MovieConfiguration::updateUrl(const QUrl&, const QString&, const QVariant&) for URL:" << url.toString() << ", Key:" << key << ", Value:" << val.toString();
    _backend->updateUrl(url, key, val);
    qDebug() << "Exiting MovieConfiguration::updateUrl(const QUrl&, const QString&, const QVariant&)";
}

void MovieConfiguration::updateUrl(const QUrl &url, KnownKey key, const QVariant &val)
{
    qDebug() << "Entering MovieConfiguration::updateUrl(const QUrl&, KnownKey, const QVariant&) for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val.toString();
    updateUrl(url, knownKey2String(key), val);
    qDebug() << "Exiting MovieConfiguration::updateUrl(const QUrl&, KnownKey, const QVariant&)";
}

void MovieConfiguration::append2ListUrl(const QUrl &url, KnownKey key, const QString &val)
{
    qDebug() << "Entering MovieConfiguration::append2ListUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto list = getByUrl(url, knownKey2String(key)).toString().split(';', QString::SkipEmptyParts);
    qDebug() << "Using Qt5 split with QString::SkipEmptyParts.";
#else
    // Qt6 中使用 Qt::SkipEmptyParts 而不是 QString::SkipEmptyParts
    auto list = getByUrl(url, knownKey2String(key)).toString().split(';', Qt::SkipEmptyParts);
    qDebug() << "Using Qt6 split with Qt::SkipEmptyParts.";
#endif
    auto bytes = val.toUtf8().toBase64();
    qDebug() << "Value encoded to Base64:" << bytes;
    list.append(bytes);
    qDebug() << "Appended encoded value to list. New list size:" << list.size();
    updateUrl(url, key, list.join(';'));
    qDebug() << "List joined and updated in URL.";
    qDebug() << "Exiting MovieConfiguration::append2ListUrl()";
}

void MovieConfiguration::removeFromListUrl(const QUrl &url, KnownKey key, const QString &val)
{
    qDebug() << "Entering MovieConfiguration::removeFromListUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val;
    ///add for warning by xxj ,no any means
    //val.isNull();
    auto list = getListByUrl(url, key);
}

QString MovieConfiguration::knownKey2String(KnownKey kk)
{
    qDebug() << "Entering MovieConfiguration::knownKey2String() with KnownKey:" << (int)kk;
    switch (kk) {
    case KnownKey::SubDelay:
        qDebug() << "KnownKey is SubDelay.";
        return "sub-delay";
    case KnownKey::SubCodepage:
        qDebug() << "KnownKey is SubCodepage.";
        return "sub-codepage";
    case KnownKey::SubId:
        qDebug() << "KnownKey is SubId.";
        return "sid";
    case KnownKey::StartPos:
        qDebug() << "KnownKey is StartPos.";
        return "start";
    case KnownKey::ExternalSubs:
        qDebug() << "KnownKey is ExternalSubs.";
        return "external-subs";
    default:
        qDebug() << "KnownKey is default (unknown).";
        return "";
    }
    qDebug() << "Exiting MovieConfiguration::knownKey2String()";
}

QStringList MovieConfiguration::getListByUrl(const QUrl &url, KnownKey key)
{
    qDebug() << "Entering MovieConfiguration::getListByUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key;
    QStringList list = decodeList(getByUrl(url, knownKey2String(key)));
    qDebug() << "Exiting MovieConfiguration::getListByUrl() with list size:" << list.size();
    return list;
}

QStringList MovieConfiguration::decodeList(const QVariant &val)
{
    qDebug() << "Entering MovieConfiguration::decodeList() with QVariant:" << val.toString();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto list = val.toString().split(';', QString::SkipEmptyParts);
    qDebug() << "Using Qt5 split for decoding list.";
#else
    // Qt6 中使用 Qt::SkipEmptyParts 而不是 QString::SkipEmptyParts
    auto list = val.toString().split(';', Qt::SkipEmptyParts);
    qDebug() << "Using Qt6 split for decoding list.";
#endif
    std::transform(list.begin(), list.end(), list.begin(), [](const QString & s) {
        qDebug() << "Transforming string:" << s;
        return QByteArray::fromBase64(s.toUtf8());
    });

    qDebug() << "Exiting MovieConfiguration::decodeList() with list size:" << list.size();
    return list;
}

QVariant MovieConfiguration::getByUrl(const QUrl &url, const QString &key)
{
    qDebug() << "Entering MovieConfiguration::getByUrl(const QUrl&, const QString&) for URL:" << url.toString() << ", Key:" << key;
    QVariant value = _backend->queryValueByUrlKey(url, key);
    qDebug() << "Exiting MovieConfiguration::getByUrl(const QUrl&, const QString&) with value:" << value.toString();
    return value;
}

QVariant MovieConfiguration::getByUrl(const QUrl &url, KnownKey key)
{
    qDebug() << "Entering MovieConfiguration::getByUrl(const QUrl&, KnownKey) for URL:" << url.toString() << ", KnownKey:" << (int)key;
    QVariant value = getByUrl(url, knownKey2String(key));
    qDebug() << "Exiting MovieConfiguration::getByUrl(const QUrl&, KnownKey) with value:" << value.toString();
    return value;
}

QMap<QString, QVariant> MovieConfiguration::queryByUrl(const QUrl &url)
{
    qDebug() << "Entering MovieConfiguration::queryByUrl() for URL:" << url.toString();
    QMap<QString, QVariant> result = _backend->queryByUrl(url);
    qDebug() << "Exiting MovieConfiguration::queryByUrl() with result size:" << result.size();
    return result;
}

MovieConfiguration::~MovieConfiguration()
{
    qDebug() << "Entering MovieConfiguration::~MovieConfiguration()";
    delete _backend;
    qDebug() << "Exiting MovieConfiguration::~MovieConfiguration()";
}

MovieConfiguration::MovieConfiguration()
    : QObject(nullptr)
{
    qDebug() << "Entering MovieConfiguration::MovieConfiguration()";
    qDebug() << "Exiting MovieConfiguration::MovieConfiguration()";
}

#ifdef SQL_TEST
static void _backend_test()
{
    qDebug() << "Entering _backend_test()";
    auto &mc = MovieConfiguration::get();
    qDebug() << "MovieConfiguration instance obtained.";
    mc.updateUrl(QUrl("movie1"), "sub-delay", -2.5);
    qDebug() << "Updated movie1 sub-delay to -2.5.";
    mc.updateUrl(QUrl("movie1"), "sub-delay", 1.5);
    qDebug() << "Updated movie1 sub-delay to 1.5.";
    mc.updateUrl(QUrl("movie2"), "sub-delay", 1.0);
    qDebug() << "Updated movie2 sub-delay to 1.0.";
    mc.updateUrl(QUrl("movie1"), "volume", 20);
    qDebug() << "Updated movie1 volume to 20.";

    auto res = mc.queryByUrl(QUrl("movie1"));
    qDebug() << "Queried movie1. Result size:" << res.size();
    Q_ASSERT (res.size() == 2);
    qInfo() << res;

    mc.removeUrl(QUrl("movie1"));
    qDebug() << "Removed movie1.";
    mc.updateUrl(QUrl("movie1"), "volume", 30);
    qDebug() << "Updated movie1 volume to 30.";
    mc.updateUrl(QUrl("movie2"), "volume", 40);
    qDebug() << "Updated movie2 volume to 40.";

    res = mc.queryByUrl(QUrl("movie1"));
    qDebug() << "Queried movie1 again. Result size:" << res.size();
    Q_ASSERT (res.size() == 1);
    qInfo() << res;
    mc.clear();
    qDebug() << "Cleared all movie configurations.";
    qDebug() << "Exiting _backend_test()";
}
#endif

void MovieConfiguration::init()
{
    qDebug() << "Initializing MovieConfiguration";
    _backend = new MovieConfigurationBackend(this);
#ifdef SQL_TEST
    qDebug() << "Running SQL tests";
    _backend_test();
#endif
    qDebug() << "MovieConfiguration initialized";
}

}
