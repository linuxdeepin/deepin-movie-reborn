// Copyright (C) 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shadow class definitions for unit tests.
//
// TooltipHandler / SliderTime / ViewProgBar / ThumbnailPreview are only
// forward-declared in src/widgets/toolbox_proxy.h and defined inside
// src/widgets/toolbox_proxy.cpp (lines 122-915). Unit tests need complete
// types to drive those classes, so this header carries a VERBATIM copy of
// the four class definitions.
//
// !!! KEEP IN SYNC with src/widgets/toolbox_proxy.cpp !!!
// If the source classes change, refresh this copy (sed -n '122,915p').
// Inline member bodies are identical in both TUs, so the linker's vague
// linkage keeps one copy; non-inline members resolve to toolbox_proxy.o.
#ifndef TEST_TOOLBOX_SHADOW_CLASSES_H
#define TEST_TOOLBOX_SHADOW_CLASSES_H

#include "config.h"
#include "ddlog.h"

#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbutton.h"
#include "dmr_settings.h"
#include "actions.h"
#include "slider.h"
#include "thumbnail_worker.h"
#include "tip.h"
#include "utils.h"
#include "filefilter.h"
#include "sysutils.h"

#include <DArrowRectangle>
#include <DApplication>
#include <QThread>
#include <DSlider>
#include <DUtil>
#include <DWindowManagerHelper>
#include <dthememanager.h>
#include <QDir>
#include <QDBusInterface>
#include "../accessibility/ac-deepin-movie-define.h"

static const QString SLIDER_ARROW = ":resources/icons/slider.svg";

