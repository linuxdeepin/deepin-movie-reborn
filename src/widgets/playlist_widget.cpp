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
#include <dthememanager.h>
#include <dscrollbar.h>

namespace dmr {
enum ItemState {
    Normal,
    Playing,
    Hover,
    Invalid, // gets deleted or similar
};

class PlayItemWidget: public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString bg READ getBg WRITE setBg DESIGNABLE true)
public:
    friend class PlaylistWidget;

    PlayItemWidget(const PlayItemInfo& pif, QListWidget* list = 0)
        : QFrame(), _pif {pif}, _listWidget {list} 
    {
        DThemeManager::instance()->registerWidget(this, QStringList() << "PlayItemThumb");
        
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
        l->addWidget(_thumb);

        auto *vl = new QVBoxLayout;
        vl->setContentsMargins(0, 0, 3, 0);
        vl->setSpacing(0);
        l->addLayout(vl, 1);

        _name = new QTextEdit(this);
        _name->setProperty("Name", true);
        _name->setReadOnly(true);
        _name->setAcceptRichText(false);
        _name->setWordWrapMode(QTextOption::WrapAnywhere);
        _name->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _name->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _name->setFrameShape(QFrame::NoFrame);
        _name->setTextInteractionFlags(Qt::NoTextInteraction);
        _name->installEventFilter(this);

        vl->addWidget(_name, 1);

        _time = new QLabel(this);
        _time->setProperty("Time", true);
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("File does not exist"));
        }
        vl->addWidget(_time);

        setBg(QString(":/resources/icons/%1/normal/film-bg.png").arg(qApp->theme())); 

        _closeBtn = new DImageButton(this);
        _closeBtn->setFixedSize(20, 20);
        _closeBtn->setObjectName("CloseBtn");
        _closeBtn->hide();
        connect(_closeBtn, &DImageButton::clicked, this, &PlayItemWidget::closeButtonClicked);
    }

    void updateInfo(const PlayItemInfo& pif) {
        _pif = pif;
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("File does not exist"));
        }
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

        QPixmap dest(pm.size());
        dest.fill(Qt::transparent);
        QPainter p(&dest);
        
        if (state() == ItemState::Invalid) {
            p.setOpacity(0.5);
        }

        p.drawPixmap(0, 0, pm);

        if (!_pif.thumbnail.isNull()) {
            auto img = _pif.thumbnail.scaledToHeight(44, Qt::SmoothTransformation);
            p.drawPixmap((pm.width() - 24)/2, (pm.height() - 44)/2, img, 
                    (img.width()-24)/2, (img.height()-44)/2, 24, 44);

        }
        p.drawPixmap((pm.width() - _play.width())/2, 
                (pm.height() - _play.height())/2, _play);
        p.end();

        _thumb->setPixmap(dest);
    }

signals:
    void closeButtonClicked();
    void doubleClicked();

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

    bool eventFilter(QObject *obj, QEvent *e) override 
    {
        if (e->type() == QEvent::MouseButtonDblClick) {
            doDoubleClick();
            return true;
        }
        return QWidget::eventFilter(obj, e);
    }

    bool event(QEvent *ee) override
    {
        if(ee->type() == QEvent::Resize) {
            int text_height = _name->document()->size().height();
            _name->setFixedHeight(text_height);

            //resize(width(), _name->height() + _time->height() + layout()->spacing());
            resize(width(), _name->height() + _time->height());
        }

        return QFrame::event(ee);
    }

    void resizeEvent(QResizeEvent* se) override
    {
        auto sz = this->size();
        _closeBtn->move(sz.width() - _closeBtn->width() - 20, (sz.height() - _closeBtn->height())/2);
    }

    void showEvent(QShowEvent *se) override
    {
        QString msg = _pif.url.fileName();
        if (!_pif.url.isLocalFile() && msg.isEmpty()) {
            msg = _pif.url.toString();
        }
        _name->setText(_name->fontMetrics().elidedText(msg, Qt::ElideMiddle, 270));

        int text_height = _name->document()->size().height();
        _name->setFixedHeight(text_height);
        //resize(width(), _name->height() + _time->height() + layout()->spacing());
        resize(width(), _name->height() + _time->height());
        QTimer::singleShot(0, [=]() {
            auto pos = _listWidget->mapFromGlobal(QCursor::pos());
            auto r = QRect(mapTo(_listWidget, QPoint()), size());
            if (r.contains(pos)) {
                _closeBtn->show();
                _closeBtn->raise();
            }
        });

    }

    void mouseDoubleClickEvent(QMouseEvent* me) override
    {
        doDoubleClick();
    }

    void doDoubleClick()
    {
        if (!_pif.url.isLocalFile() || QFileInfo::exists(_pif.info.absoluteFilePath())) {
            emit doubleClicked();
        } else {
            if (state() != ItemState::Invalid) {
                setState(ItemState::Invalid);
                _time->setText(tr("File does not exist"));
                setStyleSheet(styleSheet());
            }
        }
    }

private:
    QString _bg;
    QLabel *_thumb;
    QTextEdit *_name;
    QLabel *_time;
    QPixmap _play;
    PlayItemInfo _pif;
    DImageButton *_closeBtn;
    QListWidget *_listWidget {nullptr};
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
    DThemeManager::instance()->registerWidget(this);

    bool composited = CompositingManager::get().composited();
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
    connect(&_engine->playlist(), &PlaylistModel::itemInfoUpdated, this, &PlaylistWidget::updateItemInfo);

    QTimer::singleShot(10, this, &PlaylistWidget::loadPlaylist);
}

