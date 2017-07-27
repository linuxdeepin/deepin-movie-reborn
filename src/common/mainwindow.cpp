#include "config.h"

#include "mainwindow.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"
#include "compositing_manager.h"
#include "shortcut_manager.h"
#include "dmr_settings.h"
#include "utility.h"
#include "movieinfo_dialog.h"
#include "burst_screenshots_dialog.h"
#include "playlist_widget.h"
#include "notification_widget.h"
#include "player_engine.h"
#include "url_dialog.h"

#include <QtWidgets>
#include <QtDBus>
#include <DApplication>
#include <DTitlebar>
#include <dsettingsdialog.h>
#include <dthememanager.h>
#include <daboutdialog.h>
#include <dinputdialog.h>
#include <dimagebutton.h>

DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 6

class MainWindowFocusMonitor: public QAbstractNativeEventFilter {
public:
    MainWindowFocusMonitor(MainWindow* src) :QAbstractNativeEventFilter(), _source(src) {
        qApp->installNativeEventFilter(this);
    }

    ~MainWindowFocusMonitor() {
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *) {
        if(Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t* event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
                case XCB_LEAVE_NOTIFY: {
                    xcb_leave_notify_event_t *dne = (xcb_leave_notify_event_t*)event;
                    auto w = _source->windowHandle();
                    if (dne->event == w->winId()) {
                        //qDebug() << "---------  leave " << dne->event << dne->child;
                        emit _source->windowLeaved();
                    }
                    break;
                }

                case XCB_ENTER_NOTIFY: {
                    xcb_enter_notify_event_t *dne = (xcb_enter_notify_event_t*)event;
                    auto w = _source->windowHandle();
                    if (dne->event == w->winId()) {
                        //qDebug() << "---------  enter " << dne->event << dne->child;
                        emit _source->windowEntered();
                    }
                    break;
                }
                default:
                    break;
            }
        }

        return false;
    }

    MainWindow *_source;
};

class MainWindowEventListener : public QObject
{
    Q_OBJECT
    public:
        explicit MainWindowEventListener(QWidget *target)
            : QObject(target), _window(target->windowHandle())
        {
        }

        ~MainWindowEventListener()
        {
        }

        void setEnabled(bool v) 
        {
            enabled = v;
        }

    protected:
        bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE {
            QWindow *window = qobject_cast<QWindow*>(obj);
            if (!window || !enabled) return false;

            switch ((int)event->type()) {
            case QEvent::MouseButtonPress: {
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                setLeftButtonPressed(true);
                if (insideResizeArea(e)) startResizing = true;
                break;
            }
            case QEvent::MouseButtonRelease: {
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                setLeftButtonPressed(false);
                qApp->setOverrideCursor(window->cursor());
                startResizing = false;
                break;
            }
            case QEvent::MouseMove: {
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                const QRect window_visible_rect = _window->frameGeometry() - margins;
                //qDebug() << "mouse move" << "press" << leftButtonPressed
                    //<< "insideResizeArea" << insideResizeArea(e);

                if (!leftButtonPressed) {
                    if (insideResizeArea(e)) {
                        Utility::CornerEdge mouseCorner;
                        QRect cornerRect;

                        /// begin set cursor corner type
                        cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                        cornerRect.moveTopLeft(_window->frameGeometry().topLeft());
                        if (cornerRect.contains(e->globalPos())) {
                            mouseCorner = Utility::TopLeftCorner;
                            goto set_cursor;
                        }

                        cornerRect.moveTopRight(_window->frameGeometry().topRight());
                        if (cornerRect.contains(e->globalPos())) {
                            mouseCorner = Utility::TopRightCorner;
                            goto set_cursor;
                        }

                        cornerRect.moveBottomRight(_window->frameGeometry().bottomRight());
                        if (cornerRect.contains(e->globalPos())) {
                            mouseCorner = Utility::BottomRightCorner;
                            goto set_cursor;
                        }

                        cornerRect.moveBottomLeft(_window->frameGeometry().bottomLeft());
                        if (cornerRect.contains(e->globalPos())) {
                            mouseCorner = Utility::BottomLeftCorner;
                            goto set_cursor;
                        }

                        /// begin set cursor edge type
                        if (e->globalX() <= window_visible_rect.x()) {
                            mouseCorner = Utility::LeftEdge;
                        } else if (e->globalX() < window_visible_rect.right()) {
                            if (e->globalY() <= window_visible_rect.y()) {
                                mouseCorner = Utility::TopEdge;
                            } else if (e->globalY() >= window_visible_rect.bottom()) {
                                mouseCorner = Utility::BottomEdge;
                            } else {
                                goto skip_set_cursor;
                            }
                        } else if (e->globalX() >= window_visible_rect.right()) {
                            mouseCorner = Utility::RightEdge;
                        } else {
                            goto skip_set_cursor;
                        }
set_cursor:
                        Utility::setWindowCursor(window->winId(), mouseCorner);

                        if (qApp->mouseButtons() == Qt::LeftButton) {
                            updateGeometry(mouseCorner, e);
                        }
                        lastCornerEdge = mouseCorner;

                        return true;
                    } else {
                        qApp->setOverrideCursor(window->cursor());
                    }
                } else {
                    if (startResizing) {
                        updateGeometry(lastCornerEdge, e);
                        return true;
                    }
                }
                
skip_set_cursor:
                break;
            }

            default: break;
            }

            return false;
        }

