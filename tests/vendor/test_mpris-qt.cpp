#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include "application.h"
#include <unistd.h>
#include "src/vendor/mpris-qt/mpris.h"
#include "src/vendor/mpris-qt/mprisplayer.h"
#include "src/vendor/mpris-qt/mprisplayer_p.h"
#include "mpriscontroller_p.h"
#include "mpriscontroller.h"
#include "mprismanager.h"

TEST(Mpris, MprisPlayer)
{
    MprisPlayer *mprisPlayer = new MprisPlayer();
    MprisRootAdaptor *mrAdaptor = new MprisRootAdaptor(mprisPlayer);

    mprisPlayer->setServiceName("services");
    QString serName = mprisPlayer->serviceName();

    mprisPlayer->setCanQuit(true);
    bool isCanQuite = mprisPlayer->canQuit();

    mprisPlayer->setCanRaise(true);
    bool isCanRaise = mprisPlayer->canRaise();

    mprisPlayer->setCanSetFullscreen(true);
    bool isCanSetFullscreen = mprisPlayer->canSetFullscreen();

    mprisPlayer->setDesktopEntry("desktop");
    QString desktopEntry = mprisPlayer->desktopEntry();

    mprisPlayer->setFullscreen(true);
    bool isFullscreen = mprisPlayer->fullscreen();

    mprisPlayer->setHasTrackList(true);
    bool isHasTrackList = mprisPlayer->hasTrackList();

    mprisPlayer->setIdentity("identity");
    QString identity = mprisPlayer->identity();

    mprisPlayer->setCanControl(true);
    bool isCnaControl = mprisPlayer->canControl();

    QVariantMap metadata = mprisPlayer->metadata();
    double minimumRate = mprisPlayer->minimumRate();



    ////mprisplayer_p.h
    QObject *obj = mrAdaptor;
    isCanQuite = obj->property("CanQuit").toBool();
    isCanRaise = obj->property("CanRaise").toBool();
    isCanSetFullscreen = obj->property("CanSetFullscreen").toBool();
    desktopEntry = obj->property("DesktopEntry").toString();
    isFullscreen = obj->property("Fullscreen").toBool();
    obj->setProperty("Fullscreen", "false");
    isHasTrackList = obj->property("HasTrackList").toBool();
    identity = obj->property("Identity").toString();

}
TEST(Mpris, MprisManager)
{
    MprisManager *mManager = new MprisManager();
    QUrl url(QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4"));

    bool singleService = mManager->singleService();
    mManager->setSingleService(true);

    QString curService = mManager->currentService();
    mManager->setCurrentService("service1");
//    QCOMPARE(mManager->currentService(), "service1");

    QStringList availableSer = mManager->availableServices();

    if(mManager->canRaise()){
        mManager->raise();
    }
    if(mManager->canSetFullscreen()){
        mManager->setFullscreen(true);
    }
    mManager->fullscreen();

    QString desktopEntry = mManager->desktopEntry();

    mManager->openUri(url);
    if(mManager->canGoNext()){
            mManager->next();
    }

    mManager->pause();
    mManager->play();
    double rate = mManager->rate();
    mManager->setRate(1.5);
    mManager->seek(200);
    mManager->setPosition(500);
    mManager->playPause();
    mManager->previous();
    mManager->stop();



    if(mManager->canQuit()){
        mManager->quit();
    }


}
