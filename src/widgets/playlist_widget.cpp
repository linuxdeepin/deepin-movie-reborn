/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include "playlist_widget.h"
#include "playlist_model.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "mainwindow.h"
#include "utils.h"
#include "movieinfo_dialog.h"
#include "tip.h"

#include <DApplication>
#include <dimagebutton.h>
#include <dthememanager.h>
#include <DScrollBar>

#define PLAYLIST_FIXED_WIDTH 800
#define POPUP_DURATION 200

namespace dmr {
    QString splitText(const QString &text, int width,
            QTextOption::WrapMode wordWrap, const QFont& font, int lineHeight)
    {
        int height = 0;

        QTextLayout textLayout(text);
        QString str;

        textLayout.setFont(font);
        const_cast<QTextOption*>(&textLayout.textOption())->setWrapMode(wordWrap);

        textLayout.beginLayout();
        QTextLine line = textLayout.createLine();
        while (line.isValid()) {
            height += lineHeight;

            line.setLineWidth(width);
            const QString &tmp_str = text.mid(line.textStart(), line.textLength());
            if (tmp_str.indexOf('\n'))
                height += lineHeight;

            str += tmp_str;
            line = textLayout.createLine();

            if(line.isValid())
                str.append("\n");
        }

        textLayout.endLayout();

        return str;
    }
class PlayItemTooltipHandler: public QObject {
public:
    PlayItemTooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        switch (event->type()) {
            case QEvent::ToolTip: {
                QHelpEvent *he = static_cast<QHelpEvent *>(event);
                auto tip = obj->property("HintWidget").value<Tip*>();
                auto item = tip->property("for").value<QWidget*>();
                auto lb = tip->findChild<DLabel*>("TipText");
                lb->setAlignment(Qt::AlignLeft);

                auto msg = splitText(item->toolTip(), 200, QTextOption::WordWrap, lb->font(), 
                        lb->fontMetrics().height());
                lb->setText(msg);
                tip->update();
                tip->show();
                tip->adjustSize();
                tip->raise();
                auto pos = he->globalPos() + QPoint{0, 10};
                auto dw = qApp->desktop()->availableGeometry(item).width();
                if (pos.x() + tip->width() > dw) {
                    pos.rx() = dw - tip->width();
                }
                tip->move(pos);
                return true;
            }

            case QEvent::Leave: {
                auto tip = obj->property("HintWidget").value<Tip*>();
                tip->hide();
                event->ignore();

            }
            default: break;
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
};

enum ItemState {
    Normal,
    Playing,
    Invalid, // gets deleted or similar
};

class PlayItemWidget: public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString bg READ getBg WRITE setBg DESIGNABLE true)
public:
    friend class PlaylistWidget;

    PlayItemWidget(const PlayItemInfo& pif, QListWidget* list = 0, int index =0,PlaylistWidget *parent=nullptr)
        : QFrame(), _pif {pif}, _listWidget {list},_playlist{parent}
    {
//        DThemeManager::instance()->registerWidget(this, QStringList() << "PlayItemThumb");
        
        setProperty("PlayItemThumb", "true");
        setState(ItemState::Normal); 
        setFrameShape(QFrame::NoFrame);

        auto kd = "local";
        if (!_pif.url.isLocalFile()) {
            if (_pif.url.scheme().startsWith("dvd")) {
                kd = "dvd";
            } else {
                kd = "network";
            }
        }
        setProperty("ItemKind", kd);

        // it's the same for all themes
        _play = QPixmap(":/resources/icons/dark/normal/film-top.svg");
        _play.setDevicePixelRatio(qApp->devicePixelRatio());

        setFixedSize(_playlist->width()-250, 36);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(10, 0, 16, 0);
        l->setSpacing(10);
        setLayout(l);

        _index = new QLabel(this);
        _index->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T9));
        _index->setText(QString::number(index+1));
        _index->setFixedWidth(22);
        l->addWidget(_index);


        _thumb = new ListPic(_pif.thumbnail.scaled(QSize(42,24)),this);
//        _thumb->setPixmap(_pif.thumbnail.scaled(QSize(42,24)));
        l->addWidget(_thumb);

        auto *vl = new QHBoxLayout;
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        l->addLayout(vl);

