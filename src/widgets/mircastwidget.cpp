// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mircastwidget.h"
#include "player_engine.h"
#include "dlna/cssdpsearch.h"
#include "dlna/getdlnaxmlvalue.h"
#include "dlna/dlnacontentserver.h"
#include "compositing_manager.h"

#include "../accessibility/ac-deepin-movie-define.h"

#include <QVBoxLayout>
#include <QListWidget>
#include <QAbstractListModel>
#include <QNetworkReply>
#include <QFileInfo>
#include <QNetworkInterface>

#define MIRCASTWIDTH 240
#define MIRCASTHEIGHT 188
#define REFRESHTIME 20000
#define MAXMIRCAST 5
#define MIRCASTTIMEOUT 1000
#define ROTATE_VALUE 14.4
#define TEXT_WIDTH 170

using namespace dmr;

MircastWidget::MircastWidget(QWidget *mainWindow, void *pEngine)
: DFloatingWidget(mainWindow), m_pEngine(pEngine)
{
    qDebug() << "Initializing MircastWidget";
    setAttribute(Qt::WA_NoMousePropagation, true);//鼠标事件不进入父窗口
    if(!CompositingManager::get().composited()) {
        qDebug() << "Compositing not enabled, setting native window attribute";
        setAttribute(Qt::WA_NativeWindow);
    }
    qRegisterMetaType<DlnaPositionInfo>("DlnaPositionInfo");
    setFixedSize(MIRCASTWIDTH + 14, MIRCASTHEIGHT + 12);
    setFramRadius(8);
    m_bIsToggling = false;
    m_mircastState = Idel;
    m_nPlayStatus = MircastWidget::NoState;
    m_attempts = 0;
    m_connectTimeout = 0;
    m_nCurDuration = -1;
    m_nCurAbsTime = -1;
    m_ControlURLPro = "";
    m_URLAddrPro = "";
    m_sLocalUrl = "";

    m_searchTime.setSingleShot(true);
    connect(&m_searchTime, &QTimer::timeout, this, &MircastWidget::slotSearchTimeout);
    connect(&m_mircastTimeOut, &QTimer::timeout, this, &MircastWidget::slotMircastTimeout);
    qDebug() << "Timers initialized and connected";

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(1, 0, 0, 0);
    mainLayout->setSpacing(0);
    setLayout(mainLayout);
    setContentsMargins(0, 0, 0, 0);

    QWidget *topWdiget = new QWidget(this);
    topWdiget->setFixedHeight(40);
    mainLayout->addWidget(topWdiget);

    QHBoxLayout *topLayout = new QHBoxLayout;
    topLayout->setContentsMargins(20, 8, 20, 0);
    topWdiget->setLayout(topLayout);

    DLabel *projet = new DLabel(topWdiget);
    projet->setText(tr("Project to"));
    topLayout->addWidget(projet);

    m_refreshBtn = new RefreButtonWidget(topWdiget);
    topLayout->addWidget(m_refreshBtn);
    connect(m_refreshBtn, &RefreButtonWidget::buttonClicked, this, &MircastWidget::slotRefreshBtnClicked);
    qDebug() << "Top widget and refresh button initialized";

    QFrame *spliter = new QFrame(this);
    spliter->setAutoFillBackground(true);
    spliter->setPalette(QPalette(QColor(0, 0, 0, 13)));
    spliter->setFixedSize(MIRCASTWIDTH, 2);
    mainLayout->addWidget(spliter);

    m_hintWidget = new QWidget(this);
    mainLayout->addWidget(m_hintWidget);
    m_hintWidget->setFixedSize(MIRCASTWIDTH, MIRCASTHEIGHT - 42);
    m_hintWidget->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *hintLayout = new QVBoxLayout(m_hintWidget);
    hintLayout->setContentsMargins(0, 0, 0, 0);
    hintLayout->setSpacing(0);
    hintLayout->setAlignment(Qt::AlignCenter);
    m_hintWidget->setLayout(hintLayout);
    m_hintWidget->hide();

    m_hintLabel = new DLabel(this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setFixedSize(MIRCASTWIDTH, MIRCASTHEIGHT - 42);
    m_hintLabel->setContentsMargins(20, 0, 20, 0);
    m_hintLabel->setWordWrap(true);
    hintLayout->addWidget(m_hintLabel);
    m_hintLabel->show();
    qDebug() << "Hint widget and label initialized";

    m_listWidget = new ListWidget;
    m_mircastArea = new QScrollArea;
    m_mircastArea->setFixedSize(MIRCASTWIDTH, 34 * 4);
    m_mircastArea->setFrameShape(QFrame::NoFrame);
    m_mircastArea->setAttribute(Qt::WA_TranslucentBackground, true);
    m_mircastArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_mircastArea->setWidget(m_listWidget);
    mainLayout->addWidget(m_mircastArea);
    m_mircastArea->hide();
    connect(m_listWidget, &ListWidget::connectDevice, this, &MircastWidget::slotConnectDevice);

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        m_refreshBtn->setFixedSize(16, 16);
        topWdiget->setFixedHeight(26);
        topLayout->setContentsMargins(20, 4, 20, 0);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            m_refreshBtn->setFixedSize(24, 24);
        } else {
            m_refreshBtn->setFixedSize(16, 16);
        }
    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, topWdiget, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            topWdiget->setFixedHeight(40);
            topWdiget->layout()->setContentsMargins(20, 8, 20, 0);
        } else {
            topWdiget->setFixedHeight(26);
            topWdiget->layout()->setContentsMargins(20, 4, 20, 0);
        }
    });
