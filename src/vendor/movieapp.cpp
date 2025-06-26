// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "movieapp.h"
#include <QDebug>

MovieApp::MovieApp(MainWindow* mw, QObject* parent)
    :QObject(parent), _mw(mw)
{
    qDebug() << "Entering MovieApp::MovieApp(MainWindow*, QObject*) constructor.";
    _presenter = new Presenter(_mw);
    initMpris("Deepinmovie");
    qDebug() << "Exiting MovieApp::MovieApp(MainWindow*, QObject*) constructor.";
}

MovieApp::MovieApp(Platform_MainWindow *mw, QObject *parent)
    :QObject(parent), _mw_platform(mw)
{
    qDebug() << "Entering MovieApp::MovieApp(Platform_MainWindow*, QObject*) constructor.";
    _presenter = new Presenter(_mw_platform);
    initMpris("Deepinmovie");
    qDebug() << "Exiting MovieApp::MovieApp(Platform_MainWindow*, QObject*) constructor.";
}

void MovieApp::initMpris(const QString &serviceName)
{
    qDebug() << "Entering MovieApp::initMpris() with serviceName:" << serviceName;
    MprisPlayer* mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName(serviceName);
    qDebug() << "MprisPlayer service name set.";

    //mprisPlayer->setSupportedMimeTypes();
    mprisPlayer->setSupportedUriSchemes(QStringList() << "file");
    mprisPlayer->setCanQuit(true);
    mprisPlayer->setCanRaise(true);
    mprisPlayer->setCanSetFullscreen(false);
    mprisPlayer->setHasTrackList(false);
    // setDesktopEntry: see https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html#Property:DesktopEntry for more
    mprisPlayer->setDesktopEntry("deepin-movie");
    mprisPlayer->setIdentity("Deepin Movie Player");
    qDebug() << "MprisPlayer capabilities and identity set.";

    mprisPlayer->setCanControl(true);
    mprisPlayer->setCanPlay(true);
    mprisPlayer->setCanGoNext(true);
    mprisPlayer->setCanGoPrevious(true);
    mprisPlayer->setCanPause(true);
	connect(mprisPlayer, &MprisPlayer::quitRequested, this, &MovieApp::quit);
    qDebug() << "Connected quitRequested signal to MovieApp::quit slot.";

    _presenter->initMpris(mprisPlayer);
    qDebug() << "Presenter initMpris called.";
    qDebug() << "Exiting MovieApp::initMpris().";
}

void MovieApp::show()
{
    qDebug() << "Entering MovieApp::show().";
    // This function is intentionally left empty in the original code, as per the context provided.
    qDebug() << "Exiting MovieApp::show().";
}

void MovieApp::quit()
{
    qDebug() << "Entering MovieApp::quit().";
    if(_mw) {
        qDebug() << "MainWindow pointer is valid, requesting Exit action.";
        _mw->requestAction(ActionFactory::Exit);
    } else if (_mw_platform) {
        qDebug() << "Platform_MainWindow pointer is valid, requesting Exit action.";
         _mw_platform->requestAction(ActionFactory::Exit);
    } else {
        qDebug() << "Neither MainWindow nor Platform_MainWindow pointers are valid.";
    }
    qDebug() << "Exiting MovieApp::quit().";
}
