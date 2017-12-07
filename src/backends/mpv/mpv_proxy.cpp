#include "config.h"

#include "mpv_proxy.h"
#include "mpv_glwidget.h"
#include "compositing_manager.h"
#include "utility.h"
#include "player_engine.h"
#ifndef _LIBDMR_
#include "dmr_settings.h"
#include "movie_configuration.h"
#endif
#include <mpv/client.h>

#include <random>
#include <QtWidgets>

#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>
#include <QX11Info>

namespace dmr {
using namespace mpv::qt;

enum AsyncReplyTag {
    SEEK,
    CHANNEL,
    SPEED
};


static inline bool command_async(mpv_handle *ctx, const QVariant &args, uint64_t tag)
{
    node_builder node(args);
    int err = mpv_command_node_async(ctx, tag, node.node());
    return err == 0;
}

static inline int set_property_async(mpv_handle *ctx, const QString &name,
                                       const QVariant &v, uint64_t tag)
{
    node_builder node(v);
    return mpv_set_property_async(ctx, tag, name.toUtf8().data(), MPV_FORMAT_NODE, node.node());
}

static void mpv_callback(void *d)
{
    MpvProxy *mpv = static_cast<MpvProxy*>(d);
    QMetaObject::invokeMethod(mpv, "has_mpv_events", Qt::QueuedConnection);
}

MpvProxy::MpvProxy(QWidget *parent)
    :Backend(parent)
{
    if (!CompositingManager::get().composited()) {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_NativeWindow);
        qDebug() << "proxy hook winId " << this->winId();
    }

    _handle = Handle::FromRawHandle(mpv_init());
    if (CompositingManager::get().composited()) {
        _gl_widget = new MpvGLWidget(this, _handle);
        connect(this, &MpvProxy::stateChanged, [=]() {
            _gl_widget->setPlaying(state() != Backend::PlayState::Stopped);
            _gl_widget->update();
        });

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(_gl_widget);
        setLayout(layout);
    }

}

MpvProxy::~MpvProxy()
{
    disconnect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events);
    _connectStateChange = false;
    disconnect(window()->windowHandle(), &QWindow::windowStateChanged, 0, 0);
    if (CompositingManager::get().composited()) {
        delete _gl_widget;
    }
}

mpv_handle* MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();

    bool composited = CompositingManager::get().composited();
    
    switch(_debugLevel) {
        case DebugLevel::Info:
            mpv_request_log_messages(h, "info");
            break;

        case DebugLevel::Debug:
        case DebugLevel::Verbose:
            set_property(h, "terminal", "yes");
            if (_debugLevel == DebugLevel::Verbose) {
                set_property(h, "msg-level", "all=v");
                mpv_request_log_messages(h, "v");

            } else {
                mpv_request_log_messages(h, "debug");
            }
            break;
    }

#ifdef _LIBDMR_
    if (composited) {
        set_property(h, "opengl-hwdec-interop", "vaapi-egl");
    }
    set_property(h, "hwdec", "auto");

#else
    if (Settings::get().isSet(Settings::HWAccel)) {
        if (composited) {
            set_property(h, "opengl-hwdec-interop", "vaapi-egl");
        }
        set_property(h, "hwdec", "auto");
    } else {
        set_property(h, "hwdec", "off");
    }
#endif
    set_property(h, "panscan", 1.0);
    //set_property(h, "no-keepaspect", "true");

    if (composited) {
        set_property(h, "vo", "opengl-cb");

    } else {
        set_property(h, "vo", "opengl,xv,x11");
        set_property(h, "wid", this->winId());
    }


    set_property(h, "volume-max", 200.0);
    set_property(h, "input-cursor", "no");
    set_property(h, "cursor-autohide", "no");

    //set_property(h, "sub-ass-override", "yes");
    //set_property(h, "sub-ass-style-override", "yes");
    set_property(h, "sub-auto", "fuzzy");
    set_property(h, "sub-visibility", "true");
    //set_property(h, "sub-scale-with-window", "no");
    //set_property(h, "sub-scale-by-window", "no");
    set_property(h, "sub-pos", 100);
    set_property(h, "sub-margin-y", 36);

    set_property(h, "screenshot-template", "deepin-movie-shot%n");
    set_property(h, "screenshot-directory", "/tmp");

