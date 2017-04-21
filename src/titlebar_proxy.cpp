#include "dmr_titlebar.h"
#include "titlebar_proxy.h"
#include "actions.h"
#ifdef Q_OS_LINUX
#include "xutil.h"
#endif
#include "event_relayer.h"
#include "mainwindow.h"
#include <QtWidgets>

namespace dmr {

TitlebarProxy::TitlebarProxy(QWidget *mainWindow)
    :DBlurEffectWidget(nullptr),
    _mainWindow(mainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
    setContentsMargins(0, 0, 0, 0);

    setAttribute(Qt::WA_TranslucentBackground);
    setMaskColor(Qt::black);

    setBlendMode(DBlurEffectWidget::BehindWindowBlend);

    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    setLayout(l);

    _titlebar = new DMRTitlebar(this);
    connect(_titlebar, &DMRTitlebar::maxButtonClicked, this, &TitlebarProxy::toggleWindowState);
    connect(_titlebar, &DMRTitlebar::minButtonClicked, this, &TitlebarProxy::showMinimized);
    connect(_titlebar, &DMRTitlebar::closeButtonClicked, this, &TitlebarProxy::closeWindow);

    l->addWidget(_titlebar);
    _titlebar->show();

    (void)winId();

    _evRelay = new EventRelayer(_mainWindow->windowHandle(), this->windowHandle()); 
    connect(_evRelay, &EventRelayer::targetNeedsUpdatePosition, this, &TitlebarProxy::updatePosition);
}

TitlebarProxy::~TitlebarProxy()
{
    delete _evRelay;
}

void TitlebarProxy::closeWindow()
{
    qDebug() << __func__;
    qApp->quit();
}

void TitlebarProxy::toggleWindowState()
{
    qDebug() << __func__;
    QWidget *parentWindow = _mainWindow->window();
    if (parentWindow->isMaximized()) {
        parentWindow->showNormal();
    } else if (!parentWindow->isFullScreen()) {
        parentWindow->showMaximized();
    }
}

void TitlebarProxy::showMinimized()
{
    QWidget *parentWindow = _mainWindow->window();
    if (DPlatformWindowHandle::isEnabledDXcb(parentWindow)) {
        parentWindow->showMinimized();
    } else {
#ifdef Q_OS_LINUX
        XUtils::ShowMinimizedWindow(parentWindow, true);
#else
        parentWindow->showMinimized();
#endif
    }
}

void TitlebarProxy::updatePosition(const QPoint& p)
{
    QPoint pos(p);
    auto *mw = static_cast<MainWindow*>(_mainWindow);
    pos.rx() += mw->frameMargins().left();
    pos.ry() += mw->frameMargins().top();
    windowHandle()->setFramePosition(pos);
}

static QPoint last_proxy_pos;
static QPoint last_wm_pos;
void TitlebarProxy::mousePressEvent(QMouseEvent *event)
{
    qDebug() << __func__;
    last_wm_pos = event->globalPos();
    last_proxy_pos = _mainWindow->windowHandle()->framePosition();
    DBlurEffectWidget::mousePressEvent(event);
}

void TitlebarProxy::mouseMoveEvent(QMouseEvent *event)
{
    QPoint d = event->globalPos() - last_wm_pos;
    //qDebug() << __func__ << d;

    _mainWindow->windowHandle()->setFramePosition(last_proxy_pos + d);

    DBlurEffectWidget::mouseMoveEvent(event);
}

void TitlebarProxy::populateMenu() 
{
    auto *menu = ActionFactory::get().titlebarMenu();
    _titlebar->setMenu(menu);
    _titlebar->setSeparatorVisible(true);
}

} // namespace dmr

