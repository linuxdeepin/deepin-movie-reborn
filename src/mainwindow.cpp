#include "mainwindow.h"
#include "mpv_proxy.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"
#include "compositing_manager.h"
#include "shortcut_manager.h"

#include <QtWidgets>
#include <DApplication>
#include <DTitlebar>


DWIDGET_USE_NAMESPACE
using namespace dmr;

/// shadow
#define SHADOW_COLOR_NORMAL QColor(0, 0, 0, 255 * 0.35)
#define SHADOW_COLOR_ACTIVE QColor(0, 0, 0, 255 * 0.6)

MainWindow::MainWindow(QWidget *parent) 
    : QWidget(NULL)
{
    setWindowFlags(Qt::FramelessWindowHint);
    
    bool composited = CompositingManager::get().composited();
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

        default:
            break;
    }
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

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__;
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
    QWidget::mouseMoveEvent(ev);
}

void MainWindow::mousePressEvent(QMouseEvent *ev) 
{
    qDebug() << __func__;
    QWidget::mousePressEvent(ev);
}

#include "moc_mainwindow.cpp"
