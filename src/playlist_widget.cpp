#include "playlist_widget.h"
#include "playlist_model.h"
#include "compositing_manager.h"
#include "mpv_proxy.h"
#include "actions.h"
#include "mainwindow.h"
#include "utils.h"

#include <DApplication>
#include <dimagebutton.h>

namespace dmr {
class PlayItemWidget: public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString bg READ getBg WRITE setBg DESIGNABLE true)
public:
    friend class PlaylistWidget;

    //FIXME: what if item destroyed
    PlayItemWidget(PlayItemInfo pif, QWidget* parent = 0)
        : QFrame(parent), _pif {pif} 
    {
        setProperty("PlayItemThumb", "true");
        setFrameShape(QFrame::NoFrame);

        // it's the same for all themes
        _play = QPixmap(":/resources/icons/dark/normal/film-top.png");

        setFixedHeight(67);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        setLayout(l);

        _thumb = new QLabel(this);
        setBg(QString(":/resources/icons/%1/normal/film-bg.png").arg(qApp->theme())); 
        l->addWidget(_thumb);

        auto *vl = new QVBoxLayout;
        l->addLayout(vl, 1);

        auto w = new QLabel(this);
        w->setText(pif.info.fileName());
        vl->addWidget(w);

        w = new QLabel(this);
        w->setText(_pif.mi.durationStr());
        vl->addWidget(w);
    }

    QString getBg() const { return _bg; }
    void setBg(const QString& s) 
    { 
        _bg = s; 

        QPixmap pm(s);
        QPainter p(&pm);

        if (!_pif.thumbnail.isNull()) {
            p.drawPixmap((pm.width() - _pif.thumbnail.width())/2, 
                    (pm.height() - _pif.thumbnail.height())/2, _pif.thumbnail);
        }
        p.drawPixmap((pm.width() - _play.width())/2, 
                (pm.height() - _play.height())/2, _play);
        p.end();

        _thumb->setPixmap(pm);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* me) override 
    {
        qDebug() << __func__;
    }

    void mouseDoubleClickEvent(QMouseEvent* me) override
    {
        qDebug() << __func__;
    }

private:
    QString _bg;
    QLabel *_thumb;
    QPixmap _play;
    PlayItemInfo _pif;


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

void PlaylistWidget::openItemInFM()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget*>(_mouseItem);
    if (item) {
        utils::ShowInFileManager(item->_pif.mi.filePath);
    }
}

void PlaylistWidget::contextMenuEvent(QContextMenuEvent *cme)
{
    bool on_item = false;
    _mouseItem = nullptr;

    QPoint p = cme->pos();
    for (auto w: _items) {
        if (w->geometry().contains(p)) {
            _mouseItem = w;
            on_item = true;
            break;
        }
    }

    auto menu = ActionFactory::get().playlistContextMenu();
    for (auto act: menu->actions()) {
        auto prop = (ActionKind)act->property("kind").toInt();
        if (prop == ActionKind::MovieInfo || 
                prop == ActionKind::PlaylistOpenItemInFM) {
            act->setEnabled(on_item);
        }
    }

    ActionFactory::get().playlistContextMenu()->popup(cme->globalPos());
}

void PlaylistWidget::loadPlaylist()
{
    {
        for(auto p: _items) {
            p->deleteLater();
        }
        _items.clear();

        QLayoutItem *child;
        while ((child = layout()->takeAt(0)) != 0) {
            delete child;
        }
    }

    auto items = _mpv->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this);
        _items.append(w);
        layout()->addWidget(w);
        ++p;
    }
    static_cast<QVBoxLayout*>(layout())->addStretch(1);
}

void PlaylistWidget::togglePopup()
{
    this->setVisible(!isVisible());
}


}

#include "playlist_widget.moc"
