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
 * 此文件为切换播放暂停时窗口中间显示控件。
 *
 */
#ifndef ANIMATIONLABEL_H
#define ANIMATIONLABEL_H
#include <QLabel>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QBitmap>


/**
 * @brief The AnimationLabel class
 *
 * 实现在切换播放、暂停状态的时候显示的动画效果。
 */
class AnimationLabel : public QFrame
{
    Q_PROPERTY(int fps READ fps WRITE setFps)

public:
    /**
     * @brief AnimationLabel构造函数
     * @param parent 父窗口
     * @param pMainWindow 主窗口
     */
    explicit AnimationLabel(QWidget *parent = nullptr, QWidget *pMainWindow = nullptr);
    /**
     * @brief 由播放状态变为暂停状态
     */
    void pauseAnimation();
    /**
     * @brief 由暂停状态变为播放状态
     */
    void playAnimation();
    void setWM(bool);

private:
    /**
     * @brief 初始化成员变量
     * @param mainwindow 主窗口指针
     */
    void initMember(QWidget *pMainwindow);
    /**
     * @brief 初始化切换暂停时的动画组
     */
    void initPauseAnimation();
    /**
     * @brief 初始化切换播放时的动画组
     */
    void initPlayAnimation();

public slots:
    /**
     * @brief 具体实现播放动画的每一帧图像显示
     * @param 当前显示图像的序号
     */
    void onPlayAnimationChanged(const QVariant &value);
    /**
     * @brief 具体实现暂停动画的每一帧图像显示
     * @param 当前显示图像的序号
     */
    void onPauseAnimationChanged(const QVariant &value);
    /**
     * @brief 隐藏当前窗口
     * @param
     */
    void onHideAnimation();

protected:
    /**
     * @brief 重载绘制事件函数
     * @param event:qt绘制事件
     */
    void paintEvent(QPaintEvent *event);
    /**
     * @brief 重载显示事件函数
     * @param event:qt窗口显示事件
     */
    void showEvent(QShowEvent *event) override;
    /**
     * @brief 重载移动事件函数
     * @param event:qt窗口移动事件
     */
    void moveEvent(QMoveEvent *event) override;
    /**
     * @brief 重载鼠标释放事件函数
     * @param event:qt鼠标事件
     */
//    void mouseReleaseEvent(QMouseEvent *event) override;

    QSequentialAnimationGroup *m_pPlayAnimationGroup;     ///切换播放状态动画组
    QPropertyAnimation        *m_pPlayShowAnimation;      ///切换播放状态显示动画
    QPropertyAnimation        *m_pPlayHideAnimation;      ///切换播放状态隐藏动画
    QSequentialAnimationGroup *m_pPauseAnimationGroup;    ///切换暂停状态动画组
    QPropertyAnimation        *m_pPauseShowAnimation;     ///切换暂停状态显示动画
    QPropertyAnimation        *m_pPauseHideAnimation;     ///切换暂停状态隐藏动画
    QWidget                   *m_pMainWindow;             ///主窗口指针
    QPixmap                    m_pixmap;                  ///当前动画显示的图像
    QString                    m_sFileName;               ///动画当前显示的图像文件
    bool                       m_bIsWM;
};

#endif  // ANIMATIONLABEL_H
