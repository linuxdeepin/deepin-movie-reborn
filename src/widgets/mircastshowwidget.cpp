// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 *@file 这个文件是播放音乐时显示的窗口动画
 */

#include "mircastshowwidget.h"
#include "compositing_manager.h"

#include <QSvgRenderer>
#include <QSvgWidget>
#include <QGraphicsItem>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QMouseEvent>
#include <QApplication>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#else
#include <QGuiApplication>
#include <QScreen>
#endif


#define DEFAULT_BGWIDTH 410
#define DEFAULT_BGHEIGHT 287
#define X_OFFSET 17
#define Y_OFFSET 143
#define DEFAULT_RATION (1.0f*680/1070)

MircastShowWidget::MircastShowWidget(QWidget *parent)
: QGraphicsView(parent)
{
    if (!dmr::CompositingManager::get().composited()) {
        winId();
    }

    setAlignment(Qt::AlignCenter);
    setFrameShape(QFrame::Shape::NoFrame);
    setMouseTracking(true);

    m_pScene = new QGraphicsScene;
    m_pScene->setBackgroundBrush(QBrush(QColor(0, 0, 0)));
    this->setScene(m_pScene);

    m_pBgRender = new QSvgRenderer(QString(":/resources/icons/mircast/default_Back.svg"));

    m_pBgSvgItem = new QGraphicsSvgItem;
    m_pBgSvgItem->setSharedRenderer(m_pBgRender);
    m_pBgSvgItem->setCacheMode(QGraphicsItem::NoCache);
    m_pScene->setSceneRect(m_pBgSvgItem->boundingRect());   //要在设置位置之前，不然动画会跳动
    m_pBgSvgItem->setPos((m_pScene->width() - DEFAULT_BGWIDTH) / 2, (m_pScene->height() - DEFAULT_BGHEIGHT) / 2);

    m_pProSvgItem = new QGraphicsPixmapItem;
    QPixmap pixmap(QString(":/resources/icons/mircast/prospect.png"));
    m_pProSvgItem->setPixmap(pixmap.scaled(376, 100));
    m_pProSvgItem->setPos(m_pBgSvgItem->pos().x() + X_OFFSET, m_pBgSvgItem->pos().y() + Y_OFFSET);

    ExitButton *exitBtn = new ExitButton();
    exitBtn->setToolTip(tr("Exit Miracast"));
    exitBtn->move((m_pScene->width() - exitBtn->width()) / 2, (m_pScene->height() - exitBtn->height()) / 2);
    exitBtn->show();
    connect(exitBtn, &ExitButton::exitMircast, this, &MircastShowWidget::exitMircast);

    m_deviceName = new QGraphicsTextItem;
    m_deviceName->setDefaultTextColor(Qt::white);
    m_deviceName->setTextWidth(DEFAULT_BGWIDTH);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
    m_deviceName->setPos(m_pBgSvgItem->pos().x(), m_pBgSvgItem->pos().y() - 20);

    m_promptInformation = new QGraphicsTextItem;
    m_promptInformation->setDefaultTextColor(QColor(255, 255, 255, 153));
    m_promptInformation->setPlainText(tr("Projecting... \nPlease do not exit the Movie app during the process."));
    m_promptInformation->setTextWidth(DEFAULT_BGWIDTH);
    QFont font = m_deviceName->font();
    font.setPointSize(10);
    QTextBlockFormat infoFormat;
    infoFormat.setAlignment(Qt::AlignCenter);
    QTextCursor infoCursor = m_promptInformation->textCursor();
    infoCursor.mergeBlockFormat(infoFormat);
    m_promptInformation->setFont(font);
    m_promptInformation->setTextCursor(infoCursor);
    m_promptInformation->setPos(m_pBgSvgItem->pos().x(), m_pBgSvgItem->pos().y() + DEFAULT_BGHEIGHT + 10);

    m_pScene->addItem(m_pBgSvgItem);
    m_pScene->addItem(m_pProSvgItem);
    m_pScene->addItem(m_deviceName);
    m_pScene->addItem(m_promptInformation);
    m_pScene->addWidget(exitBtn);
}

MircastShowWidget::~MircastShowWidget()
{

}
/**
 * @brief setDeviceName 设置投屏设备名称
 */
