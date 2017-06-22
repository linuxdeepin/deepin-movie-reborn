#include "config.h"

#include "mainwindow.h"
#include "mpv_proxy.h"
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

#include <QtWidgets>
#include <QtDBus>
#include <DApplication>
#include <DTitlebar>
#include <dsettingsdialog.h>
#include <dthememanager.h>
#include <daboutdialog.h>

DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 8

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

    protected:
        enum State {
            Idle,
            Pressed,
            Resizing,
        };

        bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE {
            QWindow *window = qobject_cast<QWindow*>(obj);
            if (!window) return false;

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
            bool keep_ratio = mw->proxy()->state() != MpvProxy::CoreState::Idle;

            if (!keep_ratio) {
                return;
            }
            const auto& mi = mw->proxy()->playlist().currentInfo().mi;
            qreal ratio = mi.width / (qreal)mi.height;
            auto old_geom = mw->frameGeometry();
            auto geom = mw->frameGeometry();

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
    
    bool composited = CompositingManager::get().composited();
#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform() && composited) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        setAttribute(Qt::WA_TranslucentBackground, true);
        _handle->setTranslucentBackground(true);
        _cachedMargins = _handle->frameMargins();

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
    auto listener = new MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(listener);
    qDebug() << "event listener";
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

    _center = new QWidget(this);
    _center->move(0, 0);
    _proxy = new MpvProxy(_center);

    _toolbox = new ToolboxProxy(this, _proxy);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().mainContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);

    _playlist = new PlaylistWidget(this, _proxy);
    _playlist->hide();
    _playlist->setFixedWidth(220);

    _playState = new QLabel(this);

    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();

    //FIXME: fileLoaded may be issued for all items in playlist, not just current
    connect(_proxy, &MpvProxy::fileLoaded, [=]() {
        const auto& mi = _proxy->playlist().currentInfo().mi;
        _titlebar->setTitle(QFileInfo(mi.filePath).fileName());
        resize(mi.width, mi.height);
        updateSizeConstraints();
    });

    connect(_proxy, &MpvProxy::stateChanged, this, &MainWindow::updatePlayState);

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
#endif
}

#ifdef USE_DXCB
static QPoint last_proxy_pos;
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
            last_proxy_pos = windowHandle()->framePosition();
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
        windowHandle()->setFramePosition(last_proxy_pos + d);
    }
}

#endif

MainWindow::~MainWindow()
{
    //delete _evm;
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
    if (_proxy->state() != MpvProxy::CoreState::Idle) {
        qDebug() << __func__ << _proxy->state();
        QPixmap pm;
        if (_proxy->state() == MpvProxy::CoreState::Playing) {
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
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 2)
            auto kd = (ActionKind)act->property("kind").value<int>();
#else
            auto kd = act->property("kind").value<ActionKind>();
#endif
            reflectActionToUI(kd);
        });
    }
}

void MainWindow::reflectActionToUI(ActionKind kd)
{
    QList<QAction*> acts;
    switch(kd) {
        case ActionKind::WindowAbove:
        case ActionKind::Fullscreen:
        case ActionKind::ToggleMiniMode:
        case ActionKind::TogglePlaylist:
            qDebug() << __func__ << kd;
            acts = ActionFactory::get().findActionsByKind(kd);
        default: break;
    }

    auto p = acts.begin();
    while (p != acts.end()) {
        (*p)->setEnabled(false);
        (*p)->setChecked(!(*p)->isChecked());
        (*p)->setEnabled(true);
        ++p;
    }
}

void MainWindow::menuItemInvoked(QAction *action)
{
    auto prop = action->property("kind");
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 2)
    auto kd = (ActionKind)action->property("kind").value<int>();
#else
    auto kd = action->property("kind").value<ActionKind>();
#endif
    qDebug() << "prop = " << prop << ", kd = " << kd;
    //requestAction(kd, action->isChecked());
    requestAction(kd, true);
}