//        vl->addStretch();

        _name = new QTextEdit(this);
        _name->setProperty("Name", true);
        _name->setReadOnly(true);
        _name->setAcceptRichText(false);
        _name->setWordWrapMode(QTextOption::NoWrap);
        _name->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _name->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _name->setFrameShape(QFrame::NoFrame);
        _name->setTextInteractionFlags(Qt::NoTextInteraction);
        _name->setFixedWidth(width()-180);
//        _name->setStyleSheet("background: red;");
        _name->installEventFilter(this);
        _name->viewport()->setAutoFillBackground(false);
        _name->setAutoFillBackground(false);

        vl->addWidget(_name);
//        vl->addStretch(1);

        _time = new QLabel(this);
        _time->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T9));
        _time->setProperty("Time", true);
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("File does not exist"));
        }
        vl->addWidget(_time);
        vl->addStretch();

//        setBg(QString(":/resources/icons/%1/normal/film-bg.svg").arg(qApp->theme()));

        //_closeBtn = new FloatingButton(this);
        _closeBtn = new DFloatingButton(DStyle::SP_CloseButton, this);
        _closeBtn->setFixedSize(20, 20);
        _closeBtn->setObjectName("CloseBtn");
        _closeBtn->hide();
        connect(_closeBtn, &DFloatingButton::clicked, this, &PlayItemWidget::closeButtonClicked);
        //connect(_closeBtn, &FloatingButton::clicked, this, &PlayItemWidget::closeButtonClicked);
        //connect(_closeBtn, &FloatingButton::mouseHover, this, &PlayItemWidget::closeBtnStates);


        setToolTip(_pif.mi.title);
        auto th = new PlayItemTooltipHandler(this);
        auto t = new Tip(QPixmap(), _pif.mi.title, NULL);
        t->setWindowFlags(Qt::ToolTip|Qt::CustomizeWindowHint);
        t->setAttribute(Qt::WA_TranslucentBackground);
        t->setMaximumWidth(200);
        t->setProperty("for", QVariant::fromValue<QWidget*>(this));
        t->layout()->setContentsMargins(0, 7, 0, 7);
        t->hide();
        setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        installEventFilter(th);
        connect(_playlist,&PlaylistWidget::sizeChange,this,[=]{
            setFixedWidth(_playlist->width()-250);
//            setFixedSize(_playlist->width(), 36);
        });
    }

    void updateInfo(const PlayItemInfo& pif) {
        _pif = pif;
        _time->setText(_pif.mi.durationStr());
        setToolTip(_pif.mi.title);
        updateNameText();

        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("File does not exist"));
        }
//        setStyleSheet(styleSheet());
        update();
    }

    void setState(ItemState is) {
        setProperty("ItemState", is);
        update();
    }

    ItemState state() const {
        return (ItemState)property("ItemState").toInt();
    }
    void setIndex(int index){
        _index->setText(QString::number(index+1));
    }

    QString getBg() const { return _bg; }
    void setBg(const QString& s) 
    { 
        _bg = s; 

        auto dpr = qApp->devicePixelRatio();

        QPixmap pm = QPixmap::fromImage(utils::LoadHiDPIImage(s));

        QPixmap dest(pm.size());
        dest.setDevicePixelRatio(dpr);
        dest.fill(Qt::transparent);
        QPainter p(&dest);
        
        if (state() == ItemState::Invalid) {
            p.setOpacity(0.5);
        }

        // thumb size
        QSize sz(22, 40);
        sz *= dpr;

        p.drawPixmap(0, 0, pm);

        if (!_pif.thumbnail.isNull()) {
            auto img = _pif.thumbnail.scaledToHeight(sz.height(), Qt::SmoothTransformation);
            img.setDevicePixelRatio(dpr);

            QPointF target_pos((pm.width() - sz.width())/2, (pm.height() - sz.height())/2);
            target_pos /= dpr;

            QRectF src_rect((img.width()-sz.width())/2, (img.height()-sz.height())/2,
                    sz.width(), sz.height());
            p.drawPixmap(target_pos, img, src_rect);

        }

        if (state() == ItemState::Playing) {
            QPointF pos((pm.width() - _play.width())/2, (pm.height() - _play.height())/2);
            pos /= dpr;
            p.drawPixmap(pos, _play);
        }
        p.end();

        _thumb->setPixmap(dest);
    }

    void setHovered(bool v)
    {
        if (_hovered != v) {
            _hovered = v;
            setProperty("hovered", v);
//            setStyleSheet(styleSheet());
        }
    }

    void setCurItemHovered(bool v)
    {
        if (_hovered != v) {
            _hovered = v;
            setProperty("hovered", v);
        }

        if (v) {
            _closeBtn->show();
            _closeBtn->raise();
        }
        else {
            _closeBtn->hide();
        }

        updateClosePosition();
        update();
    }

