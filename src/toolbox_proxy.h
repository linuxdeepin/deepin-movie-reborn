#ifndef _DMR_TOOLBOX_PROXY_H
#define _DMR_TOOLBOX_PROXY_H 

#include <DPlatformWindowHandle>
#include <DBlurEffectWidget>
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

class MpvProxy;
class EventRelayer;
class ToolButton;

class ToolboxProxy: public QFrame {
    Q_OBJECT
public:
    ToolboxProxy(QWidget *mainWindow, MpvProxy*);
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
    void updatePlayState();
    void updateFullState();

protected:
    void paintEvent(QPaintEvent *pe) override;

private:
    void setup();

    QWidget *_mainWindow {nullptr};
    MpvProxy *_mpv {nullptr};
    QLabel *_timeLabel {nullptr};

    DImageButton *_playBtn {nullptr};
    DImageButton *_prevBtn {nullptr};
    DImageButton *_nextBtn {nullptr};

    DImageButton *_volBtn {nullptr};
    DImageButton *_listBtn {nullptr};
    DImageButton *_fsBtn {nullptr};
};
}


#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
