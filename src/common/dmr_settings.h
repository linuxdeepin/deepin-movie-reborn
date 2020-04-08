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
#ifndef _DMR_SETTINGS_H
#define _DMR_SETTINGS_H

#include <QObject>
#include <QPointer>

#include <DSettingsOption>
#include <DSettingsGroup>
#include <DSettings>

namespace dmr {
using namespace Dtk::Core;

class Settings: public QObject
{
    Q_OBJECT
public:
    enum Flag {
        ClearWhenQuit,
        ShowThumbnailMode,
        ResumeFromLast,
        AutoSearchSimilar,
        PreviewOnMouseover,
        MultipleInstance,
        PauseOnMinimize,
        HWAccel,
    };

    static Settings &get();
    QString configPath() const
    {
        return _configPath;
    }
    QPointer<DSettings> settings()
    {
        return _settings;
    }

    QPointer<DSettingsGroup> group(const QString &name)
    {
        return settings()->group(name);
    }
    QPointer<DSettingsGroup> shortcuts()
    {
        return group("shortcuts");
    }
    QPointer<DSettingsGroup> base()
    {
        return group("base");
    }
    QPointer<DSettingsGroup> subtitle()
    {
        return group("subtitle");
    }

    void setGeneralOption(const QString &opt, const QVariant &v);
    QVariant generalOption(const QString &opt);

    void setInternalOption(const QString &opt, const QVariant &v);
    QVariant internalOption(const QString &opt);

    // user override for mpv opengl interop
    QString forcedInterop();
    // disable interop at all
    bool disableInterop();

    // convient helpers

    bool isSet(Flag f) const;

    QStringList commonPlayableProtocols() const;
    bool iscommonPlayableProtocol(const QString &scheme) const;

    QString screenshotLocation();
    QString screenshotNameTemplate();
    QString screenshotNameSeqTemplate();

signals:
    void shortcutsChanged(const QString &, const QVariant &);
    void baseChanged(const QString &, const QVariant &);
    void subtitleChanged(const QString &, const QVariant &);
    void defaultplaymodechanged(const QString &, const QVariant &);
    void baseMuteChanged(const QString &, const QVariant &);

private:
    Settings();

    QPointer<DSettings> _settings;
    QString _configPath;
};

}

#endif /* ifndef _DMR_SETTINGS_H */