signals:
    void closeButtonClicked();
    void doubleClicked();

private slots:
    void closeBtnStates(bool bHover) {
        setCurItemHovered(bHover);
    }


protected:
    void updateClosePosition()
    {
        auto margin = 4;
        auto pl = dynamic_cast<QListWidget*>(parentWidget()->parentWidget());
//        if (pl->verticalScrollBar()->isVisible())
//            margin = 10;
        _closeBtn->move(width() - _closeBtn->width() - margin,
                (height() - _closeBtn->height())/2);
    }

    void leaveEvent(QEvent* e) override
    {
        _closeBtn->hide();
        setHovered(false);
    }

    void enterEvent(QEvent* e) override
    {
        _closeBtn->show();
        _closeBtn->raise();

        updateClosePosition();
        setHovered(true);
    }

    bool eventFilter(QObject *obj, QEvent *e) override 
    {
        if (e->type() == QEvent::MouseButtonDblClick) {
            doDoubleClick();
            return true;
        }
        return QWidget::eventFilter(obj, e);
    }

    void resizeEvent(QResizeEvent* re) override
    {
        updateClosePosition();
        _name->setFixedWidth(width()-180);
        updateNameText();
    }

    bool event(QEvent *ee) override
    {
        if(ee->type() == QEvent::Resize) {
            int text_height = _name->document()->size().height();
            _name->setFixedHeight(text_height);
        }

        if (ee->type() == QEvent::Move) {
//            _closeBtn->hide();
//            if (isVisible()) {
//                auto pos = _listWidget->mapFromGlobal(QCursor::pos());
//                auto r = QRect(mapTo(_listWidget, QPoint()), size());
//                if (r.contains(pos)) {
//                    _closeBtn->show();
//                    _closeBtn->raise();
//                }
//            }
        }

        return QFrame::event(ee);
    }

    void updateNameText() 
    {
        _name->setText(utils::ElideText(_pif.mi.title, {width()-242, 36}, QTextOption::NoWrap,
                    _name->font(), Qt::ElideRight, 18, width()-242));
        _name->viewport()->setCursor(Qt::ArrowCursor);
        _name->setCursor(Qt::ArrowCursor);
        _name->document()->setDocumentMargin(0.0);
        int text_height = _name->document()->size().height();
        _name->setFixedHeight(text_height);
    }

    void showEvent(QShowEvent *se) override
    {
        updateNameText();

//        QTimer::singleShot(0, [=]() {
//            auto pos = _listWidget->mapFromGlobal(QCursor::pos());
//            auto r = QRect(mapTo(_listWidget, QPoint()), size());
//            if (r.contains(pos)) {
//                _closeBtn->show();
//                _closeBtn->raise();
//                updateClosePosition();
//            }
//        });

    }

    void mouseDoubleClickEvent(QMouseEvent* me) override
    {
        doDoubleClick();
    }

    void doDoubleClick()
    {
        //FIXME: there is an potential inconsistency with model if pif did changed 
        //(i.e gets deleted).
        _pif.refresh();
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("File does not exist"));
        }
//        setStyleSheet(styleSheet());
        if (!_pif.url.isLocalFile() || _pif.info.exists()) {
            emit doubleClicked();
        }
    }

    void paintEvent(QPaintEvent *pe)
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRectF bgRect;
        bgRect.setSize(size());
        const DPalette pal = QGuiApplication::palette();//this->palette();
