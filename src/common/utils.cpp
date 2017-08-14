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

QFileInfoList FindSimilarFiles(const QFileInfo& fi)
{
    QFileInfoList fil;

    QDirIterator it(fi.absolutePath());
    while (it.hasNext()) {
        it.next();
        if (!it.fileInfo().isFile()) {
            continue;
        }

        auto dist = stringDistance(fi.fileName(), it.fileInfo().fileName());
        if (dist > 0 && dist <= 4) { //TODO: check ext.
            fil.append(it.fileInfo());
            qDebug() << it.fileInfo().fileName() << "=" << dist;
        }
        
    }

    //sort names by digits inside, take care of such a possible:
    //S01N04, S02N05, S01N12, S02N04, etc...
    struct {
        bool operator()(const QFileInfo& fi1, const QFileInfo& fi2) const {
            auto fileName1 = fi1.fileName();
            auto fileName2 = fi2.fileName();

            QRegExp rd("\\d+");
            int pos = 0;
            while ((pos = rd.indexIn(fileName1, pos)) != -1) {
                auto id1 = fileName1.midRef(pos, rd.matchedLength());

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

                pos += rd.matchedLength();
            }
            return fileName1.localeAwareCompare(fileName2) < 0;
        }
    } SortByDigits;
    std::sort(fil.begin(), fil.end(), SortByDigits);
    
    return fil;
}

}
}
