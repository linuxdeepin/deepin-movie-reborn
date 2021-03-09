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
 *@file 这个文件是播放音乐时显示的窗口动画
 */
#include <QHBoxLayout>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QApplication>
#include <QDesktopWidget>
#include <QPainter>

#include "moviewidget.h"

#define DEFAULT_RATION (1.0f*1080/1920)     //背景图片比例
#define INTERVAL 50                         //刷新间隔
#define ROTATE_ANGLE 360/(1000*2.5/INTERVAL)  //2.5秒转一圈
#define DEFAULT_BGLENGTH 174                //背景边长
#define DEFAULT_NTLENGTH 48                 //音符边长

namespace dmr {
/**
 * @brief MovieWidget 播放动画显示窗口
 * @param parent 父窗口
 */
MovieWidget::MovieWidget(QWidget *parent)
    : DWidget(parent), m_nRotate(0), m_nWidthNote(0), m_state(PlayState::STATE_STOP)
{
    initMember();
    m_pHBoxLayout = new QHBoxLayout(this);
    m_pHBoxLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    setLayout(m_pHBoxLayout);

    m_pLabMovie = new QLabel(this);
    m_pLabMovie->setAlignment(Qt::AlignCenter);
    m_pHBoxLayout->addWidget(m_pLabMovie);

    m_pBgRender = new QSvgRenderer(QString(":/resources/icons/music_bg.svg"));
    m_pNoteRender = new QSvgRenderer(QString(":/resources/icons/music_note.svg"));

    m_pTimer = new QTimer();
    m_pTimer->setInterval(INTERVAL);
    connect(m_pTimer, &QTimer::timeout, this, &MovieWidget::updateView);
}
/**
 * @brief startPlaying
 * 开始播放时的槽函数
 */
void MovieWidget::startPlaying()
{
    if (m_state == PlayState::STATE_STOP) {
        m_nRotate = 0;
        show();
    }

    m_pTimer->start();
    m_state = PlayState::STATE_PLAYING;
}
/**
 * @brief stopPlaying
 * 停止播放时的槽函数
 */
void MovieWidget::stopPlaying()
{
    m_pTimer->stop();
    m_state = PlayState::STATE_STOP;
    hide();
}
/**
 * @brief pausePlaying
 * 暂停播放时的槽函数
 */
void MovieWidget::pausePlaying()
{
    m_pTimer->stop();
    m_state = PlayState::STATE_PAUSE;
}
/**
 * @brief updateView
 * 更新窗口函数
 */
void MovieWidget::updateView()
{
    float fRatio = 1.0f;
    QRect rectDesktop;
    int nWidth = 0;
    int nHeight = 0;

    m_nRotate += ROTATE_ANGLE;

    nWidth = rect().width();
    nHeight = rect().height();
    rectDesktop = qApp->desktop()->availableGeometry(this);

    //根据比例缩放背景
    if (1.0f * nHeight / nWidth < DEFAULT_RATION) {

        nWidth = static_cast<int>(nHeight / DEFAULT_RATION);
        fRatio = nWidth * 2.0f / rectDesktop.width();
    } else {
        nHeight = static_cast<int>(nWidth * DEFAULT_RATION);
        fRatio = nHeight * 2.0f / rectDesktop.height();
    }

    int nBgLength = static_cast<int>(DEFAULT_BGLENGTH * fRatio);
    int nNoteLength = static_cast<int>(DEFAULT_NTLENGTH * fRatio);
    QRect rect((nBgLength - nNoteLength) / 2, (nBgLength - nNoteLength) / 2, nNoteLength, nNoteLength);

    QPixmap pixmapBg(nBgLength, nBgLength);
    pixmapBg.fill(Qt::transparent);

    QPainter painter(&pixmapBg);
    m_pBgRender->render(&painter);         //绘制背景

    painter.translate(pixmapBg.width() / 2.0, pixmapBg.height() / 2.0);
    painter.rotate(m_nRotate);
    painter.translate(-pixmapBg.width() / 2.0, -pixmapBg.height() / 2.0);
    painter.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform | QPainter::Antialiasing);

    m_pNoteRender->render(&painter, rect);  //绘制音符

    m_pLabMovie->setPixmap(pixmapBg);
}
/**
 * @brief initMember 初始化成员变量
 */
void MovieWidget::initMember()
{
    m_pLabMovie = nullptr;
    m_pTimer = nullptr;
    m_pHBoxLayout = nullptr;
}

}
