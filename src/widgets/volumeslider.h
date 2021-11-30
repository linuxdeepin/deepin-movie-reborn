/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
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
#ifndef VOLUMESLIDER_H
#define VOLUMESLIDER_H

#include <DSlider>
#include <DToolButton>
#include <QPoint>
#include <DArrowRectangle>
#include <DUtil>

#include "utils.h"
#include "volumemonitoring.h"
#include "threadpool.h"
#include "mainwindow.h"
#include "compositing_manager.h"
#include "dmr_settings.h"
#include "dbus_adpator.h"
#include "../accessibility/ac-deepin-movie-define.h"

DWIDGET_USE_NAMESPACE

namespace dmr {

class VolumeSlider: public DArrowRectangle
{
    Q_OBJECT

public:
    enum State {
        Open,
        Close
    };

signals:
    void sigVolumeChanged(int nValue);
    void sigMuteStateChanged(bool bMute);
    /**
      * @brief 功能不支持信号
      */
    void sigUnsupported();

public:
    VolumeSlider(MainWindow *mw, QWidget *parent);
    ~VolumeSlider();

    State state() const {return m_state;}
    void initVolume();   //初始化音量
    void stopTimer();
    void popup();        //弹起音量条
    void updatePoint(QPoint point);
    bool getsliderstate();
    int getVolume();   //获取当前实际音量
    void changeVolume(int nVolume);    //改变控件音量
    void calculationStep(int iAngleDelta);   //计算滚轮滚动的步进并判断步进值是否大于等于120;普通鼠标的滚轮精度为120转动一刻为120*1/8=15度

public slots:
    void volumeUp();                   //滚轮加音量
    void volumeDown();                 //滚轮减音量
    void changeMuteState(bool bMute);  //改变静音状态
    void volumeChanged(int nVolume);   //控件音量变化后的后续处理
    void muteButtnClicked();
    void setThemeType(int type);
    void delayedHide();

protected:
    void enterEvent(QEvent *e);
    void showEvent(QShowEvent *se);
    void leaveEvent(QEvent *e);
    void paintEvent(QPaintEvent *);
    void keyPressEvent(QKeyEvent *pEvent);
    bool eventFilter(QObject *obj, QEvent *e);
    void refreshIcon();                //刷新图标

    bool event(QEvent* pEvent) override;

private:
    QString readSinkInputPath();       //获取dbus接口的地址
//    cppckeck修改
//    void setAudioVolume(int volume);   //回设dock栏应用音量
    void setMute(bool muted);          //回设dock栏应用静音状态

private:
    DToolButton *m_pBtnChangeMute {nullptr};
    DLabel *m_pLabShowVolume {nullptr};
    DSlider *m_slider;
    MainWindow *_mw;
    QTimer m_autoHideTimer;
    bool m_bIsMute {false};
    bool m_bFinished {false};
    QPropertyAnimation *pVolAnimation {nullptr};
    State m_state {Close};
    QPoint m_point {0, 0};
   // QPixmap m_bgImage;
    bool m_mouseIn {false};
    int m_nVolume;                      //记录实际音量(实际音量最大值为200,显示最大到100)
    VolumeMonitoring volumeMonitoring;  //监听dock栏应用音量变化
    bool m_bHideWhenFinished;           ///等待动画结束后隐藏

    int m_iStep;                 //鼠标灵敏度的步进
    bool m_bIsWheel;             //是否是通过滚轮调节音量
    bool m_bPress;               ///鼠标按下标志
};
}


#endif // VOLUMESLIDER_H
