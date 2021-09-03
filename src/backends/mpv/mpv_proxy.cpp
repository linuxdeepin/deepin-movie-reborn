/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiepengfei <xiepengfei@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#include "player_engine.h"
#include "hwdec_probe.h"

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
#include <QLibrary>
#include <va/va_x11.h>

namespace dmr {
using namespace mpv::qt;

enum AsyncReplyTag {
    SEEK,
    CHANNEL,
    SPEED
};

static void mpv_callback(void *d)
{
    MpvProxy *pMpv = static_cast<MpvProxy *>(d);
    QMetaObject::invokeMethod(pMpv, "has_mpv_events", Qt::QueuedConnection);
}

MpvProxy::MpvProxy(QWidget *parent)
    : Backend(parent)
{
    initMember();

    m_pParentWidget = parent;

    if (!CompositingManager::get().composited()) {
        setWindowFlags(Qt::FramelessWindowHint);
        setAttribute(Qt::WA_NativeWindow);
        winId();
    }
#ifdef _LIBDMR_
    firstInit();
    m_bInited = true;
#endif

#if defined (__mips__) || defined (__aarch64__)
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
#endif
}

MpvProxy::~MpvProxy()
{
    disconnect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events);
    m_bConnectStateChange = false;
    disconnect(window()->windowHandle(), &QWindow::windowStateChanged, nullptr, nullptr);
    if (CompositingManager::get().composited()) {
        disconnect(this, &MpvProxy::stateChanged, nullptr, nullptr);
        delete m_pMpvGLwidget;
    }
}

void MpvProxy::setDecodeModel(const QVariant &value)
{
    m_decodeMode = static_cast<DecodeMode>(value.toInt());
}

void MpvProxy::initMpvFuns()
{
    QLibrary mpvLibrary(libPath("libmpv.so.1"));

    m_waitEvent = reinterpret_cast<mpv_waitEvent>(mpvLibrary.resolve("mpv_wait_event"));
    m_setOptionString = reinterpret_cast<mpv_set_optionString>(mpvLibrary.resolve("mpv_set_option_string"));
    m_setProperty = reinterpret_cast<mpv_setProperty>(mpvLibrary.resolve("mpv_set_property"));
    m_setPropertyAsync = reinterpret_cast<mpv_setProperty_async>(mpvLibrary.resolve("mpv_set_property_async"));
    m_commandNode = reinterpret_cast<mpv_commandNode>(mpvLibrary.resolve("mpv_command_node"));
    m_commandNodeAsync = reinterpret_cast<mpv_commandNode_async>(mpvLibrary.resolve("mpv_command_node_async"));
    m_getProperty = reinterpret_cast<mpv_getProperty>(mpvLibrary.resolve("mpv_get_property"));
    m_observeProperty = reinterpret_cast<mpv_observeProperty>(mpvLibrary.resolve("mpv_observe_property"));
    m_eventName = reinterpret_cast<mpv_eventName>(mpvLibrary.resolve("mpv_event_name"));
    m_creat = reinterpret_cast<mpvCreate>(mpvLibrary.resolve("mpv_create"));
    m_requestLogMessage = reinterpret_cast<mpv_requestLog_messages>(mpvLibrary.resolve("mpv_request_log_messages"));
    m_setWakeupCallback = reinterpret_cast<mpv_setWakeup_callback>(mpvLibrary.resolve("mpv_set_wakeup_callback"));
    m_initialize = reinterpret_cast<mpvinitialize>(mpvLibrary.resolve("mpv_initialize"));
    m_freeNodecontents = reinterpret_cast<mpv_freeNode_contents>(mpvLibrary.resolve("mpv_free_node_contents"));
}

void MpvProxy::firstInit()
{
#ifndef _LIBDMR_
#ifdef __x86_64__
    //第一次运行deepin-movie，检测是否支持硬解
    QString procName = QCoreApplication::applicationFilePath();
    QProcess proc;
    proc.start(procName, QStringList() << "hwdec");
    if (!proc.waitForFinished())
              return;
    //检测进程退出码
    if(proc.exitCode() != QProcess::NormalExit)
    {
        CompositingManager::setCanHwdec(false);
    } else {//检测进程日志输出
        QByteArray result = proc.readAllStandardError();
        qInfo() << "deepin-movie hwdec: " << result;
        if(result.toLower().contains("not supported")) {
            CompositingManager::setCanHwdec(false);
        } else {
            CompositingManager::setCanHwdec(true);
        }
    }
#endif
#endif
    initMpvFuns();
    if (m_creat) {
        m_handle = MpvHandle::fromRawHandle(mpv_init());
        if (CompositingManager::get().composited()) {
            m_pMpvGLwidget = new MpvGLWidget(this, m_handle);
            connect(this, &MpvProxy::stateChanged, this, &MpvProxy::slotStateChanged);

#if defined(USE_DXCB) || defined(_LIBDMR_)
            m_pMpvGLwidget->toggleRoundedClip(false);
#endif
            QHBoxLayout *pLayout = new QHBoxLayout(this);
            pLayout->setContentsMargins(0, 0, 0, 0);
            pLayout->addWidget(m_pMpvGLwidget);
            setLayout(pLayout);
            m_pMpvGLwidget->show();
        }
    }

    m_bInited = true;
    initSetting();
}

void MpvProxy::initSetting()
{
    QMapIterator<QString, QVariant> mapItor(m_mapWaitSet);
    while (mapItor.hasNext()) {
        mapItor.next();
        my_set_property(m_handle, mapItor.key(), mapItor.value());
    }

    QVectorIterator<QVariant> vecItor(m_vecWaitCommand);
    while (vecItor.hasNext()) {
        my_command(m_handle, vecItor.peekNext());
        vecItor.next();
    }
}

void MpvProxy::updateRoundClip(bool roundClip)
{
#ifdef __x86_64__
    m_pMpvGLwidget->toggleRoundedClip(roundClip);
#endif
}