namespace dmr {
/**
 * @brief The TooltipHandler class
 * 鼠标悬停事件过滤器
 */
class TooltipHandler: public QObject
{
public:
    /**
     * @brief TooltipHandler 构造函数
     * @param parent 父窗口
     */
    explicit TooltipHandler(QObject *parent): QObject(parent) {
        qCDebug(appMovie) << "TooltipHandler";
        connect(&m_showTime, &QTimer::timeout, [=]{
            //QHelpEvent *he = static_cast<QHelpEvent *>(event);
            if (m_object != nullptr) {
                auto tip = m_object->property("HintWidget").value<Tip *>();
                auto btn = tip->property("for").value<QWidget *>();
                tip->setText(btn->toolTip());
                tip->show();
                tip->raise();
                tip->adjustSize();

                QPoint pos = btn->parentWidget()->mapToGlobal(btn->pos());
                pos.rx() = pos.x() + (btn->width() - tip->width()) / 2;
                pos.ry() = pos.y() - 40;
                tip->move(pos);
            }
        });
        qCDebug(appMovie) << "TooltipHandler end";
    }

protected:
    /**
     * @brief eventFilter 事件过滤器
     * @param obj 事件对象
     * @param event 事件
     * @return 返回是否继续执行
     */
    bool eventFilter(QObject *obj, QEvent *event)
    {
        qCDebug(appMovie) << "TooltipHandler";
        switch (event->type()) {
        case QEvent::ToolTip:
        case QEvent::Enter: {
            qCDebug(appMovie) << "QEvent::Enter";
            m_object = obj;
            if (!m_showTime.isActive())
                m_showTime.start(1000);
            return true;
        }

        case QEvent::Leave: {
            qCDebug(appMovie) << "QEvent::Leave";
            m_object = nullptr;
            m_showTime.stop();
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();
            break;
        }
        case QEvent::MouseMove: {
            qCDebug(appMovie) << "QEvent::MouseMove";
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            tip->hide();
        }
        default:
            break;
        }
        qCDebug(appMovie) << "TooltipHandler end";
        // standard event processing
        return QObject::eventFilter(obj, event);
    }

private:
    QTimer m_showTime;
    QObject *m_object {nullptr};
};
/**
 * @brief The SliderTime class 进度条事件显示类
 */
class SliderTime: public DArrowRectangle
{
    Q_OBJECT
public:
    /**
     * @brief SliderTime 构造函数
     */
    SliderTime(): DArrowRectangle(DArrowRectangle::ArrowBottom)
    {
        qCDebug(appMovie) << "SliderTime";
        initMember();

        //setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::WindowStaysOnTopHint);
        resize(m_miniSize);
        setRadius(8);
        setArrowWidth(10);
        setArrowHeight(5);
        const QPalette pal = QGuiApplication::palette();
        QColor bgColor = pal.color(QPalette::Highlight);
        setBorderWidth(1);
        setBorderColor(bgColor);
        setBackgroundColor(bgColor);

        QHBoxLayout *layout = new QHBoxLayout;
        layout->setContentsMargins(0, 0, 0, 5);
        m_pTime = new DLabel(this);
        m_pTime->setAlignment(Qt::AlignCenter);
//        _time->setFixedSize(_size);
        m_pTime->setForegroundRole(DPalette::Text);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        DPalette pa = DApplicationHelper::instance()->palette(m_pTime);
#else
        DPalette pa = DGuiApplicationHelper::instance()->applicationPalette();
#endif
        QColor color = pa.textLively().color();
        qCInfo(appMovie) << color.name();
        pa.setColor(DPalette::Text, color);
        m_pTime->setPalette(pa);
        m_pTime->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));
        layout->addWidget(m_pTime, Qt::AlignCenter);
        setLayout(layout);
        connect(qApp, &QGuiApplication::fontChanged, this, &SliderTime::slotFontChanged);
        qCDebug(appMovie) << "SliderTime end";
    }

    /**
     * @brief setTime 设置时间
     * @param time 时间
     */
    void setTime(const QString &time)
    {
        qCDebug(appMovie) << "setTime";
        m_pTime->setText(time);

        if (!m_bFontChanged) {
            qCDebug(appMovie) << "!m_bFontChanged";
            QFontMetrics fontMetrics(DFontSizeManager::instance()->get(DFontSizeManager::T8));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            m_pTime->setFixedSize(fontMetrics.width(m_pTime->text()) + 5, fontMetrics.height());
#else
            m_pTime->setFixedSize(fontMetrics.horizontalAdvance(m_pTime->text()) + 5, fontMetrics.height());
#endif
        } else {
            qCDebug(appMovie) << "m_bFontChanged";
            QFontMetrics fontMetrics(m_font);
            m_pTime->setFont(m_font);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            m_pTime->setFixedSize(fontMetrics.width(m_pTime->text()) + 10, fontMetrics.height());
#else
            m_pTime->setFixedSize(fontMetrics.horizontalAdvance(m_pTime->text()) + 10, fontMetrics.height());
#endif
        }
        this->setWidth(m_pTime->width());
        this->setHeight(m_pTime->height() + 5);
        this->setMinimumSize(m_miniSize);
        qCDebug(appMovie) << "setTime end";
    }
public slots:
    /**
     * @brief slotFontChanged 字体变化槽函数
     * @param font 字体
     */
    void slotFontChanged(const QFont &font)
    {
        qCDebug(appMovie) << "slotFontChanged";
        m_font = font;
        m_bFontChanged = true;
        qCDebug(appMovie) << "slotFontChanged end";
    }
private:
    /**
     * @brief initMember 初始化成员变量
     */
    void initMember()
    {
        qCDebug(appMovie) << "initMember";
        m_pTime = nullptr;
        m_miniSize = QSize(58, 25);
        m_font = {QFont()};
        m_bFontChanged = false;
        qCDebug(appMovie) << "initMember end";
    }

    DLabel *m_pTime;
    QSize m_miniSize;
    QFont m_font;
    bool m_bFontChanged;
};

/**
 * @brief The ViewProgBar class 胶片模式窗口
 */
