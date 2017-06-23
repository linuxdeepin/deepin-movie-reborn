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
    NotificationWidget(QWidget *parent = 0);

public slots:
    void popup(const QString& msg, bool success);

private:
    QWidget *_mw {nullptr};
    QLabel *_msgLabel {nullptr};
    QLabel *_icon {nullptr};
};

}

#endif /* ifndef _DMR_NOTIFICATION_WIDGET_H */
