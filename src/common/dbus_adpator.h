#ifndef _DMR_DBUS_ADAPTOR
#define _DMR_DBUS_ADAPTOR 

#include <QtDBus>

#include "mainwindow.h"

using namespace dmr;

class ApplicationAdaptor: public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.deepin.movie")
public:
    ApplicationAdaptor(MainWindow* mw);

public slots:
    void openFile(const QString& url);

private:
    MainWindow *_mw {nullptr};
};


#endif /* ifndef _DMR_DBUS_ADAPTOR */