//            DPalette pa = DApplicationHelper::instance()->palette(this);
//            pa.setBrush(DPalette::WindowText, pa.color(DPalette::TextWarning));
        DStyleHelper styleHelper;
        QStyleOption option;
        if (!(_index->text().toInt()%2)){

//            QColor fillColor = styleHelper.getColor(static_cast<const QStyleOption *>(&option), pa, DPalette::ItemBackground);
//            painter.setBrush(QBrush(fillColor));
//            painter->fillPath(path, fillColor);
//            QColor bgColor(0,0,0,8);
            QColor bgColor  = pal.color(DPalette::AlternateBase);

            QPainterPath pp;
            pp.addRoundedRect(bgRect, 8, 8);
            painter.fillPath(pp, bgColor);

        }
        if(_hovered){
            DPalette pa = DApplicationHelper::instance()->palette(this);
            pa.setBrush(DPalette::Text, pa.color(DPalette::Highlight));
            QColor bgColor(255, 255, 255, 255*0.1);
            if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ){
                bgColor = QColor(0, 0, 0, 255*0.1);
            }

            QPainterPath pp;
            pp.addRoundedRect(bgRect, 8, 8);
            painter.fillPath(pp, bgColor);

        }
        if (state() == ItemState::Playing){
            DPalette pa = DApplicationHelper::instance()->palette(this);
            pa.setBrush(DPalette::Text, pa.color(DPalette::Highlight));
//            setPalette(pa);
            _name->setPalette(pa);
            _index->setPalette(pa);
            _time->setPalette(pa);
            _name->setFontWeight(QFont::Weight::Medium);
//            QColor bgColor  = pal.color(DPalette::ToolTipBase);
            QColor bgColor(255,255,255,51);
            if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ){
                bgColor = QColor(0,0,0,51);
            }

            QPainterPath pp;
            pp.addRoundedRect(bgRect, 8, 8);
            painter.fillPath(pp, bgColor);
        }else {
            DPalette pa_name = DApplicationHelper::instance()->palette(_name);
            pa_name.setBrush(DPalette::Text, pa_name.color(DPalette::ToolTipText));
            _name->setPalette(pa_name);
            _name->setFontWeight(QFont::Weight::Normal);
            DPalette pa = DApplicationHelper::instance()->palette(_index);
            pa.setBrush(DPalette::Text, pa.color(DPalette::TextTips));
            _index->setPalette(pa);
            _time->setPalette(pa);
        }

        QWidget::paintEvent(pe);
    }

private:
    QString _bg;
    QLabel *_index;
    ListPic *_thumb;
    QTextEdit *_name;
    QLabel *_time;
    QPixmap _play;
    PlayItemInfo _pif;
    //FloatingButton *_closeBtn;
    DFloatingButton *_closeBtn;
    QListWidget *_listWidget {nullptr};
    bool _hovered {false};
    PlaylistWidget *_playlist{nullptr};
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
                
                if (mw->insideResizeArea(me->globalPos()))
                    return false;

                if (plw->state() == PlaylistWidget::Opened && !plw->underMouse()) {
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
    :QWidget(mw), _engine(mpv), _mw(static_cast<MainWindow*>(mw))
{
//    DThemeManager::instance()->registerWidget(this);

    bool composited = CompositingManager::get().composited();
    setAttribute(Qt::WA_TranslucentBackground, false);
    //NOTE: set fixed will affect geometry animation
    //setFixedWidth(220);

    auto *mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(0, 0, 0, 0);
    mainVLayout->setSpacing(0);
    setLayout(mainVLayout);
    auto *mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(10, 0, 16, 0);
    mainLayout->setSpacing(10);
    mainLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
//    setLayout(mainLayout);
    QWidget *topspec = new QWidget;
    topspec->setFixedHeight(30);
    mainVLayout->addWidget(topspec);
    mainVLayout->addLayout(mainLayout);

    QWidget *left = new QWidget();
//    left->setFrameRect(QRect(0,0,197,288));
    left->setFixedSize(197,288);
    left->setContentsMargins(0,0,0,0);
    left->setAttribute(Qt::WA_TranslucentBackground, false);

//    left->setFrameShape(QFrame::NoFrame);
    left->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));

//    left->move(0,0);
    _title = new QLabel();
    _title->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T3));
//    DFontSizeManager::instance()->get(DFontSizeManager::T9);
//    title->setProperty("Name", true);
//    title->setReadOnly(true);
//    title->setAcceptRichText(false);
//    title->setWordWrapMode(QTextOption::WrapAnywhere);
//    title->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    title->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
//    title->setFrameShape(QFrame::NoFrame);
//    title->setTextInteractionFlags(Qt::NoTextInteraction);
    DPalette pa = DApplicationHelper::instance()->palette(_title);
    pa.setBrush(DPalette::WindowText, pa.color(DPalette::ToolTipText));
    _title->setPalette(pa);
