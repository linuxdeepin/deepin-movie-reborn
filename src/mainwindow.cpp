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

#include <QtWidgets>
#include <DApplication>
#include <DTitlebar>
#include <dsettingsdialog.h>

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
            cursorAnimation.setDuration(50);
            cursorAnimation.setEasingCurve(QEasingCurve::InExpo);

            connect(&cursorAnimation, &QVariantAnimation::valueChanged,
                    this, &MainWindowEventListener::onAnimationValueChanged);

            startAnimationTimer.setSingleShot(true);
            startAnimationTimer.setInterval(300);

            connect(&startAnimationTimer, &QTimer::timeout,
                    this, &MainWindowEventListener::startAnimation);
        }

        ~MainWindowEventListener()
        {
        }

    protected:
        bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE {
            QWindow *window = qobject_cast<QWindow*>(obj);

            if (!window)
                return false;

            const QRect &window_geometry = window->geometry();

            switch ((int)event->type()) {
            case QEvent::MouseMove:
            case QEvent::MouseButtonDblClick:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease: {
                QMouseEvent *e = static_cast<QMouseEvent*>(event);

                const QMargins margins(MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS);
                const QRect window_visible_rect = _window->frameGeometry() - margins;
                if (!leftButtonPressed && !window_visible_rect.contains(e->globalPos())) {
                    if (event->type() == QEvent::MouseMove) {
                        Utility::CornerEdge mouseCorner;
                        QRect cornerRect;
                        qDebug() << window_visible_rect << _window->frameGeometry()
                            << e->globalPos();

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
                            Utility::startWindowSystemResize(window->winId(), mouseCorner, e->globalPos());

                            cancelAdsorbCursor();
                        } else {
                            adsorbCursor(mouseCorner);
                        }
                    } else if (event->type() == QEvent::MouseButtonRelease) {
                        Utility::cancelWindowMoveResize(window->winId());
                    }

                    return true;
                }
skip_set_cursor:
                if (e->buttons() == Qt::LeftButton && e->type() == QEvent::MouseButtonPress)
                    setLeftButtonPressed(true);
                else
                    setLeftButtonPressed(false);
                qApp->setOverrideCursor(window->cursor());
                cancelAdsorbCursor();
                canAdsorbCursor = true;
                break;
            }
            case QEvent::Enter:
                 canAdsorbCursor = true;
                 break;
            case QEvent::Leave:
                 canAdsorbCursor = false;
                 cancelAdsorbCursor();
                 break;
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

        void adsorbCursor(Utility::CornerEdge cornerEdge)
        {
            lastCornerEdge = cornerEdge;

            if (!canAdsorbCursor)
                return;

            if (cursorAnimation.state() == QVariantAnimation::Running)
                return;

            startAnimationTimer.start();
        }

        void cancelAdsorbCursor()
        {
            QSignalBlocker blocker(&startAnimationTimer);
            Q_UNUSED(blocker)
                startAnimationTimer.stop();
            cursorAnimation.stop();
        }

        void onAnimationValueChanged(const QVariant &value)
        {
            QCursor::setPos(value.toPoint());
        }

        void startAnimation() {
            QPoint cursorPos = QCursor::pos();
            QPoint toPos = cursorPos;
            const QRect geometry = _window->frameGeometry().adjusted(-1, -1, 1, 1);

            switch (lastCornerEdge) {
                case Utility::TopLeftCorner:
                    toPos = geometry.topLeft();
                    break;
                case Utility::TopEdge:
                    toPos.setY(geometry.y());
                    break;
                case Utility::TopRightCorner:
                    toPos = geometry.topRight();
                    break;
                case Utility::RightEdge:
                    toPos.setX(geometry.right());
                    break;
                case Utility::BottomRightCorner:
                    toPos = geometry.bottomRight();
                    break;
                case Utility::BottomEdge:
                    toPos.setY(geometry.bottom());
                    break;
                case Utility::BottomLeftCorner:
                    toPos = geometry.bottomLeft();
                    break;
                case Utility::LeftEdge:
                    toPos.setX(geometry.x());
                    break;
                default:
                    break;
            }

            const QPoint &tmp = toPos - cursorPos;

            if (qAbs(tmp.x()) < 3 && qAbs(tmp.y()) < 3)
                return;

            canAdsorbCursor = false;

            cursorAnimation.setStartValue(cursorPos);
            cursorAnimation.setEndValue(toPos);
            cursorAnimation.start();
        }

        /// mouse left button is pressed in window vaild geometry
        bool leftButtonPressed = false;

        bool canAdsorbCursor = false;
        Utility::CornerEdge lastCornerEdge;
        QTimer startAnimationTimer;
        QVariantAnimation cursorAnimation;
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
    setContentsMargins(0, 0, 0, 0);
    
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

    qDebug() << "composited = " << composited;

    _titlebar = new DTitlebar(this);
    _titlebar->setFocusPolicy(Qt::NoFocus);
    if (!composited) {
        _titlebar->setAttribute(Qt::WA_NativeWindow);
        _titlebar->winId();
    }
    _titlebar->setStyleSheet("background: rgba(0, 0, 0, 0.6);");
    _titlebar->setMenu(ActionFactory::get().titlebarMenu());

    _toolbox = new ToolboxProxy(this);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    _center = new QWidget(this);
    _center->move(0, 0);

    _proxy = new MpvProxy(_center);

    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().mainContextMenu(), &QMenu::triggered, 
            this, &MainWindow::menuItemInvoked);

    updateProxyGeometry();

    //connect(&_timer, &QTimer::timeout, this, &MainWindow::timeout);
    //_timer.start(1000);

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();

    connect(_proxy, &MpvProxy::ellapsedChanged, [=]() {
        _toolbox->updateTimeInfo(_proxy->duration(), _proxy->ellapsed());
    });

    if (!composited) {
        connect(qApp, &QGuiApplication::applicationStateChanged,
                this, &MainWindow::onApplicationStateChanged);

        _evm = new EventMonitor(this);
        connect(_evm, &EventMonitor::buttonedPress, this, &MainWindow::onMonitorButtonPressed);
        connect(_evm, &EventMonitor::buttonedDrag, this, &MainWindow::onMonitorMotionNotify);
        connect(_evm, &EventMonitor::buttonedRelease, this, &MainWindow::onMonitorButtonReleased);
        _evm->start();
    }
}

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

