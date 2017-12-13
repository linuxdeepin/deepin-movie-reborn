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
#include <QtCore>
#include <QtGui>

#include "dmr_settings.h"
#include <qsettingbackend.h>

namespace dmr {
using namespace Dtk::Core;
static Settings* _theSettings = nullptr;

Settings& Settings::get() 
{
    if (!_theSettings) {
        _theSettings = new Settings;
    }

    return *_theSettings;
}

Settings::Settings()
    : QObject(0) 
{
    _configPath = QString("%1/%2/%3/config.conf")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    qDebug() << "configPath" << _configPath;
    auto backend = new QSettingBackend(_configPath);

    _settings = DSettings::fromJsonFile(":/resources/data/settings.json");
    _settings->setBackend(backend);

    connect(_settings, &DSettings::valueChanged,
            [=](const QString& key, const QVariant& value) {
                if (key.startsWith("shortcuts."))
                    emit shortcutsChanged(key, value);
                else if (key.startsWith("base."))
                    emit baseChanged(key, value);
                else if (key.startsWith("subtitle."))
                    emit subtitleChanged(key, value);
            });

    //qDebug() << "keys" << _settings->keys();

    QFontDatabase fontDatabase;
    auto fontFamliy = _settings->option("subtitle.font.family");
    fontFamliy->setData("items", fontDatabase.families());
    //fontFamliy->setValue(0);
}

static QString flag2key(Settings::Flag f)
{
    switch(f) {
        case Settings::Flag::ClearWhenQuit: return "emptylist";
        case Settings::Flag::ResumeFromLast: return "resumelast";
        case Settings::Flag::AutoSearchSimilar: return "addsimilar";
        case Settings::Flag::PreviewOnMouseover: return "mousepreview";
        case Settings::Flag::MultipleInstance: return "multiinstance";
        case Settings::Flag::PauseOnMinimize: return "pauseonmin";
        case Settings::Flag::HWAccel: return "hwaccel";
    }

}

bool Settings::isSet(Flag f) const
{
    auto subgroups = _settings->group("base")->childGroups();
    auto grp = std::find_if(subgroups.begin(), subgroups.end(), [=](GroupPtr grp) {
        return grp->key() == "base.play";
    });

    if (grp != subgroups.end()) {
        auto sub = (*grp)->childOptions();

        auto key = flag2key(f);
        auto p = std::find_if(sub.begin(), sub.end(), [=](OptionPtr opt) { 
                auto sk = opt->key();
                sk.remove(0, sk.lastIndexOf('.') + 1);
                return sk == key; 
            });
        
        return p != sub.end() && (*p)->value().toBool();
    }

    return false;
}

QStringList Settings::commonPlayableProtocols() const
{
    //from mpv and combined with stream media protocols
    return {
        "http", "https", "bd", "ytdl", "smb", "dvd", "dvdread", "tv", "pvr", 
            "dvb", "cdda", "lavf", "av", "avdevice", "fd", "fdclose", "edl",
            "mf", "null", "memory", "hex", "rtmp", "rtsp", "hls", "mms", "rtp", 
            "rtcp"
    };
}

bool Settings::iscommonPlayableProtocol(const QString& scheme) const
{
    for (auto pro: commonPlayableProtocols()) {
        if (pro == scheme) 
            return true;
    }

    return false;
}

QString Settings::screenshotLocation()
{
    QString save_path = settings()->value("base.screenshot.location").toString();
    if (save_path.size() && save_path[0] == '~') {
        save_path.replace(0, 1, QDir::homePath());
    }

    if (!QFileInfo(save_path).exists()) {
        QDir d;
        d.mkpath(save_path);
    }

    return save_path;
}

QString Settings::screenshotNameTemplate()
{
    return tr("%1/DMovie%2.jpg").arg(screenshotLocation())
        .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss"));
}

QString Settings::screenshotNameSeqTemplate()
{
    return tr("%1/DMovie%2(%3).jpg").arg(screenshotLocation())
        .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss"));
}

QVariant Settings::internalOption(const QString& opt)
{
    return settings()->getOption(QString("base.play.%1").arg(opt));
}

void Settings::setInternalOption(const QString& opt, const QVariant& v)
{
    settings()->setOption(QString("base.play.%1").arg(opt), v);
    settings()->sync();
}
}

