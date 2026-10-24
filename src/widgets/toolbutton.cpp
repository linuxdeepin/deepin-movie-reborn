// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "toolbutton.h"
#include "ddlog.h"
#include "utils.h"
#include <dthememanager.h>
#include <DApplication>

namespace dmr {
VolumeButton::VolumeButton(QWidget *parent)
    : DIconButton(parent), m_nVolume(100), m_bMute(false)
{
    qCDebug(appMovie) << "Initializing VolumeButton";
    setIcon(QIcon::fromTheme("dcc_volume"));
    qCDebug(appMovie) << "Set initial volume icon";

    setIconSize(QSize(36, 36));
    installEventFilter(this);
    m_pToolTip = new ToolTip;
    m_pToolTip->setText(tr("Volume"));

    connect(&m_showTime, &QTimer::timeout, [=]{
        qCDebug(appMovie) << "Tooltip timer triggered, showing volume tooltip";
        QPoint pos = this->parentWidget()->mapToGlobal(this->pos());
        pos.rx() = pos.x() + (this->width() - m_pToolTip->width()) / 2;
        pos.ry() = pos.y() - 40;

        if (nullptr != m_pToolTip) {
            m_pToolTip->move(pos);
            m_pToolTip->show();
        }
    });

    qCDebug(appMovie) << "Exiting VolumeButton constructor";
}

void VolumeButton::hideTip()
{
    qCDebug(appMovie) << "Hiding volume button tooltip";
    if (m_showTime.isActive()) {
        qCDebug(appMovie) << "Stopping tooltip timer";
        m_showTime.stop();
    }
    if (m_pToolTip->isVisible()) {
        qCDebug(appMovie) << "Hiding visible tooltip";
        m_pToolTip->hide();
    }
    qCDebug(appMovie) << "Exiting VolumeButton::hideTip()";
}

void VolumeButton::setVolume(int nVolume)
{
    qCDebug(appMovie) << "Setting volume to:" << nVolume;
    m_nVolume = nVolume;

    qCDebug(appMovie) << "Updating button style based on new volume";
    changeStyle();

    qCDebug(appMovie) << "Exiting VolumeButton::setVolume()";
}

void VolumeButton::setMute(bool bMute)
{
    qCDebug(appMovie) << "Setting mute state to:" << bMute;
    m_bMute = bMute;

    qCDebug(appMovie) << "Updating button style based on new mute state";
    changeStyle();

    qCDebug(appMovie) << "Exiting VolumeButton::setMute()";
}

void VolumeButton::setButtonEnable(bool bFlag)
{
    qCDebug(appMovie) << "Setting volume button enabled state to:" << bFlag;
    QIcon icon = QIcon::fromTheme("dcc_volumedisable");

    if (bFlag) {
        qCDebug(appMovie) << "Enabling volume button";
        setEnabled(true);
        changeStyle();
    } else {
        qCDebug(appMovie) << "Disabling volume button";
        setEnabled(false);
        setIcon(icon);
    }

    qCDebug(appMovie) << "Exiting VolumeButton::setButtonEnable()";
}

void VolumeButton::changeStyle()
{
    qCDebug(appMovie) << "Entering VolumeButton::changeStyle() - volume:" << m_nVolume << "mute:" << m_bMute;

    if (m_nVolume >= 66) {     //根据音量大小改变图标，更直观的表现
        qCDebug(appMovie) << "Setting high volume icon (volume >= 66%)";
        setIcon(QIcon::fromTheme("dcc_volume"));
    } else if (m_nVolume >= 33) {
        qCDebug(appMovie) << "Setting medium volume icon (33% <= volume < 66%)";
        setIcon(QIcon::fromTheme("dcc_volumemid"));
    } else {
        qCDebug(appMovie) << "Setting low volume icon (volume < 33%)";
        setIcon(QIcon::fromTheme("dcc_volumelow"));
    }

    if (m_bMute || m_nVolume == 0) {
        qCDebug(appMovie) << "Setting mute icon (muted or volume = 0)";
        setIcon(QIcon::fromTheme("dcc_mute"));
    }

    qCDebug(appMovie) << "Exiting VolumeButton::changeStyle()";
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void VolumeButton::enterEvent(QEvent *ev)
{
    qCDebug(appMovie) << "Entering VolumeButton::enterEvent(QEvent*)";
    
    emit entered();
    qCDebug(appMovie) << "Emitted entered signal";

    if (!utils::check_wayland_env() && !m_showTime.isActive()) {
        qCDebug(appMovie) << "Starting tooltip timer (1000ms)";
        m_showTime.start(1000);
    }

    DIconButton::enterEvent(ev);
    qCDebug(appMovie) << "Exiting VolumeButton::enterEvent(QEvent*)";
}
#else
void VolumeButton::enterEvent(QEnterEvent *ev)
{
    qCDebug(appMovie) << "Entering VolumeButton::enterEvent(QEnterEvent*)";

    emit entered();
    qCDebug(appMovie) << "Emitted entered signal";

    if (!utils::check_wayland_env() && !m_showTime.isActive()) {
        qCDebug(appMovie) << "Starting tooltip timer (1000ms)";
        m_showTime.start(1000);
    }

    DIconButton::enterEvent(ev);
    qCDebug(appMovie) << "Exiting VolumeButton::enterEvent(QEnterEvent*)";
}
#endif

void VolumeButton::leaveEvent(QEvent *ev)
{
    qCDebug(appMovie) << "Entering VolumeButton::leaveEvent()";

    emit leaved();
    qCDebug(appMovie) << "Emitted leaved signal";

    m_showTime.stop();
    qCDebug(appMovie) << "Stopped tooltip timer";

    if (!utils::check_wayland_env() && nullptr != m_pToolTip && m_pToolTip->isVisible()) {
        qCDebug(appMovie) << "Hiding tooltip with small delay";
        QThread::msleep(10);
        m_pToolTip->hide();
    }

    DIconButton::leaveEvent(ev);
    qCDebug(appMovie) << "Exiting VolumeButton::leaveEvent()";
}

void VolumeButton::wheelEvent(QWheelEvent *we)
{
    qCDebug(appMovie) << "Entering VolumeButton::wheelEvent()";
    //qCInfo(appMovie) << we->angleDelta() << we->modifiers() << we->buttons();

    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        if (we->angleDelta().y() > 0) {
            qCDebug(appMovie) << "Wheel scrolled up, requesting volume increase";
            emit requestVolumeUp();
        } else {
            qCDebug(appMovie) << "Wheel scrolled down, requesting volume decrease";
            emit requestVolumeDown();
        }
    } else {
        qCDebug(appMovie) << "Wheel event ignored due to modifiers or buttons";
    }

    qCDebug(appMovie) << "Exiting VolumeButton::wheelEvent()";
}

//tab键焦点移出时同鼠标移出，响应自动隐藏
void VolumeButton::focusOutEvent(QFocusEvent *ev)
{
    qCDebug(appMovie) << "Entering VolumeButton::focusOutEvent()";

    // mips和arm音量条属性为QTool,升起时焦点会在音量条上
    // 所以不能使用焦点是否变化来让音量调消失
#if !defined (__mips__) && !defined (__aarch64__)
    qCDebug(appMovie) << "Non-MIPS/ARM platform, emitting leaved signal on focus out";
    emit leaved();
#else
    qCDebug(appMovie) << "MIPS/ARM platform, not emitting leaved signal on focus out";
#endif
    DIconButton::focusOutEvent(ev);
    qCDebug(appMovie) << "Exiting VolumeButton::focusOutEvent()";
}

bool VolumeButton::eventFilter(QObject *obj, QEvent *e)
{
    qCDebug(appMovie) << "Entering VolumeButton::eventFilter()";

    QMouseEvent* pMouseEvent = dynamic_cast<QMouseEvent*>(e);
    if(!isEnabled() && pMouseEvent) {                 // 音量按钮不能使用时需要给出提示
        qCDebug(appMovie) << "Button disabled but received mouse event";

        if(pMouseEvent->type() == QEvent::MouseButtonPress) {
           qCDebug(appMovie) << "Mouse press on disabled button, emitting unsupported signal";
           emit sigUnsupported();
        }

        qCDebug(appMovie) << "Exiting VolumeButton::eventFilter() - returning false";
        return false;
    }

    qCDebug(appMovie) << "Exiting VolumeButton::eventFilter() - delegating to parent";
    return QObject::eventFilter(obj, e);
}

}