#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        set_property(h, "save-position-on-quit", true);
    }
#endif
    
    //only to get notification without data
    mpv_observe_property(h, 0, "time-pos", MPV_FORMAT_NONE); //playback-time ?
    mpv_observe_property(h, 0, "pause", MPV_FORMAT_NONE);
    mpv_observe_property(h, 0, "mute", MPV_FORMAT_NONE);
    mpv_observe_property(h, 0, "volume", MPV_FORMAT_NONE); //ao-volume ?
    mpv_observe_property(h, 0, "sid", MPV_FORMAT_NONE);
    mpv_observe_property(h, 0, "aid", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "dwidth", MPV_FORMAT_NODE);
    mpv_observe_property(h, 0, "dheight", MPV_FORMAT_NODE);

    // because of vpu, we need to implement playlist w/o mpv 
    //mpv_observe_property(h, 0, "playlist-pos", MPV_FORMAT_NONE);
    //mpv_observe_property(h, 0, "playlist-count", MPV_FORMAT_NONE);
    mpv_observe_property(h, 0, "core-idle", MPV_FORMAT_NODE);

    mpv_set_wakeup_callback(h, mpv_callback, this);
    connect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events,
            Qt::DirectConnection);
    if (mpv_initialize(h) < 0) {
        std::runtime_error("mpv init failed");
    }

    //load profile
    auto ol = CompositingManager::get().getBestProfile();
    auto p = ol.begin();
    while (p != ol.end()) {
        if (!p->first.startsWith("#")) {
            set_property(h, p->first.toUtf8().constData(), p->second.toUtf8().constData());
            qDebug() << "apply" << p->first << "=" << p->second;
        } else {
            qDebug() << "ignore(commented out)" << p->first << "=" << p->second;
        }
        ++p;
    }

    return h;
}

void MpvProxy::setState(PlayState s)
{
    if (_state != s) {
        _state = s;
        if (_gl_widget) { _gl_widget->setPlaying(s != PlayState::Stopped); }
        emit stateChanged();
    }
}


void MpvProxy::pollingEndOfPlayback()
{
    if (_state != Backend::Stopped) {
        _polling = true;
        blockSignals(true);
        stop();
        auto idle = get_property(_handle, "idle-active").toBool();
        if (idle) {
            blockSignals(false);
            setState(Backend::Stopped);
            _polling = false;
            return;
        }

        while (_state != Backend::Stopped) {
            mpv_event* ev = mpv_wait_event(_handle, 0.005);
            if (ev->event_id == MPV_EVENT_NONE) 
                continue;

            if (ev->event_id == MPV_EVENT_END_FILE) {
                qDebug() << "end of playback";
                blockSignals(false);
                setState(Backend::Stopped);
                break;
            }
        }

        _polling = false;
    }
}

void MpvProxy::pollingStartOfPlayback()
{
    if (_state == Backend::PlayState::Stopped) {
        _polling = true;

        while (_state == Backend::Stopped) {
            mpv_event* ev = mpv_wait_event(_handle, 0.005);
            if (ev->event_id == MPV_EVENT_NONE) 
                continue;

            if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                qDebug() << "start of playback";
                setState(Backend::Playing);
                break;
            }
        }

        _polling = false;
    }
}

const PlayingMovieInfo& MpvProxy::playingMovieInfo()
{
    return _pmf;
}