class ViewProgBar: public DWidget
{
    Q_OBJECT
public:
    /**
     * @brief ViewProgBar 构造函数
     * @param m_pProgBar 进度条
     * @param parent 父窗口
     */
    ViewProgBar(DMRSlider *m_pProgBar, QWidget *parent = nullptr)
    {
        qCDebug(appMovie) << "ViewProgBar";
        initMember();
        //传入进度条，以便重新获取胶片进度条长度 by ZhuYuliang
        this->m_pProgBar = m_pProgBar;
        _parent = parent;
        setFixedHeight(70);

        m_bIsBlockSignals = false;
        setMouseTracking(true);

        m_pBack = new QWidget(this);
        m_pBack->setFixedHeight(60);
        m_pBack->setFixedWidth(this->width());
        m_pBack->setContentsMargins(0, 0, 0, 0);

        m_pFront = new QWidget(this);
        m_pFront->setFixedHeight(60);
        m_pFront->setFixedWidth(0);
        m_pFront->setContentsMargins(0, 0, 0, 0);

        m_pIndicator = new IndicatorItem(this);
        m_pIndicator->setObjectName("indicator");

        m_pSliderTime = new SliderTime;
        m_pSliderTime->hide();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QMatrix matrix;
        matrix.rotate(180);
        QPixmap pixmap = utils::LoadHiDPIPixmap(SLIDER_ARROW);
        m_pSliderArrowUp = new DArrowRectangle(DArrowRectangle::ArrowTop);
        //m_pSliderArrowUp->setFocusPolicy(Qt::NoFocus);
        m_pSliderArrowUp->setAttribute(Qt::WA_DeleteOnClose);
        m_pSliderArrowUp->setWindowFlag(Qt::WindowStaysOnTopHint);
        m_pSliderArrowUp->setArrowWidth(10);
        m_pSliderArrowUp->setArrowHeight(7);
        const QPalette pa = QGuiApplication::palette();
        QColor bgColor = pa.color(QPalette::Highlight);
        m_pSliderArrowUp->setBackgroundColor(bgColor);
        m_pSliderArrowUp->setFixedSize(10, 7);
        m_pSliderArrowUp->hide();
        m_pSliderArrowDown = new DLabel(this);
        m_pSliderArrowDown->setFixedSize(20, 18);
        m_pSliderArrowDown->setPixmap(pixmap.transformed(matrix, Qt::SmoothTransformation));
        m_pSliderArrowDown->hide();
#else
        QTransform transform;
        transform.rotate(180);
        QPixmap pixmap = utils::LoadHiDPIPixmap(SLIDER_ARROW);
        m_pSliderArrowUp = new DArrowRectangle(DArrowRectangle::ArrowTop);
        //m_pSliderArrowUp->setFocusPolicy(Qt::NoFocus);
        m_pSliderArrowUp->setAttribute(Qt::WA_DeleteOnClose);
        m_pSliderArrowUp->setWindowFlag(Qt::WindowStaysOnTopHint);
        m_pSliderArrowUp->setArrowWidth(10);
        m_pSliderArrowUp->setArrowHeight(7);
        const QPalette pa = QGuiApplication::palette();
        QColor bgColor = pa.color(QPalette::Highlight);
        m_pSliderArrowUp->setBackgroundColor(bgColor);
        m_pSliderArrowUp->setFixedSize(10, 7);
        m_pSliderArrowUp->hide();
        m_pSliderArrowDown = new DLabel(this);
        m_pSliderArrowDown->setFixedSize(20, 18);
        m_pSliderArrowDown->setPixmap(pixmap.transformed(transform, Qt::SmoothTransformation));
        m_pSliderArrowDown->hide();
#endif

        m_pBack->setMouseTracking(true);
        m_pFront->setMouseTracking(true);
        m_pIndicator->setMouseTracking(true);

        m_pViewProgBarLayout = new QHBoxLayout(m_pBack);
        m_pViewProgBarLayout->setContentsMargins(0, 5, 0, 5);
        m_pBack->setLayout(m_pViewProgBarLayout);

        m_pViewProgBarLayout_black = new QHBoxLayout(m_pFront);
        m_pViewProgBarLayout_black->setContentsMargins(0, 5, 0, 5);
        m_pFront->setLayout(m_pViewProgBarLayout_black);

#ifdef DTKWIDGET_CLASS_DSizeMode
        qCDebug(appMovie) << "DTKWIDGET_CLASS_DSizeMode";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        setFixedHeight(46);
        m_pBack->setFixedHeight(40);
        m_pFront->setFixedHeight(40);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        qCDebug(appMovie) << "sizeModeChanged";
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            qCDebug(appMovie) << "DGuiApplicationHelper::NormalMode";
            setFixedHeight(70);
            m_pBack->setFixedHeight(60);
            m_pFront->setFixedHeight(60);
        } else {
            qCDebug(appMovie) << "!DGuiApplicationHelper::NormalMode";
            setFixedHeight(46);
            m_pBack->setFixedHeight(40);
            m_pFront->setFixedHeight(40);
        }
    });
