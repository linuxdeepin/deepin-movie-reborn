// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presenter.h"

Presenter::Presenter(MainWindow *mw, QObject *parent)
    : QObject(parent), _mw(mw)
{
    qDebug() << "Initializing Presenter with MainWindow";
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
    qDebug() << "Initializing Presenter with Platform_MainWindow";
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
    qDebug() << "Initializing MPRIS player";
    if (!mprisPlayer) {
        qWarning() << "MPRIS player is null";
        return ;
    }
    m_mprisplayer = mprisPlayer;
    if (_mw) {
        qDebug() << "Connecting MainWindow signals";
        connect(_mw->engine(), &PlayerEngine::stateChanged, this, &Presenter::slotstateChanged);
        connect(_mw->engine()->getplaylist(), &PlaylistModel::playModeChanged, this, &Presenter::slotplayModeChanged);
    } else {
        qDebug() << "Connecting Platform_MainWindow signals";
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
    connect(mprisPlayer, &MprisPlayer::seekRequested, this, &Presenter::slotseek);
    connect(mprisPlayer, &MprisPlayer::stopRequested, this, &Presenter::slotstop);
    qDebug() << "MPRIS player initialization completed";
}

void Presenter::slotplay()
{
    qDebug() << "Play requested, current status:" << m_mprisplayer->playbackStatus();
    if (m_mprisplayer->playbackStatus() == Mpris::Paused) {
        slotpause();
    } else {
        if (_mw) {
            qDebug() << "Requesting StartPlay action from MainWindow";
            _mw->requestAction(ActionFactory::StartPlay);
        } else {
            qDebug() << "Requesting StartPlay action from Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::StartPlay);
        }
    }
}

void Presenter::slotpause()
{
    qDebug() << "Pause requested";
    if (_mw) {
        qDebug() << "Requesting TogglePause action from MainWindow";
        _mw->requestAction(ActionFactory::TogglePause);
    } else {
        qDebug() << "Requesting TogglePause action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::TogglePause);
    }
}

void Presenter::slotplaynext()
{
    qDebug() << "Play next requested";
    if (_mw) {
        qDebug() << "Requesting GotoPlaylistNext action from MainWindow";
        _mw->requestAction(ActionFactory::GotoPlaylistNext);
    } else {
        qDebug() << "Requesting GotoPlaylistNext action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::GotoPlaylistNext);
    }
}

void Presenter::slotplayprev()
{
    qDebug() << "Play previous requested";
    if (_mw) {
        qDebug() << "Requesting GotoPlaylistPrev action from MainWindow";
        _mw->requestAction(ActionFactory::GotoPlaylistPrev);
    } else {
        qDebug() << "Requesting GotoPlaylistPrev action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::GotoPlaylistPrev);
    }
}

void Presenter::slotvolumeRequested(double volume)
{
    qDebug() << "Volume change requested:" << volume;
    QList<QVariant> arg;
    arg.append((volume + 0.001) * 100.0);
    if (_mw) {
        qDebug() << "Requesting ChangeVolume action from MainWindow";
        _mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
    } else {
        qDebug() << "Requesting ChangeVolume action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
    }
}

void Presenter::slotopenUrlRequested(const QUrl url)
{
    qDebug() << "Open URL requested:" << url.toString();
    if (_mw) {
        qDebug() << "Playing URL in MainWindow";
        _mw->play({url.toString()});
    } else {
        qDebug() << "Playing URL in Platform_MainWindow";
        _platform_mw->play({url.toString()});
    }
}

void Presenter::slotstateChanged()
{
    qDebug() << "Player state changed";
    if (_mw) {
        qDebug() << "Updating MPRIS state from MainWindow engine state:" << _mw->engine()->state();
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
        qDebug() << "Updating MPRIS state from Platform_MainWindow engine state:" << _platform_mw->engine()->state();
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
    qDebug() << "Loop status change requested:" << loopStatus;
    if (_mw) {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            qWarning() << "Invalid loop status requested";
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            qDebug() << "Setting OrderPlay mode in MainWindow";
            _mw->requestAction(ActionFactory::OrderPlay);
            _mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            qDebug() << "Setting SingleLoop mode in MainWindow";
            _mw->requestAction(ActionFactory::SingleLoop);
            _mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            qDebug() << "Setting ListLoop mode in MainWindow";
            _mw->requestAction(ActionFactory::ListLoop);
            _mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    } else {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            qWarning() << "Invalid loop status requested";
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            qDebug() << "Setting OrderPlay mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::OrderPlay);
            _platform_mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            qDebug() << "Setting SingleLoop mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::SingleLoop);
            _platform_mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            qDebug() << "Setting ListLoop mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::ListLoop);
            _platform_mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    }
}

void Presenter::slotplayModeChanged(PlaylistModel::PlayMode pm)
{
    qDebug() << "Play mode changed to:" << pm;
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
    qDebug() << "Volume changed";
    if (_mw) {
        if (_mw->engine()->muted()) {
            qDebug() << "Setting volume to 0 (muted)";
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _mw->getDisplayVolume();
            qDebug() << "Setting volume to:" << pert;
            m_mprisplayer->setVolume(pert);
        }
    } else {
        if (_platform_mw->engine()->muted()) {
            qDebug() << "Setting volume to 0 (muted)";
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _platform_mw->getDisplayVolume();
            qDebug() << "Setting volume to:" << pert;
            m_mprisplayer->setVolume(pert);
        }
    }
}

void Presenter::slotseek(qlonglong Offset)
{
    qDebug() << "Seek requested to offset:" << Offset;
    if (_mw) {
        qDebug() << "Seeking in MainWindow engine";
        _mw->engine()->seekAbsolute(Offset);
    } else {
        qDebug() << "Seeking in Platform_MainWindow engine";
        _platform_mw->engine()->seekAbsolute(Offset);
    }
}

void Presenter::slotstop()
{
    qDebug() << "Stop requested";
    if (_mw) {
        qDebug() << "Stopping MainWindow engine";
        _mw->engine()->stop();
    } else {
        qDebug() << "Stopping Platform_MainWindow engine";
        _platform_mw->engine()->stop();
    }
}
