// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
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
    qDebug() << "Initializing PlayerEngine";
    m_bAudio = false;
    m_stopRunningThread = false;
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);

    if(CompositingManager::isMpvExists()){
        qDebug() << "MPV backend exists, creating MpvProxy";
        _current = new MpvProxy(this);
    }
    // else {
    //     _current = new QtPlayerProxy(this);
    // }
    if (_current) {
        qDebug() << "Setting up backend connections";
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
        qWarning() << "Failed to create backend";
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
    qDebug() << "PlayerEngine initialization completed";
}

PlayerEngine::~PlayerEngine()
{
    qDebug() << "Destroying PlayerEngine";
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
    qDebug() << "PlayerEngine destroyed";
}

bool PlayerEngine::isPlayableFile(const QUrl &url)
{
    qDebug() << "Checking if file is playable:" << url.toString();
    if (FileFilter::instance()->isMediaFile(url)) {
        qDebug() << "File is playable";
        return true;
    } else {    // 网络文件不提示
        if(url.isLocalFile()) {
            qWarning() << "Invalid file:" << QFileInfo(url.toLocalFile()).fileName();
            emit sigInvalidFile(QFileInfo(url.toLocalFile()).fileName());
        }
        return false;
    }
}

static QStringList suffixes;

bool PlayerEngine::isPlayableFile(const QString &name)
{
    qDebug() << "Enter isPlayableFile function";
    QUrl url = FileFilter::instance()->fileTransfer(name);

    if (FileFilter::instance()->isMediaFile(url))
    {
        qDebug() << "File is playable";
        return true;
    }

    if (url.isLocalFile()) {   // 网络文件不提示
        qDebug() << "File is not playable";
        emit sigInvalidFile(QFileInfo(url.toLocalFile()).fileName());
        return false;
    }
    qDebug() << "File is not playable";
    return  false;
}

bool PlayerEngine::isAudioFile(const QString &name)
{
    qDebug() << "Enter isAudioFile function";
    QUrl url = FileFilter::instance()->fileTransfer(name);
    qDebug() << "File is audio";
    return FileFilter::instance()->isAudio(url);
}

bool PlayerEngine::isSubtitle(const QString &name)
{
    qDebug() << "Enter isSubtitle function";
    QUrl url = FileFilter::instance()->fileTransfer(name);
    qDebug() << "File is subtitle";
    return FileFilter::instance()->isSubtitle(url);
}

void PlayerEngine::updateSubStyles()
{
    qDebug() << "Updating subtitle styles";
#ifndef _LIBDMR_
    QPointer<DSettingsOption> pFontOpt = Settings::get().settings()->option("subtitle.font.family");
    QPointer<DSettingsOption> pSizeOpt = Settings::get().settings()->option("subtitle.font.size");
    if(!pFontOpt || !pSizeOpt) {
        qWarning() << "Failed to get subtitle style settings";
        return;
    }

    int fontId = pFontOpt->value().toInt();
    int size = pSizeOpt->value().toInt();
    QString font = pFontOpt->data("items").toStringList()[fontId];
    qDebug() << "Subtitle style - Font:" << font << "Size:" << size;

    if (_state != CoreState::Idle) {
        if (_playlist->current() < 0) {
            qDebug() << "No current playlist item";
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
        qDebug() << "Updating subtitle style with scaled size:" << size;
        updateSubStyle(font, size);
    }
#endif
}

void PlayerEngine::waitLastEnd()
{
    qDebug() << "Enter waitLastEnd function";
    if (MpvProxy *mpv = dynamic_cast<MpvProxy *>(_current)) {
        mpv->pollingEndOfPlayback();
    }
    // TODO
    // else if (QtPlayerProxy *qtPlayer = dynamic_cast<QtPlayerProxy *>(_current)) {
    //     qtPlayer->pollingEndOfPlayback();
    // }
    qDebug() << "Exiting waitLastEnd function";
}

void PlayerEngine::onBackendStateChanged()
{
    if (!_current) {
        qWarning() << "Backend state changed but no backend exists";
        return;
    }

    CoreState old = _state;
    switch (_current->state()) {
    case Backend::PlayState::Playing:
        _state = CoreState::Playing;
        if (_playlist->count() > 0) {
            m_bAudio = currFileIsAudio();
            qDebug() << "State changed to Playing, is audio:" << m_bAudio;
        }
        //playing . emit thumbnail progress mode signal with setting file
        if (old == CoreState::Idle) {
            qDebug() << "Initializing thumbnail settings";
            emit siginitthumbnailseting();
        }
        break;
    case Backend::PlayState::Paused:
        _state = CoreState::Paused;
        qDebug() << "State changed to Paused";
        break;
    case Backend::PlayState::Stopped:
        _state = CoreState::Idle;
        qDebug() << "State changed to Idle";
        break;
    }

    updateSubStyles();
    if (old != _state) {
        qDebug() << "State changed from" << old << "to" << _state;
        emit stateChanged();
    }

    auto systemEnv = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));
    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
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
    }
    qDebug() << "Exiting onBackendStateChanged function";
}

