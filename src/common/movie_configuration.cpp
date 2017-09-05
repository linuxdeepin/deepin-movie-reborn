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
        auto db_path = QString("%1/%2/%3/movies.db")
            .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
            .arg(qApp->organizationName())
            .arg(qApp->applicationName());
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

QString MovieConfiguration::knownKey2String(KnownKey kk)
{
    switch (kk) {
        case KnownKey::SubDelay: return "sub-delay";
        case KnownKey::SubCodepage: return "sub-codepage";
        default: return "";
    }
}

QVariant MovieConfiguration::getByUrl(const QUrl& url, const QString& key)
{
    auto res = queryByUrl(key);
    if (res.contains(key)) {
        return res[key];
    }

    return QVariant();
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