//    title->setText(DApplication::translate("QuickInstallWindow", "Installed"));
    _title->setText(tr("播放列表"));
    _title->setFixedSize(96,33);
    _title->setContentsMargins(0,0,0,0);

    _num = new QLabel();
    DPalette pa_num = DApplicationHelper::instance()->palette(_num);
    pa_num.setBrush(DPalette::WindowText, pa_num.color(DPalette::TextTips));
    _num->setPalette(pa_num);
    _num->setText(tr("17个视频"));
    _num->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    _num->setFixedSize(96,36);
    _num->setContentsMargins(0,0,0,0);
//    title->setFont(QFont());
    mainLayout->addWidget(left);
    auto *leftinfo = new QVBoxLayout;
    leftinfo->setContentsMargins(0, 0, 0, 0);
    leftinfo->setSpacing(0);
    left->setLayout(leftinfo);
    leftinfo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    leftinfo->addWidget(_title);
    leftinfo->addWidget(_num);
//    DPushButton *clearButton = new DPushButton(QIcon::fromTheme("dcc_clearlist"),tr("清空列表"),nullptr);
    DPushButton *clearButton = new DPushButton();
    clearButton->setIcon(QIcon::fromTheme("dcc_clearlist"));
    clearButton->setText(tr("清空列表"));
    clearButton->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));
//    clearButton->setText(tr("清空列表"));
    DPalette pa_cb = DApplicationHelper::instance()->palette(clearButton);
    if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ){
        pa_cb.setBrush(QPalette::Light, QColor(100,100,100,255));
        pa_cb.setBrush(QPalette::Dark, QColor(92,92,92,255));
    } else {
        pa_cb.setBrush(QPalette::Light, QColor(85,84,84,255));
        pa_cb.setBrush(QPalette::Dark, QColor(65,65,65,255));
    }
    pa_cb.setBrush(QPalette::ButtonText, QColor(255,255,255,255));
    clearButton->setPalette(pa_cb);
    clearButton->setFixedSize(93,30);
    clearButton->setContentsMargins(0,0,0,0);

    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::paletteTypeChanged,clearButton,
                         [=] (DGuiApplicationHelper::ColorType type) {
            DPalette pa_cb = DApplicationHelper::instance()->palette(clearButton);
            if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ){
                pa_cb.setBrush(QPalette::Light, QColor(100,100,100,255));
                pa_cb.setBrush(QPalette::Dark, QColor(92,92,92,255));
            } else {
                pa_cb.setBrush(QPalette::Light, QColor(85,84,84,255));
                pa_cb.setBrush(QPalette::Dark, QColor(65,65,65,255));
            }
            clearButton->setPalette(pa_cb);
        });

    leftinfo->addWidget(clearButton);
    connect(clearButton,&QPushButton::clicked,this, [=]{
        _engine->clearPlaylist();
    });
    left->setContentsMargins(36, 0, 0, 0);
    _title->setContentsMargins(0, 0, 0, 0);
    clearButton->setContentsMargins(0, 0, 0, 0);
    _num->setContentsMargins(0, 0, 0, 0);


    auto *vl = new QVBoxLayout;
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(0);
//    mainLayout->addLayout(vl, 3);
    QWidget *right= new QWidget();
    auto *rightinfo = new QVBoxLayout;
    rightinfo->setContentsMargins(0, 0, 0, 0);
    rightinfo->setSpacing(0);
    right->setLayout(rightinfo);
    right->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(right);

    _playlist = new DListWidget();
//    _playlist->setFixedSize(820,288);
    _playlist->setFixedSize(width()-235,288);
//    _playlist->setFixedHeight(288);
    _playlist->setContentsMargins(0, 30, 0, 0);
    _playlist->viewport()->setAutoFillBackground(false);
    _playlist->setAutoFillBackground(false);

    rightinfo->addWidget(_playlist);
    _playlist->setAttribute(Qt::WA_TranslucentBackground, false);
    _playlist->setFrameShape(QFrame::NoFrame);
    _playlist->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));

    _playlist->setSelectionMode(QListView::NoSelection);
    _playlist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _playlist->setResizeMode(QListView::Adjust);
    _playlist->setDragDropMode(QListView::InternalMove);
    _playlist->setSpacing(0);

    //setAcceptDrops(true);
    _playlist->viewport()->setAcceptDrops(true);
    _playlist->setDragEnabled(true);

    _playlist->setContentsMargins(0, 30, 0, 0);

    if (!composited) {
        _playlist->setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        _playlist->setAttribute(Qt::WA_NativeWindow);
    }

#ifndef USE_DXCB
    auto *mwl = new MainWindowListener(this);
    mw->installEventFilter(mwl);
