/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiangxiaojun <xiangxiaoju@uniontech.com>
 *
 * Maintainer: liuzheng <liuzheng@uniontech.com>
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
#include "presenter.h"

Presenter::Presenter(MainWindow *mw, QObject *parent)
    : QObject(parent), _mw(mw)
{
    MprisPlayer *mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName("Deepinmovie");

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
    mprisPlayer->setCanSeek(true);
#ifdef USE_FORK_MPRIS
    mprisPlayer->setCanShowInUI(false);
#endif
    initMpris(mprisPlayer);
}

Presenter::Presenter(Platform_MainWindow *mw, QObject *parent)
    : QObject(parent), _platform_mw(mw)
{
    MprisPlayer *mprisPlayer =  new MprisPlayer();
    mprisPlayer->setServiceName("Deepinmovie");

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
    mprisPlayer->setCanSeek(true);
#ifdef USE_FORK_MPRIS
    mprisPlayer->setCanShowInUI(false);
#endif
    initMpris(mprisPlayer);
}

void Presenter::initMpris(MprisPlayer *mprisPlayer)
{
    if (!mprisPlayer) {
        return ;
    }
    m_mprisplayer = mprisPlayer;
    if (_mw) {
        connect(_mw->engine(), &PlayerEngine::stateChanged, this, &Presenter::slotstateChanged);
        connect(_mw->engine()->getplaylist(), &PlaylistModel::playModeChanged, this, &Presenter::slotplayModeChanged);
    } else {
        connect(_platform_mw->engine(), &PlayerEngine::stateChanged, this, &Presenter::slotstateChanged);
        connect(_platform_mw->engine()->getplaylist(), &PlaylistModel::playModeChanged, this, &Presenter::slotplayModeChanged);
    }
    connect(mprisPlayer, &MprisPlayer::playRequested, this, &Presenter::slotplay);
    connect(mprisPlayer, &MprisPlayer::pauseRequested, this, &Presenter::slotpause);
    connect(mprisPlayer, &MprisPlayer::nextRequested, this, &Presenter::slotplaynext);
    connect(mprisPlayer, &MprisPlayer::previousRequested, this, &Presenter::slotplayprev);
    connect(mprisPlayer, &MprisPlayer::volumeRequested, this, &Presenter::slotvolumeRequested);
    connect(mprisPlayer, &MprisPlayer::openUriRequested, this, &Presenter::slotopenUrlRequested);
    connect(mprisPlayer, &MprisPlayer::loopStatusRequested, this, &Presenter::slotloopStatusRequested);
    //connect(mprisPlayer, &MprisPlayer::openUriRequested, this, [ = ] {_mw->requestAction(ActionFactory::Exit);});
    connect(mprisPlayer, &MprisPlayer::seekRequested, this, &Presenter::slotseek);
    connect(mprisPlayer, &MprisPlayer::stopRequested, this, &Presenter::slotstop);
    //connect(_mw->engine(),&PlayerEngine::volumeChanged,this,&Presenter::slotvolumeChanged);

//    connect(_mw->toolbox()->get_progBar(), &Presenter::progrossChanged,
//    this, [ = ](qint64 pos, qint64) {
//        mprisPlayer->setPosition(pos);
//    });

}

void Presenter::slotplay()
{
    if (m_mprisplayer->playbackStatus() == Mpris::Paused) {
        slotpause();
    } else {
        if (_mw)
            _mw->requestAction(ActionFactory::StartPlay);
        else
            _platform_mw->requestAction(ActionFactory::StartPlay);
    }
}

void Presenter::slotpause()
{
    if (_mw)
        _mw->requestAction(ActionFactory::TogglePause);
    else
        _platform_mw->requestAction(ActionFactory::TogglePause);
}

void Presenter::slotplaynext()
{
    if (_mw)
        _mw->requestAction(ActionFactory::GotoPlaylistNext);
    else
        _platform_mw->requestAction(ActionFactory::GotoPlaylistNext);
}

void Presenter::slotplayprev()
{
    if (_mw)
        _mw->requestAction(ActionFactory::GotoPlaylistPrev);
    else
        _platform_mw->requestAction(ActionFactory::GotoPlaylistPrev);
}

void Presenter::slotvolumeRequested(double volume)
{
    QList<QVariant> arg;
    arg.append((volume + 0.001) * 100.0);
    if (_mw)
        _mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
    else
        _platform_mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
}

void Presenter::slotopenUrlRequested(const QUrl url)
{
    if (_mw)
        _mw->play({url.toString()});
    else
        _platform_mw->play({url.toString()});
}

void Presenter::slotstateChanged()
{
    if (_mw) {
        switch (_mw->engine()->state()) {
        case PlayerEngine::CoreState::Idle:
            m_mprisplayer->setPlaybackStatus(Mpris::Stopped);
            break;
        case PlayerEngine::CoreState::Playing:
            m_mprisplayer->setPlaybackStatus(Mpris::Playing);
            break;
        case PlayerEngine::CoreState::Paused:
            m_mprisplayer->setPlaybackStatus(Mpris::Paused);
            break;
        }
    } else {
        switch (_platform_mw->engine()->state()) {
        case PlayerEngine::CoreState::Idle:
            m_mprisplayer->setPlaybackStatus(Mpris::Stopped);
            break;
        case PlayerEngine::CoreState::Playing:
            m_mprisplayer->setPlaybackStatus(Mpris::Playing);
            break;
        case PlayerEngine::CoreState::Paused:
            m_mprisplayer->setPlaybackStatus(Mpris::Paused);
            break;
        }
    }
}

void Presenter::slotloopStatusRequested(Mpris::LoopStatus loopStatus)
{
    if (_mw) {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            _mw->requestAction(ActionFactory::OrderPlay);
            _mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            _mw->requestAction(ActionFactory::SingleLoop);
            _mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            _mw->requestAction(ActionFactory::ListLoop);
            _mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    } else {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            _platform_mw->requestAction(ActionFactory::OrderPlay);
            _platform_mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            _platform_mw->requestAction(ActionFactory::SingleLoop);
            _platform_mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            _platform_mw->requestAction(ActionFactory::ListLoop);
            _platform_mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    }
}

void Presenter::slotplayModeChanged(PlaylistModel::PlayMode pm)
{
    if (pm == PlaylistModel::PlayMode::OrderPlay) {
        m_mprisplayer->setLoopStatus(Mpris::LoopStatus::None);
    } else if (pm == PlaylistModel::PlayMode::SingleLoop) {
        m_mprisplayer->setLoopStatus(Mpris::LoopStatus::Track);
    } else if (pm == PlaylistModel::PlayMode::ListLoop) {
        m_mprisplayer->setLoopStatus(Mpris::LoopStatus::Playlist);
    } else {
        m_mprisplayer->setLoopStatus(Mpris::LoopStatus::InvalidLoopStatus);
    }
}

void Presenter::slotvolumeChanged()
{
    if (_mw) {
        if (_mw->engine()->muted()) {
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _mw->getDisplayVolume();
            m_mprisplayer->setVolume(pert);
        }
    } else {
        if (_platform_mw->engine()->muted()) {
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _platform_mw->getDisplayVolume();
            m_mprisplayer->setVolume(pert);
        }
    }
}

void Presenter::slotseek(qlonglong Offset)
{
    if (_mw)
        _mw->engine()->seekAbsolute(Offset);
    else
        _platform_mw->engine()->seekAbsolute(Offset);
}

void Presenter::slotstop()
{
    if (_mw)
        _mw->engine()->stop();
    else
        _platform_mw->engine()->stop();
}
