#ifndef _DMR_MAIN_WINDOW_H
#define _DMR_MAIN_WINDOW_H 

#include <QObject>
#include <DMainWindow>
#include <DTitlebar>
#include <DPlatformWindowHandle>
#include <QtWidgets>

DWIDGET_USE_NAMESPACE

namespace dmr {
class MpvProxy;
class ToolboxProxy;
class EventMonitor;


class MainWindow: public QWidget {
    Q_OBJECT
    Q_PROPERTY(QMargins frameMargins READ frameMargins NOTIFY frameMarginsChanged)
public:
    MainWindow(QWidget *parent = 0);
    ~MainWindow();

    QMargins frameMargins() const;

signals:
    void frameMarginsChanged();

public slots:
    void play(const QFileInfo& fi);
    void updateProxyGeometry();

protected:
    bool event(QEvent* e) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void enterEvent(QEvent *ev) override;
    void leaveEvent(QEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *cme) override;

protected slots:
    void menuItemInvoked(QAction *action);
    void timeout();
    void onApplicationStateChanged(Qt::ApplicationState e);
    void onBindingsChanged();

    void suspendToolsWindow();
    void resumeToolsWindow();

    void onMonitorButtonPressed(int x, int y);
    void onMonitorMotionNotify(int x, int y);
    void onMonitorButtonReleased(int x, int y);

private:
    void handleSettings();

private:
    MpvProxy *_proxy {nullptr};
    DTitlebar *_titlebar {nullptr};
    ToolboxProxy *_toolbox {nullptr};
    QWidget *_center {nullptr};
    DPlatformWindowHandle *_handle {nullptr};
    QMargins _cachedMargins;
    QTimer _timer;
    EventMonitor *_evm {nullptr};
};
};

#endif /* ifndef _MAIN_WINDOW_H */


