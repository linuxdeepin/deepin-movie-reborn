/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: zhuyuliang <zhuyuliang@uniontech.com>
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

#include "qtplayer_proxy.h"
#include "mpv_glwidget.h"
#include "compositing_manager.h"
#include "player_engine.h"

#ifndef _LIBDMR_
#include "dmr_settings.h"
#include "movie_configuration.h"
#endif

#include <random>
#include <QtWidgets>
#include <QtGlobal>
#include <QVBoxLayout>

#include <QEventLoop>

namespace dmr {

enum AsyncReplyTag {
    SEEK,
    CHANNEL,
    SPEED
};

QtPlayerProxy::QtPlayerProxy(QWidget *parent)
    :Backend (parent)
{
    m_pParentWidget = parent;

    m_pPlayer = new QMediaPlayer(this);
    m_pVideoSurface = new VideoSurface;
    m_pPlayer->setVideoOutput(m_pVideoSurface);

    m_pGLWidget = new QtPlayerGLWidget(this);
    QVBoxLayout* pLayout = new QVBoxLayout;
    setLayout(pLayout);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(m_pGLWidget);;
    m_pGLWidget->show();
    m_pGLWidget->update();

    connect(m_pPlayer,&QMediaPlayer::stateChanged,this,&QtPlayerProxy::slotStateChanged);
    connect(m_pPlayer,&QMediaPlayer::mediaStatusChanged,this,&QtPlayerProxy::slotMediaStatusChanged);
    connect(m_pPlayer,&QMediaPlayer::positionChanged,this,&QtPlayerProxy::slotPositionChanged);
    connect(m_pPlayer,SIGNAL(error(QMediaPlayer::Error)),this,SLOT(slotMediaError(QMediaPlayer::Error)));
    connect(m_pVideoSurface, &VideoSurface::frameAvailable, this, &QtPlayerProxy::processFrame);
#ifdef __x86_64__
            connect(this, &QtPlayerProxy::elapsedChanged, [ this ]() {//更新opengl显示进度
                m_pGLWidget->updateMovieProgress(duration(), elapsed());
                m_pGLWidget->update();
            });
#endif
}


QtPlayerProxy::~QtPlayerProxy()
{
    //disconnect(window()->windowHandle(), &QWindow::windowStateChanged, nullptr, nullptr);
    if (CompositingManager::get().composited()) {
        disconnect(this, &QtPlayerProxy::stateChanged, nullptr, nullptr);
    }
}


void QtPlayerProxy::firstInit()
{

}

void QtPlayerProxy::updateRoundClip(bool roundClip)
{
    Q_UNUSED(roundClip);
}

void QtPlayerProxy::setState(PlayState state)
{
    bool bRawFormat = false;

    if (0 < dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->size()) {
        PlayItemInfo currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
        bRawFormat = currentInfo.mi.isRawFormat();
    }

    if (_state != state) {
        _state = state;
        if (m_pGLWidget) {
            m_pGLWidget->setPlaying(state != PlayState::Stopped);
            m_pGLWidget->update();
        }
        emit stateChanged();
    }

    if (m_pGLWidget) {
        m_pGLWidget->setRawFormatFlag(bRawFormat);
    }
}

void QtPlayerProxy::pollingEndOfPlayback()
{
    if (_state != Backend::Stopped) {
        stop();
        setState(Backend::Stopped);
        return;
    }
}

const PlayingMovieInfo &QtPlayerProxy::playingMovieInfo()
{
    return m_movieInfo;
}

void QtPlayerProxy::slotStateChanged(QMediaPlayer::State newState)
{
    switch (newState) {
    case QMediaPlayer::StoppedState:
        setState(PlayState::Stopped);
        break;
     case QMediaPlayer::PlayingState:
        setState(PlayState::Playing);
        break;
    case QMediaPlayer::PausedState:
       setState(PlayState::Paused);
       break;
    }
}

void QtPlayerProxy::slotMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::BufferedMedia:
        setState(PlayState::Playing);
         emit fileLoaded();
        break;
    default:
        break;
    }
}

