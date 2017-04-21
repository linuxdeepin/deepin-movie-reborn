#include "mainwindow.h"
#include "mpv_proxy.h"
#include "titlebar_proxy.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"

#include <QtWidgets>
#include <DApplication>


DWIDGET_USE_NAMESPACE
using namespace dmr;


MainWindow::MainWindow(QWidget *parent) 
    : QWidget(NULL)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setContentsMargins(0, 0, 0, 0);

    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        _cachedMargins = _handle->frameMargins();
    }
   
    _proxy = new MpvProxy(this);

    _titlebar = new TitlebarProxy(this);
    _titlebar->setFocusPolicy(Qt::NoFocus);
    _titlebar->populateMenu();

    _toolbox = new ToolboxProxy(this);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    _center = new QWidget(this);


    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->titlebar()->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);

    connect(&_timer, &QTimer::timeout, this, &MainWindow::timeout);
    _timer.start(1000);

    connect(qApp, &QGuiApplication::focusWindowChanged, [=](QWindow *w) {
        if (w) qDebug() << QString("focus window 0x%1").arg(w->winId(), 0, 16);
    });

    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &MainWindow::onApplicationStateChanged);

    _evm = new EventMonitor(this);
    connect(_evm, &EventMonitor::buttonedPress, this, &MainWindow::onMonitorButtonPressed);
    connect(_evm, &EventMonitor::buttonedDrag, this, &MainWindow::onMonitorMotionNotify);
    connect(_evm, &EventMonitor::buttonedRelease, this, &MainWindow::onMonitorButtonReleased);
    _evm->start();
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
    delete _titlebar;
    delete _evm;
}

void MainWindow::timeout()
{
    if (_proxy) {
        _toolbox->updateTimeInfo(_proxy->duration(), _proxy->ellapsed());
    }
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

void MainWindow::menuItemInvoked(QAction *action)
{
    auto prop = action->property("kind");
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 2)
    auto kd = (ActionFactory::ActionKind)action->property("kind").value<int>();
#else
    auto kd = action->property("kind").value<ActionFactory::ActionKind>();
#endif
    qDebug() << "prop = " << prop << ", kd = " << kd;
    switch (kd) {
        case ActionFactory::ActionKind::Exit:
            qApp->quit(); 
            break;
        case ActionFactory::ActionKind::LightTheme:
            qApp->setTheme(action->isChecked() ? "light":"dark");
            break;
        default:
            break;
    }
}

void MainWindow::play(const QFileInfo& fi)
{
    if (!fi.exists()) 
        return;

    _proxy->addPlayFile(fi);

    updateProxyGeometry();
    _proxy->show();
    _proxy->play();
}

void MainWindow::updateProxyGeometry()
{
    if (_handle) {
        _cachedMargins = _handle->frameMargins();
    }

    _center->resize(size());

    if (_proxy) {
        QRect r = _center->geometry();
        r.translate(QPoint(frameMargins().left(), frameMargins().top()));
        _proxy->setGeometry(r);
        qDebug() << "window frame " << frameGeometry();
        qDebug() << "proxy " << geometry();
    }

    if (_titlebar) {
        QRect r(frameGeometry().topLeft(), geometry().size());
        r.setHeight(40);
        _titlebar->resize(r.size());
        qDebug() << "_titlebar " << _titlebar->frameGeometry();
    }

    if (_toolbox) {
        QRect r(frameGeometry().topLeft(), geometry().size());
        r.setHeight(80);
        _toolbox->resize(r.size());
        qDebug() << "_toolbox " << _toolbox->frameGeometry();
    }
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
    if (_titlebar) {
        resumeToolsWindow();
        QTimer::singleShot(4000, this, &MainWindow::suspendToolsWindow);
    }
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__;
    updateProxyGeometry();
}

void MainWindow::enterEvent(QEvent *ev)
{
    qDebug() << __func__;
    resumeToolsWindow();
}

void MainWindow::leaveEvent(QEvent *ev)
{
    qDebug() << __func__;
    bool leave = true;
    if (qApp->topLevelAt(QCursor::pos())) {
        leave =false;
        qDebug() << __func__ << "underMouse " 
            << QString("0x%1").arg(qApp->topLevelAt(QCursor::pos())->winId());
    }

    if (leave) {
        suspendToolsWindow();
    } else {
        resumeToolsWindow();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *ev)
{
    qDebug() << __func__;
    if (ev->modifiers() == 0) {
        if (ev->key() == Qt::Key_Left) {
            _proxy->seekBackward(20);
        } else if (ev->key() == Qt::Key_Right) {
            _proxy->seekForward(20);
        }
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
    qDebug() << __func__;
    QWidget::mouseMoveEvent(ev);
}

void MainWindow::mousePressEvent(QMouseEvent *ev) 
{
    qDebug() << __func__;
    QWidget::mousePressEvent(ev);
}

#include "moc_mainwindow.cpp"
