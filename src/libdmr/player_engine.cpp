// Copyright (C) 2020 ~ 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ddlog.h"
#include <iostream>
#include "player_engine.h"
#include "playlist_model.h"
#include "movie_configuration.h"
#include "online_sub.h"
#include "mpv_proxy.h"
#include "compositing_manager.h"
#include "dguiapplicationhelper.h"
#include "filefilter.h"
// #include "qtplayer_proxy.h"
#include "eventlogutils.h"

#include <QPainterPath>

#ifndef _LIBDMR_
#include "dmr_settings.h"
#endif

#include "drecentmanager.h"
DCORE_USE_NAMESPACE
DGUI_USE_NAMESPACE
#define AV_CODEC_ID_AVS2 192
#define AV_CODEC_ID_AVS3 193

namespace dmr {

const QStringList PlayerEngine::audio_filetypes = {"*.mp3", "*.wav", "*.wma", "*.m4a", "*.aac", "*.ac3", "*.ape", "*.flac", "*.ra", "*.mka", "*.dts", "*.opus", "*.amr"};
const QStringList PlayerEngine::video_filetypes = {
    "*.3g2", "*.3ga", "*.3gp", "*.3gp2", "*.3gpp", "*.amv", "*.asf", "*.asx", "*.avf", "*.avi", "*.bdm", "*.bdmv", "*.bik", "*.clpi", "*.cpi",
    "*.dat", "*.divx", "*.drc", "*.dv", "*.dvr-ms", "*.f4v", "*.flv", "*.gvi", "*.gxf", "*.hdmov", "*.hlv", "*.iso", "*.letv", "*.lrv", "*.m1v", "*.m2p", "*.m2t", "*.m2ts",
    "*.m2v", "*.m3u", "*.m3u8", "*.m4v", "*.mkv", "*.moov", "*.mov", "*.mov", "*.mp2", "*.mp2v", "*.mp4", "*.mp4v", "*.mpe", "*.mpeg", "*.mpeg1", "*.mpeg2", "*.mpeg4", "*.mpg",
    "*.mpl", "*.mpls", "*.mpv", "*.mpv2", "*.mqv", "*.mts", "*.mts", "*.mtv", "*.mxf", "*.mxg", "*.nsv", "*.nuv", "*.ogg", "*.ogm", "*.ogv", "*.ogx", "*.ps", "*.qt", "*.qtvr",
    "*.ram", "*.rec", "*.rm", "*.rm", "*.rmj", "*.rmm", "*.rms", "*.rmvb", "*.rmx", "*.rp", "*.rpl", "*.rv", "*.rvx", "*.thp", "*.tod", "*.tp", "*.trp", "*.ts", "*.tts", "*.txd",
    "*.vcd", "*.vdr", "*.vob", "*.vp8", "*.vro", "*.webm", "*.wm", "*.wmv", "*.wtv", "*.xesc", "*.xspf", "*.ogg",
};

const QStringList PlayerEngine::subtitle_suffixs = {"ass", "sub", "srt", "aqt", "jss", "gsub", "ssf", "ssa", "smi", "usf", "idx"};

PlayerEngine::PlayerEngine(QWidget *parent)
    : QWidget(parent)
{
    qCDebug(dmrLib) << "Initializing PlayerEngine";
    m_bAudio = false;
    m_stopRunningThread = false;
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    if(CompositingManager::isMpvExists()){
        qCDebug(dmrLib) << "MPV backend exists, creating MpvProxy";
        _current = new MpvProxy(this);
    }
    // else {
    //     _current = new QtPlayerProxy(this);
    // }
    if (_current) {
        qCDebug(dmrLib) << "Setting up backend connections";
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
        connect(_current, &Backend::mpvErrorLogsChanged, this, &PlayerEngine::mpvErrorLogsChanged);
        connect(_current, &Backend::mpvWarningLogsChanged, this, &PlayerEngine::mpvWarningLogsChanged);
        connect(_current, &Backend::urlpause, this, &PlayerEngine::urlpause);
        connect(_current, &Backend::sigMediaError, this, &PlayerEngine::sigMediaError);
        l->addWidget(_current);
    } else {
        qCWarning(dmrLib) << "Failed to create backend";
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(&_networkConfigMng, &QNetworkConfigurationManager::onlineStateChanged, 
            this, &PlayerEngine::onlineStateChanged);
#endif

    setLayout(l);


#ifndef _LIBDMR_
    connect(&Settings::get(), &Settings::subtitleChanged, this, &PlayerEngine::updateSubStyles);
#endif

    connect(&OnlineSubtitle::get(), &OnlineSubtitle::subtitlesDownloadedFor,
            this, &PlayerEngine::onSubtitlesDownloaded);

    _playlist = new PlaylistModel(this);
    connect(_playlist, &PlaylistModel::asyncAppendFinished, this,
            &PlayerEngine::onPlaylistAsyncAppendFinished, Qt::DirectConnection);
    connect(_playlist, &PlaylistModel::updateDuration, this, &PlayerEngine::updateDuration);
    qCDebug(dmrLib) << "PlayerEngine initialization completed";
}

PlayerEngine::~PlayerEngine()
{
    qCDebug(dmrLib) << "Destroying PlayerEngine";
    m_stopRunningThread = true;
    FileFilter::instance()->stopThread();
    if (_current) {
        disconnect(_current, nullptr, nullptr, nullptr);
        delete _current;
        _current = nullptr;
    }
    
    if (_playlist) {
        disconnect(_playlist, nullptr, nullptr, nullptr);
        delete _playlist;
        _playlist = nullptr;
    }
    qCDebug(dmrLib) << "PlayerEngine destroyed";
}

bool PlayerEngine::isPlayableFile(const QUrl &url)
{
    qCDebug(dmrLib) << "Checking if file is playable:" << url.toString();
    if (FileFilter::instance()->isMediaFile(url)) {
        qCDebug(dmrLib) << "File is playable";
        return true;
    } else {    // 网络文件不提示
        if(url.isLocalFile()) {
            qCWarning(dmrLib) << "Invalid file:" << QFileInfo(url.toLocalFile()).fileName();
            emit sigInvalidFile(QFileInfo(url.toLocalFile()).fileName());
        }
        return false;
    }
}

static QStringList suffixes;

bool PlayerEngine::isPlayableFile(const QString &name)
{
    qCDebug(dmrLib) << "Enter isPlayableFile function";
    QUrl url = FileFilter::instance()->fileTransfer(name);

    if (FileFilter::instance()->isMediaFile(url))
    {
        qCDebug(dmrLib) << "File is playable";
        return true;
    }

    if (url.isLocalFile()) {   // 网络文件不提示
        qCDebug(dmrLib) << "File is not playable";
        emit sigInvalidFile(QFileInfo(url.toLocalFile()).fileName());
        return false;
    }
    qCDebug(dmrLib) << "File is not playable";
    return  false;
}

bool PlayerEngine::isAudioFile(const QString &name)
{
    qCDebug(dmrLib) << "Enter isAudioFile function";
    QUrl url = FileFilter::instance()->fileTransfer(name);
    qCDebug(dmrLib) << "File is audio";
    return FileFilter::instance()->isAudio(url);
}

bool PlayerEngine::isSubtitle(const QString &name)
{
    qCDebug(dmrLib) << "Enter isSubtitle function";
    QUrl url = FileFilter::instance()->fileTransfer(name);
    qCDebug(dmrLib) << "File is subtitle";
    return FileFilter::instance()->isSubtitle(url);
}

void PlayerEngine::updateSubStyles()
{
    qCDebug(dmrLib) << "Updating subtitle styles";
#ifndef _LIBDMR_
    QPointer<DSettingsOption> pFontOpt = Settings::get().settings()->option("subtitle.font.family");
    QPointer<DSettingsOption> pSizeOpt = Settings::get().settings()->option("subtitle.font.size");
    if(!pFontOpt || !pSizeOpt) {
        qCWarning(dmrLib) << "Failed to get subtitle style settings";
        return;
    }

    int fontId = pFontOpt->value().toInt();
    int size = pSizeOpt->value().toInt();
    QString font = pFontOpt->data("items").toStringList()[fontId];
    qCDebug(dmrLib) << "Subtitle style - Font:" << font << "Size:" << size;

    if (_state != CoreState::Idle) {
        if (_playlist->current() < 0) {
            qCDebug(dmrLib) << "No current playlist item";
            return;
        }

        auto vh = videoSize().height();
        if (vh <= 0) {
            vh = _playlist->currentInfo().mi.height;
        }
        double scale = vh / 720.0;
        size /= scale;
        /* magic scale number 2.0 comes from my mind, test with my eyes... */
        size *= 2.0;
        qCDebug(dmrLib) << "Updating subtitle style with scaled size:" << size;
        updateSubStyle(font, size);
    }
#endif
}

void PlayerEngine::waitLastEnd()
{
    qCDebug(dmrLib) << "Enter waitLastEnd function";
    if (MpvProxy *mpv = dynamic_cast<MpvProxy *>(_current)) {
        mpv->pollingEndOfPlayback();
    }
    // TODO
    // else if (QtPlayerProxy *qtPlayer = dynamic_cast<QtPlayerProxy *>(_current)) {
    //     qtPlayer->pollingEndOfPlayback();
    // }
    qCDebug(dmrLib) << "Exiting waitLastEnd function";
}

void PlayerEngine::onBackendStateChanged()
{
    if (!_current) {
        qCWarning(dmrLib) << "Backend state changed but no backend exists";
        return;
    }

    CoreState old = _state;
    switch (_current->state()) {
    case Backend::PlayState::Playing:
        _state = CoreState::Playing;
        if (_playlist->count() > 0) {
            m_bAudio = currFileIsAudio();
            qCDebug(dmrLib) << "State changed to Playing, is audio:" << m_bAudio;
        }
        //playing . emit thumbnail progress mode signal with setting file
        if (old == CoreState::Idle) {
            qCDebug(dmrLib) << "Initializing thumbnail settings";
            emit siginitthumbnailseting();
        }
        break;
    case Backend::PlayState::Paused:
        _state = CoreState::Paused;
        qCDebug(dmrLib) << "State changed to Paused";
        break;
    case Backend::PlayState::Stopped:
        _state = CoreState::Idle;
        qCDebug(dmrLib) << "State changed to Idle";
        break;
    }

    updateSubStyles();
    if (old != _state) {
        qCDebug(dmrLib) << "State changed from" << old << "to" << _state;
        emit stateChanged();
    }

    auto systemEnv = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));
    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
#ifndef USE_TEST
        if (_state == CoreState::Idle) {
            QPalette pal(qApp->palette());
            this->setAutoFillBackground(true);
            this->setPalette(pal);
        } else {
            QPalette pal(this->palette());
            pal.setColor(QPalette::Window, Qt::black);
            this->setAutoFillBackground(true);
            this->setPalette(pal);
        }
#endif
    }
    qCDebug(dmrLib) << "Exiting onBackendStateChanged function";
}

