// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ddlog.h"
#include "movie_configuration.h"
#include "utils.h"

#include <QtSql>
#include <atomic>

namespace dmr {
static std::atomic<MovieConfiguration *> _instance { nullptr };
static QMutex _instLock;

#define CHECKED_EXEC(q) do { \
    if (!(q).exec()) { \
        qCCritical(dmrLib) << (q).lastError(); \
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
        qCDebug(dmrLib) << "Initializing MovieConfigurationBackend";
        auto db_dir = QString("%1/%2/%3")
                      .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                      .arg(qApp->organizationName())
                      .arg(qApp->applicationName());

        QDir d;
        d.mkpath(db_dir);
        qCDebug(dmrLib) << "Database directory created:" << db_dir;

        auto db_path = QString("%1/movies.db").arg(db_dir);
        qCDebug(dmrLib) << "Database path:" << db_path;
        
        _db = QSqlDatabase::addDatabase("QSQLITE");
        _db.setDatabaseName(db_path);
        if(!_db.open()) {
            qCCritical(dmrLib) << "Failed to open movies database:" << _db.lastError().text();
            return;
        }
        qCDebug(dmrLib) << "Database opened successfully";

        auto ts = _db.tables(QSql::Tables);
        if (!ts.contains("urls") || !ts.contains("infos")) {
            qCDebug(dmrLib) << "Creating database tables";
            QSqlQuery q(_db);
            if (!q.exec("create table if not exists urls (url TEXT primary key, "
                        "md5 TEXT, timestamp DATETIME)")) {
                qCCritical(dmrLib) << "Failed to create urls table:" << q.lastError().text();
            }

            if (!q.exec("create table if not exists infos (url TEXT, "
                        "key TEXT, value BLOB, primary key (url, key))")) {
                qCCritical(dmrLib) << "Failed to create infos table:" << q.lastError().text();
            }
            qCDebug(dmrLib) << "Database tables created";
        }
        qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::MovieConfigurationBackend()";
    }

    void deleteUrl(const QUrl &url)
    {
        qCDebug(dmrLib) << "Deleting URL:" << url.toString();
        if(_db.transaction()) {
            qCDebug(dmrLib) << "Transaction started for deleteUrl.";
            QSqlQuery q(_db);
            if(q.prepare("delete from infos where url = ?")) {
                q.addBindValue(url);
                if (!q.exec()) {
                    qCWarning(dmrLib) << "Failed to delete from infos table:" << q.lastError().text();
                    if(!_db.commit()) {
                        qCCritical(dmrLib) << "Failed to commit transaction:" << _db.lastError().text();
                    }
                    qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::deleteUrl() with infos delete failure.";
                    return;
                }
            }

            if (q.numRowsAffected() > 0) {
                qCDebug(dmrLib) << "Rows affected in infos table > 0, attempting to delete from urls table.";
                QSqlQuery q_l(_db);
                if(q_l.prepare("delete from urls where url = ?")) {
                    q_l.addBindValue(url);
                    CHECKED_EXEC(q_l);
                }
            }
            qCDebug(dmrLib) << "URL deleted successfully";
        }
    }

    bool urlExists(const QUrl &url)
    {
        qCDebug(dmrLib) << "Checking if URL exists:" << url.toString();
        QSqlQuery q(_db);
        if(q.prepare("select url from urls where url = ? limit 1")) {
            q.addBindValue(url);
            CHECKED_EXEC(q);
        }

        bool exists = q.first();
        qCDebug(dmrLib) << "URL exists:" << exists;
        return exists;
    }

    void clear()
    {
        qCDebug(dmrLib) << "Clearing all database entries";
        if(_db.transaction()) {
            qCDebug(dmrLib) << "Transaction started for clear.";
            QSqlQuery q(_db);
            if (q.exec("delete from infos")) {
                qCDebug(dmrLib) << "Deleted from infos table.";
                if (q.exec("delete from urls")) {
                    qCDebug(dmrLib) << "Deleted from urls table.";
                    if(!_db.commit()) {
                        qCCritical(dmrLib) << "Failed to commit clear transaction:" << _db.lastError().text();
                    }
                    qCDebug(dmrLib) << "Database cleared successfully";
                    return;
                }
            }

            if(!_db.rollback()) {
                qCCritical(dmrLib) << "Failed to rollback clear transaction:" << _db.lastError().text();
            }
        }
        qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::clear() with potential failure or rollback.";
    }

