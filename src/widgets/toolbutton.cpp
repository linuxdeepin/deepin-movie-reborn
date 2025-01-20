// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "toolbutton.h"
#include "utils.h"
#include <dthememanager.h>
#include <DApplication>

namespace dmr {
VolumeButton::VolumeButton(QWidget *parent)
    : DIconButton(parent), m_nVolume(100), m_bMute(false)
{
    setIcon(QIcon::fromTheme("dcc_volume"));
    setIconSize(QSize(36, 36));
    installEventFilter(this);
    m_pToolTip = new ToolTip;
    m_pToolTip->setText(tr("Volume"));

    connect(&m_showTime, &QTimer::timeout, [=]{
        QPoint pos = this->parentWidget()->mapToGlobal(this->pos());
        pos.rx() = pos.x() + (this->width() - m_pToolTip->width()) / 2;
        pos.ry() = pos.y() - 40;

        if (nullptr != m_pToolTip) {
            m_pToolTip->move(pos);
            m_pToolTip->show();
        }
    });
}

void VolumeButton::hideTip()
{
    if (m_showTime.isActive())
        m_showTime.stop();
    if (m_pToolTip->isVisible())
        m_pToolTip->hide();
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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void VolumeButton::enterEvent(QEvent *ev)
{
    emit entered();
    if (!utils::check_wayland_env() && !m_showTime.isActive())
        m_showTime.start(1000);

    DIconButton::enterEvent(ev);
}
#else
void VolumeButton::enterEvent(QEnterEvent *ev)
{
    emit entered();
    if (!utils::check_wayland_env() && !m_showTime.isActive())
        m_showTime.start(1000);

    DIconButton::enterEvent(ev);
}
#endif

void VolumeButton::leaveEvent(QEvent *ev)
{
    emit leaved();
    m_showTime.stop();
    if (!utils::check_wayland_env() && nullptr != m_pToolTip && m_pToolTip->isVisible()) {
        QThread::msleep(10);
        m_pToolTip->hide();
    }

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
