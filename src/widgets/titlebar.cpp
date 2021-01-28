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
#include "config.h"
#include "titlebar.h"
#include "utils.h"

#include <QtGui>
#include <DThemeManager>
#include <QPainterPath>
#include "../accessibility/ac-deepin-movie-define.h"

DWIDGET_USE_NAMESPACE

namespace dmr {
class TitlebarPrivate
{
public:
    explicit TitlebarPrivate(Titlebar *parent) : q_ptr(parent) {}

    QColor          playColor                   = QColor(255, 255, 255, 204);
    QColor          lightEffectColor            = QColor(200, 200, 200, 45);
    QColor          darkEffectColor             = QColor(30, 30, 30, 50);
    qreal           offsetX                     = 0;
    qreal           offsetY                     = 15;
    qreal           blurRadius                  = 50;
    QGraphicsDropShadowEffect *m_shadowEffect   = nullptr;
    DTitlebar       *m_titlebar                 = nullptr;
    DLabel          *m_titletxt                 = nullptr;
    bool            m_play                      = false;

    Titlebar *q_ptr;
    Q_DECLARE_PUBLIC(Titlebar)
};
/**
 * @brief Titlebar 构造函数
 * @param parent 父窗口
 */
Titlebar::Titlebar(QWidget *parent) : DBlurEffectWidget(parent), d_ptr(new TitlebarPrivate(this))
{
    Q_D(Titlebar);

    setAttribute(Qt::WA_TranslucentBackground, false);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(TITLEBAR);
    setAccessibleName(TITLEBAR);
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    d->m_titlebar = new DTitlebar(this);
    d->m_titlebar->setFocusPolicy(Qt::NoFocus);
    layout->addWidget(d->m_titlebar);
    setLayout(layout);

    d->m_titlebar->setWindowFlags(Qt::WindowMinMaxButtonsHint |
                                  Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    d->m_titlebar->setBackgroundTransparent(false);
    d->m_titlebar->setBlurBackground(true);

    {
        auto dpr = qApp->devicePixelRatio();
        int w2 = static_cast<int>(32 * dpr);
        int w = static_cast<int>(32 * dpr);

        QIcon icon = QIcon::fromTheme("deepin-movie");
        auto logo = icon.pixmap(QSize(32, 32))
                    .scaled(w, w, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        logo.setDevicePixelRatio(dpr);
        QPixmap pm(w2, w2);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.drawPixmap((w2 - w) / 2, (w2 - w) / 2, logo);
        p.end();
        if(!utils::check_wayland_env())
            d->m_titlebar->setIcon(pm);
    }

    d->m_titlebar->setTitle("");
    d->m_titletxt = new DLabel(this);
    d->m_titletxt->setText("");
//    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(d->m_titletxt);
//    shadowEffect->setOffset(0, 1);
//    shadowEffect->setColor(QColor(0,0,0,127));
//    shadowEffect->setBlurRadius(1);
//    d->m_titletxt->setGraphicsEffect(shadowEffect);
    d->m_titletxt->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T7));
    d->m_titlebar->addWidget(d->m_titletxt, Qt::AlignCenter);

    d->m_shadowEffect = new QGraphicsDropShadowEffect(this);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &Titlebar::slotThemeTypeChanged);   
}

Titlebar::~Titlebar()
{

}

void Titlebar::slotThemeTypeChanged()
{
    Q_D(const Titlebar);
    QPalette pa1, pa2;
    if (d->m_play)
    {
        pa1.setColor(QPalette::ButtonText, d->playColor);
        pa2.setColor(QPalette::WindowText, d->playColor);
        d->m_titlebar->setPalette(pa1);
        d->m_titletxt->setPalette(pa2);
        d->m_shadowEffect->setOffset(d->offsetX, d->offsetY);
        d->m_shadowEffect->setBlurRadius(d->offsetY);
        d->m_shadowEffect->setColor(d->darkEffectColor);
    } else {
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType) {
            pa1.setColor(QPalette::ButtonText, d->playColor);
            pa2.setColor(QPalette::WindowText, d->playColor);
            d->m_titlebar->setPalette(pa1);
            d->m_titletxt->setPalette(pa2);
            d->m_shadowEffect->setOffset(d->offsetX, d->offsetY);
            d->m_shadowEffect->setBlurRadius(d->offsetY);
            d->m_shadowEffect->setColor(d->darkEffectColor);
        } else {
            pa1.setColor(QPalette::ButtonText, QColor(98, 110, 136, 225));
            pa2.setColor(QPalette::WindowText, QColor(98, 110, 136, 225));
            d->m_titlebar->setPalette(pa1);
            d->m_titletxt->setPalette(pa2);
            d->m_shadowEffect->setOffset(d->offsetX, d->offsetY);
            d->m_shadowEffect->setBlurRadius(d->blurRadius);
            d->m_shadowEffect->setColor(d->lightEffectColor);
        }
    }
    this->setGraphicsEffect(d->m_shadowEffect);
}
/**
 * @brief titlebar 获取标题栏对象指针
 * @return titlebar指针
 */
