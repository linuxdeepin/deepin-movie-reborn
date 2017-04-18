#ifndef DMR_TITLEBAR_H
#define DMR_TITLEBAR_H

#include <QWidget>
#include <QMenu>

#include <DObject>
#include <dwidget_global.h>

namespace dmr {

using namespace Dtk;

class DMRTitlebarPrivate;

class DMRTitlebar: public QWidget, public DObject
{
    Q_OBJECT
public:
    explicit DMRTitlebar(QWidget *parent = 0);

#ifndef QT_NO_MENU
    QMenu *menu() const;
    void setMenu(QMenu *menu);
#endif

    QWidget *customWidget() const;
    void setCustomWidget(QWidget *, bool fixCenterPos = false);
    void setCustomWidget(QWidget *, Qt::AlignmentFlag flag = Qt::AlignCenter, bool fixCenterPos = false);
    void setWindowFlags(Qt::WindowFlags type);
    int buttonAreaWidth() const;
    bool separatorVisible() const;

    void setVisible(bool visible) Q_DECL_OVERRIDE;

    void resize(int width, int height);
    void resize(const QSize &);
signals:
    void closeButtonClicked();
    void maxButtonClicked();
    void minButtonClicked();

    void optionClicked();
    void doubleClicked();
    void mousePressed(Qt::MouseButtons buttons);
    void mouseMoving(Qt::MouseButton botton);

public slots:
    void setFixedHeight(int h);
    void setSeparatorVisible(bool visible);
    void setTitle(const QString &title);
    void setIcon(const QPixmap &icon);

private slots:
#ifndef QT_NO_MENU
    void showMenu();
#endif

protected:
    void showEvent(QShowEvent *event) Q_DECL_OVERRIDE;
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE;

private:
    D_DECLARE_PRIVATE(DMRTitlebar)
};

}

#endif // DMR_TITLEBAR_H