void QtPlayerProxy::slotPositionChanged(qint64 position)
{
    Q_UNUSED(position);
    emit elapsedChanged();
}

void QtPlayerProxy::slotMediaError(QMediaPlayer::Error error)
{
    switch (error) {
    case QMediaPlayer::ResourceError:
    case QMediaPlayer::FormatError:
    case QMediaPlayer::NetworkError:
    case QMediaPlayer::AccessDeniedError:
    case QMediaPlayer::ServiceMissingError:
        emit sigMediaError();
        break;
    default:
        break;
    }
}

void QtPlayerProxy::processFrame(QVideoFrame &frame)
{
    frame.map(QAbstractVideoBuffer::ReadOnly);
    QImage recvImage(frame.bits(), frame.width(), frame.height(), QVideoFrame::imageFormatFromPixelFormat(frame.pixelFormat()));
    m_currentImage = recvImage;
    QImage tempImage = recvImage.rgbSwapped();
    //m_pGLWidget->setImageData(tempImage.bits(), tempImage.width(), tempImage.height());
    m_pGLWidget->setVideoTex(recvImage);
    m_pGLWidget->repaint();
    frame.unmap();
}

void QtPlayerProxy::showEvent(QShowEvent *pEvent)
{
    if (!m_bConnectStateChange) {
        m_bConnectStateChange = true;
    }
    Backend::showEvent(pEvent);
}

void QtPlayerProxy::resizeEvent(QResizeEvent *pEvent)
{
    if (state() == PlayState::Stopped) {
        return;
    }
    Backend::resizeEvent(pEvent);
}

void QtPlayerProxy::savePlaybackPosition()
{
    if (state() == PlayState::Stopped) {
        return;
    }
}

void QtPlayerProxy::setPlaySpeed(double dTimes)
{
    m_pPlayer->setPlaybackRate(dTimes);

    m_pPlayer->setPosition(m_pPlayer->position());   // 某些格式音频需要重新seek后才生效
}

void QtPlayerProxy::volumeUp()
{
    if (volume() >= 200)
        return;

    changeVolume(volume() + 10);
}

void QtPlayerProxy::volumeDown()
{
    if (volume() <= 0)
        return;

    changeVolume(volume() - 10);
}

void QtPlayerProxy::changeVolume(int nVol)
{
    m_pPlayer->setVolume(nVol);
}

int QtPlayerProxy::volume() const
{
    int nActualVol = m_pPlayer->volume();
    int nDispalyVol = static_cast<int>((nActualVol - 40) / 60.0 * 200.0);
    return nDispalyVol;
}

//int QtPlayerProxy::videoRotation() const
//{
//    int nRotate = my_get_property(m_handle, "video-rotate").toInt();
//    return (nRotate + 360) % 360;
//}

//void QtPlayerProxy::setVideoRotation(int nDegree)
//{
//    my_set_property(m_handle, "video-rotate", nDegree);
//}

//void QtPlayerProxy::setVideoAspect(double dValue)
//{
//    my_set_property(m_handle, "video-aspect", dValue);
//}

//double QtPlayerProxy::videoAspect() const
//{
//    return my_get_property(m_handle, "video-aspect").toDouble();
//}

bool QtPlayerProxy::muted() const
{
    return m_pPlayer->isMuted();
}

void QtPlayerProxy::toggleMute()
{
    bool bMute = false;

    bMute = m_pPlayer->isMuted();
    m_pPlayer->setMuted(!bMute);
}

void QtPlayerProxy::setMute(bool bMute)
{
    m_pPlayer->setMuted(bMute);
}

void QtPlayerProxy::updateSubStyle(const QString &font, int sz)
{
    Q_UNUSED(font);
    Q_UNUSED(sz);
}