#endif

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
                for (int i = 0; i < _playlist->count(); i++) {
                    if (w == _playlist->itemWidget(_playlist->item(i))) {
                        args << i;
                        _mw->requestAction(ActionFactory::ActionKind::GotoPlaylistSelected,
                                false, args);
                        togglePopup();
                        break;
                    }
                }
            });
    }

    connect(&_engine->playlist(), &PlaylistModel::emptied, this, &PlaylistWidget::clear);
    connect(&_engine->playlist(), &PlaylistModel::itemsAppended, this, &PlaylistWidget::appendItems);
    connect(&_engine->playlist(), &PlaylistModel::itemRemoved, this, &PlaylistWidget::removeItem);
    connect(&_engine->playlist(), &PlaylistModel::currentChanged, this, &PlaylistWidget::updateItemStates);
    connect(&_engine->playlist(), &PlaylistModel::itemInfoUpdated, this, &PlaylistWidget::updateItemInfo);

    QTimer::singleShot(10, this, &PlaylistWidget::loadPlaylist);

    connect(ActionFactory::get().playlistContextMenu(), &DMenu::aboutToShow, [=]() {
        QTimer::singleShot(20, [=]() {
            if (_mouseItem) {
                _clickedItem = _mouseItem;
                ((PlayItemWidget*)_mouseItem)->setHovered(true); 
            }
        });
    });
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::aboutToHide, [=]() {
        if (_mouseItem) {
            ((PlayItemWidget*)_mouseItem)->setHovered(false); 
        }
    });

    connect(_playlist->model(), &QAbstractItemModel::rowsMoved, [=]() {
        if (_lastDragged.first >= 0) {
            int target = -1;
            for (int i = 0; i < _playlist->count(); i++) {
                auto piw = dynamic_cast<PlayItemWidget*>(_playlist->itemWidget(_playlist->item(i)));
                if (piw == _lastDragged.second) {
                    target = i;
                    break;
                }
            }
            qDebug() << "swap " << _lastDragged.first << target;
            if (target >= 0 && _lastDragged.first != target) {
                _engine->playlist().switchPosition(_lastDragged.first, target);
                _lastDragged = {-1, nullptr};
            }
        }
    });
}

PlaylistWidget::~PlaylistWidget()
{
}
void PlaylistWidget::clear()
{
    _playlist->clear();
    QString s=QString(" %1 个视频").arg(_playlist->count());
    _num->setText(s);
}
void PlaylistWidget::updateItemInfo(int id)
{
    auto piw = dynamic_cast<PlayItemWidget*>(_playlist->itemWidget(_playlist->item(id)));
    piw->updateInfo(_engine->playlist().items()[id]);
}

