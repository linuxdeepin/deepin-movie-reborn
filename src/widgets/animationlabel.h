#ifndef ANIMATIONLABEL_H
#define ANIMATIONLABEL_H
#include <QLabel>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>

class AnimationLabel : public QLabel
{
    Q_PROPERTY(int fps READ fps WRITE setFps)

public:
    AnimationLabel(QWidget *parent = nullptr);

    void stop();
    void start();

private:
    void initPauseAnimation();
    void initPlayAnimation();

public slots:

    void onPlayAnimationChanged(const QVariant &value);
    void onPauseAnimationChanged(const QVariant &value);

protected:
    void paintEvent(QPaintEvent *e);

    QPixmap m_pixmap;

    QSequentialAnimationGroup *m_playGroup {nullptr};
    QPropertyAnimation *m_playShow {nullptr};
    QPropertyAnimation *m_playHide {nullptr};

    QSequentialAnimationGroup *m_pauseGroup {nullptr};
    QPropertyAnimation *m_pauseShow {nullptr};
    QPropertyAnimation *m_pauseHide {nullptr};

    QString m_fileName;
};

#endif  // ANIMATIONLABEL_H
