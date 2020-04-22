/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
#include <QtGlobal>

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
    MpvProxy *mpv = static_cast<MpvProxy *>(d);
    QMetaObject::invokeMethod(mpv, "has_mpv_events", Qt::QueuedConnection);
}

MpvProxy::MpvProxy(QWidget *parent)
    : Backend(parent)
{
    m_parentWidget = parent;
    if (!CompositingManager::get().composited()) {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_NativeWindow);
        qDebug() << "proxy hook winId " << this->winId();
    }

    _handle = Handle::FromRawHandle(mpv_init());
    if (CompositingManager::get().composited()) {
        _gl_widget = new MpvGLWidget(this, _handle);
        connect(this, &MpvProxy::stateChanged, [ = ]() {
            _gl_widget->setPlaying(state() != Backend::PlayState::Stopped);
            _gl_widget->update();
        });
#if defined(USE_DXCB) || defined(_LIBDMR_)
        _gl_widget->toggleRoundedClip(false);
#endif
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(_gl_widget);
        setLayout(layout);
    }
#ifdef __mips__
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
#endif
}

MpvProxy::~MpvProxy()
{
    disconnect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events);
    _connectStateChange = false;
    disconnect(window()->windowHandle(), &QWindow::windowStateChanged, 0, 0);
    if (CompositingManager::get().composited()) {
        disconnect(this, &MpvProxy::stateChanged, 0, 0);
        delete _gl_widget;
    }
}

mpv_handle *MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();

    bool composited = CompositingManager::get().composited();

    switch (_debugLevel) {
    case DebugLevel::Info:
        mpv_request_log_messages(h, "info");
        break;

    case DebugLevel::Debug:
    case DebugLevel::Verbose:
        set_property(h, "terminal", "yes");
        if (_debugLevel == DebugLevel::Verbose) {
            set_property(h, "msg-level", "all=status");
            mpv_request_log_messages(h, "info");

        } else {
            set_property(h, "msg-level", "all=v");
            mpv_request_log_messages(h, "v");
        }
        break;
    }

#ifdef _LIBDMR_
    if (composited) {
        auto interop = QString::fromUtf8("vaapi-glx");
        if (!qEnvironmentVariableIsEmpty("QT_XCB_GL_INTERGRATION")) {
            auto gl_int = qgetenv("QT_XCB_GL_INTERGRATION");
            if (gl_int == "xcb_egl") {
                interop = "vaapi-egl";
            } else if (gl_int == "xcb_glx") {
                interop = "vaapi-glx";
            } else {
                interop = "auto";
            }
        }
        set_property(h, "gpu-hwdec-interop", interop.toUtf8().constData());
        qDebug() << "set gpu-hwdec-interop = " << interop;
    }
    set_property(h, "hwdec", "auto");

#else
    if (Settings::get().isSet(Settings::HWAccel)) {
        if (composited) {
            auto disable = Settings::get().disableInterop();
            auto forced = Settings::get().forcedInterop();

            auto interop = QString::fromUtf8("auto");
            switch (CompositingManager::get().interopKind()) {
            case OpenGLInteropKind::INTEROP_AUTO:
                interop = QString::fromUtf8("auto");
                break;

            case OpenGLInteropKind::INTEROP_VAAPI_EGL:
                interop = QString::fromUtf8("vaapi-egl");
                break;

            case OpenGLInteropKind::INTEROP_VAAPI_GLX:
                interop = QString::fromUtf8("vaapi-glx");
                break;

            case OpenGLInteropKind::INTEROP_VDPAU_GLX:
                interop = QString::fromUtf8("vdpau-glx");
                break;

            default:
                break;

            }

            if (!forced.isEmpty()) {
                QStringList valids {"vaapi-egl", "vaapi-glx", "vdpau-glx", "auto"};
                if (valids.contains(forced)) {
                    interop = forced;
                }
            }

            if (!disable) {
                set_property(h, "gpu-hwdec-interop", interop.toUtf8().constData());
                qDebug() << "-------- set gpu-hwdec-interop = " << interop
                         << (forced.isEmpty() ? "[detected]" : "[forced]");
            } else {
                qDebug() << "-------- gpu-hwdec-interop is disabled by user";
            }
        }
        set_property(h, "hwdec", "auto");
    } else {
        set_property(h, "hwdec", "off");
    }
