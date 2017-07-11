#include "config.h"

#include "vpu_proxy.h"
#include "vpu_decoder.h"

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
        _d->stop();
        disconnect(_d, 0, 0, 0);
        _d->wait(1000);
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


void VpuProxy::setPlayFile(const QFileInfo& fi)
{
    _file = fi;

    if (_d == nullptr) {
        _d = new VpuDecoder(fi.absoluteFilePath());
    }
    _d->updateViewportSize(QSize(864, 608));

    connect(_d, &VpuDecoder::frame, [=](const QImage& img) {
        _img = img;
        this->update();
    });

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
    _d->start();
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