mpv_handle *MpvProxy::mpv_init()
{
    //test by heyi
    mpv_handle *pHandle =  static_cast<mpv_handle *>(m_creat());
    bool composited = CompositingManager::get().composited();

    switch (_debugLevel) {
    case DebugLevel::Info:
        m_requestLogMessage(pHandle, "info");
        break;

    case DebugLevel::Debug:
    case DebugLevel::Verbose:
        my_set_property(pHandle, "terminal", "yes");
        if (_debugLevel == DebugLevel::Verbose) {
            my_set_property(pHandle, "msg-level", "all=status");
            m_requestLogMessage(pHandle, "info");

        } else {
            my_set_property(pHandle, "msg-level", "all=v");
            m_requestLogMessage(pHandle, "v");
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
        my_set_property(pHandle, "gpu-hwdec-interop", interop.toUtf8().constData());
        qInfo() << "set gpu-hwdec-interop = " << interop;
    }
    my_set_property(pHandle, "hwdec", "auto");

#else
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
            my_set_property(pHandle, "gpu-hwdec-interop", interop.toUtf8().constData());
            qInfo() << "-------- set gpu-hwdec-interop = " << interop
                    << (forced.isEmpty() ? "[detected]" : "[forced]");
        } else {
            qInfo() << "-------- gpu-hwdec-interop is disabled by user";
        }
    }
//重复逻辑
//    if (CompositingManager::get().isOnlySoftDecode()) {
//        my_set_property(pHandle, "hwdec", "no");
//    } else {
//        my_set_property(pHandle, "hwdec", "auto");
//    }
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
            my_set_property(pHandle, "hwdec", "no");
            qInfo() << "modify HWDEC no";
            break;
        case 1:
            my_set_property(pHandle, "hwdec", "auto");
            qInfo() << "modify HWDEC auto";
            break;
        case 2:
            my_set_property(pHandle, "hwdec", "yes");
            qInfo() << "modify HWDEC yes";
            break;
        case 3:
            my_set_property(pHandle, "hwdec", "auto");
            qInfo() << "modify HWDEC auto";
            break;
        case 4:
            my_set_property(pHandle, "hwdec", "vdpau");
            qInfo() << "modify HWDEC vdpau";
            break;
        case 5:
            my_set_property(pHandle, "hwdec", "vaapi");
            qInfo() << "modify HWDEC vaapi";
            break;
        default:
            break;
        }
    }*/
    if (CompositingManager::get().isOnlySoftDecode()) {
        my_set_property(pHandle, "hwdec", "no");
    } else {
        my_set_property(pHandle, "hwdec", "auto");
    }
    qInfo() << "modify HWDEC auto";
#endif
    my_set_property(pHandle, "panscan", 1.0);

    if (DecodeMode::SOFTWARE == m_decodeMode) { //1.设置软解
        my_set_property(m_handle, "hwdec", "no");
    } else if (DecodeMode::HARDWARE == m_decodeMode) { //2.设置硬解
        //2.1特殊硬件
        //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
        QFileInfo fi("/dev/mwv206_0");
        if (fi.exists()) { //2.1.1景嘉微
            my_set_property(m_handle, "hwdec", "vdpau,vdpau-copy,vaapi,vaapi-copy");
            my_set_property(m_handle, "vo", "vdpau,xv,x11");
            m_sInitVo = "vdpau,xv,x11";
        } else if (QFile::exists("/dev/csmcore")) { //2.1.2中船重工
            my_set_property(m_handle, "vo", "xv,x11");
            my_set_property(m_handle, "hwdec", "auto");
            if (utils::check_wayland_env()) {
                my_set_property(pHandle, "wid", m_pParentWidget->winId());
            }
            m_sInitVo = "xv,x11";
        } else if (CompositingManager::get().isOnlySoftDecode()) {//2.1.3 鲲鹏920 || 曙光+英伟达 || 浪潮
            my_set_property(m_handle, "hwdec", "no");
        } else { //2.2非特殊硬件
            my_set_property(m_handle, "hwdec", "auto");
        }

#if defined (__mips__)
        if (!CompositingManager::get().hascard()) {
            qInfo() << "修改音视频同步模式";
            my_set_property(m_handle, "video-sync", "desync");
        }
        my_set_property(m_handle, "vo", "vdpau,gpu,x11");
        my_set_property(m_handle, "ao", "alsa");
        m_sInitVo = "vdpau,gpu,x11";
#elif defined (__sw_64__)
        //Synchronously modify the video output of the SW platform vdpau(powered by zhangfl)
        my_set_property(m_handle, "vo", "vdpau,gpu,x11");
        m_sInitVo = "vdpau,gpu,x11";
#elif defined (__aarch64__)
        my_set_property(m_handle, "vo", "gpu,xv,x11");
        m_sInitVo = "gpu,xv,x11";
#else
        //TODO(xxxxpengfei)：暂未处理intel集显情况
        if (CompositingManager::get().isZXIntgraphics()) {
            my_set_property(m_handle, "vo", "gpu");
        }
#endif
    } else { //3.设置自动
        my_set_property(m_handle, "hwdec", "auto");
    }

    if (composited) {
#ifdef __mips__
        m_setOptionString(pHandle, "vo", "opengl-cb");
        m_setOptionString(pHandle, "hwdec-preload", "auto");
        m_setOptionString(pHandle, "opengl-hwdec-interop", "auto");
        m_setOptionString(pHandle, "hwdec", "auto");
        qInfo() << "-------- __mips__hwdec____________";
        m_sInitVo = "opengl-cb";
#else
        my_set_property(pHandle, "vo", "libmpv,opengl-cb");
        my_set_property(pHandle, "vd-lavc-dr", "no");
        my_set_property(pHandle, "gpu-sw", "on");
        m_sInitVo = "libmpv,opengl-cb";
        //设置alse时，无法使用set_property(pHandle, "audio-client-name", strMovie)设置控制栏中的名字
        //        if(utils::check_wayland_env()){
        //            set_property(pHandle, "ao", "alsa");
        //        }
#endif
    } else {
        my_set_property(m_handle, "wid", m_pParentWidget->winId());
    }

//    if (QFile::exists("/dev/csmcore")) {
//        my_set_property(pHandle, "vo", "xv,x11");
//        my_set_property(pHandle, "hwdec", "auto");
//        if (utils::check_wayland_env()) {
//            my_set_property(pHandle, "wid", m_pParentWidget->winId());
//        }
//        m_sInitVo = "xv,x11";
//    }
    qInfo() << __func__ << my_get_property(pHandle, "vo").toString();
    qInfo() << __func__ << my_get_property(pHandle, "hwdec").toString();

    QString strMovie = QObject::tr("Movie");
    //设置音量名称
    my_set_property(pHandle, "audio-client-name", strMovie);
    //my_set_property(pHandle, "keepaspect-window", "no");
    //设置视频固定帧率，暂时无效
    //my_set_property(pHandle, "correct-pts", false);
    //my_set_property(pHandle, "fps", 30);
    my_set_property(pHandle, "panscan", 0);
    my_set_property(pHandle, "volume-max", 100.0);
    my_set_property(pHandle, "input-cursor", "no");
    my_set_property(pHandle, "cursor-autohide", "no");
    my_set_property(pHandle, "sub-auto", "fuzzy");
    my_set_property(pHandle, "sub-visibility", "true");
    my_set_property(pHandle, "sub-pos", 100);
    my_set_property(pHandle, "sub-margin-y", 36);
    my_set_property(pHandle, "sub-border-size", 0);
    my_set_property(pHandle, "screenshot-template", "deepin-movie-shot%n");
    my_set_property(pHandle, "screenshot-directory", "/tmp");