#endif

    mainLayout->addStretch();
    m_dlnaContentServer = nullptr;
    qDebug() << "MircastWidget initialization completed";
}
/**
 * @brief getMircastState 获取投屏连接状态
 */
MircastWidget::MircastState MircastWidget::getMircastState()
{
    return m_mircastState;
}
/**
 * @brief getMircastPlayState 获取投屏播放状态
 */
MircastWidget::MircastPlayState MircastWidget::getMircastPlayState()
{
    return m_nPlayStatus;
}
/**
 * @brief playNext 开始投屏
 */
void MircastWidget::playNext()
{
    qDebug() << "Playing next - Current state:" << m_mircastState;
    if (m_mircastState != MircastState::Idel) {
        m_mircastTimeOut.stop();
        m_attempts = 0;
        m_connectTimeout = 0;
        PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
        engine->pauseResume();
        engine->seekAbsolute(0);
        startDlnaTp();
        qDebug() << "Started DLNA transport";
    }
}
/**
 * @brief seekMircast 投屏seek
 * @param nSec seek时间 单位秒
 */
void MircastWidget::seekMircast(int nSec)
{
    qDebug() << "Seeking Miracast - Seconds:" << nSec << "Current state:" << m_mircastState;
    if(m_mircastState != MircastWidget::Screening) return;
    int nSeek = m_nCurAbsTime + nSec;
    if(nSeek < 0) {
        slotSeekMircast(0);
    } else if(nSeek > m_nCurDuration) {
        slotSeekMircast(m_nCurDuration);
    } else {
        slotSeekMircast(nSeek);
    }
}
/**
 * @brief togglePopup 工具栏投屏窗口显示与隐藏
 */
void MircastWidget::togglePopup()
{
    qDebug() << "Toggling popup - Current visibility:" << isVisible() << "Is toggling:" << m_bIsToggling;
    if (m_bIsToggling) return;
    if (isVisible()) {
        hide();
    } else {
        m_bIsToggling = true;
        show();
        raise();
        m_refreshBtn->refershStart();
        m_bIsToggling = false;
    }
}
/**
 * @brief slotRefreshBtnClicked 投屏窗口刷新按钮
 */
void MircastWidget::slotRefreshBtnClicked()
{
    qDebug() << "Refresh button clicked";
    initializeHttpServer();
    searchDevices();
    update();
}
/**
 * @brief slotSearchTimeout 投屏设备搜索超时
 */
void MircastWidget::slotSearchTimeout()
{
    qInfo() << "Search timeout - Device list empty:" << m_devicesList.isEmpty();
    if (m_devicesList.isEmpty())
        updateMircastState(SearchState::NoDevices);
    else
        updateMircastState(SearchState::ListExhibit);

    m_refreshBtn->refershTimeout();
    update();
}
/**
 * @brief slotMircastTimeout 投屏连接超时
 */
