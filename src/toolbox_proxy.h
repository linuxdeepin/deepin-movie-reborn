#ifndef _DMR_TOOLBOX_PROXY_H
#define _DMR_TOOLBOX_PROXY_H 

#include <DPlatformWindowHandle>
#include <DBlurEffectWidget>
#include <QtWidgets>
#include "dmr_titlebar.h"

DWIDGET_USE_NAMESPACE

namespace dmr {

class EventRelayer;

/**
 * TitlebarProxy is a toplevel blurred window that should be bound with main 
 * window. It needs to keep as top level to utilize deepin-wm's blurring 
 * facility.
 */
class ToolboxProxy: public DBlurEffectWidget {
    Q_OBJECT
public:
    ToolboxProxy(QWidget *mainWindow);
    virtual ~ToolboxProxy();

    void updateTimeInfo(qint64 duration, qint64 pos);

signals:
    void requestPlay();
    void requestPause();
    void requestNextInList();
    void requesstPrevInList();

protected slots:
    void updatePosition(const QPoint& p);
    void buttonClicked(QString id);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    DPlatformWindowHandle *_handle {nullptr};
    QWidget *_mainWindow {nullptr};
    EventRelayer *_evRelay {nullptr};
    QLabel *_timeLabel {nullptr};
};
}


#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