PlayerEngine::CoreState PlayerEngine::state()
{
    qCDebug(dmrLib) << "Enter state function";
    if (!_current)
        return _state;
    auto old = _state;
    switch (_current->state()) {
    case Backend::PlayState::Playing:
        qCDebug(dmrLib) << "State changed to Playing";
        _state = CoreState::Playing;
        break;
    case Backend::PlayState::Paused:
        qCDebug(dmrLib) << "State changed to Paused";
        _state = CoreState::Paused;
        break;
    case Backend::PlayState::Stopped:
        qCDebug(dmrLib) << "State changed to Idle";
        _state = CoreState::Idle;
        break;
    }

    if (old != _state) {
        qCWarning(dmrLib) << "###### state mismatch" << old << _state;
        emit stateChanged();
    }
    qCDebug(dmrLib) << "Exiting state function";
    return _state;
}

const PlayingMovieInfo &PlayerEngine::playingMovieInfo()
{
    qCDebug(dmrLib) << "Enter playingMovieInfo function";
    static PlayingMovieInfo empty;

    if (!_current) {
        qCDebug(dmrLib) << "current is null, return empty";
        return empty;
    }
    qCDebug(dmrLib) << "Exiting playingMovieInfo function";
    return _current->playingMovieInfo();
}

int PlayerEngine::aid()
{
    qCDebug(dmrLib) << "Enter aid function";
    if (state() == CoreState::Idle) {
        return 0;
    }
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0";
        return 0;
    }

    return _current->aid();
}

