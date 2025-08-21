// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

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

//#include <QtWidgets>
#include <QDir>

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

//thx  wayland chuang kou bai kuai
#define WAYLAND_BLACK_WINDOW \
    do {\
        auto systemEnv = QProcessEnvironment::systemEnvironment();\
        QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));\
        QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));\
        if (XDG_SESSION_TYPE == QLatin1String("wayland") ||\
                WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {\
            auto colortype = DGuiApplicationHelper::instance()->themeType();\
            if(colortype == DGuiApplicationHelper::LightType)\
            {\
                QPalette palette(qApp->palette());\
                this->setAutoFillBackground(true);\
                this->setPalette(palette);\
                if(m_pPlaylist)\
                {\
                    QPalette pal(qApp->palette());\
                    m_pPlaylist->setAutoFillBackground(true);\
                    m_pPlaylist->setPalette(pal);\
                }\
                if(m_pEngine )\
                {\
                    QPalette pal(qApp->palette());\
                    m_pEngine->setAutoFillBackground(true);\
                    m_pEngine->setPalette(pal);\
                }\
            }\
            else\
            {\
                QPalette palette(qApp->palette());\
                palette.setColor(QPalette::Window,Qt::black);\
                this->setAutoFillBackground(true);\
                this->setPalette(palette);\
                if(m_pPlaylist)\
                {\
                    QPalette pal(qApp->palette());\
                    pal.setColor(QPalette::Window,Qt::black);\
                    m_pPlaylist->setAutoFillBackground(true);\
                    m_pPlaylist->setPalette(pal);\
                }\
                if(m_pEngine)\
                {\
                    QPalette pal(qApp->palette());\
                    pal.setColor(QPalette::Window,Qt::black);\
                    m_pEngine->setAutoFillBackground(true);\
                    m_pEngine->setPalette(pal);\
                }\
            }\
        }\
    }while(0)

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
class TooltipHandler: public QObject
{
public:
    /**
     * @brief TooltipHandler 构造函数
     * @param parent 父窗口
     */
    explicit TooltipHandler(QObject *parent): QObject(parent) {
        qDebug() << "TooltipHandler";
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
        qDebug() << "TooltipHandler end";
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
        qDebug() << "TooltipHandler";
        switch (event->type()) {
        case QEvent::ToolTip:
        case QEvent::Enter: {
            qDebug() << "QEvent::Enter";
            m_object = obj;
            if (!m_showTime.isActive())
                m_showTime.start(1000);
            return true;
        }

        case QEvent::Leave: {
            qDebug() << "QEvent::Leave";
            m_object = nullptr;
            m_showTime.stop();
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();
            break;
        }
        case QEvent::MouseMove: {
            qDebug() << "QEvent::MouseMove";
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            tip->hide();
        }
        default:
            break;
        }
        qDebug() << "TooltipHandler end";
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
        qDebug() << "SliderTime";
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
        DPalette pa = DGuiApplicationHelper::instance()->applicationPalette();
#endif
        QColor color = pa.textLively().color();
        qInfo() << color.name();
        pa.setColor(DPalette::Text, color);
        m_pTime->setPalette(pa);
        m_pTime->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));
        layout->addWidget(m_pTime, Qt::AlignCenter);
        setLayout(layout);
        connect(qApp, &QGuiApplication::fontChanged, this, &SliderTime::slotFontChanged);
        qDebug() << "SliderTime end";
    }

    /**
     * @brief setTime 设置时间
     * @param time 时间
     */
    void setTime(const QString &time)
    {
        qDebug() << "setTime";
        m_pTime->setText(time);

        if (!m_bFontChanged) {
            qDebug() << "!m_bFontChanged";
            QFontMetrics fontMetrics(DFontSizeManager::instance()->get(DFontSizeManager::T8));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            m_pTime->setFixedSize(fontMetrics.width(m_pTime->text()) + 5, fontMetrics.height());
#else
            m_pTime->setFixedSize(fontMetrics.horizontalAdvance(m_pTime->text()) + 5, fontMetrics.height());
#endif
        } else {
            qDebug() << "m_bFontChanged";
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
        qDebug() << "setTime end";
    }
public slots:
    /**
     * @brief slotFontChanged 字体变化槽函数
     * @param font 字体
     */
    void slotFontChanged(const QFont &font)
    {
        qDebug() << "slotFontChanged";
        m_font = font;
        m_bFontChanged = true;
        qDebug() << "slotFontChanged end";
    }
private:
    /**
     * @brief initMember 初始化成员变量
     */
    void initMember()
    {
        qDebug() << "initMember";
        m_pTime = nullptr;
        m_miniSize = QSize(58, 25);
        m_font = {QFont()};
        m_bFontChanged = false;
        qDebug() << "initMember end";
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
        qDebug() << "ViewProgBar";
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
        qDebug() << "DTKWIDGET_CLASS_DSizeMode";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        setFixedHeight(46);
        m_pBack->setFixedHeight(40);
        m_pFront->setFixedHeight(40);
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        qDebug() << "sizeModeChanged";
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            qDebug() << "DGuiApplicationHelper::NormalMode";
            setFixedHeight(70);
            m_pBack->setFixedHeight(60);
            m_pFront->setFixedHeight(60);
        } else {
            qDebug() << "!DGuiApplicationHelper::NormalMode";
            setFixedHeight(46);
            m_pBack->setFixedHeight(40);
            m_pFront->setFixedHeight(40);
        }
    });
#endif
        qDebug() << "ViewProgBar end";
    }
//    virtual ~ViewProgBar();
    void setIsBlockSignals(bool isBlockSignals)
    {
        qDebug() << "setIsBlockSignals";
        m_bIsBlockSignals = isBlockSignals;
    }
    bool getIsBlockSignals()
    {
        qDebug() << "getIsBlockSignals";
        return  m_bPress ? true: m_bIsBlockSignals;
    }
    void setValue(int v)
    {
        qDebug() << "setValue";
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
        qDebug() << "getValue";
        return m_pIndicator->x();
    }
    int getTimePos()
    {
        qDebug() << "getTimePos";
        return position2progress(QPoint(m_pIndicator->x(), 0));
    }
    void setTime(qint64 pos)
    {
        qDebug() << "setTime";
        QTime time(0, 0, 0);
        QString strTime = time.addSecs(static_cast<int>(pos)).toString("hh:mm:ss");
        m_pSliderTime->setTime(strTime);
        qDebug() << "setTime end";
    }
    void setTimeVisible(bool visible)
    {
        qDebug() << "setTimeVisible";
        if (visible) {
            auto pos = this->mapToGlobal(QPoint(0, 0));
            m_pSliderTime->show(pos.x() + m_IndicatorPos.x() + 1, pos.y() + m_IndicatorPos.y() + 4);
        } else {
            m_pSliderTime->hide();
        }
        qDebug() << "setTimeVisible end";
    }
    /**
     * @brief setViewProgBar 设置胶片模式位置
     * @param pEngine 播放引擎对象指针
     * @param pmList 彩色胶片图像列表
     * @param pmBlackList 灰色胶片图像列表
     */
    void setViewProgBar(PlayerEngine *pEngine, QList<QPixmap> pmList, QList<QPixmap> pmBlackList)
    {
        qDebug() << "setViewProgBar";
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
        qDebug() << "setViewProgBar end";
    }
    void clear()
    {
        qDebug() << "clear";
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
        qDebug() << "clear end";
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
        qDebug() << "changeStyle";
        if (!isVisible()) {
            qDebug() << "!isVisible()";
            return;
        }

        if (press) {
            qDebug() << "press";
            m_pIndicator->setPressed(press);

        } else {
            qDebug() << "!press";
            m_pIndicator->setPressed(press);
        }
        qDebug() << "changeStyle end";
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
        qDebug() << "mouseMoveEvent";
        if (!isEnabled()) {
            qDebug() << "!isEnabled()";
            return;
        }

        if (e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()) {
            qDebug() << "e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()";
            int v = position2progress(e->pos());
            qDebug() << "v" << v;
            if (e->buttons() & Qt::LeftButton) {
                qDebug() << "e->buttons() & Qt::LeftButton";
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
                qDebug() << "!e->buttons() & Qt::LeftButton";
                if (m_nVlastHoverValue != v) {
                    qDebug() << "m_nVlastHoverValue != v";
                    emit hoverChanged(v);
                }
                m_nVlastHoverValue = v;
            }
        }
        qDebug() << "mouseMoveEvent end, e->accept()";
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        qDebug() << "mousePressEvent";
        if (!m_bPress && e->buttons() == Qt::LeftButton && isEnabled()) {
            qDebug() << "!m_bPress && e->buttons() == Qt::LeftButton && isEnabled()";
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
        qDebug() << "mouseReleaseEvent";
        emit mousePressed(false);
        if (m_bPress && isEnabled()) {
            qDebug() << "m_bPress && isEnabled()";
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
        qDebug() << "resizeEvent";
        m_pBack->setFixedWidth(this->width());

        DWidget::resizeEvent(event);
    }
private:
    int  position2progress(const QPoint &p)
    {
        qDebug() << "position2progress";
        int nPosition = 0;

        if (!m_pEngine) {
            qDebug() << "!m_pEngine return 0";
            return 0;
        }

        if (p.x() < m_nStartPoint) {
            qDebug() << "p.x() < m_nStartPoint";
            nPosition = m_nStartPoint;
        } else if (p.x() > (m_nViewLength + m_nStartPoint)) {
            qDebug() << "p.x() > (m_nViewLength + m_nStartPoint)";
            nPosition = (m_nViewLength + m_nStartPoint);
        } else {
            qDebug() << "else";
            nPosition = p.x();
        }

        auto total = m_pEngine->duration();
        int span = static_cast<int>(total * (nPosition - m_nStartPoint) / m_nViewLength);
        qDebug() << "span" << span;
        return span/* * (p.x())*/;
    }

    void initMember()
    {
        qDebug() << "initMember";
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
        qDebug() << "initMemeber end";
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
        qDebug() << "ThumbnailPreview";
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
            qDebug() << "m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qDebug() << "!m_bIsWM";
            DStyle::setFrameRadius(this, 0);
        }
        m_shadow_effect = new QGraphicsDropShadowEffect(this);
        qDebug() << "ThumbnailPreview end";
    }

    void updateWithPreview(const QPixmap &pm, qint64 secs, int rotation)
    {
        qDebug() << "updateWithPreview";
        QPixmap rounded;
        if (m_bIsWM) {
            qDebug() << "m_bIsWM";
            rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);
        } else {
            qDebug() << "!m_bIsWM";
            rounded = pm;
        }

        if (rounded.width() == 0) {
            qDebug() << "rounded.width() == 0";
            return;
        }
        if (rounded.width() > rounded.height()) {
            qDebug() << "rounded.width() > rounded.height()";
            static int roundedH = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.width()))
                                      * rounded.height());
            QSize size(m_thumbnailFixed, roundedH);
            resizeThumbnail(rounded, size);
        } else {
            qDebug() << "rounded.width() <= rounded.height()";
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
        qDebug() << "updateWithPreview end";
    }

    void updateWithPreview(const QPoint &pos)
    {
        qDebug() << "updateWithPreview";
        move(pos.x() - this->width() / 2, pos.y() - this->height() + 10);
        if(geometry().isValid()) {
            qDebug() << "geometry().isValid()";
            show();
        }
    }