void PlaylistWidget::updateItemStates()
{
    qDebug() << __func__ << _playlist->count() << "current = " << _engine->playlist().current();
    for (int i = 0; i < _playlist->count(); i++) {
        auto piw = dynamic_cast<PlayItemWidget*>(_playlist->itemWidget(_playlist->item(i)));

        auto old = piw->state();
        piw->setState(ItemState::Normal);
        if (!piw->_pif.valid) {
            piw->setState(ItemState::Invalid);
        }

        if (i == _engine->playlist().current()) {
            if (piw->state() != ItemState::Playing) {
                _playlist->scrollToItem(_playlist->item(i));
                piw->setState(ItemState::Playing);
            }
        }

        if (old != piw->state()) {
//            piw->setStyleSheet(piw->styleSheet());
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
        for (int i = 0; i < _playlist->count(); i++) {
            if (_clickedItem == _playlist->itemWidget(_playlist->item(i))) {
                _engine->playlist().remove(i);
                break;
            }
        }
    }
}

void PlaylistWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    auto md = ev->mimeData();
    qDebug() << md->formats();
    if (md->formats().contains("application/x-qabstractitemmodeldatalist")) {
        if (!_playlist->selectedItems().contains(_playlist->itemAt(ev->pos()))) {
            _playlist->setDropIndicatorShown(true);
        }
        QWidget::dragEnterEvent(ev);
        return;
    }

    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void PlaylistWidget::dragMoveEvent(QDragMoveEvent *ev)
{
    auto md = ev->mimeData();
    if (md->formats().contains("application/x-qabstractitemmodeldatalist")) {
        if (!_playlist->selectedItems().contains(_playlist->itemAt(ev->pos()))) {
            _playlist->setDropIndicatorShown(true);
        }
        QWidget::dragMoveEvent(ev);
        return;
    }

    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void PlaylistWidget::dropEvent(QDropEvent *ev)
{
    auto md = ev->mimeData();
    if (md->formats().contains("application/x-qabstractitemmodeldatalist")) {
        _playlist->setDropIndicatorShown(false);
        auto encoded = md->data("application/x-qabstractitemmodeldatalist");
        QDataStream stream(&encoded, QIODevice::ReadOnly);

        QList<int> l;
        while (!stream.atEnd()) {
            int row, col;
            QMap<int,  QVariant> roleDataMap;
            stream >> row >> col >> roleDataMap;
            auto piw = dynamic_cast<PlayItemWidget*>(_playlist->itemWidget(_playlist->item(row)));
            _lastDragged = qMakePair(row, piw);
            qDebug() << "drag to move " << row << piw->_pif.url;
        }

        QWidget::dropEvent(ev);
        return;
    }

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
    QPoint itempos(cme->pos().x() - 235,cme->pos().y()-30);

    if (_playlist->itemAt(itempos)) {
        _mouseItem = _playlist->itemWidget(_playlist->itemAt(itempos));
        on_item = true;
    }

    auto piw = dynamic_cast<PlayItemWidget*>(_mouseItem);
    auto menu = ActionFactory::get().playlistContextMenu();
    for (auto act: menu->actions()) {
        auto prop = (ActionFactory::ActionKind)act->property("kind").toInt();
        bool on = true;
        if (prop == ActionFactory::ActionKind::PlaylistOpenItemInFM ||
                prop == ActionFactory::ActionKind::PlaylistItemInfo) {
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

void PlaylistWidget::removeItem(int idx)
{
    qDebug() << "idx = " << idx;
    auto item = this->_playlist->takeItem(idx);
    if (item) {
        delete item;
    }

    this->_playlist->update();
    for (int i = 0;i < _playlist->count();i++){
        QWidget *item =_playlist->itemWidget(_playlist->item(i));
        if(item){
            (dynamic_cast<PlayItemWidget*>(item))->setIndex(i);
        }
    }

    if (_playlist->count() != 0 && _playlist->count() != idx) {
        QWidget *item = _playlist->itemWidget(_playlist->item(idx));
        PlayItemWidget *curItem = dynamic_cast<PlayItemWidget*>(item);
        curItem->setCurItemHovered(true);
    }
    else if (_playlist->count() != 0 && _playlist->count() == idx) {
        QWidget *item = _playlist->itemWidget(_playlist->item(--idx));
        PlayItemWidget *curItem = dynamic_cast<PlayItemWidget*>(item);
        curItem->setCurItemHovered(true);
    }

    QString s=QString(" %1 个视频").arg(_playlist->count());
    _num->setText(s);
}

void PlaylistWidget::appendItems()
{
    qDebug() << __func__;

    auto items = _engine->playlist().items();
    auto p = items.begin() + this->_playlist->count();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this->_playlist,p - items.begin() ,this);

        auto item = new QListWidgetItem;
        _playlist->addItem(item);
        _playlist->setItemWidget(item, w);

        connect(w, SIGNAL(closeButtonClicked()), _closeMapper, SLOT(map()));
        connect(w, SIGNAL(doubleClicked()), _activateMapper, SLOT(map()));
        _closeMapper->setMapping(w, w);
        _activateMapper->setMapping(w, w);
        ++p;
    }
    QString s=QString(" %1 个视频").arg(_playlist->count());
    _num->setText(s);
    batchUpdateSizeHints();
    updateItemStates();
//    _playlist->setStyleSheet(styleSheet());
//    setStyleSheet(styleSheet());
}

void PlaylistWidget::loadPlaylist()
{
    qDebug() << __func__;
    _playlist->clear();


    auto items = _engine->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this->_playlist,p-items.begin(),this);
        auto item = new QListWidgetItem;
        _playlist->addItem(item);
        _playlist->setItemWidget(item, w);

        connect(w, SIGNAL(closeButtonClicked()), _closeMapper, SLOT(map()));
        connect(w, SIGNAL(doubleClicked()), _activateMapper, SLOT(map()));
        _closeMapper->setMapping(w, w);
        _activateMapper->setMapping(w, w);
        ++p;
    }

    batchUpdateSizeHints();
    updateItemStates();
//    _playlist->setStyleSheet(styleSheet());
    QString s=QString(" %1 个视频").arg(_playlist->count());
    _num->setText(s);
//    setStyleSheet(styleSheet());
}

void PlaylistWidget::batchUpdateSizeHints()
{
    if (isVisible()) {
        for (int i = 0; i < this->_playlist->count(); i++) {
            auto item = this->_playlist->item(i);
            auto w = this->_playlist->itemWidget(item);
            auto t = w->size();
            item->setSizeHint(w->size());
        }
    }
}

void PlaylistWidget::togglePopup()
{
    auto main_rect = _mw->rect();
#ifdef USE_DXCB
    auto view_rect = main_rect;
#else
    auto view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));