void MpvProxy::handle_mpv_events()
{
    while (1) {
        mpv_event* ev = mpv_wait_event(_handle, 0.0005);
        if (ev->event_id == MPV_EVENT_NONE) 
            break;

        switch (ev->event_id) {
            case MPV_EVENT_LOG_MESSAGE:
                processLogMessage((mpv_event_log_message*)ev->data);
                break;

            case MPV_EVENT_PROPERTY_CHANGE:
                processPropertyChange((mpv_event_property*)ev->data);
                break;

            case MPV_EVENT_COMMAND_REPLY:
                if (ev->error < 0) {
                    qDebug() << "command error";
                }

                if (ev->reply_userdata == AsyncReplyTag::SEEK) {
                    this->_pendingSeek = false;
                }
                break;

            case MPV_EVENT_PLAYBACK_RESTART:
                // caused by seek or just playing
                break;

            case MPV_EVENT_TRACKS_CHANGED:
                qDebug() << mpv_event_name(ev->event_id);
                updatePlayingMovieInfo();
                emit tracksChanged();
                break;

            case MPV_EVENT_FILE_LOADED:
                qDebug() << mpv_event_name(ev->event_id);

                if (_gl_widget) {
                    auto w = get_property(_handle, "width").toInt();
                    auto h = get_property(_handle, "height").toInt();

                    qDebug() << "hwdec-interop" << get_property(_handle, "hwdec-interop");
                }
                setState(PlayState::Playing); //might paused immediately
                emit fileLoaded();
                qDebug() << QString("rotate metadata: dec %1, out %2")
                    .arg(get_property(_handle, "video-dec-params/rotate").toInt())
                    .arg(get_property(_handle, "video-params/rotate").toInt());
                break;

            case MPV_EVENT_VIDEO_RECONFIG: {
                auto sz = videoSize();
                if (!sz.isEmpty())
                    emit videoSizeChanged();
                qDebug() << "videoSize " << sz;
                break;
            }

            case MPV_EVENT_END_FILE: {
#ifndef _LIBDMR_
                MovieConfiguration::get().updateUrl(this->_file,
                        ConfigKnownKey::StartPos, 0);
#endif
                mpv_event_end_file *ev_ef = (mpv_event_end_file*)ev->data;
                qDebug() << mpv_event_name(ev->event_id) << 
                    "reason " << ev_ef->reason;
                setState(PlayState::Stopped);
                break;
            }

            case MPV_EVENT_IDLE:
                qDebug() << mpv_event_name(ev->event_id);
                setState(PlayState::Stopped);
                emit elapsedChanged();
                break;

            default:
                qDebug() << mpv_event_name(ev->event_id);
                break;
        }
    }
}

void MpvProxy::processLogMessage(mpv_event_log_message* ev)
{
    switch (ev->log_level) {
        case MPV_LOG_LEVEL_WARN: 
            qWarning() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
            break;

        case MPV_LOG_LEVEL_ERROR: 
        case MPV_LOG_LEVEL_FATAL: 
            qCritical() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
            break;

        case MPV_LOG_LEVEL_INFO: 
            qInfo() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
            break;

        default:
            qDebug() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
            break;
    }
}

void MpvProxy::processPropertyChange(mpv_event_property* ev)
{
    //if (ev->data == NULL) return;

    QString name = QString::fromUtf8(ev->name);
    if (name != "time-pos") qDebug() << name;

    if (name == "time-pos") {
        emit elapsedChanged();
    } else if (name == "volume") {
        emit volumeChanged();
    } else if (name == "dwidth" || name == "dheight") {
        auto sz = videoSize();
        if (!sz.isEmpty())
            emit videoSizeChanged();
        qDebug() << "update videoSize " << sz;
    } else if (name == "aid") {
        emit aidChanged();
    } else if (name == "sid") {
        if (_externalSubJustLoaded) {
#ifndef _LIBDMR_
            MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::SubId, sid());
#endif
            _externalSubJustLoaded = false;
        }
        emit sidChanged();
    } else if (name == "mute") {
        emit muteChanged();
    } else if (name == "sub-visibility") {
        //_hideSub = get_property(_handle, "sub-visibility")
    } else if (name == "pause") {
        auto idle = get_property(_handle, "idle-active").toBool();
        if (get_property(_handle, "pause").toBool()) {
            if (!idle)
                setState(PlayState::Paused);
            else 
                set_property(_handle, "pause", false);
        } else {
            if (state() != PlayState::Stopped)
                setState(PlayState::Playing);
        }
    } else if (name == "core-idle") {
    }
}