public slots:
    void slotWMChanged()
    {
        qDebug() << "slotWMChanged";
        m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
        if (m_bIsWM) {
            qDebug() << "m_bIsWM";
            DStyle::setFrameRadius(this, 8);
        } else {
            qDebug() << "!m_bIsWM";
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
        qDebug() << "resizeThumbnail";
        auto dpr = qApp->devicePixelRatio();
        pixmap.setDevicePixelRatio(dpr);
        pixmap = pixmap.scaled(size * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        int offect = 2;
        if (!m_bIsWM) {
            qDebug() << "!m_bIsWM";
            offect = 0;
        }
        this->setFixedWidth(size.width() + offect);
        this->setFixedHeight(size.height() + offect);
        qDebug() << "resizeThumbnail end";
    }

private:
    QImage m_thumbImg;
    int m_thumbnailFixed = 106;
    QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
    bool m_bIsWM{false};
};


void viewProgBarLoad::initThumb()
{
    qDebug() << "initThumb";
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
        qDebug() << "all thumbnailer functions are null, return";
        return;
    }

    m_video_thumbnailer = m_mvideo_thumbnailer();
    qDebug() << "m_video_thumbnailer" << m_video_thumbnailer;
}

void viewProgBarLoad::initMember()
{
    qDebug() << "initMember";
    m_pEngine = nullptr;
    m_pParent = nullptr;
    m_pProgBar = nullptr;
    m_pListPixmapMutex = nullptr;
    qDebug() << "initMember end";
}

void viewProgBarLoad::loadViewProgBar(QSize size)
{
    qDebug() << "loadViewProgBar";
    int pixWidget =  40;
    int num = int(m_pProgBar->width() / (40 + 1)); //number of thumbnails
    qDebug() << "num" << num;
#ifdef DTKWIDGET_CLASS_DSizeMode
    qDebug() << "DTKWIDGET_CLASS_DSizeMode";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        num = int(m_pProgBar->width() / (26 + 1));
    }
#endif
    int tmp = (num == 0) ? 0: (m_pEngine->duration() * 1000) / num;

    QList<QPixmap> pmList;
    QList<QPixmap> pmBlackList;

    QTime time(0, 0, 0, 0);
    if (m_pEngine->videoSize().width() > 0 && m_pEngine->videoSize().height() > 0) {
        m_video_thumbnailer->thumbnail_size = (static_cast<int>(50 * (m_pEngine->videoSize().width() / m_pEngine->videoSize().height() * 50)
                                                                * qApp->devicePixelRatio()));
    }

    if (m_image_data == nullptr) {
        qDebug() << "m_image_data == nullptr";
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    }

//    m_video_thumbnailer->seek_time = d.toString("hh:mm:ss").toLatin1().data();
    int length = strlen(time.toString("hh:mm:ss").toLatin1().data());
    memcpy(m_seekTime, time.toString("hh:mm:ss").toLatin1().data(), length + 1);
    m_video_thumbnailer->seek_time = m_seekTime;

    auto url = m_pEngine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    bool command = false;
    if(m_pEngine->duration() < 300) {
        qDebug() << "m_pEngine->duration() < 300";
        QProcess process;
        process.setProgram("ffmpeg");
        QStringList options;
        options << "-c" << QString("ffprobe -v quiet -show_frames %1 | grep \"pict_type=I\" | wc -l").arg(url.toLocalFile());
        process.start("/bin/bash", options);
        process.waitForFinished();
        process.waitForReadyRead();

        QString comStr = process.readAllStandardOutput();
        QString str = comStr.trimmed();
        int pictI = str.toInt();
        if (pictI < 5)
            command = true;
        process.close();
    }

    if (command) {
        qDebug() << "command";
        QDir dir;
        dir.mkpath("/tmp/Movie/");
        dir.cd("/tmp/Movie/");

        QProcess process;
        process.setProgram("ffmpeg");
        QStringList argList;
        argList << "-i" << url.toLocalFile() << "-r" << QString("%1/%2").arg(num).arg(m_pEngine->duration()) << "-f" << "image2" << "/tmp/Movie/image-%05d.jpg";

        process.setArguments(argList);
        process.start();

        process.waitForFinished();

        dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot); //设置过滤
        QFileInfoList fileList = dir.entryInfoList(); // 获取所有的文件信息
        foreach (QFileInfo fileInfo, fileList) {
            if (fileInfo.isFile()) {
                QImage img(fileInfo.absoluteFilePath());
                auto img_tmp = img.scaledToHeight(50);

                pmList.append(QPixmap::fromImage(img_tmp.copy(((img.width() * 50 / img.height()) - pixWidget) / 2, 0, pixWidget, 50))); //-2 为了1px的内边框
                QImage img_black = img_tmp.convertToFormat(QImage::Format_Grayscale8);
                pmBlackList.append(QPixmap::fromImage(img_black.copy(((img.width() * 50 / img.height()) - pixWidget) / 2, 0, pixWidget, 50)));
            }
        }

        dir.removeRecursively();
    } else {
        qDebug() << "!command";
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
                qDebug() << "m_mvideo_thumbnailer_generate_thumbnail_to_buffer";
                m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
                auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
                if (img.format() == QImage::Format_Invalid) {
                    qDebug() << "img.format() == QImage::Format_Invalid";
                    return;
                }
                auto img_tmp = img.scaledToHeight(50);


                pmList.append(QPixmap::fromImage(img_tmp.copy(img_tmp.size().width() / 2 - 4, 0, pixWidget, 50))); //-2 为了1px的内边框
                QImage img_black = img_tmp.convertToFormat(QImage::Format_Grayscale8);
                pmBlackList.append(QPixmap::fromImage(img_black.copy(img_black.size().width() / 2 - 4, 0, pixWidget, 50)));

            } catch (const std::logic_error &) {
                qDebug() << "const std::logic_error &";
            }
        }

        m_mvideo_thumbnailer_destroy_image_data(m_image_data);
        m_image_data = nullptr;
    }

    qDebug() << "m_pListPixmapMutex->lock()";
    m_pListPixmapMutex->lock();
    qDebug() << "m_pParent->addpmList(pmList)";
    m_pParent->addpmList(pmList);
    m_pParent->addpmBlackList(pmBlackList);
    m_pListPixmapMutex->unlock();
    emit sigFinishiLoad(size);
//    emit finished();
}
/**
 * @brief ToolboxProxy 构造函数
 * @param mainWindow 主窗口
 * @param pPlayerEngine 播放引擎对象指针
 */
ToolboxProxy::ToolboxProxy(QWidget *mainWindow, PlayerEngine *proxy)
    : DFloatingWidget(mainWindow),
      m_pMainWindow(static_cast<MainWindow *>(mainWindow)),
      m_pEngine(proxy)
{
    qDebug() << "Initializing ToolboxProxy";
    initMember();
    if (utils::check_wayland_env()) {
        qDebug() << "Wayland environment detected, disabling thumbnail mode";
        m_bThumbnailmode = false;
    }

    m_pPreviewer = new ThumbnailPreview;
    m_pPreviewTime = new SliderTime;
    m_pPreviewTime->hide();
    m_mircastWidget = new MircastWidget(mainWindow, proxy);
    m_mircastWidget->hide();
    m_pListWgt = new PlaylistBack(mainWindow);
    m_pListWgt->hide();

    setup();
    slotThemeTypeChanged();

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,
            this, &ToolboxProxy::updatePlayState);
    connect(m_mircastWidget, &MircastWidget::updatePlayStatus, this, &ToolboxProxy::updatePlayState);
    connect(m_mircastWidget, &MircastWidget::updateTime, this, &ToolboxProxy::updateMircastTime, Qt::QueuedConnection);
    qDebug() << "ToolboxProxy initialized";
}
void ToolboxProxy::finishLoadSlot(QSize size)
{
    qDebug() << "Entering ToolboxProxy::finishLoadSlot() - size:" << size;

    if (m_pmList.isEmpty()) {
        qWarning() << "No thumbnails available, cannot display thumbnail mode";
        qDebug() << "Exiting ToolboxProxy::finishLoadSlot() - empty thumbnail list";
        return;
    }
    if (!m_bThumbnailmode) {
        qDebug() << "Thumbnail mode disabled, not displaying thumbnails";
        qDebug() << "Exiting ToolboxProxy::finishLoadSlot() - thumbnail mode disabled";
        return;
    }

    qDebug() << "Setting up view progress bar with" << m_pmList.size() << "thumbnails";
    m_pViewProgBar->setViewProgBar(m_pEngine, m_pmList, m_pmBlackList);

    if(CompositingManager::get().platform() == Platform::X86) {
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            PlayItemInfo info = m_pEngine->playlist().currentInfo();
            if (!info.url.isLocalFile()) {
                qDebug() << "Current file is not local, skipping thumbnail mode display";
                qDebug() << "Exiting ToolboxProxy::finishLoadSlot() - non-local file";
                return;
            }
            qDebug() << "Switching to thumbnail view mode (index 2)";
            m_pProgBar_Widget->setCurrentIndex(2);
        }
    }

    qDebug() << "Exiting ToolboxProxy::finishLoadSlot()";
}

void ToolboxProxy::setthumbnailmode()
{
    qDebug() << "Entering ToolboxProxy::setthumbnailmode() - engine state:" << m_pEngine->state();

    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "Engine is idle, skipping thumbnail mode initialization";
        qDebug() << "Exiting ToolboxProxy::setthumbnailmode() - idle state";
        return;
    }

    if(CompositingManager::get().platform() == Platform::X86 && CompositingManager::isMpvExists()) {
        if (Settings::get().isSet(Settings::ShowThumbnailMode)) {
            qDebug() << "Enabling thumbnail mode based on user settings";
            m_bThumbnailmode = true;
            qDebug() << "Requesting thumbnail generation";
            updateThumbnail();
        } else {
            qDebug() << "Disabling thumbnail mode based on user settings";
            m_bThumbnailmode = false;
            qDebug() << "Updating movie progress with standard progress bar";
            updateMovieProgress();
            qDebug() << "Restoring standard progress bar mode (index 1)";
            m_pProgBar_Widget->setCurrentIndex(1);   //恢复进度条模式 by zhuyuliang
        }
    } else {
        qWarning() << "Platform not supported for thumbnail mode, disabling";
        m_bThumbnailmode = false;
        qDebug() << "Updating movie progress with standard progress bar";
        updateMovieProgress();
    }

    qDebug() << "Exiting ToolboxProxy::setthumbnailmode()";
}

