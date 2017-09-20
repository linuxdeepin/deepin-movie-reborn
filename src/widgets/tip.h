#ifndef _DMR_TIP_H
#define _DMR_TIP_H 
#include <QFrame>

namespace dmr
{
class TipPrivate;
class Tip : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(int radius READ radius WRITE setRadius)
    Q_PROPERTY(QBrush background READ background WRITE setBackground)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor)
public:
    explicit Tip(const QPixmap &icon,
                 const QString &text,
                 QWidget *parent = 0);
    ~Tip();

    void pop(QPoint center);

    int radius() const;
    QColor borderColor() const;
    QBrush background() const;

public slots:
    void setText(const QString text);
    void setBackground(QBrush background);
    void setRadius(int radius);
    void setBorderColor(QColor borderColor);

protected:
    virtual void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE;

private:
    QScopedPointer<TipPrivate> d_ptr;
    Q_DECLARE_PRIVATE_D(qGetPtrHelper(d_ptr), Tip)
};
}
#endif /* ifndef _DMR_TIP_H */
