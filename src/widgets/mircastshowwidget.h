// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 *@file 这个文件是播放音乐时显示的窗口动画
 */
#ifndef MIRCASTSHOWWIDGET_H
#define MIRCASTSHOWWIDGET_H

#include <QGraphicsView>
#include <QGraphicsSvgItem>

#include <DLabel>

DWIDGET_USE_NAMESPACE

class QSvgWidget;
class ExitButton: public QWidget
{
    Q_OBJECT
public:
    enum ButtonState {
        Normal,
        Hover,
        Press,
    };
public:
    ExitButton(QWidget *parent = nullptr);

signals:
    void exitMircast();

protected:
    void enterEvent(QEvent *pEvent) override;
    void leaveEvent(QEvent *pEvent) override;
    void mousePressEvent(QMouseEvent *pEvent) override;
    void mouseReleaseEvent(QMouseEvent *pEvent) override;
    void paintEvent(QPaintEvent *pEvent) override;
private:
    QSvgWidget *m_svgWidget;//投屏退出图标窗口
    ButtonState m_state; //按钮状态
};

class MircastShowWidget: public QGraphicsView
{
    Q_OBJECT
public:
    explicit MircastShowWidget(QWidget *parent = nullptr);
    ~MircastShowWidget();
    /**
     * @brief setDeviceName 设置投屏设备名称
     */
    void setDeviceName(QString);
    /**
     * @brief updateView 更新图元位置
     */
    void updateView();

protected:
    void mouseMoveEvent(QMouseEvent* pEvent) override;

signals:
    void exitMircast();

private:
    /**
     * @brief customizeText 设置投屏设备显示名称
     * @param name 设备名
     */
    QString customizeText(QString name);

private:
    QGraphicsSvgItem *m_pBgSvgItem;
    QGraphicsPixmapItem *m_pProSvgItem;
    QGraphicsScene *m_pScene;
    QGraphicsTextItem *m_deviceName;
    QGraphicsTextItem *m_promptInformation;
    QSvgRenderer *m_pBgRender;    ///背景render
};

#endif  //MIRCASTSHOWWIDGET_H
