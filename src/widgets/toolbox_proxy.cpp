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

static const int LEFT_MARGIN = 15;
static const int RIGHT_MARGIN = 10;

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
    Q_OBJECT
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *parent, SubtitleInfo si): QWidget(parent) {
        _sid = si["id"].toInt();

        DThemeManager::instance()->registerWidget(this, QStringList() << "current");
        
        setFixedWidth(200);

        auto *l = new QHBoxLayout(this);
        setLayout(l);
        l->setContentsMargins(0, 0, 0, 0);

        auto msg = si["title"].toString();
        fontMetrics().elidedText(msg, Qt::ElideMiddle, 160*2);
        _title = new QLabel(msg);
        _title->setWordWrap(true);
        l->addWidget(_title, 1);

        _selectedLabel = new QLabel(this);
        l->addWidget(_selectedLabel);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &SubtitleItemWidget::onThemeChanged);
        onThemeChanged();
    }

    int sid() const { return _sid; }

    void setCurrent(bool v)
    {
        if (v) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.png").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        } else {
            _selectedLabel->clear();
        }

        setProperty("current", v?"true":"false");
        setStyleSheet(this->styleSheet());
        style()->unpolish(_title);
        style()->polish(_title);
    }

private slots:
    void onThemeChanged() {
        if (property("current").toBool()) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.png").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        }
    }

private:
    QLabel *_selectedLabel {nullptr};
    QLabel *_title {nullptr};
    int _sid {-1};
};

class SubtitlesView: public DArrowRectangle {
    Q_OBJECT
public:
    SubtitlesView(QWidget *p, PlayerEngine* e)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, p), _engine{e} {
        setWindowFlags(Qt::Popup);

        DThemeManager::instance()->registerWidget(this);

        setMinimumHeight(20);
        setShadowBlurRadius(4);
        setRadius(4);
        setShadowDistance(0);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(6);

        QSizePolicy sz_policy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        setSizePolicy(sz_policy);

        setFixedWidth(220);

        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(8, 2, 8, 2);
        setLayout(l);

        _subsView = new QListWidget(this);
        _subsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        _subsView->setSelectionMode(QListWidget::SingleSelection);
        _subsView->setSelectionBehavior(QListWidget::SelectItems);
        l->addWidget(_subsView, 1);

        connect(_subsView, &QListWidget::itemClicked, this, &SubtitlesView::onItemClicked);
        connect(_engine, &PlayerEngine::tracksChanged, this, &SubtitlesView::populateSubtitles);
        connect(_engine, &PlayerEngine::sidChanged, this, &SubtitlesView::onSidChanged);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &SubtitlesView::onThemeChanged);
        onThemeChanged();
    }

protected:
    void showEvent(QShowEvent *se) override 
    {
        ensurePolished();
        populateSubtitles();
    }

protected slots:
    void onThemeChanged() 
    {
        if (qApp->theme() == "dark") {
            setBackgroundColor(DBlurEffectWidget::DarkColor);
        } else {
            setBackgroundColor(DBlurEffectWidget::LightColor);
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
            auto sh = siw->sizeHint();
            item->setSizeHint(sh);
            _subsView->setItemWidget(item, siw);
            auto v = (sid == sub["id"].toInt());
            siw->setCurrent(v);
            if (v) {
                _subsView->setCurrentItem(item);
            }
        }
    }

    void onSidChanged()
    {
        auto sid = _engine->sid();
        for (int i = 0; i < _subsView->count(); ++i) {
            auto siw = static_cast<SubtitleItemWidget*>(_subsView->itemWidget(_subsView->item(i)));
            siw->setCurrent(siw->sid() == sid);
        }

        qDebug() << "current " << _subsView->currentRow();
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
        setWindowFlags(Qt::Popup);

        setShadowBlurRadius(4);
        setRadius(4);
        setShadowDistance(0);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(6);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::updateBg);

        updateBg();
        
        auto *l = new QVBoxLayout;
        l->setContentsMargins(0, 0, 0, 0);
        setLayout(l);

        _slider = new QSlider(this);
        _slider->installEventFilter(this);
        _slider->show();
        _slider->setRange(0, 100);
        _slider->setOrientation(Qt::Vertical);

        _slider->setValue(_engine->volume());
        l->addWidget(_slider);

        connect(_slider, &QSlider::valueChanged, [=]() { _engine->changeVolume(_slider->value()); });
    }


    ~VolumeSlider() {
        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::updateBg);
    }
        
