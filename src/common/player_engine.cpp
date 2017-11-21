#include "config.h"

#include "player_engine.h"
#include "playlist_model.h"
#include "movie_configuration.h"

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

    connect(&OnlineSubtitle::get(), &OnlineSubtitle::subtitlesDownloadedFor, 
            this, &PlayerEngine::onSubtitlesDownloaded);
    addSubSearchPath(OnlineSubtitle::get().storeLocation());

    _playlist = new PlaylistModel(this);
    connect(_playlist, &PlaylistModel::asyncAppendFinished, this, 
            &PlayerEngine::onPlaylistAsyncAppendFinished);
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

bool PlayerEngine::isPlayableFile(const QUrl& url)
{
    if (url.isLocalFile()) {
        return isPlayableFile(url.path());
    } else {
        // for a networked url, there is no way to know if it's playable right now
        return true;
    }
}

static QStringList suffixes;

static const QStringList& buildPlayableDatabase()
{
    static QStringList mimeTypes = {
        "application/ogg",
        "application/vnd.apple.mpegurl",
        "application/vnd.rn-realmedia",
        "application/x-extension-mp4",
        "application/x-flac",
        "application/x-matroska",
        "application/x-ogg",
        "application/xspf+xml",
        "image/vnd.rn-realpix",
        "misc/ultravox",
        "video/3gpp",
        "video/dv",
        "video/mp2t",
        "video/mp4",
        "video/mp4v-es",
        "video/mpeg",
        "video/msvideo",
        "video/ogg",
        "video/quicktime",
        "video/vnd.rn-realvideo",
        "video/webm",
        "video/x-anim",
        "video/x-avi",
        "video/x-flc",
        "video/x-fli",
        "video/x-flv",
        "video/x-m4v",
        "video/x-matroska",
        "video/x-mpeg",
        "video/x-mpeg2",
        "video/x-ms-afs",
        "video/x-ms-asf",
        "video/x-msvideo",
        "video/x-ms-wmv",
        "video/x-ms-wmx",
        "video/x-ms-wvxvideo",
        "video/x-nsv",
        "video/x-ogm+ogg",
        "video/x-theora",
        "video/x-theora+ogg",
        "x-content/video-dvd",
        "x-content/video-svcd",
        "x-content/video-vcd",
        "x-scheme-handler/mms",
        "x-scheme-handler/rtmp",
        "x-scheme-handler/rtsp",
    };

    if (suffixes.isEmpty()) {
    }

    return suffixes;
}

bool PlayerEngine::isPlayableFile(const QString& name)
{
    auto suffix = QString("*") + name.mid(name.lastIndexOf('.'));
    return video_filetypes.contains(suffix, Qt::CaseInsensitive);
}

