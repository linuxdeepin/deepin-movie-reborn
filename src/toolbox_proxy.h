#ifndef _DMR_TOOLBOX_PROXY_H
#define _DMR_TOOLBOX_PROXY_H 

#include <DPlatformWindowHandle>
#include <DBlurEffectWidget>
#include <QtWidgets>

DWIDGET_USE_NAMESPACE

namespace dmr {

class EventRelayer;

class ToolboxProxy: public QWidget {
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
    void paintEvent(QPaintEvent *pe) override;

private:
    QWidget *_mainWindow {nullptr};
    QLabel *_timeLabel {nullptr};
};
}


#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
