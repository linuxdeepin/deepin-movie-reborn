/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "movieapp.h"

MovieApp::MovieApp(MainWindow* mw, QObject* parent)
    :QObject(parent), _mw(mw)
{
    _presenter = new Presenter(_mw);
    initMpris("Deepinmovie");
}

MovieApp::MovieApp(Platform_MainWindow *mw, QObject *parent)
    :QObject(parent), _mw_platform(mw)
{
    _presenter = new Presenter(_mw_platform);
    initMpris("Deepinmovie");
}

void MovieApp::initMpris(const QString &serviceName)
{
    MprisPlayer* mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName(serviceName);

    //mprisPlayer->setSupportedMimeTypes();
    mprisPlayer->setSupportedUriSchemes(QStringList() << "file");
    mprisPlayer->setCanQuit(true);
    mprisPlayer->setCanRaise(true);
    mprisPlayer->setCanSetFullscreen(false);
    mprisPlayer->setHasTrackList(false);
    // setDesktopEntry: see https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html#Property:DesktopEntry for more
    mprisPlayer->setDesktopEntry("deepin-movie");
    mprisPlayer->setIdentity("Deepin Movie Player");

    mprisPlayer->setCanControl(true);
    mprisPlayer->setCanPlay(true);
    mprisPlayer->setCanGoNext(true);
    mprisPlayer->setCanGoPrevious(true);
    mprisPlayer->setCanPause(true);
	connect(mprisPlayer, &MprisPlayer::quitRequested, this, &MovieApp::quit);


    _presenter->initMpris(mprisPlayer);
}

void MovieApp::show()
{

}

void MovieApp::quit()
{
    if(_mw)
        _mw->requestAction(ActionFactory::Exit);
    else if (_mw_platform) {
         _mw_platform->requestAction(ActionFactory::Exit);
    }
}
