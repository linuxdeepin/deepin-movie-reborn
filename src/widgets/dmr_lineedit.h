// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

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
/**
 * @file
 * 此文件为打开url时的输入框。
 */
#pragma once

#include <QtWidgets>
#include <dlineedit.h>
#include <dimagebutton.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
/**
 * @brief The LineEdit class
 * 这个类是播放网络视频输入url的输入栏
 */
class LineEdit: public QLineEdit {
public:
    /**
     * @brief LineEdit 构造函数
     * @param parent 父窗口
     */
    explicit LineEdit(QWidget* parent = 0);

protected:
    /**
     * @brief showEvent 重载显示事件
     * @param pShowEvent 显示事件
     */
    void showEvent(QShowEvent* pShowEvent) override;
    /**
     * @brief resizeEvent 重载界面大小改变事件
     * @param pResizeEvent 界面大小改变事件
     */
    void resizeEvent(QResizeEvent* pResizeEvent) override;
public slots:
    /**
     * @brief slotTextChanged 文本变化槽函数
     * @param sText 输入框内的文本
     */
    void slotTextChanged(const QString & sText);  //把lambda表达式改为槽函数，modify by myk
private:
    QAction *m_pClearAct;    ///输入框清空按键
};
}

