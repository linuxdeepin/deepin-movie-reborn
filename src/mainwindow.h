#ifndef _DMR_MAIN_WINDOW_H
#define _DMR_MAIN_WINDOW_H 

#include <QObject>
#include <DMainWindow>
#include <DPlatformWindowHandle>
#include <QtWidgets>

DWIDGET_USE_NAMESPACE

namespace dmr {
class MpvProxy;
class TitlebarProxy;

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
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *ev) override;
    void moveEvent(QMoveEvent *ev) override;

protected slots:
    void menuItemInvoked(QAction *action);

private:
    MpvProxy *_proxy {nullptr};
    TitlebarProxy *_titlebar {nullptr};
    QWidget *_center {nullptr};
    DPlatformWindowHandle *_handle {nullptr};
    QMargins _cachedMargins;
};
};

#endif /* ifndef _MAIN_WINDOW_H */


