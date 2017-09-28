#include "utils.h"
#include <QtDBus>
#include <QtWidgets>

namespace dmr {
namespace utils {
using namespace std;

void ShowInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }

    QUrl url = QUrl::fromLocalFile(QFileInfo(path).dir().absolutePath());
    QUrlQuery query;
    query.addQueryItem("selectUrl", QUrl::fromLocalFile(path).toString());
    url.setQuery(query);

    qDebug() << __func__ << url.toString();

    // Try dde-file-manager
    QProcess *fp = new QProcess();
    QObject::connect(fp, SIGNAL(finished(int)), fp, SLOT(deleteLater()));
    fp->start("dde-file-manager", QStringList(url.toString()));
    fp->waitForStarted(3000);

    if (fp->error() == QProcess::FailedToStart) {
        // Start dde-file-manager failed, try nautilus
        QDBusInterface iface("org.freedesktop.FileManager1",
                "/org/freedesktop/FileManager1",
                "org.freedesktop.FileManager1",
                QDBusConnection::sessionBus());
        if (iface.isValid()) {
            // Convert filepath to URI first.
            const QStringList uris = { QUrl::fromLocalFile(path).toString() };
            qDebug() << "freedesktop.FileManager";
            // StartupId is empty here.
            QDBusPendingCall call = iface.asyncCall("ShowItems", uris, "");
            Q_UNUSED(call);
        }
        // Try to launch other file manager if nautilus is invalid
        else {
            qDebug() << "desktopService::openUrl";
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).dir().absolutePath()));
        }
        fp->deleteLater();
    }
}


static int min(int v1, int v2, int v3) 
{
    return std::min(v1, std::min(v2, v3));
}

static int stringDistance(const QString& s1, const QString& s2) {
    int n = s1.size(), m = s2.size();
    if (!n || !m) return max(n, m);

    vector<int> dp(n+1);
    for (int i = 0; i < n+1; i++) dp[i] = i;
    int pred = 0;
    int curr = 0;

    for (int i = 0; i < m; i++) {
        dp[0] = i;
        pred = i+1;
        for (int j = 0; j < n; j++) {
            if (s1[j] == s2[i]) {
                curr = dp[j];
            } else {
                curr = min(dp[j], dp[j+1], pred) + 1;
            }
            dp[j] = pred;
            pred = curr;

        }
        dp[n] = pred;
    }

    return curr;
}

bool IsNamesSimilar(const QString& s1, const QString& s2)
{
    auto dist = stringDistance(s1, s2);
    return (dist >= 0 && dist <= 4); //TODO: check ext.
}

QFileInfoList FindSimilarFiles(const QFileInfo& fi)
{
    QFileInfoList fil;

    QDirIterator it(fi.absolutePath());
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isFile()) {
            continue;
        }

        if (IsNamesSimilar(fi.fileName(), it.fileInfo().fileName())) {
            fil.append(it.fileInfo());
        }
        
    }

    //struct {
        //bool operator()(const QFileInfo& fi1, const QFileInfo& fi2) const {
            //return CompareNames(fi1.fileName(), fi2.fileName());
        //}
    //} SortByDigits;
    //std::sort(fil.begin(), fil.end(), SortByDigits);
    return fil;
}

bool CompareNames(const QString& fileName1, const QString& fileName2) 
{
    static QRegExp rd("\\d+");
    int pos = 0;
    while ((pos = rd.indexIn(fileName1, pos)) != -1) {
        auto inc = rd.matchedLength();
        auto id1 = fileName1.midRef(pos, inc);

        auto pos2 = rd.indexIn(fileName2, pos);
        if (pos == pos2) {
            auto id2 = fileName2.midRef(pos, rd.matchedLength());
            //qDebug() << "id compare " << id1 << id2;
            if (id1 != id2) {
                bool ok1, ok2;
                bool v = id1.toInt(&ok1) < id2.toInt(&ok2);
                if (ok1 && ok2) return v;
                return id1.localeAwareCompare(id2) < 0;
            }
        }

        pos += inc;
    }
    return fileName1.localeAwareCompare(fileName2) < 0;
}

// hash the whole file takes amount of time, so just pick some areas to be hashed
QString FastFileHash(const QFileInfo& fi)
{
    auto sz = fi.size();
    QList<qint64> offsets = {
        4096,
        sz - 8192
    };

    QFile f(fi.absoluteFilePath());
    if (!f.open(QFile::ReadOnly)) {
        return QString();
    }

    if (fi.size() < 8192) {
        auto bytes = f.readAll();
        return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
    }

    QByteArray bytes;
    std::for_each(offsets.begin(), offsets.end(), [&bytes, &f](qint64 v) {
        f.seek(v);
        bytes += f.read(4096);

    });

    return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

// hash the entire file (hope file is small)
QString FullFileHash(const QFileInfo& fi)
{
    auto sz = fi.size();

    QFile f(fi.absoluteFilePath());
    if (!f.open(QFile::ReadOnly)) {
        return QString();
    }

    auto bytes = f.readAll();
    return QString(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry)
{
    QPixmap dest(pm.size());
    dest.fill(Qt::transparent);

    QPainter p(&dest);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);

    QPainterPath path;
    path.addRoundedRect(QRect(QPoint(), pm.size()), rx, ry);
    p.setClipPath(path);
    p.drawPixmap(0, 0, pm);

    return dest;
}

QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time)
{
    QPixmap dest(sz);
    dest.fill(Qt::transparent);

    QPainter p(&dest);
    p.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);

    p.setPen(QColor(0, 0, 0, 255 / 10));
    p.drawRoundedRect(dest.rect(), rx, ry);

    QPainterPath path;
    auto r = dest.rect().marginsRemoved({1, 1, 1, 1});
    path.addRoundedRect(r, rx, ry);
    p.setClipPath(path);
    p.drawPixmap(1, 1, pm);

    p.setPen(Qt::white);
    QFont ft;
    ft.setPixelSize(12);
    ft.setWeight(QFont::Medium);
    p.setFont(ft);

    auto tm_str = QTime(0, 0, 0).addSecs(time).toString("hh:mm:ss");
    QRect bounding = p.fontMetrics().boundingRect(tm_str);
    bounding.moveTopLeft({dest.width() - 5 - bounding.width(),
            dest.height() - 5 - bounding.height()});
    p.drawText(bounding, tm_str);

    return dest;
}

int InhibitStandby()
{
    QDBusInterface iface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
            "org.freedesktop.ScreenSaver");
    QDBusReply<int> reply = iface.call("Inhibit", "deepin-movie", "playing in fullscreen");
    if (reply.isValid()) {
        return reply.value();
    }
    return -1;
}

void UnInhibitStandby(int cookie)
{
    QDBusInterface iface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
            "org.freedesktop.ScreenSaver");
    iface.call("UnInhibit", cookie);
}

}
}