void MainWindow::requestAction(ActionKind kd, bool fromUI)
{
    switch (kd) {
        case ActionKind::Exit:
            qApp->quit(); 
            break;

        case ActionKind::LightTheme:
            _lightTheme = !_lightTheme;
            qApp->setTheme(_lightTheme? "light":"dark");
            break;

        case OpenFile: {
            QString filename = QFileDialog::getOpenFileName(this, tr("Open File"),
                    QDir::currentPath(),
                    tr("Movies (*.mkv *.mov *.mp4 *.rmvb)"));
            if (QFileInfo(filename).exists()) {
                play(QFileInfo(filename));
            }
            break;
        }

        case ActionKind::EmptyPlaylist: {
            _proxy->clearPlaylist();
            break;
        }

        case ActionKind::TogglePlaylist: {
            _playlist->togglePopup();
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionKind::ToggleMiniMode: {
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;
        }

        case ActionKind::MovieInfo: {
            if (_proxy->state() != MpvProxy::CoreState::Idle) {
                MovieInfoDialog mid(_proxy->playlist().currentInfo().mi);
                mid.exec();
            }
            break;
        }

        case ActionKind::WindowAbove:
            _windowAbove = !_windowAbove;
            Utility::setStayOnTop(this, _windowAbove);
            if (!fromUI) {
                reflectActionToUI(kd);
            }
            break;

        case Fullscreen: {
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

        case ActionKind::PlaylistOpenItemInFM: {
            _playlist->openItemInFM();
            break;
        }

        case ToggleMute: {
            _proxy->toggleMute();
            break;
        }

        case VolumeUp: {
            _proxy->volumeUp();
            break;
        }

        case VolumeDown: {
            _proxy->volumeDown();
            break;
        }

        case GotoPlaylistNext: {
            _proxy->next();
            break;
        }

        case GotoPlaylistPrev: {
            _proxy->prev();
            break;
        }

        case TogglePause: {
            _proxy->pauseResume();
            break;
        }

        case SeekBackward: {
            _proxy->seekBackward(20);
            break;
        }

        case SeekForward: {
            _proxy->seekForward(20);
            break;
        }

        case Settings: {
            handleSettings();
            break;
        }

        case Screenshot: {
            auto img = _proxy->takeScreenshot();
            QString savePath = Settings::get().settings()->value("base.screenshot.location").toString();
            if (!QFileInfo(savePath).exists()) {
                savePath = "/tmp";
            }

            QString filePath = QString("%1/deepin-movie-shot %2.jpg")
                .arg(savePath).arg(QDateTime::currentDateTime().toString(Qt::ISODate));
            img.save(filePath);

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
            break;
        }

        case BurstScreenshot: {
            BurstScreenshotsDialog bsd(_proxy);
            bsd.exec();
            qDebug() << "BurstScreenshot done";
            _proxy->pauseResume();
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

void MainWindow::play(const QFileInfo& fi)
{
    if (!fi.exists()) 
        return;

    _proxy->addPlayFile(fi);
    _proxy->play();
}

void MainWindow::updateProxyGeometry()
{
    if (_handle) {
        _cachedMargins = _handle->frameMargins();
    }

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

    _center->resize(size());

    auto tl = QPoint();

    if (_titlebar) {
        QSize sz(size().width(), _titlebar->height());
        _titlebar->setGeometry(QRect(tl, sz));
    }

    if (_proxy) {
        _proxy->setGeometry(QRect(tl, size()));
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

#ifdef DEBUG
    qDebug() << "margins " << frameMargins();
    qDebug() << "window frame " << frameGeometry();
    qDebug() << "window geom " << geometry();
    qDebug() << "_center " << _center->geometry();
    qDebug() << "_titlebar " << _titlebar->geometry();
    qDebug() << "proxy " << _proxy->geometry();
    qDebug() << "_toolbox " << _toolbox->geometry();
#endif
}

void MainWindow::suspendToolsWindow()
{
    _titlebar->hide();
    _toolbox->hide();
}

void MainWindow::resumeToolsWindow()
{
    _titlebar->show();
    _toolbox->show();
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
        //QTimer::singleShot(4000, this, &MainWindow::suspendToolsWindow);
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    if (_proxy->state() != MpvProxy::CoreState::Idle) {
        const auto& mi = _proxy->playlist().currentInfo().mi;
        qreal ratio = mi.width / (qreal)mi.height;
        int h = 528 / ratio;
        if (size().width() > size().height()) {
            setMinimumSize(QSize(528, h));
        } else {
            setMinimumSize(QSize(h, 528));
        }
    } else {
        setMinimumSize(QSize(528, 400));
    }
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    updateSizeConstraints();
    updateProxyGeometry();
}

void MainWindow::enterEvent(QEvent *ev)
{
    qDebug() << __func__;
    //resumeToolsWindow();
}

void MainWindow::leaveEvent(QEvent *ev)
{
    qDebug() << __func__;
    bool leave = true;

    //suspendToolsWindow();
}

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
    //qDebug() << __func__;
    Utility::startWindowSystemMove(this->winId());
    //QWidget::mouseMoveEvent(ev);
}

void MainWindow::mousePressEvent(QMouseEvent *ev) 
{
    //qDebug() << __func__;
    QWidget::mousePressEvent(ev);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *ev)
{
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    ActionFactory::get().mainContextMenu()->popup(cme->globalPos());
}

#include "mainwindow.moc"
