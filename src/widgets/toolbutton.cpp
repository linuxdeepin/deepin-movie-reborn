/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     mouyuankai <mouyuankai@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#include "toolbutton.h"
#include <dthememanager.h>
#include <DApplication>

namespace dmr {
VolumeButton::VolumeButton(QWidget *parent)
    : DIconButton(parent), m_nVolume(100), m_bMute(false)
{
    setIcon(QIcon::fromTheme("dcc_volume"));
    setIconSize(QSize(36, 36));
    installEventFilter(this);
}

void VolumeButton::setVolume(int nVolume)
{
    m_nVolume = nVolume;

    changeStyle();
}

void VolumeButton::setMute(bool bMute)
{
    m_bMute = bMute;

    changeStyle();
}

void VolumeButton::setButtonEnable(bool bFlag)
{
    QIcon icon = QIcon::fromTheme("dcc_volumedisable");
    //投屏时音量按钮不可使用
    QVariant pro = property("mircast");
    if(!pro.isNull()) {
        setEnabled(pro.toInt() != 0);
        return;
    }
    if (bFlag) {
        setEnabled(true);
        changeStyle();
    } else {
        setEnabled(false);
        setIcon(icon);
    }
}

void VolumeButton::changeStyle()
{
    if (m_nVolume >= 66)      //根据音量大小改变图标，更直观的表现
        setIcon(QIcon::fromTheme("dcc_volume"));
    else if (m_nVolume >= 33)
        setIcon(QIcon::fromTheme("dcc_volumemid"));
    else
        setIcon(QIcon::fromTheme("dcc_volumelow"));

    if (m_bMute || m_nVolume == 0)
        setIcon(QIcon::fromTheme("dcc_mute"));
}

void VolumeButton::enterEvent(QEvent *ev)
{
    emit entered();

    DIconButton::enterEvent(ev);
}

void VolumeButton::leaveEvent(QEvent *ev)
{
    emit leaved();

    DIconButton::leaveEvent(ev);
}

void VolumeButton::wheelEvent(QWheelEvent *we)
{
    //qInfo() << we->angleDelta() << we->modifiers() << we->buttons();
    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        if (we->angleDelta().y() > 0) {
            emit requestVolumeUp();
        } else {
            emit requestVolumeDown();
        }
    }
}

//tab键焦点移出时同鼠标移出，响应自动隐藏
void VolumeButton::focusOutEvent(QFocusEvent *ev)
{
    // mips和arm音量条属性为QTool,升起时焦点会在音量条上
    // 所以不能使用焦点是否变化来让音量调消失
#if !defined (__mips__) && !defined (__aarch64__)
    emit leaved();
#endif
    DIconButton::focusOutEvent(ev);
}

bool VolumeButton::eventFilter(QObject *obj, QEvent *e)
{
    QMouseEvent* pMouseEvent = dynamic_cast<QMouseEvent*>(e);
    if(!isEnabled() && pMouseEvent)                  // 音量按钮不能使用时需要给出提示
    {
        if(pMouseEvent->type() == QEvent::MouseButtonPress) {
           emit sigUnsupported();
        }
        return false;
    }

    return QObject::eventFilter(obj, e);
}

}