    private:
        void setLeftButtonPressed(bool pressed) {
            if (leftButtonPressed == pressed)
                return;

            if (!pressed)
                Utility::cancelWindowMoveResize(_window->winId());

            leftButtonPressed = pressed;
        }

        bool insideResizeArea(QMouseEvent *e) {
            const QRect window_visible_rect = _window->frameGeometry() - margins;
            return !window_visible_rect.contains(e->globalPos());
        }

        void updateGeometry(Utility::CornerEdge edge, QMouseEvent* e) {
            auto mw = static_cast<MainWindow*>(parent());
            bool keep_ratio = mw->engine()->state() != PlayerEngine::CoreState::Idle;
            auto old_geom = mw->frameGeometry();
            auto geom = mw->frameGeometry();
            qreal ratio = (qreal)geom.width() / geom.height();

            if (keep_ratio) {
                auto sz = mw->engine()->videoSize();
                if (sz.isEmpty()) {
                    const auto& mi = mw->engine()->playlist().currentInfo().mi;
                    sz = QSize(mi.width, mi.height);
                }

                ratio = sz.width() / (qreal)sz.height();
            }

            switch (edge) {
                case Utility::BottomLeftCorner:
                case Utility::TopLeftCorner:
                case Utility::LeftEdge:
                    geom.setLeft(e->globalX());
                    geom.setHeight(geom.width() / ratio);
                    break;
                case Utility::BottomRightCorner:
                case Utility::RightEdge:
                    geom.setRight(e->globalX());
                    geom.setHeight(geom.width() / ratio);
                    break;
                case Utility::TopRightCorner:
                case Utility::TopEdge:
                    geom.setTop(e->globalY());
                    geom.setWidth(geom.height() * ratio);
                    break;
                case Utility::BottomEdge:
                    geom.setBottom(e->globalY());
                    geom.setWidth(geom.height() * ratio);
                    break;
            }
            mw->setGeometry(geom);
        }

        const QMargins margins{MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS};
        bool leftButtonPressed {false};
        bool startResizing {false};
        bool enabled {true};
        Utility::CornerEdge lastCornerEdge;
        QWindow* _window;
};

#ifdef USE_DXCB
/// shadow
#define SHADOW_COLOR_NORMAL QColor(0, 0, 0, 255 * 0.35)
#define SHADOW_COLOR_ACTIVE QColor(0, 0, 0, 255 * 0.6)
#endif

MainWindow::MainWindow(QWidget *parent) 
    : QWidget(NULL)
{
    setWindowFlags(Qt::FramelessWindowHint);
    //this'll crash MainWindow while desstruction
    //setAttribute(Qt::WA_DeleteOnClose);
    
    bool composited = CompositingManager::get().composited();
#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform() && composited) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        //setAttribute(Qt::WA_TranslucentBackground, true);
        //_handle->setTranslucentBackground(true);
        _cachedMargins = _handle->frameMargins();
        _handle->enableSystemResize();
        _handle->enableSystemMove();
        _handle->setWindowRadius(5);

        connect(qApp, &QGuiApplication::focusWindowChanged, this, [=] {
            if (this->isActiveWindow()) {
                _handle->setShadowColor(SHADOW_COLOR_ACTIVE);
            } else {
                _handle->setShadowColor(SHADOW_COLOR_NORMAL);
            }
        });
    }
#else
    winId();
