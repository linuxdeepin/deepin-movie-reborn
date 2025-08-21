// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include "platform_toolbox_proxy.h"
#include "platform/platform_mainwindow.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbutton.h"
#include "dmr_settings.h"
#include "actions.h"
#include "slider.h"
#include "platform/platform_thumbnail_worker.h"
#include "tip.h"
#include "utils.h"
#include "filefilter.h"
#include "sysutils.h"

//#include <QtWidgets>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DImageButton>
#include <DThemeManager>
#endif

#include <DArrowRectangle>
#include <DApplication>
#include <QThread>
#include <DSlider>
#include <DUtil>
#include <QDBusInterface>
#include <DToolButton>
#include <dthememanager.h>
#include <DWindowManagerHelper>
#include <iostream>
#include "../accessibility/ac-deepin-movie-define.h"
static const int LEFT_MARGIN = 10;
static const int RIGHT_MARGIN = 10;
//static const int PROGBAR_SPEC = 10 + 120 + 17 + 54 + 10 + 54 + 10 + 170 + 10 + 20;

static const QString SLIDER_ARROW = ":resources/icons/slider.svg";

#define POPUP_DURATION 350

DWIDGET_USE_NAMESPACE


#define THEME_TYPE(colortype) do { \
        if (colortype == DGuiApplicationHelper::LightType){\
            QColor backMaskColor(247, 247, 247);\
            backMaskColor.setAlphaF(0.6);\
            this->blurBackground()->setMaskColor(backMaskColor);\
            bot_widget->setMaskColor(backMaskColor);\
        } else if (colortype == DGuiApplicationHelper::DarkType){\
            QColor backMaskColor(32, 32, 32);\
            backMaskColor.setAlphaF(0.5);\
            blurBackground()->setMaskColor(backMaskColor);\
            bot_widget->setMaskColor(backMaskColor);\
        }\
    } while(0);

namespace dmr {
/**
 * @brief The TooltipHandler class
 * 鼠标悬停事件过滤器
 */
class Platform_TooltipHandler: public QObject
{
public:
    /**
     * @brief TooltipHandler 构造函数
     * @param parent 父窗口
     */
    explicit Platform_TooltipHandler(QObject *parent): QObject(parent) {
        qDebug() << "TooltipHandler constructor";
        connect(&m_showTime, &QTimer::timeout, [=]{
            qDebug() << "TooltipHandler timeout";
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
        qDebug() << "TooltipHandler eventFilter";
        switch (event->type()) {
        case QEvent::ToolTip:
        case QEvent::Enter: {
            qDebug() << "event tooltip or enter";
            m_object = obj;
            if (!m_showTime.isActive())
                m_showTime.start(1000);
            return true;
        }

        case QEvent::Leave: {
            qDebug() << "event leave";
            m_object = nullptr;
            m_showTime.stop();
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();
            break;
        }
        case QEvent::MouseMove: {
            qDebug() << "event mouse move";
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            tip->hide();
        }
        default:
            break;
        }
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
class Platform_SliderTime: public DArrowRectangle
{
    Q_OBJECT
public:
    /**
     * @brief SliderTime 构造函数
     */
    Platform_SliderTime(): DArrowRectangle(DArrowRectangle::ArrowBottom)
    {
        qDebug() << "SliderTime constructor";
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
        DPalette pa = DGuiApplicationHelper::instance()->palette(m_pTime);
#else   
        DPalette pa = m_pTime->palette();
#endif
        QColor color = pa.textLively().color();
        qInfo() << color.name();
        pa.setColor(DPalette::Text, color);
        m_pTime->setPalette(pa);
        m_pTime->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));
        layout->addWidget(m_pTime, Qt::AlignCenter);
        setLayout(layout);
        connect(qApp, &QGuiApplication::fontChanged, this, &Platform_SliderTime::slotFontChanged);
        qDebug() << "SliderTime constructor end";
    }

    /**
     * @brief setTime 设置时间
     * @param time 时间
     */
    void setTime(const QString &time)
    {
        qDebug() << "SliderTime setTime";
        m_pTime->setText(time);

        if (!m_bFontChanged) {
            qDebug() << "SliderTime setTime font not changed";
            QFontMetrics fontMetrics(DFontSizeManager::instance()->get(DFontSizeManager::T8));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            m_pTime->setFixedSize(fontMetrics.width(m_pTime->text()) + 5, fontMetrics.height());
#else
            m_pTime->setFixedSize(fontMetrics.horizontalAdvance(m_pTime->text()) + 5, fontMetrics.height());
#endif
        } else {
            qDebug() << "SliderTime setTime font changed";
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
        qDebug() << "SliderTime setTime end";
    }
public slots:
    /**
     * @brief slotFontChanged 字体变化槽函数
     * @param font 字体
     */
    void slotFontChanged(const QFont &font)
    {
        qDebug() << "SliderTime slotFontChanged";
        m_font = font;
        m_bFontChanged = true;
    }
private:
    /**
     * @brief initMember 初始化成员变量
     */
    void initMember()
    {
        qDebug() << "SliderTime initMember";
        m_pTime = nullptr;
        m_miniSize = QSize(58, 25);
        m_font = {QFont()};
        m_bFontChanged = false;
    }

    DLabel *m_pTime;
    QSize m_miniSize;
    QFont m_font;
    bool m_bFontChanged;
};

/**
 * @brief The ViewProgBar class 胶片模式窗口
 */
class Platform_ViewProgBar: public DWidget
{
    Q_OBJECT
public:
    /**
     * @brief ViewProgBar 构造函数
     * @param m_pProgBar 进度条
     * @param parent 父窗口
     */
   Platform_ViewProgBar(DMRSlider *m_pProgBar, QWidget *parent = nullptr)
    {
        qDebug() << "ViewProgBar constructor";
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

        m_pIndicator = new Platform_IndicatorItem(this);
        m_pIndicator->resize(6, 60);
        m_pIndicator->setObjectName("indicator");

        m_pSliderTime = new Platform_SliderTime;
        m_pSliderTime->hide();

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        qDebug() << "ViewProgBar constructor Qt5";
        // Qt5 版本  
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
        qDebug() << "ViewProgBar constructor Qt6";
        // Qt6 版本
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
        qDebug() << "ViewProgBar constructor DTKWIDGET_CLASS_DSizeMode";
        if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
            setFixedHeight(46);
            m_pBack->setFixedHeight(40);
            m_pFront->setFixedHeight(40);
        }

        connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
            if (sizeMode == DGuiApplicationHelper::NormalMode) {
                setFixedHeight(70);
                m_pBack->setFixedHeight(60);
                m_pFront->setFixedHeight(60);
            } else {
                setFixedHeight(46);
                m_pBack->setFixedHeight(40);
                m_pFront->setFixedHeight(40);
            }
        });
#endif
        qDebug() << "ViewProgBar constructor end";
    }
//    virtual ~ViewProgBar();
    void setIsBlockSignals(bool isBlockSignals)
    {
        qDebug() << "ViewProgBar setIsBlockSignals";
        m_bIsBlockSignals = isBlockSignals;
    }
    bool getIsBlockSignals()
    {
        qDebug() << "ViewProgBar getIsBlockSignals";
        return  m_bPress ? true: m_bIsBlockSignals;
    }
    void setValue(int v)
    {
        qDebug() << "ViewProgBar setValue" << v;
        if (v < m_nStartPoint) {
            qDebug() << "ViewProgBar setValue less than m_nStartPoint";
            v = m_nStartPoint;
        } else if (v > (m_nStartPoint + m_nViewLength)) {
            qDebug() << "ViewProgBar setValue greater than m_nStartPoint + m_nViewLength";
            v = (m_nStartPoint + m_nViewLength);
        }
        m_IndicatorPos = {v, rect().y()};
        update();
    }
    int getValue()
    {
        qDebug() << "ViewProgBar getValue";
        return m_pIndicator->x();
    }
    int getTimePos()
    {
        qDebug() << "ViewProgBar getTimePos";
        return position2progress(QPoint(m_pIndicator->x(), 0));
    }
    void setTime(qint64 pos)
    {
        qDebug() << "ViewProgBar setTime";
        QTime time(0, 0, 0);
        QString strTime = time.addSecs(static_cast<int>(pos)).toString("hh:mm:ss");
        m_pSliderTime->setTime(strTime);
    }
    void setTimeVisible(bool visible)
    {
        qDebug() << "ViewProgBar setTimeVisible" << visible;
        if (visible) {
            qDebug() << "ViewProgBar setTimeVisible show";
            auto pos = this->mapToGlobal(QPoint(0, 0));
            m_pSliderTime->show(pos.x() + m_IndicatorPos.x() + 1, pos.y() + m_IndicatorPos.y() + 4);
        } else {
            qDebug() << "ViewProgBar setTimeVisible hide";
            m_pSliderTime->hide();
        }
    }
    /**
     * @brief setViewProgBar 设置胶片模式位置
     * @param pEngine 播放引擎对象指针
     * @param pmList 彩色胶片图像列表
     * @param pmBlackList 灰色胶片图像列表
     */
    void setViewProgBar(PlayerEngine *pEngine, QList<QPixmap> pmList, QList<QPixmap> pmBlackList)
    {
        qDebug() << "ViewProgBar setViewProgBar";
        m_pEngine = pEngine;

        m_pViewProgBarLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        m_pViewProgBarLayout->setSpacing(1);

        /*这段代码将胶片添加到两个label中，一个label置灰，一个彩色，通过光标调整两个label的位置
         *以实现通过光标来显示播放过的位置
         */
        int nPixWidget = 40/*m_pProgBar->width() / 100*/;
        int npixHeight = 50;
#ifdef DTKWIDGET_CLASS_DSizeMode
        qDebug() << "ViewProgBar setViewProgBar DTKWIDGET_CLASS_DSizeMode";
        if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::SizeMode::CompactMode) {
            nPixWidget = nPixWidget * 0.66;
            npixHeight = npixHeight * 0.66;
        }
#endif
        qDebug() << "ViewProgBar setViewProgBar nPixWidget" << nPixWidget;
        qDebug() << "ViewProgBar setViewProgBar npixHeight" << npixHeight;
        m_nViewLength = (nPixWidget + 1) * pmList.count() - 1;
        m_nStartPoint = (m_pProgBar->width() - m_nViewLength) / 2; //开始位置
        for (int i = 0; i < pmList.count(); i++) {
            Platform_ImageItem *label = new Platform_ImageItem(pmList.at(i), false, m_pBack);
            label->setMouseTracking(true);
            label->move(i * (nPixWidget + 1) + m_nStartPoint, 5);
            label->setFixedSize(nPixWidget, npixHeight);

            Platform_ImageItem *label_black = new Platform_ImageItem(pmBlackList.at(i), true, m_pFront);
            label_black->setMouseTracking(true);
            label_black->move(i * (nPixWidget + 1) + m_nStartPoint, 5);
            label_black->setFixedSize(nPixWidget, npixHeight);
        }
        update();
    }
    void clear()
    {
        qDebug() << "ViewProgBar clear";
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
        m_pIndicator->resize(6, 60);
        qDebug() << "ViewProgBar clear end";
    }

    int getViewLength()
    {
        qDebug() << "ViewProgBar getViewLength" << m_nViewLength;
        return m_nViewLength;
    }

