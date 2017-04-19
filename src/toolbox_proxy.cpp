#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"

#include <QtWidgets>

namespace dmr {

ToolboxProxy::ToolboxProxy(QWidget *mainWindow)
    :DBlurEffectWidget(nullptr),
    _mainWindow(mainWindow)
{
    setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::BypassWindowManagerHint);
    setContentsMargins(0, 0, 0, 0);

    setAttribute(Qt::WA_TranslucentBackground);
    setMaskColor(Qt::black);

    setBlendMode(DBlurEffectWidget::BehindWindowBlend);

    auto *l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    setLayout(l);

    _timeLabel = new QLabel("");
    l->addWidget(_timeLabel);


    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

    auto *pb = new QPushButton("Play");
    connect(pb, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(pb, "play");

    l->addWidget(pb);
    l->setAlignment(pb, Qt::AlignHCenter);


    (void)winId();

    _evRelay = new EventRelayer(_mainWindow->windowHandle(), this->windowHandle()); 
    connect(_evRelay, &EventRelayer::targetNeedsUpdatePosition, this, &ToolboxProxy::updatePosition);

}

ToolboxProxy::~ToolboxProxy()
{
    delete _evRelay;
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    QTime d(0, 0), t(0, 0);
    d = d.addSecs(duration);
    t = t.addSecs(pos);
    qDebug() << __func__ << duration << pos
        << d.toString("hh:mm:ss") 
        << (t.toString("hh:mm:ss"));
    _timeLabel->setText(QString("%1/%2").arg(d.toString("hh:mm:ss")).arg(t.toString("hh:mm:ss")));
}

void ToolboxProxy::buttonClicked(QString id)
{
    if (id == "play") {
        emit requestPlay(); // FIXME: may pause
    }
}

void ToolboxProxy::updatePosition(const QPoint& p)
{
    QPoint pos(p);
    auto *mw = static_cast<MainWindow*>(_mainWindow);
    pos.rx() += mw->frameMargins().left();
    pos.ry() += mw->frameMargins().top() + mw->height() - height();
    windowHandle()->setFramePosition(pos);
}

static QPoint last_proxy_pos;
static QPoint last_wm_pos;
void ToolboxProxy::mousePressEvent(QMouseEvent *event)
{
    qDebug() << __func__;
    last_wm_pos = event->globalPos();
    last_proxy_pos = _mainWindow->windowHandle()->framePosition();
    DBlurEffectWidget::mousePressEvent(event);
}

void ToolboxProxy::mouseMoveEvent(QMouseEvent *event)
{
    QPoint d = event->globalPos() - last_wm_pos;
    qDebug() << __func__ << d;

    _mainWindow->windowHandle()->setFramePosition(last_proxy_pos + d);

    DBlurEffectWidget::mouseMoveEvent(event);
}

}