#endif

    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);

    qDebug() << "composited = " << composited;

    _titlebar = new DTitlebar(this);
    _titlebar->layout()->setContentsMargins(0, 0, 0, 0);
    _titlebar->setFocusPolicy(Qt::NoFocus);
    if (!composited) {
        _titlebar->setAttribute(Qt::WA_NativeWindow);
        _titlebar->winId();
    }
    _titlebar->setMenu(ActionFactory::get().titlebarMenu());
    _titlebar->setIcon(QPixmap(":/resources/icons/logo.svg"));
    _titlebar->setTitle(tr("Deepin Movie"));

    _engine = new PlayerEngine(this);
    _engine->move(0, 0);

    _toolbox = new ToolboxProxy(this, _engine);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().mainContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);

    _playlist = new PlaylistWidget(this, _engine);
    _playlist->hide();

    _playState = new QLabel(this);


    // mini ui
    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper,
            static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &MainWindow::miniButtonClicked);

    _miniPlayBtn = new DImageButton(this);
    _miniPlayBtn->setObjectName("MiniPlayBtn");
    connect(_miniPlayBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniPlayBtn, "play");

    connect(_engine, &PlayerEngine::stateChanged, [=]() {
        qDebug() << __func__ << _engine->state();
        if (_engine->state() == PlayerEngine::CoreState::Playing) {
            _miniPlayBtn->setObjectName("MiniPauseBtn");
        } else {
            _miniPlayBtn->setObjectName("MiniPlayBtn");
        }
        _miniPlayBtn->setStyleSheet(_miniPlayBtn->styleSheet());
    });

    _miniCloseBtn = new DImageButton(this);
    _miniCloseBtn->setObjectName("MiniCloseBtn");
    connect(_miniCloseBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniCloseBtn, "close");

    _miniQuitMiniBtn = new DImageButton(this);
    _miniQuitMiniBtn->setObjectName("MiniQuitMiniBtn");
    connect(_miniQuitMiniBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniQuitMiniBtn, "quit_mini");

    _miniPlayBtn->setVisible(_miniMode);
    _miniCloseBtn->setVisible(_miniMode);
    _miniQuitMiniBtn->setVisible(_miniMode);
    // ~ 
 
    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();

    connect(_engine, &PlayerEngine::tracksChanged, this, &MainWindow::updateActionsState);
    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::updateActionsState);
    updateActionsState();

    reflectActionToUI(ActionFactory::ActionKind::DefaultFrame);
    reflectActionToUI(ActionFactory::ActionKind::OrderPlay);
    reflectActionToUI(ActionFactory::ActionKind::Stereo);

    connect(_engine, &PlayerEngine::sidChanged, [=]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    });
    connect(_engine, &PlayerEngine::aidChanged, [=]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    });

    auto updateConstraints = [=]() {
        if (_engine->state() == PlayerEngine::Idle || _engine->playlist().count() == 0) {
            _titlebar->setTitle(tr("Deepin Movie"));
            return;
        }

        const auto& mi = _engine->playlist().currentInfo().mi;
        _titlebar->setTitle(QFileInfo(mi.filePath).fileName());
        auto sz = _engine->videoSize();
        if (sz.isEmpty()) {
            sz = QSize(mi.width, mi.height);
            qDebug() << mi.width << mi.height;
        }
        
        auto geom = qApp->desktop()->availableGeometry(this);
        if (sz.width() > geom.width() || sz.height() > geom.height()) 
            sz.scale(geom.width(), geom.height(), Qt::KeepAspectRatio);

        qDebug() << "updateConstraints: " << sz;
        resize(sz);
    };
    connect(_engine, &PlayerEngine::fileLoaded, updateConstraints);
    connect(_engine, &PlayerEngine::videoSizeChanged, updateConstraints);
    connect(&_engine->playlist(), &PlaylistModel::currentChanged, updateConstraints);

    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::updatePlayState);

    connect(DThemeManager::instance(), &DThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
    onThemeChanged();

#ifdef USE_DXCB
    if (!composited) {
        connect(qApp, &QGuiApplication::applicationStateChanged,
                this, &MainWindow::onApplicationStateChanged);

        _evm = new EventMonitor(this);
        connect(_evm, &EventMonitor::buttonedPress, this, &MainWindow::onMonitorButtonPressed);
        connect(_evm, &EventMonitor::buttonedDrag, this, &MainWindow::onMonitorMotionNotify);
        connect(_evm, &EventMonitor::buttonedRelease, this, &MainWindow::onMonitorButtonReleased);
        _evm->start();
    }
#else
    _listener = new MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(_listener);

    auto mwfm = new MainWindowFocusMonitor(this);
    connect(this, &MainWindow::windowEntered, &MainWindow::resumeToolsWindow);
    connect(this, &MainWindow::windowLeaved, &MainWindow::suspendToolsWindow);

    if (!composited) {
        if (_engine->windowHandle())
            _engine->windowHandle()->installEventFilter(_listener);
        _titlebar->windowHandle()->installEventFilter(_listener);
        _toolbox->windowHandle()->installEventFilter(_listener);
    }
    qDebug() << "event listener";
#endif
}