#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        my_set_property(pHandle, "save-position-on-quit", true);
    }
#endif

    //only to get notification without data
    m_observeProperty(pHandle, 0, "time-pos", MPV_FORMAT_NONE); //playback-time ?
    m_observeProperty(pHandle, 0, "pause", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "mute", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "volume", MPV_FORMAT_NONE); //ao-volume ?
    m_observeProperty(pHandle, 0, "sid", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "aid", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "dwidth", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "dheight", MPV_FORMAT_NODE);

    // because of vpu, we need to implement playlist w/o mpv
    //m_observeProperty(pHandle, 0, "playlist-pos", MPV_FORMAT_NONE);
    //m_observeProperty(pHandle, 0, "playlist-count", MPV_FORMAT_NONE);
    m_observeProperty(pHandle, 0, "core-idle", MPV_FORMAT_NODE);
    m_observeProperty(pHandle, 0, "paused-for-cache", MPV_FORMAT_NODE);

    m_setWakeupCallback(pHandle, mpv_callback, this);
    connect(this, &MpvProxy::has_mpv_events, this, &MpvProxy::handle_mpv_events,
            Qt::DirectConnection);
    if (m_initialize(pHandle) < 0) {
        std::runtime_error("mpv init failed");
    }

    //load profile
    auto ol = CompositingManager::get().getBestProfile();
    auto p = ol.begin();
    while (p != ol.end()) {
        if (!p->first.startsWith("#")) {
#if !defined (__mips__ ) && !defined(__aarch64__) && !defined(__sw_64__)
#ifdef MWV206_0
            QFileInfo fi("/dev/mwv206_0");              //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
            if (!fi.exists()) {
                my_set_property(pHandle, p->first.toUtf8().constData(), p->second.toUtf8().constData());
                qInfo() << "apply" << p->first << "=" << p->second;
            }
#else
            my_set_property(pHandle, p->first.toUtf8().constData(), p->second.toUtf8().constData());
            qInfo() << "apply" << p->first << "=" << p->second;
#endif
#endif
        } else {
            qInfo() << "ignore(commented out)" << p->first << "=" << p->second;
        }
        ++p;
    }

    //设置hwdec和vo配置
    CompositingManager::get().getMpvConfig(m_pConfig);
    QMap<QString, QString>::iterator iter = m_pConfig->begin();
    qInfo() << __func__ << "First set mpv propertys!!";
    while (iter != m_pConfig->end()) {
        my_set_property(pHandle, iter.key(), iter.value());
        iter++;
    }

    return pHandle;
}

void MpvProxy::setState(PlayState state)
{
    if (_state != state) {
        _state = state;
        if (m_pMpvGLwidget) {
            m_pMpvGLwidget->setPlaying(state != PlayState::Stopped);
        }
        emit stateChanged();
    }
}

void MpvProxy::pollingEndOfPlayback()
{
    if (_state != Backend::Stopped) {
        m_bPolling = true;
        blockSignals(true);
        stop();
        bool bIdel = my_get_property(m_handle, "idle-active").toBool();
        if (bIdel) {
            blockSignals(false);
            setState(Backend::Stopped);
            m_bPolling = false;
            return;
        }

        while (_state != Backend::Stopped) {
            mpv_event *pEvent = m_waitEvent(m_handle, 0.005);
            if (pEvent->event_id == MPV_EVENT_NONE)
                continue;

            if (pEvent->event_id == MPV_EVENT_END_FILE) {
                blockSignals(false);
                setState(Backend::Stopped);
                break;
            }
        }
        m_bPolling = false;
    }
}
/*not used yet*/
/*void MpvProxy::pollingStartOfPlayback()
{
    if (_state == Backend::PlayState::Stopped) {
        m_bPolling = true;

        while (_state == Backend::Stopped) {
            mpv_event *ev = m_waitEvent(m_handle, 0.005);
            if (ev->event_id == MPV_EVENT_NONE)
                continue;

            if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                qInfo() << "start of playback";
                setState(Backend::Playing);
                break;
            }
        }

        m_bPolling = false;
    }
}*/

const PlayingMovieInfo &MpvProxy::playingMovieInfo()
{
    return m_movieInfo;
}