void MircastWidget::slotMircastTimeout()
{
    qDebug() << "Miracast timeout - Current timeout count:" << m_connectTimeout;
    m_pDlnaSoapPost->SoapOperPost(DLNA_GetPositionInfo, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    m_connectTimeout++;
    if (m_connectTimeout >= MAXMIRCAST) {
        m_mircastTimeOut.stop();
        if (m_mircastState == MircastState::Screening)
            emit mircastState(MIRCAST_DISCONNECTIONED);
        else
            emit mircastState(MIRCAST_CONNECTION_FAILED);
        qWarning() << "Miracast connection failed after" << MAXMIRCAST << "attempts";
    }
}
/**
 * @brief slotGetPositionInfo 获取投屏播放视频信息
 */
void MircastWidget::slotGetPositionInfo(DlnaPositionInfo info)
{
    qDebug() << "Getting position info - Current state:" << m_mircastState;
    if (m_mircastState == MircastState::Idel)
        return;
    //TODO:测试电视退出投屏后是否会有返回
    m_connectTimeout = 0;
    PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
    if (engine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "Engine is idle, exiting Miracast";
        emit mircastState(MIRCAST_EXIT);
        slotExitMircast();
        return;
    }
    PlaylistModel *model = engine->getplaylist();
    PlaylistModel::PlayMode playMode = model->playMode();

    if (m_mircastState == MircastState::Screening) {
        int absTime = timeConversion(info.sAbsTime);
        int duration = timeConversion(info.sTrackDuration);
        m_sTrackURI = info.sTrackURI;
        updateTime(absTime);
        qDebug() << "Screening state - Abs time:" << absTime << "Duration:" << duration;
        if (info.sAbsTime == info.sTrackDuration ||
                (info.sAbsTime.toUpper() == "NOT_IMPLEMENTED" && duration != 0)) {
            if (playMode == PlaylistModel::SinglePlay ||
                    (playMode == PlaylistModel::OrderPlay && model->current() == (model->count() - 1))) {
                qDebug() << "Playback completed, exiting Miracast";
                emit mircastState(MIRCAST_EXIT);
                slotExitMircast();
            } else if (playMode == PlaylistModel::SingleLoop) {
                qDebug() << "Single loop mode, restarting DLNA transport";
                startDlnaTp();
            } else {
                qDebug() << "Playing next item";
                model->playNext(true);
                m_mircastState = Connecting;
            }
            m_attempts = 0;
        } else if (info.sAbsTime.toUpper() == "NOT_IMPLEMENTED" && duration == 0) {
            qDebug() << "Invalid duration, exiting Miracast";
            emit mircastState(MIRCAST_EXIT);
            slotExitMircast();
        } else if (m_sTrackURI != m_sLocalUrl) {
            qDebug() << "Track URI mismatch, exiting Miracast";
            emit mircastState(MIRCAST_EXIT);
            slotExitMircast();
        }
        m_nCurAbsTime = absTime;
        m_nCurDuration = timeConversion(info.sTrackDuration);
        return;
    }
    int duration = timeConversion(info.sTrackDuration);
    int absTime = timeConversion(info.sAbsTime);
    qDebug() << "Connection state - Duration:" << duration << "Abs time:" << absTime;
    if (duration > 0 && absTime > 0) {
        m_mircastState = MircastState::Screening;
        if (m_connectDevice.deviceState == Connecting) {
            m_connectDevice.deviceState = MircastState::Screening;
            emit mircastState(MIRCAST_SUCCESSED, m_connectDevice.miracastDevice.name);
            qDebug() << "Miracast connection successful";
        }
        ItemWidget *item = m_listWidget->currentItemWidget();
        if(item)
            item->setState(ItemWidget::Checked);
        m_attempts = 0;
    } else {
        if (duration > 0 && m_connectDevice.deviceState == Connecting) {
            emit mircastState(MIRCAST_SUCCESSED, m_connectDevice.miracastDevice.name);
        }
        qWarning() << "Miracast connection failed - Attempts:" << m_attempts;
        if (m_attempts >= MAXMIRCAST * 10) {
            qWarning() << "Maximum attempts reached, trying next item";
            m_attempts = 0;
            m_mircastTimeOut.stop();
            if (playMode == PlaylistModel::SinglePlay ||
                    (playMode == PlaylistModel::OrderPlay && model->current() == (model->count() - 1))) {
                emit mircastState(MIRCAST_EXIT);
                slotExitMircast();
            } else if (playMode == PlaylistModel::SingleLoop) {
                startDlnaTp();
            } else {
                emit mircastState(MIRCAST_CONNECTION_FAILED);
                model->playNext(true);
                m_attempts = 0;
                m_mircastState = Connecting;
            }
        } else {
            qInfo() << "miracast failed! curret attempts:" << m_attempts << "Max:" << MAXMIRCAST * 10;
            m_attempts++;
            m_mircastState = Connecting;
        }
    }
}
/**
 * @brief slotConnectDevice 连接投屏设备
 */
void MircastWidget::slotConnectDevice(ItemWidget *item)
{
    qDebug() << "Connecting to device:" << item->getDevice().name;
    QString newURLAddrPro = item->property(urlAddrPro).toString();
    if (newURLAddrPro == m_URLAddrPro && m_mircastState == MircastState::Screening)
        return;
    PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
    if (engine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "Engine is idle, cannot connect";
        return;
    }
    m_connectDevice.miracastDevice = item->getDevice();
    m_connectDevice.deviceState = MircastState::Connecting;
    m_mircastState = Connecting;
    item->setState(ItemWidget::Loading);
    stopDlnaTP();
    startDlnaTp(item);
}
/**
 * @brief searchDevices 刷新查找设备
 */
void MircastWidget::searchDevices()
{
    qInfo() << "Starting device search";
    m_devicesList.clear();
    m_listWidget->clear();
    m_searchTime.start(REFRESHTIME);
    m_search->SsdpSearch();
    updateMircastState(SearchState::Searching);
}
/**
 * @brief updateMircastState 更新投屏窗口状态
 */
void MircastWidget::updateMircastState(MircastWidget::SearchState state)
{
    qDebug() << "Updating Miracast state:" << state;
    switch (state) {
    case Searching:
        if (!m_devicesList.isEmpty())
            return;
        m_hintLabel->setText(tr("Searching for devices..."));
        m_mircastArea->hide();
        m_hintWidget->show();
        break;
    case ListExhibit:
        m_hintWidget->hide();
        m_mircastArea->show();
        break;
    case NoDevices:
        m_hintLabel->setText(tr("No Miracast display devices were found. Please connect the device and your computer to the same WLAN network."));
        m_mircastArea->hide();
        break;
    }
}
/**
 * @brief createListeItem 投屏seek
 * @param data 投屏设备信息
 */
ItemWidget * MircastWidget::createListeItem(MiracastDevice device, const QByteArray &data, const QNetworkReply *reply)
{
    ItemWidget *item = m_listWidget->createListeItem(device, data, reply);
    QString itemAdd = item->property(urlAddrPro).toString();
    if (itemAdd == m_URLAddrPro && m_mircastState == MircastState::Screening)
        item->setState(ItemWidget::Checked);
    return item;
}
/**
 * @brief slotReadyRead 读取投屏设备信息
 */
void MircastWidget::slotReadyRead()
{
    QNetworkReply *reply = (QNetworkReply *)sender();
    if(!reply) return;
    if(reply->error() != QNetworkReply::NoError) {
        qInfo() << "Error:" << QString::number(reply->error());
        return;
    }
    QByteArray data = reply->readAll().replace("\r\n", "").replace("\\", "");
    qInfo() << "xml data:" << data;
    GetDlnaXmlValue dlnaxml(data);
    QString sName = dlnaxml.getValueByPath("device/friendlyName");
    QString uuid = dlnaxml.getValueByPath("device/UDN");
    QStringList uuidList = uuid.split(":");
    MiracastDevice device;
    device.name = sName;
    device.uuid = uuidList.last();
    foreach (MiracastDevice mirDevice, m_devicesList) {
        if (device.uuid == mirDevice.uuid)
            return;
    }
    m_devicesList.append(device);
    createListeItem(device, data, reply);
    updateMircastState(SearchState::ListExhibit);
}
/**
 * @brief slotExitMircast 退出投屏
 */
void MircastWidget::slotExitMircast()
{
    qInfo() << "Exiting Miracast - Current state:" << m_mircastState;
    if (m_mircastState == Idel)
        return;
    m_mircastState = Idel;
    m_connectDevice.deviceState = Idel;
    m_mircastTimeOut.stop();
    m_connectTimeout = 0;
    m_listWidget->setItemWidgetStatus(m_listWidget->selectedItemWidget(), ItemWidget::Normal);
    stopDlnaTP();
    m_URLAddrPro.clear();
    emit mircastState(1, "normal");
    //    emit closeServer();
}
/**
 * @brief slotSeekMircast 跳转投屏进度
 */
void MircastWidget::slotSeekMircast(int seek)
{
    seekDlnaTp(seek);
}
/**
 * @brief slotPauseDlnaTp 投屏视频暂停与恢复播放
 */
void MircastWidget::slotPauseDlnaTp()
{
    if(m_mircastState != MircastWidget::Screening) return;
    if(m_nPlayStatus == MircastWidget::Play) {
        pauseDlnaTp();
    } else if(m_nPlayStatus == MircastWidget::Pause) {
        playDlnaTp();
    }
}
/**
 * @brief initializeHttpServer 初始化http Sever
 */
void MircastWidget::initializeHttpServer(int port)
{
    qDebug() << "Initializing HTTP server on port:" << port;
    if(!m_dlnaContentServer) {
        m_search = new CSSDPSearch(this);
        m_pDlnaSoapPost = new CDlnaSoapPost(this);
        connect(m_pDlnaSoapPost, &CDlnaSoapPost::sigGetPostionInfo, this, &MircastWidget::slotGetPositionInfo, Qt::QueuedConnection);

        QList<QHostAddress> lstInfo = QNetworkInterface::allAddresses();
        QString sLocalIp;
        foreach(QHostAddress address, lstInfo)
        {
             if(address.protocol() == QAbstractSocket::IPv4Protocol && !address.toString().trimmed().contains("127.0."))
             {
                 sLocalIp =  address.toString().trimmed();
                 break;
             }
        }
        qDebug() << "Local IP address:" << sLocalIp;
        m_dlnaContentServer = new DlnaContentServer(NULL, port);
        connect(this, &MircastWidget::closeServer, m_dlnaContentServer, &DlnaContentServer::closeServer);

        m_dlnaContentServer->setBaseUrl(QString("http://%1:%2/").arg(sLocalIp, QString::number(port)));
        qDebug() << "Base URL set to:" << m_dlnaContentServer->getBaseUrl();
    }
}
/**
 * @brief startDlnaTp 初始化http Sever
 */
void MircastWidget::startDlnaTp(ItemWidget *item)
{
    qDebug() << "Starting DLNA transport - Item:" << (item ? item->getDevice().name : "null");
    if (item != nullptr) {
        m_ControlURLPro = item->property(controlURLPro).toString();
        m_URLAddrPro = item->property(urlAddrPro).toString();
    }

    if(!m_dlnaContentServer)
    {
        qWarning() << "HTTP server not initialized";
        return;
    } else {
        dmr::PlayerEngine *pEngine = static_cast<dmr::PlayerEngine *>(m_pEngine);
        if(pEngine && pEngine->playlist().currentInfo().url.isLocalFile()) {
            m_dlnaContentServer->setDlnaFileName(pEngine->playlist().currentInfo().url.toLocalFile());
            m_sLocalUrl = m_dlnaContentServer->getBaseUrl()  + QFileInfo(pEngine->playlist().currentInfo().url.toLocalFile()).fileName().toLatin1();
        } else {
            m_sLocalUrl = pEngine->playlist().currentInfo().url.toString();
        }
        m_isStartHttpServer = m_dlnaContentServer->getIsStartHttpServer();
        qDebug() << "Local URL set to:" << m_sLocalUrl;
    }
    if(!m_isStartHttpServer)
    {
        qWarning() << "HTTP server not started";
        return;
    }
//    if(btn->text() != "Stop") {
        m_pDlnaSoapPost->SoapOperPost(DLNA_Stop, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
        m_pDlnaSoapPost->SoapOperPost(DLNA_SetAVTransportURI, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
//        btn->setText("Stop");
//    } else {
//        m_pDlnaSoapPost->SoapOperPost(DLNA_Stop, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
//        btn->setText(btn->property(friendlyNamePro).toString());
//    }

    m_mircastTimeOut.start(MIRCASTTIMEOUT);
    m_nPlayStatus = MircastWidget::Play;
    m_nCurDuration = -1;
    m_nCurAbsTime = -1;
    emit updatePlayStatus();
    qDebug() << "DLNA transport started successfully";
}
/**
 * @brief startDlnaTp 时间字符串装换为int秒 时间格式"00:00:00"
 */
int MircastWidget::timeConversion(QString time)
{
    QStringList timeList = time.split(":");
    if (timeList.size() == 3) {
        int realTime = 0;
        realTime += timeList.at(0).toInt() * 60 * 60;
        realTime += timeList.at(1).toInt() * 60;
        realTime += timeList.at(2).toInt();

        return realTime;
    }
    return 0;
}
/**
 * @brief slotConnectDevice 投屏播放视频暂停
 */
void MircastWidget::pauseDlnaTp()
{
    m_nPlayStatus = MircastWidget::Pause;
    m_pDlnaSoapPost->SoapOperPost(DLNA_Pause, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    emit updatePlayStatus();
}
/**
 * @brief slotConnectDevice 投屏播放视频播放
 */
void MircastWidget::playDlnaTp()
{
    m_nPlayStatus = MircastWidget::Play;
    m_pDlnaSoapPost->SoapOperPost(DLNA_Play, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    emit updatePlayStatus();
}
/**
 * @brief slotConnectDevice 投屏播放视频seek
 */
void MircastWidget::seekDlnaTp(int nSeek)
{
    m_pDlnaSoapPost->SoapOperPost(DLNA_Seek, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl, nSeek);
}
/**
 * @brief stopDlnaTP 停止投屏播放视频
 */
void MircastWidget::stopDlnaTP()
{
    qDebug() << "Stopping DLNA transport - Control URL:" << m_ControlURLPro;
    m_nPlayStatus = MircastWidget::Stop;
    if (m_ControlURLPro.isNull() || m_ControlURLPro.isEmpty()) return;
    if(m_sTrackURI == m_sLocalUrl)//当前播放自身投屏的视频才启动停止投屏操作
        m_pDlnaSoapPost->SoapOperPost(DLNA_Stop, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    m_ControlURLPro.clear();
    m_URLAddrPro.clear();
    m_sLocalUrl.clear();
    emit updatePlayStatus();
}
/**
 * @brief getPosInfoDlnaTp 获取投屏播放视频信息
 */
void MircastWidget::getPosInfoDlnaTp()
{
    m_pDlnaSoapPost->SoapOperPost(DLNA_GetPositionInfo, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
}

RefreButtonWidget::RefreButtonWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(24, 24);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(mainLayout);

    m_spinner = new DSpinner;
    m_spinner->setFixedSize(size());
    mainLayout->addWidget(m_spinner);

    m_refreBtn = new DLabel;
    m_refreBtn->setPixmap(QIcon::fromTheme("dcc_update").pixmap(size()));
    m_refreBtn->setFixedSize(size());
    m_refreBtn->hide();
    mainLayout->addWidget(m_refreBtn);

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        m_spinner->setFixedSize(16, 16);
        m_refreBtn->setFixedSize(16, 16);
        m_refreBtn->setPixmap(QIcon::fromTheme("dcc_update").pixmap(QSize(16, 16)));
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            m_spinner->setFixedSize(24, 24);
            m_refreBtn->setFixedSize(24, 24);
            m_refreBtn->setPixmap(QIcon::fromTheme("dcc_update").pixmap(QSize(24, 24)));
        } else {
            m_spinner->setFixedSize(16, 16);
            m_refreBtn->setFixedSize(16, 16);
            m_refreBtn->setPixmap(QIcon::fromTheme("dcc_update").pixmap(QSize(16, 16)));
        }
    });
#endif

}

void RefreButtonWidget::refershTimeout()
{
    m_spinner->stop();
    m_spinner->hide();
    m_refreBtn->show();
}

void RefreButtonWidget::refershStart()
{
    m_spinner->start();
    m_spinner->show();
    m_refreBtn->hide();

    emit buttonClicked();
}

void RefreButtonWidget::mouseReleaseEvent(QMouseEvent *pEvent)
{
    if (m_spinner->isVisible())
        return;
    refershStart();
}

ListWidget::ListWidget(QWidget *parent)
    :QWidget (parent)
{
    setContentsMargins(0, 0, 0, 0);
    setFixedWidth(MIRCASTWIDTH);
    QVBoxLayout *listLayout = new QVBoxLayout(this);
    setLayout(listLayout);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    m_currentWidget = nullptr;
    m_lastSelectedWidget = nullptr;
}

int ListWidget::count()
{
    return m_items.size();
}

void ListWidget::clear()
{
    m_currentWidget = nullptr;
    m_lastSelectedWidget = nullptr;
    foreach (ItemWidget *item, m_items) {
        m_items.removeOne(item);
        disconnect(item, &ItemWidget::selected, this, &ListWidget::slotSelectItem);
        item->deleteLater();
        item = nullptr;
    }
}

ItemWidget* ListWidget::createListeItem(MiracastDevice device, const QByteArray &data, const QNetworkReply *reply)
{
    ItemWidget *itemWidget = new ItemWidget(device, data, reply);
    connect(itemWidget, &ItemWidget::selected, this, &ListWidget::slotSelectItem);
    connect(itemWidget, &ItemWidget::connecting, this, &ListWidget::slotsConnectingDevice);
    m_items.append(itemWidget);
    layout()->addWidget(itemWidget);
    resize(MIRCASTWIDTH, count() * 34);
#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        resize(MIRCASTWIDTH, count() * 25);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            resize(MIRCASTWIDTH, count() * 34);
        } else {
            resize(MIRCASTWIDTH, count() * 25);
        }
        update();
    });
#endif
    return itemWidget;
}