private slots:
    void updateBg() {
        if (qApp->theme() == "dark") {
            setBackgroundColor(DBlurEffectWidget::DarkColor);
        } else {
            setBackgroundColor(DBlurEffectWidget::LightColor);
        }
    }

    bool eventFilter(QObject *obj, QEvent *e) {
    if (e->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent*>(e);
        qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
        if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
            if (_slider->value() == _slider->maximum() && we->angleDelta().y() > 0) {
                //keep increasing volume
                _engine->volumeUp();
            }
        }
        return false;
    } else {
        return QObject::eventFilter(obj, e);
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
    setAttribute(Qt::WA_TranslucentBackground);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

    DThemeManager::instance()->registerWidget(this);

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
    auto *stacked = new QStackedLayout(this);
    stacked->setContentsMargins(0, 0, 0, 0);
    stacked->setStackingMode(QStackedLayout::StackAll);
    setLayout(stacked);

    _progBar = new DMRSlider();
    _progBar->setObjectName("MovieProgress");
    _progBar->setOrientation(Qt::Horizontal);
    _progBar->setFixedHeight(10);
    _progBar->setRange(0, 100);
    _progBar->setValue(0);
    connect(_progBar, &QSlider::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::leave, [=]() { _previewer->hide(); });
    stacked->addWidget(_progBar);

    auto *bot_widget = new QWidget;
    auto *bot = new QHBoxLayout();
    bot->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    bot_widget->setLayout(bot);
    stacked->addWidget(bot_widget);

    _timeLabel = new QLabel("");
    _timeLabel->setFixedWidth(_timeLabel->fontMetrics().width("239:59/240:00"));
    bot->addWidget(_timeLabel);

    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

    bot->addStretch();

    _mid = new QHBoxLayout();
    _mid->setContentsMargins(0, 0, 0, 0);
    _mid->setSpacing(14);
    bot->addLayout(_mid);
    
    _prevBtn = new DImageButton();
    _prevBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    _prevBtn->setObjectName("PrevBtn");
    connect(_prevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_prevBtn, "prev");
    _mid->addWidget(_prevBtn);

    _playBtn = new DImageButton();
    _playBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    connect(_playBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_playBtn, "play");
    _mid->addWidget(_playBtn);

    _nextBtn = new DImageButton();
    _nextBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    _nextBtn->setObjectName("NextBtn");
    connect(_nextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_nextBtn, "next");
    _mid->addWidget(_nextBtn);

    bot->addStretch();

    _right = new QHBoxLayout();
    _right->setContentsMargins(0, 0, 0, 0);
    _right->setSizeConstraint(QLayout::SetFixedSize);
    _right->setSpacing(0);
    bot->addLayout(_right);

    _subBtn = new DImageButton();
    _subBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    _subBtn->setObjectName("SubtitleBtn");
    connect(_subBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_subBtn, "sub");
    _right->addWidget(_subBtn);

    _volBtn = new VolumeButton();
    _volBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    connect(_volBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_volBtn, "vol");
    _right->addWidget(_volBtn);

    _fsBtn = new DImageButton();
    _fsBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");
    _right->addWidget(_fsBtn);

#ifndef ENABLE_VPU_PLATFORM
    _listBtn = new DImageButton();
    _listBtn->setFixedSize(48, TOOLBOX_HEIGHT);
    _listBtn->setObjectName("ListBtn");
    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    _right->addWidget(_listBtn);
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
    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        _timeLabel->setText("");

    } else {
        auto fn = [](qint64 d) -> QString {
            auto secs = d % 60;
            auto minutes = d / 60;

            auto ss = QString("%1%2").arg(secs < 10 ? "0":"").arg(secs);
            auto ms = QString("%1%2").arg(minutes < 10 ? "0":"").arg(minutes);
            return QString("%1:%2").arg(ms).arg(ss);
        };

        _timeLabel->setText(QString("%2/%1").arg(fn(duration)).arg(fn(pos)));
    }
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
        w->show(pos.x() + w->width(), pos.y() - 5);

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

void ToolboxProxy::showEvent(QShowEvent *event)
{
    // to keep left and right of the same width. which makes play button centered
    auto right_geom = _right->geometry();
    int left_w = _timeLabel->fontMetrics().width("239:59/240:00");
    int w = qMax(left_w, right_geom.width());
    _timeLabel->setFixedWidth(w + RIGHT_MARGIN - LEFT_MARGIN); 
    right_geom.setWidth(w);
    _right->setGeometry(right_geom);
}

}


#include "toolbox_proxy.moc"