#endif
        qCDebug(appMovie) << "ViewProgBar end";
    }
//    virtual ~ViewProgBar();
    void setIsBlockSignals(bool isBlockSignals)
    {
        qCDebug(appMovie) << "setIsBlockSignals";
        m_bIsBlockSignals = isBlockSignals;
    }
    bool getIsBlockSignals()
    {
        qCDebug(appMovie) << "getIsBlockSignals";
        return  m_bPress ? true: m_bIsBlockSignals;
    }
    void setValue(int v)
    {
        qCDebug(appMovie) << "setValue";
        if (v < m_nStartPoint) {
            v = m_nStartPoint;
        } else if (v > (m_nStartPoint + m_nViewLength)) {
            v = (m_nStartPoint + m_nViewLength);
        }
        m_IndicatorPos = {v, rect().y()};
        update();
    }
    int getValue()
    {
        qCDebug(appMovie) << "getValue";
        return m_pIndicator->x();
    }
    int getTimePos()
    {
        qCDebug(appMovie) << "getTimePos";
        return position2progress(QPoint(m_pIndicator->x(), 0));
    }
    void setTime(qint64 pos)
    {
        qCDebug(appMovie) << "setTime";
        QTime time(0, 0, 0);
        QString strTime = time.addSecs(static_cast<int>(pos)).toString("hh:mm:ss");
        m_pSliderTime->setTime(strTime);
        qCDebug(appMovie) << "setTime end";
    }
    void setTimeVisible(bool visible)
    {
        qCDebug(appMovie) << "setTimeVisible";
        if (visible) {
            auto pos = this->mapToGlobal(QPoint(0, 0));
            m_pSliderTime->show(pos.x() + m_IndicatorPos.x() + 1, pos.y() + m_IndicatorPos.y() + 4);
        } else {
            m_pSliderTime->hide();
        }
        qCDebug(appMovie) << "setTimeVisible end";
    }
    /**
     * @brief setViewProgBar 设置胶片模式位置
     * @param pEngine 播放引擎对象指针
     * @param pmList 彩色胶片图像列表
     * @param pmBlackList 灰色胶片图像列表
     */
    void setViewProgBar(PlayerEngine *pEngine, QList<QPixmap> pmList, QList<QPixmap> pmBlackList)
    {
        qCDebug(appMovie) << "setViewProgBar";
        m_pEngine = pEngine;

        m_pViewProgBarLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_pViewProgBarLayout->setSpacing(1);

        /*这段代码将胶片添加到两个label中，一个label置灰，一个彩色，通过光标调整两个label的位置
         *以实现通过光标来显示播放过的位置
         */
        int nPixWidget = 40/*m_pProgBar->width() / 100*/;
        int npixHeight = 50;
#ifdef DTKWIDGET_CLASS_DSizeMode
            if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::SizeMode::CompactMode) {
                nPixWidget = nPixWidget * 0.66;
                npixHeight = npixHeight * 0.66;
            }
