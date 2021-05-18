/*
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#ifndef _DMR_TIP_H
#define _DMR_TIP_H
#include <QFrame>
#include <QGuiApplication>
#include <DPalette>
#include <DApplicationHelper>
#include <DFontSizeManager>
#include <QApplication>
#include <QDesktopWidget>
#include <QDBusInterface>

namespace dmr {
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

    //单元测试在使用
    void pop(QPoint center);

    int radius() const;
    QColor borderColor() const;
    QBrush background() const;

public slots:
    void setText(const QString text);
    void setBackground(QBrush background);
    void setRadius(int radius);
    void setBorderColor(QColor borderColor);
    void slotWMChanged(QString msg);

protected:
    virtual void paintEvent(QPaintEvent *) Q_DECL_OVERRIDE;
    void enterEvent(QEvent *e) override;
    virtual void resizeEvent(QResizeEvent *ev) Q_DECL_OVERRIDE;

public:
    void resetSize(const int maxWidth);

private:
    QScopedPointer<TipPrivate> d_ptr;
    Q_DECLARE_PRIVATE_D(qGetPtrHelper(d_ptr), Tip)
    QString m_strText;
    QDBusInterface* m_pWMDBus {nullptr};
    bool bIsWM {true};
};
}
#endif /* ifndef _DMR_TIP_H */
