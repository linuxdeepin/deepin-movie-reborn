#ifndef _DMR_SLIDER_H
#define _DMR_SLIDER_H 

#include <QtWidgets>
namespace dmr {
class DMRSlider: public QSlider {
    Q_OBJECT
public:
    DMRSlider(QWidget *parent = 0);

signals:
    void hoverChanged(int);
    void leave();

protected:
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    bool _down {false};
    int _lastHoverValue {0};
    int position2progress(const QPoint& p);
};

}

#endif /* ifndef _DMR_SLIDER_H */