#endif
        m_nViewLength = (nPixWidget + 1) * pmList.count() - 1;
        m_nStartPoint = (m_pProgBar->width() - m_nViewLength) / 2; //开始位置
        for (int i = 0; i < pmList.count(); i++) {
            ImageItem *label = new ImageItem(pmList.at(i), false, m_pBack);
            label->setMouseTracking(true);
            label->move(i * (nPixWidget + 1) + m_nStartPoint, 5);
            label->setFixedSize(nPixWidget, npixHeight);

            ImageItem *label_black = new ImageItem(pmBlackList.at(i), true, m_pFront);
            label_black->setMouseTracking(true);
            label_black->move(i * (nPixWidget + 1) + m_nStartPoint, 5);
            label_black->setFixedSize(nPixWidget, npixHeight);
        }
        update();
        qCDebug(appMovie) << "setViewProgBar end";
    }
    void clear()
    {
        qCDebug(appMovie) << "clear";
        foreach (QLabel *label, m_pFront->findChildren<QLabel *>()) {
            if (label) {
                label->deleteLater();
                label = nullptr;
            }
        }

        foreach (QLabel *label, m_pBack->findChildren<QLabel *>()) {
            if (label) {
                label->deleteLater();
                label = nullptr;
            }
        }

        m_pSliderTime->setVisible(false);
        m_pSliderArrowDown->setVisible(false);
        m_pSliderArrowUp->setVisible(false);
        // 清除状态时还原初始显示状态
        m_bPress = false;
        m_pIndicator->setPressed(m_bPress);
        qCDebug(appMovie) << "clear end";
    }

    int getViewLength()
    {
        return m_nViewLength;
    }

    int getStartPoint()
    {
        return m_nStartPoint;
    }

private:
    void changeStyle(bool press)
    {
        qCDebug(appMovie) << "changeStyle";
        if (!isVisible()) {
            qCDebug(appMovie) << "!isVisible()";
            return;
        }

        if (press) {
            qCDebug(appMovie) << "press";
            m_pIndicator->setPressed(press);

        } else {
            qCDebug(appMovie) << "!press";
            m_pIndicator->setPressed(press);
        }
        qCDebug(appMovie) << "changeStyle end";
    }

signals:
    void leave();
    void hoverChanged(int);
    void sliderMoved(int);
    void mousePressed(bool pressed);