int PlayerEngine::sid()
{
    qCDebug(dmrLib) << "Enter sid function";
    if (state() == CoreState::Idle) {
        return 0;
    }
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0";
        return 0;
    }

    return _current->sid();
}

void PlayerEngine::onSubtitlesDownloaded(const QUrl &url, const QList<QString> &filenames,
                                         OnlineSubtitle::FailReason reason)
{
    qCDebug(dmrLib) << "Enter onSubtitlesDownloaded function";
    //mod for warning by xxj ,no any means
    reason = OnlineSubtitle::FailReason::NoError;

    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "state is Idle, return";
        return;
    }

    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }

    if (playlist().count() <= 0 || playlist().currentInfo().url != url) {
        qCDebug(dmrLib) << "playlist().currentInfo().url != url, return";
        return;
    }

    bool res = false;

    for (auto &filename : filenames) {
        if (true == _current->loadSubtitle(QFileInfo(filename))) {
            res = true;
        } else {
            QFile::remove(filename);
        }
    }

    emit loadOnlineSubtitlesFinished(url, res);
    qCDebug(dmrLib) << "Exiting onSubtitlesDownloaded function";
}

bool PlayerEngine::loadSubtitle(const QFileInfo &fi)
{
    qCDebug(dmrLib) << "Loading subtitle:" << fi.absoluteFilePath();
    if (state() == CoreState::Idle || playlist().count() <= 0) {
        qCDebug(dmrLib) << "Player is idle, ignoring subtitle load";
        return true;
    }
    if (!_current) {
        qCWarning(dmrLib) << "No backend available";
        return true;
    }

    const auto &pmf = _current->playingMovieInfo();
    auto pif = playlist().currentInfo();
    int i = 0;
    for (const auto &sub : pmf.subs) {
        if (sub["external"].toBool()) {
            auto path = sub["external-filename"].toString();
            if (path == fi.canonicalFilePath()) {
                qCDebug(dmrLib) << "Subtitle already loaded, selecting it";
                this->selectSubtitle(i);
                return true;
            }
        }
        ++i;
    }

    if (_current->loadSubtitle(fi)) {
        qCDebug(dmrLib) << "Subtitle loaded successfully";
#ifndef _LIBDMR_
        MovieConfiguration::get().append2ListUrl(pif.url, ConfigKnownKey::ExternalSubs,
                                                 fi.canonicalFilePath());
#endif
        return true;
    }
    qCWarning(dmrLib) << "Failed to load subtitle";
    return false;
}

void PlayerEngine::loadOnlineSubtitle(const QUrl &url)
{
    qCDebug(dmrLib) << "Requesting online subtitle for:" << url.toString();
    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "Player is idle, ignoring subtitle request";
        return;
    }
    if (!_current) {
        qCWarning(dmrLib) << "No backend available";
        return;
    }

    OnlineSubtitle::get().requestSubtitle(url);
}

void PlayerEngine::setPlaySpeed(double times)
{
    qCDebug(dmrLib) << "Enter setPlaySpeed function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setPlaySpeed(times);
}

void PlayerEngine::setSubDelay(double secs)
{
    qCDebug(dmrLib) << "Enter setSubDelay function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }

    _current->setSubDelay(secs + _current->subDelay());
}

double PlayerEngine::subDelay() const
{
    qCDebug(dmrLib) << "Enter subDelay function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0.0";
        return 0.0;
    }
    return _current->subDelay();
}

QString PlayerEngine::subCodepage()
{
    qCDebug(dmrLib) << "Enter subCodepage function";
    if (!_current)
        return "auto";
    if (_current->subCodepage().isEmpty()) {
        qCDebug(dmrLib) << "subCodepage is empty, return auto";
        return "auto";
    } else {
        qCDebug(dmrLib) << "subCodepage is not empty, return" << _current->subCodepage();
        return _current->subCodepage();
    }

}

void PlayerEngine::setSubCodepage(const QString &cp)
{
    qCDebug(dmrLib) << "Enter setSubCodepage function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setSubCodepage(cp);
    qCDebug(dmrLib) << "Exiting setSubCodepage function";
//    emit subCodepageChanged();
}

