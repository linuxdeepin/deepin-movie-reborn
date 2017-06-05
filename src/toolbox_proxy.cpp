#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"

#include <QtWidgets>
#include <dbasebutton.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
class KeyPressBubbler: public QObject {
    public:
        KeyPressBubbler(QObject *parent): QObject(parent) {}

    protected:
        bool eventFilter(QObject *obj, QEvent *event) {
            if (event->type() == QEvent::KeyPress) {
                QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
                event->setAccepted(false);
                return false;
            } else {
                // standard event processing
                return QObject::eventFilter(obj, event);
            }
        }
};


ToolboxProxy::ToolboxProxy(QWidget *mainWindow)
    :QWidget(mainWindow),
    _mainWindow(mainWindow)
{

    bool composited = CompositingManager::get().composited();
    setStyleSheet("background: rgba(0, 0, 0, 0.6);");
    setAttribute(Qt::WA_TranslucentBackground);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

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

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    pb->installEventFilter(bubbler);
}

ToolboxProxy::~ToolboxProxy()
{
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    QTime d(0, 0), t(0, 0);
    d = d.addSecs(duration);
    t = t.addSecs(pos);
    _timeLabel->setText(QString("%2/%1").arg(d.toString("hh:mm:ss")).arg(t.toString("hh:mm:ss")));
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

void ToolboxProxy::paintEvent(QPaintEvent *pe)
{
    //QPainter p(this);
    //QPainterPath pp;
    //pp.addRoundedRect(rect(), 5, 5);
    //p.setClipPath(pp);

    //auto clr = QColor::fromRgb(0, 0, 0, 128);
    //p.fillRect(rect(), clr);

    QWidget::paintEvent(pe);
}

}


