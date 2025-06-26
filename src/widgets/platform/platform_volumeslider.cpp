// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform_volumeslider.h"
#include "platform_toolbox_proxy.h"
#include "dbusutils.h"

DWIDGET_USE_NAMESPACE

namespace dmr {

Platform_VolumeSlider::Platform_VolumeSlider(Platform_MainWindow *mw, QWidget *parent)
    : QWidget(parent), _mw(mw)
{
    qDebug() << "Entering Platform_VolumeSlider constructor";
    if (CompositingManager::get().platform() != Platform::X86) {
        qDebug() << "CompositingManager::get().platform() != Platform::X86";
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    } else {
        qDebug() << "CompositingManager::get().platform() == Platform::X86";
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    }
    m_iStep = 0;
    m_bIsWheel = false;
    m_nVolume = 100;
    m_bHideWhenFinished = false;

    hide();
    setFocusPolicy(Qt::TabFocus);
    QVBoxLayout *vLayout = new QVBoxLayout(this);
    vLayout->setContentsMargins(2, 16, 2, 14);  //内边距，与UI沟通确定
    vLayout->setSpacing(0);
    setLayout(vLayout);

    QFont font;
    font.setFamily("SourceHanSansSC");
    font.setWeight(QFont::Medium);

    vLayout->addStretch();
    m_pLabShowVolume = new DLabel(_mw);
    DFontSizeManager::instance()->bind(m_pLabShowVolume, DFontSizeManager::T6);
    m_pLabShowVolume->setForegroundRole(QPalette::BrightText);
    m_pLabShowVolume->setFont(font);
    m_pLabShowVolume->setAlignment(Qt::AlignCenter);
    m_pLabShowVolume->setText("0%");
    vLayout->addWidget(m_pLabShowVolume, 0, Qt::AlignCenter);

    m_slider = new DSlider(Qt::Vertical, this);
    m_slider->setFixedWidth(24);
    m_slider->setIconSize(QSize(15, 15));
    m_slider->installEventFilter(this);
    m_slider->show();
    m_slider->slider()->setRange(0, 100);
    m_slider->setValue(m_nVolume);
    m_slider->setObjectName(VOLUME_SLIDER);
    m_slider->slider()->setObjectName(SLIDER);
    m_slider->slider()->setAccessibleName(SLIDER);
    m_slider->slider()->setMinimumHeight(120);
    m_slider->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
    m_slider->slider()->setFocusPolicy(Qt::NoFocus);
    vLayout->addWidget(m_slider, 1, Qt::AlignCenter);

    m_pBtnChangeMute = new DToolButton(this);
    m_pBtnChangeMute->setObjectName(MUTE_BTN);
    m_pBtnChangeMute->setAccessibleName(MUTE_BTN);
    m_pBtnChangeMute->setToolButtonStyle(Qt::ToolButtonIconOnly);
    //同时设置按钮与图标的尺寸，改变其默认比例
    m_pBtnChangeMute->setFixedSize(30, 30);
    m_pBtnChangeMute->setIconSize(QSize(30, 30));
    m_pBtnChangeMute->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
    m_pBtnChangeMute->setFocusPolicy(Qt::NoFocus);
    vLayout->addStretch(10);
    vLayout->addWidget(m_pBtnChangeMute, 0, Qt::AlignHCenter);
    vLayout->addStretch();

    connect(m_slider, &DSlider::valueChanged, this, &Platform_VolumeSlider::volumeChanged);
    connect(m_pBtnChangeMute, SIGNAL(clicked()), this, SLOT(muteButtnClicked()));
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged,
            this, &Platform_VolumeSlider::setThemeType);
    m_autoHideTimer.setSingleShot(true);
    connect(&m_autoHideTimer, &QTimer::timeout, this, &Platform_VolumeSlider::hide);
    qDebug() << "Exiting Platform_VolumeSlider constructor";
}

void Platform_VolumeSlider::initVolume()
{
    qDebug() << "Entering initVolume function";
    QTimer::singleShot(500, this, [ = ] { //延迟加载等待信号槽连接
        int nVolume = Settings::get().internalOption("global_volume").toInt();
        bool bMute = Settings::get().internalOption("mute").toBool();

        changeVolume(nVolume);
        changeMuteState(bMute);

        setMute(bMute);
    });
    qDebug() << "Exiting initVolume function";
}

void Platform_VolumeSlider::stopTimer()
{
    qDebug() << "Entering stopTimer function";
    m_autoHideTimer.stop();
    qDebug() << "Exiting stopTimer function";
}