void PlayerEngine::addSubSearchPath(const QString &path)
{
    qCDebug(dmrLib) << "Enter addSubSearchPath function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->addSubSearchPath(path);
    qCDebug(dmrLib) << "Exiting addSubSearchPath function";
}

void PlayerEngine::updateSubStyle(const QString &font, int sz)
{
    qCDebug(dmrLib) << "Enter updateSubStyle function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->updateSubStyle(font, sz / 2);
    qCDebug(dmrLib) << "Exiting updateSubStyle function";
}

void PlayerEngine::selectSubtitle(int id)
{
    qCDebug(dmrLib) << "Enter selectSubtitle function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    if (state() != CoreState::Idle) {
        qCDebug(dmrLib) << "state is not Idle, return";
        const auto &pmf = _current->playingMovieInfo();
        if (id >= pmf.subs.size()) return;
        auto sid = pmf.subs[id]["id"].toInt();
        _current->selectSubtitle(sid);
    }
    qCDebug(dmrLib) << "Exiting selectSubtitle function";
}

bool PlayerEngine::isSubVisible()
{
    qCDebug(dmrLib) << "Enter isSubVisible function";
    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "state is Idle, return false";
        return false;
    }
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return false";
        return false;
    }
    qCDebug(dmrLib) << "Exiting isSubVisible function";
    return _current->isSubVisible();
}

void PlayerEngine::toggleSubtitle()
{
    qCDebug(dmrLib) << "Enter toggleSubtitle function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->toggleSubtitle();
    qCDebug(dmrLib) << "Exiting toggleSubtitle function";
}

void PlayerEngine::selectTrack(int id)
{
    qCDebug(dmrLib) << "Enter selectTrack function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->selectTrack(id);
    qCDebug(dmrLib) << "Exiting selectTrack function";
}

void PlayerEngine::volumeUp()
{
    qCDebug(dmrLib) << "Enter volumeUp function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->volumeUp();
    qCDebug(dmrLib) << "Exiting volumeUp function";
}

void PlayerEngine::changeVolume(int val)
{
    qCDebug(dmrLib) << "Enter changeVolume function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->changeVolume(val);
    qCDebug(dmrLib) << "Exiting changeVolume function";
}

void PlayerEngine::volumeDown()
{
    qCDebug(dmrLib) << "Enter volumeDown function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->volumeDown();
    qCDebug(dmrLib) << "Exiting volumeDown function";
}

int PlayerEngine::volume() const
{
    qCDebug(dmrLib) << "Enter volume function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 100";
        return 100;
    }
    qCDebug(dmrLib) << "Exiting volume function";
    return _current->volume();
}

bool PlayerEngine::muted() const
{
    qCDebug(dmrLib) << "Enter muted function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return false";
        return false;
    }
    qCDebug(dmrLib) << "Exiting muted function";
    return _current->muted();
}

void PlayerEngine::changehwaccelMode(Backend::hwaccelMode hwaccelMode)
{
    qCDebug(dmrLib) << "Enter changehwaccelMode function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    qCDebug(dmrLib) << "Exiting changehwaccelMode function";
    return _current->changehwaccelMode(hwaccelMode);
}


void PlayerEngine::setDecodeModel(const QVariant &value)
{
    MpvProxy *pMpv = static_cast<MpvProxy *>(_current);
    if (pMpv) {
        pMpv->setDecodeModel(value);
    }
}

QVariant PlayerEngine::getDecodeModel()
{
    MpvProxy *pMpv = static_cast<MpvProxy *>(_current);
    if (pMpv) {
        return pMpv->getDecodeModel();
    }
    return QVariant();
}

Backend *PlayerEngine::getMpvProxy()
{
    qCDebug(dmrLib) << "Enter getMpvProxy function";
    return _current;
}

void PlayerEngine::toggleMute()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::toggleMute().";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, returning.";
        return;
    }
    //发送信号通知初始化库函数
    if (!m_bMpvFunsLoad) {
        qCDebug(dmrLib) << "mpv functions not loaded, emitting mpvFunsLoadOver().";
        emit mpvFunsLoadOver();
        m_bMpvFunsLoad = true;
    }

    _current->toggleMute();
    emit volumeChanged();
    qCDebug(dmrLib) << "Exiting PlayerEngine::toggleMute().";
}

void PlayerEngine::setMute(bool bMute)
{
    qCDebug(dmrLib) << "Entering PlayerEngine::setMute() with bMute:" << bMute;
    if (!_current) return;
    _current->setMute(bMute);
    qCDebug(dmrLib) << "Exiting PlayerEngine::setMute().";
}

void PlayerEngine::savePreviousMovieState()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::savePreviousMovieState().";
    savePlaybackPosition();
    qCDebug(dmrLib) << "Exiting PlayerEngine::savePreviousMovieState().";
}