void MpvProxy::handle_mpv_events()
{
    if (utils::check_wayland_env() && CompositingManager::get().isTestFlag()) {
        qInfo() << "not handle mpv events!";
        return;
    }
    while (1) {
        mpv_event *pEvent = m_waitEvent(m_handle, 0.0005);
        if (pEvent->event_id == MPV_EVENT_NONE)
            break;

        switch (pEvent->event_id) {
        case MPV_EVENT_LOG_MESSAGE:
            processLogMessage(reinterpret_cast<mpv_event_log_message *>(pEvent->data));
            break;

        case MPV_EVENT_PROPERTY_CHANGE:
            processPropertyChange(reinterpret_cast<mpv_event_property *>(pEvent->data));
            break;

        case MPV_EVENT_COMMAND_REPLY:
            if (pEvent->error < 0) {
                qInfo() << "command error";
            }

            if (pEvent->reply_userdata == AsyncReplyTag::SEEK) {
                m_bPendingSeek = false;
            }
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            // caused by seek or just playing
            break;

        case MPV_EVENT_TRACKS_CHANGED:
            qInfo() << m_eventName(pEvent->event_id);
            updatePlayingMovieInfo();
            emit tracksChanged();
            break;

        case MPV_EVENT_FILE_LOADED: {
            qInfo() << m_eventName(pEvent->event_id);

            if (m_pMpvGLwidget) {
                qInfo() << "hwdec-interop" << my_get_property(m_handle, "gpu-hwdec-interop")
                        << "codec: " << my_get_property(m_handle, "video-codec")
                        << "format: " << my_get_property(m_handle, "video-format");
            }
//            if (!m_bIsJingJia) {
//#ifdef __mips__
//                qInfo() << "MPV_EVENT_FILE_LOADED __mips__";
//                QString sCodec = my_get_property(m_handle, "video-codec").toString();
//                auto name = my_get_property(m_handle, "filename").toString();
//                if (sCodec.toLower().contains("wmv3") || sCodec.toLower().contains("wmv2") || sCodec.toLower().contains("mpeg2video") ||
//                        name.toLower().contains("wmv")) {
//                    qInfo() << "my_set_property hwdec no";
//                    my_set_property(m_handle, "hwdec", "no");
//                }
//#endif
//#ifdef __aarch64__
//                qInfo() << "MPV_EVENT_FILE_LOADED aarch64";
//                QString sCodec = my_get_property(m_handle, "video-codec").toString();
//                if (sCodec.toLower().contains("wmv3") || sCodec.toLower().contains("wmv2") || sCodec.toLower().contains("mpeg2video")) {
//                    qInfo() << "my_set_property hwdec auto";
//                    if (CompositingManager::get().isOnlySoftDecode()) {
//                        my_set_property(m_handle, "hwdec", "off");
//                    } else {
//                        my_set_property(m_handle, "hwdec", "auto");
//                    }
//                }
//#endif
//            }
            //设置播放参数
            QMap<QString, QString>::iterator iter = m_pConfig->begin();
            qInfo() << __func__ << "Set mpv propertys!!";
            while (iter != m_pConfig->end()) {
                my_set_property(m_handle, iter.key(), iter.value());
                iter++;
            }

            setState(PlayState::Playing); //might paused immediately
            emit fileLoaded();
            qInfo() << QString("rotate metadata: dec %1, out %2")
                    .arg(my_get_property(m_handle, "video-dec-params/rotate").toInt())
                    .arg(my_get_property(m_handle, "video-params/rotate").toInt());
            break;
        }
        case MPV_EVENT_VIDEO_RECONFIG: {
            QSize size = videoSize();
            if (!size.isEmpty())
                emit videoSizeChanged();
            break;
        }

        case MPV_EVENT_END_FILE: {
#ifndef _LIBDMR_
            MovieConfiguration::get().updateUrl(this->_file,
                                                ConfigKnownKey::StartPos, 0);
#endif
            mpv_event_end_file *ev_ef = reinterpret_cast<mpv_event_end_file *>(pEvent->data);
            qInfo() << m_eventName(pEvent->event_id) <<
                    "reason " << ev_ef->reason;

            setState(PlayState::Stopped);
            break;
        }

        case MPV_EVENT_IDLE:
            qInfo() << m_eventName(pEvent->event_id);
            setState(PlayState::Stopped);
            emit elapsedChanged();
            break;

        default:
            qInfo() << m_eventName(pEvent->event_id);
            break;
        }
    }
}

void MpvProxy::processLogMessage(mpv_event_log_message *pEvent)
{
    switch (pEvent->log_level) {
    case MPV_LOG_LEVEL_WARN:
        qWarning() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        emit mpvWarningLogsChanged(QString(pEvent->prefix), QString(pEvent->text));
        break;

    case MPV_LOG_LEVEL_ERROR:
    case MPV_LOG_LEVEL_FATAL: {
        QString strError = pEvent->text;
        if (strError.contains("Failed setup for format vdpau")) {
            m_bLastIsSpecficFormat = true;
        }
        qCritical() << QString("%1: %2").arg(pEvent->prefix).arg(strError);
        emit mpvErrorLogsChanged(QString(pEvent->prefix), strError);
    }
    break;

    case MPV_LOG_LEVEL_INFO:
        qInfo() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        break;

    default:
        qInfo() << QString("%1: %2").arg(pEvent->prefix).arg(pEvent->text);
        break;
    }
}

void MpvProxy::processPropertyChange(mpv_event_property *pEvent)
{
    QString sName = QString::fromUtf8(pEvent->name);
    if (sName != "time-pos") qInfo() << sName;

    if (sName == "time-pos") {
        emit elapsedChanged();
    } else if (sName == "volume") {
        emit volumeChanged();
    } else if (sName == "dwidth" || sName == "dheight") {
        auto sz = videoSize();
        if (!sz.isEmpty())
            emit videoSizeChanged();
        qInfo() << "update videoSize " << sz;
    } else if (sName == "aid") {
        emit aidChanged();
    } else if (sName == "sid") {
        if (m_bExternalSubJustLoaded) {
#ifndef _LIBDMR_
            MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::SubId, sid());
#endif
            m_bExternalSubJustLoaded = false;
        }
        emit sidChanged();
    } else if (sName == "mute") {
        emit muteChanged();
    } else if (sName == "sub-visibility") {
        //_hideSub = my_get_property(m_handle, "sub-visibility")
    } else if (sName == "pause") {
        auto idle = my_get_property(m_handle, "idle-active").toBool();
        if (my_get_property(m_handle, "pause").toBool()) {
            if (!idle)
                setState(PlayState::Paused);
            else
                my_set_property(m_handle, "pause", false);
        } else {
            if (state() != PlayState::Stopped) {
                setState(PlayState::Playing);
            }
        }
    } else if (sName == "core-idle") {
    } else if (sName == "paused-for-cache") {
        qInfo() << "paused-for-cache" << my_get_property_variant(m_handle, "paused-for-cache");
        emit urlpause(my_get_property_variant(m_handle, "paused-for-cache").toBool());
    }
}

bool MpvProxy::loadSubtitle(const QFileInfo &fileInfo)
{
    //movie could be in an inner state that marked as Stopped when loadfile executes
    //if (state() == PlayState::Stopped) { return true; }
    if (!fileInfo.exists())
        return false;

    QList<QVariant> args = { "sub-add", fileInfo.absoluteFilePath(), "select" };
    qInfo() << args;
    QVariant id = my_command(m_handle, args);
    if (id.canConvert<ErrorReturn>()) {
        return false;
    }

    // by settings this flag, we can match the corresponding sid change and save it
    // in the movie database
    m_bExternalSubJustLoaded = true;
    return true;
}

bool MpvProxy::isSubVisible()
{
    return my_get_property(m_handle, "sub-visibility").toBool();
}

void MpvProxy::setSubDelay(double dSecs)
{
    my_set_property(m_handle, "sub-delay", dSecs);
#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubDelay, subDelay());
#endif
}

double MpvProxy::subDelay() const
{
    return my_get_property(m_handle, "sub-delay").toDouble();
}

QString MpvProxy::subCodepage()
{
    auto cp = my_get_property(m_handle, "sub-codepage").toString();
    if (cp.startsWith("+")) {
        cp.remove(0, 1);
    }

    return cp;
}

