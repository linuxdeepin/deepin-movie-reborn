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
class MovieProgressIndicator;

class MainWindow: public QFrame {
    Q_OBJECT
    Q_PROPERTY(QMargins frameMargins READ frameMargins NOTIFY frameMarginsChanged)
    Q_PROPERTY(bool inited READ inited WRITE setInit NOTIFY initChanged)
public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QMargins frameMargins() const;

    bool inited() const { return _inited; }
    PlayerEngine* engine() { return _engine; }
    DTitlebar* titlebar() { return _titlebar; }
    ToolboxProxy* toolbox() { return _toolbox; }
    PlaylistWidget* playlist() { return _playlist; }

    void requestAction(ActionFactory::ActionKind, bool fromUI = false,
            QList<QVariant> args = {}, bool shortcut = false);

    bool insideResizeArea(const QPoint& global_p);
    QMargins dragMargins() const;

    void capturedMousePressEvent(QMouseEvent* me);
    void capturedMouseReleaseEvent(QMouseEvent* me);

signals:
    void frameMarginsChanged();
    void windowEntered();
    void windowLeaved();
    void initChanged();

public slots:
    void play(const QUrl& url);
    void playList(const QList<QString>& l);
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
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void focusInEvent(QFocusEvent *fe) override;
    void wheelEvent(QWheelEvent* we) override;

    void keyPressEvent(QKeyEvent *ev) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void moveEvent(QMoveEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *cme) override;
    void paintEvent(QPaintEvent*) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

protected slots:
    void setInit(bool v);
    void menuItemInvoked(QAction *action);
    void onApplicationStateChanged(Qt::ApplicationState e);
    void onBindingsChanged();
    void updateActionsState();
    void updatePlayState();
    void resizeByConstraints(bool forceCentered = false);

    void miniButtonClicked(QString id);

    void startBurstShooting();
    void onBurstScreenshot(const QImage& frame, qint64 timestamp);
    void delayedMouseReleaseHandler();

#ifdef USE_DXCB
    void onMonitorButtonPressed(int x, int y);
    void onMonitorMotionNotify(int x, int y);
    void onMonitorButtonReleased(int x, int y);
#endif

    void handleHelpAction();

private:
    void handleSettings();
    void updateSizeConstraints();
    void toggleUIMode();
    void reflectActionToUI(ActionFactory::ActionKind);
    bool insideToolsArea(const QPoint& p);
    void switchTheme();
    bool isActionAllowed(ActionFactory::ActionKind kd, bool fromUI, bool isShortcut);
    QString probeCdromDevice();
    void updateWindowTitle();
    void toggleShapeMask();
    void prepareSplashImages();

private:
    DTitlebar *_titlebar {nullptr};
    ToolboxProxy *_toolbox {nullptr};
    PlaylistWidget *_playlist {nullptr};
    PlayerEngine *_engine {nullptr};
    DImageButton *_playState {nullptr};
    MovieProgressIndicator *_progIndicator {nullptr};

    QList<QPair<QImage, qint64>> _burstShoots;
    bool _inBurstShootMode {false};
    bool _pausedBeforeBurst {false};

    DImageButton *_miniPlayBtn {nullptr};
    DImageButton *_miniCloseBtn {nullptr};
    DImageButton *_miniQuitMiniBtn {nullptr};

    QImage bg_dark;
    QImage bg_light;

    bool _miniMode {false};
    QSize _lastSizeInNormalMode;

    // the first time a play happens, we consider it inited.
    bool _inited {false};

    DPlatformWindowHandle *_handle {nullptr};
    QMargins _cachedMargins;
    EventMonitor *_evm {nullptr};

    bool _pausedOnHide {false};
    // track if next/prev is triggered in fs/maximized mode
    bool _movieSwitchedInFsOrMaxed {false};
    bool _hasPendingResizeByConstraint {false};

    //toggle-able states
    bool _lightTheme {false};
    bool _windowAbove {false};
    bool _mouseMoved {false};
    bool _mouseDraggedOnTitlebar {false};
    bool _mousePressed {false};
    double _playSpeed {1.0};

    enum StateBeforeEnterMiniMode {
        SBEM_None = 0x0,
        SBEM_Above = 0x01,
        SBEM_Fullscreen = 0x02,
        SBEM_PlaylistOpened = 0x04
    }; 
    int _stateBeforeMiniMode {0};

    uint32_t _lastCookie {0};

    MainWindowEventListener *_listener {nullptr};
    NotificationWidget *_nwShot {nullptr};
    NotificationWidget *_nwComm {nullptr};
    QTimer _autoHideTimer;
    QTimer _delayedMouseReleaseTimer;
};
};

#endif /* ifndef _MAIN_WINDOW_H */