PlayerEngine::CoreState PlayerEngine::state()
{
    qDebug() << "Enter state function";
    auto old = _state;
    switch (_current->state()) {
    case Backend::PlayState::Playing:
        qDebug() << "State changed to Playing";
        _state = CoreState::Playing;
        break;
    case Backend::PlayState::Paused:
        qDebug() << "State changed to Paused";
        _state = CoreState::Paused;
        break;
    case Backend::PlayState::Stopped:
        qDebug() << "State changed to Idle";
        _state = CoreState::Idle;
        break;
    }

    if (old != _state) {
        qWarning() << "###### state mismatch" << old << _state;
        emit stateChanged();
    }
    qDebug() << "Exiting state function";
    return _state;
}

const PlayingMovieInfo &PlayerEngine::playingMovieInfo()
{
    qDebug() << "Enter playingMovieInfo function";
    static PlayingMovieInfo empty;

    if (!_current) {
        qDebug() << "current is null, return empty";
        return empty;
    }
    qDebug() << "Exiting playingMovieInfo function";
    return _current->playingMovieInfo();
}

int PlayerEngine::aid()
{
    qDebug() << "Enter aid function";
    if (state() == CoreState::Idle) {
        return 0;
    }
    if (!_current) {
        qDebug() << "current is null, return 0";
        return 0;
    }

    return _current->aid();
}

int PlayerEngine::sid()
{
    qDebug() << "Enter sid function";
    if (state() == CoreState::Idle) {
        return 0;
    }
    if (!_current) {
        qDebug() << "current is null, return 0";
        return 0;
    }

    return _current->sid();
}

void PlayerEngine::onSubtitlesDownloaded(const QUrl &url, const QList<QString> &filenames,
                                         OnlineSubtitle::FailReason reason)
{
    qDebug() << "Enter onSubtitlesDownloaded function";
    //mod for warning by xxj ,no any means
    reason = OnlineSubtitle::FailReason::NoError;

    if (state() == CoreState::Idle) {
        qDebug() << "state is Idle, return";
        return;
    }
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }

    if (playlist().currentInfo().url != url) {
        qDebug() << "playlist().currentInfo().url != url, return";
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
    qDebug() << "Exiting onSubtitlesDownloaded function";
}