void MpvProxy::addSubSearchPath(const QString &sPath)
{
    my_set_property(m_handle, "sub-paths", sPath);
    my_set_property(m_handle, "sub-file-paths", sPath);
}

void MpvProxy::setSubCodepage(const QString &sCodePage)
{
    QString strTmp = sCodePage;
    if (!sCodePage.startsWith("+") && sCodePage != "auto")
        strTmp.prepend('+');

    my_set_property(m_handle, "sub-codepage", strTmp);
    my_command(m_handle, {"sub-reload"});
#ifndef _LIBDMR_
    if (_file.isValid())
        MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubCodepage, subCodepage());
#endif
}

void MpvProxy::updateSubStyle(const QString &sFont, int nSize)
{
    my_set_property(m_handle, "sub-font", sFont);
    my_set_property(m_handle, "sub-font-size", nSize);
    my_set_property(m_handle, "sub-color", "#FFFFFF");
    my_set_property(m_handle, "sub-border-size", 1);
    my_set_property(m_handle, "sub-border-color", "0.0/0.0/0.0/0.50");
    my_set_property(m_handle, "sub-shadow-offset", 1);
    my_set_property(m_handle, "sub-shadow-color", "0.0/0.0/0.0/0.50");
}

void MpvProxy::showEvent(QShowEvent *pEvent)
{
    if (!m_bConnectStateChange) {
        m_bConnectStateChange = true;
    }
    Backend::showEvent(pEvent);
}

void MpvProxy::resizeEvent(QResizeEvent *pEvent)
{
    if (state() == PlayState::Stopped) {
        return;
    }
    Backend::resizeEvent(pEvent);
}

void MpvProxy::savePlaybackPosition()
{
    if (state() == PlayState::Stopped) {
        return;
    }

#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::SubId, sid());
    if (duration() - elapsed() >= 5) {
        MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::StartPos, elapsed());
    } else {
        MovieConfiguration::get().updateUrl(this->_file, ConfigKnownKey::StartPos, elapsed() - 1);
    }
#endif
}

void MpvProxy::setPlaySpeed(double dTimes)
{
    my_set_property_async(m_handle, "speed", dTimes, AsyncReplyTag::SPEED);
}

void MpvProxy::selectSubtitle(int nId)
{
    if (nId > m_movieInfo.subs.size()) {
        nId = m_movieInfo.subs.size() == 0 ? -1 : m_movieInfo.subs[0]["id"].toInt();
    }

    my_set_property(m_handle, "sid", nId);
#ifndef _LIBDMR_
    MovieConfiguration::get().updateUrl(_file, ConfigKnownKey::SubId, sid());
#endif
}

void MpvProxy::toggleSubtitle()
{
    if (state() == PlayState::Stopped) {
        return;
    }

    my_set_property(m_handle, "sub-visibility", !isSubVisible());
}

int MpvProxy::aid() const
{
    return my_get_property(m_handle, "aid").toInt();
}

int MpvProxy::sid() const
{
    return my_get_property(m_handle, "sid").toInt();
}

void MpvProxy::selectTrack(int nId)
{
    if (nId >= m_movieInfo.audios.size()) return;
    QVariant aid  = m_movieInfo.audios[nId]["id"];
    my_set_property(m_handle, "aid", aid);
}

void MpvProxy::changeSoundMode(SoundMode soundMode)
{
    QList<QVariant> listArgs;

    switch (soundMode) {
    case SoundMode::Stereo:
        listArgs << "af" << "set" << "stereotools=muter=false";
        break;
    case SoundMode::Left:
        listArgs << "af" << "set" << "stereotools=muter=true";
        break;
    case SoundMode::Right:
        listArgs << "af" << "set" << "stereotools=mutel=true";
        break;
    }

    my_command(m_handle, listArgs);
}

void MpvProxy::volumeUp()
{
    if (volume() >= 200)
        return;

    changeVolume(volume() + 10);
}

void MpvProxy::changeVolume(int nVol)
{
    my_set_property(m_handle, "volume", volumeCorrection(nVol));
}

void MpvProxy::volumeDown()
{
    if (volume() <= 0)
        return;

    changeVolume(volume() - 10);
}

int MpvProxy::volume() const
{
    int nActualVol = my_get_property(m_handle, "volume").toInt();
    int nDispalyVol = static_cast<int>((nActualVol - 40) / 60.0 * 200.0);
    return nDispalyVol;
}

int MpvProxy::videoRotation() const
{
    int nRotate = my_get_property(m_handle, "video-rotate").toInt();
    return (nRotate + 360) % 360;
}

void MpvProxy::setVideoRotation(int nDegree)
{
    my_set_property(m_handle, "video-rotate", nDegree);
}

void MpvProxy::setVideoAspect(double dValue)
{
    my_set_property(m_handle, "video-aspect", dValue);
}

double MpvProxy::videoAspect() const
{
    return my_get_property(m_handle, "video-aspect").toDouble();
}

bool MpvProxy::muted() const
{
    return my_get_property(m_handle, "mute").toBool();
}

void MpvProxy::toggleMute()
{
    QList<QVariant> listArgs = { "cycle", "mute" };
    qInfo() << listArgs;
    my_command(m_handle, listArgs);
}

void MpvProxy::setMute(bool bMute)
{
    my_set_property(m_handle, "mute", bMute);
}

void MpvProxy::slotStateChanged()
{
    m_pMpvGLwidget->setPlaying(state() != Backend::PlayState::Stopped);
    m_pMpvGLwidget->update();
}

