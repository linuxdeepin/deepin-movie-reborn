/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiepengfei <xiepengfei@uniontech.com>
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
/**
 * @file 此文件中实现投屏搜索小窗口
 */
#ifndef _MIRCASTWIDGET_H
#define _MIRCASTWIDGET_H

#include <DWidget>
#include <DFloatingWidget>
#include <DLabel>

#include <QTimer>
#include <QScrollArea>
#include <QIcon>

#include "dlna/cdlnasoappost.h"

DWIDGET_USE_NAMESPACE

class QNetworkReply;
class QListWidget;
class CSSDPSearch;
class DlnaContentServer;
class CDlnaSoapPost;
class QListWidgetItem;

class ItemWidget: public QWidget
{
    Q_OBJECT
public:
    enum ConnectState {
        Normal = 0,
        Loading,
        Checked,
    };

    ItemWidget(QString deviceName, const QByteArray &data, const QNetworkReply *reply, QWidget *parent = nullptr);

    void clearSelect();
    void setState(ConnectState state);
    ConnectState state();

protected:
    void mousePressEvent(QMouseEvent *pEvent) override;
    void paintEvent(QPaintEvent *pEvent) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

signals:
    void selected();
    void connecting();

private:
    QString convertDisplay();

private:
    QString     m_deviceName;
    QString     m_displayName;
    QByteArray  m_data;
    bool        m_selected;
    bool        m_hover;
    ConnectState m_state;
    QIcon  m_normalLoadIcon;
    QIcon  m_selectLoadIcon;
    double m_rotate;
    QTimer m_rotateTime;
};

class ListWidget: public QWidget
{
    Q_OBJECT
public:
    ListWidget(QWidget *parent = nullptr);

    int count();
    void clear();
    ItemWidget* createListeItem(QString deviceName, const QByteArray &data, const QNetworkReply *reply);

    int currentItemIndex();
    ItemWidget* currentItemWidget();
    QList<ItemWidget*> selectedItemWidget();
    void setItemWidgetStatus(QList<ItemWidget*> lstItem, ItemWidget::ConnectState);

signals:
    void connectDevice(ItemWidget*);

private slots:
    void slotSelectItem();
    void slotsConnectingDevice();

private:
    QList<ItemWidget*>     m_items;
    ItemWidget             *m_currentWidget;
    ItemWidget             *m_lastSelectedWidget;
};

class RefreButtonWidget: public QWidget
{
    Q_OBJECT
public:
    RefreButtonWidget(QIcon refreIcon, QIcon loadingIcon, QWidget *parent = nullptr);

    void refershTimeout();

protected:
    void paintEvent(QPaintEvent *pEvent) override;
    void mouseReleaseEvent(QMouseEvent *pEvent) override;

signals:
    void buttonClicked();

private:
    QTimer m_rotateTime;
    QIcon m_refreIcon;
    QIcon m_loadingIcon;
    bool  m_refreState;
    double m_rotate;
};

class MircastWidget: public DFloatingWidget
{
    Q_OBJECT
public:
    enum SearchState {
        Searching = 0,
        ListExhibit,
        NoDevices,
    };

    enum MircastState {
        Connecting = 0,
        Screening,
        Idel,
    };
    enum MircastPlayState {
        NoState = 0,
        Play,
        Pause,
        Stop,
    };

public:
    MircastWidget(QWidget *mainWindow = nullptr, void *pEngine = nullptr);
//    virtual ~MircastWidget() override;

    MircastState getMircastState();
    MircastPlayState getMircastPlayState();
    void playNext();
    void seekMircast(int nSec);

public slots:
    void togglePopup();
    void slotReadyRead();
    void slotExitMircast();
    void slotSeekMircast(int);
    void slotPauseDlnaTp();

private slots:
    void slotRefreshBtnClicked();
    void slotSearchTimeout();
    void slotMircastTimeout();
    void slotGetPositionInfo(DlnaPositionInfo info);
    void slotConnectDevice(ItemWidget*);
    void pauseDlnaTp();
    void playDlnaTp();
    void seekDlnaTp(int nSeek);
    void stopDlnaTP();
    void getPosInfoDlnaTp();
signals:
    void closeServer();
    void mircastState(int state, QString msg = QString());
    void updateTime(int time);
    void updatePlayStatus();

private:
    /**
     * @brief searchDevices 刷新查找设备
     */
    void searchDevices();
    /**
     * @brief updateMircastState 更新投屏窗口状态
     */
    void updateMircastState(SearchState state);

    void createListeItem(QString, const QByteArray &data, const QNetworkReply*);
    //初始化http Sever
    void initializeHttpServer(int port = 9999);

    void startDlnaTp(ItemWidget *item = nullptr);

    int timeConversion(QString);

private:
    QWidget     *m_hintWidget;
    DLabel      *m_hintLabel;
    QScrollArea *m_mircastArea;
    bool        m_bIsToggling;
//    MircastDevidesModel *m_mircastModel;
//    QListWidget *m_listWidget;
    CSSDPSearch *m_search;
    RefreButtonWidget *m_refreshBtn;
    MircastState m_mircastState;
    int          m_attempts;
    int          m_connectTimeout;

    ListWidget   *m_listWidget;
    MircastPlayState     m_nPlayStatus;

    QTimer          m_searchTime;
    QTimer          m_mircastTimeOut;
    QList<QString>  m_devicesList;
    //投屏http服务，支持http断点续传请求
    DlnaContentServer *m_dlnaContentServer;
    //投屏控制
    CDlnaSoapPost *m_pDlnaSoapPost;
    //是否成功启动http server
    bool m_isStartHttpServer;
    //投屏设备的控制url
    QString m_ControlURLPro;
    //本地准备的投屏主机地址
    QString m_URLAddrPro;
    //本地准备的投屏url地址
    QString m_sLocalUrl;
    void *m_pEngine;            ///播放引擎
    int m_nCurDuration;   //当前播放视频总时长
    int m_nCurAbsTime;    //当前播放视频播放时长
};

#endif /* ifndef _MIRCASTWIDGET_H */