void ToolboxProxy::setup()
{
    qDebug() << "setup";
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
    bot_widget->setBlurEnabled(true);
    bot_widget->setMode(DBlurEffectWidget::GaussianBlur);
    auto type = DGuiApplicationHelper::instance()->themeType();
    THEME_TYPE(type);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &ToolboxProxy::slotThemeTypeChanged);

    bot_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout *botv = new QVBoxLayout(bot_widget);
    botv->setContentsMargins(0, 0, 0, 0);
    botv->setSpacing(10);
    botv->setAlignment(Qt::AlignVCenter);

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
    bot_layout->setAlignment(Qt::AlignVCenter);
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

    connect(m_pProgBar, &DMRSlider::sigUnsupported, this, &ToolboxProxy::sigUnsupported);
    connect(m_pPreviewer, &ThumbnailPreview::leavePreview, this, &ToolboxProxy::slotLeavePreview);
    connect(&Settings::get(), &Settings::baseChanged, this, &ToolboxProxy::setthumbnailmode);
    connect(m_pEngine, &PlayerEngine::siginitthumbnailseting, this, &ToolboxProxy::setthumbnailmode);

    //刷新显示预览当前时间的label
    connect(m_pProgBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(m_pProgBar, &DMRSlider::sliderMoved, this, &ToolboxProxy::progressHoverChanged);
    connect(m_pProgBar, &DMRSlider::leave, this, &ToolboxProxy::slotHidePreviewTime);

    connect(m_pProgBar, &DMRSlider::sliderPressed, this, &ToolboxProxy::slotSliderPressed);
    connect(m_pProgBar, &DMRSlider::sliderReleased, this, &ToolboxProxy::slotSliderReleased);
    connect(&Settings::get(), &Settings::baseMuteChanged, this, &ToolboxProxy::slotBaseMuteChanged);

    m_pViewProgBar = new ViewProgBar(m_pProgBar, m_pBotToolWgt);


    //刷新显示预览当前时间的label
    connect(m_pViewProgBar, &ViewProgBar::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(m_pViewProgBar, &ViewProgBar::leave, this, &ToolboxProxy::slotHidePreviewTime);
    connect(m_pViewProgBar, &ViewProgBar::mousePressed, this, &ToolboxProxy::updateTimeVisible);

    QSignalMapper *signalMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);
#else
    connect(signalMapper, &QSignalMapper::mappedString,
            this, &ToolboxProxy::buttonClicked);
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

    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
        //lmh0706,延时
        connect(m_pNextBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
        connect(m_pPlayBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
        connect(m_pPrevBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
    }

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

    m_pVolSlider = new VolumeSlider(m_pMainWindow, m_pMainWindow);
    m_pVolSlider->setObjectName(VOLUME_SLIDER_WIDGET);

    connect(m_pVolBtn, &VolumeButton ::sigUnsupported, this, &ToolboxProxy::sigUnsupported);
    connect(m_pVolBtn, &VolumeButton::clicked, this, &ToolboxProxy::slotVolumeButtonClicked);
    connect(m_pVolBtn, &VolumeButton::leaved, m_pVolSlider, &VolumeSlider::delayedHide);
    connect(m_pVolSlider, &VolumeSlider::sigVolumeChanged, this, &ToolboxProxy::slotVolumeChanged);
    connect(m_pVolSlider, &VolumeSlider::sigMuteStateChanged, this, &ToolboxProxy::slotMuteStateChanged);
    connect(m_pVolBtn, &VolumeButton::requestVolumeUp, m_pVolSlider, &VolumeSlider::volumeUp);
    connect(m_pVolBtn, &VolumeButton::requestVolumeDown, m_pVolSlider, &VolumeSlider::volumeDown);

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
    connect(m_mircastWidget, &MircastWidget::mircastState, this, &ToolboxProxy::slotUpdateMircast);

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
    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
        initToolTip();
    } else {
        qDebug() << "!utils::check_wayland_env()";
        TooltipHandler *th = new TooltipHandler(this);
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
                connect(btn, &ToolButton::entered, this, &ToolboxProxy::buttonEnter);
                connect(btn, &ToolButton::leaved, this, &ToolboxProxy::buttonLeave);
            }
        }
    }

    connect(m_pEngine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updateButtonStates);   // 控件状态变化由updateButtonStates统一处理
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::slotFileLoaded);
    connect(m_pEngine, &PlayerEngine::elapsedChanged, this, &ToolboxProxy::slotElapsedChanged);
    connect(m_pEngine, &PlayerEngine::updateDuration, this, &ToolboxProxy::slotElapsedChanged);

    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &ToolboxProxy::updateFullState);

    connect(m_pEngine, &PlayerEngine::tracksChanged, this, &ToolboxProxy::updateButtonStates);
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::updateButtonStates);
    connect(&m_pEngine->playlist(), &PlaylistModel::countChanged, this, &ToolboxProxy::updateButtonStates);
    connect(m_pMainWindow, &MainWindow::initChanged, this, &ToolboxProxy::updateButtonStates);

#ifdef DTKWIDGET_CLASS_DSizeMode
    qDebug() << "DTKWIDGET_CLASS_DSizeMode";
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
        qDebug() << "sizeModeChanged";
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
                updateThumbnail();
                updateMovieProgress();
            }
            m_pProgBar_Widget->setCurrentIndex(1);
        }
        if (utils::check_wayland_env() && m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::State::Opened)
        {
            slotPlayListStateChange(true);
        }
    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, progBarspec, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        qDebug() << "sizeModeChanged";
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

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &ToolboxProxy::slotApplicationStateChanged);
}

void ToolboxProxy::updateThumbnail()
{
    qDebug() << "Updating thumbnail";
    disconnect(m_pWorker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));

    if (m_pEngine->currFileIsAudio()) {
        qDebug() << "Current file is audio, skipping thumbnail update";
        return;
    }

    qDebug() << "Starting thumbnail worker";
    QTimer::singleShot(1000, this, &ToolboxProxy::slotUpdateThumbnailTimeOut);
}

void ToolboxProxy::updatePreviewTime(qint64 secs, const QPoint &pos)
{
    qDebug() << "Updating preview time:" << secs << "at position:" << pos;
    QTime time(0, 0, 0);
    QString strTime = time.addSecs(static_cast<int>(secs)).toString("hh:mm:ss");
    m_pPreviewTime->setTime(strTime);
    m_pPreviewTime->show(pos.x(), pos.y() + 14);
}

void ToolboxProxy::initMember()
{
    qDebug() << "initMember";
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
    qDebug() << "initMember end";
}

/**
 * @brief closeAnyPopup 关闭所有弹窗效果
 */
void ToolboxProxy::closeAnyPopup()
{
    qDebug() << "closeAnyPopup";
    if (m_pPreviewer->isVisible()) {
        m_pPreviewer->hide();
        qInfo() << "hide previewer";
    }

    if (m_pPreviewTime->isVisible()) {
        qInfo() << "hide preview time";
        m_pPreviewTime->hide();
    }

    if (m_pVolSlider->isVisible()) {
        qInfo() << "hide vol slider";
        m_pVolSlider->stopTimer();
        m_pVolSlider->hide();
    }
    qDebug() << "closeAnyPopup end";
}
/**
 * @brief anyPopupShown 是否存在一些弹出显示窗口
 * @return true时为有，false为无
 */
bool ToolboxProxy::anyPopupShown() const
{
    //返回鼠标悬停缩略图、鼠标悬停时间弹窗、音量弹窗是否有弹出
    return m_pPreviewer->isVisible() || m_pPreviewTime->isVisible() || m_pVolSlider->isVisible();
}

void ToolboxProxy::updateHoverPreview(const QUrl &url, int secs)
{
    qDebug() << "updateHoverPreview";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Idle";
        return;
    }

    if (m_pEngine->playlist().currentInfo().url != url) {
        qDebug() << "m_pEngine->playlist().currentInfo().url != url";
        return;
    }

    if (!Settings::get().isSet(Settings::PreviewOnMouseover)) {
        qDebug() << "!Settings::get().isSet(Settings::PreviewOnMouseover)";
        return;
    }

    if (m_pVolSlider->isVisible()) {
        qDebug() << "m_pVolSlider->isVisible()";
        return;
    }

    const PlayItemInfo &pif = m_pEngine->playlist().currentInfo();
    if (!pif.url.isLocalFile()) {
        qDebug() << "!pif.url.isLocalFile()";
        return;
    }

    const QString &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        qDebug() << "!QFile::exists(absPath)";
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
        return;
    }

    if (!m_bMouseFlag) {
        qDebug() << "!m_bMouseFlag";
        return;
    }

    int nPosition = 0;
    qint64 nDuration = m_pEngine->duration();
    QPoint showPoint;

    if(nDuration<=0)
    {
        qDebug() << "nDuration <= 0";
        return;
    }
    qDebug() << "m_pProgBar->isVisible()";
    if (m_pProgBar->isVisible()) {
        qDebug() << "m_pProgBar->isVisible()";
        nPosition = (secs * m_pProgBar->slider()->width()) / nDuration;
        showPoint = m_pProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    } else {
        qDebug() << "!m_pProgBar->isVisible()";
        nPosition = secs * m_pViewProgBar->getViewLength() / nDuration + m_pViewProgBar->getStartPoint();
        showPoint = m_pViewProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    }

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);


    if (!pm.isNull()) {
        QPoint point { showPoint.x(), showPoint.y() };
        qDebug() << "m_pPreviewer->updateWithPreview(pm, secs, m_pEngine->videoRotation())";
        m_pPreviewer->updateWithPreview(pm, secs, m_pEngine->videoRotation());
    }
}

void ToolboxProxy::waitPlay()
{
    qDebug() << "waitPlay";
    if (m_pPlayBtn) {
        qDebug() << "m_pPlayBtn";
        m_pPlayBtn->setEnabled(false);
    }
    if (m_pPrevBtn) {
        qDebug() << "m_pPrevBtn";
        m_pPrevBtn->setEnabled(false);
    }
    if (m_pNextBtn) {
        qDebug() << "m_pNextBtn";
        m_pNextBtn->setEnabled(false);
    }
    QTimer::singleShot(500, [ = ] {
        if (m_pPlayBtn)
        {
            qDebug() << "m_pPlayBtn";
            m_pPlayBtn->setEnabled(true);
        }
        if (m_pPrevBtn && m_pEngine->playlist().count() > 1)
        {
            qDebug() << "m_pPrevBtn";
            m_pPrevBtn->setEnabled(true);
        }
        if (m_pNextBtn && m_pEngine->playlist().count() > 1)
        {
            qDebug() << "m_pNextBtn";
            m_pNextBtn->setEnabled(true);
        }
    });
    qDebug() << "waitPlay end";
}