void MpvProxy::refreshDecode()
{
    QList<QString> canHwTypes;
    //bool bIsCanHwDec = HwdecProbe::get().isFileCanHwdec(_file.url(), canHwTypes);

    if (DecodeMode::SOFTWARE == m_decodeMode) { //1.设置软解
        my_set_property(m_handle, "hwdec", "no");
    } else if (DecodeMode::HARDWARE == m_decodeMode) {//2.设置硬解
        //2.1 特殊格式
        PlayItemInfo currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
        auto codec = currentInfo.mi.videoCodec();
        auto name = _file.fileName();
        bool isSoftCodec = codec.toLower().contains("wmv") || name.toLower().contains("wmv");
#if !defined (__x86_64__)
        isSoftCodec = isSoftCodec || codec.toLower().contains("mpeg2video");
#endif
        if (isSoftCodec) {
            qInfo() << "my_set_property hwdec no";
            my_set_property(m_handle, "hwdec", "no");
        } else { //2.2 非特殊格式
            //2.2.1 特殊硬件
            QFileInfo fi("/dev/mwv206_0"); //2.2.1.1 景嘉微
            if (fi.exists()) {
                my_set_property(m_handle, "hwdec", "vdpau,vdpau-copy,vaapi,vaapi-copy");
            } else if (CompositingManager::get().isOnlySoftDecode()) { //2.2.1.2 鲲鹏920 || 曙光+英伟达 || 浪潮
                my_set_property(m_handle, "hwdec", "no");
            } else { //2.2.2 非特殊硬件 + 非特殊格式
                 my_set_property(m_handle, "hwdec","auto");
                //bIsCanHwDec ? my_set_property(m_handle, "hwdec", canHwTypes.join(',')) : my_set_property(m_handle, "hwdec", "no");
            }
        }
    } else { //3.设置自动
#ifndef _LIBDMR_
#if defined (__mips__) || defined (__aarch64__) || defined (__sw_64__)
        //龙芯 ||（ 鲲鹏920 || 曙光+英伟达 || 浪潮 ）
        if (!CompositingManager::get().hascard() || CompositingManager::get().isOnlySoftDecode()) {
            my_set_property(m_handle, "hwdec", "no");
        }
#else
    my_set_property(m_handle, "hwdec","auto");
    //bIsCanHwDec ? my_set_property(m_handle, "hwdec", canHwTypes.join(',')) : my_set_property(m_handle, "hwdec", "no");
#endif
#else
        if (CompositingManager::get().isOnlySoftDecode()) { // 鲲鹏920 || 曙光+英伟达 || 浪潮
            my_set_property(m_handle, "hwdec", "no");
        } else {
             my_set_property(m_handle, "hwdec","auto");
            //bIsCanHwDec ? my_set_property(m_handle, "hwdec", canHwTypes.join(',')) : my_set_property(m_handle, "hwdec", "no");
        }
#endif

        //play.conf
        CompositingManager::get().getMpvConfig(m_pConfig);
        QMap<QString, QString>::iterator iter = m_pConfig->begin();
        while (iter != m_pConfig->end()) {
            if (iter.key().contains(QString("hwdec"))) {
                my_set_property(m_handle, iter.key(), iter.value());
                break;
            }
            iter++;
        }
    }
}

void MpvProxy::initMember()
{
    m_nBurstStart = 0;

    m_pMpvGLwidget = nullptr;
    m_pParentWidget = nullptr;

    m_bInBurstShotting = false;
    m_posBeforeBurst = false;
    m_bPendingSeek = false;
    m_bPolling = false;
    m_bExternalSubJustLoaded = false;
    m_bConnectStateChange = false;
    m_bPauseOnStart = false;
    m_bIsJingJia = false;
    m_bInited = false;
    m_bHwaccelAuto = false;
    m_bLastIsSpecficFormat = false;

    m_sInitVo.clear();
    m_listBurstPoints.clear();
    m_mapWaitSet.clear();
    m_vecWaitCommand.clear();

    m_waitEvent = nullptr;
    m_setOptionString = nullptr;
    m_setProperty = nullptr;
    m_setPropertyAsync = nullptr;
    m_commandNode = nullptr;
    m_commandNodeAsync = nullptr;
    m_getProperty = nullptr;
    m_observeProperty = nullptr;
    m_eventName = nullptr;
    m_creat = nullptr;
    m_requestLogMessage = nullptr;
    m_setWakeupCallback = nullptr;
    m_initialize = nullptr;
    m_freeNodecontents = nullptr;
    m_pConfig = nullptr;
}

void MpvProxy::play()
{
    QList<QVariant> listArgs = { "loadfile" };
    QStringList listOpts = { };

    if (!m_bInited) {
        firstInit();
    }

    if (PlayerEngine::isAudioFile(_file.toString())) {
        my_set_property(m_handle, "vo", "null");
    } else {
        my_set_property(m_handle, "vo", m_sInitVo);
    }

    if (_file.isLocalFile()) {
        listArgs << QFileInfo(_file.toLocalFile()).absoluteFilePath();
    } else {
        listArgs << _file.url();
    }
#ifndef _LIBDMR_
    QMap<QString, QVariant> cfg = MovieConfiguration::get().queryByUrl(_file);
    QString key = MovieConfiguration::knownKey2String(ConfigKnownKey::StartPos);
    if (Settings::get().isSet(Settings::ResumeFromLast) && cfg.contains(key)) {
        listOpts << QString("start=%1").arg(cfg[key].toInt());   //如果视频长度小于1s这段代码会导致视频无法播放
    }

    key = MovieConfiguration::knownKey2String(ConfigKnownKey::SubCodepage);
    if (cfg.contains(key)) {
        listOpts << QString("sub-codepage=%1").arg(cfg[key].toString());
    }

    key = MovieConfiguration::knownKey2String(ConfigKnownKey::SubDelay);
    if (cfg.contains(key)) {
        listOpts << QString("sub-delay=%1").arg(cfg[key].toDouble());
    }

    if (!_dvdDevice.isEmpty()) {
        listOpts << QString("dvd-device=%1").arg(_dvdDevice);
    }

//注：m_bHwaccelAuto 好像是废弃了,初始化为false,此处未执行
//    if (m_bHwaccelAuto && m_bLastIsSpecficFormat) {
//        if (!m_bIsJingJia || !utils::check_wayland_env()) {
//            // hwdec could be disabled by some codecs, so we need to re-enable it
//            my_set_property(m_handle, "hwdec", "auto");
//#if defined (__mips__) || defined (__aarch64__) || defined (__sw_64__)
//            if (!CompositingManager::get().hascard() || CompositingManager::get().isOnlySoftDecode()) {
//                my_set_property(m_handle, "hwdec", "no");
//            }
//#endif
//        }
//    }
//#else
//    if (m_bHwaccelAuto) {
//        if (CompositingManager::get().isOnlySoftDecode()) {
//            my_set_property(m_handle, "hwdec", "no");
//        } else {
//            my_set_property(m_handle, "hwdec", "auto");
//        }
//    }
#endif   

    //刷新解码模式
    refreshDecode();

    if (listOpts.size()) {
        listArgs << "replace" << listOpts.join(',');
    }

    qInfo() << listArgs;

    //设置播放参数
    QMap<QString, QString>::iterator iter = m_pConfig->begin();
    qInfo() << __func__ << "Set mpv propertys!!";
    while (iter != m_pConfig->end()) {
        my_set_property(m_handle, iter.key(), iter.value());
        iter++;
    }

    my_command(m_handle, listArgs);
    my_set_property(m_handle, "pause", m_bPauseOnStart);

#ifndef _LIBDMR_
    // by giving a period of time, movie will be loaded and auto-loaded subs are
    // all ready, then load extra subs from db
    // this keeps order of subs
    QTimer::singleShot(100, [this]() {
        auto mcfg = MovieConfiguration::get().queryByUrl(_file);
        auto ext_subs = MovieConfiguration::get().getListByUrl(_file, ConfigKnownKey::ExternalSubs);
        for (const auto &sub : ext_subs) {
            if (!QFile::exists(sub)) {
                MovieConfiguration::get().removeFromListUrl(_file, ConfigKnownKey::ExternalSubs, sub);
            } else {
                loadSubtitle(sub);
            }
        }

        auto key_s = MovieConfiguration::knownKey2String(ConfigKnownKey::SubId);
        if (mcfg.contains(key_s)) {
            selectSubtitle(mcfg[key_s].toInt());
        }
    });
#endif
}


