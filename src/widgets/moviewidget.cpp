#include "moviewidget.h"
#include <QHBoxLayout>
#include <QTimer>
#include <QLabel>

#define DEFAULT_RATION (1.0f*1080/1920)   //背景图片比例
#define BGTOTAL 310                       //背景图总数
#define INTERVAL 50                       //刷新间隔
namespace dmr {

MovieWidget::MovieWidget(QWidget *parent)
    : QWidget(parent), m_nCounter(0)
{
    m_pLabMovie = nullptr;
    m_pTimer = nullptr;
    m_pHBoxLayout = nullptr;

    m_pHBoxLayout = new QHBoxLayout(this);
    m_pHBoxLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    setLayout(m_pHBoxLayout);

    m_pLabMovie = new QLabel(this);
    m_pLabMovie->setScaledContents(true);
    m_pHBoxLayout->addStretch();
    m_pHBoxLayout->addWidget(m_pLabMovie);
    m_pHBoxLayout->addStretch();

    m_pTimer = new QTimer();
    m_pTimer->setInterval(INTERVAL);
    connect(m_pTimer, &QTimer::timeout, this, &MovieWidget::updateView);
}

void MovieWidget::startPlaying()
{
    m_nCounter = 0;
//    m_pTimer->start();
//    show();
}

void MovieWidget::stopPlaying()
{
    m_pTimer->stop();
    hide();
}

void MovieWidget::resizeEvent(QResizeEvent *qEvent)
{
    int nWidth = rect().width();
    int nHeight = rect().height();
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {

        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
    }

    m_pLabMovie->setFixedSize(nWidth, nHeight);

    DWidget::resizeEvent(qEvent);
}

void MovieWidget::updateView()
{
    QPixmap pixmap;

    pixmap.load(QString(":/resources/icons/movie/pic%1.png").arg(m_nCounter % BGTOTAL, 3, 10, QLatin1Char('0')));
    m_pLabMovie->setPixmap(pixmap);

    if (m_nCounter == BGTOTAL - 1) {
        m_nCounter = 0;
    } else {
        m_nCounter ++;
    }
}

}