#endif
    int off = _mw->isFullScreen()? 0: _mw->titlebar()->geometry().bottom();
//    QRect fixed(0, off,
//            PLAYLIST_FIXED_WIDTH,
//            _mw->toolbox()->geometry().top() + TOOLBOX_TOP_EXTENT - off);
    QRect fixed((10), (view_rect.height()-394),
            view_rect.width()-20,
            (384 - 70));
//    fixed.moveRight(view_rect.right());
    QRect shrunk = fixed;
//    shrunk.setWidth(0);
    shrunk.setHeight(0);
//    shrunk.moveRight(fixed.right());
    shrunk.moveBottom(fixed.bottom());

    if (_toggling) return;

    if (_state == State::Opened) {
        Q_ASSERT(isVisible());

        _toggling = true;
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setEasingCurve(QEasingCurve::InOutCubic);
        pa->setDuration(POPUP_DURATION);
        pa->setStartValue(fixed);
        pa->setEndValue(shrunk);;

//        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
            setVisible(!isVisible());
            _toggling = false;
            _state = State::Closed;
            emit stateChange();
        });
        setVisible(!isVisible());
        _toggling = false;
        _state = State::Closed;
        emit stateChange();
    } else {
        setVisible(!isVisible());
        _toggling = true;
        QPropertyAnimation *pa = new QPropertyAnimation(this, "geometry");
        pa->setEasingCurve(QEasingCurve::InOutCubic);
        pa->setDuration(POPUP_DURATION);
        pa->setStartValue(shrunk);
        pa->setEndValue(fixed);

//        pa->start();
        connect(pa, &QPropertyAnimation::finished, [=]() {
            pa->deleteLater();
            _toggling = false;
            _state = State::Opened;
            emit stateChange();
        });
        _toggling = false;
        _state = State::Opened;
        emit stateChange();
    }
}

void PlaylistWidget::paintEvent(QPaintEvent *pe)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    QColor bgColor = pal.color(QPalette::ToolTipBase);

    QPainterPath pp;
    pp.addRoundedRect(bgRect, 18, 18);
//    painter.fillPath(pp, bgColor);

//    {
//        auto view_rect = bgRect.marginsRemoved(QMargins(1, 1, 1, 1));
//        QPainterPath pp;
//        pp.addRoundedRect(view_rect, RADIUS, RADIUS);
//        painter.fillPath(pp, bgColor);
//    }
    if(_title && _num){
        DPalette pa = DApplicationHelper::instance()->palette(_title);
        pa.setBrush(DPalette::WindowText, pa.color(DPalette::ToolTipText));
        _title->setPalette(pa);

        DPalette pa_num = DApplicationHelper::instance()->palette(_num);
        pa_num.setBrush(DPalette::WindowText, pa_num.color(DPalette::TextTips));
        _num->setPalette(pa_num);
    }


    QWidget::paintEvent(pe);
}

void PlaylistWidget::resizeEvent(QResizeEvent *ev)
{
    auto main_rect = _mw->rect();
#ifdef USE_DXCB
    auto view_rect = main_rect;
#else
    auto view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));
#endif
    int off = _mw->isFullScreen()? 0: _mw->titlebar()->geometry().bottom();
//    QRect fixed(0, off,
//            PLAYLIST_FIXED_WIDTH,
//            _mw->toolbox()->geometry().top() + TOOLBOX_TOP_EXTENT - off);
    QRect fixed((view_rect.width()-10), (view_rect.height()-394),
            view_rect.width()-20,
            (384 - 70));
    _playlist->setFixedWidth(width()-235);
    emit sizeChange();

    QTimer::singleShot(100, this, &PlaylistWidget::batchUpdateSizeHints);
}

}

#include "playlist_widget.moc"