    int getStartPoint()
    {
        qDebug() << "ViewProgBar getStartPoint" << m_nStartPoint;
        return m_nStartPoint;
    }

private:
    void changeStyle(bool press)
    {
        qDebug() << "ViewProgBar changeStyle" << press;
        if (!isVisible()) {
            qDebug() << "ViewProgBar changeStyle not visible";
            return;
        }

        if (press) {
            qDebug() << "ViewProgBar changeStyle press";
            m_pIndicator->setPressed(press);
            m_pIndicator->resize(2, 60);

        } else {
            qDebug() << "ViewProgBar changeStyle not press";
            m_pIndicator->setPressed(press);
            m_pIndicator->resize(6, 60);
        }
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
        qDebug() << "ViewProgBar mouseMoveEvent";
        if (!isEnabled()) {
            qDebug() << "ViewProgBar mouseMoveEvent not enabled";
            return;
        }

        if (e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()) {
            qDebug() << "ViewProgBar mouseMoveEvent in range";
            int v = position2progress(e->pos());
            if (e->buttons() & Qt::LeftButton) {
                int distance = (e->pos() -  m_startPos).manhattanLength();
                if (distance >= QApplication::startDragDistance()) {
                    qDebug() << "ViewProgBar mouseMoveEvent startDrag";
                    emit sliderMoved(v);
                    emit hoverChanged(v);
                    emit mousePressed(true);
                    setValue(e->pos().x());
                    setTime(v);
                    repaint();
                }
            } else {
                qDebug() << "ViewProgBar mouseMoveEvent not left button";
                if (m_nVlastHoverValue != v) {
                    qDebug() << "ViewProgBar mouseMoveEvent hoverChanged";
                    emit hoverChanged(v);
                }
                m_nVlastHoverValue = v;
            }
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        qDebug() << "ViewProgBar mousePressEvent";
        if (!m_bPress && e->buttons() == Qt::LeftButton && isEnabled()) {
            qDebug() << "ViewProgBar mousePressEvent left button";
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
        qDebug() << "ViewProgBar mouseReleaseEvent";
        emit mousePressed(false);
        if (m_bPress && isEnabled()) {
            qDebug() << "ViewProgBar mouseReleaseEvent press";
            changeStyle(!m_bPress);
            m_bPress = !m_bPress;
            //鼠标释放时seek视频位置。
            int v = position2progress(e->pos());
            m_pEngine->seekAbsolute(v);
        }

        m_pSliderArrowUp->setVisible(m_bPress);
        setTimeVisible(m_bPress);

        DWidget::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent *e) override
    {
        qDebug() << "ViewProgBar paintEvent";
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
        qDebug() << "ViewProgBar resizeEvent";
        m_pBack->setFixedWidth(this->width());

        DWidget::resizeEvent(event);
    }
private:
    int  position2progress(const QPoint &p)
    {
        qDebug() << "ViewProgBar position2progress";
        int nPosition = 0;

        if (!m_pEngine) {
            return 0;
        }

        if (p.x() < m_nStartPoint) {
            nPosition = m_nStartPoint;
        } else if (p.x() > (m_nViewLength + m_nStartPoint)) {
            nPosition = (m_nViewLength + m_nStartPoint);
        } else {
            nPosition = p.x();
        }

        auto total = m_pEngine->duration();
        int span = static_cast<int>(total * (nPosition - m_nStartPoint) / m_nViewLength);
        return span/* * (p.x())*/;
    }

    void initMember()
    {
        qDebug() << "ViewProgBar initMember";
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
    Platform_IndicatorItem *m_pIndicator;
    Platform_SliderTime *m_pSliderTime;
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

class Platform_ThumbnailPreview: public QWidget
{
    Q_OBJECT
public:
    Platform_ThumbnailPreview()
    {
        qDebug() << "ThumbnailPreview constructor";
        setAttribute(Qt::WA_DeleteOnClose);
        // FIXME(hualet): Qt::Tooltip will cause Dock to show up even
        // the player is in fullscreen mode.
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setObjectName("ThumbnailPreview");
        resize(0, 0);
       
        connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasBlurWindowChanged, this, &Platform_ThumbnailPreview::slotWMChanged);
        m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
        if (m_bIsWM) {
            qDebug() << "ThumbnailPreview constructor m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qDebug() << "ThumbnailPreview constructor not m_bIsWM";
            DStyle::setFrameRadius(this, 0);
        }
        m_shadow_effect = new QGraphicsDropShadowEffect(this);
    }

    void updateWithPreview(const QPixmap &pm, qint64 secs, int rotation)
    {
        qDebug() << "ThumbnailPreview updateWithPreview";
        QPixmap rounded;
        if (m_bIsWM) {
            qDebug() << "ThumbnailPreview updateWithPreview m_bIsWM";
            rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);
        } else {
            qDebug() << "ThumbnailPreview updateWithPreview not m_bIsWM";
            rounded = pm;
        }

        if (rounded.width() == 0) {
            qDebug() << "Return by updateWithPreview rounded.width() == 0";
            return;
        }
        if (rounded.width() > rounded.height()) {
            qDebug() << "ThumbnailPreview updateWithPreview rounded.width() > rounded.height()";
            static int roundedH = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.width()))
                                      * rounded.height());
            QSize size(m_thumbnailFixed, roundedH);
            resizeThumbnail(rounded, size);
        } else {
            qDebug() << "ThumbnailPreview updateWithPreview rounded.width() <= rounded.height()";
            static int roundedW = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.height()))
                                      * rounded.width());
            QSize size(roundedW, m_thumbnailFixed);
            resizeThumbnail(rounded, size);
        }

        QImage image;
        qDebug() << "ThumbnailPreview updateWithPreview image";
        QPalette palette;
        image = rounded.toImage();
        qDebug() << "ThumbnailPreview updateWithPreview image end";
        m_thumbImg = image;
        update();
    }

    void updateWithPreview(const QPoint &pos)
    {
        qDebug() << "ThumbnailPreview updateWithPreview pos" << pos;
        move(pos.x() - this->width() / 2, pos.y() - this->height() + 10);
        if(geometry().isValid()) {
            qDebug() << "ThumbnailPreview updateWithPreview show";
            show();
        }
    }
public slots:
    void slotWMChanged()
    {
        qDebug() << "ThumbnailPreview slotWMChanged";
        m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
        if (m_bIsWM) {
            qDebug() << "ThumbnailPreview slotWMChanged m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qDebug() << "ThumbnailPreview slotWMChanged not m_bIsWM";
            DStyle::setFrameRadius(this, 0);
        }
    }

signals:
    void leavePreview();

protected:
    void paintEvent(QPaintEvent *e) Q_DECL_OVERRIDE{
        // qDebug() << "ThumbnailPreview paintEvent";
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
        // qDebug() << "ThumbnailPreview showEvent";
        QWidget::showEvent(se);
    }

private:
    void resizeThumbnail(QPixmap &pixmap, const QSize &size)
    {
        qDebug() << "ThumbnailPreview resizeThumbnail";
        auto dpr = qApp->devicePixelRatio();
        pixmap.setDevicePixelRatio(dpr);
        pixmap = pixmap.scaled(size * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        int offect = 2;
        if (!m_bIsWM) {
            qDebug() << "ThumbnailPreview resizeThumbnail not m_bIsWM";
            offect = 0;
        }
        qDebug() << "ThumbnailPreview resizeThumbnail offect" << offect;
        this->setFixedWidth(size.width() + offect);
        this->setFixedHeight(size.height() + offect);
        qDebug() << "ThumbnailPreview resizeThumbnail end";
    }

private:
    QImage m_thumbImg;
    int m_thumbnailFixed = 106;
    QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
    bool m_bIsWM{false};
};


void Platform_viewProgBarLoad::initThumb()
{
    qDebug() << "ThumbnailPreview initThumb";
    QLibrary library(SysUtils::libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");

    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr)
    {
        qDebug() << "ThumbnailPreview initThumb return";
        return;
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    qDebug() << "ThumbnailPreview initThumb end";
}

void Platform_viewProgBarLoad::initMember()
{
    qDebug() << "ThumbnailPreview initMember";
    m_pEngine = nullptr;
    m_pParent = nullptr;
    m_pProgBar = nullptr;
    m_pListPixmapMutex = nullptr;
}

void Platform_viewProgBarLoad::loadViewProgBar(QSize size)
{
    qDebug() << "ThumbnailPreview loadViewProgBar";
    int pixWidget =  40;
    int num = int(m_pProgBar->width() / (40 + 1)); //number of thumbnails
#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        qDebug() << "ThumbnailPreview loadViewProgBar CompactMode";
        num = int(m_pProgBar->width() / (26 + 1));
    }
#endif
    qDebug() << "ThumbnailPreview loadViewProgBar num" << num;
    int tmp = (num == 0) ? 0: (m_pEngine->duration() * 1000) / num;

    QList<QPixmap> pmList;
    QList<QPixmap> pmBlackList;

    QTime time(0, 0, 0, 0);
    if (m_pEngine->videoSize().width() > 0 && m_pEngine->videoSize().height() > 0) {
        qDebug() << "ThumbnailPreview loadViewProgBar videoSize" << m_pEngine->videoSize();
        m_video_thumbnailer->thumbnail_size = (static_cast<int>(50 * (m_pEngine->videoSize().width() / m_pEngine->videoSize().height() * 50)
                                                                * qApp->devicePixelRatio()));
    }

    if (m_image_data == nullptr) {
        qDebug() << "ThumbnailPreview loadViewProgBar m_image_data == nullptr";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    }

//    m_video_thumbnailer->seek_time = d.toString("hh:mm:ss").toLatin1().data();
    int length = strlen(time.toString("hh:mm:ss").toLatin1().data());
    memcpy(m_seekTime, time.toString("hh:mm:ss").toLatin1().data(), length + 1);
    m_video_thumbnailer->seek_time = m_seekTime;

    auto url = m_pEngine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    for (auto i = 0; i < num ; i++) {
        if (isInterruptionRequested()) {
            qInfo() << "isInterruptionRequested";
            return;
        }

        time = time.addMSecs(tmp);

//        m_video_thumbnailer->seek_time = d.toString("hh:mm:ss").toLatin1().data();
        memcpy(m_seekTime, time.toString("hh:mm:ss").toLatin1().data(), length + 1);
        m_video_thumbnailer->seek_time = m_seekTime;
        try {

            m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
            auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
            if (img.format() == QImage::Format_Invalid) {
                return;
            }
            auto img_tmp = img.scaledToHeight(50);


            pmList.append(QPixmap::fromImage(img_tmp.copy(img_tmp.size().width() / 2 - 4, 0, pixWidget, 50))); //-2 为了1px的内边框
            QImage img_black = img_tmp.convertToFormat(QImage::Format_Grayscale8);
            pmBlackList.append(QPixmap::fromImage(img_black.copy(img_black.size().width() / 2 - 4, 0, pixWidget, 50)));

        } catch (const std::logic_error &) {
            qDebug() << "ThumbnailPreview loadViewProgBar std::logic_error";
        }
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

    m_pListPixmapMutex->lock();
    m_pParent->addpmList(pmList);
    m_pParent->addpmBlackList(pmBlackList);
    m_pListPixmapMutex->unlock();
    emit sigFinishiLoad(size);
//    emit finished();
    qDebug() << "ThumbnailPreview loadViewProgBar end";
}
/**
 * @brief ToolboxProxy 构造函数
 * @param mainWindow 主窗口
 * @param pPlayerEngine 播放引擎对象指针
 */
Platform_ToolboxProxy::Platform_ToolboxProxy(QWidget *mainWindow, PlayerEngine *proxy)
    : DFloatingWidget(mainWindow),
      m_pMainWindow(static_cast<Platform_MainWindow *>(mainWindow)),
      m_pEngine(proxy)
{
    qDebug() << "Initializing Platform_ToolboxProxy";
    initMember();

    setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
    setContentsMargins(0, 0, 0, 0);
    setAttribute(Qt::WA_NativeWindow);

    m_pPreviewer = new Platform_ThumbnailPreview;
    m_pPreviewTime = new Platform_SliderTime;
    m_pPreviewTime->hide();
    m_mircastWidget = new MircastWidget(mainWindow, proxy);
    m_mircastWidget->hide();
    setup();
    slotThemeTypeChanged();

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,
            this, &Platform_ToolboxProxy::updatePlayState);
    connect(m_mircastWidget, &MircastWidget::updatePlayStatus, this, &Platform_ToolboxProxy::updatePlayState);
    connect(m_mircastWidget, &MircastWidget::updateTime, this, &Platform_ToolboxProxy::updateMircastTime, Qt::QueuedConnection);
    qDebug() << "Platform_ToolboxProxy initialization completed";
}
void Platform_ToolboxProxy::finishLoadSlot(QSize size)
{
    qInfo() << "thumbnail has finished";

    if (m_pmList.isEmpty()) {
        qDebug() << "ThumbnailPreview finishLoadSlot m_pmList.isEmpty()";
        return;
    }
    if (!m_bThumbnailmode) {
        qDebug() << "ThumbnailPreview finishLoadSlot not m_bThumbnailmode";
        return;
    }
    m_pViewProgBar->setViewProgBar(m_pEngine, m_pmList, m_pmBlackList);

    if(CompositingManager::get().platform() == Platform::X86) {
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            PlayItemInfo info = m_pEngine->playlist().currentInfo();
            if (!info.url.isLocalFile()) {
                qDebug() << "ThumbnailPreview finishLoadSlot not info.url.isLocalFile";
                return;
            }
            m_pProgBar_Widget->setCurrentIndex(2);
        }
    }
}

void Platform_ToolboxProxy::setthumbnailmode()
{
    qDebug() << "ThumbnailPreview setthumbnailmode";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview setthumbnailmode Idle";
        return;
    }

    //no thunbnail progress bar is loaded except amd plantform
    m_bThumbnailmode = false;
    updateMovieProgress();
}

