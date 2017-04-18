#include "mainwindow.h"
#include "mpv_proxy.h"
#include "titlebar_proxy.h"
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

    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
        connect(_handle, &DPlatformWindowHandle::frameMarginsChanged, 
                this, &MainWindow::frameMarginsChanged);
        _cachedMargins = _handle->frameMargins();
    }
   
    _proxy = new MpvProxy(this);
    _proxy->installEventFilter(this);

    _titlebar = new TitlebarProxy(this);
    _titlebar->populateMenu();

    _center = new QWidget(this);


    connect(this, &MainWindow::frameMarginsChanged, &MainWindow::updateProxyGeometry);
    connect(_titlebar->titlebar()->menu(), &QMenu::triggered, this, &MainWindow::menuItemInvoked);
}

MainWindow::~MainWindow()
{
    delete _titlebar;
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
        qDebug() << "proxy " << _proxy->geometry();
    }

    if (_titlebar) {
        QRect r(frameGeometry().topLeft(), geometry().size());
        r.setHeight(40);
        _titlebar->resize(r.size());
        qDebug() << "_titlebar " << _titlebar->frameGeometry();
    }
}

QMargins MainWindow::frameMargins() const
{
    return _cachedMargins;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _proxy) {
        if (event->type() == QEvent::WinIdChange) {
            qDebug() << "winid inited";
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    qDebug() << __func__;
    if (_titlebar) {
        _titlebar->show();
        _titlebar->raise();
        QTimer::singleShot(1000, this, [&]() {
            //_titleBar()->hide();
        });
    }
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__;
    updateProxyGeometry();
}

void MainWindow::moveEvent(QMoveEvent *ev)
{
    if (ev->spontaneous()) {
        QPoint p = ev->pos();
        p.rx() += frameMargins().left();
        p.ry() += frameMargins().top();
        _titlebar->move(p);
    }
}

#include "moc_mainwindow.cpp"
