// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presenter.h"
#include "ddlog.h"

Presenter::Presenter(MainWindow *mw, QObject *parent)
    : QObject(parent), _mw(mw)
{
    qCDebug(appMovie) << "Initializing Presenter with MainWindow";
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
    qCDebug(appMovie) << "Initializing Presenter with Platform_MainWindow";
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
    qCDebug(appMovie) << "Initializing MPRIS player";
    if (!mprisPlayer) {
        qCWarning(appMovie) << "MPRIS player is null";
        return ;
    }
    m_mprisplayer = mprisPlayer;
    if (_mw) {
        qCDebug(appMovie) << "Connecting MainWindow signals";
        connect(_mw->engine(), &PlayerEngine::stateChanged, this, &Presenter::slotstateChanged);
        connect(_mw->engine()->getplaylist(), &PlaylistModel::playModeChanged, this, &Presenter::slotplayModeChanged);
    } else {
        qCDebug(appMovie) << "Connecting Platform_MainWindow signals";
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
    qCDebug(appMovie) << "MPRIS player initialization completed";
}

void Presenter::slotplay()
{
    qCDebug(appMovie) << "MPRIS: Play requested, current status:" << m_mprisplayer->playbackStatus();
    if (m_mprisplayer->playbackStatus() == Mpris::Paused) {
        slotpause();
    } else {
        if (_mw) {
            qCDebug(appMovie) << "Requesting StartPlay action from MainWindow";
            _mw->requestAction(ActionFactory::StartPlay);
        } else {
            qCDebug(appMovie) << "Requesting StartPlay action from Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::StartPlay);
        }
    }
}

void Presenter::slotpause()
{
    qCDebug(appMovie) << "MPRIS: Pause requested";
    if (_mw) {
        qCDebug(appMovie) << "Requesting TogglePause action from MainWindow";
        _mw->requestAction(ActionFactory::TogglePause);
    } else {
        qCDebug(appMovie) << "Requesting TogglePause action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::TogglePause);
    }
}

void Presenter::slotplaynext()
{
    qCDebug(appMovie) << "MPRIS: Next requested";
    if (_mw) {
        qCDebug(appMovie) << "Requesting GotoPlaylistNext action from MainWindow";
        _mw->requestAction(ActionFactory::GotoPlaylistNext);
    } else {
        qCDebug(appMovie) << "Requesting GotoPlaylistNext action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::GotoPlaylistNext);
    }
}

void Presenter::slotplayprev()
{
    qCDebug(appMovie) << "MPRIS: Previous requested";
    if (_mw) {
        qCDebug(appMovie) << "Requesting GotoPlaylistPrev action from MainWindow";
        _mw->requestAction(ActionFactory::GotoPlaylistPrev);
    } else {
        qCDebug(appMovie) << "Requesting GotoPlaylistPrev action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::GotoPlaylistPrev);
    }
}

void Presenter::slotvolumeRequested(double volume)
{
    qCDebug(appMovie) << "MPRIS: Volume requested:" << volume;
    QList<QVariant> arg;
    arg.append((volume + 0.001) * 100.0);
    if (_mw) {
        qCDebug(appMovie) << "Requesting ChangeVolume action from MainWindow";
        _mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
    } else {
        qCDebug(appMovie) << "Requesting ChangeVolume action from Platform_MainWindow";
        _platform_mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
    }
}

void Presenter::slotopenUrlRequested(const QUrl url)
{
    qCDebug(appMovie) << "MPRIS: Open URI requested:" << url.toString();
    if (_mw) {
        qCDebug(appMovie) << "Playing URL in MainWindow";
        _mw->play({url.toString()});
    } else {
        qCDebug(appMovie) << "Playing URL in Platform_MainWindow";
        _platform_mw->play({url.toString()});
    }
}

void Presenter::slotstateChanged()
{
    qCDebug(appMovie) << "Player state changed";
    if (_mw) {
        qCDebug(appMovie) << "MPRIS: State changed to:" << _mw->engine()->state();
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
        qCDebug(appMovie) << "MPRIS: State changed to:" << _platform_mw->engine()->state();
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
    qCDebug(appMovie) << "MPRIS: Loop status requested:" << loopStatus;
    if (_mw) {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            qCWarning(appMovie) << "Invalid loop status requested";
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            qCDebug(appMovie) << "Setting OrderPlay mode in MainWindow";
            _mw->requestAction(ActionFactory::OrderPlay);
            _mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            qCDebug(appMovie) << "Setting SingleLoop mode in MainWindow";
            _mw->requestAction(ActionFactory::SingleLoop);
            _mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            qCDebug(appMovie) << "Setting ListLoop mode in MainWindow";
            _mw->requestAction(ActionFactory::ListLoop);
            _mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    } else {
        if (loopStatus == Mpris::LoopStatus::InvalidLoopStatus) {
            qCWarning(appMovie) << "Invalid loop status requested";
            return;
        } else if (loopStatus == Mpris::LoopStatus::None) {
            qCDebug(appMovie) << "Setting OrderPlay mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::OrderPlay);
            _platform_mw->reflectActionToUI(ActionFactory::OrderPlay);
        } else if (loopStatus == Mpris::LoopStatus::Track) {
            qCDebug(appMovie) << "Setting SingleLoop mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::SingleLoop);
            _platform_mw->reflectActionToUI(ActionFactory::SingleLoop);
        } else if (loopStatus == Mpris::LoopStatus::Playlist) {
            qCDebug(appMovie) << "Setting ListLoop mode in Platform_MainWindow";
            _platform_mw->requestAction(ActionFactory::ListLoop);
            _platform_mw->reflectActionToUI(ActionFactory::ListLoop);
        }
    }
}

void Presenter::slotplayModeChanged(PlaylistModel::PlayMode pm)
{
    qCDebug(appMovie) << "MPRIS: Play mode changed:" << pm;
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
    qCDebug(appMovie) << "MPRIS: Volume changed";
    if (_mw) {
        if (_mw->engine()->muted()) {
            qCDebug(appMovie) << "Setting volume to 0 (muted)";
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _mw->getDisplayVolume();
            qCDebug(appMovie) << "Setting volume to:" << pert;
            m_mprisplayer->setVolume(pert);
        }
    } else {
        if (_platform_mw->engine()->muted()) {
            qCDebug(appMovie) << "Setting volume to 0 (muted)";
            m_mprisplayer->setVolume(0.0);
        } else {
            double pert = _platform_mw->getDisplayVolume();
            qCDebug(appMovie) << "Setting volume to:" << pert;
            m_mprisplayer->setVolume(pert);
        }
    }
}

void Presenter::slotseek(qlonglong Offset)
{
    qCDebug(appMovie) << "MPRIS: Seek requested:" << Offset;
    if (_mw) {
        qCDebug(appMovie) << "Seeking in MainWindow engine";
        _mw->engine()->seekAbsolute(Offset);
    } else {
        qCDebug(appMovie) << "Seeking in Platform_MainWindow engine";
        _platform_mw->engine()->seekAbsolute(Offset);
    }
}

void Presenter::slotstop()
{
    qCDebug(appMovie) << "MPRIS: Stop requested";
    if (_mw) {
        qCDebug(appMovie) << "Stopping MainWindow engine";
        _mw->engine()->stop();
    } else {
        qCDebug(appMovie) << "Stopping Platform_MainWindow engine";
        _platform_mw->engine()->stop();
    }
}
