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
#include "tip.h"
#include "utils.h"

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

class TooltipHandler: public QObject {
public:
    TooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        switch (event->type()) {
            case QEvent::ToolTip: {
                QHelpEvent *he = static_cast<QHelpEvent *>(event);
                auto tip = obj->property("HintWidget").value<Tip*>();
                auto btn = tip->property("for").value<QWidget*>();
                tip->setText(btn->toolTip());
                tip->show();
                tip->raise();
                tip->adjustSize();

                auto mw = tip->parentWidget();
                auto sz = tip->size();

                QPoint pos = btn->parentWidget()->mapToParent(btn->pos());
                pos.ry() = mw->rect().bottom() - 65 - sz.height();
                pos.rx() = pos.x() - sz.width()/2 + btn->width()/2;
                tip->move(pos);
                return true;
            }

            case QEvent::Leave: {
                auto parent = obj->property("HintWidget").value<Tip*>();
                parent->hide();
                event->ignore();

            }
            default: break;
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
};

class SubtitlesView;
class SubtitleItemWidget: public QWidget {
    Q_OBJECT
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *parent, SubtitleInfo si): QWidget() {
        _sid = si["id"].toInt();

        DThemeManager::instance()->registerWidget(this, QStringList() << "current");
        
        setFixedWidth(200);

        auto *l = new QHBoxLayout(this);
        setLayout(l);
        l->setContentsMargins(0, 0, 0, 0);

        _msg = si["title"].toString();
        auto shorted = fontMetrics().elidedText(_msg, Qt::ElideMiddle, 140*2);
        _title = new QLabel(shorted);
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

protected:
    void showEvent(QShowEvent *se) override
    {
        auto fm = _title->fontMetrics();
        auto shorted = fm.elidedText(_msg, Qt::ElideMiddle, 140*2);
        int h = fm.height();
        if (fm.width(shorted) > 140) {
            h *= 2;
        } else {
        }
        _title->setFixedHeight(h);
        _title->setText(shorted);
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
    QString _msg;
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
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(6);

        QSizePolicy sz_policy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        setSizePolicy(sz_policy);

        setFixedWidth(220);

        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(8, 2, 8, 2);
        l->setSpacing(0);
        setLayout(l);

        _subsView = new QListWidget(this);
        _subsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setResizeMode(QListView::Adjust);
        _subsView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        _subsView->setSelectionMode(QListWidget::SingleSelection);
        _subsView->setSelectionBehavior(QListWidget::SelectItems);
        l->addWidget(_subsView);

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
        setFixedHeight(_subsView->height() + 4);
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

    void batchUpdateSizeHints()
    {
        QSize sz(0, 0);
        if (isVisible()) {
            for (int i = 0; i < _subsView->count(); i++) {
                auto item = _subsView->item(i);
                auto w = _subsView->itemWidget(item);
                item->setSizeHint(w->sizeHint());
                sz += w->sizeHint();
                sz += QSize(0, 2);
            }
        }
        sz += QSize(0, 2);
        _subsView->setFixedHeight(sz.height());
    }

    void populateSubtitles()
    {
        _subsView->clear();
        _subsView->adjustSize();
        adjustSize();

        auto pmf = _engine->playingMovieInfo();
        auto sid = _engine->sid();
        qDebug() << "sid" << sid;

        for (const auto& sub: pmf.subs) {
            auto item = new QListWidgetItem();
            auto siw = new SubtitleItemWidget(this, sub);
            _subsView->addItem(item);
            _subsView->setItemWidget(item, siw);
            auto v = (sid == sub["id"].toInt());
            siw->setCurrent(v);
            if (v) {
                _subsView->setCurrentItem(item);
            }
        }

        batchUpdateSizeHints();
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
        
        setObjectName("ThumbnailPreview");

        setFixedSize(162, 103);

        setShadowBlurRadius(6);
        setRadius(6);
        setShadowYOffset(4);
        setShadowXOffset(0);
        setArrowWidth(18);
        setArrowHeight(10);

        auto *l = new QVBoxLayout;
        l->setContentsMargins(2, 2, 2, 2+10);
        setLayout(l);

        _thumb = new QLabel(this);
        _thumb->setFixedSize(ThumbnailWorker::thumbSize());
        l->addWidget(_thumb);

        _time = new QLabel(this);
        _time->setAlignment(Qt::AlignCenter);
        _time->setFixedSize(64, 18);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &ThumbnailPreview::updateTheme);
        updateTheme();

        winId(); // force backed window to be created
    }

    void updateWithPreview(const QPixmap& pm, qint64 secs, int rotation) {
        auto rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);
        _thumb->setPixmap(rounded);

        QTime t(0, 0, 0);
        t = t.addSecs(secs);
        _time->setText(t.toString("hh:mm:ss"));
        _time->move((width() - _time->width())/2, 69);

        if (isVisible()) {
            move(QCursor::pos().x(), frameGeometry().y() + height());
        }
    }

