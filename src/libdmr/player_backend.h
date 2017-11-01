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
#ifndef _DMR_PLAYER_BACKEND_H
#define _DMR_PLAYER_BACKEND_H 

#include <QtWidgets>

namespace dmr {
class PlayingMovieInfo;

// Player backend base class
// There are only two backends: mpv and vpu
// mpv is the only and default on all platform except Sunway
// vpu is default for Sunway if media file can be hardware-decoded by coda vpu
class Backend: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration)
    Q_PROPERTY(qint64 elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(bool paused READ paused)

    Q_PROPERTY(PlayState state READ state NOTIFY stateChanged)
public:
    enum PlayState {
        Playing,
        Paused,
        Stopped
    };
    Q_ENUM(PlayState)

    enum SoundMode {
        Stereo,
        Left,
        Right
    };
    Q_ENUM(SoundMode)

    enum DebugLevel {
        Info,
        Debug,  // some normal debug info
        Verbose // very verbosed output from backend
    };
    Q_ENUM(DebugLevel)

    Backend(QWidget *parent = 0) {}
    virtual ~Backend() {}

    virtual void setPlayFile(const QUrl& url) { _file = url; }
    virtual void setDVDDevice(const QString& path) { _dvdDevice = path; }

    // NOTE: need to check if file is playable by this backend, 
    // this is important especially for vpu
    virtual bool isPlayable() const = 0;

    virtual qint64 duration() const { return 0; }
    virtual qint64 elapsed() const { return 0; }
    virtual QSize videoSize() const = 0;

    virtual bool paused() { return _state == PlayState::Paused; }
    virtual PlayState state() const { return _state; }
    virtual const PlayingMovieInfo& playingMovieInfo() = 0;
    virtual void setPlaySpeed(double times) = 0;
    virtual void savePlaybackPosition() = 0;
    virtual void updateSubStyle(const QString& font, int sz) = 0;
    virtual void setSubCodepage(const QString& cp) = 0;
    virtual QString subCodepage() = 0;
    virtual void addSubSearchPath(const QString& path) = 0;

    virtual bool loadSubtitle(const QFileInfo& fi) = 0;
    virtual void toggleSubtitle() = 0;
    virtual bool isSubVisible() = 0;
    virtual void selectSubtitle(int id) = 0;
    virtual void selectTrack(int id) = 0;
    virtual void setSubDelay(double secs) = 0;
    virtual double subDelay() const = 0;

    virtual int aid() const = 0;
    virtual int sid() const = 0;

    virtual void changeSoundMode(SoundMode sm) {}
    virtual int volume() const = 0;
    virtual bool muted() const = 0;

    virtual void setVideoAspect(double r) = 0;
    virtual double videoAspect() const = 0;

    virtual int videoRotation() const = 0;
    virtual void setVideoRotation(int degree) = 0;

    virtual QImage takeScreenshot() = 0;
    virtual void burstScreenshot() = 0; //initial the start of burst screenshotting
    virtual void stopBurstScreenshot() = 0;

    // hack: used to access backend internal states
    virtual QVariant getProperty(const QString&) = 0;
    virtual void setProperty(const QString&, const QVariant&) = 0;

    virtual void nextFrame() = 0;
    virtual void previousFrame() = 0;

    static void setDebugLevel(DebugLevel lvl) { _debugLevel = lvl; }

Q_SIGNALS:
    void tracksChanged();
    void elapsedChanged();
    void videoSizeChanged();
    void stateChanged();
    void fileLoaded();
    void muteChanged();
    void volumeChanged();
    void sidChanged();
    void aidChanged();

    //emit during burst screenshotting
    void notifyScreenshot(const QImage& frame, qint64 time);

public slots:
    virtual void play() = 0;
    virtual void pauseResume() = 0;
    virtual void stop() = 0;

    virtual void seekForward(int secs) = 0;
    virtual void seekBackward(int secs) = 0;
    virtual void seekAbsolute(int) = 0;
    virtual void volumeUp() = 0;
    virtual void volumeDown() = 0;
    virtual void changeVolume(int val) = 0;
    virtual void toggleMute() = 0;

protected:
    PlayState _state { PlayState::Stopped };
    QString _dvdDevice {"/dev/sr0"};
    QUrl _file;
    static DebugLevel _debugLevel;
};
}

#endif /* ifndef _DMR_PLAYER_BACKEND_H */