void PlayerEngine::updateSubStyles()
{
    auto font_opt = Settings::get().settings()->option("subtitle.font.family");
    auto font_id = font_opt->value().toInt();
    auto font = font_opt->data("items").toStringList()[font_id];
    auto sz = Settings::get().settings()->option("subtitle.font.size")->value().toInt();

    if (_state != CoreState::Idle) {
        if (_playlist->current() < 0) return;

        sz *= devicePixelRatioF();
        //double scale = _playlist->currentInfo().mi.height / 720.0;
        //sz = sz / scale;
        qDebug() << "update sub " << font << sz;
        updateSubStyle(font, sz);
    }
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

PlayerEngine::CoreState PlayerEngine::state()
{
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

    if (old != _state) {
        qWarning() << "###### state mismatch" << old << _state;
        emit stateChanged();
    }
    return _state;
}

const PlayingMovieInfo& PlayerEngine::playingMovieInfo()
{
    static PlayingMovieInfo empty;

    if (!_current) return empty;
    return _current->playingMovieInfo();
}

int PlayerEngine::aid()
{
    if (state() == CoreState::Idle) { return 0; }
    if (!_current) return 0;

    return _current->aid();
}

int PlayerEngine::sid()
{
    if (state() == CoreState::Idle) { return 0; }
    if (!_current) return 0;

    return _current->sid();
}

void PlayerEngine::onSubtitlesDownloaded(const QUrl& url, const QList<QString>& filenames,
        OnlineSubtitle::FailReason reason)
{
    if (state() == CoreState::Idle) { return; }
    if (!_current) return;

    if (playlist().currentInfo().url != url) 
        return;

    emit loadOnlineSubtitlesFinished(url,
            filenames.size() > 0 || reason == OnlineSubtitle::Duplicated);
    for (auto& filename: filenames)
        _current->loadSubtitle(filename);
}

bool PlayerEngine::loadSubtitle(const QFileInfo& fi)
{
    if (state() == CoreState::Idle) { return true; }
    if (!_current) return true;

    const auto& pmf = _current->playingMovieInfo();
    auto pif = playlist().currentInfo();
    for (const auto& sub: pmf.subs) {
        if (sub["external"].toBool()) {
            auto path = sub["external-filename"].toString();
            if (path == fi.canonicalFilePath()) {
                return true;
            }
        }
    }

    if (_current->loadSubtitle(fi)) {
        MovieConfiguration::get().append2ListUrl(pif.url, ConfigKnownKey::ExternalSubs,
                fi.canonicalFilePath());
        return true;
    }
    return false;
}

void PlayerEngine::loadOnlineSubtitle(const QUrl& url)
{
    if (state() == CoreState::Idle) { return; }
    if (!_current) return;

    OnlineSubtitle::get().requestSubtitle(url);
}

void PlayerEngine::setPlaySpeed(double times)
{
    if (!_current) return;
    _current->setPlaySpeed(times);
}

void PlayerEngine::setSubDelay(double secs)
{
    if (!_current) return;

    _current->setSubDelay(secs + _current->subDelay());
}

double PlayerEngine::subDelay() const
{
    if (!_current) return 0.0;
    return _current->subDelay();
}

QString PlayerEngine::subCodepage()
{
    return _current->subCodepage();
}

void PlayerEngine::setSubCodepage(const QString& cp)
{
    if (!_current) return;
    _current->setSubCodepage(cp);

    emit subCodepageChanged();
}

void PlayerEngine::addSubSearchPath(const QString& path)
{
    if (!_current) return;
    _current->addSubSearchPath(path);
}

void PlayerEngine::updateSubStyle(const QString& font, int sz)
{
    if (!_current) return;
    _current->updateSubStyle(font, sz);
}

void PlayerEngine::selectSubtitle(int id)
{
    if (!_current) return;
    if (state() != CoreState::Idle) {
        const auto& pmf = _current->playingMovieInfo();
        if (id >= pmf.subs.size()) return;
        auto sid = pmf.subs[id]["id"].toInt();
        _current->selectSubtitle(sid);
    }
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

void PlayerEngine::savePreviousMovieState()
{
    savePlaybackPosition();
}

//FIXME: TODO: update _current according to file 
void PlayerEngine::requestPlay(int id)
{
    if (!_current) return;
    if (id >= _playlist->count()) return;

    const auto& item = _playlist->items()[id];
    _current->setPlayFile(item.url);
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
    if (_playingRequest) return;
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playPrev(true);
    _playingRequest = false;
}

void PlayerEngine::next()
{
    if (_playingRequest) return;
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playNext(true);
    _playingRequest = false;
}

void PlayerEngine::onPlaylistAsyncAppendFinished(const QList<PlayItemInfo>& pil)
{
    if (_pendingPlayReq.isValid()) {
        auto id = _playlist->indexOf(_pendingPlayReq);
        if (pil.size() && _pendingPlayReq.scheme() == "playlist") {
            id = _playlist->indexOf(pil[0].url);
        }

        if (id >= 0) {
            _playlist->changeCurrent(id);
            _pendingPlayReq = QUrl();
        } else {
            if (!_playlist->hasPendingAppends()) {
                _pendingPlayReq = QUrl();
            } // else, wait for another signal
        }
    }
}

void PlayerEngine::playByName(const QUrl& url)
{
    savePreviousMovieState();
    auto id = _playlist->indexOf(url);
    if (id >= 0) {
        _playlist->changeCurrent(id);
    } else {
        _pendingPlayReq = url;
    }
}

void PlayerEngine::playSelected(int id)
{
    savePreviousMovieState();
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

void PlayerEngine::setDVDDevice(const QString& path)
{
    if (!_current) { return; }
    _current->setDVDDevice(path);
}

bool PlayerEngine::addPlayFile(const QUrl& url)
{
    if (isPlayableFile(url)) {
        if (url.isLocalFile())
            _playlist->appendAsync({url});
        else 
            _playlist->append(url);
        return true;
    }
    return false;
}

QList<QUrl> PlayerEngine::collectPlayDir(const QDir& dir)
{
    QList<QUrl> urls;

    QDirIterator di(dir, QDirIterator::Subdirectories);
    while (di.hasNext()) {
        di.next();
        if (di.fileInfo().isFile() && isPlayableFile(di.fileName())) {
            urls.append(QUrl::fromLocalFile(di.filePath()));
        }
    }

    return urls;
}

QList<QUrl> PlayerEngine::addPlayDir(const QDir& dir)
{
    auto valids = collectPlayDir(dir);
    _playlist->appendAsync(valids);
    return valids;
}


QList<QUrl> PlayerEngine::addPlayFiles(const QList<QUrl>& urls)
{
    QList<QUrl> valids = collectPlayFiles(urls);
    _playlist->appendAsync(valids);
    return valids;
}

QList<QUrl> PlayerEngine::collectPlayFiles(const QList<QUrl>& urls)
{
    //NOTE: take care of loop, we dont recursive, it seems safe now
    QList<QUrl> valids;
    for (const auto& url: urls) {
        if (url.isLocalFile()) {
            QFileInfo fi(url.toLocalFile());
            if (!fi.exists()) continue;

            if (fi.isDir()) {
                auto subs = collectPlayDir(fi.absoluteFilePath());
                valids += subs;
                valids += url;
                continue;
            } 
            
            if (!url.isValid() || !isPlayableFile(url)) {
                continue;
            }

            valids.append(url);
        }
    }

    return valids;
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

void PlayerEngine::resizeEvent(QResizeEvent* re)
{
    bool rounded = !window()->isFullScreen() && !window()->isMaximized();

#ifndef USE_DXCB
    if (rounded) {
        QPixmap shape(size());
        shape.fill(Qt::transparent);

        QPainter p(&shape);
        QPainterPath pp;
        pp.addRoundedRect(rect(), RADIUS, RADIUS);
        p.fillPath(pp, QBrush(Qt::white));
        p.end();

        setMask(shape.mask());
    } else {
        clearMask();
    }
#endif
}

} // end of namespace dmr