DTitlebar *Titlebar::titlebar()
{
    Q_D(const Titlebar);
    return d->m_titlebar;
}
/**
 * @brief setTitletxt 设置标题栏文本
 * @param title 标题栏文本
 */
void Titlebar::setTitletxt(const QString &title)
{
    Q_D(const Titlebar);
    d->m_titletxt->setText(title);
}
/**
 * @brief setTitleBarBackground 设置标题栏背景是否为播放状态样式
 * @param flag 传入是否为播放状态
 */
void Titlebar::setTitleBarBackground(bool flag)
{
    Q_D(Titlebar);

    QPalette pa1, pa2;

    d->m_play = flag;

    if (d->m_play) {
        d->m_titlebar->setBackgroundTransparent(d->m_play);
        pa1.setColor(QPalette::ButtonText, d->playColor);
        pa2.setColor(QPalette::WindowText, d->playColor);
        d->m_titlebar->setPalette(pa1);
        d->m_titletxt->setPalette(pa2);
        d->m_shadowEffect->setOffset(d->offsetX, d->offsetX);
        d->m_shadowEffect->setBlurRadius(d->offsetX);
        d->m_shadowEffect->setColor(Qt::transparent);
    } else {
        QPalette palette;
        palette.setColor(QPalette::Background, QColor(200, 200, 200, 50));
        this->setPalette(palette);
        d->m_titlebar->setBackgroundTransparent(d->m_play);
        d->m_titlebar->setBlurBackground(d->m_play);
        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType) {
            pa1.setColor(QPalette::ButtonText, d->playColor);
            pa2.setColor(QPalette::WindowText, d->playColor);
            d->m_titlebar->setPalette(pa1);
            d->m_titletxt->setPalette(pa2);
            d->m_shadowEffect->setOffset(d->offsetX, d->offsetY);
            d->m_shadowEffect->setBlurRadius(d->blurRadius);
            d->m_shadowEffect->setColor(d->darkEffectColor);
        } else {
            pa1.setColor(QPalette::ButtonText, QColor(98, 110, 136, 225));
            pa2.setColor(QPalette::WindowText, QColor(98, 110, 136, 225));
            d->m_titlebar->setPalette(pa1);
            d->m_titletxt->setPalette(pa2);
            d->m_shadowEffect->setOffset(d->offsetX, d->offsetY);
            d->m_shadowEffect->setBlurRadius(d->blurRadius);
            d->m_shadowEffect->setColor(d->lightEffectColor);
        }
    }
    this->setGraphicsEffect(d->m_shadowEffect);

    update();
}
/**
 * @brief paintEvent 绘制事件函数
 * @param pPaintEvent 绘制事件
 */
void Titlebar::paintEvent(QPaintEvent *pe)
{
    Q_D(const Titlebar);

    QPainter painter(this);
    QBrush bgColor;
    if (d->m_play) {
        QPalette palette;
        QPixmap pixmap = QPixmap(":resources/icons/titlebar.png");
        //palette.setBrush(QPalette::Background, QBrush(pixmap.scaled(window()->width(), 50)));
        bgColor = QBrush(pixmap);
        palette.setColor(QPalette::Background, QColor(0, 0, 0, 0));
        this->setPalette(palette);
    } else {
        bgColor = Qt::transparent;
    }

    QPainterPath pp;
    pp.setFillRule(Qt::WindingFill);
    pp.addRect(rect());
    painter.fillPath(pp, bgColor);
}
}

