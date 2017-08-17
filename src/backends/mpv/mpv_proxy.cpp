#include "mpv_proxy.h"
#include "mpv_glwidget.h"
#include "compositing_manager.h"
#include "options.h"
#include "utility.h"
#include "player_engine.h"
#include "dmr_settings.h"
#include <mpv/client.h>

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

    _burstScreenshotTimer = new QTimer(this);
    _burstScreenshotTimer->setSingleShot(true);
    connect(_burstScreenshotTimer, &QTimer::timeout, this, &MpvProxy::stepBurstScreenshot);
}

MpvProxy::~MpvProxy()
{
    disconnect(_burstScreenshotTimer, &QTimer::timeout, this, &MpvProxy::stepBurstScreenshot);
    disconnect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events);
    if (CompositingManager::get().composited()) {
        delete _gl_widget;
    }
}

mpv_handle* MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();

    bool composited = CompositingManager::get().composited();
    
    if (CommandLineManager::get().debug()) {
        set_property(h, "terminal", "yes");
        mpv_request_log_messages(h, "debug");

    } else if (CommandLineManager::get().verbose()) {
        set_property(h, "terminal", "yes");
        set_property(h, "msg-level", "all=v");
        mpv_request_log_messages(h, "v");

    } else {
        mpv_request_log_messages(h, "info");
    }

    if (composited) {
        set_property(h, "vo", "opengl-cb");

    } else {
        set_property(h, "vo", "opengl,xv,x11");
        set_property(h, "wid", this->winId());
    }

    if (Settings::get().isSet(Settings::HWAccel)) {
        if (composited) {
            set_property(h, "hwdec-preload", "auto");
        }
        set_property(h, "hwdec", "auto");
    } else {
        set_property(h, "hwdec", "off");
    }

    set_property(h, "volume-max", 200.0);
    set_property(h, "input-cursor", "no");
    set_property(h, "cursor-autohide", "no");

    set_property(h, "sub-ass-override", "force");
    set_property(h, "sub-ass-style-override", "force");
    //set_property(h, "sub-auto", "fuzzy");
    set_property(h, "sub-visibility", "true");

    set_property(h, "screenshot-template", "deepin-movie-shot%n");
    set_property(h, "screenshot-directory", "/tmp");

    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        set_property(h, "save-position-on-quit", true);
        auto watch_later_dir = QString("%1/%2/%3/watch_later")
            .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
            .arg(qApp->organizationName())
            .arg(qApp->applicationName());

        QDir d;
        d.mkpath(watch_later_dir);
        set_property(h, "watch-later-directory", watch_later_dir);
    }
    
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
        set_property(h, p->first.toUtf8().constData(), p->second.toUtf8().constData());
        qDebug() << "apply" << p->first << "=" << p->second;
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
    if (_state != Backend::Stopped) stop();

    while (_state != Backend::Stopped) {
        mpv_event* ev = mpv_wait_event(_handle, 0.005);
        if (ev->event_id == MPV_EVENT_NONE) 
            continue;

        if (ev->event_id == MPV_EVENT_END_FILE) {
            qDebug() << "end of playback";
            setState(Backend::Stopped);
            break;
        }
    }
}

const PlayingMovieInfo& MpvProxy::playingMovieInfo()
{
    return _pmf;
}

void MpvProxy::handle_mpv_events()
{
    while (1) {
        mpv_event* ev = mpv_wait_event(_handle, 0.005);
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

                setState(PlayState::Playing); //might paused immediately
                emit fileLoaded();
                break;

            case MPV_EVENT_END_FILE:
                qDebug() << mpv_event_name(ev->event_id);
                setState(PlayState::Stopped);
                break;

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
    } else if (name == "aid") {
        emit aidChanged();
    } else if (name == "sid") {
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

void MpvProxy::loadSubtitle(const QFileInfo& fi)
{
    if (state() == PlayState::Stopped) {
        return;
    }

    QList<QVariant> args = { "sub-add", fi.absoluteFilePath() };
    qDebug () << args;
    command(_handle, args);
}

bool MpvProxy::isSubVisible()
{
    return get_property(_handle, "sub-visibility").toBool();
}

void MpvProxy::setSubDelay(double secs)
{
    set_property(_handle, "sub-delay", secs);
}

void MpvProxy::updateSubStyle(const QString& font, int sz)
{
    set_property(_handle, "sub-font", font);
    set_property(_handle, "sub-font-size", sz);
}

void MpvProxy::savePlaybackPosition()
{
    if (state() == PlayState::Stopped) {
        return;
    }

    QList<QVariant> args = { "write-watch-later-config" };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::setPlaySpeed(double times)
{
    //set_property(_handle, "speed", times);
    set_property_async(_handle, "speed", times, AsyncReplyTag::SPEED);
}

void MpvProxy::selectSubtitle(int id)
{
    if (id >= _pmf.subs.size()) return;
    auto sid = _pmf.subs[id]["id"];
    set_property(_handle, "sid", sid);
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
    QList<QVariant> args = { "add", "volume", 2 };
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
    QList<QVariant> args = { "add", "volume", -2 };
    qDebug () << args;
    command(_handle, args);
}

int MpvProxy::volume() const
{
    return get_property(_handle, "volume").toInt();
}

int MpvProxy::videoRotation() const
{
    return get_property(_handle, "video-rotate").toInt();
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
    if (_file.isLocalFile()) {
        args << QFileInfo(_file.toLocalFile()).absoluteFilePath();
    } else {
        args << _file.url();
    }
    qDebug () << args;
    command(_handle, args);
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

    if (!paused()) pauseResume();
    _inBurstShotting = true;
    _burstScreenshotTimer->start();
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

    QImage img = takeOneScreenshot();
    if (img.isNull()) {
        stopBurstScreenshot();
        return;
    }

    emit notifyScreenshot(img);

    {
        QList<QVariant> args = {"frame-step"};
        command(_handle, args);
    }

    _burstScreenshotTimer->start();
}

void MpvProxy::stopBurstScreenshot()
{
    _inBurstShotting = false;
    _burstScreenshotTimer->stop();
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

    //if (_pendingSeek) return;
    QList<QVariant> args = { "seek", QVariant(pos), "absolute" };
    qDebug () << args;
    command_async(_handle, args, AsyncReplyTag::SEEK);
    _pendingSeek = true;
}

QSize MpvProxy::videoSize() const
{
    if (state() == PlayState::Stopped) return QSize(-1, -1);
    return QSize(get_property(_handle, "dwidth").toInt(),
            get_property(_handle, "dheight").toInt());

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

} // end of namespace dmr