void MpvProxy::pauseResume()
{
    if (_state == PlayState::Stopped)
        return;

    my_set_property(m_handle, "pause", !paused());
}

void MpvProxy::stop()
{
    QList<QVariant> args = { "stop" };
    qInfo() << args;
    my_command(m_handle, args);
}

QImage MpvProxy::takeScreenshot()
{
    return takeOneScreenshot();
}

void MpvProxy::burstScreenshot()
{
    if (m_bInBurstShotting) {
        qWarning() << "already in burst screenshotting mode";
        return;
    }

    if (state() == PlayState::Stopped)
        return;

    //my_command(m_handle, QList<QVariant> {"revert-seek", "mark"});
    m_posBeforeBurst = my_get_property(m_handle, "time-pos");

    int nDuration = static_cast<int>(duration() / 15);

    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> uniform_dist(0, nDuration);
    m_listBurstPoints.clear();
    for (int i = 0; i < 15; i++) {
        m_listBurstPoints.append(nDuration * i + uniform_dist(g));
    }
    m_nBurstStart = 0;

    if (duration() < 35) {
        emit notifyScreenshot(QImage(), 0);
        stopBurstScreenshot();
        return;
    }
    qInfo() << "burst span " << m_nBurstStart;

    if (!paused()) pauseResume();
    m_bInBurstShotting = true;
    QTimer::singleShot(0, this, &MpvProxy::stepBurstScreenshot);
}

qint64 MpvProxy::nextBurstShootPoint()
{
    auto next = m_listBurstPoints[static_cast<int>(m_nBurstStart++)];
    if (next >= duration()) {
        next = duration() - 5;
    }

    return next;
}

int MpvProxy::volumeCorrection(int displayVol)
{
    int realVol = 0;
    if (utils::check_wayland_env()) {
        //>100时，mpv按照显示音量：mpv 10：5的比例调节音量
        realVol = displayVol > 100 ? 100 + (displayVol - 100) / 10 * 5 : displayVol;
    } else {
        realVol = static_cast<int>((displayVol / 200.0) * 60.0 + 40);
    }
    return (realVol == 40 ? 0 : realVol);
}

QVariant MpvProxy::my_get_property(mpv_handle *pHandle, const QString &sName) const
{
    mpv_node node;
    if (!m_getProperty) return QVariant();
    int err = m_getProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, &node);
    if (err < 0)
        return QVariant::fromValue(ErrorReturn(err));
    auto variant = node_to_variant(&node);
    m_freeNodecontents(&node);
    return variant;
}

int MpvProxy::my_set_property(mpv_handle *pHandle, const QString &sName, const QVariant &v)
{
    QVariant sValue = v;
#ifndef _LIBDMR_
#ifdef __x86_64__
    bool composited = CompositingManager::get().composited();
    //设置mpv硬解码时，检测是否支持硬解，不支持则设置为软解
    if(sName.compare("hwdec") == 0 && v.toString().compare("auto") == 0 && !utils::check_wayland_env() && composited)
    {
        if(!CompositingManager::isCanHwdec())
        {
            sValue = "no";
        }
    }
#endif
#else
   if(sName.compare("hwdec") == 0) {
       sValue = "no";
   }
#endif

    node_builder node(sValue);

    if (!m_bInited) {
        m_mapWaitSet.insert(sName, sValue);
        return 0;
    }

    if (!m_setProperty) return 0;
    int res = m_setProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, node.node());
    return res;
}

bool MpvProxy::my_command_async(mpv_handle *pHandle, const QVariant &args, uint64_t tag)
{
    node_builder node(args);
    int nErr = m_commandNodeAsync(pHandle, tag, node.node());
    return nErr == 0;
}

int MpvProxy::my_set_property_async(mpv_handle *pHandle, const QString &sName, const QVariant &value, uint64_t tag)
{
    node_builder node(value);
    return m_setPropertyAsync(pHandle, tag, sName.toUtf8().data(), MPV_FORMAT_NODE, node.node());
}

QVariant MpvProxy::my_get_property_variant(mpv_handle *pHandle, const QString &sName)
{
    mpv_node node;
    if (m_getProperty(pHandle, sName.toUtf8().data(), MPV_FORMAT_NODE, &node) < 0)
        return QVariant();
    my_node_autofree f(&node);
    return node_to_variant(&node);
}

QVariant MpvProxy::my_command(mpv_handle *pHandle, const QVariant &args)
{
    if (!m_bInited) {
        m_vecWaitCommand.append(args);
        return QVariant();
    }

    node_builder node(args);
    mpv_node res;
    int nErr = m_commandNode(pHandle, node.node(), &res);
    if (nErr < 0)
        return QVariant::fromValue(ErrorReturn(nErr));
    auto variant = node_to_variant(&res);
    m_freeNodecontents(&res);
    return variant;
}

