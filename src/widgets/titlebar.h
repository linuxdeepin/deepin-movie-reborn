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
#ifndef DMR_TITLEBAR_H
#define DMR_TITLEBAR_H 
#include <QScopedPointer>
//#include <dtitlebar.h>
#include <DTitlebar>

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