void Platform_ToolboxProxy::setup()
{
    qDebug() << "ThumbnailPreview setup";
    QStackedLayout *stacked = new QStackedLayout(this);
    stacked->setContentsMargins(0, 0, 0, 0);
    stacked->setStackingMode(QStackedLayout::StackAll);
    setLayout(stacked);

    this->setBlurBackgroundEnabled(true);
    this->blurBackground()->setRadius(30);
    this->blurBackground()->setBlurEnabled(true);
    this->blurBackground()->setMode(DBlurEffectWidget::GaussianBlur);

    bot_widget = new DBlurEffectWidget(this);
    bot_widget->setObjectName(BOTTOM_WIDGET);
    bot_widget->setBlurRectXRadius(18);
    bot_widget->setBlurRectYRadius(18);
    bot_widget->setRadius(30);
    bot_widget->setBlurEnabled(false);
    bot_widget->setMode(DBlurEffectWidget::GaussianBlur);
    auto type = DGuiApplicationHelper::instance()->themeType();
    THEME_TYPE(type);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &Platform_ToolboxProxy::slotThemeTypeChanged);

    bot_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout *botv = new QVBoxLayout(bot_widget);
    botv->setContentsMargins(0, 0, 0, 0);
    botv->setSpacing(10);

    m_pBotSpec = new QWidget(bot_widget);
    m_pBotSpec->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_pBotSpec->setFixedWidth(width());
    m_pBotSpec->setVisible(false);
    botv->addWidget(m_pBotSpec);
    botv->addStretch();

    m_pBotToolWgt = new QWidget(bot_widget);
    m_pBotToolWgt->setObjectName(BOTTOM_TOOL_BUTTON_WIDGET);
    m_pBotToolWgt->setFixedHeight(TOOLBOX_HEIGHT - 12);
    m_pBotToolWgt->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QHBoxLayout *bot_layout = new QHBoxLayout(m_pBotToolWgt);
    bot_layout->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    bot_layout->setSpacing(0);
    m_pBotToolWgt->setLayout(bot_layout);
    botv->addWidget(m_pBotToolWgt);

    bot_widget->setLayout(botv);
    stacked->addWidget(bot_widget);

    m_pTimeLabel = new QLabel(m_pBotToolWgt);
    m_pTimeLabel->setAlignment(Qt::AlignCenter);
    m_pFullscreentimelable = new QLabel("");
    m_pFullscreentimelable->setAttribute(Qt::WA_DeleteOnClose);
    m_pFullscreentimelable->setForegroundRole(DPalette::Text);

    DFontSizeManager::instance()->bind(m_pTimeLabel, DFontSizeManager::T6);
    m_pTimeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_pFullscreentimelable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(m_pFullscreentimelable, DFontSizeManager::T6);
    m_pTimeLabelend = new QLabel(m_pBotToolWgt);
    m_pTimeLabelend->setAlignment(Qt::AlignCenter);
    m_pFullscreentimelableend = new QLabel("");
    m_pFullscreentimelableend->setAttribute(Qt::WA_DeleteOnClose);
    m_pFullscreentimelableend->setForegroundRole(DPalette::Text);
    m_pTimeLabelend->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(m_pTimeLabelend, DFontSizeManager::T6);
    m_pFullscreentimelableend->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(m_pFullscreentimelableend, DFontSizeManager::T6);

    m_pProgBar = new DMRSlider(m_pBotToolWgt);
    m_pProgBar->setObjectName(MOVIE_PROGRESS_WIDGET);
    m_pProgBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_pProgBar->slider()->setObjectName(PROGBAR_SLIDER);
    m_pProgBar->slider()->setAccessibleName(PROGBAR_SLIDER);
    m_pProgBar->slider()->setOrientation(Qt::Horizontal);
    m_pProgBar->slider()->setFocusPolicy(Qt::TabFocus);
    m_pProgBar->slider()->setRange(0, 100);
    m_pProgBar->setValue(0);
    m_pProgBar->setEnableIndication(m_pEngine->state() != PlayerEngine::Idle);

    connect(m_pProgBar, &DMRSlider::sigUnsupported, this, &Platform_ToolboxProxy::sigUnsupported);
    connect(m_pPreviewer, &Platform_ThumbnailPreview::leavePreview, this, &Platform_ToolboxProxy::slotLeavePreview);
    connect(&Settings::get(), &Settings::baseChanged, this, &Platform_ToolboxProxy::setthumbnailmode);
    connect(m_pEngine, &PlayerEngine::siginitthumbnailseting, this, &Platform_ToolboxProxy::setthumbnailmode);

    //刷新显示预览当前时间的label
    connect(m_pProgBar, &DMRSlider::hoverChanged, this, &Platform_ToolboxProxy::progressHoverChanged);
    connect(m_pProgBar, &DMRSlider::sliderMoved, this, &Platform_ToolboxProxy::progressHoverChanged);
    connect(m_pProgBar, &DMRSlider::leave, this, &Platform_ToolboxProxy::slotHidePreviewTime);

    connect(m_pProgBar, &DMRSlider::sliderPressed, this, &Platform_ToolboxProxy::slotSliderPressed);
    connect(m_pProgBar, &DMRSlider::sliderReleased, this, &Platform_ToolboxProxy::slotSliderReleased);
    connect(&Settings::get(), &Settings::baseMuteChanged, this, &Platform_ToolboxProxy::slotBaseMuteChanged);

    m_pViewProgBar = new Platform_ViewProgBar(m_pProgBar, m_pBotToolWgt);


    //刷新显示预览当前时间的label
    connect(m_pViewProgBar, &Platform_ViewProgBar::hoverChanged, this, &Platform_ToolboxProxy::progressHoverChanged);
    connect(m_pViewProgBar, &Platform_ViewProgBar::leave, this, &Platform_ToolboxProxy::slotHidePreviewTime);
    connect(m_pViewProgBar, &Platform_ViewProgBar::mousePressed, this, &Platform_ToolboxProxy::updateTimeVisible);

    QSignalMapper *signalMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped),
            this, &Platform_ToolboxProxy::buttonClicked);
#else
    connect(signalMapper, &QSignalMapper::mappedString,
            this, &Platform_ToolboxProxy::buttonClicked);
#endif

    _mid = new QHBoxLayout(m_pBotToolWgt);
    _mid->setContentsMargins(0, 0, 0, 0);
    _mid->setSpacing(0);
    _mid->setAlignment(Qt::AlignLeft);
    bot_layout->addLayout(_mid);

    QHBoxLayout *time = new QHBoxLayout(m_pBotToolWgt);
    time->setContentsMargins(11, 9, 11, 9);
    time->setSpacing(0);
    time->setAlignment(Qt::AlignLeft);
    bot_layout->addLayout(time);
    time->addWidget(m_pTimeLabel);
    QHBoxLayout *progBarspec = new QHBoxLayout(m_pBotToolWgt);
    progBarspec->setContentsMargins(0, 5, 0, 0);
    progBarspec->setSpacing(0);
    progBarspec->setAlignment(Qt::AlignHCenter);

    m_pProgBar_Widget = new QStackedWidget(m_pBotToolWgt);
    m_pProgBar_Widget->setObjectName(PROGBAR_WIDGET);
    m_pProgBar_Widget->setAccessibleName(PROGBAR_WIDGET);
    m_pProgBar_Widget->setContentsMargins(0, 0, 0, 0);
    m_pProgBar_Widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_pProgBarspec = new DWidget(m_pProgBar_Widget);
    m_pProgBarspec->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_pProgBar_Widget->addWidget(m_pProgBarspec);
    m_pProgBar_Widget->addWidget(m_pProgBar);
    m_pProgBar_Widget->addWidget(m_pViewProgBar);
    m_pProgBar_Widget->setCurrentIndex(0);
    progBarspec->addWidget(m_pProgBar_Widget);
    bot_layout->addLayout(progBarspec);

    QHBoxLayout *timeend = new QHBoxLayout(m_pBotToolWgt);
    timeend->setContentsMargins(10, 10, 10, 10);
    timeend->setSpacing(0);
    timeend->setAlignment(Qt::AlignRight);
    bot_layout->addLayout(timeend);
    timeend->addWidget(m_pTimeLabelend);

    m_pPalyBox = new DButtonBox(m_pBotToolWgt);
    m_pPalyBox->setFixedWidth(120);
    m_pPalyBox->setObjectName(PLAY_BUTTOB_BOX);
    m_pPalyBox->setFocusPolicy(Qt::NoFocus);
    _mid->addWidget(m_pPalyBox);
    _mid->setAlignment(m_pPalyBox, Qt::AlignLeft);
    QList<DButtonBoxButton *> list;

    m_pPrevBtn = new ButtonBoxButton("", this);
    m_pPlayBtn = new ButtonBoxButton("", this);
    m_pNextBtn = new ButtonBoxButton("", this);

    m_pPrevBtn->setIcon(QIcon::fromTheme("dcc_last", QIcon(":/icons/deepin/builtin/light/normal/last_normal.svg")));
    m_pPrevBtn->setIconSize(QSize(36, 36));
    m_pPrevBtn->setFixedSize(40, 50);
    m_pPrevBtn->setObjectName(PREV_BUTTON);
    m_pPrevBtn->setAccessibleName(PREV_BUTTON);
    m_pPrevBtn->setFocusPolicy(Qt::TabFocus);
    connect(m_pPrevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pPrevBtn, "prev");
    list.append(m_pPrevBtn);

    m_pPlayBtn->setIcon(QIcon::fromTheme("dcc_play", QIcon(":/icons/deepin/builtin/light/normal/play_normal.svg")));
    m_pPlayBtn->setIconSize(QSize(36, 36));
    m_pPlayBtn->setFixedSize(40, 50);
    m_pPlayBtn->setFocusPolicy(Qt::TabFocus);
    m_pPlayBtn->setObjectName(PLAY_BUTTON);
    m_pPlayBtn->setAccessibleName(PLAY_BUTTON);
    connect(m_pPlayBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pPlayBtn, "play");
    list.append(m_pPlayBtn);

    m_pNextBtn->setIcon(QIcon::fromTheme("dcc_next", QIcon(":/icons/deepin/builtin/light/normal/next_normal.svg")));
    m_pNextBtn->setIconSize(QSize(36, 36));
    m_pNextBtn->setFixedSize(40, 50);
    m_pNextBtn->setFocusPolicy(Qt::TabFocus);
    m_pNextBtn->setObjectName(NEXT_BUTTON);
    m_pNextBtn->setAccessibleName(NEXT_BUTTON);
    connect(m_pNextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pNextBtn, "next");
    list.append(m_pNextBtn);
    m_pPalyBox->setButtonList(list, false);

    _right = new QHBoxLayout(m_pBotToolWgt);
    _right->setContentsMargins(0, 0, 0, 0);
    _right->setSizeConstraint(QLayout::SetFixedSize);
    _right->setSpacing(0);
    bot_layout->addLayout(_right);

    m_pFullScreenBtn = new ToolButton(m_pBotToolWgt);
    m_pFullScreenBtn->setObjectName(FS_BUTTON);
    m_pFullScreenBtn->setAccessibleName(FS_BUTTON);
    m_pFullScreenBtn->setFocusPolicy(Qt::TabFocus);
    m_pFullScreenBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
    m_pFullScreenBtn->setIconSize(QSize(36, 36));
    m_pFullScreenBtn->setFixedSize(50, 50);
    m_pFullScreenBtn->initToolTip();
    connect(m_pFullScreenBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pFullScreenBtn, "fs");

    m_pVolBtn = new VolumeButton(m_pBotToolWgt);
    m_pVolBtn->installEventFilter(this);
    m_pVolBtn->setFixedSize(50, 50);
    m_pVolBtn->setFocusPolicy(Qt::TabFocus);
    m_pVolBtn->setObjectName(VOLUME_BUTTON);
    m_pVolBtn->setAccessibleName(VOLUME_BUTTON);

    m_pVolSlider = new Platform_VolumeSlider(m_pMainWindow, m_pMainWindow);
    m_pVolSlider->setObjectName(VOLUME_SLIDER_WIDGET);

    connect(m_pVolBtn, &VolumeButton ::sigUnsupported, this, &Platform_ToolboxProxy::sigUnsupported);
    connect(m_pVolBtn, &VolumeButton::clicked, this, &Platform_ToolboxProxy::slotVolumeButtonClicked);
    connect(m_pVolBtn, &VolumeButton::leaved, m_pVolSlider, &Platform_VolumeSlider::delayedHide);
    connect(m_pVolSlider, &Platform_VolumeSlider::sigVolumeChanged, this, &Platform_ToolboxProxy::slotVolumeChanged);
    connect(m_pVolSlider, &Platform_VolumeSlider::sigMuteStateChanged, this, &Platform_ToolboxProxy::slotMuteStateChanged);
    connect(m_pVolBtn, &VolumeButton::requestVolumeUp, m_pVolSlider, &Platform_VolumeSlider::volumeUp);
    connect(m_pVolBtn, &VolumeButton::requestVolumeDown, m_pVolSlider, &Platform_VolumeSlider::volumeDown);

    m_pVolSlider->initVolume();

    _right->addWidget(m_pFullScreenBtn);
    _right->addSpacing(10);
    _right->addWidget(m_pVolBtn);
    _right->addSpacing(10);

    m_pMircastBtn = new ToolButton(m_pBotToolWgt);
    m_pMircastBtn->setIcon(QIcon::fromTheme("dcc_mircast", QIcon(":/resources/icons/mircast/mircast.svg")));
    m_pMircastBtn->setIconSize(QSize(24, 24));
    m_pMircastBtn->installEventFilter(this);
    m_pMircastBtn->setCheckable(true);
    m_pMircastBtn->setFixedSize(50, 50);
    m_pMircastBtn->setFocusPolicy(Qt::TabFocus);
    m_pMircastBtn->initToolTip();
    m_pMircastBtn->setObjectName(MIRVAST_BUTTON);
    m_pMircastBtn->setAccessibleName(MIRVAST_BUTTON);
    connect(m_pMircastBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pMircastBtn, "mircast");
    connect(m_mircastWidget, &MircastWidget::mircastState, this, &Platform_ToolboxProxy::slotUpdateMircast);

    _right->addWidget(m_pMircastBtn);
    _right->addSpacing(10);

    m_pListBtn = new ToolButton(m_pBotToolWgt);
    m_pListBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    m_pListBtn->setIconSize(QSize(36, 36));
    m_pListBtn->setFocusPolicy(Qt::TabFocus);
    m_pListBtn->setFixedSize(50, 50);
    m_pListBtn->initToolTip();
    m_pListBtn->setCheckable(true);
    m_pListBtn->setObjectName(PLAYLIST_BUTTON);
    m_pListBtn->setAccessibleName(PLAYLIST_BUTTON);
    m_pListBtn->installEventFilter(this);

    connect(m_pListBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(m_pListBtn, "list");
    _right->addWidget(m_pListBtn);

    //将进度条的Tab键次序移动到nextBtn之后
    setTabOrder(m_pNextBtn, m_pProgBar->slider());

    // these tooltips is not used due to deepin ui design
    //lmh0910wayland下用这一套tooltip
    Platform_TooltipHandler *th = new Platform_TooltipHandler(this);
    QWidget *btns[] = {
        m_pPlayBtn, m_pPrevBtn, m_pNextBtn, m_pFullScreenBtn, m_pMircastBtn, m_pListBtn
    };
    QString hints[] = {
        tr("Play/Pause"), tr("Previous"), tr("Next"),
        tr("Fullscreen"), tr("Miracast"), tr("Playlist")
    };
    QString attrs[] = {
        "play", "prev", "next",
        "fs", "mir", "list"
    };

    for (unsigned int i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
        if (i < 3) { //first three buttons prev/play/next
            btns[i]->setToolTip(hints[i]);
            Tip *t = new Tip(QPixmap(), hints[i], parentWidget());
            t->setProperty("for", QVariant::fromValue<QWidget *>(btns[i]));
            btns[i]->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
            btns[i]->installEventFilter(th);
        } else {
            ToolButton *btn = dynamic_cast<ToolButton *>(btns[i]);
            btn->setTooTipText(hints[i]);
            btn->setProperty("TipId", attrs[i]);
            connect(btn, &ToolButton::entered, this, &Platform_ToolboxProxy::buttonEnter);
            connect(btn, &ToolButton::leaved, this, &Platform_ToolboxProxy::buttonLeave);
        }
    }
    qDebug() << "UI setup end, connect signals";

    connect(m_pEngine, &PlayerEngine::stateChanged, this, &Platform_ToolboxProxy::updatePlayState);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &Platform_ToolboxProxy::updateButtonStates);   // 控件状态变化由updateButtonStates统一处理
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &Platform_ToolboxProxy::slotFileLoaded);
    connect(m_pEngine, &PlayerEngine::elapsedChanged, this, &Platform_ToolboxProxy::slotElapsedChanged);
    connect(m_pEngine, &PlayerEngine::updateDuration, this, &Platform_ToolboxProxy::slotElapsedChanged);

    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &Platform_ToolboxProxy::updateFullState);

    connect(m_pEngine, &PlayerEngine::tracksChanged, this, &Platform_ToolboxProxy::updateButtonStates);
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &Platform_ToolboxProxy::updateButtonStates);
    connect(&m_pEngine->playlist(), &PlaylistModel::countChanged, this, &Platform_ToolboxProxy::updateButtonStates);
    connect(m_pMainWindow, &Platform_MainWindow::initChanged, this, &Platform_ToolboxProxy::updateButtonStates);

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        m_pBotToolWgt->setFixedHeight((TOOLBOX_HEIGHT - 12)*0.66);
        progBarspec->setContentsMargins(0, 0, 0, 0);
        m_pBotSpec->setFixedHeight(20);
        m_pPalyBox->setFixedWidth(79);
        m_pPrevBtn->setIconSize(QSize(24, 24));
        m_pPrevBtn->setFixedSize(26, 33);
        m_pPlayBtn->setIconSize(QSize(24, 24));
        m_pPlayBtn->setFixedSize(26, 33);
        m_pNextBtn->setIconSize(QSize(24, 24));
        m_pNextBtn->setFixedSize(26, 33);
        m_pFullScreenBtn->setIconSize(QSize(24, 24));
        m_pFullScreenBtn->setFixedSize(33, 33);
        m_pVolBtn->setFixedSize(33, 33);
        m_pMircastBtn->setIconSize(QSize(16, 16));
        m_pMircastBtn->setFixedSize(33, 33);
        m_pListBtn->setIconSize(QSize(24, 24));
        m_pListBtn->setFixedSize(33, 33);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            m_pBotToolWgt->setFixedHeight(TOOLBOX_HEIGHT - 12);
            m_pBotSpec->setFixedHeight(30);
            m_pPalyBox->setFixedWidth(120);
            m_pPrevBtn->setIconSize(QSize(36, 36));
            m_pPrevBtn->setFixedSize(40, 50);
            m_pPlayBtn->setIconSize(QSize(36, 36));
            m_pPlayBtn->setFixedSize(40, 50);
            m_pNextBtn->setIconSize(QSize(36, 36));
            m_pNextBtn->setFixedSize(40, 50);
            m_pFullScreenBtn->setIconSize(QSize(36, 36));
            m_pFullScreenBtn->setFixedSize(50, 50);
            m_pVolBtn->setFixedSize(50, 50);
            m_pMircastBtn->setIconSize(QSize(24, 24));
            m_pMircastBtn->setFixedSize(50, 50);
            m_pListBtn->setIconSize(QSize(36, 36));
            m_pListBtn->setFixedSize(50, 50);
        } else {
            m_pBotToolWgt->setFixedHeight((TOOLBOX_HEIGHT - 12)*0.66);
            m_pBotSpec->setFixedHeight(20);
            m_pPalyBox->setFixedWidth(79);
            m_pPrevBtn->setIconSize(QSize(24, 24));
            m_pPrevBtn->setFixedSize(26, 33);
            m_pPlayBtn->setIconSize(QSize(24, 24));
            m_pPlayBtn->setFixedSize(26, 33);
            m_pNextBtn->setIconSize(QSize(24, 24));
            m_pNextBtn->setFixedSize(26, 33);
            m_pFullScreenBtn->setIconSize(QSize(24, 24));
            m_pFullScreenBtn->setFixedSize(33, 33);
            m_pVolBtn->setFixedSize(33, 33);
            m_pMircastBtn->setIconSize(QSize(16, 16));
            m_pMircastBtn->setFixedSize(33, 33);
            m_pListBtn->setIconSize(QSize(24, 24));
            m_pListBtn->setFixedSize(33, 33);
        }
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            if (m_bThumbnailmode) {  //如果进度条为胶片模式，重新加载缩略图并显示
                if(CompositingManager::get().platform() == Platform::X86) {
                    updateThumbnail();
                }
                updateMovieProgress();
            }
            m_pProgBar_Widget->setCurrentIndex(1);
        }
    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, progBarspec, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            progBarspec->setContentsMargins(0, 5, 0, 0);
        } else {
            progBarspec->setContentsMargins(0, 0, 0, 0);
        }
    });
