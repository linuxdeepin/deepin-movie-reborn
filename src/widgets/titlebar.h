#ifndef DMR_TITLEBAR_H
#define DMR_TITLEBAR_H 
#include <QScopedPointer>
#include <dtitlebar.h>

namespace dmr {
class TitlebarPrivate;
class Titlebar : public Dtk::Widget::DTitlebar {
    Q_OBJECT

    Q_PROPERTY(QBrush background READ background WRITE setBackground)
    Q_PROPERTY(QColor borderBottom READ borderBottom WRITE setBorderBottom)
    Q_PROPERTY(QColor borderShadowTop READ borderShadowTop WRITE setBorderShadowTop)

public:
    explicit Titlebar(QWidget *parent = 0);
    ~Titlebar();

    QString viewname() const;
    QBrush background() const;
    QColor borderBottom() const;
    QColor borderShadowTop() const;


public slots:
    void setBackground(QBrush background);
    void setBorderBottom(QColor borderBottom);
    void setBorderShadowTop(QColor borderShadowTop);

protected:
    virtual void paintEvent(QPaintEvent *e) override;

private:
    QScopedPointer<TitlebarPrivate> d_ptr;
    Q_DECLARE_PRIVATE_D(qGetPtrHelper(d_ptr), Titlebar)
    QColor m_borderBottom;
};
}
#endif /* ifndef DMR_TITLEBAR_H */
