/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#ifndef _DMR_MAIN_WINDOW_H
#define _DMR_MAIN_WINDOW_H

#include <QObject>
#include <DMainWindow>
#include <DTitlebar>
#include <DPlatformWindowHandle>
//#include <QtWidgets>
#include <DFrame>
#include "actions.h"
#include "widgets/titlebar.h"
#include <DPushButton>
#include "online_sub.h"
#include <DFloatingMessage>

namespace Dtk {
namespace Widget {
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

class MainWindow: public DMainWindow
{
    Q_OBJECT
    Q_PROPERTY(bool inited READ inited WRITE setInit NOTIFY initChanged)
public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

    bool inited() const
    {
        return _inited;
    }
    PlayerEngine *engine()
    {
        return _engine;
    }
    Titlebar *titlebar()
    {
        return _titlebar;
    }
    ToolboxProxy *toolbox()
    {
        return _toolbox;
    }
    PlaylistWidget *playlist()
    {
        return _playlist;
    }

    void requestAction(ActionFactory::ActionKind, bool fromUI = false,
                       QList<QVariant> args = {}, bool shortcut = false);

    bool insideResizeArea(const QPoint &global_p);
    QMargins dragMargins() const;

    void capturedMousePressEvent(QMouseEvent *me);
    void capturedMouseReleaseEvent(QMouseEvent *me);

    void syncStaysOnTop();
    void updateGeometryNotification(const QSize &sz);

    void updateContentGeometry(const QRect &rect);

    static QString lastOpenedPath();

signals:
    void windowEntered();
    void windowLeaved();
    void initChanged();
    void frameMenuEnable(bool);

public slots:
    void play(const QUrl &url);
    void playList(const QList<QString> &l);
    void updateProxyGeometry();
    void suspendToolsWindow();
    void resumeToolsWindow();
    void checkOnlineState(const bool isOnline);
    void checkOnlineSubtitle(const OnlineSubtitle::FailReason reason);

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
    void wheelEvent(QWheelEvent *we) override;

    void keyPressEvent(QKeyEvent *ev) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void moveEvent(QMoveEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *cme) override;
    void paintEvent(QPaintEvent *) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    bool event(QEvent *event) override;

protected slots:
    void setInit(bool v);
    void menuItemInvoked(QAction *action);
    void onApplicationStateChanged(Qt::ApplicationState e);
    void onBindingsChanged();
    void updateActionsState();
    void syncPlayState();
    void animatePlayState();
    void resizeByConstraints(bool forceCentered = false);
    void onWindowStateChanged();

    void miniButtonClicked(QString id);

    void startBurstShooting();
    void onBurstScreenshot(const QImage &frame, qint64 timestamp);
    void delayedMouseReleaseHandler();

#ifdef USE_DXCB
    void onMonitorButtonPressed(int x, int y);
    void onMonitorMotionNotify(int x, int y);
    void onMonitorButtonReleased(int x, int y);

    void updateShadow();
#endif

    void handleHelpAction();

private:
    void setupTitlebar();

    void startPlayStateAnimation(bool play);
    void handleSettings();
    void updateSizeConstraints();
    void toggleUIMode();
    void reflectActionToUI(ActionFactory::ActionKind);
    bool insideToolsArea(const QPoint &p);
    void switchTheme();
    bool isActionAllowed(ActionFactory::ActionKind kd, bool fromUI, bool isShortcut);
    QString probeCdromDevice();
    void updateWindowTitle();
    void toggleShapeMask();
    void prepareSplashImages();

private:
    DFloatingMessage *popup {nullptr};

    Titlebar *_titlebar {nullptr};
    ToolboxProxy *_toolbox {nullptr};
    PlaylistWidget *_playlist {nullptr};
    PlayerEngine *_engine {nullptr};
    DIconButton *_playState {nullptr};
    MovieProgressIndicator *_progIndicator {nullptr};

    QList<QPair<QImage, qint64>> _burstShoots;
    bool _inBurstShootMode {false};
    bool _pausedBeforeBurst {false};

    DIconButton *_miniPlayBtn {nullptr};
    DIconButton *_miniCloseBtn {nullptr};
    DIconButton *_miniQuitMiniBtn {nullptr};

    QImage bg_dark;
    QImage bg_light;

    bool _miniMode {false};
    /// used to restore to recent geometry when quit fullscreen or mini mode
    QRect _lastRectInNormalMode;

    // the first time a play happens, we consider it inited.
    bool _inited {false};

    DPlatformWindowHandle *_handle {nullptr};
    EventMonitor *_evm {nullptr};

    bool _pausedOnHide {false};
    // track if next/prev is triggered in fs/maximized mode
    bool _movieSwitchedInFsOrMaxed {false};
    bool _delayedResizeByConstraint {false};

    //toggle-able states
    bool _lightTheme {false};
    bool _windowAbove {false};
    bool _mouseMoved {false};
    bool _mousePressed {false};
    double _playSpeed {1.0};

    enum StateBeforeEnterMiniMode {
        SBEM_None = 0x0,
        SBEM_Above = 0x01,
        SBEM_Fullscreen = 0x02,
        SBEM_PlaylistOpened = 0x04,
        SBEM_Maximized = 0x08,
    };
    int _stateBeforeMiniMode {0};
    Qt::WindowStates _lastWindowState {Qt::WindowNoState};

    uint32_t _lastCookie {0};
    uint32_t _powerCookie {0};

    MainWindowEventListener *_listener {nullptr};
    NotificationWidget *_nwShot {nullptr};
    NotificationWidget *_nwComm {nullptr};
    QTimer _autoHideTimer;
    QTimer _delayedMouseReleaseTimer;
};
};

#endif /* ifndef _MAIN_WINDOW_H */