QImage MpvProxy::takeOneScreenshot()
{
    if (state() == PlayState::Stopped) return QImage();

    QList<QVariant> args = {"screenshot-raw"};
    node_builder node(args);
    mpv_node res;
    int nErr = m_commandNode(m_handle, node.node(), &res);
    if (nErr < 0) {
        qWarning() << "screenshot raw failed";
        return QImage();
    }

    Q_ASSERT(res.format == MPV_FORMAT_NODE_MAP);

    int w = 0, h = 0, stride = 0;

    mpv_node_list *pNodeList = res.u.list;
    uchar *pData = nullptr;

    for (int n = 0; n < pNodeList->num; n++) {
        auto key = QString::fromUtf8(pNodeList->keys[n]);
        if (key == "w") {
            w = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "h") {
            h = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "stride") {
            stride = static_cast<int>(pNodeList->values[n].u.int64);
        } else if (key == "format") {
            auto format = QString::fromUtf8(pNodeList->values[n].u.string);
            qInfo() << "format" << format;
        } else if (key == "data") {
            pData = static_cast<uchar *>(pNodeList->values[n].u.ba->data);
        }
    }

    if (pData) {
        //alpha should be ignored
        auto img = QImage(static_cast<const uchar *>(pData), w, h, stride, QImage::Format_RGB32);
        img.bits();
        int rotationdegree = videoRotation();
        if (rotationdegree && CompositingManager::get().composited()) {      //只有opengl窗口需要自己旋转
            QMatrix matrix;
            matrix.rotate(rotationdegree);
            img = QPixmap::fromImage(img).transformed(matrix, Qt::SmoothTransformation).toImage();
        }
        m_freeNodecontents(&res);
        return img;
    }

    m_freeNodecontents(&res);
    qInfo() << "failed";
    return QImage();
}

void MpvProxy::stepBurstScreenshot()
{
    if (!m_bInBurstShotting) {
        return;
    }

    auto pos = nextBurstShootPoint();
    my_command(m_handle, QList<QVariant> {"seek", pos, "absolute"});
//    int tries = 10;
    while (true) {
        mpv_event *pEvent = m_waitEvent(m_handle, 0.005);
        if (pEvent->event_id == MPV_EVENT_NONE)
            continue;

        if (pEvent->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            qInfo() << "seek finished" << elapsed();
            break;
        }

        if (pEvent->event_id == MPV_EVENT_END_FILE) {
            qInfo() << "seek finished (end of file)" << elapsed();
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
    m_bInBurstShotting = false;
    //my_command(m_handle, QList<QVariant> {"revert-seek", "mark"});
    my_set_property(m_handle, "time-pos", m_posBeforeBurst);
}

void MpvProxy::seekForward(int nSecs)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    QList<QVariant> listArgs = { "seek", QVariant(nSecs), "relative+exact" };
    qInfo() << listArgs;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
    m_bPendingSeek = true;
}

void MpvProxy::seekBackward(int nSecs)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    if (nSecs > 0)
        nSecs = -nSecs;
    QList<QVariant> listArgs = { "seek", QVariant(nSecs), "relative+exact" };
    qInfo() << listArgs;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
    m_bPendingSeek = true;
}

void MpvProxy::seekAbsolute(int nPos)
{
    if (state() == PlayState::Stopped) return;

    if (m_bPendingSeek) return;
    QList<QVariant> listArgs = { "seek", QVariant(nPos), "absolute" };
    qInfo() << listArgs;
    //command(m_handle, args);
    m_bPendingSeek = true;
    my_command_async(m_handle, listArgs, AsyncReplyTag::SEEK);
}

QSize MpvProxy::videoSize() const
{
    if (state() == PlayState::Stopped) return QSize(-1, -1);
    QSize size = QSize(my_get_property(m_handle, "dwidth").toInt(),
                       my_get_property(m_handle, "dheight").toInt());

    auto r = my_get_property(m_handle, "video-out-params/rotate").toInt();
    if (r == 90 || r == 270) {
        size.transpose();
    }

    return size;
}

qint64 MpvProxy::duration() const
{
    return my_get_property(m_handle, "duration").value<qint64>();
}


qint64 MpvProxy::elapsed() const
{
    if (state() == PlayState::Stopped) return 0;
    return  my_get_property(m_handle, "time-pos").value<qint64>();

}

void MpvProxy::updatePlayingMovieInfo()
{
    m_movieInfo.subs.clear();
    m_movieInfo.audios.clear();

    QList<QVariant> listInfo = my_get_property(m_handle, "track-list").toList();
    auto p = listInfo.begin();
    while (p != listInfo.end()) {
        const auto &t = p->toMap();
        if (t["type"] == "audio") {
            AudioInfo audioInfo;
            audioInfo["type"] = t["type"];
            audioInfo["id"] = t["id"];
            audioInfo["lang"] = t["lang"];
            audioInfo["external"] = t["external"];
            audioInfo["external-filename"] = t["external-filename"];
            audioInfo["selected"] = t["selected"];
            audioInfo["title"] = t["title"];

            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    audioInfo["title"] = t["lang"];
                else if (!t["external"].toBool())
                    audioInfo["title"] = "[internal]";
            }


            m_movieInfo.audios.append(audioInfo);
        } else if (t["type"] == "sub") {
            SubtitleInfo titleInfo;
            titleInfo["type"] = t["type"];
            titleInfo["id"] = t["id"];
            titleInfo["lang"] = t["lang"];
            titleInfo["external"] = t["external"];
            titleInfo["external-filename"] = t["external-filename"];
            titleInfo["selected"] = t["selected"];
            titleInfo["title"] = t["title"];
            if (t["title"].toString().size() == 0) {
                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
                    titleInfo["title"] = t["lang"];
                else if (!t["external"].toBool())
                    titleInfo["title"] = tr("Internal");
            }
            m_movieInfo.subs.append(titleInfo);
        }
        ++p;
    }

    qInfo() << m_movieInfo.subs;
    qInfo() << m_movieInfo.audios;
}

void MpvProxy::nextFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> listArgs = { "frame-step"};
    my_command(m_handle, listArgs);
}

void MpvProxy::previousFrame()
{
    if (state() == PlayState::Stopped) return;

    QList<QVariant> listArgs = { "frame-back-step"};
    my_command(m_handle, listArgs);
}

void MpvProxy::changehwaccelMode(hwaccelMode hwaccelMode)
{
    switch (hwaccelMode) {
    case hwaccelAuto:
        m_bHwaccelAuto = true;
        break;
    case hwaccelOpen:
        m_bHwaccelAuto = false;
        my_set_property(m_handle, "hwdec", "auto");
        break;
    case hwaccelClose:
        m_bHwaccelAuto = false;
        my_set_property(m_handle, "hwdec", "off");
        break;
    }
}

void MpvProxy::makeCurrent()
{
    m_pMpvGLwidget->makeCurrent();
}

QVariant MpvProxy::getProperty(const QString &sName)
{
    return my_get_property(m_handle, sName.toUtf8().data());
}

void MpvProxy::setProperty(const QString &sName, const QVariant &val)
{
    if (sName == "pause-on-start") {
        m_bPauseOnStart = val.toBool();
    } else if (sName == "video-zoom") {
        my_set_property(m_handle, sName, val.toDouble());
    } else {
        my_set_property(m_handle, sName.toUtf8().data(), val);
    }
}

} // end of namespace dmr