int ListWidget::currentItemIndex()
{
    if (!m_currentWidget)
        return -1;
    return m_items.indexOf(m_currentWidget);
}

ItemWidget *ListWidget::currentItemWidget()
{
    return m_currentWidget;
}

QList<ItemWidget*> ListWidget::selectedItemWidget()
{
    QList<ItemWidget*> itemList;
    foreach (ItemWidget *item, m_items) {
        if (item->state() != ItemWidget::Normal)
            itemList.append(item);
    }
    return itemList;
}

void ListWidget::setItemWidgetStatus(QList<ItemWidget *> lstItem, ItemWidget::ConnectState nState)
{
    foreach (ItemWidget *item, lstItem) {
        item->setState(nState);
    }
}

void ListWidget::slotSelectItem()
{
    ItemWidget *senderItem = (ItemWidget*)sender();
    if (senderItem) {
        m_currentWidget = senderItem;
        foreach (ItemWidget *item, m_items) {
            if (item != senderItem)
                item->clearSelect();
        }
        update();
    }
}

void ListWidget::slotsConnectingDevice()
{
    ItemWidget *connectItem = (ItemWidget*)sender();
    if (!connectItem)
        return;
    if (m_lastSelectedWidget) {
        m_lastSelectedWidget->setState(ItemWidget::Normal);
    }
    m_lastSelectedWidget = connectItem;
    emit connectDevice(connectItem);
}

