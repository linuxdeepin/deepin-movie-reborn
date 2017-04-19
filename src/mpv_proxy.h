#ifndef _DMR_MPV_PROXY_H
#define _DMR_MPV_PROXY_H 

#include <QtWidgets>
#undef Bool
#include <mpv/qthelper.hpp>

namespace dmr {
using namespace mpv::qt;
class MpvProxy: public QWidget {
    Q_OBJECT
    Q_PROPERTY(qint64 duration READ duration)
    Q_PROPERTY(qint64 ellapsed READ ellapsed)
public:
    MpvProxy(QWidget *parent = 0);
    virtual ~MpvProxy();

    void addPlayFile(const QFileInfo& fi);
    qint64 duration() const;
    qint64 ellapsed() const;

signals:
    void has_mpv_events();

public slots:
    void play();
    void pause();
    void stop();
    void seekForward(int secs);
    void seekBackward(int secs);

protected slots:
    void handle_mpv_events();

private:
    Handle _handle;
    QList<QFileInfo> _playlist;

    mpv_handle* mpv_init();
    void process_property_change(mpv_event_property* ev);
    void process_log_message(mpv_event_log_message* ev);
};
}

#endif /* ifndef _MAIN_WINDOW_H */