    void updateUrl(const QUrl &url, const QString &key, const QVariant &val)
    {
        qCDebug(dmrLib) << "Updating URL:" << url.toString() << "Key:" << key << "Value:" << val.toString();

        if(_db.transaction()) {
            qCDebug(dmrLib) << "Transaction started for updateUrl.";
            if (!urlExists(url)) {
                qCDebug(dmrLib) << "URL does not exist, inserting new URL.";
                QString md5;
                if (url.isLocalFile()) {
                    md5 = utils::FastFileHash(QFileInfo(url.toLocalFile()));
                    qCDebug(dmrLib) << "Local file MD5:" << md5;
                } else {
                    md5 = QString(QCryptographicHash::hash(url.toString().toUtf8(), QCryptographicHash::Md5).toHex());
                    qCDebug(dmrLib) << "Remote URL MD5:" << md5;
                }

                QSqlQuery q(_db);
                if(q.prepare("insert into urls (url, md5, timestamp) values (?, ?, ?)")) {
                    q.addBindValue(url);
                    q.addBindValue(md5);
                    q.addBindValue(QDateTime::currentDateTimeUtc());
                    if (!q.exec()) {
                        qCWarning(dmrLib) << "Failed to insert URL:" << q.lastError().text();
                        if(!_db.rollback()) {
                            qCCritical(dmrLib) << "Failed to rollback URL insert:" << _db.lastError().text();
                        }
                        qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::updateUrl() with URL insert failure.";
                        return;
                    }
                }
            } else {
                qCDebug(dmrLib) << "URL already exists, skipping URL insert.";
            }

            QSqlQuery q(_db);
            if(q.prepare("replace into infos (url, key, value) values (?, ?, ?)")) {
                q.addBindValue(url);
                q.addBindValue(key);
                q.addBindValue(val);
                CHECKED_EXEC(q);
                if(!_db.commit()) {
                    qCCritical(dmrLib) << "Failed to commit update transaction:" << _db.lastError().text();
                }
                qCDebug(dmrLib) << "URL updated successfully";
            } else {
                qCWarning(dmrLib) << "Failed to prepare replace infos query:" << q.lastError().text();
            }
        } else {
            qCWarning(dmrLib) << "Failed to start transaction for updateUrl.";
        }
        qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::updateUrl()";
    }

    QVariant queryValueByUrlKey(const QUrl &url, const QString &key)
    {
        qCDebug(dmrLib) << "Querying value for URL:" << url.toString() << "Key:" << key;
        if (!urlExists(url)) {
            qCDebug(dmrLib) << "URL does not exist";
            return {};
        }

        QSqlQuery q(_db);
        if(q.prepare("select value from infos where url = ? and key = ?")) {
            q.addBindValue(url);
            q.addBindValue(key);
            CHECKED_EXEC(q);

            if (q.next()) {
                QVariant value = q.value(0);
                qCDebug(dmrLib) << "Value found:" << value.toString();
                qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::queryValueByUrlKey() with value:" << value.toString();
                return value;
            } else {
                qCDebug(dmrLib) << "No value found for URL and key.";
            }
        } else {
            qCWarning(dmrLib) << "Failed to prepare queryValueByUrlKey query:" << q.lastError().text();
        }

        qCDebug(dmrLib) << "No value found";
        qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::queryValueByUrlKey() with empty QVariant.";
        return QVariant();
    }

    QMap<QString, QVariant> queryByUrl(const QUrl &url)
    {
        qCDebug(dmrLib) << "Querying all values for URL:" << url.toString();
        if (!urlExists(url)) {
            qCDebug(dmrLib) << "URL does not exist, returning empty QMap.";
            return {};
        }

        QSqlQuery q(_db);
        if(q.prepare("select key, value from infos where url = ?")) {
            q.addBindValue(url);
            CHECKED_EXEC(q);
        } else {
            qCWarning(dmrLib) << "Failed to prepare queryByUrl query:" << q.lastError().text();
        }

        QMap<QString, QVariant> res;
        while (q.next()) {
            res.insert(q.value(0).toString(), q.value(1));
            qCDebug(dmrLib) << "Found key:" << q.value(0).toString() << "value:" << q.value(1).toString();
        }

        qCDebug(dmrLib) << "Found" << res.size() << "values";
        return res;
    }

    ~MovieConfigurationBackend();

private:
    QSqlDatabase _db;
};

MovieConfigurationBackend::~MovieConfigurationBackend()
{
    qCDebug(dmrLib) << "Destroying MovieConfigurationBackend";
    _db.close();
    QSqlDatabase::removeDatabase(_db.connectionName());
    qCDebug(dmrLib) << "Exiting MovieConfigurationBackend::~MovieConfigurationBackend()";
}