ItemWidget::ItemWidget(MiracastDevice device, const QByteArray &data, const QNetworkReply *reply, QWidget *parent)
    :m_device(device), m_data(data), QWidget (parent)
{
    m_selected = false;
    m_hover = false;
    m_rotate = 0.0;
    m_normalLoadIcon = QIcon(":/resources/icons/mircast/spinner.svg");
    m_selectLoadIcon = QIcon(":/resources/icons/mircast/spinner_White.svg");
    connect(&m_rotateTime, &QTimer::timeout, [=](){
        m_rotate += ROTATE_VALUE;
    });
    setToolTip(m_device.name);
    setFixedWidth(MIRCASTWIDTH);
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_displayName = convertDisplay();
    GetDlnaXmlValue dlnaxml(m_data);
    QString sName = dlnaxml.getValueByPath("device/friendlyName");
    QString urlAddrProValue = "";
    if(reply)
        urlAddrProValue = reply->property(urlAddrPro).toString();
    setProperty(urlAddrPro, urlAddrProValue);
    QString strControlURL = dlnaxml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:AVTransport:1", "controlURL");
    if(!strControlURL.startsWith("/")) {
        setProperty(controlURLPro, urlAddrProValue + "/" +strControlURL);
    } else {
        setProperty(controlURLPro, urlAddrProValue +strControlURL);
    }
    setProperty(friendlyNamePro, sName);
#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        setFixedSize(MIRCASTWIDTH, 25);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            setFixedSize(MIRCASTWIDTH, 34);
        } else {
            setFixedSize(MIRCASTWIDTH, 25);
        }
        update();
    });