void PlayerEngine::paintEvent(QPaintEvent *e)
{
    qCDebug(dmrLib) << "Entering PlayerEngine::paintEvent().";
#ifndef USE_TEST
    QRect rect = this->rect();
    QPainter p(this);

    if (!CompositingManager::get().composited() || utils::check_wayland_env()) {  // wayland下不会进入mainwindow的paintevent函数导致图标未绘制
        qCDebug(dmrLib) << "Compositing manager not composited or wayland environment detected.";
        if (_state != Idle && m_bAudio) {
            qCDebug(dmrLib) << "Player is not idle and is audio, filling black rectangle.";
            p.fillRect(rect, QBrush(QColor(0, 0, 0)));
        } else {
            qCDebug(dmrLib) << "Player is idle or not audio, drawing icon.";
            qreal dpr = devicePixelRatioF();
            int iconSize = static_cast<int>(130 * dpr);
            QImage icon = QIcon::fromTheme("deepin-movie").pixmap(iconSize, iconSize).toImage();
            icon.setDevicePixelRatio(dpr);
            QPixmap pix = QPixmap::fromImage(icon);
            QPointF pos = rect.center() - QPointF(pix.width() / 2.0 / dpr, pix.height() / 2.0 / dpr);

            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                qCDebug(dmrLib) << "Theme type is Light, filling white rectangle and drawing pixmap.";
                p.fillRect(rect, QBrush(QColor(255, 255, 255)));
                p.drawPixmap(pos, pix);
            } else {
                qCDebug(dmrLib) << "Theme type is not Light, filling black rectangle and drawing pixmap.";
                p.fillRect(rect, QBrush(QColor(0, 0, 0)));
                p.drawPixmap(pos, pix);
            }
        }
    } else {
        qCDebug(dmrLib) << "Compositing manager is composited and not wayland environment, skipping custom paint.";
    }
#endif
    qCDebug(dmrLib) << "Exiting PlayerEngine::paintEvent().";
    return QWidget::paintEvent(e);
}

void PlayerEngine::requestPlay(int id)
{
    qCDebug(dmrLib) << "Requesting play for item" << id;
    if (!_current) {
        qCWarning(dmrLib) << "No backend available";
        return;
    }
    if (id >= _playlist->count()) {
        qCWarning(dmrLib) << "Invalid playlist index:" << id;
        return;
    }

    const auto &item = _playlist->items()[id];
    qCDebug(dmrLib) << "Setting play file:" << item.url.toString();
    _current->setPlayFile(item.url);

    DRecentData data;
    data.appName = "Deepin Movie";
    data.appExec = "deepin-movie";
    DRecentManager::addItem(item.url.toLocalFile(), data);

    if (!_current) return;
    if (_current->isPlayable()) {
        qCDebug(dmrLib) << "File is playable, starting playback";
#ifdef __sw_64__
        // 1.1.0以上版本的dav1d在多线程环境下会卡死，更换解码器使用
        if(!FileFilter::instance()->isFormatSupported(item.url)) {
            qCDebug(dmrLib) << "Using libaom-av1 decoder for unsupported format";
            _current->setProperty("vd", "libaom-av1");
        }
#endif
        _current->play();
    } else {
        // TODO: delete and try next backend?
        qCDebug(dmrLib) << "File is not playable, considering next backend or handling error.";
    }

    QJsonObject obj{
        {"tid", EventLogUtils::StartPlaying},
        {"version", VERSION},
        {"successful", item.url.isLocalFile() ? "true" : ""},
        {"type", currFileIsAudio() ? "audio" : "video"},
        {"origin", item.url.isLocalFile() ? "local" : "http"},
        {"encapsulation_format", item.mi.fileType},
        {"coding_format",  utils::videoIndex2str(item.mi.vCodecID)}
    };

    qCDebug(dmrLib) << "Logging playback event:" << obj;
    EventLogUtils::get().writeLogs(obj);
    qCDebug(dmrLib) << "Exiting PlayerEngine::requestPlay().";
}

void PlayerEngine::savePlaybackPosition()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::savePlaybackPosition().";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, returning.";
        return;
    }
    _current->savePlaybackPosition();
    qCDebug(dmrLib) << "Exiting PlayerEngine::savePlaybackPosition().";
}

void PlayerEngine::nextFrame()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::nextFrame().";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, returning.";
        return;
    }
    _current->nextFrame();
    qCDebug(dmrLib) << "Exiting PlayerEngine::nextFrame().";
}

void PlayerEngine::previousFrame()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::previousFrame().";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, returning.";
        return;
    }
    _current->previousFrame();
    qCDebug(dmrLib) << "Exiting PlayerEngine::previousFrame().";
}

void PlayerEngine::makeCurrent()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::makeCurrent().";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, returning.";
        return;
    }
    _current->makeCurrent();
    qCDebug(dmrLib) << "Exiting PlayerEngine::makeCurrent().";
}

void PlayerEngine::play()
{
    qCDebug(dmrLib) << "Play requested";
    if (!_current || !_playlist->count()) {
        qCWarning(dmrLib) << "Cannot play: no backend or empty playlist";
        return;
    }
    if (_playingRequest) return;

    if (state() == CoreState::Paused &&
            getBackendProperty("keep-open").toBool() &&
            getBackendProperty("eof-reached").toBool()) {
        qCDebug(dmrLib) << "End of file reached, stopping and playing next";
        stop();
        next();
    } else if (state() == CoreState::Idle) {
        playByName(_playlist->currentInfo().url);
    }
    qCDebug(dmrLib) << "Exiting PlayerEngine::play().";
}

void PlayerEngine::prev()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::prev().";
    if (_playingRequest) {
        qCDebug(dmrLib) << "_playingRequest is true, returning.";
        return;
    }
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playPrev(true);
    _playingRequest = false;
    qCDebug(dmrLib) << "Exiting PlayerEngine::prev().";
}

void PlayerEngine::next()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::next().";
    if (_playingRequest) {
        qCDebug(dmrLib) << "_playingRequest is true, returning.";
        return;
    }
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playNext(true);
    _playingRequest = false;
    qCDebug(dmrLib) << "Exiting PlayerEngine::next().";
}