bool PlayerEngine::loadSubtitle(const QFileInfo &fi)
{
    qDebug() << "Loading subtitle:" << fi.absoluteFilePath();
    if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, ignoring subtitle load";
        return true;
    }
    if (!_current) {
        qWarning() << "No backend available";
        return true;
    }

    const auto &pmf = _current->playingMovieInfo();
    auto pif = playlist().currentInfo();
    int i = 0;
    for (const auto &sub : pmf.subs) {
        if (sub["external"].toBool()) {
            auto path = sub["external-filename"].toString();
            if (path == fi.canonicalFilePath()) {
                qDebug() << "Subtitle already loaded, selecting it";
                this->selectSubtitle(i);
                return true;
            }
        }
        ++i;
    }

    if (_current->loadSubtitle(fi)) {
        qDebug() << "Subtitle loaded successfully";
#ifndef _LIBDMR_
        MovieConfiguration::get().append2ListUrl(pif.url, ConfigKnownKey::ExternalSubs,
                                                 fi.canonicalFilePath());
#endif
        return true;
    }
    qWarning() << "Failed to load subtitle";
    return false;
}

void PlayerEngine::loadOnlineSubtitle(const QUrl &url)
{
    qDebug() << "Requesting online subtitle for:" << url.toString();
    if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, ignoring subtitle request";
        return;
    }
    if (!_current) {
        qWarning() << "No backend available";
        return;
    }

    OnlineSubtitle::get().requestSubtitle(url);
}

void PlayerEngine::setPlaySpeed(double times)
{
    qDebug() << "Enter setPlaySpeed function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setPlaySpeed(times);
}

void PlayerEngine::setSubDelay(double secs)
{
    qDebug() << "Enter setSubDelay function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }

    _current->setSubDelay(secs + _current->subDelay());
}

double PlayerEngine::subDelay() const
{
    qDebug() << "Enter subDelay function";
    if (!_current) {
        qDebug() << "current is null, return 0.0";
        return 0.0;
    }
    return _current->subDelay();
}

QString PlayerEngine::subCodepage()
{
    qDebug() << "Enter subCodepage function";
    if (_current->subCodepage().isEmpty()) {
        qDebug() << "subCodepage is empty, return auto";
        return "auto";
    } else {
        qDebug() << "subCodepage is not empty, return" << _current->subCodepage();
        return _current->subCodepage();
    }

}

void PlayerEngine::setSubCodepage(const QString &cp)
{
    qDebug() << "Enter setSubCodepage function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setSubCodepage(cp);
    qDebug() << "Exiting setSubCodepage function";
//    emit subCodepageChanged();
}

void PlayerEngine::addSubSearchPath(const QString &path)
{
    qDebug() << "Enter addSubSearchPath function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->addSubSearchPath(path);
    qDebug() << "Exiting addSubSearchPath function";
}

void PlayerEngine::updateSubStyle(const QString &font, int sz)
{
    qDebug() << "Enter updateSubStyle function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->updateSubStyle(font, sz / 2);
    qDebug() << "Exiting updateSubStyle function";
}

void PlayerEngine::selectSubtitle(int id)
{
    qDebug() << "Enter selectSubtitle function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    if (state() != CoreState::Idle) {
        qDebug() << "state is not Idle, return";
        const auto &pmf = _current->playingMovieInfo();
        if (id >= pmf.subs.size()) return;
        auto sid = pmf.subs[id]["id"].toInt();
        _current->selectSubtitle(sid);
    }
    qDebug() << "Exiting selectSubtitle function";
}

bool PlayerEngine::isSubVisible()
{
    qDebug() << "Enter isSubVisible function";
    if (state() == CoreState::Idle) {
        qDebug() << "state is Idle, return false";
        return false;
    }
    if (!_current) {
        qDebug() << "current is null, return false";
        return false;
    }
    qDebug() << "Exiting isSubVisible function";
    return _current->isSubVisible();
}

void PlayerEngine::toggleSubtitle()
{
    qDebug() << "Enter toggleSubtitle function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->toggleSubtitle();
    qDebug() << "Exiting toggleSubtitle function";
}

void PlayerEngine::selectTrack(int id)
{
    qDebug() << "Enter selectTrack function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->selectTrack(id);
    qDebug() << "Exiting selectTrack function";
}