bool MpvProxy::loadSubtitle(const QFileInfo& fi)
{
    if (state() == PlayState::Stopped) {
        return true;
    }

    if (!fi.exists())
        return false;

    QList<QVariant> args = { "sub-add", fi.absoluteFilePath(), "select" };
    qDebug () << args;
    QVariant id = command(_handle, args);
    if (id.canConvert<ErrorReturn>()) {
        return false;
    }

    // by settings this flag, we can match the corresponding sid change and save it 
    // in the movie database
    _externalSubJustLoaded = true;
    return true;
}

bool MpvProxy::isSubVisible()
{
    return get_property(_handle, "sub-visibility").toBool();
}

void MpvProxy::setSubDelay(double secs)
{
    set_property(_handle, "sub-delay", secs);
#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubDelay, subDelay());
#endif
}

double MpvProxy::subDelay() const
{
    return get_property(_handle, "sub-delay").toDouble();
}

QString MpvProxy::subCodepage()
{
    auto cp = get_property(_handle, "sub-codepage").toString();
    if (cp.startsWith("+")) {
        cp.remove(0, 1);
    }

    return cp;
}

void MpvProxy::addSubSearchPath(const QString& path)
{
    set_property(_handle, "sub-paths", path);
    set_property(_handle, "sub-file-paths", path);
}

void MpvProxy::setSubCodepage(const QString& cp)
{
    auto cp2 = cp;
    if (!cp.startsWith("+") && cp != "auto")
        cp2.prepend('+');

    set_property(_handle, "sub-codepage", cp2);
    command(_handle, {"sub-reload"});
#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubCodepage, subCodepage());
#endif
}

void MpvProxy::updateSubStyle(const QString& font, int sz)
{
    set_property(_handle, "sub-font", font);
    set_property(_handle, "sub-font-size", sz);
}

void MpvProxy::showEvent(QShowEvent *re)
{
    if (!_connectStateChange) {
        connect(window()->windowHandle(), &QWindow::windowStateChanged, [=](Qt::WindowState ws) {
            set_property(_handle, "panscan",
                (ws != Qt::WindowMaximized && ws != Qt::WindowFullScreen) ? 1.0 : 0.0);

        });
        _connectStateChange = true;
    }
}

void MpvProxy::resizeEvent(QResizeEvent *re)
{
    if (state() == PlayState::Stopped) {
        return;
    }
}

void MpvProxy::savePlaybackPosition()
{
    if (state() == PlayState::Stopped) {
        return;
    }

#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::SubId, sid());
    MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::StartPos, elapsed());
#endif
}

void MpvProxy::setPlaySpeed(double times)
{
    //set_property(_handle, "speed", times);
    set_property_async(_handle, "speed", times, AsyncReplyTag::SPEED);
}

void MpvProxy::selectSubtitle(int id)
{
    if (id > _pmf.subs.size()) {
        id = _pmf.subs.size() == 0? -1: _pmf.subs[0]["id"].toInt();
    }

    set_property(_handle, "sid", id);
#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubId, sid());
#endif
}

void MpvProxy::toggleSubtitle()
{
    if (state() == PlayState::Stopped) {
        return;
    }

    set_property(_handle, "sub-visibility", !isSubVisible());
}

int MpvProxy::aid() const
{
    return get_property(_handle, "aid").toInt();
}

int MpvProxy::sid() const
{
    return get_property(_handle, "sid").toInt();
}