void ToolboxProxy::slotThemeTypeChanged()
{
    qDebug() << "slotThemeTypeChanged";
    QPalette textPalette;
    bool bRawFormat = false;
    auto type = DGuiApplicationHelper::instance()->themeType();
    WAYLAND_BLACK_WINDOW;
    THEME_TYPE(type);

    // 组合按钮无边框
    QColor framecolor("#FFFFFF");
    framecolor.setAlphaF(0.00);
    QString rStr;
    if (type == DGuiApplicationHelper::LightType) {
        qDebug() << "type == DGuiApplicationHelper::LightType";
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
        pa.setColor(DPalette::Shadow, btnframecolor);
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
        qDebug() << "type == DGuiApplicationHelper::DarkType";
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
        qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
        bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
        if(bRawFormat && !m_pEngine->currFileIsAudio()) {
            qDebug() << "bRawFormat && !m_pEngine->currFileIsAudio()";
            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);

            m_pVolBtn->setButtonEnable(false);
        }
        else if (bRawFormat) {
            qDebug() << "bRawFormat";
            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);
        }
        else {
            qDebug() << "else";
            textPalette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
            textPalette.setColor(QPalette::Text, DApplication::palette().text().color());

            m_pTimeLabel->setPalette(textPalette);
            m_pTimeLabelend->setPalette(textPalette);
            m_pFullscreentimelable->setPalette(textPalette);
            m_pFullscreentimelableend->setPalette(textPalette);

            m_pVolBtn->setButtonEnable(true);
        }
    } else {
        qDebug() << "else idle";
        textPalette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
        textPalette.setColor(QPalette::Text, DApplication::palette().text().color());

        m_pTimeLabel->setPalette(textPalette);
        m_pTimeLabelend->setPalette(textPalette);
        m_pFullscreentimelable->setPalette(textPalette);
        m_pFullscreentimelableend->setPalette(textPalette);

        m_pVolBtn->setButtonEnable(true);
    }
    qDebug() << "slotThemeTypeChanged end";
}

void ToolboxProxy::slotLeavePreview()
{
    qDebug() << "Entering ToolboxProxy::slotLeavePreview()";
    
    auto pos = m_pProgBar->mapFromGlobal(QCursor::pos());
    if (!m_pProgBar->geometry().contains(pos)) {
        qDebug() << "Cursor outside progress bar, hiding preview elements";
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
        m_pProgBar->forceLeave();
    } else {
        qDebug() << "Cursor still within progress bar, keeping preview visible";
    }
    
    qDebug() << "Exiting ToolboxProxy::slotLeavePreview()";
}

void ToolboxProxy::slotHidePreviewTime()
{
    qDebug() << "Entering ToolboxProxy::slotHidePreviewTime()";
    
    qDebug() << "Hiding thumbnail previewer";
    m_pPreviewer->hide();
    
    qDebug() << "Hiding preview time display";
    m_pPreviewTime->hide();
    
    qDebug() << "Setting mouse flag to false";
    m_bMouseFlag = false;
    
    qDebug() << "Exiting ToolboxProxy::slotHidePreviewTime()";
}

void ToolboxProxy::slotSliderPressed()
{
    qDebug() << "Entering ToolboxProxy::slotSliderPressed()";
    
    qDebug() << "Setting mouse press flag to true";
    m_bMousePree = true;
    
    qDebug() << "Exiting ToolboxProxy::slotSliderPressed()";
}

void ToolboxProxy::slotSliderReleased()
{
    qDebug() << "Entering ToolboxProxy::slotSliderReleased()";
    
    qDebug() << "Setting mouse press flag to false";
    m_bMousePree = false;
    
    int position = m_pProgBar->slider()->sliderPosition();
    qDebug() << "Slider released at position:" << position;
    
    if (m_mircastWidget->getMircastState() == MircastWidget::Screening) {
        qDebug() << "Mircast active, seeking mircast playback";
        m_mircastWidget->slotSeekMircast(position);
    } else {
        qDebug() << "Seeking local playback";
        m_pEngine->seekAbsolute(position);
    }
    
    qDebug() << "Exiting ToolboxProxy::slotSliderReleased()";
}

void ToolboxProxy::slotBaseMuteChanged(QString sk, const QVariant &/*val*/)
{
    if (sk == "base.play.mousepreview") {
        m_pProgBar->setEnableIndication(m_pEngine->state() != PlayerEngine::Idle);
    }
}

void ToolboxProxy::slotVolumeButtonClicked()
{
    qDebug() << "Entering ToolboxProxy::slotVolumeButtonClicked()";

    //与其他按键保持一致，工具栏隐藏时不响应
    if (!isVisible()) {
        qDebug() << "Toolbox not visible, ignoring volume button click";
        qDebug() << "Exiting ToolboxProxy::slotVolumeButtonClicked() - toolbox hidden";
        return;
    }

    qDebug() << "Hiding volume button tooltip";
    m_pVolBtn->hideTip();
    
    if (m_pVolSlider->getsliderstate()) {
        qDebug() << "Volume slider in transition state, ignoring click";
        qDebug() << "Exiting ToolboxProxy::slotVolumeButtonClicked() - slider in transition";
        return;
    }

    /*
     * 设置-2为已经完成第一次打开设置音量
     * -1为初始化数值
     * 大于等于零表示为已完成初始化
     */
    if (!m_pVolSlider->isVisible()) {
        qDebug() << "Volume slider not visible, showing it now";
        m_pVolSlider->show();
        m_pVolSlider->raise();
//        m_pVolSlider->popup();
    } else {
        qDebug() << "Volume slider already visible, hiding it now";
//        m_pVolSlider->popup();
        m_pVolSlider->hide();
    }

    qDebug() << "Exiting ToolboxProxy::slotVolumeButtonClicked()";
}

void ToolboxProxy::slotFileLoaded()
{
    qDebug() << "File loaded, updating progress bar";
    m_pProgBar->slider()->setRange(0, static_cast<int>(m_pEngine->duration()));
    m_pProgBar_Widget->setCurrentIndex(1);
    m_pPreviewer->setFixedSize(0, 0);
    update();
    //正在投屏时如果当前播放为音频直接播放下一首。
    if(m_pEngine->currFileIsAudio() && m_mircastWidget->getMircastState() != MircastWidget::Idel) {
        qDebug() << "Audio file detected during active mircast, handling next video";

        //如果全是音频文件则退出投屏
        bool isAllAudio = true;
        QString sNextVideoName;
        int nNextIndex = -1;
        QList<PlayItemInfo> lstItemInfo = m_pEngine->getplaylist()->items();

        qDebug() << "Scanning playlist for video files";
        for(int i = 0; i < lstItemInfo.count(); i++) {
            PlayItemInfo iteminfo = lstItemInfo.at(i);
            if(iteminfo.mi.vCodecID != -1) {
                qDebug() << "Found video file at index" << i << ":" << iteminfo.mi.filePath;
                isAllAudio = false;
                if(sNextVideoName.isNull()) {
                    sNextVideoName = iteminfo.mi.filePath;
                    nNextIndex = i;
                    break;
                }
            }
        }

        if(isAllAudio) {
            qWarning() << "All files are audio, cannot continue mircast";
            qDebug() << "Exiting mircast session";
            m_pMainWindow->slotExitMircast();
            qDebug() << "Exiting ToolboxProxy::slotFileLoaded() - all audio files";
            return;
        }

        qDebug() << "Finding current file position in playlist";
        QString sCurPath = m_pEngine->getplaylist()->currentInfo().mi.filePath;
        int nIndex = -1;
        for(int i = 0; i < lstItemInfo.count(); i++) {
            PlayItemInfo iteminfo = lstItemInfo.at(i);
            if(iteminfo.mi.filePath == sCurPath) {
                nIndex = i;
                qDebug() << "Current file found at index" << nIndex;
                break;
            }
        }

        if(nIndex == -1) {
            qWarning() << "Current file not found in playlist";
            qDebug() << "Exiting ToolboxProxy::slotFileLoaded() - file not in playlist";
            return;
        }

        if(nIndex < nNextIndex && !sNextVideoName.isNull()) {
            qDebug() << "Playing next video in sequence:" << sNextVideoName;
            m_pMainWindow->play({sNextVideoName});
        } else {
            bool isNext = true;
            qDebug() << "Searching for next video file after current position";
            for(int i = nIndex; i < lstItemInfo.count(); i++) {
                PlayItemInfo iteminfo = lstItemInfo.at(i);
                if(iteminfo.mi.vCodecID != -1) {
                    isNext = false;
                    qDebug() << "Found video to play at index" << i << ":" << iteminfo.mi.filePath;
                    m_pMainWindow->play({iteminfo.mi.filePath});
                    break;
                }
            }
            
            if(m_pEngine->getplaylist()->playMode() == PlaylistModel::OrderPlay) {
                if(isNext) {
                    qWarning() << "No more videos in order play mode, must exit mircast";
                    m_pMainWindow->slotExitMircast();
                }
                qDebug() << "Exiting ToolboxProxy::slotFileLoaded() - order play mode handling";
                return;
            }
            
            if(isNext && !sNextVideoName.isNull()){
                qDebug() << "Playing next available video in playlist:" << sNextVideoName;
                m_pMainWindow->play({sNextVideoName});
            }
        }

        qDebug() << "Exiting ToolboxProxy::slotFileLoaded() - after mircast audio handling";
        return;
    }
    qDebug() << "Calling mircast playNext()";
    m_mircastWidget->playNext();

    qDebug() << "Exiting ToolboxProxy::slotFileLoaded()";
}

void ToolboxProxy::slotElapsedChanged()
{
    qDebug() << "Entering ToolboxProxy::slotElapsedChanged()";

    if(m_mircastWidget->getMircastState() != MircastWidget::Idel) {
        qDebug() << "Mircast active, skipping elapsed time update";
        qDebug() << "Exiting ToolboxProxy::slotElapsedChanged() - mircast active";
        return;
    }

    quint64 url = static_cast<quint64>(-1);
    if (m_pEngine->playlist().current() != -1) {
        url = static_cast<quint64>(m_pEngine->duration());
        qDebug() << "Current duration from engine:" << url;
    } else {
        qDebug() << "No current playlist item, using default duration";
    }

    qint64 elapsed = m_pEngine->elapsed();
    qDebug() << "Current elapsed time:" << elapsed << "of" << url;

    //TODO(xxxpengfei):此处代码同时更新全屏的时长并未判断全屏状态，请维护同事查看是否存在优化空间
    qDebug() << "Updating standard time display labels";
    updateTimeInfo(static_cast<qint64>(url), elapsed, m_pTimeLabel, m_pTimeLabelend, true);

    qDebug() << "Updating fullscreen time display labels";
    updateTimeInfo(static_cast<qint64>(url), elapsed, m_pFullscreentimelable, m_pFullscreentimelableend, false);

    QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "Setting minimum widths for fullscreen time labels (Qt5)";
    m_pFullscreentimelable->setMinimumWidth(fm.width(m_pFullscreentimelable->text()));
    m_pFullscreentimelableend->setMinimumWidth(fm.width(m_pFullscreentimelableend->text()));
#else
    qDebug() << "Setting minimum widths for fullscreen time labels (Qt6)";
    m_pFullscreentimelable->setMinimumWidth(fm.horizontalAdvance(m_pFullscreentimelable->text()));
    m_pFullscreentimelableend->setMinimumWidth(fm.horizontalAdvance(m_pFullscreentimelableend->text()));
#endif
    qDebug() << "Updating movie progress display";
    updateMovieProgress();
    qDebug() << "Exiting ToolboxProxy::slotElapsedChanged()";
}

