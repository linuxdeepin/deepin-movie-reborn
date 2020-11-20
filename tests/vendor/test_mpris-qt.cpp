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
    MprisPlayerAdaptor *mpAdaptor = new MprisPlayerAdaptor(mprisPlayer);
    QObject *obj_mr_adaptor = mrAdaptor;
    QObject *obj_mp_adaptor = mpAdaptor;

    mprisPlayer->setServiceName("services");
    QString serName = mprisPlayer->serviceName();   //services

    mprisPlayer->setCanQuit(true);
    bool isCanQuite = mprisPlayer->canQuit();      //true

    mprisPlayer->setCanRaise(true);
    bool isCanRaise = mprisPlayer->canRaise();  //true

    mprisPlayer->setCanSetFullscreen(true);
    bool isCanSetFullscreen = mprisPlayer->canSetFullscreen();  //true

    mprisPlayer->setDesktopEntry("desktop");
    QString desktopEntry = mprisPlayer->desktopEntry(); //desktop

    mprisPlayer->setFullscreen(true);
    bool isFullscreen = mprisPlayer->fullscreen();  //true

    mprisPlayer->setHasTrackList(true);
    bool isHasTrackList = mprisPlayer->hasTrackList();  //true

    mprisPlayer->setIdentity("identity");
    QString identity = mprisPlayer->identity(); //identity

    mprisPlayer->setCanControl(true);
    bool isCnaControl = mprisPlayer->canControl();  //true

    QMap<QString, QVariant> map;
    map.insert("Title", "test");
//    map.insert("Url", QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4"));
    map.insert("UserRating", 0.5);
    map.insert("UseCount", 1);
    map.insert("LastUsed", "2020/11/19");
    mprisPlayer->setMetadata(QVariantMap(map));
    QVariantMap metadata = mprisPlayer->metadata();  //

    double minimumRate = mprisPlayer->minimumRate();    //1
    mprisPlayer->setMinimumRate(1.5);
    minimumRate = mprisPlayer->minimumRate();    //1.5

    Mpris::PlaybackStatus playbackStatus = mprisPlayer->playbackStatus();   //Mpris::Stopped
    mprisPlayer->setPlaybackStatus(Mpris::Paused);
    playbackStatus = mprisPlayer->playbackStatus(); //Mpris::Paused

    bool shuffle = mprisPlayer->shuffle();
    mprisPlayer->setShuffle(true);

    double volume = mprisPlayer->volume();
    mprisPlayer->setVolume(20);


    //////////
//    QTest::qWait(100);
    bool isCanControl = obj_mp_adaptor->property("CanControl").toBool();
    bool isCanGoNext = obj_mp_adaptor->property("CanGoNext").toBool();
    bool isCanGoPrevious = obj_mp_adaptor->property("CanGoPrevious").toBool();
    bool isCanPause = obj_mp_adaptor->property("CanPause").toBool();
    bool isCanPlay = obj_mp_adaptor->property("CanPlay").toBool();
    bool isCanSeek = obj_mp_adaptor->property("CanSeek").toBool();

    QString loopStatus = obj_mp_adaptor->property("LoopStatus").toString();
    obj_mp_adaptor->setProperty("LoopStatus", Mpris::Playing);

    double maximumRate = obj_mp_adaptor->property("MaximumRate").toDouble();
    metadata = obj_mp_adaptor->property("Metadata").toMap();
    minimumRate = obj_mp_adaptor->property("MinimumRate").toDouble();
    QString s_playbackStatus = obj_mp_adaptor->property("PlaybackStatus").toString();
    qlonglong position = obj_mp_adaptor->property("PlaybackStatus").toLongLong();

    double rate = obj_mp_adaptor->property("Rate").toDouble();
    obj_mp_adaptor->setProperty("Rate", 2.0);

    shuffle = obj_mp_adaptor->property("Shuffle").toBool();
    obj_mp_adaptor->setProperty("Shuffle", true);

    volume = obj_mp_adaptor->property("Volume").toDouble();
    obj_mp_adaptor->setProperty("Volume", 50);

//    mpAdaptor->OpenUri("/usr/share/dde-introduction/demo.mp4");
//    mpAdaptor->Play();
//    QTest::qWait(100);
//    mpAdaptor->Pause();
//    mpAdaptor->Next();
//    mpAdaptor->PlayPause();
//    mpAdaptor->Previous();
//    mpAdaptor->Seek(235);
//    mpAdaptor->Stop();



    ///////
    isCanQuite = obj_mr_adaptor->property("CanQuit").toBool();
    isCanRaise = obj_mr_adaptor->property("CanRaise").toBool();
    isCanSetFullscreen = obj_mr_adaptor->property("CanSetFullscreen").toBool();
    desktopEntry = obj_mr_adaptor->property("DesktopEntry").toString();
    isFullscreen = obj_mr_adaptor->property("Fullscreen").toBool();
    obj_mr_adaptor->setProperty("Fullscreen", "false");
    isHasTrackList = obj_mr_adaptor->property("HasTrackList").toBool();
    identity = obj_mr_adaptor->property("Identity").toString();


//    delete mprisPlayer;
//    delete mrAdaptor;
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
    if(mManager->canPlay()){
        mManager->play();
    }
    if(mManager->canPause()){
        mManager->pause();
    }

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
