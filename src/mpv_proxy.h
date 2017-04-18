#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include <QtWidgets>
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvProxy: public QWidget {
    Q_OBJECT
public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    void addPlayFile(const QFileInfo& fi);

    public slots:
        void play();
        void pause();
        void stop();

private:
    Handle _handle;
    QList<QFileInfo> _playlist;

    mpv_handle* mpv_init();
};
}

#endif /* ifndef _MAIN_WINDOW_H */



