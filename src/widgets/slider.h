#ifndef _DMR_SLIDER_H
#define _DMR_SLIDER_H 

#include <QtWidgets>
namespace dmr {
class DMRSlider: public QSlider {
    Q_OBJECT
public:
    DMRSlider(QWidget *parent = 0);

protected:
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
};

}

#endif /* ifndef _DMR_SLIDER_H */