void MpvProxy::selectTrack(int id)
{
    if (id >= _pmf.audios.size()) return;
    auto sid = _pmf.audios[id]["id"];
    set_property(_handle, "aid", sid);
}

void MpvProxy::changeSoundMode(SoundMode sm)
{
    QList<QVariant> args;

    switch(sm) {
        case SoundMode::Stereo:
            args << "af" << "del" << "@sm"; break;
        case SoundMode::Left:
            args << "af" << "add" << "@sm:channels=2:[0-0:1-0]"; break;
        case SoundMode::Right:
            args << "af" << "add" << "@sm:channels=2:[0-1:1-1]"; break;
    }

    command_async(_handle, args, AsyncReplyTag::CHANNEL);
}

void MpvProxy::volumeUp()
{
    QList<QVariant> args = { "add", "volume", 8 };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::changeVolume(int val)
{
    val = qMin(qMax(val, 0), 200);
    set_property(_handle, "volume", val);
}

void MpvProxy::volumeDown()
{
    QList<QVariant> args = { "add", "volume", -8 };
    qDebug () << args;
    command(_handle, args);
}

int MpvProxy::volume() const
{
    return get_property(_handle, "volume").toInt();
}

int MpvProxy::videoRotation() const
{
    auto vr = get_property(_handle, "video-rotate").toInt();
    return (vr + 360) % 360;
}

void MpvProxy::setVideoRotation(int degree)
{
    set_property(_handle, "video-rotate", degree);
}

void MpvProxy::setVideoAspect(double r)
{
    set_property(_handle, "video-aspect", r);
}

double MpvProxy::videoAspect() const
{
    return get_property(_handle, "video-aspect").toDouble();
}

bool MpvProxy::muted() const
{
    return get_property(_handle, "mute").toBool();
}

void MpvProxy::toggleMute()
{
    QList<QVariant> args = { "cycle", "mute" };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::play()
{
    QList<QVariant> args = { "loadfile" };
    QStringList opts = { };

    if (_file.isLocalFile()) {
        args << QFileInfo(_file.toLocalFile()).absoluteFilePath();
    } else {
        args << _file.url();
    }
#ifndef _LIBDMR_
    auto cfg = MovieConfiguration::get().queryByUrl(_file);
    auto key = MovieConfiguration::knownKey2String(ConfigKnownKey::StartPos);
    if (Settings::get().isSet(Settings::ResumeFromLast) && cfg.contains(key)) {
        opts << QString("start=%1").arg(cfg[key].toInt());
    }

    key = MovieConfiguration::knownKey2String(ConfigKnownKey::SubCodepage);
    if (cfg.contains(key)) {
        opts << QString("sub-codepage=%1").arg(cfg[key].toString());
    }

    key = MovieConfiguration::knownKey2String(ConfigKnownKey::SubDelay);
    if (cfg.contains(key)) {
        opts << QString("sub-delay=%1").arg(cfg[key].toDouble());
    }

    if (!_dvdDevice.isEmpty()) {
        opts << QString("dvd-device=%1").arg(_dvdDevice);
    }
#endif

    if (opts.size()) {
        //opts << "sub-auto=fuzzy";
        args << "replace" << opts.join(',');
    }

    qDebug () << args;
    command(_handle, args);
    set_property(_handle, "pause", false);

#ifndef _LIBDMR_
    // by giving a period of time, movie will be loaded and auto-loaded subs are 
    // all ready, then load extra subs from db
    // this keeps order of subs
    QTimer::singleShot(100, [this]() {
        auto cfg = MovieConfiguration::get().queryByUrl(_file);
        auto ext_subs = MovieConfiguration::get().getListByUrl(_file,
                ConfigKnownKey::ExternalSubs);
        for(const auto& sub: ext_subs) {
            if (!QFile::exists(sub)) {
                MovieConfiguration::get().removeFromListUrl(_file, ConfigKnownKey::ExternalSubs, sub);
            } else {
                loadSubtitle(sub);
            }
        }

        auto key = MovieConfiguration::knownKey2String(ConfigKnownKey::SubId);
        if (cfg.contains(key)) {
            selectSubtitle(cfg[key].toInt());
        }
    });
#endif
}


void MpvProxy::pauseResume()
{
    if (_state == PlayState::Stopped)
        return;

    set_property(_handle, "pause", !paused());
}

void MpvProxy::stop()
{
    QList<QVariant> args = { "stop" };
    qDebug () << args;
    command(_handle, args);
}

QImage MpvProxy::takeScreenshot()
{
    return takeOneScreenshot();
}

void MpvProxy::burstScreenshot()
{
    if (_inBurstShotting) {
        qWarning() << "already in burst screenshotting mode";
        return;
    }

    if (state() == PlayState::Stopped)
        return;

    //command(_handle, QList<QVariant> {"revert-seek", "mark"});
     _posBeforeBurst = get_property(_handle, "time-pos");

    int d = duration() / 15;

	std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> uniform_dist(0, d);
    _burstPoints.clear();
    for (int i = 0; i < 15; i++) {
        _burstPoints.append(d*i + uniform_dist(g));
    }
    _burstStart = 0;

    if (duration() < 35) {
        emit notifyScreenshot(QImage(), 0);
        stopBurstScreenshot();
        return;
    }
    qDebug() << "burst span " << _burstPoints;

    if (!paused()) pauseResume();
    _inBurstShotting = true;
    QTimer::singleShot(0, this, &MpvProxy::stepBurstScreenshot);
}

qint64 MpvProxy::nextBurstShootPoint()
{
    auto next = _burstPoints[_burstStart++];
    if (next >= duration()) {
        next = duration() - 5;
    }

    return next;
}

QImage MpvProxy::takeOneScreenshot()
{
    if (state() == PlayState::Stopped) return QImage();

    QList<QVariant> args = {"screenshot-raw"};
    node_builder node(args);
    mpv_node res;
    int err = mpv_command_node(_handle, node.node(), &res);
    if (err < 0) {
        qWarning() << "screenshot raw failed";
        return QImage();
    }

    node_autofree f(&res);

    Q_ASSERT(res.format == MPV_FORMAT_NODE_MAP);

    int w,h,stride;

    mpv_node_list *list = res.u.list;
    uchar *data = NULL;

    for (int n = 0; n < list->num; n++) {
        auto key = QString::fromUtf8(list->keys[n]);
        if (key == "w") {
            w = list->values[n].u.int64;
        } else if (key == "h") {
            h = list->values[n].u.int64;
        } else if (key == "stride") {
            stride = list->values[n].u.int64;
        } else if (key == "format") {
            auto format = QString::fromUtf8(list->values[n].u.string);
            qDebug() << "format" << format;
        } else if (key == "data") {
            data = (uchar*)list->values[n].u.ba->data;
        }
    }

    if (data) {
        //alpha should be ignored
        auto img = QImage((const uchar*)data, w, h, stride, QImage::Format_RGB32);
        img.bits();
        return img;
    }

    qDebug() << "failed";
    return QImage();
}

void MpvProxy::stepBurstScreenshot()
{
    if (!_inBurstShotting) {
        return;
    }

    auto pos = nextBurstShootPoint();
    command(_handle, QList<QVariant> {"seek", pos, "absolute"});
    int tries = 10;
    while (tries) {
        mpv_event* ev = mpv_wait_event(_handle, 0.005);
        if (ev->event_id == MPV_EVENT_NONE) 
            continue;

        if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            qDebug() << "seek finished" << elapsed();
            break;
        }

        if (ev->event_id == MPV_EVENT_END_FILE) {
            qDebug() << "seek finished (end of file)" << elapsed();
            break;
        }
    }

    QImage img = takeOneScreenshot();
    if (img.isNull()) {
        emit notifyScreenshot(img, elapsed());
        stopBurstScreenshot();
        return;
    }
    emit notifyScreenshot(img, elapsed());

    QTimer::singleShot(0, this, &MpvProxy::stepBurstScreenshot);
}

void MpvProxy::stopBurstScreenshot()
{
    _inBurstShotting = false;
    //command(_handle, QList<QVariant> {"revert-seek", "mark"});
    set_property(_handle, "time-pos", _posBeforeBurst);
}

void MpvProxy::seekForward(int secs)
{
    if (state() == PlayState::Stopped) return;

    //if (_pendingSeek) return;
    QList<QVariant> args = { "seek", QVariant(secs), "relative+keyframes" };
    qDebug () << args;
    command_async(_handle, args, AsyncReplyTag::SEEK);
    _pendingSeek = true;
}

void MpvProxy::seekBackward(int secs)
{
    if (state() == PlayState::Stopped) return;

    //if (_pendingSeek) return;
    if (secs > 0) secs = -secs;
    QList<QVariant> args = { "seek", QVariant(secs), "relative+keyframes" };
    qDebug () << args;
    command_async(_handle, args, AsyncReplyTag::SEEK);
    _pendingSeek = true;
}

void MpvProxy::seekAbsolute(int pos)
{
    if (state() == PlayState::Stopped) return;

    if (_pendingSeek) return;
    QList<QVariant> args = { "seek", QVariant(pos), "absolute" };
    qDebug () << args;
    //command(_handle, args);
    _pendingSeek = true;
    command_async(_handle, args, AsyncReplyTag::SEEK);
}

QSize MpvProxy::videoSize() const
{
    if (state() == PlayState::Stopped) return QSize(-1, -1);
    auto sz = QSize(get_property(_handle, "dwidth").toInt(),
            get_property(_handle, "dheight").toInt());

    auto r = get_property(_handle, "video-out-params/rotate").toInt();
    if (r == 90 || r == 270) {
        sz.transpose();
    }

    return sz;
}

qint64 MpvProxy::duration() const
{
    return get_property(_handle, "duration").value<qint64>();
}


qint64 MpvProxy::elapsed() const
{
    if (state() == PlayState::Stopped) return 0;
    return get_property(_handle, "time-pos").value<qint64>();
}

void MpvProxy::changeProperty(const QString& name, const QVariant& v)
{
}

void MpvProxy::updatePlayingMovieInfo()
{
    _pmf.subs.clear();
    _pmf.audios.clear();

    auto v = get_property(_handle, "track-list").toList();
    auto p = v.begin();
    while (p != v.end()) {
        const auto& t = p->toMap();
        if (t["type"] == "audio") {
            AudioInfo ai;
            ai["type"] = t["type"];
            ai["id"] = t["id"];
            ai["lang"] = t["lang"];
            ai["external"] = t["external"];
            ai["external-filename"] = t["external-filename"];
            ai["selected"] = t["selected"];
            ai["title"] = t["title"];

            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    ai["title"] = t["lang"];
                else if (!t["external"].toBool())
                    ai["title"] = tr("[internal]");
            }


            _pmf.audios.append(ai);
        } else if (t["type"] == "sub") {
            SubtitleInfo si;
            si["type"] = t["type"];
            si["id"] = t["id"];
            si["lang"] = t["lang"];
            si["external"] = t["external"];
            si["external-filename"] = t["external-filename"];
            si["selected"] = t["selected"];
            si["title"] = t["title"];
            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    si["title"] = t["lang"];
                else if (!t["external"].toBool())
                    si["title"] = tr("[internal]");
            }
            _pmf.subs.append(si);
        }
        ++p;
    }

    qDebug() << _pmf.subs;
    qDebug() << _pmf.audios;
}

QVariant MpvProxy::getProperty(const QString& name)
{
    return get_property(_handle, name.toUtf8().data());
}

void MpvProxy::setProperty(const QString& name, const QVariant& val)
{
    set_property(_handle, name.toUtf8().data(), val);
}

} // end of namespace dmr