QString Platform_VolumeSlider::readSinkInputPath()
{
    qDebug() << "Entering readSinkInputPath function";
    QString strPath = "";

    QVariant v = ApplicationAdaptor::redDBusProperty("org.deepin.dde.Audio1", "/org/deepin/dde/Audio1",
                                                     "org.deepin.dde.Audio1", "SinkInputs");

    if (!v.isValid())
    {
        qDebug() << "Invalid D-Bus property value";
        return strPath;
    }

    QList<QDBusObjectPath> allSinkInputsList = v.value<QList<QDBusObjectPath> >();

    for (auto curPath : allSinkInputsList) {
        QVariant nameV = ApplicationAdaptor::redDBusProperty("org.deepin.dde.Audio1", curPath.path(),
                                                             "org.deepin.dde.Audio1.SinkInput", "Name");
        QString strMovie = QObject::tr("Movie");
        if (!nameV.isValid() || (!nameV.toString().contains(strMovie, Qt::CaseInsensitive) && !nameV.toString().contains("deepin movie", Qt::CaseInsensitive))) {
            qDebug() << "Skipping invalid or non-movie sink input:" << nameV.toString();
            continue;
        }

        strPath = curPath.path();
        break;
    }
    qDebug() << "Exiting readSinkInputPath function, result:" << strPath;
    return strPath;
}

void Platform_VolumeSlider::setMute(bool muted)
{
    qDebug() << "Setting mute state to:" << muted;
    QString sinkInputPath = readSinkInputPath();

    if (!sinkInputPath.isEmpty()) {
        QDBusInterface ainterface("org.deepin.dde.Audio1", sinkInputPath,
                                  "org.deepin.dde.Audio1.SinkInput",
                                  QDBusConnection::sessionBus());
        if (!ainterface.isValid()) {
            qDebug() << "Invalid D-Bus interface";
            return;
        }

        //调用设置音量
        ainterface.call(QLatin1String("SetMute"), muted);
    } else {
        qDebug() << "No sink input path found";
    }

    return;
}

void Platform_VolumeSlider::updatePoint(QPoint point)
{
    qDebug() << "Updating slider position to:" << point;
    QRect main_rect = _mw->rect();
    QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));
    m_point = point + QPoint(view_rect.width() - (TOOLBOX_BUTTON_WIDTH * 3 + 40 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2),
                             view_rect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT);
}
void Platform_VolumeSlider::popup()
{
    qDebug() << "Entering popup function";
    QRect main_rect = _mw->rect();
    QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));

    int x = view_rect.width() - (TOOLBOX_BUTTON_WIDTH * 3 + 40 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2);
    int y = view_rect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT;
    y += 10;
    QRect end(x, y, VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT);
    QRect start = end;

    start.setWidth(start.width() + 12);
    start.setHeight(start.height() + 10);
    end.moveTo(m_point + QPoint(6, 0));
    start.moveTo(m_point - QPoint(0, 14));

    //动画未完成，等待动画结束后再隐藏控件
    if (pVolAnimation) {
        qDebug() << "Animation is already running, setting hide when finished";
        m_bHideWhenFinished = true;
        return;
    }

    if (m_state == State::Close && isVisible()) {
        qDebug() << "Creating new animation";
        pVolAnimation = new QPropertyAnimation(this, "geometry");
        pVolAnimation->setEasingCurve(QEasingCurve::Linear);
        pVolAnimation->setKeyValueAt(0, end);
        pVolAnimation->setKeyValueAt(0.3, start);
        pVolAnimation->setKeyValueAt(1, end);
        pVolAnimation->setDuration(230);
        m_bFinished = true;
        raise();
        pVolAnimation->start();
        connect(pVolAnimation, &QPropertyAnimation::finished, [ = ] {
            qDebug() << "Animation finished";
            pVolAnimation->deleteLater();
            pVolAnimation = nullptr;
            m_state = Open;
            m_bFinished = false;
            if (m_bHideWhenFinished) {
                qDebug() << "Hiding when finished, calling popup again";
                popup();
                m_bHideWhenFinished = false;
            }
        });
    } else {
        qDebug() << "Setting state to Close and hiding";
        m_state = Close;
        hide();
    }
    qDebug() << "Exiting popup function";
}
void Platform_VolumeSlider::delayedHide()
{   
    qDebug() << "Entering delayedHide function";
    const int nGap = 18;   // 音量条和音量按钮之间的间距
    QRect adRect = QRect(m_point + QPoint(6, VOLSLIDER_HEIGHT), m_point + QPoint(6 + VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT + nGap));

    m_mouseIn = false;
    if(adRect.contains(QCursor::pos())){
        DUtil::TimerSingleShot(2000, [=]() {
            if (!m_mouseIn)
                hide();
        });
    } else {
        DUtil::TimerSingleShot(100, [=]() {
            if (!m_mouseIn)
                hide();
        });
    }
    qDebug() << "Exiting delayedHide function";
}
void Platform_VolumeSlider::changeVolume(int nVolume)
{   
    qDebug() << "Entering changeVolume function";
    qDebug() << "Changing volume to:" << nVolume;
    if (nVolume < 0) {
        qDebug() << "Volume is less than 0, setting to 0";
        nVolume = 0;
    } else if (nVolume > 200) {
        qDebug() << "Volume is greater than 200, setting to 200";
        nVolume = 200;
    }
    m_nVolume = nVolume;
    m_slider->setValue(nVolume > 100 ? 100 : nVolume);  //音量实际能改变200,但是音量条最大支持到100

    //1.保证初始化音量(100)和配置文件中音量一致时，也能得到刷新
    //2.m_slider的最大值为100,如果大于100必须主动调用
    if (nVolume >= 100) {
        qDebug() << "Volume is greater than or equal to 100, calling volumeChanged";
        volumeChanged(nVolume);
    }
    qDebug() << "Exiting changeVolume function";
}

