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

}