MovieConfiguration &MovieConfiguration::get()
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::get()";
    if (_instance == nullptr) {
        QMutexLocker lock(&_instLock);
        qCDebug(dmrLib) << "_instance is nullptr, acquiring lock and creating new instance.";
        _instance = new MovieConfiguration;
        qCDebug(dmrLib) << "Created new MovieConfiguration instance";
    } else {
        qCDebug(dmrLib) << "_instance already exists.";
    }
    qCDebug(dmrLib) << "Exiting MovieConfiguration::get() with instance:" << _instance;
    return *_instance;
}

void MovieConfiguration::removeUrl(const QUrl &url)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::removeUrl() for URL:" << url.toString();
    _backend->deleteUrl(url);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::removeUrl()";
}

bool MovieConfiguration::urlExists(const QUrl &url)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::urlExists() for URL:" << url.toString();
    bool exists = _backend->urlExists(url);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::urlExists() with result:" << exists;
    return exists;
}

void MovieConfiguration::clear()
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::clear()";
    _backend->clear();
    qCDebug(dmrLib) << "Exiting MovieConfiguration::clear()";
}

void MovieConfiguration::updateUrl(const QUrl &url, const QString &key, const QVariant &val)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::updateUrl(const QUrl&, const QString&, const QVariant&) for URL:" << url.toString() << ", Key:" << key << ", Value:" << val.toString();
    _backend->updateUrl(url, key, val);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::updateUrl(const QUrl&, const QString&, const QVariant&)";
}

void MovieConfiguration::updateUrl(const QUrl &url, KnownKey key, const QVariant &val)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::updateUrl(const QUrl&, KnownKey, const QVariant&) for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val.toString();
    updateUrl(url, knownKey2String(key), val);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::updateUrl(const QUrl&, KnownKey, const QVariant&)";
}

void MovieConfiguration::append2ListUrl(const QUrl &url, KnownKey key, const QString &val)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::append2ListUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto list = getByUrl(url, knownKey2String(key)).toString().split(';', QString::SkipEmptyParts);
    qCDebug(dmrLib) << "Using Qt5 split with QString::SkipEmptyParts.";
#else
    // Qt6 中使用 Qt::SkipEmptyParts 而不是 QString::SkipEmptyParts
    auto list = getByUrl(url, knownKey2String(key)).toString().split(';', Qt::SkipEmptyParts);
    qCDebug(dmrLib) << "Using Qt6 split with Qt::SkipEmptyParts.";
#endif
    auto bytes = val.toUtf8().toBase64();
    qCDebug(dmrLib) << "Value encoded to Base64:" << bytes;
    list.append(bytes);
    qCDebug(dmrLib) << "Appended encoded value to list. New list size:" << list.size();
    updateUrl(url, key, list.join(';'));
    qCDebug(dmrLib) << "List joined and updated in URL.";
    qCDebug(dmrLib) << "Exiting MovieConfiguration::append2ListUrl()";
}

void MovieConfiguration::removeFromListUrl(const QUrl &url, KnownKey key, const QString &val)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::removeFromListUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key << ", Value:" << val;
    ///add for warning by xxj ,no any means
    //val.isNull();
    auto list = getListByUrl(url, key);
}

QString MovieConfiguration::knownKey2String(KnownKey kk)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::knownKey2String() with KnownKey:" << (int)kk;
    switch (kk) {
    case KnownKey::SubDelay:
        qCDebug(dmrLib) << "KnownKey is SubDelay.";
        return "sub-delay";
    case KnownKey::SubCodepage:
        qCDebug(dmrLib) << "KnownKey is SubCodepage.";
        return "sub-codepage";
    case KnownKey::SubId:
        qCDebug(dmrLib) << "KnownKey is SubId.";
        return "sid";
    case KnownKey::StartPos:
        qCDebug(dmrLib) << "KnownKey is StartPos.";
        return "start";
    case KnownKey::ExternalSubs:
        qCDebug(dmrLib) << "KnownKey is ExternalSubs.";
        return "external-subs";
    default:
        qCDebug(dmrLib) << "KnownKey is default (unknown).";
        return "";
    }
    qCDebug(dmrLib) << "Exiting MovieConfiguration::knownKey2String()";
}

QStringList MovieConfiguration::getListByUrl(const QUrl &url, KnownKey key)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::getListByUrl() for URL:" << url.toString() << ", KnownKey:" << (int)key;
    QStringList list = decodeList(getByUrl(url, knownKey2String(key)));
    qCDebug(dmrLib) << "Exiting MovieConfiguration::getListByUrl() with list size:" << list.size();
    return list;
}

QStringList MovieConfiguration::decodeList(const QVariant &val)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::decodeList() with QVariant:" << val.toString();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto list = val.toString().split(';', QString::SkipEmptyParts);
    qCDebug(dmrLib) << "Using Qt5 split for decoding list.";