void Platform_VolumeSlider::calculationStep(int iAngleDelta){
    qDebug() << "Calculating volume step with angle delta:" << iAngleDelta;
    m_bIsWheel = true;

    if ((m_iStep > 0 && iAngleDelta > 0) || (m_iStep < 0 && iAngleDelta < 0)) {
        qDebug() << "Adding to step";
        m_iStep += iAngleDelta;
    } else {
        qDebug() << "Setting step to:" << iAngleDelta;
        m_iStep = iAngleDelta;
    }
    qDebug() << "Exiting calculationStep function";
}

void Platform_VolumeSlider::volumeUp()
{
    qDebug() << "Entering volumeUp function";
    if (m_bIsWheel) {
        qDebug() << "m_bIsWheel is true";
        m_bIsWheel = false;
        if(qAbs(m_iStep) >= 120) {
            qDebug() << "Step is greater than or equal to 120, changing volume";
            m_nVolume += qAbs(m_iStep) / 120 * 10;
            changeVolume(qMin(m_nVolume, 200));
            m_iStep = 0;
        }
    } else {
        qDebug() << "m_bIsWheel is false, changing volume";
        changeVolume(qMin(m_nVolume + 10, 200));
    }
    qDebug() << "Exiting volumeUp function";
}

void Platform_VolumeSlider::volumeDown()
{
    qDebug() << "Entering volumeDown function";
    if(m_bIsWheel){
        qDebug() << "m_bIsWheel is true";
        m_bIsWheel = false;
        if(qAbs(m_iStep) >= 120){
            qDebug() << "Step is greater than or equal to 120, changing volume";
            m_nVolume -= qAbs(m_iStep) / 120 * 10 ;
            changeVolume(qMax(m_nVolume, 0));
            m_iStep = 0;
        }
    }else{
        qDebug() << "m_bIsWheel is false, changing volume";
        changeVolume(qMax(m_nVolume - 10, 0));
    }
    qDebug() << "Exiting volumeDown function";
}

void Platform_VolumeSlider::changeMuteState(bool bMute)
{
    qDebug() << "Entering changeMuteState function";
    qDebug() << "Changing mute state to:" << bMute;
    if (m_bIsMute == bMute || m_nVolume == 0) {
        qDebug() << "Mute state is the same or volume is 0, returning";
        return;
    }

    m_bIsMute = bMute;
    refreshIcon();
    Settings::get().setInternalOption("mute", m_bIsMute);
    emit sigMuteStateChanged(bMute);
    qDebug() << "Exiting changeMuteState function";
}

void Platform_VolumeSlider::volumeChanged(int nVolume)
{
    qDebug() << "Entering volumeChanged function";
    qDebug() << "Volume changed to:" << nVolume;
    if (m_nVolume != nVolume) {
        qDebug() << "Volume is different, updating volume";
        m_nVolume = nVolume;
    }

    if (m_nVolume > 0 && m_bIsMute) {      //音量改变时改变静音状态
        qDebug() << "Volume is greater than 0 and mute is true, changing mute state to false";
        changeMuteState(false);
        setMute(false);
    }

    refreshIcon();

    emit sigVolumeChanged(nVolume);
    qDebug() << "Exiting volumeChanged function";
}

