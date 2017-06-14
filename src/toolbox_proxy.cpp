#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"
#include "mpv_proxy.h"
#include "toolbutton.h"
#include "actions.h"

#include <QtWidgets>
#include <dimagebutton.h>
#include <dthememanager.h>
#include <DApplication>

DWIDGET_USE_NAMESPACE

namespace dmr {
class KeyPressBubbler: public QObject {
public:
    KeyPressBubbler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            event->setAccepted(false);
            return false;
        } else {
            // standard event processing
            return QObject::eventFilter(obj, event);
        }
    }
};

class VolumeSlider: public QWidget {
    Q_OBJECT
public:
    VolumeSlider(MpvProxy* mpv): QWidget(nullptr, Qt::Popup), _mpv(mpv) {
        setFixedSize(QSize(24, 105));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowOpacity(0.92);

        //auto img = QImage(":/resources/icons/volume-slider-shape.png");
        ////img = img.convertToFormat(QImage::Format_Alpha8);
        //img = img.createHeuristicMask();
        //setMask(QPixmap::fromImage(img).mask());

        //QRegion maskedRegion(1, 1, 22, 103, QRegion::Ellipse);
        //setMask(maskedRegion);
        
        auto *l = new QVBoxLayout;
        l->setContentsMargins(0, 0, 0, 0);
        setLayout(l);

        _slider = new QSlider(this);
        _slider->show();
        _slider->setRange(0, 100);
        _slider->setOrientation(Qt::Vertical);

        _slider->setValue(_mpv->volume());
        l->addWidget(_slider);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::onThemeChanged);
        onThemeChanged();

        connect(_slider, &QSlider::valueChanged, [=]() { _mpv->changeVolume(_slider->value()); });
    }

    ~VolumeSlider() {
        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::onThemeChanged);
    }
        
public slots:
    void onThemeChanged() {
        QFile darkF(":/resources/qss/dark/widgets.qss"),
              lightF(":/resources/qss/light/widgets.qss");

        if ("dark" == qApp->theme()) {
            if (darkF.open(QIODevice::ReadOnly)) {
                setStyleSheet(darkF.readAll());
                darkF.close();
            }
        } else {
            if (lightF.open(QIODevice::ReadOnly)) {
                setStyleSheet(lightF.readAll());
                lightF.close();
            }
        }
    }

private:
    MpvProxy *_mpv;
    QSlider *_slider;
};

ToolboxProxy::ToolboxProxy(QWidget *mainWindow, MpvProxy *proxy)
    :QFrame(mainWindow),
    _mainWindow(mainWindow),
    _mpv(proxy)
{
    bool composited = CompositingManager::get().composited();
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

    setup();
}

ToolboxProxy::~ToolboxProxy()
{
}

void ToolboxProxy::setup()
{
    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(10, 0, 10, 0);
    setLayout(l);

    _timeLabel = new QLabel("");
    l->addWidget(_timeLabel);

    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

    l->addStretch();

    auto *mid = new QHBoxLayout();
    l->addLayout(mid);
    
    _prevBtn = new DImageButton();
    _prevBtn->setObjectName("PrevBtn");
    connect(_prevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_prevBtn, "prev");
    mid->addWidget(_prevBtn);

    _playBtn = new DImageButton();
    connect(_playBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_playBtn, "play");
    mid->addWidget(_playBtn);

    _nextBtn = new DImageButton();
    _nextBtn->setObjectName("NextBtn");
    connect(_nextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_nextBtn, "next");
    mid->addWidget(_nextBtn);

    l->addStretch();

    auto *right = new QHBoxLayout();
    l->addLayout(right);

    _volBtn = new VolumeButton();
    connect(_volBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_volBtn, "vol");
    right->addWidget(_volBtn);

    _fsBtn = new DImageButton();
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");
    right->addWidget(_fsBtn);

    _listBtn = new DImageButton();
    _listBtn->setObjectName("ListBtn");
    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    right->addWidget(_listBtn);


    connect(_mpv, &MpvProxy::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(_mpv, &MpvProxy::ellapsedChanged, [=]() {
        updateTimeInfo(_mpv->duration(), _mpv->ellapsed());
    });
    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, 
            &ToolboxProxy::updateFullState);
    connect(_mpv, &MpvProxy::muteChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_mpv, &MpvProxy::volumeChanged, this, &ToolboxProxy::updateVolumeState);

    updatePlayState();
    updateFullState();

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);
}

void ToolboxProxy::updateVolumeState()
{
    if (_mpv->muted()) {
        _volBtn->changeLevel(VolumeButton::Mute);
    } else {
        auto v = _mpv->volume();
        qDebug() << __func__ << v;
        if (v >= 80)
            _volBtn->changeLevel(VolumeButton::High);
        else if (v >= 40)
            _volBtn->changeLevel(VolumeButton::Mid);
        else if (v == 0)
            _volBtn->changeLevel(VolumeButton::Off);
        else 
            _volBtn->changeLevel(VolumeButton::Low);
    }
}

void ToolboxProxy::updateFullState()
{
    bool isFullscreen = window()->isFullScreen();
    if (isFullscreen) {
        _fsBtn->setObjectName("UnfsBtn");
    } else {
        _fsBtn->setObjectName("FsBtn");
    }
    _fsBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updatePlayState()
{
    qDebug() << __func__ << _mpv->state();
    if (_mpv->state() == MpvProxy::CoreState::Playing) {
        _playBtn->setObjectName("PauseBtn");
    } else {
        _playBtn->setObjectName("PlayBtn");
    }
    _playBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    QTime d(0, 0), t(0, 0);
    d = d.addSecs(duration);
    t = t.addSecs(pos);
    _timeLabel->setText(QString("%2/%1").arg(d.toString("hh:mm:ss")).arg(t.toString("hh:mm:ss")));
}

void ToolboxProxy::buttonClicked(QString id)
{
    qDebug() << __func__ << id;
    auto mw = static_cast<MainWindow*>(_mainWindow);
    if (id == "play") {
        if (_mpv->state() == MpvProxy::CoreState::Idle) {
            mw->requestAction(ActionKind::OpenFile);
        } else {
            mw->requestAction(ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        mw->requestAction(ActionKind::Fullscreen);
    } else if (id == "vol") {
        auto *w = new VolumeSlider(_mpv);
        connect(w, &QObject::destroyed, [=]() {
                qDebug() << "slider destroyed";
        });
        QPoint pos = _volBtn->parentWidget()->mapToGlobal(_volBtn->pos());
        pos.ry() -= w->height();
        w->move(pos);
        w->show();

    } else if (id == "prev") {
    } else if (id == "next") {
    } else if (id == "list") {
    }
}

void ToolboxProxy::updatePosition(const QPoint& p)
{
    QPoint pos(p);
    auto *mw = static_cast<MainWindow*>(_mainWindow);
    pos.rx() += mw->frameMargins().left();
    pos.ry() += mw->frameMargins().top() + mw->height() - height();
    windowHandle()->setFramePosition(pos);
}

void ToolboxProxy::paintEvent(QPaintEvent *pe)
{
    QWidget::paintEvent(pe);
}

}


#include "toolbox_proxy.moc"