void PlayerEngine::volumeUp()
{
    qDebug() << "Enter volumeUp function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->volumeUp();
    qDebug() << "Exiting volumeUp function";
}

void PlayerEngine::changeVolume(int val)
{
    qDebug() << "Enter changeVolume function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->changeVolume(val);
    qDebug() << "Exiting changeVolume function";
}

void PlayerEngine::volumeDown()
{
    qDebug() << "Enter volumeDown function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->volumeDown();
    qDebug() << "Exiting volumeDown function";
}

int PlayerEngine::volume() const
{
    qDebug() << "Enter volume function";
    if (!_current) {
        qDebug() << "current is null, return 100";
        return 100;
    }
    qDebug() << "Exiting volume function";
    return _current->volume();
}

bool PlayerEngine::muted() const
{
    qDebug() << "Enter muted function";
    if (!_current) {
        qDebug() << "current is null, return false";
        return false;
    }
    qDebug() << "Exiting muted function";
    return _current->muted();
}

void PlayerEngine::changehwaccelMode(Backend::hwaccelMode hwaccelMode)
{
    qDebug() << "Enter changehwaccelMode function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    qDebug() << "Exiting changehwaccelMode function";
    return _current->changehwaccelMode(hwaccelMode);
}

Backend *PlayerEngine::getMpvProxy()
{
    qDebug() << "Enter getMpvProxy function";
    return _current;
}

void PlayerEngine::toggleMute()
{
    qDebug() << "Entering PlayerEngine::toggleMute().";
    if (!_current) {
        qDebug() << "_current backend is null, returning.";
        return;
    }
    //发送信号通知初始化库函数
    if (!m_bMpvFunsLoad) {
        qDebug() << "mpv functions not loaded, emitting mpvFunsLoadOver().";
        emit mpvFunsLoadOver();
        m_bMpvFunsLoad = true;
    }

    _current->toggleMute();
    emit volumeChanged();
    qDebug() << "Exiting PlayerEngine::toggleMute().";
}

void PlayerEngine::setMute(bool bMute)
{
    qDebug() << "Entering PlayerEngine::setMute() with bMute:" << bMute;
    _current->setMute(bMute);
    qDebug() << "Exiting PlayerEngine::setMute().";
}

void PlayerEngine::savePreviousMovieState()
{
    qDebug() << "Entering PlayerEngine::savePreviousMovieState().";
    savePlaybackPosition();
    qDebug() << "Exiting PlayerEngine::savePreviousMovieState().";
}

void PlayerEngine::paintEvent(QPaintEvent *e)
{
    qDebug() << "Entering PlayerEngine::paintEvent().";
    QRect rect = this->rect();
    QPainter p(this);

    if (!CompositingManager::get().composited() || utils::check_wayland_env()) {  // wayland下不会进入mainwindow的paintevent函数导致图标未绘制
        qDebug() << "Compositing manager not composited or wayland environment detected.";
        if (_state != Idle && m_bAudio) {
            qDebug() << "Player is not idle and is audio, filling black rectangle.";
            p.fillRect(rect, QBrush(QColor(0, 0, 0)));
        } else {
            qDebug() << "Player is idle or not audio, drawing icon.";
            QImage icon = QIcon::fromTheme("deepin-movie").pixmap(130, 130).toImage();;
            QPixmap pix = QPixmap::fromImage(icon);
            QPointF pos = rect.center() - QPoint(pix.width() / 2, pix.height() / 2) / devicePixelRatioF();

            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                qDebug() << "Theme type is Light, filling white rectangle and drawing pixmap.";
                p.fillRect(rect, QBrush(QColor(255, 255, 255)));
                p.drawPixmap(pos, pix);
            } else {
                qDebug() << "Theme type is not Light, filling black rectangle and drawing pixmap.";
                p.fillRect(rect, QBrush(QColor(0, 0, 0)));
                p.drawPixmap(pos, pix);
            }
        }
    } else {
        qDebug() << "Compositing manager is composited and not wayland environment, skipping custom paint.";
    }
    qDebug() << "Exiting PlayerEngine::paintEvent().";
    return QWidget::paintEvent(e);
}

