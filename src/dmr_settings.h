#ifndef _DMR_SETTINGS_H
#define _DMR_SETTINGS_H 

#include <QObject>
#include <QPointer>
#include <settings.h>

namespace dmr {
class Settings: public QObject {
    Q_OBJECT
    public:
        static Settings& get();
        QString configPath() const { return _configPath; }
        QPointer<Dtk::Settings> settings() { return _settings; }

    private:
        Settings();

        QPointer<Dtk::Settings> _settings;
        QString _configPath;
};

}

#endif /* ifndef _DMR_SETTINGS_H */