protected:
    void leaveEvent(QEvent *e) override
    {
        emit leave();

        DWidget::leaveEvent(e);
    }
    void showEvent(QShowEvent *se) override
    {
        DWidget::showEvent(se);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        qCDebug(appMovie) << "mouseMoveEvent";
        if (!isEnabled()) {
            qCDebug(appMovie) << "!isEnabled()";
            return;
        }

        if (e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()) {
            qCDebug(appMovie) << "e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()";
            int v = position2progress(e->pos());
            qCDebug(appMovie) << "v" << v;
            if (e->buttons() & Qt::LeftButton) {
                qCDebug(appMovie) << "e->buttons() & Qt::LeftButton";
                int distance = (e->pos() -  m_startPos).manhattanLength();
                if (distance >= QApplication::startDragDistance()) {
                    emit sliderMoved(v);
                    emit hoverChanged(v);
                    emit mousePressed(true);
                    setValue(e->pos().x());
                    setTime(v);
                    repaint();
                }
            } else {
                qCDebug(appMovie) << "!e->buttons() & Qt::LeftButton";
                if (m_nVlastHoverValue != v) {
                    qCDebug(appMovie) << "m_nVlastHoverValue != v";
                    emit hoverChanged(v);
                }
                m_nVlastHoverValue = v;
            }
        }
        qCDebug(appMovie) << "mouseMoveEvent end, e->accept()";
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        qCDebug(appMovie) << "mousePressEvent";
        if (!m_bPress && e->buttons() == Qt::LeftButton && isEnabled()) {
            qCDebug(appMovie) << "!m_bPress && e->buttons() == Qt::LeftButton && isEnabled()";
            m_startPos = e->pos();

            int v = position2progress(e->pos());
            emit sliderMoved(v);
            emit hoverChanged(v);
            emit mousePressed(true);
            setValue(e->pos().x());
            changeStyle(!m_bPress);
            m_bPress = !m_bPress;
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        qCDebug(appMovie) << "mouseReleaseEvent";
        emit mousePressed(false);
        if (m_bPress && isEnabled()) {
            qCDebug(appMovie) << "m_bPress && isEnabled()";
            changeStyle(!m_bPress);
            m_bPress = !m_bPress;
            //鼠标释放时seek视频位置。
            int v = position2progress(e->pos());
            if (m_pEngine) {
                m_pEngine->seekAbsolute(v);
            }
        }

        m_pSliderArrowUp->setVisible(m_bPress);
        setTimeVisible(m_bPress);

        DWidget::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent *e) override
    {
        m_pIndicator->move(m_IndicatorPos.x(), m_IndicatorPos.y());
        QPoint pos = this->mapToGlobal(QPoint(0, 0));
        m_pSliderArrowUp->move(pos.x() + m_IndicatorPos.x() + 1, pos.y() + m_pIndicator->height() - 5);
        m_pFront->setFixedWidth(m_IndicatorPos.x());

        m_pSliderArrowUp->setVisible(m_bPress);
        setTimeVisible(m_bPress);

        DWidget::paintEvent(e);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        qCDebug(appMovie) << "resizeEvent";
        m_pBack->setFixedWidth(this->width());

        DWidget::resizeEvent(event);
    }
private:
    int  position2progress(const QPoint &p)
    {
        qCDebug(appMovie) << "position2progress";
        int nPosition = 0;

        if (!m_pEngine) {
            qCDebug(appMovie) << "!m_pEngine return 0";
            return 0;
        }

        if (p.x() < m_nStartPoint) {
            qCDebug(appMovie) << "p.x() < m_nStartPoint";
            nPosition = m_nStartPoint;
        } else if (p.x() > (m_nViewLength + m_nStartPoint)) {
            qCDebug(appMovie) << "p.x() > (m_nViewLength + m_nStartPoint)";
            nPosition = (m_nViewLength + m_nStartPoint);
        } else {
            qCDebug(appMovie) << "else";
            nPosition = p.x();
        }

        auto total = m_pEngine->duration();
        int span = static_cast<int>(total * (nPosition - m_nStartPoint) / m_nViewLength);
        qCDebug(appMovie) << "span" << span;
        return span/* * (p.x())*/;
    }

    void initMember()
    {
        qCDebug(appMovie) << "initMember";
        m_pEngine = nullptr;
        _parent = nullptr;
//        m_pViewProgBarLoad = nullptr;
        m_pBack = nullptr;
        m_pFront = nullptr;
        m_pIndicator  = nullptr;
        m_pSliderTime = nullptr;
        m_pSliderArrowDown = nullptr;
        m_pSliderArrowUp = nullptr;
        m_pIndicatorLayout = nullptr;
        m_pViewProgBarLayout = nullptr;
        m_pViewProgBarLayout_black = nullptr;
        m_pProgBar = nullptr;
        m_nViewLength = 0;
        m_nStartPoint = 0;
        m_nVlastHoverValue = 0;
        m_startPos = QPoint(0, 0);
        m_IndicatorPos  = QPoint(0, 0);
        m_bPress = false;
        m_bIsBlockSignals = false;
        qCDebug(appMovie) << "initMemeber end";
    }

    PlayerEngine *m_pEngine;
    QWidget *_parent;
    int  m_nVlastHoverValue;
    QPoint  m_startPos;
    bool  m_bIsBlockSignals;
    QPoint m_IndicatorPos;
    QColor _indicatorColor;
//    viewProgBarLoad *m_pViewProgBarLoad;
    QWidget *m_pBack;
    QWidget *m_pFront;
    IndicatorItem *m_pIndicator;
    SliderTime *m_pSliderTime;
    DLabel *m_pSliderArrowDown;
    DArrowRectangle *m_pSliderArrowUp;
    bool m_bPress;
    QHBoxLayout *m_pIndicatorLayout;
    QHBoxLayout *m_pViewProgBarLayout;
    QHBoxLayout *m_pViewProgBarLayout_black;
    DMRSlider *m_pProgBar;
    int m_nViewLength;
    int m_nStartPoint;
};