#ifdef USE_DXCB
static QPoint last_engine_pos;
static QPoint last_wm_pos;
static bool clicked = false;
void MainWindow::onMonitorButtonPressed(int x, int y)
{
    QPoint p(x, y);
    int d = 2;
    QMargins m(d, d, d, d);
    if (geometry().marginsRemoved(m).contains(p)) {
        auto w = qApp->topLevelAt(p);
        if (w && w == this) {
            qDebug() << __func__ << "click inside main window";
            last_wm_pos = QPoint(x, y);
            last_engine_pos = windowHandle()->framePosition();
            clicked = true;
        }
    }
}

void MainWindow::onMonitorButtonReleased(int x, int y)
{
    if (clicked) {
        qDebug() << __func__;
        clicked = false;
    }
}

void MainWindow::onMonitorMotionNotify(int x, int y)
{
    if (clicked) {
        QPoint d = QPoint(x, y) - last_wm_pos;
        windowHandle()->setFramePosition(last_engine_pos + d);
    }
}

#endif

MainWindow::~MainWindow()
{
    qDebug() << __func__;
    disconnect(_engine, 0, 0, 0);
    disconnect(&_engine->playlist(), 0, 0, 0);

#ifdef USE_DXCB
    if (_evm) {
        disconnect(_evm, 0, 0, 0);
        delete _evm;
    }
#endif
}

void MainWindow::onApplicationStateChanged(Qt::ApplicationState e)
{
    switch (e) {
        case Qt::ApplicationActive:
            if (qApp->focusWindow())
                qDebug() << QString("focus window 0x%1").arg(qApp->focusWindow()->winId(), 0, 16);
            qApp->setActiveWindow(this);
            _evm->resumeRecording();
            resumeToolsWindow();
            break;

        case Qt::ApplicationInactive:
            _evm->suspendRecording();
            suspendToolsWindow();
            break;

        default: break;
    }
}

