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
#include <QDebug>

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
    qDebug() << "Initializing MircastShowWidget";
    if (!dmr::CompositingManager::get().composited()) {
        qDebug() << "Compositing not enabled, getting window ID";
        winId();
    } else {
        qDebug() << "Compositing enabled.";
    }

    setAlignment(Qt::AlignCenter);
    setFrameShape(QFrame::Shape::NoFrame);
    setMouseTracking(true);
    qDebug() << "GraphicsView properties set.";

    m_pScene = new QGraphicsScene;
    m_pScene->setBackgroundBrush(QBrush(QColor(0, 0, 0)));
    this->setScene(m_pScene);
    qDebug() << "GraphicsScene initialized.";

    m_pBgRender = new QSvgRenderer(QString(":/resources/icons/mircast/default_Back.svg"));
    qDebug() << "Loading background SVG renderer";

    m_pBgSvgItem = new QGraphicsSvgItem;
    m_pBgSvgItem->setSharedRenderer(m_pBgRender);
    m_pBgSvgItem->setCacheMode(QGraphicsItem::NoCache);
    m_pScene->setSceneRect(m_pBgSvgItem->boundingRect());   //要在设置位置之前，不然动画会跳动
    m_pBgSvgItem->setPos((m_pScene->width() - DEFAULT_BGWIDTH) / 2, (m_pScene->height() - DEFAULT_BGHEIGHT) / 2);
    qDebug() << "Background SVG item positioned at:" << m_pBgSvgItem->pos();

    m_pProSvgItem = new QGraphicsPixmapItem;
    QPixmap pixmap(QString(":/resources/icons/mircast/prospect.png"));
    m_pProSvgItem->setPixmap(pixmap.scaled(376, 100));
    m_pProSvgItem->setPos(m_pBgSvgItem->pos().x() + X_OFFSET, m_pBgSvgItem->pos().y() + Y_OFFSET);
    qDebug() << "Prospect item positioned at:" << m_pProSvgItem->pos();

    ExitButton *exitBtn = new ExitButton();
    exitBtn->setToolTip(tr("Exit Miracast"));
    exitBtn->move((m_pScene->width() - exitBtn->width()) / 2, (m_pScene->height() - exitBtn->height()) / 2);
    exitBtn->show();
    connect(exitBtn, &ExitButton::exitMircast, this, &MircastShowWidget::exitMircast);
    qDebug() << "Exit button created and positioned";

    m_deviceName = new QGraphicsTextItem;
    m_deviceName->setDefaultTextColor(Qt::white);
    m_deviceName->setTextWidth(DEFAULT_BGWIDTH);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
    m_deviceName->setPos(m_pBgSvgItem->pos().x(), m_pBgSvgItem->pos().y() - 20);
    qDebug() << "Device name text item created and positioned";

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
    qDebug() << "Prompt information text item created and positioned";

    m_pScene->addItem(m_pBgSvgItem);
    m_pScene->addItem(m_pProSvgItem);
    m_pScene->addItem(m_deviceName);
    m_pScene->addItem(m_promptInformation);
    m_pScene->addWidget(exitBtn);
    qDebug() << "All items added to scene";
    qDebug() << "Exiting MircastShowWidget constructor.";
}

MircastShowWidget::~MircastShowWidget()
{
    qDebug() << "Destroying MircastShowWidget";
}
/**
 * @brief setDeviceName 设置投屏设备名称
 * @param name 设备名
 */
void MircastShowWidget::setDeviceName(QString name)
{
    qDebug() << "Setting device name to:" << name;
    QString display = QString(tr("Display device"))+QString(":  %1").arg(customizeText(name));
    m_deviceName->setPlainText(display);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
    qDebug() << "Device name set and formatted.";
    qDebug() << "Exiting MircastShowWidget::setDeviceName().";
}

void MircastShowWidget::updateView()
{
    qDebug() << "Updating view";
    qreal fRatio = 1.0;
    QRect rectDesktop;
    int nWidth = 0;
    int nHeight = 0;

    nWidth = rect().width();
    nHeight = rect().height();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "Qt version < 6.0.0, using QDesktopWidget for available geometry.";
    rectDesktop = qApp->desktop()->availableGeometry(this);
#else
    qDebug() << "Qt version >= 6.0.0, using QGuiApplication::primaryScreen for available geometry.";
    rectDesktop = QGuiApplication::primaryScreen()->availableGeometry();
#endif
    qDebug() << "View dimensions - Width:" << nWidth << "Height:" << nHeight;

    //根据比例缩放背景
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {
        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
        fRatio = nWidth * 2.0 / rectDesktop.width();
        qDebug() << "Adjusted width based on ratio:" << nWidth << "Ratio:" << fRatio;
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
        fRatio = nHeight * 2.0 / rectDesktop.height();
        qDebug() << "Adjusted height based on ratio:" << nHeight << "Ratio:" << fRatio;
    }

    m_pBgSvgItem->setScale(fRatio);
    m_pProSvgItem->setScale(fRatio);
    qDebug() << "SVG items scaled with ratio:" << fRatio;

    m_pBgSvgItem->setPos((m_pScene->width() - DEFAULT_BGWIDTH * fRatio) / 2, (m_pScene->height() - DEFAULT_BGHEIGHT * fRatio) / 2);
    m_pProSvgItem->setPos(m_pBgSvgItem->pos().x() + (X_OFFSET * fRatio), m_pBgSvgItem->pos().y() + (Y_OFFSET * fRatio));
    m_deviceName->setTextWidth(DEFAULT_BGWIDTH * fRatio);
    m_deviceName->setPos(m_pBgSvgItem->pos().x(), m_pBgSvgItem->pos().y() - 20);
    m_promptInformation->setTextWidth(DEFAULT_BGWIDTH * fRatio);
    m_promptInformation->setPos(m_pBgSvgItem->pos().x(), (DEFAULT_BGHEIGHT + 10) * fRatio + m_pBgSvgItem->pos().y());
    viewport()->update();
    qDebug() << "View updated with new positions and scales";
    qDebug() << "Exiting MircastShowWidget::updateView().";
}