class ThumbnailPreview: public QWidget
{
    Q_OBJECT
public:
    ThumbnailPreview()
    {
        qCDebug(appMovie) << "ThumbnailPreview";
        setAttribute(Qt::WA_DeleteOnClose);
        // FIXME(hualet): Qt::Tooltip will cause Dock to show up even
        // the player is in fullscreen mode.
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setObjectName("ThumbnailPreview");
        resize(0, 0);

        m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
        connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasBlurWindowChanged, this, &ThumbnailPreview::slotWMChanged);
        if (m_bIsWM) {
            qCDebug(appMovie) << "m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qCDebug(appMovie) << "!m_bIsWM";
            DStyle::setFrameRadius(this, 0);
        }
        m_shadow_effect = new QGraphicsDropShadowEffect(this);
        qCDebug(appMovie) << "ThumbnailPreview end";
    }

    void updateWithPreview(const QPixmap &pm, qint64 secs, int rotation)
    {
        qCDebug(appMovie) << "updateWithPreview";
        QPixmap rounded;
        if (m_bIsWM) {
            qCDebug(appMovie) << "m_bIsWM";
            rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);
        } else {
            qCDebug(appMovie) << "!m_bIsWM";
            rounded = pm;
        }

        if (rounded.width() == 0) {
            qCDebug(appMovie) << "rounded.width() == 0";
            return;
        }
        if (rounded.width() > rounded.height()) {
            qCDebug(appMovie) << "rounded.width() > rounded.height()";
            static int roundedH = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.width()))
                                      * rounded.height());
            QSize size(m_thumbnailFixed, roundedH);
            resizeThumbnail(rounded, size);
        } else {
            qCDebug(appMovie) << "rounded.width() <= rounded.height()";
            static int roundedW = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.height()))
                                      * rounded.width());
            QSize size(roundedW, m_thumbnailFixed);
            resizeThumbnail(rounded, size);
        }

        QImage image;
        QPalette palette;
        image = rounded.toImage();
        m_thumbImg = image;
        update();
        qCDebug(appMovie) << "updateWithPreview end";
    }

    void updateWithPreview(const QPoint &pos)
    {
        qCDebug(appMovie) << "updateWithPreview";
        move(pos.x() - this->width() / 2, pos.y() - this->height() + 10);
        if(geometry().isValid()) {
            qCDebug(appMovie) << "geometry().isValid()";
            show();
        }
    }
public slots:
    void slotWMChanged()
    {
        qCDebug(appMovie) << "slotWMChanged";
        m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
        if (m_bIsWM) {
            qCDebug(appMovie) << "m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qCDebug(appMovie) << "!m_bIsWM";
            DStyle::setFrameRadius(this, 0);
        }
    }

signals:
    void leavePreview();

protected:
    void paintEvent(QPaintEvent *e) Q_DECL_OVERRIDE{
        m_shadow_effect->setOffset(0, 0);
        m_shadow_effect->setColor(Qt::gray);
        m_shadow_effect->setBlurRadius(8);
        setGraphicsEffect(m_shadow_effect);
        QPainter painter(this);
        QPainterPath path;
        QRect rt = rect().marginsRemoved(QMargins(1, 1, 1, 1));
        if (!m_bIsWM)
        {
            path.addRect(rect());
            painter.fillPath(path, QColor(230, 230, 230));
        } else {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            path.addRoundRect(rt, 20, 20);
#else
            path.addRoundedRect(rt, 20, 20);
#endif
            painter.setRenderHints(QPainter::Antialiasing, true);
        }
        painter.setClipPath(path);
        if(!m_thumbImg.isNull())
            painter.drawImage(rt, m_thumbImg, QRect(0, 0, m_thumbImg.width(), m_thumbImg.height()));
        QWidget::paintEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        emit leavePreview();
    }

    void showEvent(QShowEvent *se) override
    {
        QWidget::showEvent(se);
    }

private:
    void resizeThumbnail(QPixmap &pixmap, const QSize &size)
    {
        qCDebug(appMovie) << "resizeThumbnail";
        auto dpr = qApp->devicePixelRatio();
        pixmap.setDevicePixelRatio(dpr);
        pixmap = pixmap.scaled(size * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        int offect = 2;
        if (!m_bIsWM) {
            qCDebug(appMovie) << "!m_bIsWM";
            offect = 0;
        }
        this->setFixedWidth(size.width() + offect);
        this->setFixedHeight(size.height() + offect);
        qCDebug(appMovie) << "resizeThumbnail end";
    }

private:
    QImage m_thumbImg;
    int m_thumbnailFixed = 106;
    QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
    bool m_bIsWM{false};
};
} // namespace dmr

#endif // TEST_TOOLBOX_SHADOW_CLASSES_H
