/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
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

#define DEFAULT_BGWIDTH 410
#define DEFAULT_BGHEIGHT 287

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

    m_pBgRender = new QSvgRenderer(QString(":/resources/icons/mircast/default.svg"));

    m_pBgSvgItem = new QGraphicsSvgItem;
    m_pBgSvgItem->setSharedRenderer(m_pBgRender);
    m_pBgSvgItem->setCacheMode(QGraphicsItem::NoCache);
    m_pScene->setSceneRect(m_pBgSvgItem->boundingRect());   //要在设置位置之前，不然动画会跳动
    m_pBgSvgItem->setPos((m_pScene->width() - DEFAULT_BGWIDTH) / 2, (m_pScene->height() - DEFAULT_BGHEIGHT) / 2);

    ExitButton *exitBtn = new ExitButton();
    exitBtn->move((m_pScene->width() - exitBtn->width()) / 2, (m_pScene->height() - exitBtn->height()) / 2);
    exitBtn->show();
    connect(exitBtn, &ExitButton::exitMircast, this, &MircastShowWidget::exitMircast);

    m_deviceName = new QGraphicsTextItem;
    m_deviceName->setDefaultTextColor(Qt::white);
    m_deviceName->setTextWidth(390);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
    m_deviceName->setPos(m_pBgSvgItem->pos().x() + 10, m_pBgSvgItem->pos().y() - 20);

    QGraphicsTextItem *promptInformation = new QGraphicsTextItem;
    promptInformation->setDefaultTextColor(QColor(255, 255, 255, 153));
    promptInformation->setPlainText(tr("Projecting... \nPlease do not exit the Movie app during the process."));
    promptInformation->setTextWidth(224);
    promptInformation->setTextCursor(cursor);
    promptInformation->setPos(m_pBgSvgItem->pos().x() + 93, m_pBgSvgItem->pos().y() + 297);

    m_pScene->addItem(m_pBgSvgItem);
    m_pScene->addItem(m_deviceName);
    m_pScene->addItem(promptInformation);
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
    QString display = QString(tr("Display device"))+QString(":%1").arg(customizeText(name));
    m_deviceName->setPlainText(display);
    QTextBlockFormat format;
    format.setAlignment(Qt::AlignCenter);
    QTextCursor cursor = m_deviceName->textCursor();
    cursor.mergeBlockFormat(format);
    m_deviceName->setTextCursor(cursor);
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

void ExitButton::enterEvent(QEvent *pEvent)
{
    Q_UNUSED(pEvent);
    m_state = Hover;
    update();
}

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
