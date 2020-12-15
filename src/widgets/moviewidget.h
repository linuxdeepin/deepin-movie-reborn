#ifndef MOVIEWIDGET_H
#define MOVIEWIDGET_H

#include <DWidget>
#include <QResizeEvent>

DWIDGET_USE_NAMESPACE

class QTimer;
class QHBoxLayout;
class QLabel;

namespace dmr {

class MovieWidget: public DWidget
{
    Q_OBJECT

public:
    MovieWidget(QWidget *parent = nullptr);

public slots:
    void startPlaying();
    void stopPlaying();
    void updateView();

protected:
    void resizeEvent(QResizeEvent *pEvent);

private:
    QLabel *m_pLabMovie;
    QTimer *m_pTimer;
    QHBoxLayout *m_pHBoxLayout;
    int m_nCounter;
};

}

#endif // MOVIEWIDGET_H
