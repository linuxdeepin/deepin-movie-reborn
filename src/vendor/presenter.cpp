#include "presenter.h"

Presenter::Presenter(MainWindow* mw, QObject *parent)
    : QObject(parent), _mw(mw)
{

}

void Presenter::initMpris(MprisPlayer *mprisPlayer)
{
    if(!mprisPlayer){
        return ;
    }

    connect(_mw->engine(), &PlayerEngine::stateChanged,
            this, [ = ] {
        switch (_mw->engine()->state()) {
        case PlayerEngine::CoreState::Idle:
            mprisPlayer->setPlaybackStatus(Mpris::Stopped);
            break;
        case PlayerEngine::CoreState::Playing:
            mprisPlayer->setPlaybackStatus(Mpris::Playing);
            break;
        case PlayerEngine::CoreState::Paused:
            mprisPlayer->setPlaybackStatus(Mpris::Paused);
            break;
        }
    });

    connect(mprisPlayer, &MprisPlayer::playRequested,
    this, [ = ]() {
        pause();
    });
    connect(mprisPlayer, &MprisPlayer::pauseRequested,
    this, [ = ]() {
        pause();
    });

    connect(mprisPlayer, &MprisPlayer::nextRequested,
    this, [ = ]() {
        playnext();
    });

    connect(mprisPlayer, &MprisPlayer::previousRequested,
    this, [ = ]() {
        playprev();
    });

    connect(mprisPlayer, &MprisPlayer::volumeRequested,
    this, [ = ](double volume) {
        if (_mw->engine()->muted()) {
            _mw->engine()->toggleMute();
        }

        _mw->engine()->changeVolume(volume);
        Settings::get().setInternalOption("global_volume", qMin(_mw->engine()->volume(), 140));
        double pert = _mw->engine()->volume();
        if (pert > VOLUME_OFFSET) {
            pert -= VOLUME_OFFSET;
        }
        _mw->get_nwComm()->updateWithMessage(tr("Volume: %1%").arg(pert));
    });

//    connect(_mw->toolbox()->get_progBar(), &Presenter::progrossChanged,
//    this, [ = ](qint64 pos, qint64) {
//        mprisPlayer->setPosition(pos);
//    });


}

void Presenter::pause()
{
    _mw->requestAction(ActionFactory::TogglePause);
}

void Presenter::playnext()
{
    _mw->requestAction(ActionFactory::GotoPlaylistNext);
}

void Presenter::playprev()
{
    _mw->requestAction(ActionFactory::GotoPlaylistPrev);
}