#endif
#ifdef __aarch64__
    /*QString path = QString("%1/%2/%3/conf")
                   .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                   .arg(qApp->organizationName())
                   .arg(qApp->applicationName());
    QFile configFile(path);
    if (configFile.exists()) {
        configFile.open(QIODevice::ReadOnly);
        int index = configFile.readLine().left(1).toInt();
        switch (index) {
        case 0:
            set_property(h, "hwdec", "no");
            qDebug() << "modify HWDEC no";
            break;
        case 1:
            set_property(h, "hwdec", "auto");
            qDebug() << "modify HWDEC auto";
            break;
        case 2:
            set_property(h, "hwdec", "yes");
            qDebug() << "modify HWDEC yes";
            break;
        case 3:
            set_property(h, "hwdec", "auto-safe");
            qDebug() << "modify HWDEC auto-safe";
            break;
        case 4:
            set_property(h, "hwdec", "vdpau");
            qDebug() << "modify HWDEC vdpau";
            break;
        case 5:
            set_property(h, "hwdec", "vaapi");
            qDebug() << "modify HWDEC vaapi";
            break;
        default:
            break;
        }
    }*/
    set_property(h, "hwdec", "auto-safe");
    qDebug() << "modify HWDEC auto-safe";
#endif
    set_property(h, "panscan", 1.0);
    //set_property(h, "no-keepaspect", "true");

    if (composited) {
        //vo=gpu seems broken, it'll makes video output into a seperate window
        //set_property(h, "vo", "gpu");
#ifdef __mips__
        mpv_set_option_string(h, "vo", "opengl-cb");
        mpv_set_option_string(h, "hwdec-preload", "auto");
        mpv_set_option_string(h, "opengl-hwdec-interop", "auto");
        mpv_set_option_string(h, "hwdec", "auto");
        qDebug() << "-------- __mips__hwdec____________";
#else
        set_property(h, "vo", "libmpv,opengl-cb");
        set_property(h, "vd-lavc-dr", "no");
        set_property(h, "gpu-sw", "on");
        //set_property(h, "ao", "alsa");
#endif
    } else {
#ifdef __mips__
        set_property(h, "vo", "gpu,x11,xv");
        set_property(h, "ao", "alsa");
#else
#ifdef MWV206_0
        QFileInfo fi("/dev/mwv206_0");              //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
        if (fi.exists()) {
            set_property(h, "hwdec", "vdpau");
            set_property(h, "vo", "vdpau");
        } else {
            auto e = QProcessEnvironment::systemEnvironment();
            QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
            QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

            if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
                    WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
                set_property(h, "vo", "gpu,x11,xv");
            } else {
                set_property(h, "vo", "xv");
            }

        }
#else
        set_property(h, "vo", "gpu,xv,x11");
#endif
#endif
        set_property(h, "wid", m_parentWidget->winId());
    }


    set_property(h, "volume-max", 240.0);
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
    set_property(h, "sub-border-size", 0);

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
    mpv_observe_property(h, 0, "paused-for-cache", MPV_FORMAT_NODE);

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
#ifndef __mips__
#ifdef MWV206_0
            QFileInfo fi("/dev/mwv206_0");              //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
            if (!fi.exists()) {
                set_property(h, p->first.toUtf8().constData(), p->second.toUtf8().constData());
                qDebug() << "apply" << p->first << "=" << p->second;
            }
#else
            set_property(h, p->first.toUtf8().constData(), p->second.toUtf8().constData());
            qDebug() << "apply" << p->first << "=" << p->second;
#endif
#endif
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
        if (_gl_widget) {
            _gl_widget->setPlaying(s != PlayState::Stopped);
        }
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
            mpv_event *ev = mpv_wait_event(_handle, 0.005);
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
            mpv_event *ev = mpv_wait_event(_handle, 0.005);
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

const PlayingMovieInfo &MpvProxy::playingMovieInfo()
{
    return _pmf;
}

