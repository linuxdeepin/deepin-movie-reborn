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
#ifndef _DMR_PLAYLIST_WIDGET_H
#define _DMR_PLAYLIST_WIDGET_H

#include <DPlatformWindowHandle>
#include <QPushButton>
#include <QtWidgets>
#include <DIconButton>
#include <DFloatingButton>
#include <DListWidget>
#include <DApplicationHelper>
#include <DFontSizeManager>
#include <QBrush>
#include <DStyle>
#include <DLabel>
#include <DTextEdit>
#include <QPainterPath>


namespace Dtk {
namespace Widget {
class DImageButton;
}
}

DWIDGET_USE_NAMESPACE

namespace dmr {

class PlayerEngine;
class MainWindow;
class ListPic: public QLabel
{
    Q_OBJECT
public:
    ListPic(QPixmap pic, QWidget *parent): QLabel(parent)
    {
        setFixedSize(QSize(42, 24));
        _pic = pic;
    }
    void setPic(QPixmap pic)
    {
        _pic = pic;
    }
protected:
    void paintEvent(QPaintEvent *pe) override
    {
        /*        QPainter painter(this);
                QBrush bgColor = QBrush(_pic);
                QPainterPath pp;
                QRectF bgRect;
                bgRect.setSize(size());
                painter.setRenderHint(QPainter::Antialiasing,true);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                pp.addRoundedRect(bgRect, 3, 3);
                painter.fillPath(pp, bgColor);

                QPen pen;
                pen.setWidth(2);
                QColor borderColor(234,234,234);
        //        borderColor.setAlphaF(0.1);
                pen.setColor(borderColor);
                pen.setStyle(Qt::SolidLine);
        //        pen.setCapStyle(Qt::RoundCap);
                painter.setPen(pen);
                painter.drawPath(pp);
        */

        /*        QPainter painter(this);
                painter.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform |
                                       QPainter::Antialiasing);
                QSize size(_pic.size());
                QBitmap mask(size);
                QPainter painter1(&mask);
                painter1.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
                painter1.fillRect(mask.rect(), Qt::white);
                painter1.setBrush(QColor(0, 0, 0));
                painter1.drawRoundedRect(mask.rect(), 4, 4);
                QPixmap image = _pic;
                image.setMask(mask);

                painter.drawPixmap(rect(), image);

                QPen pen;
                pen.setWidth(2);
                if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                    pen.setColor(QColor(234, 234, 234));
                    painter.setPen(pen);
                } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
                    pen.setColor(QColor(255, 255, 255, 0.2 * 255));
                    painter.setPen(pen);
                }
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(rect(), 4, 4);
        */


        QPainter painter(this);
        painter.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform |
                               QPainter::Antialiasing);
        QSize size(_pic.size());
        QBitmap mask(size);
        QPainter painter1(&mask);
        painter1.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter1.fillRect(mask.rect(), Qt::white);
        painter1.setBrush(QColor(0, 0, 0));
        painter1.drawRoundedRect(mask.rect(), 5, 5);
        QPixmap image = _pic;
        image.setMask(mask);

        painter.setClipping(true);
        QPainterPath bg0;
        bg0.addRoundedRect(rect(), 5, 5);   //使用5个像素点，圆角效果更好
        painter.setClipPath(bg0);

        painter.drawPixmap(rect(), image);

        painter.setPen(
            QPen(DGuiApplicationHelper::instance()->applicationPalette().frameBorder().color(), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect(), 5, 5);
    };
private:
    QPixmap _pic;
};

///be replaced by DFloatingButton
/*class FloatingButton: public DPushButton
{
    Q_OBJECT
public:
    explicit FloatingButton(QWidget *p = nullptr) {}
    virtual ~FloatingButton() {}

protected:
    virtual void enterEvent(QEvent *e)
    {
        emit mouseHover(true);
    }

    virtual void leaveEvent(QEvent *e)
    {
        emit mouseHover(false);
    }

signals:
    void mouseHover(bool bFlag);
};
*/

class PlayItemWidget;

class PlaylistWidget: public QWidget
{
    Q_OBJECT
public:
    enum State {
        Opened,
        Closed,
    };

    PlaylistWidget(QWidget *, PlayerEngine *);
    virtual ~PlaylistWidget();

    State state() const
    {
        return _state;
    }
    bool toggling() const
    {
        return _toggling;
    }
    DListWidget *get_playlist()
    {
        return _playlist;
    }
    void updateSelectItem(const int key);
    void clear();
    void endAnimation();
    bool isFocusInPlaylist();
    /**
     * @brief resetFocusAttribute
     * 重置键盘交互属性，确保置首条只有tab键交互生效
     *
     * @param atr true为执行，false为跳过
     */
    void resetFocusAttribute(bool &atr);

signals:
    void stateChange(bool isShortcut);
    void sizeChange();

public slots:
    /**
     * @brief togglePopup
     * 播放列表升起/降下
     *
     * @param isShortcut 该函数是否通过快捷键触发
     */
    void togglePopup(bool isShortcut);
    void loadPlaylist();
    void openItemInFM();
    void showItemInfo();
    void removeClickedItem(bool isShortcut);
    void slotCloseTimeTimeOut();
    void slotCloseItem(QWidget *w);
    void slotDoubleClickedItem(QWidget *w);
    void slotRowsMoved();

protected:
    void contextMenuEvent(QContextMenuEvent *cme) override;
//    void dragEnterEvent(QDragEnterEvent *event) override;
//    void dragMoveEvent(QDragMoveEvent *event) override;
//    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *se) override;
    void paintEvent(QPaintEvent *pe) override;
    void resizeEvent(QResizeEvent *ev) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

protected slots:
    void updateItemStates();
    void updateItemInfo(int);
    void appendItems();
    void removeItem(int);
    void slotShowSelectItem(QListWidgetItem *);
    void OnItemChanged(QListWidgetItem *current, QListWidgetItem *previous);

private:
    PlayerEngine *_engine {nullptr};
    MainWindow *_mw {nullptr};
    QWidget *_mouseItem {nullptr};
    QWidget *_clickedItem {nullptr};
    QSignalMapper *_closeMapper {nullptr};
    QSignalMapper *_activateMapper {nullptr};
    DListWidget *_playlist {nullptr};
    State _state {Closed};
    DLabel *_num {nullptr};
    DLabel *_title {nullptr};
    /// < original row, data>
    QPair<int, PlayItemWidget *> _lastDragged {-1, nullptr};
    int _index {0};
    PlayItemWidget *pSelectItemWgt;
    void batchUpdateSizeHints();

    QPropertyAnimation *paOpen ;
    QPropertyAnimation *paClose ;
    DPushButton *m_pClearButton;

    bool _toggling {false};
    bool m_bButtonFocusOut {false};       ///键盘交互标志位
};
}

#endif /* ifndef _DMR_PLAYLIST_WIDGET_H */
