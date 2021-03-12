/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiangxiaojun <xiangxiaoju@uniontech.com>
 *
 * Maintainer: liuzheng <liuzheng@uniontech.com>
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
/**
 * @file
 * 此文件为打开url时的输入框。
 */
#include "dmr_lineedit.h"

namespace dmr {
/**
 * @brief LineEdit 构造函数
 * @param parent 父窗口
 */
LineEdit::LineEdit(QWidget *parent)
    : QLineEdit(parent)
{
    //参考设计图
    setFixedHeight(30);

    QIcon icon;
    icon.addFile(":/resources/icons/input_clear_normal.svg", QSize(), QIcon::Normal);
    icon.addFile(":/resources/icons/input_clear_press.svg", QSize(), QIcon::Selected);
    icon.addFile(":/resources/icons/input_clear_hover.svg", QSize(), QIcon::Active);
    m_pClearAct = new QAction(icon, "", this);

    connect(m_pClearAct, &QAction::triggered, this, &QLineEdit::clear);
	connect(this, &QLineEdit::textChanged, this, &LineEdit::slotTextChanged);

}
/**
 * @brief showEvent 重载显示事件
 * @param pShowEvent 显示事件
 */
void LineEdit::showEvent(QShowEvent *pShowEvent)
{
    QLineEdit::showEvent(pShowEvent);
}
/**
 * @brief resizeEvent 重载界面大小改变事件
 * @param pResizeEvent 界面大小改变事件
 */
void LineEdit::resizeEvent(QResizeEvent *pResizeEvent)
{
    QLineEdit::resizeEvent(pResizeEvent);
}
/**
 * @brief slotTextChanged 文本变化槽函数
 * @param sText 输入框内的文本
 */
void LineEdit::slotTextChanged(const QString &sText)
{
    if (sText.isEmpty()) {
        removeAction(m_pClearAct);
    } else {
        addAction(m_pClearAct, QLineEdit::TrailingPosition);
    }
}

}