#endif

    updatePlayState();
    updateFullState();
    updateButtonStates();

    if(CompositingManager::get().platform() != Platform::X86) {
        qDebug() << "ThumbnailPreview setup connect applicationStateChanged";
        connect(qApp, &QGuiApplication::applicationStateChanged, this, &Platform_ToolboxProxy::slotApplicationStateChanged);
    }
    qDebug() << "ThumbnailPreview setup end";
}

void Platform_ToolboxProxy::updateThumbnail()
{
    qDebug() << "ThumbnailPreview updateThumbnail";
    disconnect(m_pWorker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));

    if (m_pEngine->currFileIsAudio()) {
        qDebug() << "ThumbnailPreview updateThumbnail currFileIsAudio";
        return;
    }

    qDebug() << "ThumbnailPreview updateThumbnail worker" << m_pWorker;
    QTimer::singleShot(1000, this, &Platform_ToolboxProxy::slotUpdateThumbnailTimeOut);
    qDebug() << "ThumbnailPreview updateThumbnail end";
}

void Platform_ToolboxProxy::updatePreviewTime(qint64 secs, const QPoint &pos)
{
    qDebug() << "ThumbnailPreview updatePreviewTime";
    QTime time(0, 0, 0);
    QString strTime = time.addSecs(static_cast<int>(secs)).toString("hh:mm:ss");
    m_pPreviewTime->setTime(strTime);
    m_pPreviewTime->show(pos.x(), pos.y() + 14);
    qDebug() << "ThumbnailPreview updatePreviewTime end";
}

void Platform_ToolboxProxy::initMember()
{
    qDebug() << "ThumbnailPreview initMember";
    m_pmList.clear();
    m_pmBlackList.clear();

    m_pPlaylist = nullptr;

    m_pProgBarspec = nullptr;
    m_pBotSpec = nullptr;
    m_pBotToolWgt = nullptr;
    m_pProgBar_Widget = nullptr;
    bot_widget = nullptr;

    _mid = nullptr;
    _right = nullptr;

    m_pFullscreentimelable = nullptr;
    m_pFullscreentimelableend = nullptr;
    m_pTimeLabel = nullptr;
    m_pTimeLabelend = nullptr;
    m_pViewProgBar = nullptr;
    m_pProgBar = nullptr;
    m_pPreviewer = nullptr;
    m_pPreviewTime = nullptr;
    m_mircastWidget = nullptr;

    m_pPlayBtn = nullptr;
    m_pPrevBtn = nullptr;
    m_pNextBtn = nullptr;
    m_pPalyBox = nullptr;
    m_pVolBtn = nullptr;
    m_pListBtn = nullptr;
    m_pFullScreenBtn = nullptr;

    m_pPlayBtnTip = nullptr;
    m_pPrevBtnTip = nullptr;
    m_pNextBtnTip = nullptr;
    m_pFullScreenBtnTip = nullptr;
    m_pListBtnTip = nullptr;

    m_pWorker = nullptr;
    m_pPaOpen = nullptr;
    m_pPaClose = nullptr;

    m_nClickTime = 0;
    m_processAdd = 0.0;

    m_bMouseFlag = false;
    m_bMousePree = false;
    m_bThumbnailmode = false;
    m_bAnimationFinash = true;
    m_bCanPlay = false;
    m_bSetListBtnFocus = false;
    qDebug() << "ThumbnailPreview initMember end";
}

/**
 * @brief closeAnyPopup 关闭所有弹窗效果
 */
void Platform_ToolboxProxy::closeAnyPopup()
{
    qInfo() << "Closing all popup windows";
    if (m_pPreviewer->isVisible()) {
        m_pPreviewer->hide();
        qInfo() << "Previewer hidden";
    }

    if (m_pPreviewTime->isVisible()) {
        m_pPreviewTime->hide();
        qInfo() << "Preview time hidden";
    }

    if (m_pVolSlider->isVisible()) {
        m_pVolSlider->stopTimer();
        m_pVolSlider->hide();
        qInfo() << "Volume slider hidden";
    }
}
/**
 * @brief anyPopupShown 是否存在一些弹出显示窗口
 * @return true时为有，false为无
 */
bool Platform_ToolboxProxy::anyPopupShown() const
{
    qDebug() << "ThumbnailPreview anyPopupShown";
    //返回鼠标悬停缩略图、鼠标悬停时间弹窗、音量弹窗是否有弹出
    return m_pPreviewer->isVisible() || m_pPreviewTime->isVisible() || m_pVolSlider->isVisible();
}

void Platform_ToolboxProxy::updateHoverPreview(const QUrl &url, int secs)
{
    qDebug() << "ThumbnailPreview updateHoverPreview";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview updateHoverPreview Idle";
        return;
    }

    if (m_pEngine->playlist().currentInfo().url != url) {
        qDebug() << "ThumbnailPreview updateHoverPreview url not match";
        return;
    }

    if (!Settings::get().isSet(Settings::PreviewOnMouseover)) {
        qDebug() << "ThumbnailPreview updateHoverPreview not Settings::PreviewOnMouseover";
        return;
    }

    if (m_pVolSlider->isVisible()) {
        qDebug() << "ThumbnailPreview updateHoverPreview VolSlider isVisible";
        return;
    }

    const PlayItemInfo &pif = m_pEngine->playlist().currentInfo();
    if (!pif.url.isLocalFile()) {
        qDebug() << "ThumbnailPreview updateHoverPreview not LocalFile";
        return;
    }

    const QString &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        qDebug() << "ThumbnailPreview updateHoverPreview not exists";
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
        return;
    }

    if (!m_bMouseFlag) {
        qDebug() << "ThumbnailPreview updateHoverPreview not m_bMouseFlag";
        return;
    }

    int nPosition = 0;
    qint64 nDuration = m_pEngine->duration();
    QPoint showPoint;

    if(nDuration<=0)
    {
        qDebug() << "ThumbnailPreview updateHoverPreview nDuration <= 0";
        return;
    }

    if (m_pProgBar->isVisible()) {
        qDebug() << "ThumbnailPreview updateHoverPreview m_pProgBar isVisible";
        nPosition = (secs * m_pProgBar->slider()->width()) / nDuration;
        showPoint = m_pProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    } else {
        qDebug() << "ThumbnailPreview updateHoverPreview m_pProgBar isNotVisible";
        nPosition = secs * m_pViewProgBar->getViewLength() / nDuration + m_pViewProgBar->getStartPoint();
        showPoint = m_pViewProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    }

    QPixmap pm = Platform_ThumbnailWorker::get().getThumb(url, secs);


    if (!pm.isNull()) {
        qDebug() << "ThumbnailPreview updateHoverPreview pm is not null";
        QPoint point { showPoint.x(), showPoint.y() };
        m_pPreviewer->updateWithPreview(pm, secs, m_pEngine->videoRotation());
    }
    qDebug() << "ThumbnailPreview updateHoverPreview end";
}

void Platform_ToolboxProxy::waitPlay()
{
    qDebug() << "ThumbnailPreview waitPlay";
    if (m_pPlayBtn) {
        qDebug() << "ThumbnailPreview waitPlay m_pPlayBtn";
        m_pPlayBtn->setEnabled(false);
    }
    if (m_pPrevBtn) {
        qDebug() << "ThumbnailPreview waitPlay m_pPrevBtn";
        m_pPrevBtn->setEnabled(false);
    }
    if (m_pNextBtn) {
        qDebug() << "ThumbnailPreview waitPlay m_pNextBtn";
        m_pNextBtn->setEnabled(false);
    }
    QTimer::singleShot(500, [ = ] {
        if (m_pPlayBtn)
        {
            m_pPlayBtn->setEnabled(true);
        }
        if (m_pPrevBtn && m_pEngine->playlist().count() > 1)
        {
            m_pPrevBtn->setEnabled(true);
        }
        if (m_pNextBtn && m_pEngine->playlist().count() > 1)
        {
            m_pNextBtn->setEnabled(true);
        }
    });
    qDebug() << "ThumbnailPreview waitPlay end";
}