void MpvProxy::handle_mpv_events()
{
    while (1) {
        mpv_event *ev = mpv_wait_event(_handle, 0.0005);
        if (ev->event_id == MPV_EVENT_NONE)
            break;

        switch (ev->event_id) {
        case MPV_EVENT_LOG_MESSAGE:
            processLogMessage((mpv_event_log_message *)ev->data);
            break;

        case MPV_EVENT_PROPERTY_CHANGE:
            processPropertyChange((mpv_event_property *)ev->data);
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

                qDebug() << "hwdec-interop" << get_property(_handle, "gpu-hwdec-interop")
                         << "codec: " << get_property(_handle, "video-codec")
                         << "format: " << get_property(_handle, "video-format");
#ifdef __mips__
                qDebug() << "MPV_EVENT_FILE_LOADED __mips__";
                auto codec = get_property(_handle, "video-codec").toString();
                if (codec.toLower().contains("wmv3") || codec.toLower().contains("wmv2") || codec.toLower().contains("mpeg2video")) {
                    qDebug() << "set_property hwdec no";
                    set_property(_handle, "hwdec", "no");
                }
#endif
#ifdef __aarch64__
                qDebug() << "MPV_EVENT_FILE_LOADED aarch64";
                auto codec = get_property(_handle, "video-codec").toString();
                if (codec.toLower().contains("wmv3") || codec.toLower().contains("wmv2") || codec.toLower().contains("mpeg2video")) {
//                    qDebug() << "set_property hwdec no";
//                    set_property(_handle, "hwdec", "no");
                    qDebug() << "set_property hwdec auto-safe";
                    set_property(_handle, "hwdec", "auto-safe");
                }
#endif
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
            mpv_event_end_file *ev_ef = (mpv_event_end_file *)ev->data;
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

void MpvProxy::processLogMessage(mpv_event_log_message *ev)
{
    switch (ev->log_level) {
    case MPV_LOG_LEVEL_WARN:
        qWarning() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
        emit mpvWarningLogsChanged(QString(ev->prefix), QString(ev->text));
        break;

    case MPV_LOG_LEVEL_ERROR:
    case MPV_LOG_LEVEL_FATAL:
        qCritical() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
        emit mpvErrorLogsChanged(QString(ev->prefix), QString(ev->text));
        break;

    case MPV_LOG_LEVEL_INFO:
        qInfo() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
        break;

    default:
        qDebug() << QString("%1: %2").arg(ev->prefix).arg(ev->text);
        break;
    }
}

void MpvProxy::processPropertyChange(mpv_event_property *ev)
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
            if (state() != PlayState::Stopped) {
                setState(PlayState::Playing);
                if (_startPlayDuration != 0) {
                    seekAbsolute(_startPlayDuration);
                    _startPlayDuration = 0;
                }
            }
        }
    } else if (name == "core-idle") {
    } else if (name == "paused-for-cache") {
        qDebug() << "paused-for-cache" << get_property_variant(_handle, "paused-for-cache");
        emit urlpause(get_property_variant(_handle, "paused-for-cache").toBool());
    }
}

bool MpvProxy::loadSubtitle(const QFileInfo &fi)
{
    //movie could be in an inner state that marked as Stopped when loadfile executes
    //if (state() == PlayState::Stopped) { return true; }

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

void MpvProxy::addSubSearchPath(const QString &path)
{
    set_property(_handle, "sub-paths", path);
    set_property(_handle, "sub-file-paths", path);
}

void MpvProxy::setSubCodepage(const QString &cp)
{
    auto cp2 = cp;
    if (!cp.startsWith("+") && cp != "auto")
        cp2.prepend('+');

    set_property(_handle, "sub-codepage", cp2);
    command(_handle, {"sub-reload"});
#ifndef _LIBDMR_
    if (_file.isValid())
        MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubCodepage, subCodepage());
#endif
}

void MpvProxy::updateSubStyle(const QString &font, int sz)
{
    set_property(_handle, "sub-font", font);
    set_property(_handle, "sub-font-size", sz);
    set_property(_handle, "sub-color", "#FFFFFF");
    set_property(_handle, "sub-border-size", 1);
    set_property(_handle, "sub-border-color", "0.0/0.0/0.0/0.50");
    set_property(_handle, "sub-shadow-offset", 1);
    set_property(_handle, "sub-shadow-color", "0.0/0.0/0.0/0.50");
}