void PlayerEngine::requestPlay(int id)
{
    qDebug() << "Requesting play for item" << id;
    if (!_current) {
        qWarning() << "No backend available";
        return;
    }
    if (id >= _playlist->count()) {
        qWarning() << "Invalid playlist index:" << id;
        return;
    }

    const auto &item = _playlist->items()[id];
    qDebug() << "Setting play file:" << item.url.toString();
    _current->setPlayFile(item.url);

    DRecentData data;
    data.appName = "Deepin Movie";
    data.appExec = "deepin-movie";
    DRecentManager::addItem(item.url.toLocalFile(), data);

    if (_current->isPlayable()) {
        qDebug() << "File is playable, starting playback";
#ifdef __sw_64__
        // 1.1.0以上版本的dav1d在多线程环境下会卡死，更换解码器使用
        if(!FileFilter::instance()->isFormatSupported(item.url)) {
            qDebug() << "Using libaom-av1 decoder for unsupported format";
            _current->setProperty("vd", "libaom-av1");
        }
#endif
        _current->play();
    } else {
        // TODO: delete and try next backend?
        qDebug() << "File is not playable, considering next backend or handling error.";
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

    qDebug() << "Logging playback event:" << obj;
    EventLogUtils::get().writeLogs(obj);
    qDebug() << "Exiting PlayerEngine::requestPlay().";
}

void PlayerEngine::savePlaybackPosition()
{
    qDebug() << "Entering PlayerEngine::savePlaybackPosition().";
    if (!_current) {
        qDebug() << "_current backend is null, returning.";
        return;
    }
    _current->savePlaybackPosition();
    qDebug() << "Exiting PlayerEngine::savePlaybackPosition().";
}

void PlayerEngine::nextFrame()
{
    qDebug() << "Entering PlayerEngine::nextFrame().";
    if (!_current) {
        qDebug() << "_current backend is null, returning.";
        return;
    }
    _current->nextFrame();
    qDebug() << "Exiting PlayerEngine::nextFrame().";
}

void PlayerEngine::previousFrame()
{
    qDebug() << "Entering PlayerEngine::previousFrame().";
    if (!_current) {
        qDebug() << "_current backend is null, returning.";
        return;
    }
    _current->previousFrame();
    qDebug() << "Exiting PlayerEngine::previousFrame().";
}

void PlayerEngine::makeCurrent()
{
    qDebug() << "Entering PlayerEngine::makeCurrent().";
    if (!_current) {
        qDebug() << "_current backend is null, returning.";
        return;
    }
    _current->makeCurrent();
    qDebug() << "Exiting PlayerEngine::makeCurrent().";
}

void PlayerEngine::play()
{
    qDebug() << "Play requested";
    if (!_current || !_playlist->count()) {
        qWarning() << "Cannot play: no backend or empty playlist";
        return;
    }

    if (state() == CoreState::Paused &&
            getBackendProperty("keep-open").toBool() &&
            getBackendProperty("eof-reached").toBool()) {
        qDebug() << "End of file reached, stopping and playing next";
        stop();
        next();
    } else if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, playing next item";
        next();
    }
    qDebug() << "Exiting PlayerEngine::play().";
}

void PlayerEngine::prev()
{
    qDebug() << "Entering PlayerEngine::prev().";
    if (_playingRequest) {
        qDebug() << "_playingRequest is true, returning.";
        return;
    }
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playPrev(true);
    _playingRequest = false;
    qDebug() << "Exiting PlayerEngine::prev().";
}

void PlayerEngine::next()
{
    qDebug() << "Entering PlayerEngine::next().";
    if (_playingRequest) {
        qDebug() << "_playingRequest is true, returning.";
        return;
    }
    _playingRequest = true;
    savePreviousMovieState();
    _playlist->playNext(true);
    _playingRequest = false;
    qDebug() << "Exiting PlayerEngine::next().";
}