void Platform_ToolboxProxy::slotThemeTypeChanged()
{
    qDebug() << "ThumbnailPreview slotThemeTypeChanged";
    QPalette textPalette;
    bool bRawFormat = false;
    auto type = DGuiApplicationHelper::instance()->themeType();
    THEME_TYPE(type);

    // 组合按钮无边框
    QColor framecolor("#FFFFFF");
    framecolor.setAlphaF(0.00);
    QString rStr;
    if (type == DGuiApplicationHelper::LightType) {
        qDebug() << "ThumbnailPreview slotThemeTypeChanged LightType";
        textPalette.setColor(QPalette::WindowText, QColor(0, 0, 0, 40));   // 浅色背景下时长显示置灰
        textPalette.setColor(QPalette::Text, QColor(0, 0, 0, 40));

        QColor maskColor(247, 247, 247);
        maskColor.setAlphaF(0.60);
        rStr = "light";

        DPalette pa;
        pa = m_pFullScreenBtn->palette();
        pa.setColor(DPalette::Light, QColor("#FFFFFF"));
        pa.setColor(DPalette::Dark, QColor("#FFFFFF"));
        pa.setColor(DPalette::ButtonText, QColor(Qt::black));
        // 单个按钮边框
        QColor btnframecolor("#000000");
        btnframecolor.setAlphaF(0.00);
        pa.setColor(DPalette::FrameBorder, btnframecolor);
        // 取消阴影
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        DGuiApplicationHelper::instance()->setPalette(m_pFullScreenBtn, pa);
        DGuiApplicationHelper::instance()->setPalette(m_pVolBtn, pa);
        DGuiApplicationHelper::instance()->setPalette(m_pListBtn, pa);
#else
        m_pFullScreenBtn->setPalette(pa);
        m_pVolBtn->setPalette(pa);
        m_pListBtn->setPalette(pa);
#endif

        DPalette pl = m_pPalyBox ->palette();
        pl.setColor(DPalette::Button, QColor("#FFFFFF"));
        //这个地方会导致按钮setdisable设置失效，按钮无法置灰
//        pl.setColor(DPalette::ButtonText, QColor(Qt::black));
        pl.setColor(DPalette::FrameBorder, framecolor);
        pl.setColor(DPalette::Shadow, framecolor);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        DGuiApplicationHelper::instance()->setPalette(m_pPalyBox, pl);
#else
        m_pPalyBox->setPalette(pl);
#endif
    } else {
        qDebug() << "ThumbnailPreview slotThemeTypeChanged DarkType";
        textPalette.setColor(QPalette::WindowText, QColor(255, 255, 255, 40));   // 深色背景下时长显示置灰
        textPalette.setColor(QPalette::Text, QColor(255, 255, 255, 40));

        QColor maskColor(32, 32, 32);
        maskColor.setAlphaF(0.80);
        rStr = "dark";

        DPalette pa;
        pa = m_pFullScreenBtn->palette();
        QColor btnMaskColor("#000000");
        btnMaskColor.setAlphaF(0.30);
        pa.setColor(DPalette::Light, btnMaskColor);
        pa.setColor(DPalette::Dark, btnMaskColor);
        pa.setColor(DPalette::ButtonText, QColor("#c5cfe0"));
        pa.setColor(DPalette::FrameBorder, framecolor);
        // 取消阴影
        pa.setColor(DPalette::Shadow, framecolor);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)    
        DGuiApplicationHelper::instance()->setPalette(m_pFullScreenBtn, pa);
        DGuiApplicationHelper::instance()->setPalette(m_pVolBtn, pa);
        DGuiApplicationHelper::instance()->setPalette(m_pListBtn, pa);
#else
        m_pFullScreenBtn->setPalette(pa);
        m_pVolBtn->setPalette(pa);
        m_pListBtn->setPalette(pa);
#endif
        DPalette pl = m_pPalyBox ->palette();
        QColor btnColor("#000000");
        btnColor.setAlphaF(0.60);
        pl.setColor(DPalette::Button, btnColor);
//        pl.setColor(DPalette::ButtonText, QColor("#c5cfe0"));
        pl.setColor(DPalette::FrameBorder, framecolor);
        pl.setColor(DPalette::Shadow, framecolor);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        DGuiApplicationHelper::instance()->setPalette(m_pPalyBox, pl);
#else
        m_pPalyBox->setPalette(pl);
#endif
    }

    if(m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview slotThemeTypeChanged not Idle";
        bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
        if(bRawFormat && !m_pEngine->currFileIsAudio()) {
            qDebug() << "ThumbnailPreview slotThemeTypeChanged bRawFormat && !m_pEngine->currFileIsAudio";
            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);

            m_pVolBtn->setButtonEnable(false);
        }
        else if (bRawFormat) {
            qDebug() << "ThumbnailPreview slotThemeTypeChanged bRawFormat";
            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);
        }
        else {
            qDebug() << "ThumbnailPreview slotThemeTypeChanged else";
            textPalette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
            textPalette.setColor(QPalette::Text, DApplication::palette().text().color());

            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);

            m_pVolBtn->setButtonEnable(true);
        }
    } else {
        qDebug() << "ThumbnailPreview slotThemeTypeChanged Idle";
        textPalette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
        textPalette.setColor(QPalette::Text, DApplication::palette().text().color());

        m_pTimeLabel->setPalette(textPalette);
        m_pTimeLabelend->setPalette(textPalette);
        m_pFullscreentimelable->setPalette(textPalette);
        m_pFullscreentimelableend->setPalette(textPalette);

        m_pVolBtn->setButtonEnable(true);
    }
    qDebug() << "ThumbnailPreview slotThemeTypeChanged end";
}

void Platform_ToolboxProxy::slotLeavePreview()
{
    qDebug() << "ThumbnailPreview slotLeavePreview";
    auto pos = m_pProgBar->mapFromGlobal(QCursor::pos());
    if (!m_pProgBar->geometry().contains(pos)) {
        qDebug() << "ThumbnailPreview slotLeavePreview not contains";
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
        m_pProgBar->forceLeave();
    }
    qDebug() << "ThumbnailPreview slotLeavePreview end";
}

void Platform_ToolboxProxy::slotHidePreviewTime()
{
    qDebug() << "ThumbnailPreview slotHidePreviewTime";
    m_pPreviewer->hide();
    m_pPreviewTime->hide();
    m_bMouseFlag = false;
    qDebug() << "ThumbnailPreview slotHidePreviewTime end";
}

void Platform_ToolboxProxy::slotSliderPressed()
{
    m_bMousePree = true;
    qDebug() << "ThumbnailPreview slotSliderPressed end";
}

void Platform_ToolboxProxy::slotSliderReleased()
{
    qDebug() << "ThumbnailPreview slotSliderReleased";
    m_bMousePree = false;
    if (m_mircastWidget->getMircastState() == MircastWidget::Screening)
        m_mircastWidget->slotSeekMircast(m_pProgBar->slider()->sliderPosition());
    else
        m_pEngine->seekAbsolute(m_pProgBar->slider()->sliderPosition());
    qDebug() << "ThumbnailPreview slotSliderReleased end";
}

void Platform_ToolboxProxy::slotBaseMuteChanged(QString sk, const QVariant &/*val*/)
{
    qDebug() << "ThumbnailPreview slotBaseMuteChanged";
    if (sk == "base.play.mousepreview") {
        qDebug() << "ThumbnailPreview slotBaseMuteChanged base.play.mousepreview";
        m_pProgBar->setEnableIndication(m_pEngine->state() != PlayerEngine::Idle);
    }
    qDebug() << "ThumbnailPreview slotBaseMuteChanged end";
}

void Platform_ToolboxProxy::slotVolumeButtonClicked()
{
    qDebug() << "ThumbnailPreview slotVolumeButtonClicked";
    //与其他按键保持一致，工具栏隐藏时不响应
    if (!isVisible()) {
        qDebug() << "ThumbnailPreview slotVolumeButtonClicked not isVisible";
        return;
    }
    m_pVolBtn->hideTip();
    if (m_pVolSlider->getsliderstate())
        return;
    /*
     * 设置-2为已经完成第一次打开设置音量
     * -1为初始化数值
     * 大于等于零表示为已完成初始化
     */
    if (!m_pVolSlider->isVisible()) {
        qDebug() << "ThumbnailPreview slotVolumeButtonClicked not isVisible";
        m_pVolSlider->adjustSize();
        m_pVolSlider->show();
        m_pVolSlider->raise();
    } else {
        qDebug() << "ThumbnailPreview slotVolumeButtonClicked isVisible";
        m_pVolSlider->hide();
    }
    qDebug() << "ThumbnailPreview slotVolumeButtonClicked end";
}

void Platform_ToolboxProxy::slotFileLoaded()
{
    qDebug() << "ThumbnailPreview slotFileLoaded";
    m_pProgBar->slider()->setRange(0, static_cast<int>(m_pEngine->duration()));
    m_pProgBar_Widget->setCurrentIndex(1);
    m_pPreviewer->setFixedSize(0, 0);
    update();
    //正在投屏时如果当前播放为音频直接播放下一首。
    if(m_pEngine->currFileIsAudio()&&m_mircastWidget->getMircastState() != MircastWidget::Idel) {
        qDebug() << "ThumbnailPreview slotFileLoaded currFileIsAudio && m_mircastWidget->getMircastState != Idel";
        //如果全是音频文件则退出投屏
        bool isAllAudio = true;
        QString sNextVideoName;
        int nNextIndex = -1;
        QList<PlayItemInfo> lstItemInfo = m_pEngine->getplaylist()->items();
        for(int i = 0; i < lstItemInfo.count(); i++) {
            PlayItemInfo iteminfo = lstItemInfo.at(i);
            if(iteminfo.mi.vCodecID != -1) {
                isAllAudio = false;
                if(sNextVideoName.isNull()) {
                    sNextVideoName = iteminfo.mi.filePath;
                    nNextIndex = i;
                    break;
                }
            }
        }
        if(isAllAudio) {
            m_pMainWindow->slotExitMircast();
            qDebug() << "ThumbnailPreview slotFileLoaded isAllAudio, return";
            return;
        }
        QString sCurPath = m_pEngine->getplaylist()->currentInfo().mi.filePath;
        int nIndex = -1;
        for(int i = 0; i < lstItemInfo.count(); i++) {
            PlayItemInfo iteminfo = lstItemInfo.at(i);
            if(iteminfo.mi.filePath == sCurPath) {
                nIndex = i;
                break;
            }
        }
        if(nIndex == -1) {
            qDebug() << "ThumbnailPreview slotFileLoaded nIndex == -1";
            return;
        }
        if(nIndex < nNextIndex && !sNextVideoName.isNull()) {
            qDebug() << "ThumbnailPreview slotFileLoaded nIndex < nNextIndex";
            m_pMainWindow->play({sNextVideoName});
        } else{
            qDebug() << "ThumbnailPreview slotFileLoaded nIndex >= nNextIndex";
            bool isNext = true;
            for(int i = nIndex; i < lstItemInfo.count(); i++) {
                PlayItemInfo iteminfo = lstItemInfo.at(i);
                if(iteminfo.mi.vCodecID != -1) {
                    isNext = false;
                    m_pMainWindow->play({iteminfo.mi.filePath});
                    break;
                }
            }
            if(m_pEngine->getplaylist()->playMode() == PlaylistModel::OrderPlay) {
                qDebug() << "ThumbnailPreview slotFileLoaded playMode == OrderPlay";
                if(isNext)
                    m_pMainWindow->slotExitMircast();
                return;
            }
            if(isNext && !sNextVideoName.isNull()){
                qDebug() << "Next play";
                m_pMainWindow->play({sNextVideoName});
            }
        }
        qDebug() << "ThumbnailPreview slotFileLoaded return";
        return;
    }
    m_mircastWidget->playNext();
    qDebug() << "ThumbnailPreview slotFileLoaded playNext and end";
}

void Platform_ToolboxProxy::slotElapsedChanged()
{
    qDebug() << "ThumbnailPreview slotElapsedChanged";
    if(m_mircastWidget->getMircastState() != MircastWidget::Idel) {
        qDebug() << "ThumbnailPreview slotElapsedChanged not Idel";
        return;
    }
    quint64 url = static_cast<quint64>(-1);
    if (m_pEngine->playlist().current() != -1) {
        qDebug() << "ThumbnailPreview slotElapsedChanged current != -1";
        url = static_cast<quint64>(m_pEngine->duration());
    }
    //TODO(xxxpengfei):此处代码同时更新全屏的时长并未判断全屏状态，请维护同事查看是否存在优化空间
    updateTimeInfo(static_cast<qint64>(url), m_pEngine->elapsed(), m_pTimeLabel, m_pTimeLabelend, true);
    updateTimeInfo(static_cast<qint64>(url), m_pEngine->elapsed(), m_pFullscreentimelable, m_pFullscreentimelableend, false);
    QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_pFullscreentimelable->setMinimumWidth(fm.width(m_pFullscreentimelable->text()));
    m_pFullscreentimelableend->setMinimumWidth(fm.width(m_pFullscreentimelableend->text()));
#else
    m_pFullscreentimelable->setMinimumWidth(fm.horizontalAdvance(m_pFullscreentimelable->text()));
    m_pFullscreentimelableend->setMinimumWidth(fm.horizontalAdvance(m_pFullscreentimelableend->text()));
#endif
    updateMovieProgress();
    qDebug() << "ThumbnailPreview slotElapsedChanged end";
}

void Platform_ToolboxProxy::slotApplicationStateChanged(Qt::ApplicationState e)
{
    qDebug() << "ThumbnailPreview slotApplicationStateChanged";
    if (e == Qt::ApplicationInactive && anyPopupShown()) {
        qDebug() << "ThumbnailPreview slotApplicationStateChanged anyPopupShown";
        closeAnyPopup();
    }
    qDebug() << "ThumbnailPreview slotApplicationStateChanged end";
}

void Platform_ToolboxProxy::slotPlayListStateChange(bool isShortcut)
{
    qDebug() << "ThumbnailPreview slotPlayListStateChange";
    if (m_bAnimationFinash == false) {
        qDebug() << "ThumbnailPreview slotPlayListStateChange not m_bAnimationFinash";
        return ;
    }

    qDebug() << "ThumbnailPreview slotPlayListStateChange closeAnyPopup";
    closeAnyPopup();
    qDebug() << "ThumbnailPreview slotPlayListStateChange closeAnyPopup end";

/**
 * 此处在动画执行前设定好ToolboxProxy的起始位置和终止位置
 * 基于 MainWindow::updateProxyGeometry 所设置的初始状态 以及 是否是紧凑模式 定位ToolboxProxy的起始位置和终止位置
*/
    QRect rc_view=m_pMainWindow->rect();
    QRect rc_opened;
    rc_opened = QRect(5, rc_view.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT) - rc_view.top() - 5,
                      rc_view.width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7));
    QRect rc_closed = QRect(5, rc_view.height() - TOOLBOX_HEIGHT - rc_view.top() - 5,
                            rc_view.width() - 10, TOOLBOX_HEIGHT);

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        rc_opened = QRect(5, rc_view.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT) - rc_view.top() - 5,
                          rc_view.width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT + 7));
        rc_closed = QRect(5, rc_view.height() - TOOLBOX_DSIZEMODE_HEIGHT - rc_view.top() - 5,
                          rc_view.width() - 10, TOOLBOX_DSIZEMODE_HEIGHT);
    }
