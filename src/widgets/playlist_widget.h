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

class PlayerEngine;
class MainWindow;

class PlaylistWidget: public QScrollArea {
    Q_OBJECT
public:
    PlaylistWidget(QWidget *, PlayerEngine*);
    virtual ~PlaylistWidget();

public slots:
    void togglePopup();
    void loadPlaylist();
    void openItemInFM();
    void removeClickedItem();

protected:
    void contextMenuEvent(QContextMenuEvent *cme);
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

protected slots:
    void updateItemStates();

private:
    PlayerEngine *_engine {nullptr};
    MainWindow *_mw {nullptr};
    QList<QWidget*> _items;
    QWidget *_mouseItem {nullptr};
    QWidget *_clickedItem {nullptr};
    QSignalMapper *_mapper {nullptr};
};
}

#endif /* ifndef _DMR_PLAYLIST_WIDGET_H */
