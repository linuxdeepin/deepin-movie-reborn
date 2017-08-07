#include "config.h"

#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbutton.h"
#include "dmr_settings.h"
#include "actions.h"
#include "slider.h"
#include "thumbnail_worker.h"

#include <QtWidgets>
#include <dimagebutton.h>
#include <dthememanager.h>
#include <darrowrectangle.h>
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

class SubtitlesView;
class SubtitleItemWidget: public QWidget {
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *parent, SubtitleInfo si): QWidget(parent) {
        _sid = si["id"].toInt();

        auto *l = new QHBoxLayout(this);
        setLayout(l);
        l->setContentsMargins(0, 0, 0, 0);

        l->addWidget(new QLabel(si["title"].toString()), 1);

        _selectedLabel = new QLabel(this);
        l->addWidget(_selectedLabel);
    }

    int sid() const { return _sid; }

    void setCurrent(bool v)
    {
        if (v) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.png").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        } else 
            _selectedLabel->clear();

        setProperty("current", v?"true":"false");
        setStyleSheet(this->styleSheet());
    }

private:
    QLabel *_selectedLabel {nullptr};
    int _sid {-1};
};

class SubtitlesView: public DArrowRectangle {
    Q_OBJECT
public:
    SubtitlesView(QWidget *p, PlayerEngine* e)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, p), _engine{e} {
        //setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Popup);

        setMinimumHeight(20);

        setShadowBlurRadius(4);
        setRadius(4);
        setShadowDistance(0);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(5);

        QSizePolicy sz_policy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        setSizePolicy(sz_policy);

        setFixedWidth(220);

        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(8, 2, 8, 2);
        setLayout(l);

        _subsView = new QListWidget(this);
        _subsView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        _subsView->setSelectionMode(QListWidget::SingleSelection);
        _subsView->setSelectionBehavior(QListWidget::SelectRows);
        l->addWidget(_subsView, 1);

        connect(_subsView, &QListWidget::itemClicked, this, &SubtitlesView::onItemClicked);
        connect(_engine, &PlayerEngine::tracksChanged, this, &SubtitlesView::populateSubtitles);
        connect(_engine, &PlayerEngine::sidChanged, this, &SubtitlesView::onSidChanged);
        populateSubtitles();

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &SubtitlesView::onThemeChanged);
        onThemeChanged();
    }

protected slots:
    void onThemeChanged() 
    {
        QFile darkF(":/resources/qss/dark/subtitlesview.qss"),
              lightF(":/resources/qss/light/subtitlesview.qss");

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

    void populateSubtitles()
    {
        _subsView->clear();
        auto pmf = _engine->playingMovieInfo();
        auto sid = _engine->sid();
        qDebug() << "sid" << sid;

        for (const auto& sub: pmf.subs) {
            auto item = new QListWidgetItem();
            auto siw = new SubtitleItemWidget(this, sub);
            _subsView->addItem(item);
            item->setSizeHint(siw->sizeHint());
            _subsView->setItemWidget(item, siw);
            auto v = (sid == sub["id"].toInt());
            siw->setCurrent(v);
            if (v) {
                _subsView->setCurrentItem(item);
            }
        }

        this->setMaximumHeight(28 * pmf.subs.size());
    }

    void onSidChanged()
    {
        auto sid = _engine->sid();
        for (int i = 0; i < _subsView->count(); ++i) {
            auto siw = static_cast<SubtitleItemWidget*>(_subsView->itemWidget(_subsView->item(i)));
            siw->setCurrent(siw->sid() == sid);
        }
    }

    void onItemClicked(QListWidgetItem* item)
    {
        auto id = _subsView->row(item);
        _engine->selectSubtitle(id);
    }

private:
    PlayerEngine *_engine {nullptr};
    QListWidget *_subsView {nullptr};
};

class ThumbnailPreview: public DArrowRectangle {
    Q_OBJECT
public:
    ThumbnailPreview(): DArrowRectangle(DArrowRectangle::ArrowBottom) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::ToolTip);
        
        setShadowBlurRadius(4);
        setRadius(4);
        setShadowDistance(0);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(5);
        
        auto *l = new QVBoxLayout;
        l->setContentsMargins(2, 2, 2, 2);
        setLayout(l);

        _thumb = new QLabel(this);
        _thumb->setFixedWidth(160);
        l->addWidget(_thumb);
    }

    void updateWithPreview(const QPixmap& pm) {
        _thumb->setPixmap(pm);
    }

    void updateWithPreview(const QPoint& pos) {
        show(pos.x(), pos.y() - 5);
    }

private:
    QLabel *_thumb;
};

class VolumeSlider: public DArrowRectangle {
    Q_OBJECT
public:
    VolumeSlider(PlayerEngine* eng): DArrowRectangle(DArrowRectangle::ArrowBottom), _engine(eng) {
        setFixedSize(QSize(24, 105));
        setAttribute(Qt::WA_DeleteOnClose);
        //setWindowOpacity(0.92);
        setWindowFlags(Qt::Popup);

        setShadowBlurRadius(4);
        //setBorderWidth(1);
        //setBorderColor(qRgba(255, 255, 255, 26));
        setRadius(4);
        setShadowDistance(0);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(5);
        
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

    _previewer = new ThumbnailPreview;
    _previewer->hide();

    _subView = new SubtitlesView(0, _engine);
    _subView->hide();
    setup();
}

ToolboxProxy::~ToolboxProxy()
{
    ThumbnailWorker::get().stop();
    delete _subView;
    delete _previewer;
}

void ToolboxProxy::setup()
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    setLayout(l);