void QtPlayerProxy::setSubCodepage(const QString &cp)
{
    Q_UNUSED(cp);
}

QString QtPlayerProxy::subCodepage()
{
    return QString();
}

void QtPlayerProxy::addSubSearchPath(const QString &path)
{
    Q_UNUSED(path);
}

bool QtPlayerProxy::loadSubtitle(const QFileInfo &fi)
{
    Q_UNUSED(fi);
    return false;
}

void QtPlayerProxy::toggleSubtitle()
{

}

bool QtPlayerProxy::isSubVisible()
{
    return false;
}

void QtPlayerProxy::selectSubtitle(int id)
{
    Q_UNUSED(id);
}

void QtPlayerProxy::selectTrack(int id)
{
    Q_UNUSED(id);
}

void QtPlayerProxy::setSubDelay(double secs)
{
    Q_UNUSED(secs);
}

double QtPlayerProxy::subDelay() const
{

}

int QtPlayerProxy::aid() const
{
    return 0;
}

int QtPlayerProxy::sid() const
{
    return 0;
}

void QtPlayerProxy::changeSoundMode(Backend::SoundMode)
{

}

void QtPlayerProxy::setVideoAspect(double r)
{
    Q_UNUSED(r);
}

double QtPlayerProxy::videoAspect() const
{
    return 0.0;
}

int QtPlayerProxy::videoRotation() const
{
    return 0;
}

void QtPlayerProxy::setVideoRotation(int degree)
{
    Q_UNUSED(degree);
}

QImage QtPlayerProxy::takeScreenshot()
{
    return m_currentImage;
}

void QtPlayerProxy::burstScreenshot()
{
    int nCurrentPos = static_cast<int>(m_pPlayer->position());
    int nDuration = static_cast<int>(m_pPlayer->duration() / 15);
    int nTime = 0;

    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<int> uniform_dist(0, nDuration);
    //m_listBurstPoints.clear();
    for (int i = 0; i < 15; i++) {
        //m_listBurstPoints.append(nDuration * i + uniform_dist(g));
        nTime = nDuration * i + uniform_dist(g) - 200;
        m_pPlayer->setPosition(nTime);
        QEventLoop loop;
        QTimer::singleShot(200, &loop, SLOT(quit()));
        loop.exec();
        emit notifyScreenshot(m_currentImage, nTime/1000);
    }

    m_pPlayer->setPosition(nCurrentPos);
}

void QtPlayerProxy::stopBurstScreenshot()
{

}

QVariant QtPlayerProxy::getProperty(const QString &)
{
    return 0;
}

void QtPlayerProxy::setProperty(const QString &, const QVariant &)
{

}

void QtPlayerProxy::nextFrame()
{

}

void QtPlayerProxy::previousFrame()
{

}

void QtPlayerProxy::makeCurrent()
{

}

void QtPlayerProxy::changehwaccelMode(Backend::hwaccelMode hwaccelMode)
{
    Q_UNUSED(hwaccelMode);
}

void QtPlayerProxy::initMember()
{
    m_nBurstStart = 0;

    m_pParentWidget = nullptr;

    m_bInBurstShotting = false;
    m_posBeforeBurst = false;
    m_bExternalSubJustLoaded = false;
    m_bConnectStateChange = false;
    m_bPauseOnStart = false;
    m_bInited = false;
    m_bHwaccelAuto = false;
    m_bLastIsSpecficFormat = false;

    m_listBurstPoints.clear();
    m_mapWaitSet.clear();
    m_vecWaitCommand.clear();

    m_pConfig = nullptr;
}

void QtPlayerProxy::play()
{
    if (_file.isLocalFile()) {
        QString strFilePath = QFileInfo(_file.toLocalFile()).absoluteFilePath();
        m_pPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(strFilePath)));
    } else {
        m_pPlayer->setMedia(QMediaContent(_file));
    }
    m_pPlayer->play();
}