#else
    // Qt6 中使用 Qt::SkipEmptyParts 而不是 QString::SkipEmptyParts
    auto list = val.toString().split(';', Qt::SkipEmptyParts);
    qCDebug(dmrLib) << "Using Qt6 split for decoding list.";
#endif
    std::transform(list.begin(), list.end(), list.begin(), [](const QString & s) {
        qCDebug(dmrLib) << "Transforming string:" << s;
        return QByteArray::fromBase64(s.toUtf8());
    });

    qCDebug(dmrLib) << "Exiting MovieConfiguration::decodeList() with list size:" << list.size();
    return list;
}

QVariant MovieConfiguration::getByUrl(const QUrl &url, const QString &key)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::getByUrl(const QUrl&, const QString&) for URL:" << url.toString() << ", Key:" << key;
    QVariant value = _backend->queryValueByUrlKey(url, key);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::getByUrl(const QUrl&, const QString&) with value:" << value.toString();
    return value;
}

QVariant MovieConfiguration::getByUrl(const QUrl &url, KnownKey key)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::getByUrl(const QUrl&, KnownKey) for URL:" << url.toString() << ", KnownKey:" << (int)key;
    QVariant value = getByUrl(url, knownKey2String(key));
    qCDebug(dmrLib) << "Exiting MovieConfiguration::getByUrl(const QUrl&, KnownKey) with value:" << value.toString();
    return value;
}

QMap<QString, QVariant> MovieConfiguration::queryByUrl(const QUrl &url)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::queryByUrl() for URL:" << url.toString();
    QMap<QString, QVariant> result = _backend->queryByUrl(url);
    qCDebug(dmrLib) << "Exiting MovieConfiguration::queryByUrl() with result size:" << result.size();
    return result;
}

MovieConfiguration::~MovieConfiguration()
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::~MovieConfiguration()";
    delete _backend;
    qCDebug(dmrLib) << "Exiting MovieConfiguration::~MovieConfiguration()";
}

MovieConfiguration::MovieConfiguration()
    : QObject(nullptr)
{
    qCDebug(dmrLib) << "Entering MovieConfiguration::MovieConfiguration()";
    qCDebug(dmrLib) << "Exiting MovieConfiguration::MovieConfiguration()";
}

#ifdef SQL_TEST
static void _backend_test()
{
    qCDebug(dmrLib) << "Entering _backend_test()";
    auto &mc = MovieConfiguration::get();
    qCDebug(dmrLib) << "MovieConfiguration instance obtained.";
    mc.updateUrl(QUrl("movie1"), "sub-delay", -2.5);
    qCDebug(dmrLib) << "Updated movie1 sub-delay to -2.5.";
    mc.updateUrl(QUrl("movie1"), "sub-delay", 1.5);
    qCDebug(dmrLib) << "Updated movie1 sub-delay to 1.5.";
    mc.updateUrl(QUrl("movie2"), "sub-delay", 1.0);
    qCDebug(dmrLib) << "Updated movie2 sub-delay to 1.0.";
    mc.updateUrl(QUrl("movie1"), "volume", 20);
    qCDebug(dmrLib) << "Updated movie1 volume to 20.";

    auto res = mc.queryByUrl(QUrl("movie1"));
    qCDebug(dmrLib) << "Queried movie1. Result size:" << res.size();
    Q_ASSERT (res.size() == 2);
    qCInfo(dmrLib) << res;

    mc.removeUrl(QUrl("movie1"));
    qCDebug(dmrLib) << "Removed movie1.";
    mc.updateUrl(QUrl("movie1"), "volume", 30);
    qCDebug(dmrLib) << "Updated movie1 volume to 30.";
    mc.updateUrl(QUrl("movie2"), "volume", 40);
    qCDebug(dmrLib) << "Updated movie2 volume to 40.";

    res = mc.queryByUrl(QUrl("movie1"));
    qCDebug(dmrLib) << "Queried movie1 again. Result size:" << res.size();
    Q_ASSERT (res.size() == 1);
    qCInfo(dmrLib) << res;
    mc.clear();
    qCDebug(dmrLib) << "Cleared all movie configurations.";
    qCDebug(dmrLib) << "Exiting _backend_test()";
}
#endif

void MovieConfiguration::init()
{
    qCDebug(dmrLib) << "Initializing MovieConfiguration";
    _backend = new MovieConfigurationBackend(this);
#ifdef SQL_TEST
    qCDebug(dmrLib) << "Running SQL tests";
    _backend_test();
#endif
    qCDebug(dmrLib) << "MovieConfiguration initialized";
}

}
