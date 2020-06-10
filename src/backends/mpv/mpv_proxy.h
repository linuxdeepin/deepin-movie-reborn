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
#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H

#define MWV206_0  //After Jing Jiawei's graphics card is upgraded, deal with the macro according to the situation,
//This macro is also available for compositing_manager.cpp.

#include <player_backend.h>
#include <player_engine.h>
#include <xcb/xproto.h>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;

class MpvProxy: public Backend
{
    Q_OBJECT

public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    const PlayingMovieInfo &playingMovieInfo() override;
    // mpv plays all files by default  (I hope)
    bool isPlayable() const override
    {
        return true;
    }

    // polling until current playback ended
    void pollingEndOfPlayback();
    // polling until current playback started
    void pollingStartOfPlayback();

    qint64 duration() const override;
    qint64 elapsed() const override;
    QSize videoSize() const override;
    void setPlaySpeed(double times) override;
    void savePlaybackPosition() override;

    bool loadSubtitle(const QFileInfo &fi) override;
    void toggleSubtitle() override;
    bool isSubVisible() override;
    void selectSubtitle(int id) override;
    int sid() const override;
    void setSubDelay(double secs) override;
    double subDelay() const override;
    void updateSubStyle(const QString &font, int sz) override;
    void setSubCodepage(const QString &cp) override;
    QString subCodepage() override;
    void addSubSearchPath(const QString &path) override;

    void selectTrack(int id) override;
    int aid() const override;

    void changeSoundMode(SoundMode sm) override;
    int volume() const override;
    bool muted() const override;

    void setVideoAspect(double r) override;
    double videoAspect() const override;
    int videoRotation() const override;
    void setVideoRotation(int degree) override;

    QImage takeScreenshot() override;
    void burstScreenshot() override; //initial the start of burst screenshotting
    void stopBurstScreenshot() override;

    QVariant getProperty(const QString &) override;
    void setProperty(const QString &, const QVariant &) override;

    void nextFrame() override;
    void previousFrame() override;

public slots:
    void play() override;
    void pauseResume() override;
    void stop() override;

    void seekForward(int secs) override;
    void seekBackward(int secs) override;
    void seekAbsolute(int pos) override;
    void volumeUp() override;
    void volumeDown() override;
    void changeVolume(int val) override;
    void toggleMute() override;

protected:
    void resizeEvent(QResizeEvent *re) override;
    void showEvent(QShowEvent *re) override;

protected slots:
    void handle_mpv_events();
    void stepBurstScreenshot();

signals:
    void has_mpv_events();

private:
    Handle _handle;
    MpvGLWidget *_gl_widget{nullptr};
    QWidget *m_parentWidget;

    bool _inBurstShotting {false};
    QVariant _posBeforeBurst;
    qint64 _burstStart {0};
    QList<qint64> _burstPoints;

    qint64 _startPlayDuration {0};

    bool _pendingSeek {false};
    PlayingMovieInfo _pmf;
    int _videoRotation {0};

    bool _polling {false};

    bool _externalSubJustLoaded {false};

    bool _connectStateChange {false};

    bool _pauseOnStart {false};

    mpv_handle *mpv_init();
    void processPropertyChange(mpv_event_property *ev);
    void processLogMessage(mpv_event_log_message *ev);
    QImage takeOneScreenshot();
    //void changeProperty(const QString &name, const QVariant &v);
    void updatePlayingMovieInfo();
    void setState(PlayState s);
    qint64 nextBurstShootPoint();
    int volumeCorrection(int);
};
}

#endif /* ifndef _DMR_MPV_PROXY_H */