    _progBar = new DMRSlider();
    _progBar->setObjectName("MovieProgress");
    _progBar->setOrientation(Qt::Horizontal);
    _progBar->setFixedHeight(8);
    _progBar->setRange(0, 100);
    _progBar->setValue(0);
    connect(_progBar, &QSlider::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::leave, [=]() { _previewer->hide(); });
    l->addWidget(_progBar, 0);

    auto *bot = new QHBoxLayout();
    bot->setContentsMargins(10, 0, 10, 8);
    l->addLayout(bot, 1);

    _timeLabel = new QLabel("");
    _timeLabel->setFixedWidth(_timeLabel->fontMetrics().width("239:59/240:00"));
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

    _subBtn = new DImageButton();
    _subBtn->setObjectName("SubtitleBtn");
    connect(_subBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_subBtn, "sub");
    right->addWidget(_subBtn);

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
    connect(_engine, &PlayerEngine::fileLoaded, [=]() {
        _progBar->setRange(0, _engine->duration());
    });
    connect(_engine, &PlayerEngine::elapsedChanged, [=]() {
        updateTimeInfo(_engine->duration(), _engine->elapsed());
        updateMovieProgress();
    });
    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &ToolboxProxy::updateFullState);
    connect(_engine, &PlayerEngine::muteChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_engine, &PlayerEngine::volumeChanged, this, &ToolboxProxy::updateVolumeState);

    connect(_engine, &PlayerEngine::tracksChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_engine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::updateButtonStates);
    connect(&_engine->playlist(), &PlaylistModel::countChanged, this, &ToolboxProxy::updateButtonStates);

    updatePlayState();
    updateFullState();
    updateButtonStates();

    connect(&ThumbnailWorker::get(), &ThumbnailWorker::thumbGenerated,
            this, &ToolboxProxy::updateHoverPreview);

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);
}

bool ToolboxProxy::anyPopupShown() const
{
    return _previewer->isVisible() || _subView->isVisible();
}

void ToolboxProxy::updateHoverPreview(const QUrl& url, int secs)
{
    if (_engine->playlist().currentInfo().url != url)
        return;

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);

    _previewer->updateWithPreview(pm);
}

void ToolboxProxy::progressHoverChanged(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    if (!Settings::get().isSet(Settings::PreviewOnMouseover))
        return;

    qDebug() << v;
    _lastHoverValue = v;
    ThumbnailWorker::get().requestThumb(_engine->playlist().currentInfo().url, v);

    auto geom = _progBar->frameGeometry();
    double pert = (double) _lastHoverValue / (_progBar->maximum() - _progBar->minimum());

    auto pos = _progBar->mapToGlobal(QPoint(0, 0));
    QPoint p = {
        (int)(pos.x() + geom.width() * pert), pos.y()
    };
    _previewer->updateWithPreview(p);
}

void ToolboxProxy::setProgress()
{
    qDebug() << _progBar->sliderPosition() << _progBar->value() << _progBar->maximum();
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    _engine->seekAbsolute(_progBar->sliderPosition());
    
}

void ToolboxProxy::updateMovieProgress()
{
    if (_progBar->signalsBlocked())
        return;

    auto d = _engine->duration();
    auto e = _engine->elapsed();
    int v = 0;
    if (d != 0 && e != 0) {
        v = _progBar->maximum() * ((double)e / d);
    }
    _progBar->setValue(v);
}

void ToolboxProxy::updateButtonStates()
{
    qDebug() << _engine->playingMovieInfo().subs.size();
    bool vis = _engine->playlist().count() > 1;
    _prevBtn->setVisible(vis);
    _nextBtn->setVisible(vis);

    vis = _engine->state() != PlayerEngine::CoreState::Idle;
    if (vis) {
        vis = _engine->playingMovieInfo().subs.size() > 0;
    }
    _subBtn->setVisible(vis);
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
            _mainWindow->requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            _mainWindow->requestAction(ActionFactory::ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        _mainWindow->requestAction(ActionFactory::ActionKind::Fullscreen);
    } else if (id == "vol") {
        auto *w = new VolumeSlider(_engine);
        QPoint pos = _volBtn->parentWidget()->mapToGlobal(_volBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        w->show(pos.x() + w->width()/2, pos.y() - 5);

    } else if (id == "prev") {
        _mainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistPrev);
    } else if (id == "next") {
        _mainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);
    } else if (id == "list") {
        _mainWindow->requestAction(ActionFactory::ActionKind::TogglePlaylist);
    } else if (id == "sub") {
        _subView->setVisible(true);
        
        QPoint pos = _subBtn->parentWidget()->mapToGlobal(_subBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        _subView->show(pos.x() + _subBtn->width()/2, pos.y() - 5);
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
