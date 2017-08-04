#include "config.h"

#include "player_engine.h"
#include "playlist_model.h"

#include "mpv_proxy.h"
#ifdef ENABLE_VPU_PLATFORM
#include "vpu_proxy.h"
#endif

#include "dmr_settings.h"

namespace dmr {

PlayerEngine::PlayerEngine(QWidget *parent)
    :QWidget(parent)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    //FIXME: need to dynamically change this
#ifdef ENABLE_VPU_PLATFORM
    _current = new VpuProxy(this);
#else
    _current = new MpvProxy(this);
#endif
    if (_current) {
        connect(_current, &Backend::stateChanged, this, &PlayerEngine::onBackendStateChanged);
        connect(_current, &Backend::tracksChanged, this, &PlayerEngine::tracksChanged);
        connect(_current, &Backend::elapsedChanged, this, &PlayerEngine::elapsedChanged);
        connect(_current, &Backend::fileLoaded, this, &PlayerEngine::fileLoaded);
        connect(_current, &Backend::muteChanged, this, &PlayerEngine::muteChanged);
        connect(_current, &Backend::volumeChanged, this, &PlayerEngine::volumeChanged);
        connect(_current, &Backend::sidChanged, this, &PlayerEngine::sidChanged);
        connect(_current, &Backend::aidChanged, this, &PlayerEngine::aidChanged);
        connect(_current, &Backend::videoSizeChanged, this, &PlayerEngine::videoSizeChanged);
        connect(_current, &Backend::notifyScreenshot, this, &PlayerEngine::notifyScreenshot);
        l->addWidget(_current);
    }

    setLayout(l);
    connect(&Settings::get(), &Settings::subtitleChanged, this, &PlayerEngine::updateSubStyles);

    _playlist = new PlaylistModel(this);
}

PlayerEngine::~PlayerEngine()
{
    disconnect(_playlist, 0, 0, 0);
    delete _playlist;
    _playlist = nullptr;

    if (_current) {
        disconnect(_current, 0, 0, 0);
        delete _current;
        _current = nullptr;
    }
    qDebug() << __func__;
}

void PlayerEngine::updateSubStyles()
{
    auto font_opt = Settings::get().settings()->option("subtitle.font.family");
    auto font_id = font_opt->value().toInt();
    auto font = font_opt->data("items").toStringList()[font_id];
    auto sz = Settings::get().settings()->option("subtitle.font.size")->value().toInt();

    if (state() != CoreState::Idle) {
        double scale = _playlist->currentInfo().mi.height / 720.0;
        sz = sz / scale;
    }
    qDebug() << "update sub " << font << sz;
    this->updateSubStyle(font, sz);
}

void PlayerEngine::waitLastEnd()
{
    if (auto *mpv = dynamic_cast<MpvProxy*>(_current)) {
        mpv->pollingEndOfPlayback();
    }
}

void PlayerEngine::onBackendStateChanged()
{
    if (!_current) return;

    auto old = _state;
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

    updateSubStyles();
    if (old != _state)
        emit stateChanged();
}

const PlayingMovieInfo& PlayerEngine::playingMovieInfo()
{
    static PlayingMovieInfo empty;

    if (!_current) return empty;
    return _current->playingMovieInfo();
}

int PlayerEngine::aid() const
{
    if (state() == CoreState::Idle) { return 0; }
    if (!_current) return 0;

    return _current->aid();
}

int PlayerEngine::sid() const
{
    if (state() == CoreState::Idle) { return 0; }
    if (!_current) return 0;

    return _current->sid();
}

void PlayerEngine::loadSubtitle(const QFileInfo& fi)
{
    if (state() == CoreState::Idle) { return; }
    if (!_current) return;

    _current->loadSubtitle(fi);
}

void PlayerEngine::setPlaySpeed(double times)
{
    if (!_current) return;
    _current->setPlaySpeed(times);
}

void PlayerEngine::setSubDelay(double secs)
{
    if (!_current) return;
    _current->setSubDelay(secs);
}

