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
#ifndef _DMR_MOVIE_CONFIGURATION_H
#define _DMR_MOVIE_CONFIGURATION_H 

#include <QtCore>

namespace dmr 
{
class MovieConfigurationBackend;


class MovieConfiguration: public QObject 
{
    Q_OBJECT
public:
    enum KnownKey {
        SubDelay,
        SubCodepage,
        SubId,
        StartPos,
        ExternalSubs
    };

    static MovieConfiguration& get();
    void init(); // call once

    void removeUrl(const QUrl& url);
    void clear();
    bool urlExists(const QUrl& url);
    void updateUrl(const QUrl& url, const QString& key, const QVariant& val);
    void updateUrl(const QUrl& url, KnownKey key, const QVariant& val);
    //helper for update list type entries
    void append2ListUrl(const QUrl& url, KnownKey key, const QString& val);
    void removeFromListUrl(const QUrl& url, KnownKey key, const QString& val);

    //list all settings for url
    QMap<QString, QVariant> queryByUrl(const QUrl& url);

    QVariant getByUrl(const QUrl& url, const QString& key);
    QVariant getByUrl(const QUrl& url, KnownKey key);
    //helper for get list type entries
    QStringList getListByUrl(const QUrl& url, KnownKey key);

    //helper
    QStringList decodeList(const QVariant& val);

    ~MovieConfiguration();
    static QString knownKey2String(KnownKey kk);

private:
    MovieConfiguration();

    MovieConfigurationBackend* _backend {nullptr};
};

using ConfigKnownKey = MovieConfiguration::KnownKey;
}

#endif /* ifndef _DMR_MOVIE_CONFIGURATION_H */

