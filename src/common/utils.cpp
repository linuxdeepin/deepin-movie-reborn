#include "utils.h"
#include <QtDBus>
#include <QtWidgets>

namespace dmr {
namespace utils {

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

static int minDistance(QString s1, QString s2) 
{
    int n = s1.size(), m = s2.size();
    if (!n || !m) return std::max(n, m);
    std::vector<std::vector<int>> dp(n+1, std::vector<int>(m+1, 0)); // augmented
    for (int i = 0; i <= m; i++) dp[0][i] = i;
    for (int i = 0; i <= n; i++) dp[i][0] = i;  

    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            if (s1[i-1] == s2[j-1]) {
                dp[i][j] = dp[i-1][j-1];
            } else 
                dp[i][j] = min(dp[i-1][j], dp[i][j-1], dp[i-1][j-1]) + 1; 
        }
    }

    return dp[n][m];
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

        auto dist = minDistance(fi.fileName(), it.fileInfo().fileName());
        qDebug() << it.fileInfo().fileName() << "=" << dist;
        if (dist > 0 && dist < 4) { //TODO: check ext.
            fil.append(it.fileInfo());
        }
        
    }

    return fil;
}

}
}
