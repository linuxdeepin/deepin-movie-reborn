#include <QtCore>
#include <QtGui>

#include "dmr_settings.h"

#include <qsettingbackend.h>
#include <option.h>
#include <group.h>
#include <settings.h>

namespace dmr {
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
    auto backend = new Dtk::QSettingBackend(_configPath);

    _settings = Dtk::Settings::fromJsonFile(":/resources/data/settings.json");
    _settings->setBackend(backend);

    connect(_settings, &Dtk::Settings::valueChanged,
            [=](const QString& key, const QVariant& value) {
                qDebug() << key << value;
            });

    QFontDatabase fontDatabase;
    auto fontFamliy = _settings->option("subtitle.font.family");
    fontFamliy->setData("items", fontDatabase.families());
    fontFamliy->setValue(0);
}

}

