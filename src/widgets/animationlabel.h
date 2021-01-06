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
    explicit AnimationLabel(QWidget *parent = nullptr, QWidget *mw = nullptr, bool composited = false);

    void stop();
    void start();

private:
    void initPauseAnimation();
    void initPlayAnimation();
    void setGeometryByMainWindow(QWidget *mw);

public slots:

    void onPlayAnimationChanged(const QVariant &value);
    void onPauseAnimationChanged(const QVariant &value);

protected:
    void paintEvent(QPaintEvent *e);
    //显示事件
    void showEvent(QShowEvent *e) override;
    //界面移动事件
    void moveEvent(QMoveEvent *e) override;
    //鼠标释放事件
    void mouseReleaseEvent(QMouseEvent *ev) override;

    QPixmap m_pixmap;

    QSequentialAnimationGroup *m_playGroup {nullptr};
    QPropertyAnimation *m_playShow {nullptr};
    QPropertyAnimation *m_playHide {nullptr};

    QSequentialAnimationGroup *m_pauseGroup {nullptr};
    QPropertyAnimation *m_pauseShow {nullptr};
    QPropertyAnimation *m_pauseHide {nullptr};
    QWidget* _mw {nullptr};
    bool _composited {false};
    QString m_fileName;
};

#endif  // ANIMATIONLABEL_H
