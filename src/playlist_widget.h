#ifndef _DMR_PLAYLIST_WIDGET_H
#define _DMR_PLAYLIST_WIDGET_H 

#include <DPlatformWindowHandle>
#include <QtWidgets>

namespace Dtk
{
namespace Widget
{
    class DImageButton;
}
}

DWIDGET_USE_NAMESPACE

namespace dmr {

class MpvProxy;
class MainWindow;

class PlaylistWidget: public QFrame {
    Q_OBJECT
public:
    PlaylistWidget(QWidget *, MpvProxy*);
    virtual ~PlaylistWidget();

public slots:
    void togglePopup();
    void loadPlaylist();

private:
    MpvProxy *_mpv {nullptr};
    MainWindow *_mw {nullptr};
    QList<QWidget*> _items;
};
}

#endif /* ifndef _DMR_PLAYLIST_WIDGET_H */
