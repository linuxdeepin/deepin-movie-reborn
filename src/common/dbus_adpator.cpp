#include "dbus_adpator.h"

ApplicationAdaptor::ApplicationAdaptor(MainWindow* mw)
    :QDBusAbstractAdaptor(mw), _mw(mw) 
{
}

void ApplicationAdaptor::openFiles(const QStringList& list)
{
    _mw->playList(list);
}

void ApplicationAdaptor::openFile(const QString& file) 
{
    QRegExp url_re("\\w+://");

    QUrl url;
    if (url_re.indexIn(file) == 0) {
        url = QUrl(file);
    } else {
        url = QUrl::fromLocalFile(file);
    }
    _mw->play(url);
}