void ToolboxProxy::slotApplicationStateChanged(Qt::ApplicationState e)
{
    qDebug() << "slotApplicationStateChanged";
    if (e == Qt::ApplicationInactive && anyPopupShown()) {
        qDebug() << "e == Qt::ApplicationInactive && anyPopupShown()";
        closeAnyPopup();
    }
    qDebug() << "slotApplicationStateChanged end";
}

void ToolboxProxy::slotPlayListStateChange(bool isShortcut)
{
    qDebug() << "slotPlayListStateChange";
    if (m_bAnimationFinash == false) {
        qDebug() << "m_bAnimationFinash == false";
        return;
    }

    closeAnyPopup();

/**
 * 此处在动画执行前设定好ToolboxProxy的起始位置和终止位置
 * 基于 MainWindow::updateProxyGeometry所设置的初始状态 以及 是否是紧凑模式 定位ToolboxProxy的起始位置和终止位置
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

    if (m_pPlaylist->state() == PlaylistWidget::State::Opened) {
        qDebug() << "m_pPlaylist->state() == PlaylistWidget::State::Opened";
        //非x86平台播放列表切换不展示动画,故按键状态不做限制
        if(CompositingManager::get().platform() == Platform::X86) {
            qDebug() << "CompositingManager::get().platform() == Platform::X86";
            if (isShortcut && m_pListBtn->isChecked()) {
                m_pListBtn->setIcon(QIcon(":/icons/deepin/builtin/light/checked/episodes_checked.svg"));
            } else {
                m_pListBtn->setChecked(true);
                m_pListBtn->setIcon(QIcon(":/icons/deepin/builtin/light/checked/episodes_checked.svg"));
            }

            m_bAnimationFinash = false;
            m_pPaOpen = new QPropertyAnimation(this, "geometry");
            m_pPaOpen->setEasingCurve(QEasingCurve::Linear);
            m_pPaOpen->setDuration(POPUP_DURATION) ;
            m_pPaOpen->setStartValue(rc_closed);
            m_pPaOpen->setEndValue(rc_opened);
            m_pPaOpen->start();
            connect(m_pPaOpen, &QPropertyAnimation::finished, this, &ToolboxProxy::slotProAnimationFinished);
        } else {
            qDebug() << "CompositingManager::get().platform() != Platform::X86";
            Q_UNUSED(isShortcut);
            if(utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                m_pListWgt->setGeometry(rc_opened.adjusted(-10,-10,10,10));
                m_pListWgt->show();
                m_pListWgt->raise();
                m_pPlaylist->raise();
                raise();
            } else {
                qDebug() << "!utils::check_wayland_env()";
                setGeometry(rc_opened);
            }

            m_pListBtn->setChecked(true);
        }
    } else {
        if(CompositingManager::get().platform() == Platform::X86) {
            qDebug() << "CompositingManager::get().platform() == Platform::X86";
            m_bAnimationFinash = false;

            if (isShortcut && !m_pListBtn->isChecked()) {
                m_pListBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
            } else {
                m_pListBtn->setChecked(false);
                m_pListBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
            }
            m_pPaClose = new QPropertyAnimation(this, "geometry");
            m_pPaClose->setEasingCurve(QEasingCurve::Linear);
            m_pPaClose->setDuration(POPUP_DURATION);
            m_pPaClose->setStartValue(rc_opened);
            m_pPaClose->setEndValue(rc_closed);
            m_pPaClose->start();
            connect(m_pPaClose, &QPropertyAnimation::finished, this, &ToolboxProxy::slotProAnimationFinished);
        } else {
            Q_UNUSED(isShortcut);
            if(utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                m_pListWgt->hide();
            } else {
                qDebug() << "!utils::check_wayland_env()";
                setGeometry(rc_closed);
            }
            m_pListBtn->setChecked(false);
        }
    }
}

void ToolboxProxy::slotUpdateThumbnailTimeOut()
{
    qDebug() << "Entering ToolboxProxy::slotUpdateThumbnailTimeOut()";
    
    //如果视频长度小于1s应该直接返回不然会UI错误
    if (m_pEngine->duration() < 1) {
        qWarning() << "Video duration too short (<1s), cannot generate thumbnails";
        qDebug() << "Exiting ToolboxProxy::slotUpdateThumbnailTimeOut() - invalid duration";
        return;
    }

    qDebug() << "Clearing previous thumbnails from progress bar";
    m_pViewProgBar->clear();  //清除前一次进度条中的缩略图,以便显示新的缩略图
    qDebug() << "Clearing thumbnail image lists";
    m_listPixmapMutex.lock();
    m_pmList.clear();
    m_pmBlackList.clear();
    m_listPixmapMutex.unlock();

    if (m_pWorker == nullptr) {
        qDebug() << "Creating new thumbnail worker";
        m_pWorker = new viewProgBarLoad(m_pEngine, m_pProgBar, this);
        m_pWorker->setListPixmapMutex(&m_listPixmapMutex);
    } else {
        qDebug() << "Interrupting existing thumbnail worker";
    }
    m_pWorker->requestInterruption();
    qDebug() << "Scheduling thumbnail worker to start in 500ms";
    QTimer::singleShot(500, this, [ = ] {
        qDebug() << "Starting thumbnail worker thread";
        m_pWorker->start();
    });

    qDebug() << "Connecting thumbnail worker completion signal";
    connect(m_pWorker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
    qDebug() << "Setting progress bar widget to standard mode (index 1)";
    m_pProgBar_Widget->setCurrentIndex(1);
    qDebug() << "Exiting ToolboxProxy::slotUpdateThumbnailTimeOut()";
}

void ToolboxProxy::slotProAnimationFinished()
{
    qDebug() << "slotProAnimationFinished";
    m_pListBtn->setEnabled(true);
    QObject *pProAnimation = sender();
    if (pProAnimation == m_pPaOpen) {   
        qDebug() << "pProAnimation == m_pPaOpen";
        m_pPaOpen->deleteLater();
        m_pPaOpen = nullptr;
        m_bAnimationFinash = true;
    } else if (pProAnimation == m_pPaClose) {
        qDebug() << "pProAnimation == m_pPaClose";
        m_pPaClose->deleteLater();
        m_pPaClose = nullptr;
        m_bAnimationFinash = true;
        //Wait for the animation to end before setting the focus
        if (m_bSetListBtnFocus) {
            m_pListBtn->setFocus();
        }
    }
    qDebug() << "slotProAnimationFinished end";
//    m_bSetListBtnFocus = false;
}

void ToolboxProxy::slotVolumeChanged(int nVolume)
{
    qDebug() << "slotVolumeChanged";
    m_pVolBtn->setVolume(nVolume);

    emit sigVolumeChanged(nVolume);
}

void ToolboxProxy::slotMuteStateChanged(bool bMute)
{
    qDebug() << "slotMuteStateChanged";
    m_pVolBtn->setMute(bMute);

    emit sigMuteStateChanged(bMute);
}

qint64 ToolboxProxy::getMouseTime()
{
    qDebug() << "getMouseTime";
    return m_nClickTime;
}

void ToolboxProxy::clearPlayListFocus()
{
    qDebug() << "clearPlayListFocus";
    if (m_pPlaylist->isFocusInPlaylist()) {
        qDebug() << "m_pPlaylist->isFocusInPlaylist()";
        m_pPlaylist->clearFocus();
    }
    m_bSetListBtnFocus = false;
}

void ToolboxProxy::setBtnFocusSign(bool sign)
{
    qDebug() << "setBtnFocusSign";
    m_bSetListBtnFocus = sign;
}

bool ToolboxProxy::isInMircastWidget(const QPoint &p)
{
    qDebug() << "isInMircastWidget";
    if (!m_mircastWidget->isVisible()) {
        qDebug() << "!m_mircastWidget->isVisible()";
        return false;
    }
    return m_mircastWidget->geometry().contains(p);
}

void ToolboxProxy::updateMircastWidget(QPoint p)
{
    qDebug() << "updateMircastWidget";
    m_mircastWidget->move(p.x() - m_mircastWidget->width(), p.y() - m_mircastWidget->height() - 10);
}

void ToolboxProxy::hideMircastWidget()
{
    qDebug() << "hideMircastWidget";
    m_mircastWidget->hide();
    m_pMircastBtn->setChecked(false);
    m_pMircastBtn->setIcon(QIcon::fromTheme("dcc_mircast"));
}
/**
 * @brief volumeUp 鼠标滚轮增加音量
 */
void ToolboxProxy::volumeUp()
{
    qDebug() << "volumeUp";
    if(!m_pVolSlider->isEnabled()) {    // 不能调节音量需要给出提示
        qDebug() << "!m_pVolSlider->isEnabled()";
        emit sigUnsupported();
    } else {
        qDebug() << "m_pVolSlider->volumeUp()";
        m_pVolSlider->volumeUp();
    }
}
/**
 * @brief volumeUp 鼠标滚轮减少音量
 */
void ToolboxProxy::volumeDown()
{
    qDebug() << "volumeDown";
    if(!m_pVolSlider->isEnabled()) {
        qDebug() << "!m_pVolSlider->isEnabled()";
        emit sigUnsupported();
    } else {
        qDebug() << "m_pVolSlider->volumeDown()";
        m_pVolSlider->volumeDown();
    }
}
/**
 * @brief calculationStep 计算鼠标滚轮滚动的步进
 * @param iAngleDelta 鼠标滚动的距离
 */
void ToolboxProxy::calculationStep(int iAngleDelta)
{
    qDebug() << "calculationStep";
    m_pVolSlider->calculationStep(iAngleDelta);
}
/**
 * @brief changeMuteState 切换静音模式
 */
void ToolboxProxy::changeMuteState()
{
    qDebug() << "changeMuteState";
    m_pVolSlider->muteButtnClicked();
}
/**
 * @brief playlistClosedByEsc Esc关闭播放列表
 */
void ToolboxProxy::playlistClosedByEsc()
{
    qDebug() << "playlistClosedByEsc";
    if (m_pPlaylist->isFocusInPlaylist() && m_bSetListBtnFocus) {
        qDebug() << "m_pPlaylist->isFocusInPlaylist() && m_bSetListBtnFocus";
//        m_bSetListBtnFocus = true;
        m_pMainWindow->requestAction(ActionFactory::TogglePlaylist);
//        m_pListBtn->setFocus();   //焦点回到播放列表按钮
    }
    qDebug() << "playlistClosedByEsc end";
}

void ToolboxProxy::progressHoverChanged(int nValue)
{
    qDebug() << "progressHoverChanged";
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Idle";
        return;
    }

    if (m_pVolSlider->isVisible()) {
        qDebug() << "m_pVolSlider->isVisible()";
        return;
    }

    const auto &pif = m_pEngine->playlist().currentInfo();
    if (!pif.url.isLocalFile()) {
        qDebug() << "!pif.url.isLocalFile()";
        return;
    }
    const auto &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        qDebug() << "!QFile::exists(absPath)";
        m_pPreviewer->hide();
        m_pPreviewTime->hide();
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
    qDebug() << "point.x() < startPoint.x()";
    if (point.x() < startPoint.x()) {
        point.setX(startPoint.x());
    }

    qDebug() << "point.x() > endPoint.x()";
    if (point.x() > endPoint.x()) {
        point.setX(endPoint.x());
    }

    qDebug() << "bIsAudio";
    bool bIsAudio = m_pEngine->currFileIsAudio();
    if (!Settings::get().isSet(Settings::PreviewOnMouseover) || bIsAudio) {
        qDebug() << "!Settings::get().isSet(Settings::PreviewOnMouseover) || bIsAudio";
        updatePreviewTime(nValue, point);
        return;
    }
    //鼠标移动时同步缩略图显示位置
    qDebug() << "鼠标移动时同步缩略图显示位置";
    int nPosition = 0;
    qint64 nDuration = m_pEngine->duration();

    if(nDuration<=0)
    {
        qDebug() << "nDuration <= 0";
        return;
    }

    qDebug() << "m_pProgBar->isVisible()";
    if (m_pProgBar->isVisible()) {
        qDebug() << "m_pProgBar->isVisible()";
        nPosition = (nValue * m_pProgBar->slider()->width()) / nDuration;
        point = m_pProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    } else {
        qDebug() << "!m_pProgBar->isVisible()";
        nPosition = nValue * m_pViewProgBar->getViewLength() / nDuration + m_pViewProgBar->getStartPoint();
        point = m_pViewProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    }
    qDebug() << "m_pPreviewer->updateWithPreview(point)";
    m_pPreviewer->updateWithPreview(point);
    if(CompositingManager::isMpvExists()) {
        qDebug() << "CompositingManager::isMpvExists()";
        ThumbnailWorker::get().requestThumb(pif.url, nValue);
    }
    qDebug() << "progressHoverChanged end";
}