void PlayerEngine::onPlaylistAsyncAppendFinished(const QList<PlayItemInfo> &pil)
{
    qCDebug(dmrLib) << "Entering PlayerEngine::onPlaylistAsyncAppendFinished().";
    if (_pendingPlayReq.isValid()) {
        qCDebug(dmrLib) << "_pendingPlayReq is valid.";
        auto id = _playlist->indexOf(_pendingPlayReq);
        if (pil.size() && _pendingPlayReq.scheme() == "playlist") {
            qCDebug(dmrLib) << "Playlist scheme detected, updating ID.";
            id = _playlist->indexOf(pil[0].url);
        }

        if (id >= 0) {
            qCDebug(dmrLib) << "ID is valid, changing current playlist item to ID:" << id;
            _playlist->changeCurrent(id);
            _pendingPlayReq = QUrl();
        } else {
            qCDebug(dmrLib) << "ID is invalid, info logged.";
        }
        // else, wait for another signal
    } else {
        qCDebug(dmrLib) << "_pendingPlayReq is not valid, info logged.";
    }
    qCDebug(dmrLib) << "Exiting PlayerEngine::onPlaylistAsyncAppendFinished().";
}

void PlayerEngine::playByName(const QUrl &url)
{
    qCDebug(dmrLib) << "Entering PlayerEngine::playByName() with URL:" << url;
    savePreviousMovieState();
    int id = _playlist->indexOf(url);
    qCInfo(dmrLib) << __func__ << url << "id:" << id;
    if (id >= 0) {
        qCDebug(dmrLib) << "URL found in playlist at ID:" << id << ", changing current.";
        _playlist->changeCurrent(id);
    } else {
        qCDebug(dmrLib) << "URL not found in playlist, setting as pending play request.";
        _pendingPlayReq = url;
    }
    qCDebug(dmrLib) << "Exiting PlayerEngine::playByName().";
}

void PlayerEngine::playSelected(int id)
{
    qCDebug(dmrLib) << "Entering PlayerEngine::playSelected() with ID:" << id;
    qCInfo(dmrLib) << __func__ << id;
    savePreviousMovieState();
    _playlist->changeCurrent(id);
    qCDebug(dmrLib) << "Exiting PlayerEngine::playSelected().";
}

void PlayerEngine::clearPlaylist()
{
    qCDebug(dmrLib) << "Entering PlayerEngine::clearPlaylist().";
    _playlist->clear();
    MovieConfiguration::get().clear();
    qCDebug(dmrLib) << "Exiting PlayerEngine::clearPlaylist().";
}

void PlayerEngine::pauseResume()
{
    qCDebug(dmrLib) << "Pause/Resume requested";
    if (!_current) {
        qCDebug(dmrLib) << "_current backend is null, cannot pause/resume, returning.";
        qCWarning(dmrLib) << "No backend available";
        return;
    }
    if (_state == CoreState::Idle) {
        qCDebug(dmrLib) << "Player state is Idle, returning.";
        return;
    }

    _current->pauseResume();
    qCDebug(dmrLib) << "Exiting PlayerEngine::pauseResume().";
}

void PlayerEngine::stop()
{
    qCDebug(dmrLib) << "Stop requested";
    if (!_current) {
        qCWarning(dmrLib) << "No backend available";
        return;
    }
    _current->stop();
    qCDebug(dmrLib) << "Exiting PlayerEngine::stop().";
}

bool PlayerEngine::paused()
{
    qCDebug(dmrLib) << "Enter paused function";
    return _state == CoreState::Paused;
}

QImage PlayerEngine::takeScreenshot()
{
    qCDebug(dmrLib) << "Enter takeScreenshot function";
    if (!_current) return QImage();
    return _current->takeScreenshot();
}

void PlayerEngine::burstScreenshot()
{
    qCDebug(dmrLib) << "Enter burstScreenshot function";
    if (!_current) return;
    _current->burstScreenshot();
}

void PlayerEngine::stopBurstScreenshot()
{
    qCDebug(dmrLib) << "Enter stopBurstScreenshot function";
    if (!_current) return;
    _current->stopBurstScreenshot();
}

void PlayerEngine::seekForward(int secs)
{
    qCDebug(dmrLib) << "Seeking forward" << secs << "seconds";
    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "Player is idle, ignoring seek";
        return;
    }

    static int lastElapsed = 0;

    if (elapsed() == lastElapsed) {
        qCDebug(dmrLib) << "Elapsed time unchanged, ignoring seek";
        return;
    }
    if (!_current) return;
    _current->seekForward(secs);
}

void PlayerEngine::seekBackward(int secs)
{
    qCDebug(dmrLib) << "Seeking backward" << secs << "seconds";
    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "Player is idle, ignoring seek";
        return;
    }

    if (!_current) return;
    if (elapsed() - abs(secs) <= 0) {
        qCDebug(dmrLib) << "Seeking to start of file";
        _current->seekBackward(static_cast<int>(elapsed()));
    } else {
        _current->seekBackward(secs);
    }
}


void PlayerEngine::seekAbsolute(int pos)
{
    qCDebug(dmrLib) << "Seeking to absolute position:" << pos;
    if (state() == CoreState::Idle) {
        qCDebug(dmrLib) << "Player is idle, ignoring seek";
        return;
    }

    if (!_current) return;
    _current->seekAbsolute(pos);
}

void PlayerEngine::setDVDDevice(const QString &path)
{
    qCDebug(dmrLib) << "Enter setDVDDevice function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setDVDDevice(path);
    qCDebug(dmrLib) << "Exiting setDVDDevice function";
}