MainWindow::~MainWindow()
{
    //delete _evm;
}

void MainWindow::timeout()
{
    _toolbox->updateTimeInfo(_proxy->duration(), _proxy->ellapsed());
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
        connect(act, &QAction::triggered, [=]() { this->menuItemInvoked(act); });
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
    switch (kd) {
        case ActionKind::Exit:
            qApp->quit(); 
            break;
        case ActionKind::LightTheme:
            qApp->setTheme(action->isChecked() ? "light":"dark");
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
            _proxy->takeScreenshot();
            break;
        }

        case BurstScreenshot: {
            _proxy->burstScreenshot();
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

    _center->resize(size());

    //auto tl = QPoint(frameMargins().left(), frameMargins().top());
    auto tl = QPoint();

    if (_titlebar) {
        QSize sz(size().width(), _titlebar->height());
        _titlebar->setGeometry(QRect(tl, sz));
    }

    if (_proxy) {
        _proxy->setGeometry(QRect(tl, size()));
    }

    if (_toolbox) {
        QRect r(0, size().height() - 80, size().width(), 80);
        _toolbox->setGeometry(r);
    }

    qDebug() << "margins " << frameMargins();
    qDebug() << "window frame " << frameGeometry();
    qDebug() << "window geom " << geometry();
    qDebug() << "_center " << _center->geometry();
    qDebug() << "_titlebar " << _titlebar->geometry();
    qDebug() << "proxy " << _proxy->geometry();
    qDebug() << "_toolbox " << _toolbox->geometry();
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

bool MainWindow::event(QEvent* e)
{
    if (e->type() == QEvent::Shortcut) {
        qDebug() << __func__;
    }

    return QWidget::event(e);
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
void MainWindow::resizeEvent(QResizeEvent *ev)
{
    //qDebug() << __func__;
    //if (size().width() > size().height()) {
        //this->setMinimumSize(QSize(528, 0));
    //} else {
        //this->setMinimumSize(QSize(0, 528));
    //}
    updateProxyGeometry();
}

void MainWindow::enterEvent(QEvent *ev)
{
    //qDebug() << __func__;
    //resumeToolsWindow();
}

void MainWindow::leaveEvent(QEvent *ev)
{
    //qDebug() << __func__;
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
    qDebug() << __func__;
    QWidget::mousePressEvent(ev);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    qDebug() << __func__;
    ActionFactory::get().mainContextMenu()->popup(cme->globalPos());
}

#include "mainwindow.moc"
