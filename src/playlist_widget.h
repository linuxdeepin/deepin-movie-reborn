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
    void openItemInFM();

protected:
    void contextMenuEvent(QContextMenuEvent *cme);

protected slots:
    void updateItemStates();

private:
    MpvProxy *_mpv {nullptr};
    MainWindow *_mw {nullptr};
    QList<QWidget*> _items;
    QWidget *_mouseItem {nullptr};
};
}

#endif /* ifndef _DMR_PLAYLIST_WIDGET_H */