    void updateWithPreview(const QPoint& pos) {
        resizeWithContent();
        move(pos.x(), pos.y() - 5);
        show(pos.x(), pos.y() - 5);
    }

protected slots:
    void updateTheme()
    {
        if (qApp->theme() == "dark") {
            setBackgroundColor(QColor(23, 23, 23, 255 * 8 / 10));
            setBorderColor(QColor(255, 255 ,255, 25));
            _time->setStyleSheet(R"(
                border-radius: 3px;
                background-color: rgba(23, 23, 23, 0.8);
                font-size: 12px;
                color: #ffffff; 
            )");
        } else {
            setBackgroundColor(QColor(255, 255, 255, 255 * 8 / 10));
            setBorderColor(QColor(0, 0 ,0, 25));
            _time->setStyleSheet(R"(
                border-radius: 3px;
                background-color: rgba(255, 255, 255, 0.8);
                font-size: 12px;
                color: #303030; 
            )");
        }
    }

protected:
    void showEvent(QShowEvent *se) override
    {
        _time->move((width() - _time->width())/2, 69);
    }

private:
    QLabel *_thumb;
    QLabel *_time;
};

class VolumeSlider: public DArrowRectangle {
    Q_OBJECT
public:
    VolumeSlider(PlayerEngine* eng, MainWindow* mw)
        :DArrowRectangle(DArrowRectangle::ArrowBottom), _engine(eng), _mw(mw) {
        setFixedSize(QSize(24, 105));
        setWindowFlags(Qt::ToolTip);

        setShadowBlurRadius(4);
        setRadius(4);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(6);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::updateBg);

        updateBg();
        
        auto *l = new QVBoxLayout;
        l->setContentsMargins(0, 4, 0, 10);
        setLayout(l);

        _slider = new QSlider(this);
        _slider->installEventFilter(this);
        _slider->show();
        _slider->setRange(0, 100);
        _slider->setOrientation(Qt::Vertical);

        _slider->setValue(_engine->volume());
        l->addWidget(_slider);

        connect(_slider, &QSlider::valueChanged, [=]() {
            _mw->requestAction(ActionFactory::ChangeVolume, false, QList<QVariant>() << _slider->value());
        });

        _autoHideTimer.setSingleShot(true);
        connect(&_autoHideTimer, &QTimer::timeout, this, &VolumeSlider::hide);

        connect(_engine, &PlayerEngine::volumeChanged, [=]() {
            _slider->setValue(_engine->volume());
        });
    }


    ~VolumeSlider() {
        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged, 
                this, &VolumeSlider::updateBg);
    }

    void stopTimer() {
        _autoHideTimer.stop();
    }

public slots:
    void delayedHide() {
        _autoHideTimer.start(500);
    }

protected:
    void enterEvent(QEvent* e) {
        _autoHideTimer.stop();
    }

    void showEvent(QShowEvent* se) {
        _autoHideTimer.stop();
    }

    void leaveEvent(QEvent* e) {
        _autoHideTimer.start(500);
    }
        