void MircastShowWidget::setDeviceName(QString name)
{
    QString display = QString(tr("Display device"))+QString(":  %1").arg(customizeText(name));
    m_deviceName->setPlainText(display);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
}

void MircastShowWidget::updateView()
{
    qreal fRatio = 1.0;
    QRect rectDesktop;
    int nWidth = 0;
    int nHeight = 0;

    nWidth = rect().width();
    nHeight = rect().height();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    rectDesktop = qApp->desktop()->availableGeometry(this);
#else
    rectDesktop = QGuiApplication::primaryScreen()->availableGeometry();
#endif

    //根据比例缩放背景
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {
        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
        fRatio = nWidth * 2.0 / rectDesktop.width();
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
        fRatio = nHeight * 2.0 / rectDesktop.height();
    }

    m_pBgSvgItem->setScale(fRatio);
    m_pProSvgItem->setScale(fRatio);

    m_pBgSvgItem->setPos((m_pScene->width() - DEFAULT_BGWIDTH * fRatio) / 2, (m_pScene->height() - DEFAULT_BGHEIGHT * fRatio) / 2);
    m_pProSvgItem->setPos(m_pBgSvgItem->pos().x() + (X_OFFSET * fRatio), m_pBgSvgItem->pos().y() + (Y_OFFSET * fRatio));
    m_deviceName->setTextWidth(DEFAULT_BGWIDTH * fRatio);
    m_deviceName->setPos(m_pBgSvgItem->pos().x(), m_pBgSvgItem->pos().y() - 20);
    m_promptInformation->setTextWidth(DEFAULT_BGWIDTH * fRatio);
    m_promptInformation->setPos(m_pBgSvgItem->pos().x(), (DEFAULT_BGHEIGHT + 10) * fRatio + m_pBgSvgItem->pos().y());
    viewport()->update();
}

void MircastShowWidget::mouseMoveEvent(QMouseEvent *pEvent)
{
    pEvent->ignore();
    QGraphicsView::mouseMoveEvent(pEvent);
}
/**
 * @brief customizeText 设置投屏设备显示名称
 * @param name 设备名
 */
QString MircastShowWidget::customizeText(QString name)
{
    return name.length() > 20 ? name.left(20) + QString("...") : name;
}

ExitButton::ExitButton(QWidget *parent)
: QWidget(parent)
{
    m_state = ButtonState::Normal;
    setFixedSize(62, 62);
    setAttribute(Qt::WA_TranslucentBackground, true);

    m_svgWidget = new QSvgWidget(this);
    m_svgWidget->setFixedSize(32, 32);
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit normal.svg"));
    m_svgWidget->move((rect().width() - m_svgWidget->width()) / 2, (rect().height() - m_svgWidget->height()) / 2);
    m_svgWidget->show();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void ExitButton::enterEvent(QEvent *pEvent)
{
    Q_UNUSED(pEvent);
    m_state = Hover;
    update();
}
#else
void ExitButton::enterEvent(QEnterEvent *pEvent)
{
    Q_UNUSED(pEvent);
    m_state = Hover;
    update();
}
#endif

void ExitButton::leaveEvent(QEvent *pEvent)
{
    Q_UNUSED(pEvent);
    m_state = Normal;
    update();
}

void ExitButton::mousePressEvent(QMouseEvent *pEvent)
{
    Q_UNUSED(pEvent);
    m_state = Press;
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit pressed.svg"));
    update();
}

void ExitButton::mouseReleaseEvent(QMouseEvent *pEvent)
{
    Q_UNUSED(pEvent);
    emit exitMircast();
    m_state = Normal;
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit normal.svg"));
    update();
}

void ExitButton::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(rect());
    QLinearGradient linearGradient(0, rect().width() / 2, rect().height(), rect().width() / 2);

    switch (m_state) {
    case ButtonState::Normal:
        linearGradient.setColorAt(0, QColor(72, 72, 72));
        linearGradient.setColorAt(0, QColor(65, 65, 65));
        break;
    case ButtonState::Hover:
        linearGradient.setColorAt(0, QColor(114, 114, 114));
        linearGradient.setColorAt(0, QColor(83, 83, 83));
        break;
    case ButtonState::Press:
        linearGradient.setColorAt(0, QColor(36, 36, 36));
        linearGradient.setColorAt(0, QColor(40, 40, 40));
        break;
    }
    QBrush brush(linearGradient);
    painter.fillPath(path, brush);
}
