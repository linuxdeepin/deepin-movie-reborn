#ifndef MOVIEWIDGET_H
#define MOVIEWIDGET_H

#include <DWidget>
#include <QResizeEvent>

DWIDGET_USE_NAMESPACE

class QTimer;
class QHBoxLayout;
class QLabel;
class QPixmap;

namespace dmr {

class MovieWidget: public DWidget
{
    Q_OBJECT

    enum PlayState {
        StatePlaying,
        StatePause,
        StateStop
    };

public:
    MovieWidget(QWidget *parent = nullptr);

public slots:
    void startPlaying();
    void stopPlaying();
    void pausePlaying();
    void updateView();

private:
    QLabel *m_pLabMovie;
    QTimer *m_pTimer;
    QHBoxLayout *m_pHBoxLayout;
    QPixmap m_pixmapBg;
    int m_nRotate;
    PlayState m_state;
};

}

#endif // MOVIEWIDGET_H
