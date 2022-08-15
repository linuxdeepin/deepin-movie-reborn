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

using namespace dmr;

MircastWidget::MircastWidget(QWidget *mainWindow, void *pEngine)
: DFloatingWidget(mainWindow), m_pEngine(pEngine)
{
    setAttribute(Qt::WA_NoMousePropagation, true);//鼠标事件不进入父窗口
    if(!CompositingManager::get().composited())
        setAttribute(Qt::WA_NativeWindow);
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

    m_searchTime.setSingleShot(true);
    connect(&m_searchTime, &QTimer::timeout, this, &MircastWidget::slotSearchTimeout);
    connect(&m_mircastTimeOut, &QTimer::timeout, this, &MircastWidget::slotMircastTimeout);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(1, 0, 0, 0);
    mainLayout->setSpacing(0);
    setLayout(mainLayout);
    setContentsMargins(0, 0, 0, 0);

    QWidget *topWdiget = new QWidget(this);
    topWdiget->setFixedHeight(40);
    mainLayout->addWidget(topWdiget);

    DLabel *projet = new DLabel(topWdiget);
    projet->setText(tr("Project to"));
    projet->move(20, 8);

    m_refreshBtn = new RefreButtonWidget(QIcon::fromTheme("dcc_update", QIcon(":/resources/icons/mircast/mircast.svg"))
                                                          , QIcon(":/resources/icons/mircast/spinner.svg"), topWdiget);
    m_refreshBtn->move(206, 8);
    connect(m_refreshBtn, &RefreButtonWidget::buttonClicked, this, &MircastWidget::slotRefreshBtnClicked);

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
    m_hintWidget->setLayout(hintLayout);
    m_hintWidget->hide();

    m_hintLabel = new DLabel(this);
    m_hintLabel->setAlignment(Qt::AlignCenter);
    m_hintLabel->setFixedSize(MIRCASTWIDTH, MIRCASTHEIGHT - 42);
    m_hintLabel->setWordWrap(true);
    hintLayout->addWidget(m_hintLabel);
    m_hintLabel->show();

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

    mainLayout->addStretch();
    m_dlnaContentServer = nullptr;
}

MircastWidget::MircastState MircastWidget::getMircastState()
{
    return m_mircastState;
}

MircastWidget::MircastPlayState MircastWidget::getMircastPlayState()
{
    return m_nPlayStatus;
}

void MircastWidget::playNext()
{
    if (m_mircastState != MircastState::Idel) {
        m_mircastTimeOut.stop();
        m_attempts = 0;
        m_connectTimeout = 0;
        PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
        engine->pauseResume();
        engine->seekAbsolute(0);
//        stopDlnaTP();
        startDlnaTp();
    }
}

