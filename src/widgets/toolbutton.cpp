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
    qDebug() << "Initializing VolumeButton";
    setIcon(QIcon::fromTheme("dcc_volume"));
    qDebug() << "Set initial volume icon";

    setIconSize(QSize(36, 36));
    installEventFilter(this);
    m_pToolTip = new ToolTip;
    m_pToolTip->setText(tr("Volume"));

    connect(&m_showTime, &QTimer::timeout, [=]{
        qDebug() << "Tooltip timer triggered, showing volume tooltip";
        QPoint pos = this->parentWidget()->mapToGlobal(this->pos());
        pos.rx() = pos.x() + (this->width() - m_pToolTip->width()) / 2;
        pos.ry() = pos.y() - 40;

        if (nullptr != m_pToolTip) {
            m_pToolTip->move(pos);
            m_pToolTip->show();
        }
    });

    qDebug() << "Exiting VolumeButton constructor";
}

void VolumeButton::hideTip()
{
    qDebug() << "Hiding volume button tooltip";
    if (m_showTime.isActive()) {
        qDebug() << "Stopping tooltip timer";
        m_showTime.stop();
    }
    if (m_pToolTip->isVisible()) {
        qDebug() << "Hiding visible tooltip";
        m_pToolTip->hide();
    }
    qDebug() << "Exiting VolumeButton::hideTip()";
}

void VolumeButton::setVolume(int nVolume)
{
    qDebug() << "Setting volume to:" << nVolume;
    m_nVolume = nVolume;

    qDebug() << "Updating button style based on new volume";
    changeStyle();

    qDebug() << "Exiting VolumeButton::setVolume()";
}

void VolumeButton::setMute(bool bMute)
{
    qDebug() << "Setting mute state to:" << bMute;
    m_bMute = bMute;

    qDebug() << "Updating button style based on new mute state";
    changeStyle();

    qDebug() << "Exiting VolumeButton::setMute()";
}

void VolumeButton::setButtonEnable(bool bFlag)
{
    qDebug() << "Setting volume button enabled state to:" << bFlag;
    QIcon icon = QIcon::fromTheme("dcc_volumedisable");

    if (bFlag) {
        qDebug() << "Enabling volume button";
        setEnabled(true);
        changeStyle();
    } else {
        qDebug() << "Disabling volume button";
        setEnabled(false);
        setIcon(icon);
    }

    qDebug() << "Exiting VolumeButton::setButtonEnable()";
}

void VolumeButton::changeStyle()
{
    qDebug() << "Entering VolumeButton::changeStyle() - volume:" << m_nVolume << "mute:" << m_bMute;

    if (m_nVolume >= 66) {     //根据音量大小改变图标，更直观的表现
        qDebug() << "Setting high volume icon (volume >= 66%)";
        setIcon(QIcon::fromTheme("dcc_volume"));
    } else if (m_nVolume >= 33) {
        qDebug() << "Setting medium volume icon (33% <= volume < 66%)";
        setIcon(QIcon::fromTheme("dcc_volumemid"));
    } else {
        qDebug() << "Setting low volume icon (volume < 33%)";
        setIcon(QIcon::fromTheme("dcc_volumelow"));
    }

    if (m_bMute || m_nVolume == 0) {
        qDebug() << "Setting mute icon (muted or volume = 0)";
        setIcon(QIcon::fromTheme("dcc_mute"));
    }

    qDebug() << "Exiting VolumeButton::changeStyle()";
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void VolumeButton::enterEvent(QEvent *ev)
{
    qDebug() << "Entering VolumeButton::enterEvent(QEvent*)";
    
    emit entered();
    qDebug() << "Emitted entered signal";

    if (!utils::check_wayland_env() && !m_showTime.isActive()) {
        qDebug() << "Starting tooltip timer (1000ms)";
        m_showTime.start(1000);
    }

    DIconButton::enterEvent(ev);
    qDebug() << "Exiting VolumeButton::enterEvent(QEvent*)";
}
#else
void VolumeButton::enterEvent(QEnterEvent *ev)
{
    qDebug() << "Entering VolumeButton::enterEvent(QEnterEvent*)";

    emit entered();
    qDebug() << "Emitted entered signal";

    if (!utils::check_wayland_env() && !m_showTime.isActive()) {
        qDebug() << "Starting tooltip timer (1000ms)";
        m_showTime.start(1000);
    }

    DIconButton::enterEvent(ev);
    qDebug() << "Exiting VolumeButton::enterEvent(QEnterEvent*)";
}
#endif

void VolumeButton::leaveEvent(QEvent *ev)
{
    qDebug() << "Entering VolumeButton::leaveEvent()";

    emit leaved();
    qDebug() << "Emitted leaved signal";

    m_showTime.stop();
    qDebug() << "Stopped tooltip timer";

    if (!utils::check_wayland_env() && nullptr != m_pToolTip && m_pToolTip->isVisible()) {
        qDebug() << "Hiding tooltip with small delay";
        QThread::msleep(10);
        m_pToolTip->hide();
    }

    DIconButton::leaveEvent(ev);
    qDebug() << "Exiting VolumeButton::leaveEvent()";
}

void VolumeButton::wheelEvent(QWheelEvent *we)
{
    qDebug() << "Entering VolumeButton::wheelEvent()";
    //qInfo() << we->angleDelta() << we->modifiers() << we->buttons();

    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        if (we->angleDelta().y() > 0) {
            qDebug() << "Wheel scrolled up, requesting volume increase";
            emit requestVolumeUp();
        } else {
            qDebug() << "Wheel scrolled down, requesting volume decrease";
            emit requestVolumeDown();
        }
    } else {
        qDebug() << "Wheel event ignored due to modifiers or buttons";
    }

    qDebug() << "Exiting VolumeButton::wheelEvent()";
}

//tab键焦点移出时同鼠标移出，响应自动隐藏
void VolumeButton::focusOutEvent(QFocusEvent *ev)
{
    qDebug() << "Entering VolumeButton::focusOutEvent()";

    // mips和arm音量条属性为QTool,升起时焦点会在音量条上
    // 所以不能使用焦点是否变化来让音量调消失
#if !defined (__mips__) && !defined (__aarch64__)
    qDebug() << "Non-MIPS/ARM platform, emitting leaved signal on focus out";
    emit leaved();
#else
    qDebug() << "MIPS/ARM platform, not emitting leaved signal on focus out";
#endif
    DIconButton::focusOutEvent(ev);
    qDebug() << "Exiting VolumeButton::focusOutEvent()";
}

bool VolumeButton::eventFilter(QObject *obj, QEvent *e)
{
    qDebug() << "Entering VolumeButton::eventFilter()";

    QMouseEvent* pMouseEvent = dynamic_cast<QMouseEvent*>(e);
    if(!isEnabled() && pMouseEvent) {                 // 音量按钮不能使用时需要给出提示
        qDebug() << "Button disabled but received mouse event";

        if(pMouseEvent->type() == QEvent::MouseButtonPress) {
           qDebug() << "Mouse press on disabled button, emitting unsupported signal";
           emit sigUnsupported();
        }

        qDebug() << "Exiting VolumeButton::eventFilter() - returning false";
        return false;
    }

    qDebug() << "Exiting VolumeButton::eventFilter() - delegating to parent";
    return QObject::eventFilter(obj, e);
}

}