void MircastShowWidget::mouseMoveEvent(QMouseEvent *pEvent)
{
    qDebug() << "Entering MircastShowWidget::mouseMoveEvent().";
    pEvent->ignore();
    QGraphicsView::mouseMoveEvent(pEvent);
    qDebug() << "Exiting MircastShowWidget::mouseMoveEvent().";
}
/**
 * @brief customizeText 设置投屏设备显示名称
 * @param name 设备名
 */
QString MircastShowWidget::customizeText(QString name)
{
    qDebug() << "Entering MircastShowWidget::customizeText() with name:" << name;
    QString result = name.length() > 20 ? name.left(20) + QString("...") : name;
    qDebug() << "Customizing text - Original:" << name << "Result:" << result;
    qDebug() << "Exiting MircastShowWidget::customizeText() with result:" << result;
    return result;
}

ExitButton::ExitButton(QWidget *parent)
: QWidget(parent)
{
    qDebug() << "Initializing ExitButton";
    m_state = ButtonState::Normal;
    setFixedSize(62, 62);
    setAttribute(Qt::WA_TranslucentBackground, true);
    qDebug() << "ExitButton initialized with normal state, fixed size, and translucent background.";

    m_svgWidget = new QSvgWidget(this);
    m_svgWidget->setFixedSize(32, 32);
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit normal.svg"));
    m_svgWidget->move((rect().width() - m_svgWidget->width()) / 2, (rect().height() - m_svgWidget->height()) / 2);
    m_svgWidget->show();
    qDebug() << "SVG widget initialized and positioned.";
    qDebug() << "Exiting ExitButton constructor.";
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
void ExitButton::enterEvent(QEvent *pEvent)
{
    qDebug() << "Entering ExitButton::enterEvent() (Qt5) with event:" << pEvent;
    Q_UNUSED(pEvent);
    m_state = Hover;
    update();
    qDebug() << "Button state set to Hover and updated.";
    qDebug() << "Exiting ExitButton::enterEvent() (Qt5).";
}
#else
void ExitButton::enterEvent(QEnterEvent *pEvent)
{
    qDebug() << "Entering ExitButton::enterEvent() (Qt6) with event:" << pEvent;
    Q_UNUSED(pEvent);
    m_state = Hover;
    update();
    qDebug() << "Button state set to Hover and updated.";
    qDebug() << "Exiting ExitButton::enterEvent() (Qt6).";
}
#endif

void ExitButton::leaveEvent(QEvent *pEvent)
{
    qDebug() << "Entering ExitButton::leaveEvent() with event:" << pEvent;
    Q_UNUSED(pEvent);
    m_state = Normal;
    update();
    qDebug() << "Button state set to Normal and updated.";
    qDebug() << "Exiting ExitButton::leaveEvent().";
}

void ExitButton::mousePressEvent(QMouseEvent *pEvent)
{
    qDebug() << "Entering ExitButton::mousePressEvent() with event:" << pEvent;
    Q_UNUSED(pEvent);
    qDebug() << "Exit button pressed - changing state to Press";
    m_state = Press;
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit pressed.svg"));
    update();
    qDebug() << "Button state set to Press, SVG loaded, and updated.";
    qDebug() << "Exiting ExitButton::mousePressEvent().";
}

void ExitButton::mouseReleaseEvent(QMouseEvent *pEvent)
{
    qDebug() << "Entering ExitButton::mouseReleaseEvent() with event:" << pEvent;
    Q_UNUSED(pEvent);
    qDebug() << "Exit button released - emitting exitMircast signal";
    emit exitMircast();
    m_state = Normal;
    m_svgWidget->load(QString(":/resources/icons/mircast/icon-exit normal.svg"));
    update();
    qDebug() << "exitMircast signal emitted, state set to Normal, SVG loaded, and updated.";
    qDebug() << "Exiting ExitButton::mouseReleaseEvent().";
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
