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
#include "movie_progress_indicator.h"
#include "options.h"
#include "titlebar.h"
#include "utils.h"

#include <QtWidgets>
#include <QtDBus>
#include <dlabel.h>
#include <DApplication>
#include <DTitlebar>
#include <dsettingsdialog.h>
#include <dthememanager.h>
#include <daboutdialog.h>
#include <dinputdialog.h>
#include <dimagebutton.h>
#include <DWidgetUtil>
#include <DSettingsWidgetFactory>
#include <dlineedit.h>

#define AUTOHIDE_TIMEOUT 2000

DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 6

static void workaround_updateStyle(QWidget *parent, const QString &theme)
{
    parent->setStyle(QStyleFactory::create(theme));
    for (auto obj : parent->children()) {
        auto w = qobject_cast<QWidget *>(obj);
        if (w) {
            workaround_updateStyle(w, theme);
        }
    }
}


static QWidget *createSelectableLineEditOptionHandle(QObject *opt)
{
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(opt);

    auto le = new DLineEdit();
    le->setFixedHeight(24);
    le->setObjectName("OptionSelectableLineEdit");
    le->setText(option->value().toString());
    le->setMaxLength(255);

    le->setIconVisible(true);
    le->setNormalIcon(":resources/icons/select-normal.svg");
    le->setHoverIcon(":resources/icons/select-hover.svg");
    le->setPressIcon(":resources/icons/select-press.svg");

    auto optionWidget = DSettingsWidgetFactory::createTwoColumWidget(option, le);
    workaround_updateStyle(optionWidget, "dlight");

    auto validate = [=](QString name, bool alert = true) -> bool {
        name = name.trimmed();
        if (name.isEmpty()) return false;

        if (name.size() && name[0] == '~') {
            name.replace(0, 1, QDir::homePath());
        }

        QFileInfo fi(name);
        if (fi.exists()) {
            if (!fi.isDir()) {
                if (alert) le->showAlertMessage(QObject::tr("Invalid folder"));
                return false;
            }

            if (!fi.isReadable() || !fi.isWritable()) {
                if (alert) le->showAlertMessage(QObject::tr("You don't have permission to operate this folder"));
                return false;
            }
        }

        return true;
    };

    option->connect(le, &DLineEdit::iconClicked, [=]() {
        QString name = QFileDialog::getExistingDirectory(0, QObject::tr("Open Folder"),
                QDir::currentPath(),
                QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (validate(name, false)) {
            option->setValue(name);
        }
    });

    option->connect(le, &QLineEdit::editingFinished, option, [=]() {
        if (validate(le->text(), false)) {
            option->setValue(le->text());
        } else {
            option->setValue(option->defaultValue());
            le->setText(option->defaultValue().toString());
        }
    });

    option->connect(le, &DLineEdit::textEdited, option, [=](const QString& newStr) {
        validate(newStr);
    });

    option->connect(option, &DTK_CORE_NAMESPACE::DSettingsOption::valueChanged, le, 
        [ = ](const QVariant & value) {
            le->setText(value.toString());
            le->update();
        });

    return  optionWidget;
}

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
            if (!window) return false;

            switch ((int)event->type()) {
            case QEvent::MouseButtonPress: {
                if (!enabled) return false;
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                setLeftButtonPressed(true);
                auto mw = static_cast<MainWindow*>(parent());
                if (mw->insideResizeArea(e->globalPos()) && lastCornerEdge != Utility::NoneEdge) 
                    startResizing = true;

                mw->capturedMousePressEvent(e);
                if (startResizing) {
                    return true;
                }
                break;
            }
            case QEvent::MouseButtonRelease: {
                if (!enabled) return false;
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                setLeftButtonPressed(false);
                qApp->setOverrideCursor(window->cursor());

                auto mw = static_cast<MainWindow*>(parent());
                mw->capturedMouseReleaseEvent(e);
                if (startResizing) {
                    startResizing = false;
                    return true;
                }
                startResizing = false;
                break;
            }
            case QEvent::MouseMove: {
                QMouseEvent *e = static_cast<QMouseEvent*>(event);
                auto mw = static_cast<MainWindow*>(parent());
                mw->resumeToolsWindow();

                if (!enabled) return false;
                const QRect window_visible_rect = _window->frameGeometry() - mw->dragMargins();

                if (!leftButtonPressed) {
                    if (mw->insideResizeArea(e->globalPos())) {
                        Utility::CornerEdge mouseCorner = Utility::NoneEdge;
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

                        goto skip_set_cursor; // disable edges

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

skip_set_cursor:
                        lastCornerEdge = mouseCorner = Utility::NoneEdge;
                        return false;
                    } else {
                        qApp->setOverrideCursor(window->cursor());
                    }
                } else {
                    if (startResizing) {
                        updateGeometry(lastCornerEdge, e);
                        return true;
                    }
                }
                
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

        void updateGeometry(Utility::CornerEdge edge, QMouseEvent* e) {
            auto mw = static_cast<MainWindow*>(parent());
            bool keep_ratio = mw->engine()->state() != PlayerEngine::CoreState::Idle;
            auto old_geom = mw->frameGeometry();
            auto geom = mw->frameGeometry();
            qreal ratio = (qreal)geom.width() / geom.height();

            // disable edges
            switch (edge) {
                case Utility::BottomEdge:
                case Utility::TopEdge:
                case Utility::LeftEdge:
                case Utility::RightEdge:
                case Utility::NoneEdge:
                    return;
                default: break;
            }

            if (keep_ratio) {
                auto sz = mw->engine()->videoSize();
                if (sz.isEmpty()) {
                    const auto& mi = mw->engine()->playlist().currentInfo().mi;
                    sz = QSize(mi.width, mi.height);
                }

                ratio = sz.width() / (qreal)sz.height();
                switch (edge) {
                    case Utility::TopLeftCorner:
                        geom.setLeft(e->globalX());
                        geom.setTop(geom.bottom() - geom.width() / ratio);
                        break;
                    case Utility::BottomLeftCorner:
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
                    default: break;
                }
            } else {
                switch (edge) {
                    case Utility::BottomLeftCorner:
                        geom.setBottomLeft(e->globalPos());
                        break;
                    case Utility::TopLeftCorner:
                        geom.setTopLeft(e->globalPos());
                        break;
                    case Utility::LeftEdge:
                        geom.setLeft(e->globalX());
                        break;
                    case Utility::BottomRightCorner:
                        geom.setBottomRight(e->globalPos());
                        break;
                    case Utility::RightEdge:
                        geom.setRight(e->globalX());
                        break;
                    case Utility::TopRightCorner:
                        geom.setTopRight(e->globalPos());
                        break;
                    case Utility::TopEdge:
                        geom.setTop(e->globalY());
                        break;
                    case Utility::BottomEdge:
                        geom.setBottom(e->globalY());
                        break;
                    default: break;
                }
            }

            auto min = mw->minimumSize();
            if (old_geom.width() <= min.width() && geom.left() > old_geom.left()) {
                geom.setLeft(old_geom.left());
            }
            if (old_geom.height() <= min.height() && geom.top() > old_geom.top()) {
                geom.setTop(old_geom.top());
            }

            geom.setWidth(qMax(geom.width(), min.width()));
            geom.setHeight(qMax(geom.height(), min.height()));
            mw->setGeometry(geom);

        }

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
    : QFrame(NULL)
{
    bool composited = CompositingManager::get().composited();
    setWindowFlags(Qt::FramelessWindowHint);
    setAcceptDrops(true);

    if (composited) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, false);
    }

    DThemeManager::instance()->registerWidget(this);
    setFrameShape(QFrame::NoFrame);
    
#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform() && composited) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        setAttribute(Qt::WA_TranslucentBackground, true);
        _handle->setTranslucentBackground(true);
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

    setContentsMargins(0, 0, 0, 0);

    _titlebar = new Titlebar(this);
    _titlebar->move(1, 1);
    _titlebar->setFixedHeight(30);
    _titlebar->layout()->setContentsMargins(0, 0, 6, 0);
    _titlebar->setFocusPolicy(Qt::NoFocus);
    if (!composited) {
        _titlebar->setAttribute(Qt::WA_NativeWindow);
        _titlebar->winId();
    }
    _titlebar->setMenu(ActionFactory::get().titlebarMenu());
    {
        auto dpr = qApp->devicePixelRatio();
        int w2 = 24 * dpr;
        int w = 16 * dpr;
        //hack: titlebar fixed icon size to (24x24), but we need (16x16)
        auto logo = QPixmap(":/resources/icons/logo.svg")
            .scaled(w, w, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        logo.setDevicePixelRatio(dpr);
        QPixmap pm(w2, w2);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.drawPixmap((w2-w)/2, (w2-w)/2, logo);
        p.end();
        _titlebar->setIcon(pm);
        _titlebar->setTitle(tr("Deepin Movie"));
    }
    {
        auto l = _titlebar->findChildren<DLabel*>();
        for (auto w: l) {
            w->setStyleSheet("font-size: 12px;");
        }
    }

    {
        auto help = new QShortcut(QKeySequence(Qt::Key_F1), this);
        help->setContext(Qt::ApplicationShortcut);
        connect(help, &QShortcut::activated, this, &MainWindow::handleHelpAction);
    }


    _engine = new PlayerEngine(this);
    _engine->move(1, 1);

    int volume = Settings::get().internalOption("global_volume").toInt();
    _engine->changeVolume(volume);

    _toolbox = new ToolboxProxy(this, _engine);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    connect(_engine, &PlayerEngine::stateChanged, [=]() {
        setInit(_engine->state() != PlayerEngine::Idle);
        resumeToolsWindow();
        updateWindowTitle();
    });

    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().mainContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);
    connect(qApp, &QGuiApplication::focusWindowChanged, [=]() {
        if (qApp->focusWindow() != windowHandle())
            suspendToolsWindow();
        else 
            resumeToolsWindow();
    });

    _playlist = new PlaylistWidget(this, _engine);
    _playlist->hide();

    _playState = new QLabel(this);
    _playState->setFixedSize(128, 128);
    _playState->setVisible(false);

    _progIndicator = new MovieProgressIndicator(this);
    _progIndicator->setVisible(false);
    connect(windowHandle(), &QWindow::windowStateChanged, [=]() {
        qDebug() << windowState();
        Qt::WindowFlags hint = Qt::WindowCloseButtonHint | Qt::WindowTitleHint |
            Qt::WindowSystemMenuHint;
        if (!isFullScreen()) {
            hint |= Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint;
            qApp->restoreOverrideCursor();
            if (_lastCookie > 0) {
                utils::UnInhibitStandby(_lastCookie);
                qDebug() << "uninhibit cookie" << _lastCookie;
                _lastCookie = 0;
            }
            _listener->setEnabled(!isMaximized() && !_miniMode);
        } else {
            qApp->setOverrideCursor(Qt::BlankCursor);

            if (_lastCookie > 0) {
                utils::UnInhibitStandby(_lastCookie);
                qDebug() << "uninhibit cookie" << _lastCookie;
                _lastCookie = 0;
            }
            _lastCookie = utils::InhibitStandby();
            qDebug() << "inhibit cookie" << _lastCookie;
            _listener->setEnabled(false);
        }
        _titlebar->setWindowFlags(hint);
        //WTF: this->geometry() is not size of fullscreen !
        //_progIndicator->move(geometry().width() - _progIndicator->width() - 18, 14);
        _progIndicator->setVisible(isFullScreen());
        toggleShapeMask();

        if (isFullScreen()) {
            _titlebar->move(0, 0);
            _engine->move(0, 0);
        } else {
            _titlebar->move(1, 1);
            _engine->move(1, 1);
        }

        if (!isFullScreen() && !isMaximized()) {
            if (_movieSwitchedInFsOrMaxed && !_hasPendingResizeByConstraint) {
                setMinimumSize({0, 0});
                resizeByConstraints(true);
            }
            _movieSwitchedInFsOrMaxed = false;
        }
        update();
    });
    connect(_engine, &PlayerEngine::elapsedChanged, [=]() {
        _progIndicator->updateMovieProgress(_engine->duration(), _engine->elapsed());
    });

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

    reflectActionToUI(ActionFactory::DefaultFrame);
    reflectActionToUI(ActionFactory::OrderPlay);
    reflectActionToUI(ActionFactory::Stereo);
    requestAction(ActionFactory::ChangeSubCodepage, false, {"auto"});

    _lightTheme = Settings::get().internalOption("light_theme").toBool();
    if (_lightTheme) reflectActionToUI(ActionFactory::LightTheme);
    prepareSplashImages();

    connect(_engine, &PlayerEngine::sidChanged, [=]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    });
    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    connect(_engine, &PlayerEngine::aidChanged, [=]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    });
    connect(_engine, &PlayerEngine::subCodepageChanged, [=]() {
        reflectActionToUI(ActionFactory::ActionKind::ChangeSubCodepage);
    });

    connect(_engine, &PlayerEngine::fileLoaded, [=]() {
        this->resizeByConstraints();
    });
    connect(_engine, &PlayerEngine::videoSizeChanged, [=]() {
        this->resizeByConstraints();
    });

    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::updatePlayState);
    updatePlayState();

    connect(_engine, &PlayerEngine::loadOnlineSubtitlesFinished,
        [this](const QUrl& url, bool success) {
            _nwComm->updateWithMessage(success?tr("Load successfully"):tr("Load failed"));
        });

    connect(DThemeManager::instance(), &DThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
    onThemeChanged();

    connect(&_autoHideTimer, &QTimer::timeout, this, &MainWindow::suspendToolsWindow);
    _autoHideTimer.setSingleShot(true);
    connect(&_delayedMouseReleaseTimer, &QTimer::timeout, this, &MainWindow::delayedMouseReleaseHandler);
    _delayedMouseReleaseTimer.setSingleShot(true);

    _nwComm = new NotificationWidget(this); 
    _nwComm->setFixedHeight(30);
    _nwComm->setAnchor(NotificationWidget::AnchorNorthWest);
    _nwComm->setAnchorPoint(QPoint(30, 38));
    _nwComm->hide();

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

void MainWindow::handleHelpAction()
{
    QString appid = qApp->applicationName();
#ifdef DTK_DMAN_PORTAL
    if (!qgetenv("FLATPAK_APPID").isEmpty()) {
        appid = qgetenv("FLATPAK_APPID");
    }

    QDBusInterface dmanInterface("com.deepin.dman",
                                 "/com/deepin/dman",
                                 "com.deepin.dman");
    if (dmanInterface.isValid()) {
        auto reply = dmanInterface.call("ShowManual", appid);
        if (dmanInterface.lastError().isValid()) {
            qCritical() << "failed call ShowManual" << appid << dmanInterface.lastError();
        }
    } else {
        qCritical() << "can not create dman dbus interface";
    }
#else
    QProcess::startDetached("dman", QStringList() << appid);
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

    auto theme = qApp->theme();
    auto pm = utils::LoadHiDPIPixmap(QString(":/resources/icons/%1/normal/play-big.svg")
            .arg(qApp->theme()));
    _playState->setPixmap(pm);
}

void MainWindow::updatePlayState()
{
    if (_miniMode) {
        _playState->setVisible(false);
        return;
    }

    if (!_inBurstShootMode && _engine->state() == PlayerEngine::CoreState::Paused) {
        auto r = QRect(QPoint(0, 0), QSize(128, 128));
        r.moveCenter(rect().center());
        _playState->move(r.topLeft());

        _playState->setVisible(true);
        _playState->raise();
    } else {
        if (_playState->isVisible()) {
            _playState->setVisible(false);
        }
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
        });
    }
}

