#ifndef _DMR_TITLEBAR_PROXY_H
#define _DMR_TITLEBAR_PROXY_H 

#include <DPlatformWindowHandle>
#include <DBlurEffectWidget>
#include "dmr_titlebar.h"

DWIDGET_USE_NAMESPACE

namespace dmr {

class EventRelayer;

/**
 * TitlebarProxy is a toplevel blurred window that should be bound with main 
 * window. It needs to keep as top level to utilize deepin-wm's blurring 
 * facility.
 */
class TitlebarProxy: public DBlurEffectWidget {
    Q_OBJECT
public:
    TitlebarProxy(QWidget *mainWindow);
    virtual ~TitlebarProxy();
    DMRTitlebar* titlebar() { return _titlebar; }
    void populateMenu();

protected slots:
    void toggleWindowState();
    void closeWindow();
    void showMinimized();

    void updatePosition(const QPoint& p);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    DPlatformWindowHandle *_handle {nullptr};
    DMRTitlebar *_titlebar {nullptr};
    QWidget *_mainWindow {nullptr};
    EventRelayer *_evRelay {nullptr};
};
}

#endif /* ifndef _DMR_TITLEBAR_PROXY_H */
