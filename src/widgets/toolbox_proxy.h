#ifndef _DMR_TOOLBOX_PROXY_H
#define _DMR_TOOLBOX_PROXY_H 

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
class VolumeButton;
class MainWindow;

class ToolboxProxy: public QFrame {
    Q_OBJECT
public:
    ToolboxProxy(QWidget *mainWindow, PlayerEngine*);
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
    void updateVolumeState();
    void updateMovieProgress();
    void updateButtonStates();

protected:
    void paintEvent(QPaintEvent *pe) override;

private:
    void setup();

    MainWindow *_mainWindow {nullptr};
    PlayerEngine *_engine {nullptr};
    QLabel *_timeLabel {nullptr};

    DImageButton *_playBtn {nullptr};
    DImageButton *_prevBtn {nullptr};
    DImageButton *_nextBtn {nullptr};

    VolumeButton *_volBtn {nullptr};
    DImageButton *_listBtn {nullptr};
    DImageButton *_fsBtn {nullptr};

    QProgressBar *_progBar {nullptr};
};
}


#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
