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
class PlayItemWidget;

class PlaylistWidget: public QListWidget {
    Q_OBJECT
public:
    enum State {
        Opened,
        Closed,
    };
    PlaylistWidget(QWidget *, PlayerEngine*);
    virtual ~PlaylistWidget();
    State state() const { return _state; }
    bool toggling() const { return _toggling; }

public slots:
    void togglePopup();
    void loadPlaylist();
    void openItemInFM();
    void showItemInfo();
    void removeClickedItem();

protected:
    void contextMenuEvent(QContextMenuEvent *cme) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *se) override;

protected slots:
    void updateItemStates();
    void updateItemInfo(int);
    void appendItems();
    void removeItem(int);

private:

    PlayerEngine *_engine {nullptr};
    MainWindow *_mw {nullptr};
    QWidget *_mouseItem {nullptr};
    QWidget *_clickedItem {nullptr};
    QSignalMapper *_closeMapper {nullptr};
    QSignalMapper *_activateMapper {nullptr};
    State _state {Closed};
    bool _toggling {false};
    /// < original row, data>
    QPair<int, PlayItemWidget*> _lastDragged {-1, nullptr}; 

    void batchUpdateSizeHints();
};
}

#endif /* ifndef _DMR_PLAYLIST_WIDGET_H */
