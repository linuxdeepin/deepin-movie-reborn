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
    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, 
            &ToolboxProxy::updateFullState);
    updatePlayState();
    updateFullState();

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);
}

void ToolboxProxy::updateFullState()
{
    bool isFullscreen = window()->windowHandle()->windowState() == Qt::WindowFullScreen;
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
    if (id == "play") {
        if (_mpv->state() == MpvProxy::CoreState::Idle) {
            static_cast<MainWindow*>(_mainWindow)->requestAction(ActionKind::OpenFile);
        } else {
            static_cast<MainWindow*>(_mainWindow)->requestAction(ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        bool isFullscreen = window()->windowHandle()->windowState() == Qt::WindowFullScreen;
        if (isFullscreen) {
            //FIXME: restore to orignal state
            window()->windowHandle()->setWindowState(Qt::WindowNoState);
        } else {
            window()->windowHandle()->setWindowState(Qt::WindowFullScreen);
        }
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
    //QPainter p(this);
    //QPainterPath pp;
    //pp.addRoundedRect(rect(), 5, 5);
    //p.setClipPath(pp);

    //auto clr = QColor::fromRgb(0, 0, 0, 128);
    //p.fillRect(rect(), clr);

    QWidget::paintEvent(pe);
}

}


