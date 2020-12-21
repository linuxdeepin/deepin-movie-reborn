#include "moviewidget.h"
#include <QHBoxLayout>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QTransform>
#include <QApplication>
#include <QDesktopWidget>

#define DEFAULT_RATION (1.0f*1080/1920)   //背景图片比例
#define INTERVAL 50                       //刷新间隔
namespace dmr {

MovieWidget::MovieWidget(QWidget *parent)
    : QWidget(parent), m_nRotate(0), m_state(PlayState::StateStop)
{
    m_pLabMovie = nullptr;
    m_pTimer = nullptr;
    m_pHBoxLayout = nullptr;

    m_pHBoxLayout = new QHBoxLayout(this);
    m_pHBoxLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    setLayout(m_pHBoxLayout);

    m_pLabMovie = new QLabel(this);
    //m_pLabMovie->setScaledContents(true);
    m_pHBoxLayout->addStretch();
    m_pHBoxLayout->addWidget(m_pLabMovie);
    m_pHBoxLayout->addStretch();

    m_pixmapBg.load(":/resources/icons/music_bg.svg");

    m_pTimer = new QTimer();
    m_pTimer->setInterval(INTERVAL);
    connect(m_pTimer, &QTimer::timeout, this, &MovieWidget::updateView);
}

void MovieWidget::startPlaying()
{
    if (m_state == PlayState::StateStop) {
        m_nRotate = 0;
        show();
    }

    m_pTimer->start();
    m_state = PlayState::StatePlaying;
}

void MovieWidget::stopPlaying()
{
    m_pTimer->stop();
    m_state = PlayState::StateStop;
    hide();
}

void MovieWidget::pausePlaying()
{
    m_pTimer->stop();
    m_state = PlayState::StatePause;
}

void MovieWidget::updateView()
{
    QMatrix matri;
    QPixmap pixmapBg;
    float nRatio = 1.0f;
    QRect rectDesktop;

    int nWidth = rect().width();
    int nHeight = rect().height();
    rectDesktop = qApp->desktop()->availableGeometry(this);

    //根据比例缩放背景
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {

        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
        nRatio = nWidth * 2.0f / rectDesktop.width();
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
        nRatio = nHeight * 2.0f / rectDesktop.height();
    }

    pixmapBg = m_pixmapBg.scaled(static_cast<int>(m_pixmapBg.width() * nRatio),
                                 static_cast<int>(m_pixmapBg.height() * nRatio), Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
    //旋转背景
    matri.translate(pixmapBg.width() / 2.0, pixmapBg.height() / 2.0);
    matri.rotate(m_nRotate);
    m_nRotate = m_nRotate + 9;           //两秒转一圈
    matri.translate(-pixmapBg.width() / 2.0, -pixmapBg.height() / 2.0);

    pixmapBg = pixmapBg.transformed(matri, Qt::SmoothTransformation);

    m_pLabMovie->setPixmap(pixmapBg);
}

}