void PlayerEngine::onPlaylistAsyncAppendFinished(const QList<PlayItemInfo> &pil)
{
    qDebug() << "Entering PlayerEngine::onPlaylistAsyncAppendFinished().";
    if (_pendingPlayReq.isValid()) {
        qDebug() << "_pendingPlayReq is valid.";
        auto id = _playlist->indexOf(_pendingPlayReq);
        if (pil.size() && _pendingPlayReq.scheme() == "playlist") {
            qDebug() << "Playlist scheme detected, updating ID.";
            id = _playlist->indexOf(pil[0].url);
        }

        if (id >= 0) {
            qDebug() << "ID is valid, changing current playlist item to ID:" << id;
            _playlist->changeCurrent(id);
            _pendingPlayReq = QUrl();
        } else {
            qDebug() << "ID is invalid, info logged.";
        }
        // else, wait for another signal
    } else {
        qDebug() << "_pendingPlayReq is not valid, info logged.";
    }
    qDebug() << "Exiting PlayerEngine::onPlaylistAsyncAppendFinished().";
}

void PlayerEngine::playByName(const QUrl &url)
{
    qDebug() << "Entering PlayerEngine::playByName() with URL:" << url;
    savePreviousMovieState();
    int id = _playlist->indexOf(url);
    qInfo() << __func__ << url << "id:" << id;
    if (id >= 0) {
        qDebug() << "URL found in playlist at ID:" << id << ", changing current.";
        _playlist->changeCurrent(id);
    } else {
        qDebug() << "URL not found in playlist, setting as pending play request.";
        _pendingPlayReq = url;
    }
    qDebug() << "Exiting PlayerEngine::playByName().";
}

void PlayerEngine::playSelected(int id)
{
    qDebug() << "Entering PlayerEngine::playSelected() with ID:" << id;
    qInfo() << __func__ << id;
    savePreviousMovieState();
    _playlist->changeCurrent(id);
    qDebug() << "Exiting PlayerEngine::playSelected().";
}

void PlayerEngine::clearPlaylist()
{
    qDebug() << "Entering PlayerEngine::clearPlaylist().";
    _playlist->clear();
    MovieConfiguration::get().clear();
    qDebug() << "Exiting PlayerEngine::clearPlaylist().";
}

void PlayerEngine::pauseResume()
{
    qDebug() << "Pause/Resume requested";
    if (!_current) {
        qDebug() << "_current backend is null, cannot pause/resume, returning.";
        qWarning() << "No backend available";
        return;
    }
    if (_state == CoreState::Idle) {
        qDebug() << "Player state is Idle, returning.";
        return;
    }

    _current->pauseResume();
    qDebug() << "Exiting PlayerEngine::pauseResume().";
}

void PlayerEngine::stop()
{
    qDebug() << "Stop requested";
    if (!_current) {
        qWarning() << "No backend available";
        return;
    }
    _current->stop();
    qDebug() << "Exiting PlayerEngine::stop().";
}

bool PlayerEngine::paused()
{
    qDebug() << "Enter paused function";
    return _state == CoreState::Paused;
}

QImage PlayerEngine::takeScreenshot()
{
    qDebug() << "Enter takeScreenshot function";
    return _current->takeScreenshot();
}

void PlayerEngine::burstScreenshot()
{
    qDebug() << "Enter burstScreenshot function";
    _current->burstScreenshot();
}

void PlayerEngine::stopBurstScreenshot()
{
    qDebug() << "Enter stopBurstScreenshot function";
    _current->stopBurstScreenshot();
}

void PlayerEngine::seekForward(int secs)
{
    qDebug() << "Seeking forward" << secs << "seconds";
    if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, ignoring seek";
        return;
    }

    static int lastElapsed = 0;

    if (elapsed() == lastElapsed) {
        qDebug() << "Elapsed time unchanged, ignoring seek";
        return;
    }
    _current->seekForward(secs);
}