void Platform_VolumeSlider::refreshIcon()
{
    qDebug() << "Entering refreshIcon function";
    int nValue = m_slider->value();
    qDebug() << "nValue:" << nValue;

    if (nValue >= 66) {
        qDebug() << "nValue is greater than or equal to 66, setting icon to dcc_volume";
        m_pBtnChangeMute->setIcon(QIcon::fromTheme("dcc_volume"));
    }
    else if (nValue >= 33) {
        qDebug() << "nValue is greater than or equal to 33, setting icon to dcc_volumemid";
        m_pBtnChangeMute->setIcon(QIcon::fromTheme("dcc_volumemid"));
    }
    else {
        qDebug() << "nValue is less than 33, setting icon to dcc_volumelow";
        m_pBtnChangeMute->setIcon(QIcon::fromTheme("dcc_volumelow"));
    }

    if (m_bIsMute || nValue == 0) {
        qDebug() << "Mute is true or nValue is 0, setting icon to dcc_mute";
        m_pBtnChangeMute->setIcon(QIcon::fromTheme("dcc_mute"));
    }

    m_pLabShowVolume->setText(QString("%1%").arg(nValue * 1.0 / m_slider->maximum() * 100));
    qDebug() << "Exiting refreshIcon function";
}

void Platform_VolumeSlider::muteButtnClicked()
{
    qDebug() << "Entering muteButtnClicked function";
    bool bMute = m_bIsMute;
    qDebug() << "bMute:" << bMute;
    changeMuteState(!bMute);
    setMute(!bMute);
    qDebug() << "Exiting muteButtnClicked function";
}

bool Platform_VolumeSlider::getsliderstate()
{
    qDebug() << "Entering getsliderstate function";
    qDebug() << "m_bFinished:" << m_bFinished;
    return m_bFinished;
}

int Platform_VolumeSlider::getVolume()
{
    qDebug() << "Entering getVolume function";
    qDebug() << "m_nVolume:" << m_nVolume;
    return m_nVolume;
}

void Platform_VolumeSlider::setThemeType(int type)
{
    qDebug() << "Entering setThemeType function";
    qDebug() << "type:" << type;
    Q_UNUSED(type)
    qDebug() << "Exiting setThemeType function";
}

void Platform_VolumeSlider::enterEvent(QEnterEvent *e)
{
    qDebug() << "Entering enterEvent function";
    m_mouseIn = true;
    QWidget::enterEvent(e);
}
void Platform_VolumeSlider::showEvent(QShowEvent *se)
{
    qDebug() << "Entering showEvent function";
    QRect main_rect = _mw->rect();
    QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));

    int x = view_rect.width() - (TOOLBOX_BUTTON_WIDTH * 3 + 40 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2);
    int y = view_rect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT;
#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        qDebug() << "DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode";
        x += 50;
        y += 30;
    }
#endif
    QPoint p = _mw->mapToGlobal(QPoint(0, 0));
    QRect end(x + p.x(), y + p.y(), VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT);
    setGeometry(end);

    QWidget::showEvent(se);
}
void Platform_VolumeSlider::leaveEvent(QEvent *e)
{
    qDebug() << "Entering leaveEvent function";
    m_mouseIn = false;
    delayedHide();
    QWidget::leaveEvent(e);
}
void Platform_VolumeSlider::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QColor bgColor = this->palette().background().color();
#else
    QColor bgColor = this->palette().window().color();
#endif
    painter.fillRect(rect(), bgColor);
}

void Platform_VolumeSlider::keyPressEvent(QKeyEvent *pEvent)
{
    qDebug() << "Entering keyPressEvent function";
    int nCurVolume = getVolume();
    if (pEvent->key() == Qt::Key_Up) {
        qDebug() << "Key_Up";
        changeVolume(qMin(nCurVolume + 5, 200));
    } else if (pEvent->key() == Qt::Key_Down) {
        qDebug() << "Key_Down";
        changeVolume(qMax(nCurVolume - 5, 0));
    }
    QWidget::keyPressEvent(pEvent);
}

bool Platform_VolumeSlider::eventFilter(QObject *obj, QEvent *e)
{
    qDebug() << "Entering eventFilter function";
    if (e->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent *>(e);
        qDebug() << "Wheel event - Angle delta:" << we->angleDelta() << "Modifiers:" << we->modifiers() << "Buttons:" << we->buttons();
        
        if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
            calculationStep(we->angleDelta().y());
            if (we->angleDelta().y() > 0) {
                qDebug() << "Angle delta is greater than 0, calling volumeUp";
                volumeUp();
            } else {
                qDebug() << "Angle delta is less than 0, calling volumeDown";
                volumeDown();
            }
        }
        return false;
    } else {
        qDebug() << "Event is not a wheel event, calling QObject::eventFilter";
        return QObject::eventFilter(obj, e);
    }
}

Platform_VolumeSlider::~Platform_VolumeSlider()
{
    qInfo() << "Destroying Platform_VolumeSlider";
}

}
