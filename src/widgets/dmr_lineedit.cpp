// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file
 * 此文件为打开url时的输入框。
 */
#include "dmr_lineedit.h"
#include "ddlog.h"
#include <QDebug>

namespace dmr {
/**
 * @brief LineEdit 构造函数
 * @param parent 父窗口
 */
LineEdit::LineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    qCDebug(appMovie) << "Entering LineEdit constructor.";
    //参考设计图,dtk默认最大36
    setMaximumHeight(36);
    qCDebug(appMovie) << "Maximum height set to 36.";

    QIcon icon;
    icon.addFile(":/resources/icons/input_clear_normal.svg", QSize(), QIcon::Normal);
    icon.addFile(":/resources/icons/input_clear_press.svg", QSize(), QIcon::Selected);
    icon.addFile(":/resources/icons/input_clear_hover.svg", QSize(), QIcon::Active);
    m_pClearAct = new QAction(icon, "", this);
    qCDebug(appMovie) << "Clear action initialized with icons.";

    connect(m_pClearAct, &QAction::triggered, this, &QLineEdit::clear);
	connect(this, &QLineEdit::textChanged, this, &LineEdit::slotTextChanged);
    qCDebug(appMovie) << "Signals connected for clear action and text changed.";
    qCDebug(appMovie) << "Exiting LineEdit constructor.";
}
/**
 * @brief showEvent 重载显示事件
 * @param pShowEvent 显示事件
 */
void LineEdit::showEvent(QShowEvent *pShowEvent)
{
    qCDebug(appMovie) << "Entering LineEdit::showEvent() with event:" << pShowEvent;
    QLineEdit::showEvent(pShowEvent);
    qCDebug(appMovie) << "Exiting LineEdit::showEvent().";
}
/**
 * @brief resizeEvent 重载界面大小改变事件
 * @param pResizeEvent 界面大小改变事件
 */
void LineEdit::resizeEvent(QResizeEvent *pResizeEvent)
{
    qCDebug(appMovie) << "Entering LineEdit::resizeEvent() with event:" << pResizeEvent;
    QLineEdit::resizeEvent(pResizeEvent);
    qCDebug(appMovie) << "Exiting LineEdit::resizeEvent().";
}
/**
 * @brief slotTextChanged 文本变化槽函数
 * @param sText 输入框内的文本
 */
void LineEdit::slotTextChanged(const QString &sText)
{
    qCDebug(appMovie) << "Entering LineEdit::slotTextChanged() with text:" << sText;
    if (sText.isEmpty()) {
        setClearButtonEnabled(false);
        qCDebug(appMovie) << "Text is empty, clear button disabled.";
    } else {
        setClearButtonEnabled(true);
        qCDebug(appMovie) << "Text is not empty, clear button enabled.";
    }
    qCDebug(appMovie) << "Exiting LineEdit::slotTextChanged().";
}

}