void PlayerEngine::seekBackward(int secs)
{
    qDebug() << "Seeking backward" << secs << "seconds";
    if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, ignoring seek";
        return;
    }

    if (elapsed() - abs(secs) <= 0) {
        qDebug() << "Seeking to start of file";
        _current->seekBackward(static_cast<int>(elapsed()));
    } else {
        _current->seekBackward(secs);
    }
}


void PlayerEngine::seekAbsolute(int pos)
{
    qDebug() << "Seeking to absolute position:" << pos;
    if (state() == CoreState::Idle) {
        qDebug() << "Player is idle, ignoring seek";
        return;
    }

    _current->seekAbsolute(pos);
}

void PlayerEngine::setDVDDevice(const QString &path)
{
    qDebug() << "Enter setDVDDevice function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setDVDDevice(path);
    qDebug() << "Exiting setDVDDevice function";
}

bool PlayerEngine::addPlayFile(const QUrl &url)
{
    qDebug() << "Adding file to playlist:" << url.toString();
    QUrl realUrl = FileFilter::instance()->fileTransfer(url.toString());
    if (!isPlayableFile(realUrl)) {
        qWarning() << "File is not playable:" << realUrl.toString();
        return false;
    }

    _playlist->append(realUrl);
    qDebug() << "File added to playlist successfully";
    return true;
}

QList<QUrl> PlayerEngine::addPlayDir(const QDir &dir)
{
    qDebug() << "Adding directory to playlist:" << dir.path();
    QList<QUrl> valids = FileFilter::instance()->filterDir(dir);
    qDebug() << "Found" << valids.size() << "valid files in directory";

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
                    //qInfo() << "id compare " << id1 << id2;
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
                    //qInfo() << "id compare " << id1 << id2;
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
    qDebug() << "Adding" << urls.size() << "files to playlist";
    QList<QUrl> valids;

    for (QUrl url : urls) {
        if (m_stopRunningThread) {
            qDebug() << "Thread stopped, breaking file addition";
            break;
        }
        if (isPlayableFile(url))
            valids << url;
    }

    qDebug() << "Found" << valids.size() << "playable files";
    _playlist->appendAsync(valids);

    return valids;
}

QList<QUrl> PlayerEngine::addPlayFiles(const QList<QString> &lstFile)
{
    qDebug() << "Enter addPlayFiles function";
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
    qDebug() << "Enter addPlayFs function";
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
    qDebug() << "Exiting addPlayFs function";
}

qint64 PlayerEngine::duration() const
{
    qDebug() << "Enter duration function";
    if (!_current) {
        qDebug() << "current is null, return 0";
        return 0;
    }
    qDebug() << "Exiting duration function";
    return _current->duration();
}

QSize PlayerEngine::videoSize() const
{
    qDebug() << "Enter videoSize function";
    if (!_current) {
        qDebug() << "current is null, return {0, 0}";
        return {0, 0};
    }
    qDebug() << "Exiting videoSize function";
    return _current->videoSize();
}

qint64 PlayerEngine::elapsed() const
{
    qDebug() << "Enter elapsed function";
    if (!_current) {
        qDebug() << "current is null, return 0";
        return 0;
    }
    if (!_playlist) {
        qDebug() << "playlist is null, return 0";
        return 0;
    }
    if (_playlist->count() == 0 || _playlist->current() < 0) {
        qDebug() << "playlist count is 0 or current is less than 0, return 0";
        return 0;
    }
    qint64 nDuration = _current->duration();        //因为文件信息的持续时间和MPV返回的持续有些差别，所以，我们使用文件返回的持续时间
    qint64 nElapsed = _current->elapsed();
    if (nElapsed < 0) {
        qDebug() << "elapsed is negative, return 0";
        return 0;
    }
    if (nElapsed > nDuration) {
        qDebug() << "elapsed is greater than duration, return duration";
        return nDuration;
    }
    qDebug() << "Exiting elapsed function";
    return nElapsed;
}

