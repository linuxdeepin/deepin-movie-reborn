#include "config.h"

#include "player_engine.h"
#include "playlist_model.h"

#include "mpv_proxy.h"
#ifdef ENABLE_VPU_PLATFORM
#include "vpu_proxy.h"
#endif

namespace dmr {

PlayerEngine::PlayerEngine(QWidget *parent)
    :QWidget(parent)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    //FIXME: need to dynamically change this
#ifdef ENABLE_VPU_PLATFORM
    //_current = new VpuProxy(this);
#else
    _current = new MpvProxy(this);
#endif
    connect(_current, &Backend::stateChanged, this, &PlayerEngine::onBackendStateChanged);
    connect(_current, &Backend::tracksChanged, this, &PlayerEngine::tracksChanged);
    connect(_current, &Backend::ellapsedChanged, this, &PlayerEngine::ellapsedChanged);
    connect(_current, &Backend::fileLoaded, this, &PlayerEngine::fileLoaded);
    connect(_current, &Backend::muteChanged, this, &PlayerEngine::muteChanged);
    connect(_current, &Backend::volumeChanged, this, &PlayerEngine::volumeChanged);
    connect(_current, &Backend::notifyScreenshot, this, &PlayerEngine::notifyScreenshot);
    l->addWidget(_current);

    setLayout(l);


    _playlist = new PlaylistModel(this);
}

PlayerEngine::~PlayerEngine()
{
}

void PlayerEngine::onBackendStateChanged()
{
    switch (_current->state()) {
        case Backend::PlayState::Playing:
            _state = CoreState::Playing;
            break;
        case Backend::PlayState::Paused:
            _state = CoreState::Paused;
            break;
        case Backend::PlayState::Stopped:
            _state = CoreState::Idle;
            break;
    }

    emit stateChanged();
}

const PlayingMovieInfo& PlayerEngine::playingMovieInfo()
{
    return _current->playingMovieInfo();
}

void PlayerEngine::loadSubtitle(const QFileInfo& fi)
{
    if (state() == CoreState::Idle) { return; }

    _current->loadSubtitle(fi);
}

bool PlayerEngine::isSubVisible()
{
    if (state() == CoreState::Idle) { return false; }

    return _current->isSubVisible();
}

void PlayerEngine::toggleSubtitle()
{
    _current->toggleSubtitle();

}

void PlayerEngine::volumeUp()
{
    _current->volumeUp();
}

void PlayerEngine::changeVolume(int val)
{
    _current->changeVolume(val);
}

void PlayerEngine::volumeDown()
{
    _current->volumeDown();
}

int PlayerEngine::volume() const
{
    return _current->volume();
}

bool PlayerEngine::muted() const
{
    return _current->muted();
}

void PlayerEngine::toggleMute()
{
    _current->toggleMute();
}

//FIXME: TODO: update _current according to file 
void PlayerEngine::requestPlay(int id)
{
    auto info = _playlist->currentInfo();
    _current->setPlayFile(info.info.absoluteFilePath());
    if (_current->isPlayable()) {
        _current->play();
    } else {
        // TODO: delete and try next backend?
    }
}

void PlayerEngine::play()
{
    if (!_playlist->count()) return;

    if (state() == CoreState::Idle) {
        next();
    }
}

void PlayerEngine::prev()
{
    if (!_playlist->count()) return;

    if (state() != CoreState::Idle) {
        stop();
    }
    _playlist->playPrev();
}

void PlayerEngine::next()
{
    if (!_playlist->count()) return;

    if (state() != CoreState::Idle) {
        stop();
    }
    _playlist->playNext();
}

void PlayerEngine::clearPlaylist()
{
    if (!_playlist->count()) return;

    _playlist->clear();
}

void PlayerEngine::pauseResume()
{
    if (_state == CoreState::Idle)
        return;

    _current->pauseResume();
}

void PlayerEngine::stop()
{
    _current->stop();
}

bool PlayerEngine::paused()
{
    return _state == CoreState::Paused;
}

QPixmap PlayerEngine::takeScreenshot()
{
    return _current->takeScreenshot();
}

void PlayerEngine::burstScreenshot()
{
    _current->burstScreenshot();
}

void PlayerEngine::stopBurstScreenshot()
{
    _current->stopBurstScreenshot();
}

void PlayerEngine::seekForward(int secs)
{
    if (state() == CoreState::Idle) return;

    _current->seekForward(secs);
}

void PlayerEngine::seekBackward(int secs)
{
    if (state() == CoreState::Idle) return;

    _current->seekBackward(secs);
}

void PlayerEngine::addPlayFile(const QFileInfo& fi)
{
    _playlist->append(fi);
}

qint64 PlayerEngine::duration() const
{
    return _current->duration();
}


qint64 PlayerEngine::ellapsed() const
{
    return _current->ellapsed();
}


} // end of namespace dmr