PlaylistWidget::~PlaylistWidget()
{
}

void PlaylistWidget::updateItemInfo(int id)
{
    auto piw = dynamic_cast<PlayItemWidget*>(itemWidget(item(id)));
    piw->updateInfo(_engine->playlist().items()[id]);
}

void PlaylistWidget::updateItemStates()
{
    qDebug() << __func__ << count() << "current = " << _engine->playlist().current();
    for (int i = 0; i < count(); i++) {
        auto piw = dynamic_cast<PlayItemWidget*>(itemWidget(item(i)));

        auto old = piw->state();
        piw->setState(ItemState::Normal);
        if (!piw->_pif.valid) {
            piw->setState(ItemState::Invalid);
        }

        if (_mouseItem == piw) {
            piw->setState(ItemState::Hover);
        }

        if (i == _engine->playlist().current()) {
            piw->setState(ItemState::Playing);
        }

        if (old != piw->state()) {
            piw->setStyleSheet(piw->styleSheet());
        }
    }

}

void PlaylistWidget::showItemInfo()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget*>(_mouseItem);
    if (item) {
        MovieInfoDialog mid(item->_pif);
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
                break;
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
    if (!ev->mimeData()->hasUrls()) {
        return;
    }
    auto urls = ev->mimeData()->urls();
    _engine->addPlayFiles(urls);

    ev->acceptProposedAction();
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

    auto piw = dynamic_cast<PlayItemWidget*>(_mouseItem);
    auto menu = ActionFactory::get().playlistContextMenu();
    for (auto act: menu->actions()) {
        auto prop = (ActionFactory::ActionKind)act->property("kind").toInt();
        bool on = true;
        if (prop == ActionFactory::ActionKind::PlaylistOpenItemInFM) {
            on = on_item;
        } else if (prop == ActionFactory::ActionKind::PlaylistItemInfo) {
            on = on_item && piw->_pif.valid && piw->_pif.url.isLocalFile();
        }
        act->setEnabled(on);
    }

    ActionFactory::get().playlistContextMenu()->popup(cme->globalPos());
}

void PlaylistWidget::showEvent(QShowEvent *se)
{
    batchUpdateSizeHints();
    adjustSize();
}

void PlaylistWidget::loadPlaylist()
{
    qDebug() << __func__;
    clear();

    if (!_closeMapper) {
        _closeMapper = new QSignalMapper(this);
        connect(_closeMapper,
                static_cast<void(QSignalMapper::*)(QWidget*)>(&QSignalMapper::mapped),
            [=](QWidget* w) {
                qDebug() << "item close clicked";
                _clickedItem = w;
                _mw->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);
            });
    }

    if (!_activateMapper) {
        _activateMapper = new QSignalMapper(this);
        connect(_activateMapper,
                static_cast<void(QSignalMapper::*)(QWidget*)>(&QSignalMapper::mapped),
            [=](QWidget* w) {
                qDebug() << "item double clicked";
                QList<QVariant> args;
                for (int i = 0; i < count(); i++) {
                    if (w == itemWidget(item(i))) {
                        args << i;
                        _mw->requestAction(ActionFactory::ActionKind::GotoPlaylistSelected,
                                false, args);
                        break;
                    }
                }
            });
    }

    auto items = _engine->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this);
        //w->show();
        auto item = new QListWidgetItem;
        addItem(item);
        setItemWidget(item, w);

        connect(w, SIGNAL(closeButtonClicked()), _closeMapper, SLOT(map()));
        connect(w, SIGNAL(doubleClicked()), _activateMapper, SLOT(map()));
        _closeMapper->setMapping(w, w);
        _activateMapper->setMapping(w, w);
        ++p;
    }

    batchUpdateSizeHints();
    updateItemStates();
    setStyleSheet(styleSheet());
}

void PlaylistWidget::batchUpdateSizeHints()
{
    if (isVisible()) {
        for (int i = 0; i < this->count(); i++) {
            auto item = this->item(i);
            auto w = this->itemWidget(item);
            item->setSizeHint(w->size());
        }
    }
}

void PlaylistWidget::togglePopup()
{
    QRect fixed(_mw->size().width() - width(),
            _mw->titlebar()->geometry().bottom(),
            width(),
            _mw->toolbox()->geometry().top() - _mw->titlebar()->geometry().bottom());

    if (_toggling) return;

    if (_state == State::Opened) {
        Q_ASSERT(isVisible());

        _toggling = true;
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setDuration(300);
        pa->setStartValue(fixed);
        pa->setEndValue(QRect(fixed.right(), fixed.top(), 0, fixed.height()));

        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
            setVisible(!isVisible());
            _toggling = false;
            _state = State::Closed;
        });
    } else {
        setVisible(!isVisible());
        _toggling = true;
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setDuration(300);
        pa->setStartValue(QRect(fixed.right(), fixed.top(), 0, fixed.height()));
        pa->setEndValue(fixed);

        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
            _toggling = false;
            _state = State::Opened;
        });
    }
}

}

#include "playlist_widget.moc"
