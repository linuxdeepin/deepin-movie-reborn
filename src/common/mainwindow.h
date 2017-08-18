#ifndef _DMR_MAIN_WINDOW_H
#define _DMR_MAIN_WINDOW_H 

#include <QObject>
#include <DMainWindow>
#include <DTitlebar>
#include <DPlatformWindowHandle>
#include <QtWidgets>
#include "actions.h"

namespace Dtk
{
namespace Widget
{
    class DImageButton;
}
}

DWIDGET_USE_NAMESPACE

class MainWindowEventListener;

namespace dmr {
class ToolboxProxy;
class EventMonitor;
class PlaylistWidget;
class PlayerEngine;
class NotificationWidget;

class MainWindow: public QWidget {
    Q_OBJECT
    Q_PROPERTY(QMargins frameMargins READ frameMargins NOTIFY frameMarginsChanged)
public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QMargins frameMargins() const;

    PlayerEngine* engine() { return _engine; }
    DTitlebar* titlebar() { return _titlebar; }
    ToolboxProxy* toolbox() { return _toolbox; }
    PlaylistWidget* playlist() { return _playlist; }

    void requestAction(ActionFactory::ActionKind, bool fromUI = false, QList<QVariant> args = {});

    bool insideResizeArea(const QPoint& global_p);
    QMargins dragMargins() const;

signals:
    void frameMarginsChanged();
    void windowEntered();
    void windowLeaved();

public slots:
    void play(const QUrl& url);
    void updateProxyGeometry();
    void suspendToolsWindow();
    void resumeToolsWindow();


protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;

    void keyPressEvent(QKeyEvent *ev) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void moveEvent(QMoveEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *cme) override;
    void paintEvent(QPaintEvent*) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

protected slots:
    void menuItemInvoked(QAction *action);
    void onApplicationStateChanged(Qt::ApplicationState e);
    void onBindingsChanged();
    void updateActionsState();
    void onThemeChanged();
    void updatePlayState();

    void miniButtonClicked(QString id);

#ifdef USE_DXCB
    void onMonitorButtonPressed(int x, int y);
    void onMonitorMotionNotify(int x, int y);
    void onMonitorButtonReleased(int x, int y);
#endif

private:
    void handleSettings();
    void updateSizeConstraints();
    void toggleUIMode();
    void reflectActionToUI(ActionFactory::ActionKind);
    bool insideToolsArea(const QPoint& p);
    void switchTheme();

private:
    DTitlebar *_titlebar {nullptr};
    ToolboxProxy *_toolbox {nullptr};
    PlaylistWidget *_playlist {nullptr};
    PlayerEngine *_engine {nullptr};
    QLabel *_playState {nullptr};

    DImageButton *_miniPlayBtn {nullptr};
    DImageButton *_miniCloseBtn {nullptr};
    DImageButton *_miniQuitMiniBtn {nullptr};

    bool _miniMode {false};
    QSize _lastSizeInNormalMode;
    bool _inited {false};

    DPlatformWindowHandle *_handle {nullptr};
    QMargins _cachedMargins;
    EventMonitor *_evm {nullptr};

    bool _pausedOnHide {false};

    //toggle-able states
    bool _lightTheme {false};
    bool _windowAbove {false};
    bool _mouseMoved {false};
    bool _mousePressed {false};
    double _playSpeed {1.0};

    MainWindowEventListener *_listener {nullptr};
    NotificationWidget *_nwShot {nullptr};
    NotificationWidget *_nwComm {nullptr};
    QTimer _autoHideTimer;
};
};

#endif /* ifndef _MAIN_WINDOW_H */


