#include "moviewidget.h"

#include <QHBoxLayout>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QApplication>
#include <QDesktopWidget>
#include <QPainter>

#define DEFAULT_RATION (1.0f*1080/1920)     //背景图片比例
#define INTERVAL 50                         //刷新间隔
#define ROTATE_ANGLE 360/(1000*2.5/INTERVAL)  //2.5秒转一圈

namespace dmr {

MovieWidget::MovieWidget(QWidget *parent)
    : DWidget(parent), m_nRotate(0), m_nWidthNote(0), m_state(PlayState::StateStop)
{
    m_pLabMovie = nullptr;
    m_pTimer = nullptr;
    m_pHBoxLayout = nullptr;

    m_pHBoxLayout = new QHBoxLayout(this);
    m_pHBoxLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    setLayout(m_pHBoxLayout);

    m_pLabMovie = new QLabel(this);
    m_pLabMovie->setAlignment(Qt::AlignCenter);
    m_pHBoxLayout->addWidget(m_pLabMovie);

    m_pixmapBg.load(":/resources/icons/music_bg.svg");
    m_pixmapNote.load(":/resources/icons/music_note.svg");
    m_nWidthNote = m_pixmapNote.width();

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
    QPixmap pixmapBg;
    float nRatio = 1.0f;
    QRect rectDesktop;
    int nWidth = 0;
    int nHeight = 0;

    pixmapBg = m_pixmapBg;

    //绘制旋转音符
    QPainter painter(&pixmapBg);
    painter.translate(pixmapBg.width() / 2.0, pixmapBg.height() / 2.0);
    painter.rotate(m_nRotate);
    painter.translate(-pixmapBg.width() / 2.0, -pixmapBg.height() / 2.0);
    painter.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    painter.drawPixmap(pixmapBg.width() / 2 - m_nWidthNote / 2, pixmapBg.height() / 2 - m_nWidthNote / 2, m_nWidthNote, m_nWidthNote, m_pixmapNote);

    m_nRotate += ROTATE_ANGLE;

    nWidth = rect().width();
    nHeight = rect().height();
    rectDesktop = qApp->desktop()->availableGeometry(this);

    //根据比例缩放背景
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {

        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
        nRatio = nWidth * 2.0f / rectDesktop.width();
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
        nRatio = nHeight * 2.0f / rectDesktop.height();
    }

    QPixmap pixmapFinal = pixmapBg.scaled(static_cast<int>(pixmapBg.width() * nRatio),
                                          static_cast<int>(pixmapBg.height() * nRatio), Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation);
    m_pLabMovie->setPixmap(pixmapFinal);
}

}