private slots:
    void updateBg() {
        if (qApp->theme() == "dark") {
            setBackgroundColor(QColor(49, 49, 49, 255 * 9 / 10));
        } else {
            setBackgroundColor(QColor(255, 255, 255, 255 * 9 / 10));
        }
    }

    bool eventFilter(QObject *obj, QEvent *e) {
    if (e->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent*>(e);
        qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
        if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
            if (_slider->value() == _slider->maximum() && we->angleDelta().y() > 0) {
                //keep increasing volume
                _mw->requestAction(ActionFactory::VolumeUp);
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
    MainWindow *_mw;
    QTimer _autoHideTimer;
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
    _progBar->setEnableIndication(Settings::get().isSet(Settings::PreviewOnMouseover));

    connect(_progBar, &QSlider::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::leave, [=]() { _previewer->hide(); });
    connect(&Settings::get(), &Settings::baseChanged,
        [=](QString sk, const QVariant& val) {
            if (sk == "base.play.mousepreview") {
                _progBar->setEnableIndication(val.toBool());
            }
        });
    stacked->addWidget(_progBar);

	auto *border_frame = new QFrame;
    border_frame->setFrameShape(QFrame::NoFrame);
    border_frame->setFixedHeight(1);
    border_frame->setObjectName("ToolBoxTopBorder");
    stacked->addWidget(border_frame);

	auto *inner_border_frame = new QFrame;
    inner_border_frame->setFrameShape(QFrame::NoFrame);
    inner_border_frame->setFixedHeight(2);
    inner_border_frame->setObjectName("ToolBoxTopBorderInner");
    stacked->addWidget(inner_border_frame);


    auto *bot_widget = new QWidget;
    auto *bot = new QHBoxLayout();
    bot->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    bot_widget->setLayout(bot);
    stacked->addWidget(bot_widget);


    _timeLabel = new QLabel("");
    _timeLabel->setFixedWidth(_timeLabel->fontMetrics().width("99:99:99/99:99:99"));
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

    _volSlider = new VolumeSlider(_engine, _mainWindow);
    connect(_volBtn, &VolumeButton::entered, [=]() {
        _volSlider->stopTimer();
        QPoint pos = _volBtn->parentWidget()->mapToGlobal(_volBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        _volSlider->show(pos.x() + _volSlider->width(), pos.y() - 5);
    });
    connect(_volBtn, &VolumeButton::leaved, _volSlider, &VolumeSlider::delayedHide);
    connect(_volBtn, &VolumeButton::requestVolumeUp, [=]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeUp);
    });
    connect(_volBtn, &VolumeButton::requestVolumeDown, [=]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeDown);
    });



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

    // these tooltips is not used due to deepin ui design
    _playBtn->setToolTip(tr("Play/Pause"));
    //_volBtn->setToolTip(tr("Volume"));
    _prevBtn->setToolTip(tr("Previous"));
    _nextBtn->setToolTip(tr("Next"));
    _subBtn->setToolTip(tr("Subtitles"));
    _listBtn->setToolTip(tr("Playlist"));
    _fsBtn->setToolTip(tr("Fullscreen"));

    auto th = new TooltipHandler(this);
    QWidget* btns[] = {
        _playBtn, _prevBtn, _nextBtn, _subBtn, _listBtn, _fsBtn,
    };
    QString hints[] = {
        tr("Play/Pause"), tr("Previous"), tr("Next"),
        tr("Subtitles"), tr("Playlist"), tr("Fullscreen"),
    };

    for (int i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
        auto t = new Tip(QPixmap(), hints[i], parentWidget());
        t->setFixedHeight(32);
        t->setProperty("for", QVariant::fromValue<QWidget*>(btns[i]));
        btns[i]->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        btns[i]->installEventFilter(th);
    }

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
    connect(_mainWindow, &MainWindow::initChanged, this, &ToolboxProxy::updateButtonStates);

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
    return _previewer->isVisible() || _subView->isVisible() || _volSlider->isVisible();
}

void ToolboxProxy::updateHoverPreview(const QUrl& url, int secs)
{
    if (_engine->playlist().currentInfo().url != url)
        return;

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);

    _previewer->updateWithPreview(pm, secs, _engine->videoRotation());
}

void ToolboxProxy::progressHoverChanged(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    if (!Settings::get().isSet(Settings::PreviewOnMouseover))
        return;

    if (_volSlider->isVisible())
        return;

    const auto& pif = _engine->playlist().currentInfo();
    if (!pif.url.isLocalFile())
        return;

    const auto& absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        _previewer->hide();
        return;
    }

    _lastHoverValue = v;
    ThumbnailWorker::get().requestThumb(pif.url, v);

    auto pos = _progBar->mapToGlobal(QPoint(0, 0));
    QPoint p = {
        QCursor::pos().x(), pos.y()
    };

    _previewer->updateWithPreview(p);
}

