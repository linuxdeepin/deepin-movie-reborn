#ifndef _DMR_SETTINGS_H
#define _DMR_SETTINGS_H 

#include <QObject>
#include <QPointer>

#include <DSettingsOption>
#include <DSettingsGroup>
#include <DSettings>

namespace dmr {
using namespace Dtk::Core;

class Settings: public QObject {
    Q_OBJECT
    public:
        static Settings& get();
        QString configPath() const { return _configPath; }
        QPointer<DSettings> settings() { return _settings; }
        
        QPointer<DSettingsGroup> group(const QString& name) {
            return settings()->group(name);
        }
        QPointer<DSettingsGroup> shortcuts() { return group("shortcuts"); }
        QPointer<DSettingsGroup> base() { return group("base"); }
        QPointer<DSettingsGroup> subtitle() { return group("subtitle"); }

    signals:
        void shortcutsChanged(const QString&, const QVariant&);
        void baseChanged(const QString&, const QVariant&);

    private:
        Settings();

        QPointer<DSettings> _settings;
        QString _configPath;
};

}

#endif /* ifndef _DMR_SETTINGS_H */