#endif

    if (m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened) {
        //窗口绑定渲染不展示动画,故按键状态不做限制
        Q_UNUSED(isShortcut);
        qDebug() << "ThumbnailPreview slotPlayListStateChange state Opened";
        setGeometry(rc_opened);
        m_pListBtn->setChecked(true);
    } else {
        Q_UNUSED(isShortcut);
        qDebug() << "ThumbnailPreview slotPlayListStateChange state Closed";
        setGeometry(rc_closed);
        m_pListBtn->setChecked(false);
    }

}

void Platform_ToolboxProxy::slotUpdateThumbnailTimeOut()
{
    qDebug() << "ThumbnailPreview slotUpdateThumbnailTimeOut";
    //如果视频长度小于1s应该直接返回不然会UI错误
    if (m_pEngine->playlist().currentInfo().mi.duration < 1) {
        return;
    }

    m_pViewProgBar->clear();  //清除前一次进度条中的缩略图,以便显示新的缩略图
    m_listPixmapMutex.lock();
    m_pmList.clear();
    m_pmBlackList.clear();
    m_listPixmapMutex.unlock();

    if (m_pWorker == nullptr) {
        qDebug() << "ThumbnailPreview slotUpdateThumbnailTimeOut m_pWorker == nullptr";
        m_pWorker = new Platform_viewProgBarLoad(m_pEngine, m_pProgBar, this);
        m_pWorker->setListPixmapMutex(&m_listPixmapMutex);
    }
    m_pWorker->requestInterruption();
    QTimer::singleShot(500, this, [ = ] {m_pWorker->start();});
    connect(m_pWorker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
    m_pProgBar_Widget->setCurrentIndex(1);
    qDebug() << "ThumbnailPreview slotUpdateThumbnailTimeOut end";
}

void Platform_ToolboxProxy::slotProAnimationFinished()
{
    qDebug() << "ThumbnailPreview slotProAnimationFinished";
    m_pListBtn->setEnabled(true);
    QObject *pProAnimation = sender();
    if (pProAnimation == m_pPaOpen) {
        qDebug() << "ThumbnailPreview slotProAnimationFinished m_pPaOpen";
        m_pPaOpen->deleteLater();
        m_pPaOpen = nullptr;
        m_bAnimationFinash = true;
    } else if (pProAnimation == m_pPaClose) {
        qDebug() << "ThumbnailPreview slotProAnimationFinished m_pPaClose";
        m_pPaClose->deleteLater();
        m_pPaClose = nullptr;
        m_bAnimationFinash = true;
        //Wait for the animation to end before setting the focus
        if (m_bSetListBtnFocus) {
            m_pListBtn->setFocus();
        }
    }
    qDebug() << "ThumbnailPreview slotProAnimationFinished end";
//    m_bSetListBtnFocus = false;
}

void Platform_ToolboxProxy::slotVolumeChanged(int nVolume)
{
    qDebug() << "ThumbnailPreview slotVolumeChanged" << nVolume;
    m_pVolBtn->setVolume(nVolume);

    emit sigVolumeChanged(nVolume);
    qDebug() << "ThumbnailPreview slotVolumeChanged end";
}

void Platform_ToolboxProxy::slotMuteStateChanged(bool bMute)
{
    qDebug() << "ThumbnailPreview slotMuteStateChanged" << bMute;
    m_pVolBtn->setMute(bMute);

    emit sigMuteStateChanged(bMute);
}

qint64 Platform_ToolboxProxy::getMouseTime()
{
    return m_nClickTime;
}

void Platform_ToolboxProxy::clearPlayListFocus()
{
    qDebug() << "ThumbnailPreview clearPlayListFocus";
    if (m_pPlaylist->isFocusInPlaylist()) {
        qDebug() << "ThumbnailPreview clearPlayListFocus isFocusInPlaylist";
        m_pPlaylist->clearFocus();
    }
    m_bSetListBtnFocus = false;
    qDebug() << "ThumbnailPreview clearPlayListFocus end";
}

void Platform_ToolboxProxy::setBtnFocusSign(bool sign)
{
    qDebug() << "ThumbnailPreview setBtnFocusSign" << sign;
    m_bSetListBtnFocus = sign;
}

bool Platform_ToolboxProxy::isInMircastWidget(const QPoint &p)
{
    qDebug() << "ThumbnailPreview isInMircastWidget";
    if (!m_mircastWidget->isVisible()) {
        qDebug() << "ThumbnailPreview isInMircastWidget not isVisible";
        return false;
    }
    qDebug() << "ThumbnailPreview isInMircastWidget geometry contains";
    return m_mircastWidget->geometry().contains(p);
}

void Platform_ToolboxProxy::updateMircastWidget(QPoint p)
{
    qDebug() << "ThumbnailPreview updateMircastWidget";
    m_mircastWidget->move(p.x() - m_mircastWidget->width(), p.y() - m_mircastWidget->height() - 10);
}

void Platform_ToolboxProxy::hideMircastWidget()
{
    qDebug() << "ThumbnailPreview hideMircastWidget";
    m_mircastWidget->hide();
    m_pMircastBtn->setChecked(false);
    m_pMircastBtn->setIcon(QIcon::fromTheme("dcc_mircast"));
    qDebug() << "ThumbnailPreview hideMircastWidget end";
}
/**
 * @brief volumeUp 鼠标滚轮增加音量
 */
void Platform_ToolboxProxy::volumeUp()
{
    qDebug() << "ThumbnailPreview volumeUp";
    if(!m_pVolSlider->isEnabled()) {    // 不能调节音量需要给出提示
        qDebug() << "ThumbnailPreview volumeUp not isEnabled";
        emit sigUnsupported();
    } else {
        m_pVolSlider->volumeUp();
    }
    qDebug() << "ThumbnailPreview volumeUp end";
}
/**
 * @brief volumeUp 鼠标滚轮减少音量
 */
void Platform_ToolboxProxy::volumeDown()
{
    qDebug() << "ThumbnailPreview volumeDown";
    if(!m_pVolSlider->isEnabled()) {
        qDebug() << "ThumbnailPreview volumeDown not isEnabled";
        emit sigUnsupported();
    } else {
        m_pVolSlider->volumeDown();
    }
    qDebug() << "ThumbnailPreview volumeDown end";
}
/**
 * @brief calculationStep 计算鼠标滚轮滚动的步进
 * @param iAngleDelta 鼠标滚动的距离
 */
void Platform_ToolboxProxy::calculationStep(int iAngleDelta)
{
    qDebug() << "ThumbnailPreview calculationStep" << iAngleDelta;
    m_pVolSlider->calculationStep(iAngleDelta);
    qDebug() << "ThumbnailPreview calculationStep end";
}
/**
 * @brief changeMuteState 切换静音模式
 */
void Platform_ToolboxProxy::changeMuteState()
{
    qDebug() << "ThumbnailPreview changeMuteState";
    m_pVolSlider->muteButtnClicked();
}
/**
 * @brief playlistClosedByEsc Esc关闭播放列表
 */
void Platform_ToolboxProxy::playlistClosedByEsc()
{
    qDebug() << "ThumbnailPreview playlistClosedByEsc";
    if (m_pPlaylist->isFocusInPlaylist() && m_bSetListBtnFocus) {
        qDebug() << "ThumbnailPreview playlistClosedByEsc isFocusInPlaylist && bSetListBtnFocus";
//        m_bSetListBtnFocus = true;
        m_pMainWindow->requestAction(ActionFactory::TogglePlaylist);
//        m_pListBtn->setFocus();   //焦点回到播放列表按钮
    }
    qDebug() << "ThumbnailPreview playlistClosedByEsc end";
}

void Platform_ToolboxProxy::progressHoverChanged(int nValue)
{
    qDebug() << "ThumbnailPreview progressHoverChanged" << nValue;
    if(m_pProgBar->slider()->value() == 0)   // 没有时长信息的影片不需要预览
    {
        qDebug() << "ThumbnailPreview progressHoverChanged nValue == 0";
        return;
    }
    qDebug() << "ThumbnailPreview progressHoverChanged nValue != 0";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview progressHoverChanged state Idle";
        return;
    }
    qDebug() << "ThumbnailPreview progressHoverChanged state != Idle";
    if (m_pVolSlider->isVisible())
        return;
    qDebug() << "ThumbnailPreview progressHoverChanged volSlider not visible";
    const auto &pif = m_pEngine->playlist().currentInfo();
    if (!pif.url.isLocalFile()) {
        qDebug() << "ThumbnailPreview progressHoverChanged pif.url.isLocalFile, return";
        return;
    }
    qDebug() << "ThumbnailPreview progressHoverChanged pif.url.isLocalFile";
    const auto &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
        qDebug() << "ThumbnailPreview progressHoverChanged absPath not exists, return";
        return;
    }

    m_bMouseFlag = true;

    QPoint pos = m_pProgBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
    QPoint point { QCursor::pos().x(), pos.y() };
    QPoint startPoint = mapToGlobal(QPoint(m_pProgBar_Widget->x(), 0));
    QPoint endPoint = mapToGlobal(QPoint(m_pProgBar_Widget->x() + m_pProgBar->width(), 0));

    /*********************************
    * 时长显示不能超出进度条
    * ********************************/
    if (point.x() < startPoint.x()) {
        qDebug() << "ThumbnailPreview progressHoverChanged point.x() < startPoint.x()";
        point.setX(startPoint.x());
    }

    if (point.x() > endPoint.x()) {
        qDebug() << "ThumbnailPreview progressHoverChanged point.x() > endPoint.x()";
        point.setX(endPoint.x());
    }

    bool bIsAudio = m_pEngine->currFileIsAudio();
    if (!Settings::get().isSet(Settings::PreviewOnMouseover) || bIsAudio) {
        updatePreviewTime(nValue, point);
        qDebug() << "ThumbnailPreview progressHoverChanged not PreviewOnMouseover or bIsAudio";
        return;
    }
    //鼠标移动时同步缩略图显示位置
    int nPosition = 0;
    qint64 nDuration = m_pEngine->duration();

    if(nDuration<=0)
    {
        qDebug() << "ThumbnailPreview progressHoverChanged nDuration <= 0, return";
        return;
    }

    if (m_pProgBar->isVisible()) {
        qDebug() << "ThumbnailPreview progressHoverChanged m_pProgBar->isVisible()";
        nPosition = (nValue * m_pProgBar->slider()->width()) / nDuration;
        point = m_pProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    } else {
        qDebug() << "ThumbnailPreview progressHoverChanged m_pProgBar->isVisible() == false";
        nPosition = nValue * m_pViewProgBar->getViewLength() / nDuration + m_pViewProgBar->getStartPoint();
        point = m_pViewProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    }
    m_pPreviewer->updateWithPreview(point);
    Platform_ThumbnailWorker::get().requestThumb(pif.url, nValue);
    qDebug() << "ThumbnailPreview progressHoverChanged end";
}

void Platform_ToolboxProxy::updateTimeVisible(bool visible)
{
    qDebug() << "ThumbnailPreview updateTimeVisible" << visible;
    if (Settings::get().isSet(Settings::PreviewOnMouseover)) {
        qDebug() << "ThumbnailPreview updateTimeVisible PreviewOnMouseover";
        return;
    }
    qDebug() << "ThumbnailPreview updateTimeVisible not PreviewOnMouseover";
    if (m_pPreviewTime) {
        qDebug() << "ThumbnailPreview updateTimeVisible m_pPreviewTime not null";
        m_pPreviewTime->setVisible(!visible);
    }
}

void Platform_ToolboxProxy::updateMovieProgress()
{
    qDebug() << "ThumbnailPreview updateMovieProgress";
    if (m_bMousePree == true) {
        qDebug() << "Skipping progress update due to mouse press";
        return;
    }
    
    auto d = m_pEngine->duration();
    auto e = m_pEngine->elapsed();
    qDebug() << "Updating movie progress - Duration:" << d << "Elapsed:" << e;
    
    if (d > m_pProgBar->maximum()) {
        qDebug() << "Duration exceeds progress bar maximum, adjusting duration";
        d = m_pProgBar->maximum();
    }
    int v = 0;
    int v2 = 0;
    if (d != 0 && e != 0) {
        v = static_cast<int>(m_pProgBar->maximum() * e / d);
        v2 = static_cast<int>(m_pViewProgBar->getViewLength() * e / d + m_pViewProgBar->getStartPoint());
        qDebug() << "Calculated progress values - v:" << v << "v2:" << v2;
    }
    
    if (!m_pProgBar->signalsBlocked()) {
        qDebug() << "Updating progress bar value - v:" << v;
        m_pProgBar->blockSignals(true);
        m_pProgBar->setValue(v);
        m_pProgBar->blockSignals(false);
    }
    if (!m_pViewProgBar->getIsBlockSignals()) {
        qDebug() << "Updating view progress bar value - v2:" << v2;
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(v2);
        m_pViewProgBar->setTime(e);
        m_pViewProgBar->setIsBlockSignals(false);
    }
    qDebug() << "ThumbnailPreview updateMovieProgress end";
}