void ToolboxProxy::setProgress()
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    _engine->seekAbsolute(_progBar->sliderPosition());
    if (_progBar->sliderPosition() != _lastHoverValue) {
        progressHoverChanged(_progBar->sliderPosition());
    }
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
    _progBar->blockSignals(true);
    _progBar->setValue(v);
    _progBar->blockSignals(false);
}

void ToolboxProxy::updateButtonStates()
{
    qDebug() << _engine->playingMovieInfo().subs.size();
    bool vis = _engine->playlist().count() > 1 && _mainWindow->inited();
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
        //_volBtn->setToolTip(tr("Mute"));
    } else {
        auto v = _engine->volume();
        //_volBtn->setToolTip(tr("Volume"));
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
        _fsBtn->setToolTip(tr("Exit fullscreen"));
    } else {
        _fsBtn->setObjectName("FsBtn");
        _fsBtn->setToolTip(tr("Fullscreen"));
    }
    _fsBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updatePlayState()
{
    qDebug() << __func__ << _engine->state();
    if (_engine->state() == PlayerEngine::CoreState::Playing) {
        _playBtn->setObjectName("PauseBtn");
        _playBtn->setToolTip(tr("Pause"));
    } else {
        _playBtn->setObjectName("PlayBtn");
        _playBtn->setToolTip(tr("Play"));
    }

    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        if (_subView->isVisible())
            _subView->hide();

        if (_previewer->isVisible()) {
            _previewer->hide();
        }
        setProperty("idle", true);
    } else {
        setProperty("idle", false);
    }
    
    _progBar->setEnabled(_engine->state() != PlayerEngine::CoreState::Idle);
    setStyleSheet(styleSheet());
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        _timeLabel->setText("");

    } else {
        //mpv returns a slightly different duration from movieinfo.duration
        //_timeLabel->setText(QString("%2/%1").arg(utils::Time2str(duration))
                //.arg(utils::Time2str(pos)));
        _timeLabel->setText(QString("%2/%1")
                .arg(_engine->playlist().currentInfo().mi.durationStr())
                .arg(utils::Time2str(pos)));
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
        _mainWindow->requestAction(ActionFactory::ActionKind::ToggleFullscreen);
    } else if (id == "vol") {
        _mainWindow->requestAction(ActionFactory::ActionKind::ToggleMute);
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

void ToolboxProxy::resizeEvent(QResizeEvent* re)
{
#ifndef USE_DXCB
    QPixmap shape(size());
    shape.fill(Qt::transparent);

    QPainter p(&shape);
    p.setRenderHint(QPainter::Antialiasing);

    auto radius = RADIUS;
    auto titleBarHeight = this->height();
    QRectF r = rect();

    QRectF botLeftRect(r.bottomLeft() - QPoint(0, 2 * radius), QSize(2 * radius, 2 * radius));
    QRectF botRightRect(QPoint(r.right() - 2 * radius, r.bottom() - 2 * radius),
                        QSize(2 * radius, 2 * radius));

    QPainterPath border;
    border.moveTo(r.topLeft());
    border.lineTo(r.topRight());
    border.lineTo(r.right(), r.bottom() - radius);
    border.arcTo(botRightRect, 0.0, -90.0);
    border.lineTo(r.left() + radius, r.bottom());
    border.arcTo(botLeftRect, 270.0, -90.0);
    border.closeSubpath();

    p.setClipPath(border);
    p.fillPath(border, QBrush(Qt::white));
    p.end();

    setMask(shape.mask());
#endif

}

void ToolboxProxy::showEvent(QShowEvent *event)
{
    // to keep left and right of the same width. which makes play button centered
    auto right_geom = _right->geometry();
    int left_w = _timeLabel->fontMetrics().width("99:99:99/99:99:99");
    int w = qMax(left_w, right_geom.width());
    _timeLabel->setFixedWidth(w + RIGHT_MARGIN - LEFT_MARGIN); 
    right_geom.setWidth(w);
    _right->setGeometry(right_geom);
}

}


#include "toolbox_proxy.moc"