void MpvProxy::showEvent(QShowEvent *re)
{
    if (!_connectStateChange) {
        connect(window()->windowHandle(), &QWindow::windowStateChanged, [ = ](Qt::WindowState ws) {
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
    if (elapsed() - 10 >= 0) {
        MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::StartPos, elapsed() - 10);
    } else {
        MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::StartPos, 10);
    }
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
        id = _pmf.subs.size() == 0 ? -1 : _pmf.subs[0]["id"].toInt();
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

    switch (sm) {
    case SoundMode::Stereo:
        args << "af" << "set" << "stereotools=muter=false";
        break;
    case SoundMode::Left:
        args << "af" << "set" << "stereotools=muter=true";
        break;
    case SoundMode::Right:
        args << "af" << "set" << "stereotools=mutel=true";
        break;
    }

    command(_handle, args);
}

void MpvProxy::volumeUp()
{
    QList<QVariant> args = { "add", "volume", 8 };
    qDebug () << args;
    command(_handle, args);
}

void MpvProxy::changeVolume(int val)
{
    val += 40;
    val = qMin(qMax(val, 40), 240);
    set_property(_handle, "volume", val);
}

void MpvProxy::volumeDown()
{
    if (volume() <= 0)
        return;
    QList<QVariant> args = { "add", "volume", -8 };
    qDebug () << args;
    command(_handle, args);
}

int MpvProxy::volume() const
{
    return get_property(_handle, "volume").toInt() - 40;
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
        opts << QString("start=%1").arg(0);
        _startPlayDuration = cfg[key].toInt();
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

    // hwdec could be disabled by some codecs, so we need to re-enable it
    if (Settings::get().isSet(Settings::HWAccel)) {
        set_property(_handle, "hwdec", "auto-safe");
    } else {
        set_property(_handle, "hwdec", "off");
    }
#else
    set_property(_handle, "hwdec", "auto");
#endif

    if (opts.size()) {
        //opts << "sub-auto=fuzzy";
        args << "replace" << opts.join(',');
    }

    qDebug () << args;
    command(_handle, args);
    set_property(_handle, "pause", _pauseOnStart);

#ifndef _LIBDMR_
    // by giving a period of time, movie will be loaded and auto-loaded subs are
    // all ready, then load extra subs from db
    // this keeps order of subs
    QTimer::singleShot(100, [this]() {
        auto cfg = MovieConfiguration::get().queryByUrl(_file);
        auto ext_subs = MovieConfiguration::get().getListByUrl(_file, ConfigKnownKey::ExternalSubs);
        for (const auto &sub : ext_subs) {
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
        _burstPoints.append(d * i + uniform_dist(g));
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

    int w, h, stride;

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
            data = (uchar *)list->values[n].u.ba->data;
        }
    }

    if (data) {
        //alpha should be ignored
        auto img = QImage((const uchar *)data, w, h, stride, QImage::Format_RGB32);
        img.bits();
        int rotationdegree = videoRotation();
        if (rotationdegree) {
            QMatrix matrix;
            matrix.rotate(rotationdegree);
            img = QPixmap::fromImage(img).transformed(matrix, Qt::SmoothTransformation).toImage();
        }
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
        mpv_event *ev = mpv_wait_event(_handle, 0.005);
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
    QList<QVariant> args = { "seek", QVariant(secs), "relative+exact" };
    qDebug () << args;
    command_async(_handle, args, AsyncReplyTag::SEEK);
    _pendingSeek = true;
}

void MpvProxy::seekBackward(int secs)
{
    if (state() == PlayState::Stopped) return;

    //if (_pendingSeek) return;
    if (secs > 0) secs = -secs;
    QList<QVariant> args = { "seek", QVariant(secs), "relative+exact" };
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

void MpvProxy::changeProperty(const QString &name, const QVariant &v)
{
}

void MpvProxy::updatePlayingMovieInfo()
{
    _pmf.subs.clear();
    _pmf.audios.clear();

    auto v = get_property(_handle, "track-list").toList();
    auto p = v.begin();
    while (p != v.end()) {
        const auto &t = p->toMap();
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
                    ai["title"] = "[internal]";
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
                    si["title"] = tr("Internal");
            }
            _pmf.subs.append(si);
        }
        ++p;
    }

    qDebug() << _pmf.subs;
    qDebug() << _pmf.audios;
}

void MpvProxy::nextFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> args = { "frame-step"};
    command(_handle, args);
}

void MpvProxy::previousFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> args = { "frame-back-step"};
    command(_handle, args);
}

QVariant MpvProxy::getProperty(const QString &name)
{
    return get_property(_handle, name.toUtf8().data());
}

void MpvProxy::setProperty(const QString &name, const QVariant &val)
{
    if (name == "pause-on-start") {
        _pauseOnStart = val.toBool();
    } else if (name == "video-zoom") {
        set_property(_handle, name, val.toDouble());
    } else {
        set_property(_handle, name.toUtf8().data(), val);
    }
}

} // end of namespace dmr