void ToolboxProxy::updateTimeVisible(bool visible)
{
    qDebug() << "updateTimeVisible";
    if (Settings::get().isSet(Settings::PreviewOnMouseover)) {
        qDebug() << "Settings::get().isSet(Settings::PreviewOnMouseover)";
        return;
    }

    if (m_pPreviewTime) {
        qDebug() << "m_pPreviewTime";
        m_pPreviewTime->setVisible(!visible);
    }
}

void ToolboxProxy::updateMovieProgress()
{
    qDebug() << "updateMovieProgress";
    if (m_bMousePree == true) {
        qDebug() << "m_bMousePree == true";
        return ;
    }
    auto d = m_pEngine->duration();
    auto e = m_pEngine->elapsed();
    if (d > m_pProgBar->maximum()) {
        qDebug() << "d > m_pProgBar->maximum()";
        d = m_pProgBar->maximum();
    }
    int v = 0;
    int v2 = 0;
    if (d != 0 && e != 0) {
        qDebug() << "d != 0 && e != 0";
        v = static_cast<int>(m_pProgBar->maximum() * e / d);
        v2 = static_cast<int>(m_pViewProgBar->getViewLength() * e / d + m_pViewProgBar->getStartPoint());
    }
    if (!m_pProgBar->signalsBlocked()) {
        qDebug() << "!m_pProgBar->signalsBlocked()";
        m_pProgBar->blockSignals(true);
        m_pProgBar->setValue(v);
        m_pProgBar->blockSignals(false);
    }
    if (!m_pViewProgBar->getIsBlockSignals()) {
        qDebug() << "!m_pViewProgBar->getIsBlockSignals()";
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(v2);
        m_pViewProgBar->setTime(e);
        m_pViewProgBar->setIsBlockSignals(false);
    }
}

void ToolboxProxy::updateButtonStates()
{
    qDebug() << "updateButtonStates";
    QPalette palette;              // 时长显示的颜色，在某些情况下变化字体颜色区别功能
    bool bRawFormat = false;

    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "DGuiApplicationHelper::LightType";
        palette.setColor(QPalette::WindowText, QColor(0, 0, 0, 40));       // 浅色背景下置灰
        palette.setColor(QPalette::Text, QColor(0, 0, 0, 40));
    } else {
        qDebug() << "DGuiApplicationHelper::DarkType";
        palette.setColor(QPalette::WindowText, QColor(255, 255, 255, 40)); // 深色背景下置灰
        palette.setColor(QPalette::Text, QColor(255, 255, 255, 40));
    }

    if(m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
        bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
        m_pMircastBtn->setEnabled(!m_pEngine->currFileIsAudio());
        if(m_pEngine->currFileIsAudio()) {
            qDebug() << "m_pEngine->currFileIsAudio()";
            m_mircastWidget->setVisible(false);
        }
        if(bRawFormat && !m_pEngine->currFileIsAudio()){                                             // 如果正在播放的视频是裸流不支持音量调节和进度调节
            qDebug() << "bRawFormat && !m_pEngine->currFileIsAudio()";
            m_pProgBar->setEnabled(false);
            m_pProgBar->setEnableIndication(false);
            m_pVolSlider->setEnabled(false);

            m_pTimeLabel->setPalette(palette);             // 如果正在播放的视频是裸流置灰
            m_pTimeLabelend->setPalette(palette);
            m_pFullscreentimelable->setPalette(palette);
            m_pFullscreentimelableend->setPalette(palette);

            m_pVolBtn->setButtonEnable(false);
        } else if (bRawFormat) {
            qDebug() << "bRawFormat";
            m_pProgBar->setEnabled(false);
            m_pProgBar->setEnableIndication(false);

            m_pTimeLabel->setPalette(palette);
            m_pTimeLabelend->setPalette(palette);
            m_pFullscreentimelable->setPalette(palette);
            m_pFullscreentimelableend->setPalette(palette);
        } else {
            qDebug() << "else";
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
        qDebug() << "else idle";
        m_pVolSlider->setEnabled(true);

        palette.setColor(QPalette::WindowText, DApplication::palette().windowText().color());
        palette.setColor(QPalette::Text, DApplication::palette().text().color());

        m_pTimeLabel->setPalette(palette);
        m_pTimeLabelend->setPalette(palette);
        m_pFullscreentimelable->setPalette(palette);
        m_pFullscreentimelableend->setPalette(palette);

         m_pVolBtn->setButtonEnable(true);
    }

    qDebug() << "m_pEngine->playingMovieInfo().subs.size()" << m_pEngine->playingMovieInfo().subs.size();
    bool vis = m_pEngine->playlist().count() > 1 && m_pMainWindow->inited();

    //播放状态为空闲或播放列表只有一项时，将上下一曲按钮置灰
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle ||
            m_pEngine->getplaylist()->items().size() <= 1) {
        qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Idle || m_pEngine->getplaylist()->items().size() <= 1";
        m_pPrevBtn->setDisabled(true);
        m_pNextBtn->setDisabled(true);
    } else {
        qDebug() << "else not idle";
        m_pPrevBtn->setEnabled(true);
        m_pNextBtn->setEnabled(true);
    }

    m_bCanPlay = vis;  //防止连续切换上下曲目
}

void ToolboxProxy::updateFullState()
{
    qDebug() << "updateFullState";
    bool isFullscreen = window()->isFullScreen();
    qDebug() << "isFullscreen" << isFullscreen;
    if (isFullscreen || m_pFullscreentimelable->isVisible()) {
        m_pFullScreenBtn->setIcon(QIcon::fromTheme("dcc_zoomout"));
        if (utils::check_wayland_env()) {
            qDebug() << "utils::check_wayland_env()";
            m_pFullScreenBtnTip->setText(tr("Exit fullscreen"));
        } else
            m_pFullScreenBtn->setTooTipText(tr("Exit fullscreen"));
    } else {
        m_pFullScreenBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
        if (utils::check_wayland_env()) {
            qDebug() << "utils::check_wayland_env()";
            m_pFullScreenBtnTip->setText(tr("Fullscreen"));
        } else
            m_pFullScreenBtn->setTooTipText(tr("Fullscreen"));
    }
    qDebug() << "updateFullState end";
}

void ToolboxProxy::slotUpdateMircast(int state, QString msg)
{
    qDebug() << "slotUpdateMircast";
    emit sigMircastState(state, msg);
    if (state == 0) {
        qDebug() << "state == 0";
        m_pVolBtn->setButtonEnable(false);
        m_pFullScreenBtn->setEnabled(false);
    } else {
        if(m_pEngine->getplaylist()->items().size() == 0) {
            qDebug() << "m_pEngine->getplaylist()->items().size() == 0";
            m_pVolBtn->setButtonEnable(true);
        } else {
            qDebug() << "m_pEngine->getplaylist()->items().size() != 0";
            bool bRawFormat = m_pEngine->getplaylist()->currentInfo().mi.isRawFormat();
            if(bRawFormat && !m_pEngine->currFileIsAudio()) {
                qDebug() << "bRawFormat && !m_pEngine->currFileIsAudio()";
                m_pVolBtn->setButtonEnable(false);
            } else {
                qDebug() << "bRawFormat && m_pEngine->currFileIsAudio()";
                m_pVolBtn->setButtonEnable(true);
            }
        }
    }
    m_pFullScreenBtn->setEnabled(true);
    qDebug() << "slotUpdateMircast end";
}

