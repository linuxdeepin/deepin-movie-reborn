#include "mpv_proxy.h"
#include <mpv/client.h>

namespace dmr {
using namespace mpv::qt;

MpvProxy::MpvProxy(QWidget *parent)
    :QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setAttribute(Qt::WA_NativeWindow);

    qDebug() << "proxy hook winId " << this->winId();
    _handle = Handle::FromRawHandle(mpv_init());
}

MpvProxy::~MpvProxy()
{
}

mpv_handle* MpvProxy::mpv_init()
{
    mpv_handle *h = mpv_create();
    /*        - config
     *        - config-dir
     *        - input-conf
     *        - load-scripts
     *        - script
     *        - player-operation-mode
     *        - input-app-events (OSX)
     *      - all encoding mode options
     */

    set_property(h, "vo", "opengl");
    set_property(h, "wid", this->winId());
    mpv_initialize(h);

    return h;
}

void MpvProxy::play()
{
    if (_playlist.size()) {
        QList<QVariant> args = { "loadfile", _playlist[0].canonicalFilePath() };
        qDebug () << args;
        command(_handle, args);
    }
}

void MpvProxy::pause()
{
}

void MpvProxy::stop()
{
}

void MpvProxy::addPlayFile(const QFileInfo& fi)
{
    if (fi.exists()) _playlist.append(fi);
}

} // end of namespace dmr