void MainWindow::updateActionsState()
{
    auto pmf = _engine->playingMovieInfo();
    auto update = [=](QAction* act) {
        auto kd = ActionFactory::actionKind(act);
        bool v = true;
        switch(kd) {
            case ActionFactory::ActionKind::Screenshot:
            case ActionFactory::ActionKind::MatchOnlineSubtitle:
            case ActionFactory::ActionKind::BurstScreenshot:
            case ActionFactory::ActionKind::ToggleMiniMode:
            case ActionFactory::ActionKind::WindowAbove:
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

    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    //so we need to workaround it.
    reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
}

void MainWindow::reflectActionToUI(ActionFactory::ActionKind kd)
{
    QList<QAction*> acts;
    switch(kd) {
        case ActionFactory::ActionKind::WindowAbove:
        case ActionFactory::ActionKind::ToggleFullscreen:
        case ActionFactory::ActionKind::LightTheme:
        case ActionFactory::ActionKind::ToggleMiniMode:
        case ActionFactory::ActionKind::TogglePlaylist:
        case ActionFactory::ActionKind::HideSubtitle: {
            qDebug() << __func__ << kd;
            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            while (p != acts.end()) {
                auto old = (*p)->isEnabled();
                (*p)->setEnabled(false);
                if (kd == ActionFactory::TogglePlaylist) {
                    // here what we read is the last state of playlist 
                    (*p)->setChecked(_playlist->state() != PlaylistWidget::Opened);
                } else {
                    (*p)->setChecked(!(*p)->isChecked());
                }
                (*p)->setEnabled(old);
                ++p;
            }
            break;
        }

        case ActionFactory::ActionKind::ChangeSubCodepage: {
            auto cp = _engine->subCodepage();
            qDebug() << "codepage" << cp;
            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            while (p != acts.end()) {
                auto args = ActionFactory::actionArgs(*p);
                if (args[0].toString() == cp) {
                    (*p)->setEnabled(false);
                    if (!(*p)->isChecked()) (*p)->setChecked(true);
                    (*p)->setEnabled(true);
                    break;
                }

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

            qDebug() << __func__ << kd << "idx = " << idx;
            acts = ActionFactory::get().findActionsByKind(kd);
            auto p = acts.begin();
            while (p != acts.end()) {
                auto args = ActionFactory::actionArgs(*p);
                (*p)->setEnabled(false);
                if (args[0].toInt() == idx) {
                    if (!(*p)->isChecked()) (*p)->setChecked(true);
                } else {
                    (*p)->setChecked(false);
                }
                (*p)->setEnabled(true);

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
        default: break;
    }

}

void MainWindow::menuItemInvoked(QAction *action)
{
    auto kd = ActionFactory::actionKind(action);

    auto isShortcut = ActionFactory::isActionFromShortcut(action);
    if (ActionFactory::actionHasArgs(action)) {
        requestAction(kd, !isShortcut, ActionFactory::actionArgs(action), isShortcut);
    } else {
        requestAction(kd, !isShortcut, {}, isShortcut);
    }

    if (!isShortcut) {
        suspendToolsWindow();
    }
}

void MainWindow::switchTheme()
{
    _lightTheme = !_lightTheme;
    qApp->setTheme(_lightTheme? "light":"dark");
    Settings::get().setInternalOption("light_theme", _lightTheme);
}

bool MainWindow::isActionAllowed(ActionFactory::ActionKind kd, bool fromUI, bool isShortcut)
{
    if (_inBurstShootMode) {
        return false;
    }

    if (_miniMode) {
        if (fromUI || isShortcut) {
            switch (kd) {
                case ActionFactory::ToggleFullscreen:
                case ActionFactory::TogglePlaylist:
                case ActionFactory::BurstScreenshot:
                    return false;

                case ActionFactory::ToggleMiniMode:
                    return true;

                default: break;
            }
        }
    }

    if (isShortcut) {
        auto pmf = _engine->playingMovieInfo();
        bool v = true;
        switch(kd) {
            case ActionFactory::Screenshot:
            case ActionFactory::ToggleMiniMode:
            case ActionFactory::MatchOnlineSubtitle:
            case ActionFactory::BurstScreenshot:
                v = _engine->state() != PlayerEngine::Idle;
                break;

            case ActionFactory::MovieInfo:
                v = _engine->state() != PlayerEngine::Idle;
                if (v) {
                    v = v && _engine->playlist().count();
                    if (v) {
                        auto pif =_engine->playlist().currentInfo();
                        v = v && pif.loaded && pif.url.isLocalFile();
                    }
                }
                break;

            case ActionFactory::HideSubtitle:
            case ActionFactory::SelectSubtitle:
                v = pmf.subs.size() > 0;
            default: break;
        }
        if (!v) return v;
    }

    return true;
}

void MainWindow::requestAction(ActionFactory::ActionKind kd, bool fromUI,
        QList<QVariant> args, bool isShortcut)
{
    qDebug() << "kd = " << kd << "fromUI " << fromUI << (isShortcut ? "shortcut":"");

    if (!isActionAllowed(kd, fromUI, isShortcut)) {
        qDebug() << kd << "disallowed";
        return;
    }

    switch (kd) {
        case ActionFactory::ActionKind::Exit:
            qApp->quit(); 
            break;

        case ActionFactory::ActionKind::LightTheme:
            if (fromUI) switchTheme();
            break;

        case ActionFactory::ActionKind::OpenCdrom: {
            auto dev = dmr::CommandLineManager::get().dvdDevice();
            if (dev.isEmpty()) {
                dev = probeCdromDevice();
            }
            if (dev.isEmpty()) {
                _nwComm->updateWithMessage(tr("No device found"));
                break;
            }
            _engine->setDVDDevice(dev);
            //FIXME: how to tell if it's bluray
            QUrl url(QString("dvdread:///%1").arg(dev));
            //QUrl url(QString("dvdnav://"));
            play(url);
            break;
        }

        case ActionFactory::ActionKind::OpenUrl: {
            UrlDialog dlg;
            if (dlg.exec() == QDialog::Accepted) {
                auto url = dlg.url();
                if (url.isValid()) {
                    play(url);
                } else {
                    _nwComm->updateWithMessage(tr("Parse Failed"));
                }
            }
            break;
        }

        case ActionFactory::ActionKind::OpenDirectory: {
            QString name = QFileDialog::getExistingDirectory(this, tr("Open Folder"),
                    QDir::currentPath(),
                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

            QFileInfo fi(name);
            if (fi.isDir() && fi.exists()) {
                const auto& urls = _engine->addPlayDir(name);
                if (urls.size()) {
                    _engine->playByName(QUrl("playlist://0"));
                }
            }
            break;
        }

        case ActionFactory::ActionKind::OpenFileList: {
            QStringList filenames = QFileDialog::getOpenFileNames(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Movies (%1)").arg(_engine->video_filetypes.join(" ")));
            
            QList<QUrl> urls;
            if (filenames.size()) {
                for (const auto& filename: filenames) {
                    urls.append(QUrl::fromLocalFile(filename));
                }
                const auto& valids = _engine->addPlayFiles(urls);
                _engine->playByName(valids[0]);
            }
            break;
        }

        case ActionFactory::ActionKind::OpenFile: {
            QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Movies (%1)").arg(_engine->video_filetypes.join(" ")));
            if (QFileInfo(filename).exists()) {
                play(QUrl::fromLocalFile(filename));
            }
            break;
        }

        case ActionFactory::ActionKind::StartPlay: {
            if (_engine->playlist().count() == 0) {
                requestAction(ActionFactory::ActionKind::OpenFileList);
            } else {
                if (_engine->state() == PlayerEngine::CoreState::Idle) {
                    _engine->play();
                }
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
            this->resumeToolsWindow();
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
                MovieInfoDialog mid(_engine->playlist().currentInfo());
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

        case ActionFactory::ActionKind::QuitFullscreen: {
            if (isFullScreen()) {
                showNormal();
                if (!fromUI) {
                    reflectActionToUI(ActionFactory::ToggleFullscreen);
                }
            }
            break;
        }

        case ActionFactory::ActionKind::ToggleFullscreen: {
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

        case ActionFactory::ActionKind::PlaylistItemInfo: {
            _playlist->showItemInfo();
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
            if (_engine->muted()) {
                _nwComm->updateWithMessage(tr("Muted"));
            } else {
                double pert = _engine->volume();
                _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
            }
            break;
        }

        case ActionFactory::ActionKind::ChangeVolume: {
            if (_engine->muted()) {
                _engine->toggleMute();
            }
            _engine->changeVolume(args[0].toInt()); 
            Settings::get().setInternalOption("global_volume", qMin(_engine->volume(), 100));
            double pert = _engine->volume();
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
            break;
        }

        case ActionFactory::ActionKind::VolumeUp: {
            if (_engine->muted()) {
                _engine->toggleMute();
            }
            _engine->volumeUp();
            double pert = _engine->volume();
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
            break;
        }

        case ActionFactory::ActionKind::VolumeDown: {
            _engine->volumeDown();
            double pert = _engine->volume();
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
            break;
        }

        case ActionFactory::ActionKind::GotoPlaylistSelected: {
            _engine->playSelected(args[0].toInt());
            break;
        }

        case ActionFactory::ActionKind::GotoPlaylistNext: {
            if (isFullScreen() || isMaximized()) {
                _movieSwitchedInFsOrMaxed = true;
            }
            _engine->next();
            break;
        }

        case ActionFactory::ActionKind::GotoPlaylistPrev: {
            if (isFullScreen() || isMaximized()) {
                _movieSwitchedInFsOrMaxed = true;
            }
            _engine->prev();
            break;
        }

        case ActionFactory::ActionKind::SelectTrack: {
            Q_ASSERT(args.size() == 1);
            _engine->selectTrack(args[0].toInt());
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionFactory::ActionKind::MatchOnlineSubtitle: {
            _engine->loadOnlineSubtitle(_engine->playlist().currentInfo().url);
            break;
        }

        case ActionFactory::ActionKind::SelectSubtitle: {
            Q_ASSERT(args.size() == 1);
            _engine->selectSubtitle(args[0].toInt());
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionFactory::ActionKind::ChangeSubCodepage: {
            Q_ASSERT(args.size() == 1);
            _engine->setSubCodepage(args[0].toString());
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionFactory::ActionKind::HideSubtitle: {
            _engine->toggleSubtitle();
            break;
        }

        case ActionFactory::ActionKind::SubDelay: {
            _engine->setSubDelay(0.5);
            auto d = _engine->subDelay();
            _nwComm->updateWithMessage(tr("Subtitle %1: %2s")
                    .arg(d > 0.0?tr("delayed"):tr("advanced")).arg(d>0.0?d:-d));
            break;
        }

        case ActionFactory::ActionKind::SubForward: {
            _engine->setSubDelay(-0.5);
            auto d = _engine->subDelay();
            _nwComm->updateWithMessage(tr("Subtitle %1: %2s")
                    .arg(d > 0.0?tr("delayed"):tr("advanced")).arg(d>0.0?d:-d));
            break;
        }

        case ActionFactory::ActionKind::AccelPlayback: {
            _playSpeed = qMin(2.0, _playSpeed + 0.1);
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
            break;
        }

        case ActionFactory::ActionKind::DecelPlayback: {
            _playSpeed = qMax(0.1, _playSpeed - 0.1);
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
            break;
        }

        case ActionFactory::ActionKind::ResetPlayback: {
            _playSpeed = 1.0;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
            break;
        }

        case ActionFactory::ActionKind::LoadSubtitle: {
            QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Subtitle (*.ass *.aqt *.jss *.gsub *.ssf *.srt *.sub *.ssa *.usf *.idx)"));
            if (QFileInfo(filename).exists()) {
                auto success = _engine->loadSubtitle(QFileInfo(filename));
                _nwComm->updateWithMessage(success?tr("Load successfully"):tr("Load failed"));
            }
            break;
            break;
        }

        case ActionFactory::ActionKind::TogglePause: {
            if (_engine->state() == PlayerEngine::Idle && isShortcut) {
                requestAction(ActionFactory::StartPlay);
            } else {
                _engine->pauseResume();
            }
            break;
        }

        case ActionFactory::ActionKind::SeekBackward: {
            _engine->seekBackward(5);
            break;
        }

        case ActionFactory::ActionKind::SeekForward: {
            _engine->seekForward(5);
            break;
        }

        case ActionFactory::ActionKind::SeekAbsolute: {
            Q_ASSERT(args.size() == 1);
            _engine->seekAbsolute(args[0].toInt());
            break;
        }

        case ActionFactory::ActionKind::Settings: {
            handleSettings();
            break;
        }

        case ActionFactory::ActionKind::Screenshot: {
            auto img = _engine->takeScreenshot();

            QString filePath = Settings::get().screenshotNameTemplate();
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

            if (!_nwShot) {
                _nwShot = new NotificationWidget(this); 
                _nwShot->setAnchor(NotificationWidget::AnchorNorthWest);
                _nwShot->setAnchorPoint(QPoint(30, 38));
            }
            auto pm = utils::LoadHiDPIPixmap(QString(":/resources/icons/%1.svg").arg(success?"success":"fail"));
            auto msg = success?tr("The screenshot is saved"):tr("The screenshot is failed to save");
            _nwShot->popupWithIcon(msg, pm);
#endif
            break;
        }

        case ActionFactory::ActionKind::BurstScreenshot: {
            startBurstShooting();
            break;
        }

        case ActionFactory::ActionKind::ViewShortcut: {
            QRect rect = window()->geometry();
            QPoint pos(rect.x() + rect.width()/2 , rect.y() + rect.height()/2);
            QStringList shortcutString;
            QString param1 = "-j=" + ShortcutManager::get().toJson();
            QString param2 = "-p=" + QString::number(pos.x()) + "," + QString::number(pos.y());
            shortcutString << param1 << param2;

            QProcess* shortcutViewProcess = new QProcess();
            shortcutViewProcess->startDetached("deepin-shortcut-viewer", shortcutString);

            connect(shortcutViewProcess, SIGNAL(finished(int)),
            shortcutViewProcess, SLOT(deleteLater()));

            break;
        }

        default:
            break;
    }
}

void MainWindow::onBurstScreenshot(const QImage& frame, qint64 timestamp)
{
    qDebug() << _burstShoots.size();
    if (!frame.isNull()) {
        _burstShoots.append(qMakePair(frame, timestamp));
    }

    if (_burstShoots.size() >= 15 || frame.isNull()) {
        disconnect(_engine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
        _engine->stopBurstScreenshot();
        _inBurstShootMode = false;
        _toolbox->setEnabled(true);
        _listener->setEnabled(!_miniMode);

        if (frame.isNull()) {
            _burstShoots.clear();
            if (!_pausedBeforeBurst)
                _engine->pauseResume();
            return;
        }

        BurstScreenshotsDialog bsd(_engine->playlist().currentInfo());
        bsd.updateWithFrames(_burstShoots);
        auto ret = bsd.exec();
        qDebug() << "BurstScreenshot done";

        _burstShoots.clear();
        if (!_pausedBeforeBurst)
            _engine->pauseResume();

        if (ret == QDialog::Accepted) {
            auto poster_path = bsd.savedPosterPath();
            if (!_nwShot) {
                _nwShot = new NotificationWidget(this); 
                _nwShot->setAnchor(NotificationWidget::AnchorNorthWest);
                _nwShot->setAnchorPoint(QPoint(30, 38));
            }
            auto pm = utils::LoadHiDPIPixmap(QString(":/resources/icons/%1.svg").arg(QFileInfo::exists(poster_path)?"success":"fail"));
            _nwShot->popupWithIcon(tr("The screenshot is saved"), pm);
        }
    }
}

void MainWindow::startBurstShooting()
{
    _inBurstShootMode = true;
    _toolbox->setEnabled(false);
    _listener->setEnabled(false);

    _pausedBeforeBurst = _engine->paused();

    connect(_engine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
    _engine->burstScreenshot();
}

void MainWindow::handleSettings()
{
    auto dsd = new DSettingsDialog(this);
    dsd->widgetFactory()->registerWidget("selectableEdit", createSelectableLineEditOptionHandle);

    dsd->setProperty("_d_QSSThemename", "dark");
    dsd->setProperty("_d_QSSFilename", "DSettingsDialog");
    DThemeManager::instance()->registerWidget(dsd);
    
    dsd->updateSettings(Settings::get().settings());
    workaround_updateStyle(dsd, "dlight");

    //hack:
    auto subft = dsd->findChild<QSpinBox*>("OptionDSpinBox");
    if (subft) {
        subft->setMinimum(8);
    }

    // hack: reset is set to default by QDialog, which makes lineedit's enter
    // press is responsed by reset button
    auto reset = dsd->findChild<QPushButton*>("SettingsContentReset");
    reset->setDefault(false);
    reset->setAutoDefault(false);

    dsd->exec();
    delete dsd;
    Settings::get().settings()->sync();
}

void MainWindow::playList(const QList<QString>& l)
{
    QList<QUrl> urls;
    for (const auto& filename: l) {
        urls.append(QUrl::fromLocalFile(filename));
    }
    const auto& valids = _engine->addPlayFiles(urls);
    if (valids.size()) {
        if (!isHidden()) {
            activateWindow();
        }
        _engine->playByName(valids[0]);
    }
}

void MainWindow::play(const QUrl& url)
{
    if (!url.isValid()) 
        return;

    if (!isHidden()) {
        activateWindow();
    }

    if (!_engine->addPlayFile(url)) {
        auto msg = QString(tr("Invalid file: %1").arg(url.fileName()));
        _nwComm->updateWithMessage(msg);
        return;
    }
    _engine->playByName(url);
}

void MainWindow::toggleShapeMask()
{
    if (CompositingManager::get().composited()) {
        return;
    }

#ifndef USE_DXCB
    if (isFullScreen() || isMaximized()) {
        clearMask();
    } else {
        QPixmap shape(size());
        shape.setDevicePixelRatio(windowHandle()->devicePixelRatio());
        shape.fill(Qt::transparent);

        QPainter p(&shape);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath pp;
        pp.addRoundedRect(rect(), RADIUS, RADIUS);
        p.fillPath(pp, QBrush(Qt::white));
        p.end();

        setMask(shape.mask());
    }
#endif
}

void MainWindow::updateProxyGeometry()
{
    if (_handle) {
        _cachedMargins = _handle->frameMargins();
    }

    toggleShapeMask();

    // leave one pixel for border
    auto view_rect = rect().marginsRemoved(QMargins(1, 1, 1, 1));
    if (isFullScreen()) view_rect = rect();
    _engine->resize(view_rect.size());

    if (!_miniMode) {
        if (_titlebar) {
            _titlebar->setFixedWidth(view_rect.width());
        }

        if (_toolbox) {
            QRect r(1, height() - TOOLBOX_HEIGHT - 1, view_rect.width(), TOOLBOX_HEIGHT);
            if (isFullScreen()) {
                r.moveTopLeft({0, height() - TOOLBOX_HEIGHT});
            }
            _toolbox->setGeometry(r);
        }

        if (_playlist && !_playlist->toggling()) {
            QRect fixed(0, titlebar()->geometry().bottom(),
                    220,
                    toolbox()->geometry().top() - titlebar()->geometry().bottom());
            fixed.moveRight(view_rect.right());
            _playlist->setGeometry(fixed);
        }
    }

    if (_playState) {
        auto r = QRect(QPoint(0, 0), QSize(128, 128));
        r.moveCenter(rect().center());
        _playState->move(r.topLeft());
    }

#if 0
    qDebug() << "margins " << frameMargins();
    qDebug() << "window frame " << frameGeometry();
    qDebug() << "window geom " << geometry();
    qDebug() << "_engine " << _engine->geometry();
    qDebug() << "_titlebar " << _titlebar->geometry();
    qDebug() << "_playlist " << _playlist->geometry();
    qDebug() << "_toolbox " << _toolbox->geometry();
#endif
}

void MainWindow::suspendToolsWindow()
{
    if (!_miniMode) {
        if (_playlist && _playlist->state() == PlaylistWidget::Opened)
            return;

        if (qApp->applicationState() == Qt::ApplicationInactive) {

        } else {
            // menus  are popped up
            if (qApp->focusWindow() != windowHandle())
                return;

            if (insideToolsArea(mapFromGlobal(QCursor::pos())))
                return;
        }

        if (_toolbox->anyPopupShown())
            return;

        if (_engine->state() == PlayerEngine::Idle)
            return;

        if (_autoHideTimer.isActive())
            return;

        if (isFullScreen()) {
            qApp->setOverrideCursor(Qt::BlankCursor);
        }
        _titlebar->hide();
        _toolbox->hide();
    } else {
        if (_autoHideTimer.isActive())
            return;

        _miniPlayBtn->hide();
        _miniCloseBtn->hide();
        _miniQuitMiniBtn->hide();
    }
}

void MainWindow::resumeToolsWindow()
{
    if (qApp->applicationState() == Qt::ApplicationActive) {
        if (!frameGeometry().contains(QCursor::pos())) {
            goto _finish;
        }
    }

    qApp->restoreOverrideCursor();
    setCursor(Qt::ArrowCursor);

    if (!_miniMode) {
        _titlebar->show();
        _toolbox->show();
    } else {
        _miniPlayBtn->show();
        _miniCloseBtn->show();
        _miniQuitMiniBtn->show();
    }

_finish:
    _autoHideTimer.start(AUTOHIDE_TIMEOUT);
}

QMargins MainWindow::frameMargins() const
{
    return _cachedMargins;
}

void MainWindow::hideEvent(QHideEvent *event)
{
    if (Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_engine && _engine->state() == PlayerEngine::Playing) {
            _pausedOnHide = true;
            requestAction(ActionFactory::TogglePause);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    qDebug() << __func__;
    if (_lastCookie > 0) {
        utils::UnInhibitStandby(_lastCookie);
        qDebug() << "uninhibit cookie" << _lastCookie;
        _lastCookie = 0;
    }
    _engine->savePlaybackPosition();
    ev->accept();
}

void MainWindow::wheelEvent(QWheelEvent* we)
{
    if (insideToolsArea(we->pos()) || insideResizeArea(we->globalPos())) 
        return;

    if (_playlist->state() == PlaylistWidget::Opened) {
        we->ignore();
        return;
    }

    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        requestAction(we->angleDelta().y() > 0 ? ActionFactory::VolumeUp: ActionFactory::VolumeDown);
    }
}

void MainWindow::focusInEvent(QFocusEvent *fe)
{
    resumeToolsWindow();
}

void MainWindow::showEvent(QShowEvent *event)
{
    qDebug() << __func__;
    if (_pausedOnHide || Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_pausedOnHide && _engine && _engine->state() != PlayerEngine::Playing) {
            requestAction(ActionFactory::TogglePause);
            _pausedOnHide = false;
        }
    }

    _titlebar->raise();
    _toolbox->raise();
    resumeToolsWindow();
}

void MainWindow::resizeByConstraints(bool forceCentered) 
{
    if (_engine->state() == PlayerEngine::Idle || _engine->playlist().count() == 0) {
        _titlebar->setTitle(tr("Deepin Movie"));
        return;
    }

    if (_miniMode || isFullScreen() || isMaximized()) {
        //_lastSizeInNormalMode = QSize(-1, -1);
        return;
    }

    updateWindowTitle();

    const auto& mi = _engine->playlist().currentInfo().mi;
    auto sz = _engine->videoSize();
    if (sz.isEmpty()) {
        sz = QSize(mi.width, mi.height);
        qDebug() << mi.width << mi.height;
    }

    auto geom = qApp->desktop()->availableGeometry(this);
    if (sz.width() > geom.width() || sz.height() > geom.height()) {
        sz.scale(geom.width(), geom.height(), Qt::KeepAspectRatio);
    }

    qDebug() << sz;
    if (forceCentered) {
        QRect r;
        r.setSize(sz);
        r.moveTopLeft({(geom.width() - r.width()) /2, (geom.height() - r.height())/2});
        this->setGeometry(r);
    } else {
        QRect r = this->geometry();
        r.setSize(sz);
        this->setGeometry(r);
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    auto m = size();

    if (_miniMode) {
        m = QSize(40, 40);
    } else {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto sz = _engine->videoSize();
            qreal ratio = (qreal)sz.width() / sz.height();
            if (sz.width() > sz.height()) {
                int h = 528 / ratio;
                m = QSize(528, h);
            } else {
                int w = 528 * ratio;
                m = QSize(w, 528);
            }
        } else {
            m = QSize(630, 386);
        }
    }

    qDebug() << __func__ << m;
    this->setMinimumSize(m);
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__ << geometry();
    if (_mousePressed && !_mouseMoved) {
        auto msg = QString("%1x%2").arg(width()) .arg(height());
        _nwComm->updateWithMessage(msg);
    } else if (_mouseMoved) {
        //when in maximized state, drag to resize don't issue state change 
        //we need to change manually
        if (windowState() == Qt::WindowMaximized) {
            setWindowState(Qt::WindowNoState);
            //FIXME: I can not resize at this situation. it seems that
            //qt forbids me to do correct resizing while holding press.
            _hasPendingResizeByConstraint = true;
            return;
        }
    }
    
    if (isFullScreen()) {
        _progIndicator->move(geometry().width() - _progIndicator->width() - 18, 8);
    }

    updateSizeConstraints();
    updateProxyGeometry();
    QTimer::singleShot(0, [=]() { updateWindowTitle(); });
}

void MainWindow::updateWindowTitle()
{
    if (_engine->state() != PlayerEngine::Idle) {
        const auto& mi = _engine->playlist().currentInfo().mi;
        auto title = _titlebar->fontMetrics().elidedText(mi.title,
                Qt::ElideMiddle, _titlebar->contentsRect().width() - 300);
        _titlebar->setTitle(title);
    } else {
        _titlebar->setTitle(tr("Deepin Movie"));
    }
    _titlebar->setProperty("idle", _engine->state() == PlayerEngine::Idle);
    _titlebar->setStyleSheet(styleSheet());
}

void MainWindow::moveEvent(QMoveEvent *ev)
{
}

void MainWindow::keyPressEvent(QKeyEvent *ev)
{
    QWidget::keyPressEvent(ev);
}

void MainWindow::keyReleaseEvent(QKeyEvent *ev)
{
    QWidget::keyReleaseEvent(ev);
}

void MainWindow::capturedMousePressEvent(QMouseEvent* me)
{
    _mouseMoved = false;
    if (me->buttons() == Qt::LeftButton) {
        _mousePressed = true;
    }
}

void MainWindow::capturedMouseReleaseEvent(QMouseEvent* me)
{
    _mousePressed = false;
    if (_hasPendingResizeByConstraint) {
        QTimer::singleShot(100, [=]() {
            _movieSwitchedInFsOrMaxed = false;
            _hasPendingResizeByConstraint = false;
            setMinimumSize({0, 0});
            resizeByConstraints(false);
            update();
        });
    }
}

static bool _afterDblClick = false;
void MainWindow::mousePressEvent(QMouseEvent *ev)
{
    _mouseMoved = false;
    if (ev->buttons() == Qt::LeftButton) {
        _mousePressed = true;
    }
}


void MainWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!_miniMode && !_inBurstShootMode) {
        _delayedMouseReleaseTimer.stop();
        _mousePressed = false;
        if (_engine->state() == PlayerEngine::Idle) {
            requestAction(ActionFactory::StartPlay);
        } else {
            requestAction(ActionFactory::ToggleFullscreen, false, {}, true);
        }
        ev->accept();
        _afterDblClick = true;
    }
}

bool MainWindow::insideToolsArea(const QPoint& p)
{
    return _titlebar->geometry().contains(p) || _toolbox->geometry().contains(p);
}

QMargins MainWindow::dragMargins() const
{
    return QMargins {MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS};
}

bool MainWindow::insideResizeArea(const QPoint& global_p)
{
    const QRect window_visible_rect = frameGeometry() - dragMargins();
    return !window_visible_rect.contains(global_p);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    _mousePressed = false;
    // dtk has a bug, DImageButton propagates mouseReleaseEvent event when it responsed to.
    if (!insideResizeArea(ev->globalPos()) && !_mouseMoved && !insideToolsArea(ev->pos())) {
        if (_playlist->state() != PlaylistWidget::Opened)
            _delayedMouseReleaseTimer.start(120);
    } else if (_mouseMoved) {
        if (_hasPendingResizeByConstraint) {
            QTimer::singleShot(100, [=]() {
                _movieSwitchedInFsOrMaxed = false;
                _hasPendingResizeByConstraint = false;
                setMinimumSize({0, 0});
                resizeByConstraints(false);
                update();
            });
        }
    }

    _mouseMoved = false;
}

void MainWindow::delayedMouseReleaseHandler()
{
    if (!_afterDblClick)
        requestAction(ActionFactory::TogglePause, false, {}, true);
    _afterDblClick = false;
}

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
    _mouseMoved = true;
#ifndef USE_DXCB
    if (windowState() == Qt::WindowNoState) {
        Utility::startWindowSystemMove(this->winId());
    }
#endif
    QWidget::mouseMoveEvent(ev);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    if (_miniMode || _inBurstShootMode) 
        return;

    resumeToolsWindow();
    QTimer::singleShot(0, [=]() {
        qApp->restoreOverrideCursor();
        ActionFactory::get().mainContextMenu()->popup(QCursor::pos());
    });
    cme->accept();
}

void MainWindow::prepareSplashImages()
{
    bg_dark = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
    bg_light = utils::LoadHiDPIImage(":/resources/icons/light/init-splash.svg");
}

void MainWindow::paintEvent(QPaintEvent* pe)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    bool light = ("light" == qApp->theme());
    bool rounded = !isFullScreen() && !isMaximized();

    p.fillRect(rect(), Qt::transparent);

    auto bg_clr = QColor(16, 16, 16);
    QImage& bg = bg_dark;
    if (light) {
        bg = bg_light;
        bg_clr = QColor(252, 252, 252);
    }

    if (rounded) {
        QPainterPath pp;
        pp.addRoundedRect(rect(), RADIUS, RADIUS);
        p.fillPath(pp, QColor(0, 0, 0, light ? 255 * 0.1: 255));

        {
            /* we supposed to draw by qss background-color here, but it's conflict with 
             * border area (border has alpha, which blends with background-color.
             */
            auto view_rect = rect().marginsRemoved(QMargins(1, 1, 1, 1));
            QPainterPath pp;
            pp.addRoundedRect(view_rect, RADIUS, RADIUS);
            p.fillPath(pp, bg_clr);
        }
    } else {
        QPainterPath pp;
        pp.addRect(rect());
        p.fillPath(pp, bg_clr);
    }

    auto pt = rect().center() - QPoint(bg.width()/2, bg.height()/2)/devicePixelRatioF();
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

    updatePlayState();

    resumeToolsWindow();

    if (_miniMode) {
        _stateBeforeMiniMode = SBEM_None;

        if (!_windowAbove) {
            _stateBeforeMiniMode |= SBEM_Above;
            requestAction(ActionFactory::WindowAbove);
        }

        if (_playlist->state() == PlaylistWidget::Opened) {
            _stateBeforeMiniMode |= SBEM_PlaylistOpened;
            requestAction(ActionFactory::TogglePlaylist);
        }

        if (isFullScreen()) {
            _stateBeforeMiniMode |= SBEM_Fullscreen;
            requestAction(ActionFactory::QuitFullscreen);
        }

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
        if (_stateBeforeMiniMode & SBEM_Above) {
            requestAction(ActionFactory::WindowAbove);
        }
        if (_stateBeforeMiniMode & SBEM_Fullscreen) {
            requestAction(ActionFactory::ToggleFullscreen);
        } else {
            if (_lastSizeInNormalMode.isValid()) {
                resize(_lastSizeInNormalMode);
                //utils::MoveToCenter(this);
            } else {
                if (_engine->state() == PlayerEngine::CoreState::Idle) {
                    resize(850, 600);
                } else {
                    resizeByConstraints();
                }
            }
        }

        if (_stateBeforeMiniMode & SBEM_PlaylistOpened &&
                _playlist->state() == PlaylistWidget::Closed) {
            requestAction(ActionFactory::TogglePlaylist);
        }

        _stateBeforeMiniMode = SBEM_None;
    }
}

void MainWindow::miniButtonClicked(QString id)
{
    qDebug() << id;
    if (id == "play") {
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            requestAction(ActionFactory::ActionKind::TogglePause);
        }

    } else if (id == "close") {
        close();

    } else if (id == "quit_mini") {
        requestAction(ActionFactory::ActionKind::ToggleMiniMode);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *ev)
{
    qDebug() << ev->mimeData()->formats();
    if (!ev->mimeData()->hasUrls()) {
        return;
    }

    auto urls = ev->mimeData()->urls();
    auto valids = _engine->addPlayFiles(urls);

    {
        auto all = urls.toSet();
        auto accepted = valids.toSet();
        auto invalids = all.subtract(accepted).toList();
        int ms = 0;
        for (const auto& url: invalids) {
            QTimer::singleShot(ms, [=]() {
                auto msg = QString(tr("Invalid file: %1").arg(url.fileName()));
                _nwComm->updateWithMessage(msg);
            });

            ms += 1000;
        }
    }

    if (valids.size()) {
        if (valids.size() == 1) {
            _engine->playByName(valids[0]);
        } else {
            _engine->playByName(QUrl("playlist://0"));
        }
    }
    ev->acceptProposedAction();
}

void MainWindow::setInit(bool v)
{
    if (_inited != v) {
        _inited = v;
        emit initChanged();
    }
}

QString MainWindow::probeCdromDevice()
{
    QStringList cands = {
        "/dev/sr0",
        "/dev/cdrom"
    };

    for (auto d: cands) {
        if (QFile::exists(d)) {
            return d;
        }
    }

    return QString();
}

#include "mainwindow.moc"