bool PlayerEngine::addPlayFile(const QUrl &url)
{
    qCDebug(dmrLib) << "Adding file to playlist:" << url.toString();
    QUrl realUrl = FileFilter::instance()->fileTransfer(url.toString());
    if (!isPlayableFile(realUrl)) {
        qCWarning(dmrLib) << "File is not playable:" << realUrl.toString();
        return false;
    }

    _playlist->append(realUrl);
    qCDebug(dmrLib) << "File added to playlist successfully";
    return true;
}

QList<QUrl> PlayerEngine::addPlayDir(const QDir &dir)
{
    qCDebug(dmrLib) << "Adding directory to playlist:" << dir.path();
    QList<QUrl> valids = FileFilter::instance()->filterDir(dir);
    qCDebug(dmrLib) << "Found" << valids.size() << "valid files in directory";

    struct {
        bool operator()(const QUrl& fi1, const QUrl& fi2) const {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            static QRegExp rd("\\d+");
            int pos = 0;
            QString fileName1 = QFileInfo(fi1.toLocalFile()).fileName();
            QString fileName2 = QFileInfo(fi2.toLocalFile()).fileName();
            while ((pos = rd.indexIn(fileName1, pos)) != -1) {
                auto inc = rd.matchedLength();
                auto id1 = fileName1.midRef(pos, inc);

                auto pos2 = rd.indexIn(fileName2, pos);
                if (pos == pos2) {
                    auto id2 = fileName2.midRef(pos, rd.matchedLength());
                    //qCInfo(dmrLib) << "id compare " << id1 << id2;
                    if (id1 != id2) {
                        bool ok1, ok2;
                        bool v = id1.toInt(&ok1) < id2.toInt(&ok2);
                        if (ok1 && ok2) return v;
                        return id1.localeAwareCompare(id2) < 0;
                    }
                }

                pos += inc;
            }
            return fileName1.localeAwareCompare(fileName2) < 0;
#else
            static QRegularExpression rd("\\d+");
            QRegularExpressionMatch match;
            int pos = 0;
            QString fileName1 = QFileInfo(fi1.toLocalFile()).fileName();
            QString fileName2 = QFileInfo(fi2.toLocalFile()).fileName();
            while ((match = rd.match(fileName1, pos)).hasMatch()) {
                auto inc = match.capturedLength();
                auto id1 = QStringView(fileName1).mid(match.capturedStart(), inc);

                auto pos2 = rd.match(fileName2, pos).capturedStart();
                if (pos == pos2) {
                    auto id2 = QStringView(fileName2).mid(pos, rd.match(fileName2, pos).capturedLength());
                    //qCInfo(dmrLib) << "id compare " << id1 << id2;
                    if (id1 != id2) {
                        bool ok1, ok2;
                        bool v = id1.toInt(&ok1) < id2.toInt(&ok2);
                        if (ok1 && ok2) return v;
                        return id1.localeAwareCompare(id2) < 0;
                    }
                }

                pos = match.capturedEnd();
            }
            return fileName1.localeAwareCompare(fileName2) < 0;
#endif
        }
    } SortByDigits;

    std::sort(valids.begin(), valids.end(), SortByDigits);
    valids = addPlayFiles(valids);
    _playlist->appendAsync(valids);

    return valids;
}

QList<QUrl> PlayerEngine::addPlayFiles(const QList<QUrl> &urls)
{
    qCDebug(dmrLib) << "Adding" << urls.size() << "files to playlist";
    QList<QUrl> valids;

    for (QUrl url : urls) {
        if (m_stopRunningThread) {
            qCDebug(dmrLib) << "Thread stopped, breaking file addition";
            break;
        }
        if (isPlayableFile(url))
            valids << url;
    }

    qCDebug(dmrLib) << "Found" << valids.size() << "playable files";
    _playlist->appendAsync(valids);

    return valids;
}

QList<QUrl> PlayerEngine::addPlayFiles(const QList<QString> &lstFile)
{
    qCDebug(dmrLib) << "Enter addPlayFiles function";
    QList<QUrl> valids;
    QUrl realUrl;

    for (QString strFile : lstFile) {
          realUrl = FileFilter::instance()->fileTransfer(strFile);
          if (QFileInfo(realUrl.path()).isDir()) {
              if (realUrl.isLocalFile())          // 保证不是网络路径
                  valids << FileFilter::instance()->filterDir(QDir(realUrl.path()));
          } else {
              valids << realUrl;
          }
    }

    return addPlayFiles(valids);
}

void PlayerEngine::addPlayFs(const QList<QString> &lstFile)
{
    qCDebug(dmrLib) << "Enter addPlayFs function";
    QList<QUrl> valids;
    QUrl realUrl;

    for (QString strFile : lstFile) {
          realUrl = FileFilter::instance()->fileTransfer(strFile);
          if (QFileInfo(realUrl.path()).isDir()) {
              if (realUrl.isLocalFile())          // 保证不是网络路径
                  valids << FileFilter::instance()->filterDir(QDir(realUrl.path()));
          } else {
              valids << realUrl;
          }
    }

    if (valids.isEmpty()) {
        blockSignals(false);
        return;
    }
    QList<QUrl> addFiles = addPlayFiles(valids);
    blockSignals(false);
    emit finishedAddFiles(addFiles);
    qCDebug(dmrLib) << "Exiting addPlayFs function";
}

qint64 PlayerEngine::duration() const
{
    qCDebug(dmrLib) << "Enter duration function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0";
        return 0;
    }
    qCDebug(dmrLib) << "Exiting duration function";
    return _current->duration();
}

QSize PlayerEngine::videoSize() const
{
    qCDebug(dmrLib) << "Enter videoSize function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return {0, 0}";
        return {0, 0};
    }
    qCDebug(dmrLib) << "Exiting videoSize function";
    return _current->videoSize();
}

