#ifndef _DMR_NOTIFICATION_WIDGET_H
#define _DMR_NOTIFICATION_WIDGET_H 

#include <QtWidgets>

#include <DBlurEffectWidget>

DWIDGET_USE_NAMESPACE

namespace dmr {

class MainWindow;

class NotificationWidget: public DBlurEffectWidget {
    Q_OBJECT
public:
    enum MessageAnchor {
        AnchorNone,
        AnchorBottom
    };
    NotificationWidget(QWidget *parent = 0);
    void setAnchor(MessageAnchor ma) { _anchor = ma; }
    void setAnchorDistance(int v) { _anchorDist = v; }

public slots:
    void popupWithIcon(const QString& msg, const QPixmap&);
    void popup(const QString& msg);
    void updateWithMessage(const QString& newMsg);

protected:
    void showEvent(QShowEvent *event) override;

private:
    QWidget *_mw {nullptr};
    QLabel *_msgLabel {nullptr};
    QLabel *_icon {nullptr};
    QTimer *_timer {nullptr};
    QFrame *_frame {nullptr};
    QHBoxLayout *_layout {nullptr};
    MessageAnchor _anchor {AnchorNone};
    int _anchorDist {10};
};

}

#endif /* ifndef _DMR_NOTIFICATION_WIDGET_H */