void QtPlayerProxy::pauseResume()
{
    if (_state == PlayState::Playing){
        m_pPlayer->pause();
    }else if (_state == PlayState::Paused) {
        m_pPlayer->play();
    }
}

void QtPlayerProxy::stop()
{
    m_pPlayer->stop();
}

int QtPlayerProxy::volumeCorrection(int displayVol)
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

void QtPlayerProxy::seekForward(int nSecs)
{
    qint64 nPosition = 0;

    nPosition = m_pPlayer->position();
    nPosition = nPosition + nSecs*1000;

    if (state() != PlayState::Stopped) {
        m_pPlayer->setPosition(nPosition);
    }
}

void QtPlayerProxy::seekBackward(int nSecs)
{
    qint64 nPosition = 0;

    nPosition = m_pPlayer->position();
    nPosition = nPosition - nSecs*1000;

    if (state() != PlayState::Stopped) {
        m_pPlayer->setPosition(nPosition);
    }
}

void QtPlayerProxy::seekAbsolute(int nPos)
{
    if (state() != PlayState::Stopped)
        m_pPlayer->setPosition(nPos*1000);
}

QSize QtPlayerProxy::videoSize() const
{
    PlayItemInfo currentInfo;
    if (0 < dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->size()) {
         currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
    }

    return QSize(currentInfo.mi.width, currentInfo.mi.height);
}

qint64 QtPlayerProxy::duration() const
{
    PlayItemInfo currentInfo;
    if (0 < dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->size()) {
         currentInfo = dynamic_cast<PlayerEngine *>(m_pParentWidget)->getplaylist()->currentInfo();
    }
    if(currentInfo.mi.duration > 0){
        return currentInfo.mi.duration;
    } else {
        return m_pPlayer->duration()/1000;
    }
}


qint64 QtPlayerProxy::elapsed() const
{
    return m_pPlayer->position()/1000;
}

void QtPlayerProxy::updatePlayingMovieInfo()
{
//    m_movieInfo.subs.clear();
//    m_movieInfo.audios.clear();

//    QList<QVariant> listInfo = my_get_property(m_handle, "track-list").toList();
//    auto p = listInfo.begin();
//    while (p != listInfo.end()) {
//        const auto &t = p->toMap();
//        if (t["type"] == "audio") {
//            AudioInfo audioInfo;
//            audioInfo["type"] = t["type"];
//            audioInfo["id"] = t["id"];
//            audioInfo["lang"] = t["lang"];
//            audioInfo["external"] = t["external"];
//            audioInfo["external-filename"] = t["external-filename"];
//            audioInfo["selected"] = t["selected"];
//            audioInfo["title"] = t["title"];

//            if (t["title"].toString().size() == 0) {
//                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
//                    audioInfo["title"] = t["lang"];
//                else if (!t["external"].toBool())
//                    audioInfo["title"] = "[internal]";
//            }


//            m_movieInfo.audios.append(audioInfo);
//        } else if (t["type"] == "sub") {
//            SubtitleInfo titleInfo;
//            titleInfo["type"] = t["type"];
//            titleInfo["id"] = t["id"];
//            titleInfo["lang"] = t["lang"];
//            titleInfo["external"] = t["external"];
//            titleInfo["external-filename"] = t["external-filename"];
//            titleInfo["selected"] = t["selected"];
//            titleInfo["title"] = t["title"];
//            if (t["title"].toString().size() == 0) {
//                if (t["lang"].isValid() && t["lang"].toString().size() && t["lang"].toString() != "und")
//                    titleInfo["title"] = t["lang"];
//                else if (!t["external"].toBool())
//                    titleInfo["title"] = tr("Internal");
//            }
//            m_movieInfo.subs.append(titleInfo);
//        }
//        ++p;
//    }

//    qInfo() << m_movieInfo.subs;
//    qInfo() << m_movieInfo.audios;
}


} // end of namespace dmr