void MircastWidget::seekMircast(int nSec)
{
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


void MircastWidget::togglePopup()
{
    if (m_bIsToggling) return;
    if (isVisible()) {
        hide();
    } else {
        m_bIsToggling = true;
        show();
        raise();
        m_bIsToggling = false;
    }
}

void MircastWidget::slotRefreshBtnClicked()
{
    initializeHttpServer();
    searchDevices();
    update();
}

void MircastWidget::slotSearchTimeout()
{
    qInfo() << "search timeout!!";
    if (m_devicesList.isEmpty())
        updateMircastState(SearchState::NoDevices);
    else
        updateMircastState(SearchState::ListExhibit);

    m_refreshBtn->refershTimeout();
    update();
}

void MircastWidget::slotMircastTimeout()
{
    m_pDlnaSoapPost->SoapOperPost(DLNA_GetPositionInfo, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    m_connectTimeout++;
    if (m_connectTimeout >= MAXMIRCAST) {
        m_mircastTimeOut.stop();
        if (m_mircastState == MircastState::Screening)
            emit mircastState(MIRCAST_DISCONNECTIONED);
        else
            emit mircastState(MIRCAST_CONNECTION_FAILED);
    }
}

void MircastWidget::slotGetPositionInfo(DlnaPositionInfo info)
{
    if (m_mircastState == MircastState::Idel)
        return;
    //TODO:测试电视退出投屏后是否会有返回
    m_connectTimeout = 0;
    PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
    if (engine->state() == PlayerEngine::CoreState::Idle) {
        emit mircastState(MIRCAST_EXIT);
        slotExitMircast();
        return;
    }
    PlaylistModel *model = engine->getplaylist();
    PlaylistModel::PlayMode playMode = model->playMode();

    if (m_mircastState == MircastState::Screening) {
        int absTime = timeConversion(info.sAbsTime);
        updateTime(absTime);
        if (info.sAbsTime == info.sTrackDuration ||
                info.sAbsTime.toUpper() == "NOT_IMPLEMENTED" && timeConversion(info.sTrackDuration) != 0) {
            if (playMode == PlaylistModel::SinglePlay ||
                    (playMode == PlaylistModel::OrderPlay && model->current() == (model->count() - 1))) {
                emit mircastState(MIRCAST_EXIT);
                slotExitMircast();
            } else if (playMode == PlaylistModel::SingleLoop) {
                startDlnaTp();
            } else {
                model->playNext(true);
                m_mircastState = Connecting;
            }
            m_attempts = 0;
        }
        m_nCurAbsTime = absTime;
        m_nCurDuration = timeConversion(info.sTrackDuration);
        return;
    }
    int duration = timeConversion(info.sTrackDuration);
    int absTime = timeConversion(info.sAbsTime);
    if (duration > 0 && absTime > 0) {
        emit mircastState(0, m_devicesList.at(0));
        m_mircastState = MircastState::Screening;
        m_listWidget->currentItemWidget()->setState(ItemWidget::Checked);
        m_attempts = 0;
    } else {
        if (duration > 0)
            emit mircastState(0, m_devicesList.at(0));
        qWarning() << "mircast failed!";
        if (m_attempts >= MAXMIRCAST) {
            qWarning() << "attempts time out! try next.";
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
            qInfo() << "mircast failed! curret attempts:" << m_attempts << "Max:" << MAXMIRCAST;
            m_attempts++;
            m_mircastState = Connecting;
        }
    }
}

void MircastWidget::slotConnectDevice(ItemWidget *item)
{
    QString newURLAddrPro = item->property(urlAddrPro).toString();
    if (newURLAddrPro == m_URLAddrPro && m_mircastState == MircastState::Screening)
        return;
    PlayerEngine *engine =static_cast<PlayerEngine *>(m_pEngine);
    if (engine->state() == PlayerEngine::CoreState::Idle) {
        return;
    }
    m_mircastState = Connecting;
    item->setState(ItemWidget::Loading);
    stopDlnaTP();
    startDlnaTp(item);
}

void MircastWidget::searchDevices()
{
    qInfo() << __func__ << "start search Devices!";
    m_devicesList.clear();
    m_listWidget->clear();
    m_searchTime.start(REFRESHTIME);
    m_search->SsdpSearch();
    updateMircastState(SearchState::Searching);
}

void MircastWidget::updateMircastState(MircastWidget::SearchState state)
{
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
        m_hintLabel->setText(tr("No Mircast display devices were found. Please connect the device and your computer to the same WLAN network."));
        m_mircastArea->hide();
        break;
    }
}

void MircastWidget::createListeItem(QString name, const QByteArray &data, const QNetworkReply *reply)
{
    ItemWidget *item = m_listWidget->createListeItem(name, data, reply);
    QString itemAdd = item->property(urlAddrPro).toString();
    if (itemAdd == m_URLAddrPro && m_mircastState == MircastState::Screening)
        item->setState(ItemWidget::Checked);
}

void MircastWidget::slotReadyRead()
{
    QNetworkReply *reply = (QNetworkReply *)sender();
    if(reply->error() != QNetworkReply::NoError) {
//        QMessageBox::warning(this,"Error", QString::number(reply->error()));
        qInfo() << "Error:" << QString::number(reply->error());
        return;
    }
    QByteArray data = reply->readAll().replace("\r\n", "").replace("\\", "");
    qInfo() << "xml data:" << data;
    GetDlnaXmlValue dlnaxml(data);
    QString sName = dlnaxml.getValueByPath("device/friendlyName");
    m_devicesList.append(sName);


//    int nNum = reply->property(replayShowNum).toInt();
//    if(nNum >= 20) return;
//    QPushButton *btn = m_listBtn.at(nNum);
//    btn->setText(sName);
//    btn->setToolTip(reply->property(urlAddrPro).toString());
//    btn->show();
////    QPushButton *btn = new QPushButton("荣耀智慧屏");
//    btn->setProperty(urlAddrPro, reply->property(urlAddrPro).toString());
//    QString strControlURL = dlnaxml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:AVTransport:1", "controlURL");
//    if(!strControlURL.startsWith("/")) {
//        btn->setProperty(controlURLPro, reply->property(urlAddrPro).toString() + "/" +strControlURL);
//    } else {
//        btn->setProperty(controlURLPro, reply->property(urlAddrPro).toString() +strControlURL);
//    }
//    btn->setProperty(friendlyNamePro, sName);


//    ui->textEdit->append(btn->property(controlURLPro).toString());

    createListeItem(sName, data, reply);
    updateMircastState(SearchState::ListExhibit);
}

void MircastWidget::slotExitMircast()
{
    qInfo() << __func__ << "Exit Mircast.";
    if (m_mircastState == Idel)
        return;
    m_mircastState = Idel;
    m_mircastTimeOut.stop();
    m_connectTimeout = 0;
    m_listWidget->setItemWidgetStatus(m_listWidget->selectedItemWidget(), ItemWidget::Normal);
    stopDlnaTP();
    m_URLAddrPro.clear();
    emit mircastState(1, "normal");
    //    emit closeServer();
}

void MircastWidget::slotSeekMircast(int seek)
{
    seekDlnaTp(seek);
}

void MircastWidget::slotPauseDlnaTp()
{
    if(m_mircastState != MircastWidget::Screening) return;
    if(m_nPlayStatus == MircastWidget::Play) {
        pauseDlnaTp();
    } else if(m_nPlayStatus == MircastWidget::Pause) {
        playDlnaTp();
    }
}

void MircastWidget::initializeHttpServer(int port)
{
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
        m_dlnaContentServer = new DlnaContentServer(NULL, port);
        connect(this, &MircastWidget::closeServer, m_dlnaContentServer, &DlnaContentServer::closeServer);


        m_dlnaContentServer->setBaseUrl(QString("http://%1:%2/").arg(sLocalIp, QString::number(port)));
    }
}

