#ifndef _DMR_NOTIFICATION_WIDGET_H
#define _DMR_NOTIFICATION_WIDGET_H 

#include <QtWidgets>

#include <DBlurEffectWidget>

DWIDGET_USE_NAMESPACE

namespace dmr {

class MainWindow;

class NotificationWidget: public QFrame {
    Q_OBJECT
public:
    enum MessageAnchor {
        AnchorNone,
        AnchorBottom,
        AnchorNorthWest
    };
    NotificationWidget(QWidget *parent = 0);
    void setAnchor(MessageAnchor ma) { _anchor = ma; }
    void setAnchorDistance(int v) { _anchorDist = v; }
    void setAnchorPoint(const QPoint& p) { _anchorPoint = p; }

public slots:
    void popupWithIcon(const QString& msg, const QPixmap&);
    void popup(const QString& msg);
    void updateWithMessage(const QString& newMsg);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *re) override;

private:
    QWidget *_mw {nullptr};
    QLabel *_msgLabel {nullptr};
    QLabel *_icon {nullptr};
    QTimer *_timer {nullptr};
    QFrame *_frame {nullptr};
    QHBoxLayout *_layout {nullptr};
    MessageAnchor _anchor {AnchorNone};
    int _anchorDist {10};
    QPoint _anchorPoint;

    void syncPosition();
};

}

#endif /* ifndef _DMR_NOTIFICATION_WIDGET_H */
