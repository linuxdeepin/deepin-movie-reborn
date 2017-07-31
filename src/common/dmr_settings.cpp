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
                    emit baseChanged(key, value);
            });

    //qDebug() << "keys" << _settings->keys();

    QFontDatabase fontDatabase;
    auto fontFamliy = _settings->option("subtitle.font.family");
    fontFamliy->setData("items", fontDatabase.families());
    fontFamliy->setValue(0);
}

static QString flag2key(Settings::Flag f)
{
    switch(f) {
        case Settings::Flag::ClearWhenQuit: return "emptylist";
        case Settings::Flag::ResumeFromLast: return "resumelast";
        case Settings::Flag::AutoSearchSimilar: return "contnext";
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
        std::for_each(sub.begin(), sub.end(), [=](OptionPtr opt) {
                qDebug() << opt->key() << opt->name();
            });

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

}