qint64 PlayerEngine::elapsed() const
{
    qCDebug(dmrLib) << "Enter elapsed function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0";
        return 0;
    }
    if (!_playlist) {
        qCDebug(dmrLib) << "playlist is null, return 0";
        return 0;
    }
    if (_playlist->count() == 0 || _playlist->current() < 0) {
        qCDebug(dmrLib) << "playlist count is 0 or current is less than 0, return 0";
        return 0;
    }
    qint64 nDuration = _current->duration();        //因为文件信息的持续时间和MPV返回的持续有些差别，所以，我们使用文件返回的持续时间
    qint64 nElapsed = _current->elapsed();
    if (nElapsed < 0) {
        qCDebug(dmrLib) << "elapsed is negative, return 0";
        return 0;
    }
    if (nElapsed > nDuration) {
        qCDebug(dmrLib) << "elapsed is greater than duration, return duration";
        return nDuration;
    }
    qCDebug(dmrLib) << "Exiting elapsed function";
    return nElapsed;
}

void PlayerEngine::setVideoAspect(double r)
{
    qCDebug(dmrLib) << "Enter setVideoAspect function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setVideoAspect(r);
    qCDebug(dmrLib) << "Exiting setVideoAspect function";
}

double PlayerEngine::videoAspect() const
{
    qCDebug(dmrLib) << "Enter videoAspect function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0.0";
        return 0.0;
    }
    qCDebug(dmrLib) << "Exiting videoAspect function";
    return _current->videoAspect();
}

int PlayerEngine::videoRotation() const
{
    qCDebug(dmrLib) << "Enter videoRotation function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return 0";
        return 0;
    }
    qCDebug(dmrLib) << "Exiting videoRotation function";
    return _current->videoRotation();
}

void PlayerEngine::setVideoRotation(int degree)
{
    qCDebug(dmrLib) << "Enter setVideoRotation function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setVideoRotation(degree);
    qCDebug(dmrLib) << "Exiting setVideoRotation function";
}

void PlayerEngine::changeSoundMode(Backend::SoundMode sm)
{
    qCDebug(dmrLib) << "Enter changeSoundMode function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->changeSoundMode(sm);
    qCDebug(dmrLib) << "Exiting changeSoundMode function";
}

void PlayerEngine::resizeEvent(QResizeEvent *)
{
    qCDebug(dmrLib) << "Enter resizeEvent function";
#if !defined(USE_DXCB) && !defined(_LIBDMR_)
    bool rounded = !window()->isFullScreen() && !window()->isMaximized();
    if (rounded) {
        QPixmap shape(size());
        shape.fill(Qt::transparent);

        QPainter p(&shape);
        QPainterPath pp;
        pp.addRect(rect());
        p.fillPath(pp, QBrush(Qt::white));
        p.end();

        setMask(shape.mask());
    } else {
        clearMask();
    }
#endif
    qCDebug(dmrLib) << "Exiting resizeEvent function";
}

void PlayerEngine::setBackendProperty(const QString &name, const QVariant &val)
{
    qCDebug(dmrLib) << "Enter setBackendProperty function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return";
        return;
    }
    _current->setProperty(name, val);
    qCDebug(dmrLib) << "Exiting setBackendProperty function";
}

QVariant PlayerEngine::getBackendProperty(const QString &name)
{
    qCDebug(dmrLib) << "Enter getBackendProperty function";
    if (!_current) {
        qCDebug(dmrLib) << "current is null, return QVariant()";
        return QVariant();
    }
    qCDebug(dmrLib) << "Exiting getBackendProperty function";
    return _current->getProperty(name);
}

void PlayerEngine::toggleRoundedClip(bool roundClip)
{
    qCDebug(dmrLib) << "Enter toggleRoundedClip function";
    MpvProxy* pMpvProxy = nullptr;

    pMpvProxy = dynamic_cast<MpvProxy *>(_current);
    // if(!pMpvProxy) {
    //     dynamic_cast<QtPlayerProxy *>(_current)->updateRoundClip(roundClip);
    // } else {
        pMpvProxy->updateRoundClip(roundClip);
    // }
    qCDebug(dmrLib) << "Exiting toggleRoundedClip function";
}

bool PlayerEngine::currFileIsAudio()
{
    qCDebug(dmrLib) << "Enter currFileIsAudio function";
    bool bAudio = false;
    PlayItemInfo pif;

    if (_playlist->count() > 0) {
        qCDebug(dmrLib) << "playlist count > 0";
        pif = _playlist->currentInfo();
    }

    if (CompositingManager::isMpvExists()) {
        qCDebug(dmrLib) << "CompositingManager::isMpvExists()";
        if(pif.mi.vCodecID == AV_CODEC_ID_AVS2 || pif.mi.vCodecID == AV_CODEC_ID_AVS3) {
            qCDebug(dmrLib) << "AV_CODEC_ID_AVS2 || AV_CODEC_ID_AVS3";
            bAudio = false;
        } else {
            qCDebug(dmrLib) << "AV_CODEC_ID_AVS2 || AV_CODEC_ID_AVS3 else";
            bAudio = pif.url.isLocalFile() && (pif.mi.width <= 0 && pif.mi.height <= 0);
        }
    } else {
        qCDebug(dmrLib) << "CompositingManager::isMpvExists() else";
        bAudio = isAudioFile(pif.url.toString());
    }

    qCDebug(dmrLib) << "Exiting currFileIsAudio function";
    return bAudio;
}

} // end of namespace dmr
