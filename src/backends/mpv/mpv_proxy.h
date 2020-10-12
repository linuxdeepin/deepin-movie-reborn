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

typedef mpv_event *(*mpv_waitEvent)(mpv_handle *ctx, double timeout);
typedef int (*mpv_set_optionString)(mpv_handle *ctx, const char *name, const char *data);
typedef int (*mpv_setProperty)(mpv_handle *ctx, const char *name, mpv_format format,
                               void *data);
typedef int (*mpv_setProperty_async)(mpv_handle *ctx, uint64_t reply_userdata,
                                     const char *name, mpv_format format, void *data);
typedef int (*mpv_commandNode)(mpv_handle *ctx, mpv_node *args, mpv_node *result);
typedef int (*mpv_commandNode_async)(mpv_handle *ctx, uint64_t reply_userdata,
                                     mpv_node *args);
typedef int (*mpv_getProperty)(mpv_handle *ctx, const char *name, mpv_format format,
                               void *data);
typedef int (*mpv_observeProperty)(mpv_handle *mpv, uint64_t reply_userdata,
                                   const char *name, mpv_format format);
typedef const char *(*mpv_eventName)(mpv_event_id event);
typedef mpv_handle *(*mpvCreate)(void);
typedef int (*mpv_requestLog_messages)(mpv_handle *ctx, const char *min_level);
typedef int (*mpv_observeProperty)(mpv_handle *mpv, uint64_t reply_userdata,
                                   const char *name, mpv_format format);
typedef void (*mpv_setWakeup_callback)(mpv_handle *ctx, void (*cb)(void *d), void *d);
typedef int (*mpvinitialize)(mpv_handle *ctx);
typedef void (*mpv_freeNode_contents)(mpv_node *node);
typedef void (*mpv_terminateDestroy)(mpv_handle *ctx);

static QString libPath(const QString &strlib)
{
    QDir  dir;
    QString path  = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    dir.setPath(path);
    QStringList list = dir.entryList(QStringList() << (strlib + "*"), QDir::NoDotAndDotDot | QDir::Files); //filter name with strlib
    if (list.contains(strlib)) {
        return strlib;
    } else {
        list.sort();
    }

    Q_ASSERT(list.size() > 0);
    return list.last();
}

class myHandle
{
    struct container {
        container(mpv_handle *h) : mpv(h) {}
        ~container()
        {
            mpv_terminateDestroy fun = (mpv_terminateDestroy)QLibrary::resolve(libPath("libmpv.so.1"), "mpv_terminate_destroy");
            fun(mpv);
        }
        mpv_handle *mpv;
    };
    QSharedPointer<container> sptr;
public:
    // Construct a new Handle from a raw mpv_handle with refcount 1. If the
    // last Handle goes out of scope, the mpv_handle will be destroyed with
    // mpv_terminate_destroy().
    // Never destroy the mpv_handle manually when using this wrapper. You
    // will create dangling pointers. Just let the wrapper take care of
    // destroying the mpv_handle.
    // Never create multiple wrappers from the same raw mpv_handle; copy the
    // wrapper instead (that's what it's for).
    static myHandle myFromRawHandle(mpv_handle *handle)
    {
        myHandle h;
        h.sptr = QSharedPointer<container>(new container(handle));
        return h;
    }

    // Return the raw handle; for use with the libmpv C API.
    operator mpv_handle *() const
    {
        return sptr ? (*sptr).mpv : 0;
    }
};

namespace dmr {
using namespace mpv::qt;
class MpvGLWidget;

class MpvProxy: public Backend
{
    Q_OBJECT

    struct my_node_autofree {
        mpv_node *ptr;
        my_node_autofree(mpv_node *a_ptr) : ptr(a_ptr) {}
        ~my_node_autofree()
        {
            mpv_freeNode_contents(ptr);
        }
    };

public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

//    //add by heyi
    /**
     * @brief initMpvFuns   初始化MPV动态调用库函数
     */
    void initMpvFuns();

    //add by heyi
    /**
     * @brief firstInit 第一次播放需要初库始化函数指针
     */
    void firstInit();

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
public:
    //add by heyi
    QVariant my_get_property(mpv_handle *ctx, const QString &name) const;
    int my_set_property(mpv_handle *ctx, const QString &name, const QVariant &v);
    bool my_command_async(mpv_handle *ctx, const QVariant &args, uint64_t tag);
    int my_set_property_async(mpv_handle *ctx, const QString &name,
                              const QVariant &v, uint64_t tag);
    QVariant my_get_property_variant(mpv_handle *ctx, const QString &name);
    QVariant my_command(mpv_handle *ctx, const QVariant &args);

    mpv_waitEvent m_waitEvent{nullptr};
    mpv_set_optionString m_setOptionString{nullptr};
    mpv_setProperty m_setProperty{nullptr};
    mpv_setProperty_async m_setPropertyAsync{nullptr};
    mpv_commandNode m_commandNode{nullptr};
    mpv_commandNode_async m_commandNodeAsync{nullptr};
    mpv_getProperty m_getProperty{nullptr};
    mpv_observeProperty m_observeProperty{nullptr};
    mpv_eventName m_eventName{nullptr};
    mpvCreate m_creat{nullptr};
    mpv_requestLog_messages m_requestLogMessage{nullptr};
    mpv_setWakeup_callback m_setWakeupCallback{nullptr};
    mpvinitialize m_initialize{nullptr};
    mpv_freeNode_contents m_freeNodecontents{nullptr};

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
    //lambda表达式改为槽函数
    void slotStateChanged();

protected:
    void resizeEvent(QResizeEvent *re) override;
    void showEvent(QShowEvent *re) override;

protected slots:
    void handle_mpv_events();
    void stepBurstScreenshot();

signals:
    void has_mpv_events();

private:
    myHandle _handle;
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
    bool m_bInited {false};
};

}

#endif /* ifndef _DMR_MPV_PROXY_H */