void PlayerEngine::updateSubStyle(const QString& font, int sz)
{
    if (!_current) return;
    _current->updateSubStyle(font, sz);
}

void PlayerEngine::selectSubtitle(int id)
{
    if (!_current) return;
    _current->selectSubtitle(id);
}

bool PlayerEngine::isSubVisible()
{
    if (state() == CoreState::Idle) { return false; }
    if (!_current) return false;

    return _current->isSubVisible();
}

void PlayerEngine::toggleSubtitle()
{
    if (!_current) return;
    _current->toggleSubtitle();

}

void PlayerEngine::selectTrack(int id)
{
    if (!_current) return;
    _current->selectTrack(id);
}

void PlayerEngine::volumeUp()
{
    if (!_current) return;
    _current->volumeUp();
}

void PlayerEngine::changeVolume(int val)
{
    if (!_current) return;
    _current->changeVolume(val);
}

void PlayerEngine::volumeDown()
{
    if (!_current) return;
    _current->volumeDown();
}

int PlayerEngine::volume() const
{
    if (!_current) return 100;
    return _current->volume();
}

bool PlayerEngine::muted() const
{
    if (!_current) return false;
    return _current->muted();
}

void PlayerEngine::toggleMute()
{
    if (!_current) return;
    _current->toggleMute();
}

//FIXME: TODO: update _current according to file 
void PlayerEngine::requestPlay(int id)
{
    if (!_current) return;
    const auto& item = _playlist->items()[id];
    if (item.url.isLocalFile()) 
        _current->setPlayFile(item.info.absoluteFilePath());
    else
        _current->setPlayFile(item.url.url());

    if (_current->isPlayable()) {
        _current->play();
    } else {
        // TODO: delete and try next backend?
    }
}

void PlayerEngine::savePlaybackPosition()
{
    if (!_current) return;
    _current->savePlaybackPosition();
}

void PlayerEngine::play()
{
    if (state() == CoreState::Idle && _playlist->count()) {
        next();
    }
}

void PlayerEngine::prev()
{
    _playlist->playPrev(true);
}

void PlayerEngine::next()
{
    _playlist->playNext(true);
}

void PlayerEngine::playSelected(int id)
{
    _playlist->changeCurrent(id);
}

void PlayerEngine::clearPlaylist()
{
    _playlist->clear();
}

void PlayerEngine::pauseResume()
{
    if (!_current) return;
    if (_state == CoreState::Idle)
        return;

    _current->pauseResume();
}

void PlayerEngine::stop()
{
    if (!_current) return;
    _current->stop();
}

bool PlayerEngine::paused()
{
    return _state == CoreState::Paused;
}

QImage PlayerEngine::takeScreenshot()
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

void PlayerEngine::seekAbsolute(int pos)
{
    if (state() == CoreState::Idle) return;

    _current->seekAbsolute(pos);
}

void PlayerEngine::addPlayFile(const QUrl& url)
{
    _playlist->append(url);
}

qint64 PlayerEngine::duration() const
{
    if (!_current) return 0;
    return _current->duration();
}

QSize PlayerEngine::videoSize() const
{
    if (!_current) return {0, 0};
    return _current->videoSize();
}

qint64 PlayerEngine::elapsed() const
{
    if (!_current) return 0;
    return _current->elapsed();
}

void PlayerEngine::setVideoAspect(double r)
{
    if (_current)
        _current->setVideoAspect(r);
}

double PlayerEngine::videoAspect() const
{
    if (!_current) return 0.0;
    return _current->videoAspect();
}

int PlayerEngine::videoRotation() const
{
    if (!_current) return 0;
    return _current->videoRotation();
}

void PlayerEngine::setVideoRotation(int degree)
{
    if (_current) 
        _current->setVideoRotation(degree);
}

void PlayerEngine::changeSoundMode(Backend::SoundMode sm)
{
    if (_current) 
        _current->changeSoundMode(sm);
}

} // end of namespace dmr