void MainWindow::onThemeChanged()
{
    qDebug() << __func__ << qApp->theme();
    static QFile darkF(":/resources/qss/dark/widgets.qss");
    static QFile lightF(":/resources/qss/light/widgets.qss");

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

void MainWindow::updatePlayState()
{
    if (_engine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << __func__ << _engine->state();
        QPixmap pm;
        if (_engine->state() == PlayerEngine::CoreState::Playing) {
            pm = QPixmap(QString(":/resources/icons/%1/normal/pause-big.png")
                    .arg(qApp->theme()));
            QTimer::singleShot(100, [=]() { _playState->setVisible(false); });
        } else {
            pm = QPixmap(QString(":/resources/icons/%1/normal/play-big.png")
                    .arg(qApp->theme()));
        }
        _playState->setPixmap(pm);

        auto r = QRect(QPoint(0, 0), QSize(128, 128));
        r.moveCenter(rect().center());
        _playState->move(r.topLeft());

        _playState->setVisible(true);
        _playState->raise();
    }
}

void MainWindow::onBindingsChanged()
{
    qDebug() << __func__;
    {
        auto actions = this->actions();
        this->actions().clear();
        for (auto* act: actions) {
            delete act;
        }
    }

    auto& scmgr = ShortcutManager::get();
    auto actions = scmgr.actionsForBindings();
    for (auto* act: actions) {
        this->addAction(act);
        connect(act, &QAction::triggered, [=]() { 
            this->menuItemInvoked(act); 
            auto prop = act->property("kind");
            auto kd = ActionFactory::actionKind(act);
            reflectActionToUI(kd);
        });
    }
}

void MainWindow::updateActionsState()
{
    auto pmf = _engine->playingMovieInfo();
    auto update = [=](QAction* act) {
        auto prop = act->property("kind");
        auto kd = ActionFactory::actionKind(act);
        bool v = true;
        switch(kd) {
            case ActionFactory::ActionKind::Screenshot:
            case ActionFactory::ActionKind::ToggleMiniMode:
            case ActionFactory::ActionKind::Fullscreen:
            case ActionFactory::ActionKind::BurstScreenshot:
                v = _engine->state() != PlayerEngine::Idle;
                break;

            case ActionFactory::ActionKind::MovieInfo:
                v = _engine->state() != PlayerEngine::Idle;
                if (v) {
                    v = v && _engine->playlist().count();
                    if (v) {
                        auto pif =_engine->playlist().currentInfo();
                        v = v && pif.loaded && pif.url.isLocalFile();
                    }
                }
                break;

            case ActionFactory::ActionKind::HideSubtitle:
            case ActionFactory::ActionKind::SelectSubtitle:
                v = pmf.subs.size() > 0;
            default: break;
        }
        act->setEnabled(v);
    };

    ActionFactory::get().updateMainActionsForMovie(pmf);
    ActionFactory::get().forEachInMainMenu(update);
}

void MainWindow::reflectActionToUI(ActionFactory::ActionKind kd)
{
    QList<QAction*> acts;
    switch(kd) {
        case ActionFactory::ActionKind::WindowAbove:
        case ActionFactory::ActionKind::Fullscreen:
        case ActionFactory::ActionKind::ToggleMiniMode:
        case ActionFactory::ActionKind::TogglePlaylist:
        case ActionFactory::ActionKind::HideSubtitle: {
            qDebug() << __func__ << kd;
            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            while (p != acts.end()) {
                auto old = (*p)->isEnabled();
                (*p)->setEnabled(false);
                (*p)->setChecked(!(*p)->isChecked());
                (*p)->setEnabled(old);
                ++p;
            }
            break;
        }

        case ActionFactory::ActionKind::SelectTrack:
        case ActionFactory::ActionKind::SelectSubtitle: {
            if (_engine->state() == PlayerEngine::Idle)
                break;

            auto pmf = _engine->playingMovieInfo();
            int id = -1;
            int idx = -1;
            if (kd == ActionFactory::ActionKind::SelectTrack) {
                id = _engine->aid();
                for (idx = 0; idx < pmf.audios.size(); idx++) {
                    if (id == pmf.audios[idx]["id"].toInt()) {
                        break;
                    }
                }
            } else if (kd == ActionFactory::ActionKind::SelectSubtitle) {
                id = _engine->sid();
                for (idx = 0; idx < pmf.subs.size(); idx++) {
                    if (id == pmf.subs[idx]["id"].toInt()) {
                        break;
                    }
                }
            }

            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            while (p != acts.end()) {
                auto args = ActionFactory::actionArgs(*p);
                if (args[0].toInt() == idx) {
                    (*p)->setEnabled(false);
                    (*p)->setChecked(!(*p)->isChecked());
                    (*p)->setEnabled(true);
                    break;
                }

                ++p;
            }
            break;
        }

        case ActionFactory::ActionKind::Stereo:
        case ActionFactory::ActionKind::OrderPlay:
        case ActionFactory::ActionKind::DefaultFrame: {
            qDebug() << __func__ << kd;
            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            auto old = (*p)->isEnabled();
            (*p)->setEnabled(false);
            (*p)->setChecked(!(*p)->isChecked());
            (*p)->setEnabled(old);
            break;
        }
    }

}

void MainWindow::menuItemInvoked(QAction *action)
{
    auto prop = action->property("kind");
    auto kd = ActionFactory::actionKind(action);

    qDebug() << "prop = " << prop << ", kd = " << kd;
    if (ActionFactory::actionHasArgs(action)) {
        requestAction(kd, true, ActionFactory::actionArgs(action));
    } else {
        requestAction(kd, true);
    }
}

void MainWindow::requestAction(ActionFactory::ActionKind kd, bool fromUI, QList<QVariant> args)
{
    qDebug() << "kd = " << kd << "fromUI " << fromUI;
    switch (kd) {
        case ActionFactory::ActionKind::Exit:
            qApp->quit(); 
            break;

        case ActionFactory::ActionKind::LightTheme:
            _lightTheme = !_lightTheme;
            qApp->setTheme(_lightTheme? "light":"dark");
            break;

        case ActionFactory::ActionKind::OpenUrl: {
            UrlDialog dlg;
            if (dlg.exec() == QDialog::Accepted) {
                auto url = dlg.url();
                if (url.isValid()) {
                    play(url);
                }
            }
            break;
        }

        case ActionFactory::ActionKind::OpenFile: {
            QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Movies (*.mkv *.mov *.mp4 *.rmvb)"));
            if (QFileInfo(filename).exists()) {
                play(QUrl::fromLocalFile(filename));
                _engine->next();
            }
            break;
        }

        case ActionFactory::ActionKind::StartPlay: {
            if (_engine->playlist().count() == 0) {
                QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                        QDir::currentPath(),
                        tr("Movies (*.mkv *.mov *.mp4 *.rmvb)"));
                if (QFileInfo(filename).exists()) {
                    play(QUrl::fromLocalFile(filename));
                }
            } else {
                _engine->play();
            }
            break;
        }

        case ActionFactory::ActionKind::EmptyPlaylist: {
            _engine->clearPlaylist();
            break;
        }

        case ActionFactory::ActionKind::TogglePlaylist: {
            _playlist->togglePopup();
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionFactory::ActionKind::ToggleMiniMode: {
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            toggleUIMode();
            break;
        }

        case ActionFactory::ActionKind::MovieInfo: {
            if (_engine->state() != PlayerEngine::CoreState::Idle) {
                MovieInfoDialog mid(_engine->playlist().currentInfo().mi);
                mid.exec();
            }
            break;
        }

        case ActionFactory::ActionKind::WindowAbove:
            _windowAbove = !_windowAbove;
            Utility::setStayOnTop(this, _windowAbove);
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;

        case ActionFactory::ActionKind::Fullscreen: {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionFactory::ActionKind::PlaylistRemoveItem: {
            _playlist->removeClickedItem();
            break;
        }

        case ActionFactory::ActionKind::PlaylistOpenItemInFM: {
            _playlist->openItemInFM();
            break;
        }

        case ActionFactory::ActionKind::ClockwiseFrame: {
            auto old = _engine->videoRotation();
            _engine->setVideoRotation((old + 90) % 360);
            break;
        }
        case ActionFactory::ActionKind::CounterclockwiseFrame: {
            auto old = _engine->videoRotation();
            _engine->setVideoRotation(((old - 90) + 360) % 360);
            break;
        }

        case ActionFactory::ActionKind::OrderPlay: {
            _engine->playlist().setPlayMode(PlaylistModel::OrderPlay);
            break;
        }
        case ActionFactory::ActionKind::ShufflePlay: {
            _engine->playlist().setPlayMode(PlaylistModel::ShufflePlay);
            break;
        }
        case ActionFactory::ActionKind::SinglePlay: {
            _engine->playlist().setPlayMode(PlaylistModel::SinglePlay);
            break;
        }
        case ActionFactory::ActionKind::SingleLoop: {
            _engine->playlist().setPlayMode(PlaylistModel::SingleLoop);
            break;
        }
        case ActionFactory::ActionKind::ListLoop: {
            _engine->playlist().setPlayMode(PlaylistModel::ListLoop);
            break;
        }

        case ActionFactory::ActionKind::Stereo: {
            _engine->changeSoundMode(Backend::SoundMode::Stereo);
            break;
        }
        case ActionFactory::ActionKind::LeftChannel: {
            _engine->changeSoundMode(Backend::SoundMode::Left);
            break;
        }
        case ActionFactory::ActionKind::RightChannel: {
            _engine->changeSoundMode(Backend::SoundMode::Right);
            break;
        }

        case ActionFactory::ActionKind::DefaultFrame: {
            _engine->setVideoAspect(-1.0);
            break;
        }
        case ActionFactory::ActionKind::Ratio4x3Frame: {
            _engine->setVideoAspect(4.0 / 3.0);
            break;
        }
        case ActionFactory::ActionKind::Ratio16x9Frame: {
            _engine->setVideoAspect(16.0 / 9.0);
            break;
        }
        case ActionFactory::ActionKind::Ratio16x10Frame: {
            _engine->setVideoAspect(16.0 / 10.0);
            break;
        }
        case ActionFactory::ActionKind::Ratio185x1Frame: {
            _engine->setVideoAspect(1.85);
            break;
        }
        case ActionFactory::ActionKind::Ratio235x1Frame: {
            _engine->setVideoAspect(2.35);
            break;
        }

        case ActionFactory::ActionKind::ToggleMute: {
            _engine->toggleMute();
            break;
        }

        case ActionFactory::ActionKind::VolumeUp: {
            _engine->volumeUp();
            break;
        }

        case ActionFactory::ActionKind::VolumeDown: {
            _engine->volumeDown();
            break;
        }

        case ActionFactory::ActionKind::GotoPlaylistNext: {
            _engine->next();
            break;
        }

        case ActionFactory::ActionKind::GotoPlaylistPrev: {
            _engine->prev();
            break;
        }

        case ActionFactory::ActionKind::SelectTrack: {
            Q_ASSERT(args.size() == 1);
            _engine->selectTrack(args[0].toInt());
            break;
        }

        case ActionFactory::ActionKind::SelectSubtitle: {
            Q_ASSERT(args.size() == 1);
            _engine->selectSubtitle(args[0].toInt());
            break;
        }

        case ActionFactory::ActionKind::HideSubtitle: {
            _engine->toggleSubtitle();
            break;
        }

        case ActionFactory::ActionKind::LoadSubtitle: {
            QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Subtitle (*.ass *.aqt *.jss *.gsub *.ssf *.srt *.sub *.ssa *.usf *.idx)"));
            if (QFileInfo(filename).exists()) {
                _engine->loadSubtitle(QFileInfo(filename));
            }
            break;
            break;
        }

        case ActionFactory::ActionKind::TogglePause: {
            _engine->pauseResume();
            break;
        }

        case ActionFactory::ActionKind::SeekBackward: {
            _engine->seekBackward(20);
            break;
        }

        case ActionFactory::ActionKind::SeekForward: {
            _engine->seekForward(20);
            break;
        }

        case ActionFactory::ActionKind::Settings: {
            handleSettings();
            break;
        }

        case ActionFactory::ActionKind::Screenshot: {
            auto img = _engine->takeScreenshot();
            QString savePath = Settings::get().settings()->value("base.screenshot.location").toString();
            if (!QFileInfo(savePath).exists()) {
                savePath = "/tmp";
            }

            QString filePath = QString("%1/deepin-movie-shot %2.jpg")
                .arg(savePath).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            bool success = false;
            if (img.isNull()) 
                qDebug()<< __func__ << "pixmap is null";
            else
                success = img.save(filePath);

#ifdef USE_SYSTEM_NOTIFY
            // Popup notify.
            QDBusInterface notification("org.freedesktop.Notifications",
                    "/org/freedesktop/Notifications",
                    "org.freedesktop.Notifications",
                    QDBusConnection::sessionBus());

            QStringList actions;
            actions << "_open" << tr("View");


            QVariantMap hints;
            hints["x-deepin-action-_open"] = QString("xdg-open,%1").arg(filePath);


            QList<QVariant> arg;
            arg << (QCoreApplication::applicationName())                 // appname
                << ((unsigned int) 0)                                    // id
                << QString("deepin-movie")                               // icon
                << tr("Movie Screenshot")                                // summary
                << QString("%1 %2").arg(tr("Saved to")).arg(filePath) // body
                << actions                                               // actions
                << hints                                                 // hints
                << (int) -1;                                             // timeout
            notification.callWithArgumentList(QDBus::AutoDetect, "Notify", arg);

#else

            NotificationWidget *nw = new NotificationWidget(this); 
            auto msg = QString("%1 %2").arg(tr("Saved to")).arg(filePath);
            nw->popup(msg, success);
#endif
            break;
        }

        case ActionFactory::ActionKind::BurstScreenshot: {
            BurstScreenshotsDialog bsd(_engine);
            bsd.exec();
            qDebug() << "BurstScreenshot done";
            _engine->pauseResume();
            break;
        }

        default:
            break;
    }
}

void MainWindow::handleSettings()
{
    DSettingsDialog dsd(this);
    dsd.updateSettings(Settings::get().settings());
    dsd.exec();
}

void MainWindow::play(const QUrl& url)
{
    if (!url.isValid()) 
        return;

    _engine->addPlayFile(url);
    _engine->play();
}

void MainWindow::updateProxyGeometry()
{
    if (_handle) {
        _cachedMargins = _handle->frameMargins();
    }

#ifndef USE_DXCB
    {
        QPixmap shape(size());
        shape.fill(Qt::transparent);

        QPainter p(&shape);
        QPainterPath pp;
        pp.addRoundedRect(QRect(QPoint(), size()), 4, 4);
        p.fillPath(pp, QBrush(Qt::white));
        p.end();

        setMask(shape.mask());
    }
#endif

    auto tl = QPoint();
    _engine->setGeometry(QRect(tl, size()));

    if (!_miniMode) {
        if (_titlebar) {
            QSize sz(size().width(), _titlebar->height());
            _titlebar->setGeometry(QRect(tl, sz));
        }

        if (_toolbox) {
            QRect r(0, size().height() - 50, size().width(), 50);
            _toolbox->setGeometry(r);
        }

        if (_playlist) {
            QRect r(size().width() - _playlist->width(), _titlebar->geometry().bottom(),
                    _playlist->width(), _toolbox->geometry().top() - _titlebar->geometry().bottom());
            _playlist->setGeometry(r);
        }

        if (_playState) {
            auto r = QRect(QPoint(0, 0), QSize(128, 128));
            r.moveCenter(rect().center());
            _playState->move(r.topLeft());
        }
    }

#if 0
    qDebug() << "margins " << frameMargins();
    qDebug() << "window frame " << frameGeometry();
    qDebug() << "window geom " << geometry();
    qDebug() << "_engine " << _engine->geometry();
    qDebug() << "_titlebar " << _titlebar->geometry();
    qDebug() << "_toolbox " << _toolbox->geometry();
#endif
}

void MainWindow::suspendToolsWindow()
{
    if (!_miniMode && !this->frameGeometry().contains(QCursor::pos())) {
        _titlebar->hide();
        _toolbox->hide();
    }
}

void MainWindow::resumeToolsWindow()
{
    if (!_miniMode) {
        _titlebar->show();
        _toolbox->show();
    }
}

QMargins MainWindow::frameMargins() const
{
    return _cachedMargins;
}

void MainWindow::showEvent(QShowEvent *event)
{
    qDebug() << __func__;

    _titlebar->raise();
    _toolbox->raise();
    if (_titlebar) {
        resumeToolsWindow();
        QTimer::singleShot(4000, this, &MainWindow::suspendToolsWindow);
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    auto m = size();

    if (_miniMode) {
        m = QSize(0, 0);
    } else {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto sz = _engine->videoSize();
            qreal ratio = (qreal)sz.width() / sz.height();
            int h = 528 / ratio;
            if (size().width() > size().height()) {
                m = QSize(528, h);
            } else {
                m = QSize(h, 528);
            }
        } else {
            m = QSize(528, 0);
        }
    }

    qDebug() << __func__ << m;
    this->setMinimumSize(m);
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__ << geometry();
    updateSizeConstraints();
    updateProxyGeometry();
}

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
#ifndef USE_DXCB
    Utility::startWindowSystemMove(this->winId());
#endif
    QWidget::mouseMoveEvent(ev);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    if (!_miniMode)
        ActionFactory::get().mainContextMenu()->popup(cme->globalPos());
}

void MainWindow::paintEvent(QPaintEvent* pe)
{
    QPainter p(this);
    static QImage bg_dark(":/resources/icons/dark/init-splash.png");
    static QImage bg_light(":/resources/icons/light/init-splash.png");

    QImage bg = bg_dark;
    if ("light" == qApp->theme()) {
        bg = bg_light;
    }
    auto pt = rect().center() - QPoint(bg.width()/2, bg.height()/2);
    p.drawImage(pt, bg);
}

void MainWindow::toggleUIMode()
{
    _miniMode = !_miniMode;
    qDebug() << __func__ << _miniMode;

    _listener->setEnabled(!_miniMode);

    updateSizeConstraints();

    _titlebar->setVisible(!_miniMode);
    _toolbox->setVisible(!_miniMode);


    _miniPlayBtn->setVisible(_miniMode);
    _miniCloseBtn->setVisible(_miniMode);
    _miniQuitMiniBtn->setVisible(_miniMode);

    _miniPlayBtn->setEnabled(_miniMode);
    _miniCloseBtn->setEnabled(_miniMode);
    _miniQuitMiniBtn->setEnabled(_miniMode);

    if (_miniMode) {
        _lastSizeInNormalMode = size();
        auto sz = QSize(380, 380);
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto vid_size = _engine->videoSize();
            qreal ratio = vid_size.width() / (qreal)vid_size.height();

            if (vid_size.width() > vid_size.height()) {
                sz = QSize(380, 380 / ratio);
            } else {
                sz = QSize(380 * ratio, 380);
            }
        }
        resize(sz);
        _miniQuitMiniBtn->move(sz.width() - 14 - _miniQuitMiniBtn->width(),
                sz.height() - 10 - _miniQuitMiniBtn->height()); 
        _miniCloseBtn->move(sz.width() - 4 - _miniCloseBtn->width(), 4);
        _miniPlayBtn->move(14, sz.height() - 10 - _miniPlayBtn->height()); 

    } else {
        resize(_lastSizeInNormalMode);
    }
}

void MainWindow::miniButtonClicked(QString id)
{
    qDebug() << id;
    if (id == "play") {
        requestAction(ActionFactory::ActionKind::TogglePause);

    } else if (id == "close") {
        close();

    } else if (id == "quit_mini") {
        requestAction(ActionFactory::ActionKind::ToggleMiniMode);
    }
}

#include "mainwindow.moc"