void Platform_ToolboxProxy::updateButtonStates()
{
    qDebug() << "ThumbnailPreview updateButtonStates";
    QPalette palette;              // 时长显示的颜色，在某些情况下变化字体颜色区别功能
    bool bRawFormat = false;

    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "ThumbnailPreview updateButtonStates LightType";
        palette.setColor(QPalette::WindowText, QColor(0, 0, 0, 40));       // 浅色背景下置灰
        palette.setColor(QPalette::Text, QColor(0, 0, 0, 40));
    } else {
        qDebug() << "ThumbnailPreview updateButtonStates DarkType";
        palette.setColor(QPalette::WindowText, QColor(255, 255, 255, 40)); // 深色背景下置灰
        palette.setColor(QPalette::Text, QColor(255, 255, 255, 40));
    }

    if(m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview updateButtonStates state != Idle";
        bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
        m_pMircastBtn->setEnabled(!m_pEngine->currFileIsAudio());
        if(m_pEngine->currFileIsAudio())
            m_mircastWidget->setVisible(false);
        if(bRawFormat && !m_pEngine->currFileIsAudio()){                                             // 如果正在播放的视频是裸流不支持音量调节和进度调节
            m_pProgBar->setEnabled(false);
            m_pProgBar->setEnableIndication(false);
            m_pVolSlider->setEnabled(false);

            m_pTimeLabel->setPalette(palette);             // 如果正在播放的视频是裸流置灰
            m_pTimeLabelend->setPalette(palette);
            m_pFullscreentimelable->setPalette(palette);
            m_pFullscreentimelableend->setPalette(palette);

            m_pVolBtn->setButtonEnable(false);
        } else if (bRawFormat) {
            qDebug() << "ThumbnailPreview updateButtonStates bRawFormat";
            m_pProgBar->setEnabled(false);
            m_pProgBar->setEnableIndication(false);

            m_pTimeLabel->setPalette(palette);
            m_pTimeLabelend->setPalette(palette);
            m_pFullscreentimelable->setPalette(palette);
            m_pFullscreentimelableend->setPalette(palette);
        } else {
            qDebug() << "ThumbnailPreview updateButtonStates bRawFormat == false";
            m_pProgBar->setEnabled(true);
            m_pProgBar->setEnableIndication(true);
            m_pVolSlider->setEnabled(true);

            palette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
            palette.setColor(QPalette::Text, DApplication::palette().text().color());

            m_pTimeLabel->setPalette(palette);
            m_pTimeLabelend->setPalette(palette);
            m_pFullscreentimelable->setPalette(palette);
            m_pFullscreentimelableend->setPalette(palette);

            m_pVolBtn->setButtonEnable(true);
        }
    } else {
        qDebug() << "ThumbnailPreview updateButtonStates state == Idle";
        m_pVolSlider->setEnabled(true);

        palette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
        palette.setColor(QPalette::Text, DApplication::palette().text().color());

        m_pTimeLabel->setPalette(palette);
        m_pTimeLabelend->setPalette(palette);
        m_pFullscreentimelable->setPalette(palette);
        m_pFullscreentimelableend->setPalette(palette);

         m_pVolBtn->setButtonEnable(true);
    }

    qInfo() << m_pEngine->playingMovieInfo().subs.size();
    bool vis = m_pEngine->playlist().count() > 1 && m_pMainWindow->inited();

    //播放状态为空闲或播放列表只有一项时，将上下一曲按钮置灰
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle ||
            m_pEngine->getplaylist()->items().size() <= 1) {
        m_pPrevBtn->setDisabled(true);
        m_pNextBtn->setDisabled(true);
    } else {
        m_pPrevBtn->setEnabled(true);
        m_pNextBtn->setEnabled(true);
    }

    m_bCanPlay = vis;  //防止连续切换上下曲目
    qDebug() << "ThumbnailPreview updateButtonStates end";
}

void Platform_ToolboxProxy::updateFullState()
{
    qDebug() << "ThumbnailPreview updateFullState";
    bool isFullscreen = window()->isFullScreen();
    if (isFullscreen || m_pFullscreentimelable->isVisible()) {
        qDebug() << "ThumbnailPreview updateFullState isFullscreen";
        m_pFullScreenBtn->setIcon(QIcon::fromTheme("dcc_zoomout"));
        m_pFullScreenBtn->setTooTipText(tr("Exit fullscreen"));
    } else {
        qDebug() << "ThumbnailPreview updateFullState not isFullscreen";
        m_pFullScreenBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
        m_pFullScreenBtn->setTooTipText(tr("Fullscreen"));
    }
    qDebug() << "ThumbnailPreview updateFullState end";
}

void Platform_ToolboxProxy::slotUpdateMircast(int state, QString msg)
{
    emit sigMircastState(state, msg);
    if (state == 0) {
        m_pVolBtn->setButtonEnable(false);
        m_pFullScreenBtn->setEnabled(false);
    } else {
        bool bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
        if(bRawFormat && !m_pEngine->currFileIsAudio()) {
            m_pVolBtn->setButtonEnable(false);
        } else {
            m_pVolBtn->setButtonEnable(true);
        }
        m_pFullScreenBtn->setEnabled(true);
    }
}

void Platform_ToolboxProxy::updatePlayState()
{
    qDebug() << "ThumbnailPreview updatePlayState";
    if (((m_mircastWidget->getMircastState() != MircastWidget::Idel) && (m_mircastWidget->getMircastPlayState() == MircastWidget::Play))
            || m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qDebug() << "ThumbnailPreview updatePlayState playing";
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            qDebug() << "ThumbnailPreview updatePlayState playing LightType";
            DPalette pa;
            pa = m_pPalyBox->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Button, QColor(255, 255, 255, 255));
            m_pPalyBox->setPalette(pa);

            pa = m_pVolBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pVolBtn->setPalette(pa);

            pa = m_pFullScreenBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pFullScreenBtn->setPalette(pa);

            pa = m_pListBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pListBtn->setPalette(pa);

            pa = m_pMircastBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pMircastBtn->setPalette(pa);
        } else {
            qDebug() << "ThumbnailPreview updatePlayState playing DarkType";
            DPalette pa;
            pa = m_pPalyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 255));
            m_pPalyBox->setPalette(pa);

            pa = m_pVolBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pVolBtn->setPalette(pa);

            pa = m_pFullScreenBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pFullScreenBtn->setPalette(pa);

            pa = m_pListBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pListBtn->setPalette(pa);

            pa = m_pMircastBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pMircastBtn->setPalette(pa);
        }
        m_pPlayBtn->setIcon(QIcon::fromTheme("dcc_suspend", QIcon(":/icons/deepin/builtin/light/normal/suspend_normal.svg")));
        //lmh0910wayland下用这一套tooltip
        m_pPlayBtn->setToolTip(tr("Pause"));
    } else {
        qDebug() << "ThumbnailPreview updatePlayState not playing";
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            qDebug() << "ThumbnailPreview updatePlayState not playing LightType";
            DPalette pa;
            pa = m_pPalyBox->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Button, QColor(255, 255, 255, 255));
            m_pPalyBox->setPalette(pa);


            pa = m_pVolBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pVolBtn->setPalette(pa);

            pa = m_pFullScreenBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pFullScreenBtn->setPalette(pa);

            pa = m_pListBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pListBtn->setPalette(pa);

            pa = m_pMircastBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            m_pMircastBtn->setPalette(pa);
        } else {
            qDebug() << "ThumbnailPreview updatePlayState not playing DarkType";
            DPalette pa;
            pa = m_pPalyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 255));
            m_pPalyBox->setPalette(pa);

            pa = m_pVolBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pVolBtn->setPalette(pa);

            pa = m_pFullScreenBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pFullScreenBtn->setPalette(pa);

            pa = m_pListBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pListBtn->setPalette(pa);

            pa = m_pMircastBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            m_pMircastBtn->setPalette(pa);

        }
        //lmh0910wayland下用这一套tooltip
        m_pPlayBtn->setToolTip(tr("Play"));
        m_pPlayBtn->setIcon(QIcon::fromTheme("dcc_play", QIcon(":/icons/deepin/builtin/light/normal/play_normal.svg")));
    }

    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "ThumbnailPreview updatePlayState Idle";
        if (m_pPreviewer->isVisible()) {
            m_pPreviewer->hide();
        }

        if (m_pPreviewTime->isVisible()) {
            m_pPreviewTime->hide();
        }

        if (m_pProgBar->isVisible()) {
            m_pProgBar->setVisible(false);
        }
        m_pProgBar_Widget->setCurrentIndex(0);
        setProperty("idle", true);
    } else {
        qDebug() << "ThumbnailPreview updatePlayState not Idle";
        setProperty("idle", false);
    }
    qDebug() << "ThumbnailPreview updatePlayState end";
}
/**
 * @brief updateTimeInfo 更新工具栏中播放时间显示
 * @param duration 视频总时长
 * @param pos 当前播放的时间点
 * @param pTimeLabel 当前播放时间
 * @param pTimeLabelend 视频总时长
 * @param flag 是否为全屏的控件
 */
void Platform_ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos, QLabel *pTimeLabel, QLabel *pTimeLabelend, bool flag)
{
    qDebug() << "ThumbnailPreview updateTimeInfo";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        pTimeLabel->setText("");
        pTimeLabelend->setText("");
        qDebug() << "ThumbnailPreview updateTimeInfo Idle";
    } else {
        qDebug() << "ThumbnailPreview updateTimeInfo not Idle";
        //mpv returns a slightly different duration from movieinfo.duration
        //m_pTimeLabel->setText(QString("%2/%1").arg(utils::Time2str(duration))
        //.arg(utils::Time2str(pos)));
        if (1 == flag) {
            pTimeLabel->setText(QString("%1")
                                .arg(utils::Time2str(pos)));
            pTimeLabelend->setText(QString("%1")
                                   .arg(utils::Time2str(duration)));
        } else {
            pTimeLabel->setText(QString("%1 %2")
                                .arg(utils::Time2str(pos)).arg("/"));
            pTimeLabelend->setText(QString("%1")
                                   .arg(utils::Time2str(duration)));
        }

    }
    qDebug() << "ThumbnailPreview updateTimeInfo end";
}

void Platform_ToolboxProxy::buttonClicked(QString id)
{
    qDebug() << "ThumbnailPreview buttonClicked" << id;
    //add by heyi
    static bool bFlags = true;
    if (bFlags) {
//        m_pMainWindow->firstPlayInit();
        m_pMainWindow->repaint();
        bFlags = false;
    }
    qDebug() << "ThumbnailPreview buttonClicked isVisible" << isVisible();
    if (!isVisible()) return;
    qDebug() << "ThumbnailPreview buttonClicked isVisible true";
    qInfo() << __func__ << id;
    if (id == "play") {
        qDebug() << "ThumbnailPreview buttonClicked play";
        if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
            qDebug() << "ThumbnailPreview buttonClicked play Idle";
            m_pMainWindow->requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            qDebug() << "ThumbnailPreview buttonClicked play not Idle";
            m_pMainWindow->requestAction(ActionFactory::ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        qDebug() << "ThumbnailPreview buttonClicked fs";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::ToggleFullscreen);
    } else if (id == "vol") {
        qDebug() << "ThumbnailPreview buttonClicked vol";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::ToggleMute);
    } else if (id == "prev" && m_bCanPlay) {  //如果影片未加载完成，则不播放上一曲
        qDebug() << "ThumbnailPreview buttonClicked prev";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistPrev);
    } else if (id == "next" && m_bCanPlay) {
        qDebug() << "ThumbnailPreview buttonClicked next";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);
    } else if (id == "list") {
        qDebug() << "ThumbnailPreview buttonClicked list";
        m_nClickTime = QDateTime::currentMSecsSinceEpoch();
        m_pMainWindow->requestAction(ActionFactory::ActionKind::TogglePlaylist);
        m_pListBtn->hideToolTip();
    } else if (id == "mircast") {
        qDebug() << "ThumbnailPreview buttonClicked mircast";
        m_mircastWidget->togglePopup();
        m_pMircastBtn->hideToolTip();
        m_pMircastBtn->setChecked(m_mircastWidget->isVisible());
        if (m_pMircastBtn->isChecked())
            m_pMircastBtn->setIcon(QIcon(":/icons/deepin/builtin/light/checked/mircast_chenked.svg"));
        else
            m_pMircastBtn->setIcon(QIcon::fromTheme("dcc_mircast"));
    }
    qDebug() << "ThumbnailPreview buttonClicked end";
}

void Platform_ToolboxProxy::buttonEnter()
{
    qDebug() << "ThumbnailPreview buttonEnter";
    if (!isVisible()) return;
    qDebug() << "ThumbnailPreview buttonEnter isVisible true";
    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == "sub" || id == "fs" || id == "list" || id == "mir") {
        qDebug() << "ThumbnailPreview buttonEnter id" << id;
        updateToolTipTheme(btn);
        btn->showToolTip();
    }
    qDebug() << "ThumbnailPreview buttonEnter end";
}

void Platform_ToolboxProxy::buttonLeave()
{
    qDebug() << "ThumbnailPreview buttonLeave";
    if (!isVisible()) {
        qDebug() << "ThumbnailPreview buttonLeave isVisible false";
        return;
    }
    qDebug() << "ThumbnailPreview buttonLeave isVisible true";
    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == "sub" || id == "fs" || id == "list" || id == "mir") {
        qDebug() << "ThumbnailPreview buttonLeave id" << id;
        btn->hideToolTip();
    }
    qDebug() << "ThumbnailPreview buttonLeave end";
}

void Platform_ToolboxProxy::showEvent(QShowEvent *event)
{
    qDebug() << "ThumbnailPreview showEvent";
    updateTimeLabel();

    DFloatingWidget::showEvent(event);
}

void Platform_ToolboxProxy::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    setFixedWidth(m_pMainWindow->width());
    //使偏移位置与初始化偏移的位置相同
    int widthOffset = 0;
    move(widthOffset, m_pMainWindow->height() - this->height());
    if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        painter.fillRect(rect(), QBrush(QColor(31, 31, 31)));
    } else {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        painter.fillRect(rect(), this->palette().background());
#else
        painter.fillRect(rect(), this->palette().window());
#endif
    }
}