#endif
}

void ItemWidget::clearSelect()
{
    m_selected = false;
}

void ItemWidget::setState(ItemWidget::ConnectState state)
{
    m_state = state;
    if (m_state == Loading) {
            m_rotateTime.start(40);
    } else {
        if(m_rotateTime.isActive())
            m_rotateTime.stop();
        m_rotate = 0.0;
    }
}

ItemWidget::ConnectState ItemWidget::state()
{
    return m_state;
}

MiracastDevice ItemWidget::getDevice()
{
    return m_device;
}

void ItemWidget::mousePressEvent(QMouseEvent *pEvent)
{
    Q_UNUSED(pEvent);

    m_selected = true;
    m_hover = false;
    emit selected();
    update();
}

void ItemWidget::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);
    bool isCompactMode = false;
    QPoint centerPos(218, 17);
#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        isCompactMode = true;
        centerPos.setX(226);
        centerPos.setY(11);
    }
#endif

    QPainter paint(this);
    paint.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRect(rect());
    QColor TextColor(Qt::black);
    if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType())
        TextColor = QColor(198, 207, 205);
    if (m_selected) {
        paint.fillPath(path, QBrush(QColor(0,129,255)));
        TextColor = Qt::white;
    } else if (m_hover) {
        paint.fillPath(path, QBrush(QColor(0, 0, 0, 0.05 * 255)));
    }
    paint.setPen(TextColor);
    paint.drawText(QRect(20, (rect().height() - 20) / 2, 176, 20), m_displayName, QTextOption(Qt::AlignVCenter));

    QIcon icon;
    switch (m_state) {
    case Normal:
        return;
    case Loading:
        icon = m_normalLoadIcon;
        if (m_selected) icon = m_selectLoadIcon;

        paint.save();
        paint.translate(centerPos);
        paint.rotate(m_rotate);
        if (!isCompactMode)
            paint.drawPixmap(-12, -12, QPixmap(icon.pixmap(QSize(24, 24))));
        else
            paint.drawPixmap(-8, -8, QPixmap(icon.pixmap(QSize(16, 16))));
        paint.restore();
        update();

        break;
    case Checked:
        QColor selectColor(Qt::black);
        if (m_selected) selectColor.setRgb(255, 255, 255);
        paint.setPen(QPen(selectColor, 2));
        QList<QPointF> points = QList<QPointF>();
        if (!isCompactMode)
            points << QPointF(214, 17) << QPointF(219, 22) << QPointF(227, 11);
        else
            points << QPointF(217, 16) << QPointF(220, 18) << QPointF(225, 10);
        QPainterPath path(points[0]);
        for (int i = 1; i < points.size(); i++) {
            path.lineTo(points[i]);
        }
        paint.drawPath(path);
        break;
    }
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void ItemWidget::enterEvent(QEvent *event)
{
    Q_UNUSED(event);

    m_hover = true;
}
#else
void ItemWidget::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hover = true;
}
#endif

void ItemWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);

    m_hover = false;
}

void ItemWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);

    emit connecting();
}
/**
 * @brief convertDisplay 设备名称操作170个字符转换
 */
QString ItemWidget::convertDisplay()
{
    QFontMetrics fm = fontMetrics();
    double textWidth;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    textWidth = fm.width(m_device.name);
#else
    textWidth = fm.horizontalAdvance(m_device.name);
#endif

    if (textWidth > TEXT_WIDTH) {
        QString displayName;
        for (int i = 0; i < m_device.name.size(); i++) {
            displayName += m_device.name.at(i);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            if (fm.width(displayName) > TEXT_WIDTH) {
#else
            if (fm.horizontalAdvance(displayName) > TEXT_WIDTH) {
#endif
                displayName.chop(1);
                displayName += "...";
                break;
            }
        }
        return displayName;
    } else {
        return m_device.name;
    }
}