void MircastWidget::startDlnaTp(ItemWidget *item)
{
    if (item != nullptr) {
        m_ControlURLPro = item->property(controlURLPro).toString();
        m_URLAddrPro = item->property(urlAddrPro).toString();
    }

    if(!m_dlnaContentServer)
    {
        qInfo() << "note: please Create httpServer!";
        return;
    } else {
        dmr::PlayerEngine *pEngine = static_cast<dmr::PlayerEngine *>(m_pEngine);
        if(pEngine && pEngine->playlist().currentInfo().url.isLocalFile()) {
            m_dlnaContentServer->setDlnaFileName(pEngine->playlist().currentInfo().url.toLocalFile());
            m_sLocalUrl = m_dlnaContentServer->getBaseUrl()  + QFileInfo(pEngine->playlist().currentInfo().url.toLocalFile()).fileName();
        } else {
            m_sLocalUrl = pEngine->playlist().currentInfo().url.toString();
        }
        m_isStartHttpServer = m_dlnaContentServer->getIsStartHttpServer();
    }
    if(!m_isStartHttpServer)
    {
        qInfo() << "note: please start httpServer!";
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
}

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

void MircastWidget::pauseDlnaTp()
{
    m_nPlayStatus = MircastWidget::Pause;
    m_pDlnaSoapPost->SoapOperPost(DLNA_Pause, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    emit updatePlayStatus();
}

void MircastWidget::playDlnaTp()
{
    m_nPlayStatus = MircastWidget::Play;
    m_pDlnaSoapPost->SoapOperPost(DLNA_Play, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
    emit updatePlayStatus();
}

void MircastWidget::seekDlnaTp(int nSeek)
{
    m_pDlnaSoapPost->SoapOperPost(DLNA_Seek, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl, nSeek);
}

void MircastWidget::stopDlnaTP()
{
    m_nPlayStatus = MircastWidget::Stop;
    m_pDlnaSoapPost->SoapOperPost(DLNA_Stop, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
}

void MircastWidget::getPosInfoDlnaTp()
{
    m_pDlnaSoapPost->SoapOperPost(DLNA_GetPositionInfo, m_ControlURLPro, m_URLAddrPro, m_sLocalUrl);
}

RefreButtonWidget::RefreButtonWidget(QIcon refreIcon, QIcon loadingIcon, QWidget *parent)
    : QWidget(parent), m_refreIcon(refreIcon), m_loadingIcon(loadingIcon)
{
    setFixedSize(24, 24);
    m_refreState = true;
    m_rotate = 0.0;

    connect(&m_rotateTime, &QTimer::timeout, [=](){
        m_rotate += 14.4;
    });
}

void RefreButtonWidget::refershTimeout()
{
    m_refreState = true;
    m_rotateTime.stop();
    m_rotate = 0.0;
}

void RefreButtonWidget::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);
    QPainter painter(this);
    QPoint centerPos = rect().center();

    if (m_refreState) {
        painter.drawPixmap(rect(), QPixmap(m_refreIcon.pixmap(rect().size())));
    } else {
        painter.save();
        painter.translate(centerPos);
        painter.rotate(m_rotate);
        painter.drawPixmap(-12, -12, QPixmap(m_loadingIcon.pixmap(rect().size())));
        painter.restore();
        update();
    }
}

void RefreButtonWidget::mouseReleaseEvent(QMouseEvent *pEvent)
{
    m_refreState = false;
    m_rotateTime.start(40);
    emit buttonClicked();
}

ListWidget::ListWidget(QWidget *parent)
    :QWidget (parent)
{
//    QPalette pal(palette());
//    //设置背景黑色
//    pal.setColor(QPalette::Background, Qt::red);
//    setAutoFillBackground(true);
//    setPalette(pal);

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
    foreach (ItemWidget *item, m_items) {
        m_items.removeOne(item);
        disconnect(item, &ItemWidget::selected, this, &ListWidget::slotSelectItem);
        delete item;
        item = nullptr;
    }
}

ItemWidget* ListWidget::createListeItem(QString deviceName, const QByteArray &data, const QNetworkReply *reply)
{
    ItemWidget *itemWidget = new ItemWidget(deviceName, data, reply);
    connect(itemWidget, &ItemWidget::selected, this, &ListWidget::slotSelectItem);
    connect(itemWidget, &ItemWidget::connecting, this, &ListWidget::slotsConnectingDevice);
    m_items.append(itemWidget);
    layout()->addWidget(itemWidget);
    resize(MIRCASTWIDTH, count() * 34);
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

ItemWidget::ItemWidget(QString deviceName, const QByteArray &data, const QNetworkReply *reply, QWidget *parent)
    :m_deviceName(deviceName), m_data(data), QWidget (parent)
{
    m_selected = false;
    m_hover = false;
    m_rotate = 0.0;
    m_normalLoadIcon = QIcon(":/resources/icons/mircast/spinner.svg");
    m_selectLoadIcon = QIcon(":/resources/icons/mircast/spinner_White.svg");

    connect(&m_rotateTime, &QTimer::timeout, [=](){
        m_rotate += 14.4;
    });

    setToolTip(m_deviceName);
    setFixedSize(MIRCASTWIDTH, 34);
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_displayName = convertDisplay();

    GetDlnaXmlValue dlnaxml(m_data);
    QString sName = dlnaxml.getValueByPath("device/friendlyName");
    setProperty(urlAddrPro, reply->property(urlAddrPro).toString());
    QString strControlURL = dlnaxml.getValueByPathValue("device/serviceList", "serviceType=urn:schemas-upnp-org:service:AVTransport:1", "controlURL");
    if(!strControlURL.startsWith("/")) {
        setProperty(controlURLPro, reply->property(urlAddrPro).toString() + "/" +strControlURL);
    } else {
        setProperty(controlURLPro, reply->property(urlAddrPro).toString() +strControlURL);
    }
    setProperty(friendlyNamePro, sName);
}

void ItemWidget::clearSelect()
{
    m_selected = false;
}

void ItemWidget::setState(ItemWidget::ConnectState state)
{
    m_state = state;
    if (m_state == Loading) m_rotateTime.start(40);
    else {
        m_rotateTime.stop();
        m_rotate = 0.0;
    }
}

ItemWidget::ConnectState ItemWidget::state()
{
    return m_state;
}

void ItemWidget::mousePressEvent(QMouseEvent *pEvent)
{
    Q_UNUSED(pEvent);

    m_selected = true;
    m_hover = false;
    emit selected();
    update();
//    pEvent->accept();//鼠标事件处理后不进入父窗口，单个事件的开关
}

void ItemWidget::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);

    QPainter paint(this);
    paint.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    QPainterPath path;
    path.addRect(rect());
//    QRect rect = rect();
    QColor TextColor(Qt::black);
    if (m_selected) {
        paint.fillPath(path, QBrush(QColor(0,129,255)));
        TextColor.setRgb(255, 255, 255);
    } else if (m_hover) {
        paint.fillPath(path, QBrush(QColor(0, 0, 0, 0.05 * 255)));
    }
    paint.setPen(TextColor);
    paint.drawText(QRect(20, 7, 176, 20), m_displayName, QTextOption(Qt::AlignVCenter));

    QPoint centerPos(218, 17);
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
        paint.drawPixmap(-12, -12, QPixmap(icon.pixmap(QSize(24, 24))));
        paint.restore();
        update();

        break;
    case Checked:
        QColor selectColor(Qt::black);
        if (m_selected) selectColor.setRgb(255, 255, 255);
        paint.setPen(QPen(selectColor, 2));
        QList<QPointF> points = QList<QPointF>() << QPointF(214, 17) << QPointF(219, 22) << QPointF(227, 11);
        QPainterPath path(points[0]);
        for (int i = 1; i < points.size(); i++) {
            path.lineTo(points[i]);
        }
        paint.drawPath(path);
        break;
    }
}

void ItemWidget::enterEvent(QEvent *event)
{
    Q_UNUSED(event);

    m_hover = true;
}

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

QString ItemWidget::convertDisplay()
{
    QFontMetrics fm = fontMetrics();
    double textWidth = fm.width(m_deviceName);
    if (textWidth > 170) {
        QString displayName;
        for (int i = 0; i < m_deviceName.size(); i++) {
            displayName += m_deviceName.at(i);
            if (fm.width(displayName) > 170) {
                displayName.chop(1);
                displayName += "...";
                break;
            }
        }
        return displayName;
    } else {
        return m_deviceName;
    }
}
