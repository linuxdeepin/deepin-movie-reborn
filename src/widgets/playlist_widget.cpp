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

        setFixedWidth(220);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 10, 0);
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
        _closeBtn->setFixedSize(20, 20);
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
    :QListWidget(mw), _engine(mpv), _mw(static_cast<MainWindow*>(mw))
{
    bool composited = CompositingManager::get().composited();
    setFrameShape(QFrame::NoFrame);
    //setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setFixedWidth(220);
    setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));

    setSelectionMode(QListView::SingleSelection);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setResizeMode(QListView::Adjust);
    setDragDropMode(QListView::DropOnly);
    setSpacing(4);

    setAcceptDrops(true);

    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

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
    qDebug() << __func__ << count() << "current = " << _engine->playlist().current();
    for (int i = 0; i < count(); i++) {
        auto piw = dynamic_cast<PlayItemWidget*>(itemWidget(item(i)));

        auto old = piw->state();
        piw->setState(ItemState::Normal);

        if (_mouseItem == piw) {
            piw->setState(ItemState::Hover);
        }

        if (i == _engine->playlist().current()) {
            piw->setState(ItemState::Playing);
        }

        if (old != piw->state()) {
            //piw->style()->unpolish(piw);
            //piw->style()->polish(piw);
            piw->setStyleSheet(piw->styleSheet());
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
    auto piw = dynamic_cast<PlayItemWidget*>(_clickedItem);
    if (piw) {
        qDebug() << __func__;
        for (int i = 0; i < count(); i++) {
            if (_clickedItem == itemWidget(item(i))) {
                _engine->playlist().remove(i);
            }
        }
    }
}

void PlaylistWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void PlaylistWidget::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void PlaylistWidget::dropEvent(QDropEvent *ev)
{
    qDebug() << ev->mimeData()->formats();
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

    if (itemAt(cme->pos())) {
        _mouseItem = dynamic_cast<PlayItemWidget*>(itemWidget(itemAt(cme->pos())));
        on_item = true;
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
    clear();

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
        auto item = new QListWidgetItem;
        addItem(item);
        item->setSizeHint(w->sizeHint());
        setItemWidget(item, w);

        connect(w, SIGNAL(closeButtonClicked()), _mapper, SLOT(map()));
        _mapper->setMapping(w, w);
        ++p;
    }

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
