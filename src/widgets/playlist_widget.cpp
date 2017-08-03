#include "playlist_widget.h"
#include "playlist_model.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "mainwindow.h"
#include "utils.h"
#include "movieinfo_dialog.h"

#include <DApplication>
#include <dimagebutton.h>

namespace dmr {
enum ItemState {
    Normal,
    Playing,
    Hover,
};

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
        setState(ItemState::Normal); 
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
        w->setProperty("Name", true);
        if (pif.url.isLocalFile())
            w->setText(pif.info.fileName());
        else 
            w->setText(pif.url.fileName());
        w->setWordWrap(true);
        vl->addWidget(w);

        w = new QLabel(this);
        w->setProperty("Time", true);
        w->setText(_pif.mi.durationStr());
        vl->addWidget(w);

        _closeBtn = new DImageButton(this);
        _closeBtn->setObjectName("CloseBtn");
        _closeBtn->hide();
        connect(_closeBtn, &DImageButton::clicked, this, &PlayItemWidget::closeButtonClicked);
    }

    void setState(ItemState is) {
        setProperty("ItemState", is);
    }

    ItemState state() const {
        return (ItemState)property("ItemState").toInt();
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

signals:
    void closeButtonClicked();

protected:
    void leaveEvent(QEvent* e) override
    {
        _closeBtn->hide();
    }

    void enterEvent(QEvent* e) override
    {
        _closeBtn->show();
        _closeBtn->raise();
    }

    void mouseReleaseEvent(QMouseEvent* me) override 
    {
        qDebug() << __func__;
    }

    void resizeEvent(QResizeEvent* se) override
    {
        qDebug() << __func__ << size() << _closeBtn->width();
        auto sz = this->size();
        _closeBtn->move(sz.width() - _closeBtn->width(), (sz.height() - _closeBtn->height())/2);
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
    DImageButton *_closeBtn;
};

class MainWindowListener: public QObject {
public:
    MainWindowListener(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);

            if (me->buttons() == Qt::LeftButton) {
                auto *plw = dynamic_cast<PlaylistWidget*>(parent());
                auto *mw = dynamic_cast<MainWindow*>(plw->parent());
                
                if (plw->isVisible() && !plw->underMouse()) {
                    mw->requestAction(ActionFactory::ActionKind::TogglePlaylist);
                }
            }
            return false;
        } else {
            // standard event processing
            return QObject::eventFilter(obj, event);
        }
    }
};

PlaylistWidget::PlaylistWidget(QWidget *mw, PlayerEngine *mpv)
    :QScrollArea(mw), _engine(mpv), _mw(static_cast<MainWindow*>(mw))
{
    bool composited = CompositingManager::get().composited();
    setFrameShape(QFrame::NoFrame);
    //setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedWidth(220);
    setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));

    setAcceptDrops(true);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

    auto w = new QWidget;
    w->setFixedWidth(220);
    auto *l = new QVBoxLayout(w);
    l->setSizeConstraint(QLayout::SetMinimumSize);
    w->setLayout(l);

    setWidget(w);

#ifndef USE_DXCB
    auto *mwl = new MainWindowListener(this);
    mw->installEventFilter(mwl);
#endif

    connect(&_engine->playlist(), &PlaylistModel::countChanged, this, &PlaylistWidget::loadPlaylist);
    connect(&_engine->playlist(), &PlaylistModel::currentChanged, this, &PlaylistWidget::updateItemStates);

    QTimer::singleShot(10, this, &PlaylistWidget::loadPlaylist);
}

PlaylistWidget::~PlaylistWidget()
{
}

void PlaylistWidget::updateItemStates()
{
    qDebug() << __func__ << _items.size() << "current = " << _engine->playlist().current();
    for (int i = 0; i < _items.size(); i++) {
        auto item = dynamic_cast<PlayItemWidget*>(_items.at(i));

        auto old = item->state();
        item->setState(ItemState::Normal);

        if (_mouseItem == item) {
            item->setState(ItemState::Hover);
        }

        if (i == _engine->playlist().current()) {
            item->setState(ItemState::Playing);
        }

        if (old != item->state()) {
            //item->style()->unpolish(item);
            //item->style()->polish(item);
            item->setStyleSheet(item->styleSheet());
        }
    }

}

void PlaylistWidget::showItemInfo()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget*>(_mouseItem);
    if (item) {
        MovieInfoDialog mid(item->_pif.mi);
        mid.exec();
    }
}

void PlaylistWidget::openItemInFM()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget*>(_mouseItem);
    if (item) {
        utils::ShowInFileManager(item->_pif.mi.filePath);
    }
}

void PlaylistWidget::removeClickedItem()
{
    if (!_clickedItem) return;
    auto item = dynamic_cast<PlayItemWidget*>(_clickedItem);
    if (item) {
        qDebug() << __func__;
        _engine->playlist().remove(_items.indexOf(_clickedItem));
    }
}

void PlaylistWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void PlaylistWidget::dropEvent(QDropEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        auto urls = ev->mimeData()->urls();
        for (const auto& url: urls) {
            if (!url.isValid()) continue;

            if (url.isLocalFile()) {
                QFileInfo fi(url.toLocalFile());
                if (!fi.exists()) continue;
            }
            _engine->addPlayFile(url);
        }

        ev->acceptProposedAction();
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

    updateItemStates();

    auto menu = ActionFactory::get().playlistContextMenu();
    for (auto act: menu->actions()) {
        auto prop = (ActionFactory::ActionKind)act->property("kind").toInt();
        if (prop == ActionFactory::ActionKind::PlaylistItemInfo || 
                prop == ActionFactory::ActionKind::PlaylistOpenItemInFM) {
            act->setEnabled(on_item);
        }
    }

    ActionFactory::get().playlistContextMenu()->popup(cme->globalPos());
}

void PlaylistWidget::loadPlaylist()
{
    qDebug() << __func__;
    {
        for(auto p: _items) {
            p->deleteLater();
        }
        _items.clear();

        QLayoutItem *child;
        while ((child = widget()->layout()->takeAt(0)) != 0) {
            delete child;
        }
    }

    if (!_mapper) {
        _mapper = new QSignalMapper(this);
        connect(_mapper, static_cast<void(QSignalMapper::*)(QWidget*)>(&QSignalMapper::mapped),
            [=](QWidget* w) {
                qDebug() << "item close clicked";
                _clickedItem = w;
                _mw->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);
            });
    }

    auto items = _engine->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this);
        _items.append(w);
        widget()->layout()->addWidget(w);

        connect(w, SIGNAL(closeButtonClicked()), _mapper, SLOT(map()));
        _mapper->setMapping(w, w);
        ++p;
    }
    static_cast<QVBoxLayout*>(widget()->layout())->addStretch(1);

    updateItemStates();
}

void PlaylistWidget::togglePopup()
{
    QRect fixed(_mw->size().width() - width(),
            _mw->titlebar()->geometry().bottom(),
            width(),
            _mw->toolbox()->geometry().top() - _mw->titlebar()->geometry().bottom());

    if (isVisible()) {
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setDuration(300);
        pa->setStartValue(fixed);
        pa->setEndValue(QRect(fixed.right(), fixed.top(), 0, fixed.height()));

        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
            setVisible(!isVisible());
        });
    } else {
        setVisible(!isVisible());
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setDuration(300);
        pa->setStartValue(QRect(fixed.right(), fixed.top(), 0, fixed.height()));
        pa->setEndValue(fixed);

        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
        });
    }
}

}

#include "playlist_widget.moc"
