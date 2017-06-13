#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"
#include "mpv_proxy.h"
#include "toolbutton.h"
#include "actions.h"

#include <QtWidgets>
#include <dimagebutton.h>

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


