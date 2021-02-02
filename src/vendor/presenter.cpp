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
    initMpris(mprisPlayer);
}

void Presenter::initMpris(MprisPlayer *mprisPlayer)
{
    if (!mprisPlayer) {
        return ;
    }
    m_mprisplayer = mprisPlayer;
    connect(_mw->engine(), &PlayerEngine::stateChanged, this, &Presenter::slotstateChanged);
    connect(mprisPlayer, &MprisPlayer::playRequested, this, &Presenter::slotpause);
    connect(mprisPlayer, &MprisPlayer::pauseRequested, this, &Presenter::slotpause);
    connect(mprisPlayer, &MprisPlayer::nextRequested, this, &Presenter::slotplaynext);
    connect(mprisPlayer, &MprisPlayer::previousRequested, this, &Presenter::slotplayprev);
    connect(mprisPlayer, &MprisPlayer::volumeRequested, this, &Presenter::slotvolumeRequested);
    connect(mprisPlayer, &MprisPlayer::openUriRequested, this, &Presenter::slotopenUriRequested);
    connect(mprisPlayer, &MprisPlayer::loopStatusRequested, this, &Presenter::slotloopStatusRequested);
    connect(_mw->engine()->getplaylist(), &PlaylistModel::playModeChanged, this, &Presenter::slotplayModeChanged);
    connect(mprisPlayer, &MprisPlayer::openUriRequested, this, [ = ] {_mw->requestAction(ActionFactory::Exit);});
    //connect(_mw->engine(),&PlayerEngine::volumeChanged,this,&Presenter::slotvolumeChanged);

//    connect(_mw->toolbox()->get_progBar(), &Presenter::progrossChanged,
//    this, [ = ](qint64 pos, qint64) {
//        mprisPlayer->setPosition(pos);
//    });

}

void Presenter::slotpause()
{
    _mw->requestAction(ActionFactory::TogglePause);
}

void Presenter::slotplaynext()
{
    _mw->requestAction(ActionFactory::GotoPlaylistNext);
}

void Presenter::slotplayprev()
{
    _mw->requestAction(ActionFactory::GotoPlaylistPrev);
}

void Presenter::slotvolumeRequested(double volume)
{
    QList<QVariant> arg;
    arg.append((volume + 0.001) * 100.0);
    _mw->requestAction(ActionFactory::ChangeVolume, 1, arg);
}

void Presenter::slotopenUriRequested(const QUrl url)
{
    _mw->play(url);
}

void Presenter::slotstateChanged()
{
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
}

void Presenter::slotloopStatusRequested(Mpris::LoopStatus loopStatus)
{
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
    if (_mw->engine()->muted()) {
        m_mprisplayer->setVolume(0.0);
    } else {
        double pert = _mw->getDisplayVolume();
        m_mprisplayer->setVolume(pert);
    }
}
