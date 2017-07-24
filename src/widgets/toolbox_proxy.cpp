#include "config.h"

#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"
#include "player_engine.h"
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
    VolumeSlider(PlayerEngine* eng): QWidget(nullptr, Qt::Popup), _engine(eng) {
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

        _slider->setValue(_engine->volume());
        l->addWidget(_slider);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::onThemeChanged);
        onThemeChanged();

        connect(_slider, &QSlider::valueChanged, [=]() { _engine->changeVolume(_slider->value()); });
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
    PlayerEngine *_engine;
    QSlider *_slider;
};

ToolboxProxy::ToolboxProxy(QWidget *mainWindow, PlayerEngine *proxy)
    :QFrame(mainWindow),
    _mainWindow(static_cast<MainWindow*>(mainWindow)),
    _engine(proxy)
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
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    setLayout(l);

    _progBar = new QProgressBar();
    _progBar->setTextVisible(false);
    _progBar->setFixedHeight(2);
    _progBar->setRange(0, 100);
    _progBar->setValue(0);
    l->addWidget(_progBar, 0);

    auto *bot = new QHBoxLayout();
    bot->setContentsMargins(10, 0, 10, 0);
    l->addLayout(bot, 1);

    _timeLabel = new QLabel("");
    bot->addWidget(_timeLabel);

    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

    bot->addStretch();

    auto *mid = new QHBoxLayout();
    bot->addLayout(mid);
    
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

    bot->addStretch();

    auto *right = new QHBoxLayout();
    bot->addLayout(right);

    _volBtn = new VolumeButton();
    connect(_volBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_volBtn, "vol");
    right->addWidget(_volBtn);

    _fsBtn = new DImageButton();
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");
    right->addWidget(_fsBtn);

#ifndef ENABLE_VPU_PLATFORM
    _listBtn = new DImageButton();
    _listBtn->setObjectName("ListBtn");
    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    right->addWidget(_listBtn);
#endif

    connect(_engine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(_engine, &PlayerEngine::elapsedChanged, [=]() {
        updateTimeInfo(_engine->duration(), _engine->elapsed());
    });
    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &ToolboxProxy::updateFullState);
    connect(_engine, &PlayerEngine::muteChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_engine, &PlayerEngine::volumeChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_engine, &PlayerEngine::elapsedChanged, this, &ToolboxProxy::updateMovieProgress);
    connect(&_engine->playlist(), &PlaylistModel::countChanged, this, &ToolboxProxy::updateButtonStates);

    updatePlayState();
    updateFullState();
    updateButtonStates();

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);
}

void ToolboxProxy::updateMovieProgress()
{
    auto d = _engine->duration();
    auto e = _engine->elapsed();
    int v = 100 * ((double)e / d);
    _progBar->setValue(v);
}

void ToolboxProxy::updateButtonStates()
{
    bool vis = _engine->playlist().count() > 1;
    _prevBtn->setVisible(vis);
    _nextBtn->setVisible(vis);
}

void ToolboxProxy::updateVolumeState()
{
    if (_engine->muted()) {
        _volBtn->changeLevel(VolumeButton::Mute);
    } else {
        auto v = _engine->volume();
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
    qDebug() << __func__ << _engine->state();
    if (_engine->state() == PlayerEngine::CoreState::Playing) {
        _playBtn->setObjectName("PauseBtn");
    } else {
        _playBtn->setObjectName("PlayBtn");
    }
    _playBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    auto fn = [](qint64 d) -> QString {
        auto secs = d % 60;
        auto minutes = d / 60;
        return QString("%1:%2").arg(minutes).arg(secs);
    };

    _timeLabel->setText(QString("%2/%1").arg(fn(duration)).arg(fn(pos)));
}

void ToolboxProxy::buttonClicked(QString id)
{
    if (!isVisible()) return;

    qDebug() << __func__ << id;
    if (id == "play") {
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            _mainWindow->requestAction(ActionKind::OpenFile);
        } else {
            _mainWindow->requestAction(ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        _mainWindow->requestAction(ActionKind::Fullscreen);
    } else if (id == "vol") {
        auto *w = new VolumeSlider(_engine);
        connect(w, &QObject::destroyed, [=]() {
                qDebug() << "slider destroyed";
        });
        QPoint pos = _volBtn->parentWidget()->mapToGlobal(_volBtn->pos());

        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y() - w->height();
        w->move(pos);
        w->show();

    } else if (id == "prev") {
        _mainWindow->requestAction(ActionKind::GotoPlaylistPrev);
    } else if (id == "next") {
        _mainWindow->requestAction(ActionKind::GotoPlaylistNext);
    } else if (id == "list") {
        _mainWindow->requestAction(ActionKind::TogglePlaylist);
    }
}

void ToolboxProxy::updatePosition(const QPoint& p)
{
    QPoint pos(p);
    pos.rx() += _mainWindow->frameMargins().left();
    pos.ry() += _mainWindow->frameMargins().top() + _mainWindow->height() - height();
    windowHandle()->setFramePosition(pos);
}

void ToolboxProxy::paintEvent(QPaintEvent *pe)
{
    QWidget::paintEvent(pe);
}

}


#include "toolbox_proxy.moc"