void ToolboxProxy::updatePlayState()
{
    qDebug() << "Updating play state, mircast state:" << m_mircastWidget->getMircastState() 
             << "engine state:" << m_pEngine->state();
    if (((m_mircastWidget->getMircastState() != MircastWidget::Idel) && (m_mircastWidget->getMircastPlayState() == MircastWidget::Play))
            || m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qDebug() << "Setting play state to playing";
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            qDebug() << "DGuiApplicationHelper::LightType";
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
            qDebug() << "DGuiApplicationHelper::DarkType";
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
        if (utils::check_wayland_env()) {
            m_pPlayBtnTip->setText(tr("Pause"));
        } else {
            m_pPlayBtn->setToolTip(tr("Pause"));
        }
    } else {
        qDebug() << "Setting play state to paused";
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            qDebug() << "DGuiApplicationHelper::LightType";
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
            qDebug() << "DGuiApplicationHelper::DarkType";
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
        if (utils::check_wayland_env()) {
            m_pPlayBtnTip->setText(tr("Play"));
        } else {
            m_pPlayBtn->setToolTip(tr("Play"));
        }
        m_pPlayBtn->setIcon(QIcon::fromTheme("dcc_play", QIcon(":/icons/deepin/builtin/light/normal/play_normal.svg")));
    }

    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "Engine is idle, clearing time labels";
        m_pTimeLabel->setText("");
        m_pTimeLabelend->setText("");

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
        qDebug() << "setProperty idle true";
    } else {
        qDebug() << "setProperty idle false";
        setProperty("idle", false);
    }
}
/**
 * @brief updateTimeInfo 更新工具栏中播放时间显示
 * @param duration 视频总时长
 * @param pos 当前播放的时间点
 * @param pTimeLabel 当前播放时间
 * @param pTimeLabelend 视频总时长
 * @param flag 是否为全屏的控件
 */
void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos, QLabel *pTimeLabel, QLabel *pTimeLabelend, bool flag)
{
    qDebug() << "Updating time info - duration:" << duration << "position:" << pos << "flag:" << flag;
    if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
        pTimeLabel->setText("");
        pTimeLabelend->setText("");
    } else {
        //mpv returns a slightly different duration from movieinfo.duration
        //m_pTimeLabel->setText(QString("%2/%1").arg(utils::Time2str(duration))
        //.arg(utils::Time2str(pos)));
        if (1 == flag) {
            pTimeLabel->setText(QString("%1").arg(utils::Time2str(pos)));
            pTimeLabelend->setText(QString("%1").arg(utils::Time2str(duration)));
        } else {
            pTimeLabel->setText(QString("%1 %2").arg(utils::Time2str(pos)).arg("/"));
            pTimeLabelend->setText(QString("%1").arg(utils::Time2str(duration)));
        }
    }
}

void ToolboxProxy::buttonClicked(QString id)
{
    qDebug() << "Button clicked:" << id;
    static bool bFlags = true;
    if (bFlags) {
        m_pMainWindow->repaint();
        bFlags = false;
    }

    if (!isVisible()) {
        qDebug() << "Toolbox not visible, ignoring button click";
        return;
    }

    if (id == "play") {
        if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
            qDebug() << "Starting playback";
            m_pMainWindow->requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            qDebug() << "Toggling pause";
            m_pMainWindow->requestAction(ActionFactory::ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        qDebug() << "Toggling fullscreen";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::ToggleFullscreen);
    } else if (id == "vol") {
        qDebug() << "Toggling mute";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::ToggleMute);
    } else if (id == "prev" && m_bCanPlay) {  //如果影片未加载完成，则不播放上一曲
        qDebug() << "Playing previous item";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistPrev);
    } else if (id == "next" && m_bCanPlay) {
        qDebug() << "Playing next item";
        m_pMainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);
    } else if (id == "list") {
        qDebug() << "Toggling playlist";
        m_nClickTime = QDateTime::currentMSecsSinceEpoch();
        m_pMainWindow->requestAction(ActionFactory::ActionKind::TogglePlaylist);
        m_pListBtn->hideToolTip();
    } else if (id == "mircast") {
        qDebug() << "Toggling mircast";
        m_mircastWidget->togglePopup();
        m_pMircastBtn->hideToolTip();
        m_pMircastBtn->setChecked(m_mircastWidget->isVisible());
        if (m_pMircastBtn->isChecked())
            m_pMircastBtn->setIcon(QIcon(":/icons/deepin/builtin/light/checked/mircast_chenked.svg"));
        else
            m_pMircastBtn->setIcon(QIcon::fromTheme("dcc_mircast"));
    }
}

void ToolboxProxy::buttonEnter()
{
    qDebug() << "buttonEnter";
    if (!isVisible()) return;

    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == "sub" || id == "fs" || id == "list" || id == "mir") {
        qDebug() << "id is sub/fs/list/mir";
        updateToolTipTheme(btn);
        btn->showToolTip();
    }
}

void ToolboxProxy::buttonLeave()
{
    qDebug() << "buttonLeave";
    if (!isVisible()) return;

    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == "sub" || id == "fs" || id == "list" || id == "mir") {
        qDebug() << "id is sub/fs/mir";
        btn->hideToolTip();
    }
}

void ToolboxProxy::showEvent(QShowEvent *event)
{
    qDebug() << "showEvent";
    DFloatingWidget::showEvent(event);
}

void ToolboxProxy::paintEvent(QPaintEvent *event)
{
    if(CompositingManager::get().platform() != X86) {
        QPainter painter(this);
            setFixedWidth(m_pMainWindow->width());
            move(0, m_pMainWindow->height() - this->height());
            if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
                painter.fillRect(rect(), QBrush(QColor(31, 31, 31)));
            } else {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                painter.fillRect(rect(), this->palette().background());
#else
                painter.fillRect(rect(), this->palette().window());
#endif
            }
    } else {
        DFloatingWidget::paintEvent(event);
    }
}

void ToolboxProxy::resizeEvent(QResizeEvent *event)
{
    qDebug() << "resizeEvent";
    if (event->oldSize().width() != event->size().width()) {
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
            if (m_bThumbnailmode) {  //如果进度条为胶片模式，重新加载缩略图并显示
                if(CompositingManager::get().platform() == Platform::X86 && CompositingManager::isMpvExists()) {
                    qDebug() << "CompositingManager::get().platform() == Platform::X86 && CompositingManager::isMpvExists()";
                    updateThumbnail();
                }
                updateMovieProgress();
            }
            m_pProgBar_Widget->setCurrentIndex(1);
        }
        if (utils::check_wayland_env() && m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::State::Opened)
        {
            qDebug() << "utils::check_wayland_env() && m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::State::Opened";
            slotPlayListStateChange(true);
        }
    }

    DFloatingWidget::resizeEvent(event);
}

void ToolboxProxy::mouseMoveEvent(QMouseEvent *ev)
{
    qDebug() << "mouseMoveEvent";
    setButtonTooltipHide();
    QWidget::mouseMoveEvent(ev);
}

bool ToolboxProxy::eventFilter(QObject *obj, QEvent *ev)
{
    qDebug() << "eventFilter";
    if (obj == m_pVolBtn) {
        qDebug() << "obj == m_pVolBtn";
        if (ev->type() == QEvent::KeyPress && (m_pVolSlider->state() == VolumeSlider::Open)) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
            int nCurVolume = m_pVolSlider->getVolume();
            //如果音量条升起且上下键按下，以步长为5调整音量
            if (keyEvent->key() == Qt::Key_Up) {
                m_pVolSlider->changeVolume(qMin(nCurVolume + 5, 200));
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                m_pVolSlider->changeVolume(qMax(nCurVolume - 5, 0));
                return true;
            }
        }
    }

    if(CompositingManager::get().platform() == Platform::X86) {
        if (obj == m_pListBtn) {
            qDebug() << "obj == m_pListBtn";
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(ev);
            if (ev->type() == QEvent::MouseButtonRelease && mouseEvent->button() == Qt::LeftButton) {
                if (m_pPlaylist->state() == PlaylistWidget::State::Opened && m_pListBtn->isChecked()) {
                    qDebug() << "m_pPlaylist->state() == PlaylistWidget::State::Opened && m_pListBtn->isChecked()";
                    m_pListBtn->setChecked(!m_pListBtn->isChecked());
                }
                if (m_pPlaylist->state() == PlaylistWidget::State::Closed && !m_pListBtn->isChecked()) {
                    qDebug() << "m_pPlaylist->state() == PlaylistWidget::State::Closed && !m_pListBtn->isChecked()";
                    m_pListBtn->setChecked(!m_pListBtn->isChecked());
                }
            }
        }
    }

    return QObject::eventFilter(obj, ev);
}

void ToolboxProxy::updateMircastTime(int time)
{
    qDebug() << "updateMircastTime";
    if (m_pProgBar_Widget->currentIndex() == 1) {              //进度条模式
        qDebug() << "m_pProgBar_Widget->currentIndex() == 1";
        if (!m_pProgBar->signalsBlocked()) {
            m_pProgBar->blockSignals(true);
        }

        m_pProgBar->slider()->setSliderPosition(time);
        m_pProgBar->slider()->setValue(time);
        m_pProgBar->blockSignals(false);
    } else {
        qDebug() << "m_pProgBar_Widget->currentIndex() != 1";
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(time);
        m_pViewProgBar->setIsBlockSignals(false);
    }

    quint64 url = static_cast<quint64>(-1);
    if (m_pEngine->playlist().current() != -1) {
        qDebug() << "m_pEngine->playlist().current() != -1";
        url = static_cast<quint64>(m_pEngine->duration());
    }
    updateTimeInfo(url, time, m_pTimeLabel, m_pTimeLabelend, true);
    qDebug() << "updateMircastTime end";
}

void ToolboxProxy::updateToolTipTheme(ToolButton *btn)
{
    qDebug() << "updateToolTipTheme";
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "DGuiApplicationHelper::LightType";
        btn->changeTheme(lightTheme);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        qDebug() << "DGuiApplicationHelper::DarkType";
        btn->changeTheme(darkTheme);
    } else {
        qDebug() << "DGuiApplicationHelper::LightType";
        btn->changeTheme(lightTheme);
    }
}

void ToolboxProxy::setPlaylist(PlaylistWidget *pPlaylist)
{
    qDebug() << "setPlaylist";
    m_pPlaylist = pPlaylist;
    WAYLAND_BLACK_WINDOW;
    connect(m_pPlaylist, &PlaylistWidget::stateChange, this, &ToolboxProxy::slotPlayListStateChange);

}
QLabel *ToolboxProxy::getfullscreentimeLabel()
{
    qDebug() << "getfullscreentimeLabel";
    return m_pFullscreentimelable;
}

QLabel *ToolboxProxy::getfullscreentimeLabelend()
{
    qDebug() << "getfullscreentimeLabelend";
    return m_pFullscreentimelableend;
}

bool ToolboxProxy::getbAnimationFinash()
{
    qDebug() << "getbAnimationFinash";
    return  m_bAnimationFinash;
}

void ToolboxProxy::setVolSliderHide()
{
    qDebug() << "setVolSliderHide";
    if (m_pVolSlider->isVisible()) {
        qDebug() << "m_pVolSlider->hide";
        m_pVolSlider->hide();
    }
}

void ToolboxProxy::setButtonTooltipHide()
{
    qDebug() << "setButtonTooltipHide";
    if (utils::check_wayland_env()) {
        qDebug() << "m_pPlayBtnTip->hide";
        m_pPlayBtnTip->hide();
        m_pPrevBtnTip->hide();
        m_pNextBtnTip->hide();
        m_pFullScreenBtnTip->hide();
        m_pListBtnTip->hide();
    } else {
        qDebug() << "m_pListBtn->hideToolTip";
        m_pListBtn->hideToolTip();
        m_pFullScreenBtn->hideToolTip();
    }
}