void Platform_ToolboxProxy::resizeEvent(QResizeEvent *event)
{
    if (event->oldSize().width() != event->size().width()) {
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            if (m_bThumbnailmode) {  //如果进度条为胶片模式，重新加载缩略图并显示
                qInfo() << "Updating thumbnail due to resize";
                if(CompositingManager::get().platform() == Platform::X86) {
                    updateThumbnail();
                }
                updateMovieProgress();
            }
            m_pProgBar_Widget->setCurrentIndex(1);
        }
    }
    if (CompositingManager::get().platform() != Platform::Alpha) {
        if (m_bAnimationFinash == false && m_pPaOpen != nullptr && m_pPaClose != nullptr) {
            qDebug() << "Ending playlist animation due to resize";
            m_pPlaylist->endAnimation();
            m_pPaOpen->setDuration(0);
            m_pPaClose->setDuration(0);
        }

        updateTimeLabel();
    }
    DFloatingWidget::resizeEvent(event);
}

void Platform_ToolboxProxy::mouseMoveEvent(QMouseEvent *ev)
{
    setButtonTooltipHide();
    QWidget::mouseMoveEvent(ev);
}

bool Platform_ToolboxProxy::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == m_pVolBtn) {
        if (ev->type() == QEvent::KeyPress && (m_pVolSlider->state() == Platform_VolumeSlider::Open)) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
            int nCurVolume = m_pVolSlider->getVolume();
            qDebug() << "Volume key press - Current volume:" << nCurVolume;
            //如果音量条升起且上下键按下，以步长为5调整音量
            if (keyEvent->key() == Qt::Key_Up) {
                int newVolume = qMin(nCurVolume + 5, 200);
                qDebug() << "Volume up - New volume:" << newVolume;
                m_pVolSlider->changeVolume(newVolume);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                int newVolume = qMax(nCurVolume - 5, 0);
                qDebug() << "Volume down - New volume:" << newVolume;
                m_pVolSlider->changeVolume(newVolume);
                return true;
            }
        }
    }

    if(CompositingManager::get().platform() == Platform::X86) {
        if (obj == m_pListBtn) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(ev);
            if (ev->type() == QEvent::MouseButtonRelease && mouseEvent->button() == Qt::RightButton) {
                qDebug() << "Right click on list button - Current state:" << m_pPlaylist->state();
                if (m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened && m_pListBtn->isChecked()) {
                    m_pListBtn->setChecked(!m_pListBtn->isChecked());
                }
                if (m_pPlaylist->state() == Platform_PlaylistWidget::State::Closed && !m_pListBtn->isChecked()) {
                    m_pListBtn->setChecked(!m_pListBtn->isChecked());
                }
            }
        }
    }

    return QObject::eventFilter(obj, ev);
}
/**
 * @brief updateTimeLabel 界面显示或大小变化时更新控件显示状态
 */
void Platform_ToolboxProxy::updateTimeLabel()
{
    qDebug() << "ThumbnailPreview updateTimeLabel";
    if (CompositingManager::get().platform() != Platform::Alpha)  {
        qDebug() << "ThumbnailPreview updateTimeLabel CompositingManager::get().platform() != Platform::Alpha";
        // to keep left and right of the same width. which makes play button centered
        m_pListBtn->setVisible(width() > 300);
        m_pTimeLabel->setVisible(width() > 450);
        m_pTimeLabelend->setVisible(width() > 450);
    }
    qDebug() << "ThumbnailPreview updateTimeLabel end";
}

void Platform_ToolboxProxy::updateMircastTime(int time)
{
    qDebug() << "ThumbnailPreview updateMircastTime";
    if (m_pProgBar_Widget->currentIndex() == 1) {              //进度条模式
        qDebug() << "ThumbnailPreview updateMircastTime m_pProgBar_Widget->currentIndex() == 1";
        if (!m_pProgBar->signalsBlocked()) {
            m_pProgBar->blockSignals(true);
        }
        qDebug() << "ThumbnailPreview updateMircastTime m_pProgBar->signalsBlocked() == false";
        m_pProgBar->slider()->setSliderPosition(time);
        m_pProgBar->slider()->setValue(time);
        m_pProgBar->blockSignals(false);
        qDebug() << "ThumbnailPreview updateMircastTime m_pProgBar->signalsBlocked() == false end";
    } else {
        qDebug() << "ThumbnailPreview updateMircastTime m_pProgBar_Widget->currentIndex() != 1";
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(time);
        m_pViewProgBar->setIsBlockSignals(false);
    }
    quint64 url = static_cast<quint64>(-1);
    if (m_pEngine->playlist().current() != -1) {
        qDebug() << "ThumbnailPreview updateMircastTime m_pEngine->playlist().current() != -1";
        url = static_cast<quint64>(m_pEngine->duration());
    }
    updateTimeInfo(url, time, m_pTimeLabel, m_pTimeLabelend, true);
    qDebug() << "ThumbnailPreview updateMircastTime end";
}

void Platform_ToolboxProxy::updateToolTipTheme(ToolButton *btn)
{
    qDebug() << "ThumbnailPreview updateToolTipTheme";
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "ThumbnailPreview updateToolTipTheme LightType";
        btn->changeTheme(lightTheme);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "ThumbnailPreview updateToolTipTheme DarkType";
        btn->changeTheme(darkTheme);
    } else {
        qDebug() << "ThumbnailPreview updateToolTipTheme else";
        btn->changeTheme(lightTheme);
    }
    qDebug() << "ThumbnailPreview updateToolTipTheme end";
}

void Platform_ToolboxProxy::setPlaylist(Platform_PlaylistWidget *pPlaylist)
{
    qDebug() << "ThumbnailPreview setPlaylist";
    m_pPlaylist = pPlaylist;
    connect(m_pPlaylist, &Platform_PlaylistWidget::stateChange, this, &Platform_ToolboxProxy::slotPlayListStateChange);
    qDebug() << "ThumbnailPreview setPlaylist end";
}
QLabel *Platform_ToolboxProxy::getfullscreentimeLabel()
{
    qDebug() << "ThumbnailPreview getfullscreentimeLabel";
    return m_pFullscreentimelable;
}

QLabel *Platform_ToolboxProxy::getfullscreentimeLabelend()
{
    qDebug() << "ThumbnailPreview getfullscreentimeLabelend";
    return m_pFullscreentimelableend;
}

bool Platform_ToolboxProxy::getbAnimationFinash()
{
    qDebug() << "ThumbnailPreview getbAnimationFinash";
    return  m_bAnimationFinash;
}

void Platform_ToolboxProxy::setVolSliderHide()
{
    qDebug() << "ThumbnailPreview setVolSliderHide";
    if (m_pVolSlider->isVisible()) {
        m_pVolSlider->hide();
    }
}

void Platform_ToolboxProxy::setButtonTooltipHide()
{
    qDebug() << "ThumbnailPreview setButtonTooltipHide";
    m_pListBtn->hideToolTip();
    m_pFullScreenBtn->hideToolTip();
}

void Platform_ToolboxProxy::initToolTip()
{
    qDebug() << "ThumbnailPreview initToolTip";
    //lmh0910全屏按键
    m_pFullScreenBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pFullScreenBtnTip->setText(tr("Fullscreen"));
    connect(m_pFullScreenBtn, &ToolButton::entered, [ = ]() {
        m_pFullScreenBtnTip->move(m_pMainWindow->width() - m_pFullScreenBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 140,
                                  m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pFullScreenBtnTip->show();
        m_pFullScreenBtnTip->QWidget::activateWindow();
        m_pFullScreenBtnTip->update();
        m_pFullScreenBtnTip->releaseMouse();

    });
    connect(m_pFullScreenBtn, &ToolButton::leaved, [ = ]() {
        QTimer::singleShot(0, [ = ] {
            m_pFullScreenBtnTip->hide();
        });
    });
    //lmh0910list按键
    m_pListBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pListBtnTip->setText(tr("Playlist"));
    connect(m_pListBtn, &ToolButton::entered, [ = ]() {
        m_pListBtnTip->move(m_pMainWindow->width() - m_pListBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 20,
                            m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pListBtnTip->show();
        m_pListBtnTip->QWidget::activateWindow();
        m_pListBtnTip->update();
        m_pListBtnTip->releaseMouse();

    });
    connect(m_pListBtn, &ToolButton::leaved, [ = ]() {
        QTimer::singleShot(0, [ = ] {
            m_pListBtnTip->hide();
        });
    });
    qDebug() << "ThumbnailPreview initToolTip end";
}

bool Platform_ToolboxProxy::getListBtnFocus()
{
    qDebug() << "ThumbnailPreview getListBtnFocus";
    return m_pListBtn->hasFocus();
}

bool Platform_ToolboxProxy::getVolSliderIsHided()
{
    qDebug() << "ThumbnailPreview getVolSliderIsHided";
    return m_pVolSlider->isHidden();
}
/**
 * @brief updateProgress 更新播放进度条显示
 * @param nValue 进度条的值
 */
void Platform_ToolboxProxy::updateProgress(int nValue)
{
    qDebug() << "ThumbnailPreview updateProgress";
    int nDuration = static_cast<int>(m_pEngine->duration());
    qDebug() << "ThumbnailPreview updateProgress nDuration" << nDuration;
    if (m_pProgBar_Widget->currentIndex() == 1) {              //进度条模式
        qDebug() << "ThumbnailPreview updateProgress m_pProgBar_Widget->currentIndex() == 1";
        float value = nValue * nDuration / m_pProgBar->width();
        int nCurrPos;
        if (value > 1 || value < -1) {
            nCurrPos = m_pProgBar->value() + value;
            qDebug() << "ThumbnailPreview updateProgress nCurrPos" << nCurrPos;
        } else {
            qDebug() << "ThumbnailPreview updateProgress else";
            if (m_processAdd < 1.0 && m_processAdd > -1.0) {
                m_processAdd += (float)(nValue * nDuration) / m_pProgBar->width();
                qInfo() << m_processAdd;
                qDebug() << "ThumbnailPreview updateProgress return m_processAdd" << m_processAdd;
                return;
            }
            else {
                nCurrPos = m_pProgBar->value() + m_processAdd;
                m_processAdd = .0;
            }
        }
        if (!m_pProgBar->signalsBlocked()) {
            qDebug() << "ThumbnailPreview updateProgress m_pProgBar->signalsBlocked()";
            m_pProgBar->blockSignals(true);
        }

        m_pProgBar->slider()->setSliderPosition(nCurrPos);
        m_pProgBar->slider()->setValue(nCurrPos);
    } else {
        qDebug() << "ThumbnailPreview updateProgress m_pProgBar_Widget->currentIndex() != 1";
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(m_pViewProgBar->getValue() + nValue);
    }
    qDebug() << "ThumbnailPreview updateProgress end";
}
/**
 * @brief updateSlider 根据进度条显示更新影片实际进度
 */
void Platform_ToolboxProxy::updateSlider()
{
    qDebug() << "ThumbnailPreview updateSlider";
    if (m_pProgBar_Widget->currentIndex() == 1) {
        qDebug() << "ThumbnailPreview updateSlider m_pProgBar_Widget->currentIndex() == 1";
        m_pEngine->seekAbsolute(m_pProgBar->value());

        m_pProgBar->blockSignals(false);
    } else {
        qDebug() << "ThumbnailPreview updateSlider m_pProgBar_Widget->currentIndex() != 1";
        m_pEngine->seekAbsolute(m_pViewProgBar->getTimePos());
        m_pViewProgBar->setIsBlockSignals(false);
    }
    qDebug() << "ThumbnailPreview updateSlider end";
}
/**
 * @brief initThumb 初始化加载胶片线程
 */
void Platform_ToolboxProxy::initThumbThread()
{
    qDebug() << "ThumbnailPreview initThumbThread";
    Platform_ThumbnailWorker::get().setPlayerEngine(m_pEngine);
    connect(&Platform_ThumbnailWorker::get(), &Platform_ThumbnailWorker::thumbGenerated,
            this, &Platform_ToolboxProxy::updateHoverPreview);
}
/**
 * @brief updateSliderPoint 非x86平台下更新音量条控件位置
 * @param point 传入主窗口左上角顶点在屏幕的位置
 */
void Platform_ToolboxProxy::updateSliderPoint(QPoint &point)
{
    m_pVolSlider->updatePoint(point);
}

/**
 * @brief ~ToolboxProxy 析构函数
 */
Platform_ToolboxProxy::~Platform_ToolboxProxy()
{
    qDebug() << "Destroying Platform_ToolboxProxy";
    Platform_ThumbnailWorker::get().stop();
    delete m_pPreviewer;
    delete m_pPreviewTime;

    if (m_pWorker) {
        qDebug() << "Stopping worker thread";
        m_pWorker->quit();
        m_pWorker->deleteLater();
    }
    qDebug() << "Platform_ToolboxProxy destroyed";
}

Platform_viewProgBarLoad::Platform_viewProgBarLoad(PlayerEngine *engine, DMRSlider *progBar, Platform_ToolboxProxy *parent)
{
    qDebug() << "Platform_viewProgBarLoad";
    initMember();
    m_pParent = parent;
    m_pEngine = engine;
    m_pProgBar = progBar;
    m_seekTime = new char[12];
    initThumb();
    qDebug() << "Platform_viewProgBarLoad end";
}

void Platform_viewProgBarLoad::setListPixmapMutex(QMutex *pMutex)
{
    qDebug() << "Platform_viewProgBarLoad setListPixmapMutex";
    m_pListPixmapMutex = pMutex;
}

void Platform_viewProgBarLoad::run()
{
    qDebug() << "Platform_viewProgBarLoad run";
    loadViewProgBar(m_pParent->size());
    qDebug() << "Platform_viewProgBarLoad run end";
}

Platform_viewProgBarLoad::~Platform_viewProgBarLoad()
{
    qDebug() << "Platform_viewProgBarLoad ~Platform_viewProgBarLoad";
    delete [] m_seekTime;
    m_seekTime = nullptr;

    if (m_video_thumbnailer != nullptr) {
        qDebug() << "Platform_viewProgBarLoad ~Platform_viewProgBarLoad m_video_thumbnailer";
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
        m_video_thumbnailer = nullptr;
    }
    qDebug() << "Platform_viewProgBarLoad ~Platform_viewProgBarLoad end";
}

}
#undef THEME_TYPE
#include "platform_toolbox_proxy.moc"
