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
#ifndef _DMR_ONLINE_SUB_H
#define _DMR_ONLINE_SUB_H 

#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>


namespace dmr {
struct ShooterSubtitleMeta {
    int id; 
    int delay;
    QString desc;
    QString ext;
    QString link; // url to download
    QString local; // saved position when downloaded
};

class OnlineSubtitle: public QObject {
    Q_OBJECT
public:
    enum FailReason {
        NoError,
        NetworkError,
        NoSubFound,
        Duplicated,  // the same hash with local cache
    };

    static OnlineSubtitle& get();
    QString storeLocation();

public slots:
    void requestSubtitle(const QUrl& url);

private slots:
    void replyReceived(QNetworkReply*);  
    void downloadSubtitles();

signals:
    void subtitlesDownloadedFor(const QUrl& url, const QList<QString>& filenames, FailReason r);

private:
    QString _defaultLocation;
    QNetworkAccessManager *_nam {nullptr};

    int _pendingDownloads {0}; // this should equal to _subs.size() basically
    QList<ShooterSubtitleMeta> _subs;
    QFileInfo _lastReqVideo;
    FailReason _lastReason {NoError};

    OnlineSubtitle();
    void subtitlesDownloadComplete();
    QString findAvailableName(const QString& tmpl, int id);
    bool hasHashConflict(const QString& path, const QString& tmpl); 
};
}

#endif /* ifndef _DMR_ONLINE_SUB_H */
