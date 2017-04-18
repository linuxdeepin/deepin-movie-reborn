
#include <QtWidgets>
#include <QX11Info>

#include <xcb/xproto.h>
#include <xcb/xcb_aux.h>

#include "dmr_titlebar.h"
#include "titlebar_proxy.h"
#include "actions.h"
#ifdef Q_OS_LINUX
#include "xutil.h"
#endif

namespace dmr {

class EventRelayer: public QAbstractNativeEventFilter 
{
public:
    friend class TitlebarProxy;
    QWindow *_source, *_target;

    EventRelayer(QWindow* src, QWindow *dest)
        :QAbstractNativeEventFilter(), _source(src), _target(dest) {
        int screen = 0;
        xcb_screen_t *s = xcb_aux_get_screen (QX11Info::connection(), screen);
        const uint32_t data[] = { 
            XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
        };
        xcb_change_window_attributes (QX11Info::connection(), _source->winId(),
                XCB_CW_EVENT_MASK, data);

        qApp->installNativeEventFilter(this);
    }

    virtual ~EventRelayer() {
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *) override {
        if(Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t* event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
                case XCB_CONFIGURE_NOTIFY: {
                    xcb_configure_notify_event_t *cne = (xcb_configure_notify_event_t*)event;
                    if (cne->window != _source->winId())
                        return false;

                    QPoint p(cne->x, cne->y);
                    if (p != _target->framePosition()) {
                        qDebug() << "cne: " << QRect(cne->x, cne->y, cne->width, cne->height)
                            << "origin: " << _source->framePosition()
                            << "dest: " << _target->framePosition();
                        _target->setFramePosition(QPoint(cne->x, cne->y));
                    }
                    break;
                }
                default:
                    break;
            }
        }

        return false;
    }
};

TitlebarProxy::TitlebarProxy(QWidget *mainWindow)
    :DBlurEffectWidget(nullptr),
    _mainWindow(mainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
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
    //_titlebar->installEventFilter(this);

    (void)winId();

    new EventRelayer(this->windowHandle(), _mainWindow->windowHandle());
}

TitlebarProxy::~TitlebarProxy()
{
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

bool TitlebarProxy::eventFilter(QObject *watched, QEvent *event)
{
    return DBlurEffectWidget::eventFilter(watched, event);
}

void TitlebarProxy::resizeEvent(QResizeEvent* ev)
{
    DBlurEffectWidget::resizeEvent(ev);
}

void TitlebarProxy::showEvent(QShowEvent* ev)
{
#ifdef Q_OS_LINUX
    //QTimer::singleShot(0, this, [&]() { XUtils::SetStayOnTop(this, true); });
#endif

    DBlurEffectWidget::showEvent(ev);
}


void TitlebarProxy::mousePressEvent(QMouseEvent *event)
{
    DBlurEffectWidget::mousePressEvent(event);
}

void TitlebarProxy::mouseMoveEvent(QMouseEvent *event)
{
#ifdef Q_OS_LINUX
    XUtils::MoveWindow(this, event->button());
#endif
    qDebug() << __func__ << event->globalPos();

    DBlurEffectWidget::mouseMoveEvent(event);
}

void TitlebarProxy::populateMenu() 
{
    auto *menu = ActionFactory::get().titlebarMenu();
    _titlebar->setMenu(menu);
    _titlebar->setSeparatorVisible(true);
}

} // namespace dmr

