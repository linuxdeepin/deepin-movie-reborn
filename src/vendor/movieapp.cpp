// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "movieapp.h"
#include "ddlog.h"
#include <QDebug>

MovieApp::MovieApp(MainWindow* mw, QObject* parent)
    :QObject(parent), _mw(mw)
{
    qCDebug(appMovie) << "Entering MovieApp::MovieApp(MainWindow*, QObject*) constructor.";
    _presenter = new Presenter(_mw);
    initMpris("Deepinmovie");
    qCDebug(appMovie) << "Exiting MovieApp::MovieApp(MainWindow*, QObject*) constructor.";
}

MovieApp::MovieApp(Platform_MainWindow *mw, QObject *parent)
    :QObject(parent), _mw_platform(mw)
{
    qCDebug(appMovie) << "Entering MovieApp::MovieApp(Platform_MainWindow*, QObject*) constructor.";
    _presenter = new Presenter(_mw_platform);
    initMpris("Deepinmovie");
    qCDebug(appMovie) << "Exiting MovieApp::MovieApp(Platform_MainWindow*, QObject*) constructor.";
}

void MovieApp::initMpris(const QString &serviceName)
{
    qCDebug(appMovie) << "Entering MovieApp::initMpris() with serviceName:" << serviceName;
    MprisPlayer* mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName(serviceName);
    qCDebug(appMovie) << "MprisPlayer service name set.";

    //mprisPlayer->setSupportedMimeTypes();
    mprisPlayer->setSupportedUriSchemes(QStringList() << "file");
    mprisPlayer->setCanQuit(true);
    mprisPlayer->setCanRaise(true);
    mprisPlayer->setCanSetFullscreen(false);
    mprisPlayer->setHasTrackList(false);
    // setDesktopEntry: see https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html#Property:DesktopEntry for more
    mprisPlayer->setDesktopEntry("deepin-movie");
    mprisPlayer->setIdentity("Deepin Movie Player");
    qCDebug(appMovie) << "MprisPlayer capabilities and identity set.";

    mprisPlayer->setCanControl(true);
    mprisPlayer->setCanPlay(true);
    mprisPlayer->setCanGoNext(true);
    mprisPlayer->setCanGoPrevious(true);
    mprisPlayer->setCanPause(true);
	connect(mprisPlayer, &MprisPlayer::quitRequested, this, &MovieApp::quit);
    qCDebug(appMovie) << "Connected quitRequested signal to MovieApp::quit slot.";

    _presenter->initMpris(mprisPlayer);
    qCDebug(appMovie) << "Presenter initMpris called.";
    qCDebug(appMovie) << "Exiting MovieApp::initMpris().";
}

void MovieApp::show()
{
    qCDebug(appMovie) << "Entering MovieApp::show().";
    // This function is intentionally left empty in the original code, as per the context provided.
    qCDebug(appMovie) << "Exiting MovieApp::show().";
}

void MovieApp::quit()
{
    qCDebug(appMovie) << "Entering MovieApp::quit().";
    if(_mw) {
        qCDebug(appMovie) << "MainWindow pointer is valid, requesting Exit action.";
        _mw->requestAction(ActionFactory::Exit);
    } else if (_mw_platform) {
        qCDebug(appMovie) << "Platform_MainWindow pointer is valid, requesting Exit action.";
         _mw_platform->requestAction(ActionFactory::Exit);
    } else {
        qCDebug(appMovie) << "Neither MainWindow nor Platform_MainWindow pointers are valid.";
    }
    qCDebug(appMovie) << "Exiting MovieApp::quit().";
}
