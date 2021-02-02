#ifndef MOVIEAPP_H
#define MOVIEAPP_H

#include <QObject>
#include <Mpris>
#include <MprisPlayer>
#include "mainwindow.h"
#include "presenter.h"

using namespace dmr;

class MprisPlayer;
class MovieApp : public QObject
{
public:
    MovieApp(MainWindow* mw, QObject* parent = nullptr);

    void initUI();
    void initConnection();
    void initMpris(const QString &serviceName);
    void show();
public slots:
    void quit();

private:
    MainWindow* _mw;
    Presenter* _presenter = nullptr;

};

#endif // MOVIEAPP_H
