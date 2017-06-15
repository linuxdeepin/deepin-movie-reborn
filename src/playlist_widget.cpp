#include "playlist_widget.h"
#include "playlist_model.h"
#include "compositing_manager.h"
#include "mpv_proxy.h"
#include "actions.h"
#include "mainwindow.h"

namespace dmr {
class PlayItemWidget: public QFrame {
public:
    PlayItemWidget() {
    }
};

PlaylistWidget::PlaylistWidget(QWidget *mw, MpvProxy *mpv)
    :QFrame(mw), _mpv(mpv), _mw(static_cast<MainWindow*>(mw))
{
    bool composited = CompositingManager::get().composited();
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

    auto *l = new QVBoxLayout(this);
    setLayout(l);

    connect(&_mpv->playlist(), &PlaylistModel::countChanged, this, &PlaylistWidget::loadPlaylist);
}

PlaylistWidget::~PlaylistWidget()
{
}

void PlaylistWidget::loadPlaylist()
{
    {
        for(auto p: _items) {
            p->deleteLater();
        }
        _items.clear();
    }

    auto items = _mpv->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new QLabel(this);
        w->setText(p->info.fileName());
        _items.append(w);
        layout()->addWidget(w);
        ++p;
    }
    static_cast<QVBoxLayout*>(layout())->addStretch();
}

void PlaylistWidget::togglePopup()
{
    this->setVisible(!isVisible());
}


}

