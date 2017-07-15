#include "config.h"

#include "vpu_proxy.h"
#include "vpu_decoder.h"
#if defined (__cplusplus)
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libavresample/avresample.h>


#if defined (__cplusplus)
}
#endif

namespace dmr {

VpuProxy::VpuProxy(QWidget *parent)
    :Backend(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    //setAttribute(Qt::WA_PaintOnScreen);
}

VpuProxy::~VpuProxy()
{
}

void VpuProxy::closeEvent(QCloseEvent *ce)
{
    if (_d) {
        disconnect(_d, 0, 0, 0);
        _d->stop();

        int tries = 10;
        while (tries--) 
            _d->wait(500);
        delete _d;
        _d = 0;
    }
    ce->accept();
}

void VpuProxy::paintEvent(QPaintEvent *pe)
{
    QPainter p(this);
    p.drawImage(0, 0, _img);
}

void VpuProxy::video_refresh_timer() 
{
    double actual_delay, delay, sync_threshold, ref_clock, diff;

    if (_d == nullptr || _d->isFinished()) 
        return;

    //if(_d->frames().size() == 0) {
        //QTimer::singleShot(0, this, &VpuProxy::video_refresh_timer);
    //} else 
    {
        auto vp = _d->frames().deque();

        delay = vp.pts - _frameLastPts; /* the pts from last time */
        if(delay <= 0 || delay >= 1.0) {
            /* if incorrect delay, use previous one */
            delay = _frameLastDelay;
        }
        /* save for next time */
        _frameLastDelay = delay;
        _frameLastPts = vp.pts;

        /* update delay to sync to audio */
        //ref_clock = get_audio_clock(is);
        ref_clock = _d->getClock();
        diff = vp.pts - ref_clock;

        /* Skip or repeat the frame. Take delay into account
           FFPlay still doesn't "know if this is the best guess." */
        sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
        if(fabs(diff) < AV_NOSYNC_THRESHOLD) {
            if(diff <= -sync_threshold) {
                delay = 0;
            } else if(diff >= sync_threshold) {
                delay = 2 * delay;
            }
        }
        _frameTimer += delay;
        /* computer the REAL delay */
        actual_delay = _frameTimer - (av_gettime() / 1000000.0);
        if(actual_delay < 0.010) {
            /* Really it should skip the picture instead */
            actual_delay = 0.000;
        }
        fprintf(stderr, "%s: audio clock %f, vp.pts %f, delay %f, actual_delay %f, _frameTimer %f\n",
                __func__, ref_clock, vp.pts, delay, actual_delay, _frameTimer);
        QTimer::singleShot((int)(actual_delay * 1000 + 0.5), this, &VpuProxy::video_refresh_timer);

        /* show the picture! */
        _img = vp.img;
        this->update();
    }
}

void VpuProxy::setPlayFile(const QFileInfo& fi)
{
    _file = fi;

    if (_d == nullptr) {
        _d = new VpuMainThread(fi.absoluteFilePath());
        _d->videoThread()->updateViewportSize(QSize(864, 608));
    }

    fprintf(stderr, "%s\n", __func__);
}


void VpuProxy::setState(PlayState s)
{
    if (_state != s) {
        _state = s;
        emit stateChanged();
    }
}

const PlayingMovieInfo& VpuProxy::playingMovieInfo()
{
    return _pmf;
}

void VpuProxy::loadSubtitle(const QFileInfo& fi)
{
    if (state() == PlayState::Stopped) {
        return;
    }

}

bool VpuProxy::isSubVisible()
{
    return true;
}

void VpuProxy::toggleSubtitle()
{
    if (state() == PlayState::Stopped) {
        return;
    }

}

void VpuProxy::volumeUp()
{
}

void VpuProxy::changeVolume(int val)
{
}

void VpuProxy::volumeDown()
{
}

int VpuProxy::volume() const
{
    return 100;
}

bool VpuProxy::muted() const
{
    return false;
}

void VpuProxy::toggleMute()
{
}

void VpuProxy::play()
{
    _frameTimer = (double)av_gettime() / 1000000.0;
    _frameLastDelay = 40e-3;
    _d->start();
    video_refresh_timer();
}


void VpuProxy::pauseResume()
{
    if (_state == PlayState::Stopped)
        return;
}

void VpuProxy::stop()
{
}

QImage VpuProxy::takeScreenshot()
{
    return QImage();
}

void VpuProxy::burstScreenshot()
{
}

void VpuProxy::stopBurstScreenshot()
{
}

void VpuProxy::seekForward(int secs)
{
    if (state() == PlayState::Stopped) return;

}

void VpuProxy::seekBackward(int secs)
{
    if (state() == PlayState::Stopped) return;
}

qint64 VpuProxy::duration() const
{
    return 0;
}


qint64 VpuProxy::ellapsed() const
{
    return 0;
}

void VpuProxy::updatePlayingMovieInfo()
{
}
}
