#include "mainwindow.h"
#include "mpv_proxy.h"
#include "titlebar_proxy.h"
#include "toolbox_proxy.h"
#include "actions.h"

#include <QtWidgets>
#include <DApplication>

DWIDGET_USE_NAMESPACE
using namespace dmr;

MainWindow::MainWindow(QWidget *parent) 
    : QWidget(NULL)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setContentsMargins(0, 0, 0, 0);
    setMouseTracking(true);

    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        _cachedMargins = _handle->frameMargins();
    }
   
    _proxy = new MpvProxy(this);
    _proxy->installEventFilter(this);

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
}

MainWindow::~MainWindow()
{
    delete _titlebar;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _proxy) {
        if (event->type() == QEvent::MouseMove) {
            qDebug() << "player mouse move";
        }
    }

    return QWidget::eventFilter(watched, event);
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
            resumeToolsWindow();
            break;

        case Qt::ApplicationInactive:
            if (qApp->focusWindow())
            qDebug() << QString("focus window 0x%1").arg(qApp->focusWindow()->winId(), 0, 16);
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
        QTimer::singleShot(2000, this, &MainWindow::suspendToolsWindow);
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
}

#include "moc_mainwindow.cpp"
