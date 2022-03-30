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
#ifndef _DMR_QTPLAYER_PROXY_H
#define _DMR_QTPLAYER_PROXY_H

#include <player_backend.h>
#include <player_engine.h>
#include <QMediaPlayer>
#include <QMediaContent>
#include "videosurface.h"
#include "qtplayer_glwidget.h"

namespace dmr {
/**
 * @file 封装qmediaplayer播放引擎
 */

class QtPlayerProxy: public Backend
{
    Q_OBJECT

public:
    explicit QtPlayerProxy(QWidget *parent = 0);
    virtual ~QtPlayerProxy();

    /**
     * @brief firstInit 第一次播放需要初库始化函数指针
     */
    void firstInit();
    /**
     * @brief updateRoundClip 更新opengl绘制圆角
     * @param roundClip 是否为圆角
     */
    void updateRoundClip(bool roundClip);
    /**
     * @brief 正在播放影片的影片信息
     */
    const PlayingMovieInfo &playingMovieInfo() override;
    bool isPlayable() const override
    {
        return true;
    }
    // polling until current playback ended
    void pollingEndOfPlayback();
    // polling until current playback started
    void pollingStartOfPlayback();
    /**
     * @brief 获取影片时间长
     */
    qint64 duration() const override;
    /**
     * @brief 获取影片当前进度
     */
    qint64 elapsed() const override;
    /**
     * @brief 获取当前影片显示大小
     */
    QSize videoSize() const override;
    /**
     * @brief 设置播放速度
     * @param 范围0.01-100
     */
    void setPlaySpeed(double dTimes) override;
    /**
     * @brief 播放记录，记录播放到当前时刻
     */
    void savePlaybackPosition() override;
public slots:
    /**
     * @brief 播放当前影片
     */
    void play() override;
    /**
     * @brief 暂停或恢复暂停
     */
    void pauseResume() override;
    /**
     * @brief 终止播放
     */
    void stop() override;
    /**
     * @brief 向前seek
     * @param 当前往前多少秒
     */
    void seekForward(int nSecs) override;
    /**
     * @brief 向后seek
     * @param 当前往后多少秒
     */
    void seekBackward(int nSecs) override;
    /**
     * @brief seek到某个位置
     * @param 某个进度点(秒)
     */
    void seekAbsolute(int nPos) override;
    /**
     * @brief 加音量
     */
    void volumeUp() override;
    /**
     * @brief 减音量
     */
    void volumeDown() override;
    /**
     * @brief 调整音量大小
     */
    void changeVolume(int nVol) override;
    /**
     * @brief 获取音量
     */
    int volume() const override;
    /**
     * @brief 静音状态
     */
    bool muted() const override;
    /**
     * @brief 循环改变静音状态
     */
    void toggleMute() override;
    /**
     * @brief 指定改变静音状态
     */
    void setMute(bool bMute) override;

    void updateSubStyle(const QString &font, int sz);
    void setSubCodepage(const QString &cp);
    QString subCodepage();
    void addSubSearchPath(const QString &path);
    bool loadSubtitle(const QFileInfo &fi);
    void toggleSubtitle();
    bool isSubVisible();
    void selectSubtitle(int id);
    void selectTrack(int id);
    void setSubDelay(double secs);
    double subDelay() const;
    int aid() const;
    int sid() const;
    void changeSoundMode(SoundMode);
    void setVideoAspect(double r);
    double videoAspect() const;
    int videoRotation() const;
    void setVideoRotation(int degree);
    QImage takeScreenshot();
    void burstScreenshot();
    void stopBurstScreenshot();
    QVariant getProperty(const QString &);
    void setProperty(const QString &, const QVariant &);
    void nextFrame();
    void previousFrame();
    void makeCurrent();
    void changehwaccelMode(hwaccelMode hwaccelMode);

protected:
    void initMember();      //初始化成员变量
    void resizeEvent(QResizeEvent *pEvent) override;
    void showEvent(QShowEvent *pEvent) override;

protected slots:
    void slotStateChanged(QMediaPlayer::State newState);
    void slotMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void slotPositionChanged(qint64 position);
    void processFrame(QVideoFrame& frame);

private:
    void updatePlayingMovieInfo();
    void setState(PlayState state);
    int volumeCorrection(int);

private:
    QMediaPlayer* m_pPlayer;
    VideoSurface* m_pVideoSurface;
    QtPlayerGLWidget* m_pGLWidget;
    QWidget *m_pParentWidget;
    PlayingMovieInfo m_movieInfo;          //播放过的影片的信息

    QVariant m_posBeforeBurst;             //截图前影片播放位置
    QList<qint64> m_listBurstPoints;       //存储连拍截图截图位置

    qint64 m_nBurstStart;                  //记录连拍截图次数

    bool m_bInBurstShotting;               //是否停止连拍截图

    bool m_bExternalSubJustLoaded;         //是否加载在线字幕
    bool m_bConnectStateChange;
    bool m_bPauseOnStart;                  //mpv是否在暂停中
    bool m_bInited;                        //mpv是否已经初始化
    bool m_bHwaccelAuto;                   //如果设置为不为自动，则不允许此类改变硬件设置
    bool m_bLastIsSpecficFormat;           //上一曲是否是特殊格式的影片，如果是则应该重新设置vo
    QMap<QString, QVariant> m_mapWaitSet;  //等待mpv初始化后设置的参数
    QVector<QVariant> m_vecWaitCommand;    //等待mpv初始化后设置的参数
    //mpv播放配置
    QMap<QString, QString> *m_pConfig;
    QImage m_currentImage;                 //当前画面
};

}

#endif /* ifndef _DMR_MPV_PROXY_H */