void ToolboxProxy::initToolTip()
{
    qDebug() << "initToolTip";
    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
        //lmh0910播放
        m_pPlayBtnTip = new ButtonToolTip(m_pMainWindow);
        m_pPlayBtnTip->setText(tr("Play"));
        connect(m_pPlayBtn, &ButtonBoxButton::entered, [ = ]() {
            qDebug() << "m_pPlayBtnTip->move";
            m_pPlayBtnTip->move(80, m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_pPlayBtnTip->show();
            m_pPlayBtnTip->update();
            m_pPlayBtnTip->releaseMouse();

        });
        connect(m_pPlayBtn, &ButtonBoxButton::leaved, [ = ]() {
            qDebug() << "m_pPlayBtnTip->hide";
            QTimer::singleShot(0, [ = ] {
                m_pPlayBtnTip->hide();
            });
        });
        //lmh0910上一个
        m_pPrevBtnTip = new ButtonToolTip(m_pMainWindow);
        m_pPrevBtnTip->setText(tr("Previous"));
        connect(m_pPrevBtn, &ButtonBoxButton::entered, [ = ]() {
            qDebug() << "m_pPrevBtnTip->move";
            m_pPrevBtnTip->move(40,
                                m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_pPrevBtnTip->show();
            m_pPrevBtnTip->update();
            m_pPrevBtnTip->releaseMouse();

        });
        connect(m_pPrevBtn, &ButtonBoxButton::leaved, [ = ]() {
            qDebug() << "m_pPrevBtnTip->hide";
            QTimer::singleShot(0, [ = ] {
                m_pPrevBtnTip->hide();
            });
        });

        //lmh0910下一个
        m_pNextBtnTip = new ButtonToolTip(m_pMainWindow);
        m_pNextBtnTip->setText(tr("Next"));
        connect(static_cast<ButtonBoxButton *>(m_pNextBtn), &ButtonBoxButton::entered, [ = ]() {
            qDebug() << "m_pNextBtnTip->move";
            m_pNextBtnTip->move(120,
                                m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_pNextBtnTip->show();
            m_pNextBtnTip->update();
            m_pNextBtnTip->releaseMouse();

        });
        connect(static_cast<ButtonBoxButton *>(m_pNextBtn), &ButtonBoxButton::leaved, [ = ]() {
            qDebug() << "m_pNextBtnTip->hide";
            QTimer::singleShot(0, [ = ] {
                m_pNextBtnTip->hide();
            });
        });
    }
    //lmh0910全屏按键
    m_pFullScreenBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pFullScreenBtnTip->setText(tr("Fullscreen"));
    connect(m_pFullScreenBtn, &ToolButton::entered, [ = ]() {
        qDebug() << "m_pFullScreenBtnTip->move";
        m_pFullScreenBtnTip->move(m_pMainWindow->width() - m_pFullScreenBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 195,
                                  m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pFullScreenBtnTip->show();
        m_pFullScreenBtnTip->update();
        m_pFullScreenBtnTip->releaseMouse();

    });
    connect(m_pFullScreenBtn, &ToolButton::leaved, [ = ]() {
        qDebug() << "m_pFullScreenBtnTip->hide";
        QTimer::singleShot(0, [ = ] {
            m_pFullScreenBtnTip->hide();
        });
    });
    //lmh0910list按键
    m_pListBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pListBtnTip->setText(tr("Playlist"));
    connect(m_pListBtn, &ToolButton::entered, [ = ]() {
        qDebug() << "m_pListBtnTip->move";
        m_pListBtnTip->move(m_pMainWindow->width() - m_pListBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 20,
                            m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pListBtnTip->show();
        m_pListBtnTip->update();
        m_pListBtnTip->releaseMouse();

    });
    connect(m_pListBtn, &ToolButton::leaved, [ = ]() {
        qDebug() << "m_pListBtnTip->hide";
        QTimer::singleShot(0, [ = ] {
            m_pListBtnTip->hide();
        });
    });

    //lmh0910vol按键
    m_pVolBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pVolBtnTip->setText(tr("Volume"));
    connect(m_pVolBtn, &VolumeButton::entered, [ = ]() {
        qDebug() << "m_pVolBtnTip->move";
        m_pVolBtnTip->move(m_pMainWindow->width() - m_pVolBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 135,
                            m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pVolBtnTip->show();
        m_pVolBtnTip->update();
        m_pVolBtnTip->releaseMouse();

    });
    connect(m_pVolBtn, &VolumeButton::leaved, [ = ]() {
        qDebug() << "m_pVolBtnTip->hide";
        QTimer::singleShot(0, [ = ] {
            m_pVolBtnTip->hide();
        });
    });

    //lmh0910mircast按键
    m_pMircastBtnTip = new ButtonToolTip(m_pMainWindow);
    m_pMircastBtnTip->setText(tr("Miracast"));
    connect(m_pMircastBtn, &ToolButton::entered, [ = ]() {
        qDebug() << "m_pMircastBtnTip->move";
        m_pMircastBtnTip->move(m_pMainWindow->width() - m_pMircastBtn->width() / 2 /*- m_pPlayBtn->width()*/ - 80,
                            m_pMainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_pMircastBtnTip->show();
        m_pMircastBtnTip->update();
        m_pMircastBtnTip->releaseMouse();

    });
    connect(m_pMircastBtn, &ToolButton::leaved, [ = ]() {
        qDebug() << "m_pMircastBtnTip->hide";
        QTimer::singleShot(0, [ = ] {
            m_pMircastBtnTip->hide();
        });
    });
}

bool ToolboxProxy::getListBtnFocus()
{
    qDebug() << "getListBtnFocus";
    return m_pListBtn->hasFocus();
}

bool ToolboxProxy::getVolSliderIsHided()
{
    qDebug() << "getVolSliderIsHided";
    return m_pVolSlider->isHidden();
}
/**
 * @brief updateProgress 更新播放进度条显示
 * @param nValue 进度条的值
 */
void ToolboxProxy::updateProgress(int nValue)
{
    qDebug() << "Updating progress:" << nValue;
    int nDuration = static_cast<int>(m_pEngine->duration());

    if (m_pProgBar_Widget->currentIndex() == 1) {              //进度条模式
        qDebug() << "m_pProgBar_Widget->currentIndex() == 1";
        float value = nValue * nDuration / m_pProgBar->width();
        int nCurrPos;
        if (value > 1 || value < -1) {
            nCurrPos = m_pProgBar->value() + value;
        } else {
            if (m_processAdd < 1.0 && m_processAdd > -1.0) {
                m_processAdd += (float)(nValue * nDuration) / m_pProgBar->width();
                qDebug() << "Accumulating progress:" << m_processAdd;
                return;
            }
            else {
                nCurrPos = m_pProgBar->value() + m_processAdd;
                m_processAdd = .0;
            }
        }
        if (!m_pProgBar->signalsBlocked()) {
            m_pProgBar->blockSignals(true);
        }

        m_pProgBar->slider()->setSliderPosition(nCurrPos);
        m_pProgBar->slider()->setValue(nCurrPos);
    } else {
        qDebug() << "m_pProgBar_Widget->currentIndex() != 1";
        m_pViewProgBar->setIsBlockSignals(true);
        m_pViewProgBar->setValue(m_pViewProgBar->getValue() + nValue);
    }
}
/**
 * @brief updateSlider 根据进度条显示更新影片实际进度
 */
void ToolboxProxy::updateSlider()
{
    qDebug() << "Updating slider position";
    if (m_pProgBar_Widget->currentIndex() == 1) {
        qDebug() << "m_pProgBar_Widget->currentIndex() == 1";
        m_pEngine->seekAbsolute(m_pProgBar->value());
        m_pProgBar->blockSignals(false);
    } else {
        qDebug() << "m_pProgBar_Widget->currentIndex() != 1";
        m_pEngine->seekAbsolute(m_pViewProgBar->getTimePos());
        m_pViewProgBar->setIsBlockSignals(false);
    }
}
/**
 * @brief initThumb 初始化加载胶片线程
 */
void ToolboxProxy::initThumbThread()
{
    qDebug() << "initThumbThread";
    ThumbnailWorker::get().setPlayerEngine(m_pEngine);
    connect(&ThumbnailWorker::get(), &ThumbnailWorker::thumbGenerated,
            this, &ToolboxProxy::updateHoverPreview);
    qDebug() << "initThumbThread end";
}
/**
 * @brief updateSliderPoint 非x86平台下更新音量条控件位置
 * @param point 传入主窗口左上角顶点在屏幕的位置
 */
void ToolboxProxy::updateSliderPoint(QPoint &point)
{
    qDebug() << "updateSliderPoint";
    m_pVolSlider->updatePoint(point);

    // move to the final position
    QRect mainRect = m_pMainWindow->rect();
    QRect viewRect = mainRect.marginsRemoved(QMargins(1, 1, 1, 1));
    QPoint volPoint = point + QPoint(viewRect.width() - (TOOLBOX_BUTTON_WIDTH * 2 + 30 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2),
                             viewRect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT) + QPoint(6, 0);
    m_pVolSlider->move(volPoint);
    qDebug() << "updateSliderPoint end";
}

/**
 * @brief ~ToolboxProxy 析构函数
 */
ToolboxProxy::~ToolboxProxy()
{
    qDebug() << "Destroying ToolboxProxy";
    if(CompositingManager::isMpvExists()) {
        ThumbnailWorker::get().stop();
    }
    delete m_pPreviewer;
    delete m_pPreviewTime;

    if (m_pWorker) {
        m_pWorker->quit();
        m_pWorker->deleteLater();
    }
}

viewProgBarLoad::viewProgBarLoad(PlayerEngine *engine, DMRSlider *progBar, ToolboxProxy *parent)
{
    qDebug() << "viewProgBarLoad";
    initMember();
    m_pParent = parent;
    m_pEngine = engine;
    m_pProgBar = progBar;
    m_seekTime = new char[12];
    initThumb();
}

void viewProgBarLoad::setListPixmapMutex(QMutex *pMutex)
{
    qDebug() << "setListPixmapMutex";
    m_pListPixmapMutex = pMutex;
}

void viewProgBarLoad::run()
{
    qDebug() << "run";
    loadViewProgBar(m_pParent->size());
}

viewProgBarLoad::~viewProgBarLoad()
{
    qDebug() << "~viewProgBarLoad";
    delete [] m_seekTime;
    m_seekTime = nullptr;

    if (m_video_thumbnailer != nullptr) {
        qDebug() << "m_video_thumbnailer != nullptr";
        m_mvideo_thumbnailer_destroy(m_video_thumbnailer);
        m_video_thumbnailer = nullptr;
    }
    qDebug() << "~viewProgBarLoad end";
}

}
#undef THEME_TYPE
#include "toolbox_proxy.moc"
