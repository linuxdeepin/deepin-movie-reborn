#include "animationlabel.h"
#include <QDateTime>
#include <QDebug>
#include <QEventLoop>
#include <QPainter>

#define ANIMATION_TIME 250
#define DELAY_TIME 2000

AnimationLabel::AnimationLabel(QWidget *parent)
    : QLabel(parent)
{
    this->resize(200, 200);
    initPlayAnimation();
    initPauseAnimation();
}

void AnimationLabel::stop()
{
    if (m_pauseGroup && m_pauseGroup->state() == QAbstractAnimation::Running)
        m_pauseGroup->stop();

    m_playGroup->start();
}

void AnimationLabel::start()
{
    if (m_playGroup && m_playGroup->state() == QAbstractAnimation::Running)
        m_playGroup->stop();

    m_pauseGroup->start();
}

void AnimationLabel::initPauseAnimation()
{
    m_pauseGroup = new QSequentialAnimationGroup(this);
    m_pauseShow = new QPropertyAnimation(this, "fps");
    m_pauseHide = new QPropertyAnimation(this, "fps");


    m_pauseShow->setDuration(ANIMATION_TIME);
    m_pauseShow->setEasingCurve(QEasingCurve::InQuart);
    m_pauseShow->setStartValue(0);
    m_pauseShow->setEndValue(9);
    connect(m_pauseShow, &QPropertyAnimation::valueChanged, this,
            &AnimationLabel::onPauseAnimationChanged);

    m_pauseHide->setDuration(ANIMATION_TIME);
    m_pauseHide->setEasingCurve(QEasingCurve::InQuart);
    m_pauseHide->setStartValue(9);
    m_pauseHide->setEndValue(18);
    connect(m_pauseHide, &QPropertyAnimation::valueChanged, this,
            &AnimationLabel::onPauseAnimationChanged);

    m_pauseGroup->addAnimation(m_pauseShow);
    m_pauseGroup->addPause(DELAY_TIME);
    m_pauseGroup->addAnimation(m_pauseHide);
}

void AnimationLabel::initPlayAnimation()
{
    m_playGroup = new QSequentialAnimationGroup(this);
    m_playShow = new QPropertyAnimation(this, "fps");
    m_playHide = new QPropertyAnimation(this, "fps");


    m_playShow->setDuration(ANIMATION_TIME);
    m_playShow->setEasingCurve(QEasingCurve::InQuart);
    m_playShow->setStartValue(0);
    m_playShow->setEndValue(9);
    connect(m_playShow, &QPropertyAnimation::valueChanged, this,
            &AnimationLabel::onPlayAnimationChanged);

    m_playHide->setDuration(ANIMATION_TIME);
    m_playHide->setEasingCurve(QEasingCurve::InQuart);
    m_playHide->setStartValue(9);
    m_playHide->setEndValue(18);
    connect(m_playHide, &QPropertyAnimation::valueChanged, this,
            &AnimationLabel::onPlayAnimationChanged);

    m_playGroup->addAnimation(m_playShow);
    m_playGroup->addPause(DELAY_TIME);
    m_playGroup->addAnimation(m_playHide);
}

void AnimationLabel::onPlayAnimationChanged(const QVariant &value)
{
    m_fileName = QString(":/resources/icons/stop/%1.png").arg(value.toInt());

    m_pixmap = QPixmap(m_fileName);
    qInfo() << m_fileName << value.toInt();

    update();
}

void AnimationLabel::onPauseAnimationChanged(const QVariant &value)
{
    m_fileName = QString(":/resources/icons/start/%1.png").arg(value.toInt());

    m_pixmap = QPixmap(m_fileName);
    qInfo() << m_fileName << value.toInt();

    update();
}

void AnimationLabel::paintEvent(QPaintEvent *e)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.drawPixmap(rect(), m_pixmap);

    QLabel::paintEvent(e);
}