void PlayerEngine::setVideoAspect(double r)
{
    qDebug() << "Enter setVideoAspect function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setVideoAspect(r);
    qDebug() << "Exiting setVideoAspect function";
}

double PlayerEngine::videoAspect() const
{
    qDebug() << "Enter videoAspect function";
    if (!_current) {
        qDebug() << "current is null, return 0.0";
        return 0.0;
    }
    qDebug() << "Exiting videoAspect function";
    return _current->videoAspect();
}

int PlayerEngine::videoRotation() const
{
    qDebug() << "Enter videoRotation function";
    if (!_current) {
        qDebug() << "current is null, return 0";
        return 0;
    }
    qDebug() << "Exiting videoRotation function";
    return _current->videoRotation();
}

void PlayerEngine::setVideoRotation(int degree)
{
    qDebug() << "Enter setVideoRotation function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setVideoRotation(degree);
    qDebug() << "Exiting setVideoRotation function";
}

void PlayerEngine::changeSoundMode(Backend::SoundMode sm)
{
    qDebug() << "Enter changeSoundMode function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->changeSoundMode(sm);
    qDebug() << "Exiting changeSoundMode function";
}

void PlayerEngine::resizeEvent(QResizeEvent *)
{
    qDebug() << "Enter resizeEvent function";
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
    qDebug() << "Exiting resizeEvent function";
}

void PlayerEngine::setBackendProperty(const QString &name, const QVariant &val)
{
    qDebug() << "Enter setBackendProperty function";
    if (!_current) {
        qDebug() << "current is null, return";
        return;
    }
    _current->setProperty(name, val);
    qDebug() << "Exiting setBackendProperty function";
}

QVariant PlayerEngine::getBackendProperty(const QString &name)
{
    qDebug() << "Enter getBackendProperty function";
    if (!_current) {
        qDebug() << "current is null, return QVariant()";
        return QVariant();
    }
    qDebug() << "Exiting getBackendProperty function";
    return _current->getProperty(name);
}

void PlayerEngine::toggleRoundedClip(bool roundClip)
{
    qDebug() << "Enter toggleRoundedClip function";
    MpvProxy* pMpvProxy = nullptr;

    pMpvProxy = dynamic_cast<MpvProxy *>(_current);
    // if(!pMpvProxy) {
    //     dynamic_cast<QtPlayerProxy *>(_current)->updateRoundClip(roundClip);
    // } else {
        pMpvProxy->updateRoundClip(roundClip);
    // }
    qDebug() << "Exiting toggleRoundedClip function";
}

bool PlayerEngine::currFileIsAudio()
{
    qDebug() << "Enter currFileIsAudio function";
    bool bAudio = false;
    PlayItemInfo pif;

    if (_playlist->count() > 0) {
        qDebug() << "playlist count > 0";
        pif = _playlist->currentInfo();
    }

    if (CompositingManager::isMpvExists()) {
        qDebug() << "CompositingManager::isMpvExists()";
        if(pif.mi.vCodecID == AV_CODEC_ID_AVS2 || pif.mi.vCodecID == AV_CODEC_ID_AVS3) {
            qDebug() << "AV_CODEC_ID_AVS2 || AV_CODEC_ID_AVS3";
            bAudio = false;
        } else {
            qDebug() << "AV_CODEC_ID_AVS2 || AV_CODEC_ID_AVS3 else";
            bAudio = pif.url.isLocalFile() && (pif.mi.width <= 0 && pif.mi.height <= 0);
        }
    } else {
        qDebug() << "CompositingManager::isMpvExists() else";
        bAudio = isAudioFile(pif.url.toString());
    }

    qDebug() << "Exiting currFileIsAudio function";
    return bAudio;
}

} // end of namespace dmr
