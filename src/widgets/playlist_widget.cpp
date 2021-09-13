/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: liuzheng <liuzheng@uniontech.com>
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
#include "../accessibility/ac-deepin-movie-define.h"

#define PLAYLIST_FIXED_WIDTH 800
#define POPUP_DURATION 350

namespace dmr {

/*QString splitText(const QString &text, int width,
                  QTextOption::WrapMode wordWrap, const QFont &font, int lineHeight)
{
    int height = 0;

    QTextLayout textLayout(text);
    QString str;

    textLayout.setFont(font);
    const_cast<QTextOption *>(&textLayout.textOption())->setWrapMode(wordWrap);

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

        if (line.isValid())
            str.append("\n");
    }

    textLayout.endLayout();

    return str;
}*/

class PlayItemTooltipHandler: public QObject
{
public:
    explicit PlayItemTooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        switch (event->type()) {
        case QEvent::ToolTip: {
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            auto item = tip->property("for").value<QWidget *>();
            auto lb = tip->findChild<DLabel *>("TipText");
            lb->setAlignment(Qt::AlignLeft);

//            auto msg = splitText(item->toolTip(), 200, QTextOption::WordWrap, lb->font(),
//                                 lb->fontMetrics().height());
//            lb->setText(msg);
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
            auto tip = obj->property("HintWidget").value<Tip *>();
            tip->hide();
            event->ignore();
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(obj, event); // standard event processing
    }
};

enum ItemState {
    Normal,
    Playing,
    Invalid, // gets deleted or similar
};

class PlayItemWidget: public QFrame
{
    Q_OBJECT
//    Q_PROPERTY(QString bg READ getBg WRITE setBg DESIGNABLE true)
public:
    friend class PlaylistWidget;

    PlayItemWidget(const PlayItemInfo &pif, QListWidget *list = nullptr, int index = 0, PlaylistWidget *parent = nullptr)
        : QFrame(), _pif {pif}, _listWidget {list}, _playlist{parent}
    {
        _thumb = nullptr;
        m_pSvgWidget = nullptr;
        setProperty("PlayItemThumb", "true");
        //setState(ItemState::Normal);
        setFrameShape(QFrame::NoFrame);
        this->setObjectName(PLAYITEM_WIDGET);
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

        setFixedSize(_playlist->width() - 230, 36);
        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(17, 0, 0, 0);
        l->setSpacing(10);
        setLayout(l);

        _index = new DLabel(this);
        DFontSizeManager::instance()->bind(_index, DFontSizeManager::T9);
        _index->setText(QString::number(index + 1));
        _index->setFixedWidth(22);
        l->addWidget(_index);

        bool bDarkTheme = false;
        if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            bDarkTheme = true;
        }

        if (_pif.thumbnail.isNull() && _pif.thumbnail_dark.isNull()) {
            if (bDarkTheme) {
                m_pSvgWidget = new QSvgWidget(QString(":/resources/icons/music-dark.svg"), this);
            } else {
                m_pSvgWidget = new QSvgWidget(QString(":/resources/icons/music-light.svg"), this);
            }
            m_pSvgWidget->setFixedSize(42, 24);
        } else {
            if (bDarkTheme) {
                _thumb = new ListPic(_pif.thumbnail_dark.scaled(QSize(42, 24)), this);
            } else {
                _thumb = new ListPic(_pif.thumbnail.scaled(QSize(42, 24)), this);
            }
        }

        if (_thumb) {
            l->addWidget(_thumb);
        } else {
            l->addWidget(m_pSvgWidget);
        }
        QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &PlayItemWidget::slotThemeTypeChanged);


        auto *vl = new QHBoxLayout;
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        l->addLayout(vl);

//        vl->addStretch();

        _name = new DLabel(this);
        _name->setProperty("Name", true);
        _name->setFrameShape(QFrame::NoFrame);
        _name->setTextInteractionFlags(Qt::NoTextInteraction);
        _name->setFixedWidth(width() - 180);
        _name->installEventFilter(this);
        _name->setAutoFillBackground(false);

        vl->addWidget(_name);

        _time = new DLabel(this);
        DFontSizeManager::instance()->bind(_time, DFontSizeManager::T9);
        _time->setProperty("Time", true);
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("The file does not exist"));
        }
        vl->addStretch();
        l->addWidget(_time);
        l->addSpacing(10);

        _closeBtn = new DFloatingButton(DStyle::SP_CloseButton, this);
        _closeBtn->setFocusPolicy(Qt::NoFocus);
        _closeBtn->setObjectName(PLAYITEN_CLOSE_BUTTON);
        _closeBtn->setAccessibleName(PLAYITEN_CLOSE_BUTTON);
        _closeBtn->setIconSize(QSize(28, 28));
        _closeBtn->setFixedSize(25, 25);
        _closeBtn->hide();
        connect(_closeBtn, &DFloatingButton::clicked, this, &PlayItemWidget::closeButtonClicked);

        setToolTip(_pif.mi.title);
        auto th = new PlayItemTooltipHandler(this);
        auto t = new Tip(QPixmap(), _pif.mi.title, nullptr);
        t->setWindowFlags(Qt::ToolTip | Qt::CustomizeWindowHint);
        t->setText(_pif.mi.title);
        t->resetSize(QApplication::desktop()->availableGeometry().width());
        t->setAttribute(Qt::WA_TranslucentBackground);
        t->setProperty("for", QVariant::fromValue<QWidget *>(this));
        t->layout()->setContentsMargins(5, 10, 5, 10);
        t->hide();
        setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        installEventFilter(th);
        connect(_playlist, &PlaylistWidget::sizeChange, this, &PlayItemWidget::slotSizeChange);

        m_opacityEffect = new QGraphicsOpacityEffect;
        _time->setGraphicsEffect(m_opacityEffect);
        m_opacityEffect_1 = new QGraphicsOpacityEffect;
        _index->setGraphicsEffect(m_opacityEffect_1);
        setState(ItemState::Normal);
    }
    
    ~PlayItemWidget() override
    {
        if (m_pSvgWidget) {
            delete  m_pSvgWidget;
            m_pSvgWidget = nullptr;
        }

        if (_thumb) {
            delete _thumb;
            _thumb = nullptr;
        }
    }

    void updateInfo(const PlayItemInfo &pif)
    {
        _pif = pif;
        _time->setText(_pif.mi.durationStr());
        setToolTip(_pif.mi.title);
        updateNameText();

        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("The file does not exist"));
        }
        update();
    }

    void setState(ItemState is)
    {
        setProperty("ItemState", is);

        if (state() == ItemState::Playing) {
            DPalette pa = DApplicationHelper::instance()->palette(this);
            pa.setBrush(DPalette::Text, pa.color(DPalette::Highlight));

            if (!m_bIsSelect) {
                _name->setForegroundRole(DPalette::Highlight);
                _index->setForegroundRole(DPalette::Highlight);
                _time->setForegroundRole(DPalette::Highlight);
            } else {
                _name->setForegroundRole(DPalette::ToolTipText);
                _index->setForegroundRole(DPalette::BrightText);
                _time->setForegroundRole(DPalette::BrightText);
            }

            m_opacityEffect_1->setOpacity(1.0);
            m_opacityEffect->setOpacity(1.0);
            DFontSizeManager::instance()->bind(_name, DFontSizeManager::T6, QFont::Medium);
            DFontSizeManager::instance()->bind(_index, DFontSizeManager::T6, QFont::Medium);
            DFontSizeManager::instance()->bind(_time, DFontSizeManager::T6, QFont::Medium);

        } else if (state() == ItemState::Normal) {
            _name->setForegroundRole(DPalette::ToolTipText);
            _index->setForegroundRole(DPalette::BrightText);
            _time->setForegroundRole(DPalette::BrightText);
            m_opacityEffect->setOpacity(0.5);

            if (m_bIsSelect) {
                m_opacityEffect_1->setOpacity(1.0);
            } else {
                m_opacityEffect_1->setOpacity(0.5);
            }

            DFontSizeManager::instance()->bind(_name, DFontSizeManager::T6, QFont::Normal);
            DFontSizeManager::instance()->bind(_index, DFontSizeManager::T6, QFont::Normal);
            DFontSizeManager::instance()->bind(_time, DFontSizeManager::T6, QFont::Normal);
        }
    }

    ItemState state() const
    {
        return static_cast<ItemState>(property("ItemState").toInt());
    }
    void setIndex(int index)
    {
        _index->setText(QString::number(index + 1));
    }

    void setHovered(bool v)
    {
        if (_hovered != v) {
            _hovered = v;
            setProperty("hovered", v);
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
        } else {
            _closeBtn->hide();
        }

        updateClosePosition();
        update();
    }

    bool getBIsSelect() const
    {
        return m_bIsSelect;
    }

    void setBIsSelect(bool bIsSelect)
    {
        m_bIsSelect = bIsSelect;

        if (m_bIsSelect) {
            _time->hide();
            _closeBtn->show();
            _closeBtn->raise();
            QPalette pe;
            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                pe.setColor(QPalette::ToolTipText, Qt::white);
                _name->setPalette(pe);
                pe.setColor(QPalette::BrightText, Qt::white);
                _index->setPalette(pe);
            } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
                pe.setColor(QPalette::ToolTipText, Qt::white);
                _name->setPalette(pe);
                pe.setColor(QPalette::BrightText, Qt::white);
                _index->setPalette(pe);
            }
        } else {
            _time->show();
            _closeBtn->hide();
            QPalette pe;
            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                pe.setColor(QPalette::ToolTipText, Qt::black);
                _name->setPalette(pe);
                pe.setColor(QPalette::BrightText, Qt::black);
                _index->setPalette(pe);
            } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
                pe.setColor(QPalette::ToolTipText, Qt::white);
                _name->setPalette(pe);
                pe.setColor(QPalette::BrightText, Qt::white);
                _index->setPalette(pe);
            }
        }
    }

    void doDoubleClick()
    {
        //FIXME: there is an potential inconsistency with model if pif did changed
        //(i.e gets deleted).
        _pif.refresh();
        _time->setText(_pif.mi.durationStr());
        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _time->setText(tr("The file does not exist"));
        }
        if (!_pif.url.isLocalFile() || _pif.info.exists()) {
            emit doubleClicked();
        }
    }

signals:
    void closeButtonClicked();
    void doubleClicked();

private slots:
    void slotThemeTypeChanged()
    {
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            if (_thumb) {
                _thumb->setPic(_pif.thumbnail);
            } else {
                m_pSvgWidget->load(QString(":/resources/icons/music-light.svg"));
            }
        };
        if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            if (_thumb) {
                _thumb->setPic(_pif.thumbnail_dark);
            } else {
                m_pSvgWidget->load(QString(":/resources/icons/music-dark.svg"));
            }
        }
    }
    void slotSizeChange()
    {
        setFixedWidth(_playlist->width() - 230);
    }

protected:
    void updateClosePosition()
    {
        auto margin = 10;
        _closeBtn->move(width() - _closeBtn->width() - margin,
                        (height() - _closeBtn->height()) / 2);
    }
    void leaveEvent(QEvent *e) override
    {
        _closeBtn->hide();
        setHovered(false);

        QFrame::leaveEvent(e);
    }
    void enterEvent(QEvent *e) override
    {
        _closeBtn->show();
        _closeBtn->raise();

        updateClosePosition();
        setHovered(true);

        QFrame::enterEvent(e);
    }
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick) {
            doDoubleClick();
            return true;
        }
        return QWidget::eventFilter(obj, e);
    }
    void resizeEvent(QResizeEvent *re) override
    {
        updateClosePosition();
        _name->setFixedWidth(width() - 180);
        updateNameText();

        QFrame::resizeEvent(re);
    }
    bool event(QEvent *ee) override
    {
        if (ee->type() == QEvent::Resize) {
            _name->setFixedHeight(36);
        }

        if (ee->type() == QEvent::Move) {
        }

        return QFrame::event(ee);
    }
    void updateNameText()
    {
        _name->setText(utils::ElideText(_pif.mi.title, {width() - 242, 36}, QTextOption::NoWrap,
                                        _name->font(), Qt::ElideRight, 18, width() - 242));
        _name->setCursor(Qt::ArrowCursor);
        _name->setFixedHeight(36);
    }
    void showEvent(QShowEvent *se) override
    {
        updateNameText();
        QFrame::showEvent(se);
    }
    void mouseDoubleClickEvent(QMouseEvent *me) override
    {
        doDoubleClick();

        QFrame::mouseDoubleClickEvent(me);
    }
    void paintEvent(QPaintEvent *pe) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRectF bgRect;
        bgRect.setSize(size());
        const DPalette pal = QGuiApplication::palette();
        DStyleHelper styleHelper;
        QStyleOption option;

        if (!(_index->text().toInt() % 2)) {
            QColor bgColor;
            if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType) {
                bgColor = QColor(255, 255, 255);
                bgColor.setAlphaF(0.05);
            } else {
                bgColor = pal.color(DPalette::AlternateBase);
            }
            QPainterPath pp;
            pp.addRoundedRect(bgRect, 8, 8);
            painter.fillPath(pp, bgColor);
        }

        if (_hovered) {
            DPalette pa = DApplicationHelper::instance()->palette(this);
            pa.setBrush(DPalette::Text, pa.color(DPalette::Highlight));
            QColor bgColor(255, 255, 255, static_cast<int>(255 * 0.05));
            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                bgColor = QColor(0, 0, 0, static_cast<int>(255 * 0.05));
            }

            QPainterPath pp;
            pp.addRoundedRect(bgRect, 8, 8);
            painter.fillPath(pp, bgColor);

        }

        if (!_pif.valid) {
            setState(ItemState::Invalid);
            _name->setForegroundRole(DPalette::TextTips);
            _time->setText(tr("The file does not exist"));
        }

        QFrame::paintEvent(pe);
    }

private:
    QString _bg;
    DLabel *_index;
    ListPic *_thumb;
    QSvgWidget *m_pSvgWidget;
    DLabel *_name;
    DLabel *_time;
    QPixmap _play;
    PlayItemInfo _pif;
    //FloatingButton *_closeBtn;
    DFloatingButton *_closeBtn;
    QListWidget *_listWidget {nullptr};
    bool _hovered {false};
    PlaylistWidget *_playlist{nullptr};
    bool m_bIsSelect = false;
    QGraphicsOpacityEffect *m_opacityEffect {nullptr};
    QGraphicsOpacityEffect *m_opacityEffect_1 {nullptr};
};

class MainWindowListener: public QObject
{
public:
    explicit MainWindowListener(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);

            if (me->buttons() == Qt::LeftButton) {
                if (!m_bClicked) {
                    m_bClicked = true;
                    m_lastPoint = me->globalPos();

                    QTimer::singleShot(200, this, [ = ] {
                        if (!m_bClicked)
                        {
                            return;
                        }
                        m_bClicked = false;
                        auto *plw = dynamic_cast<PlaylistWidget *>(parent());
                        auto *mw = dynamic_cast<MainWindow *>(plw->parent());

                        if (mw->insideResizeArea(m_lastPoint))
                            return;
                        if (plw->state() == PlaylistWidget::Opened && !plw->underMouse())
                        {
                            mw->requestAction(ActionFactory::ActionKind::TogglePlaylist);
                            mw->set_playlistopen_clicktogglepause(true);
                        }
                    });
                }
            }
            return false;
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            m_bClicked = false;
            return QObject::eventFilter(obj, event);
        }/* else if (event->type() == QEvent::KeyRelease) { //这段代码会导致Enter播放列表后，播放列表关闭
            QKeyEvent *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                auto *plw = dynamic_cast<PlaylistWidget *>(parent());

                if (plw->state() == PlaylistWidget::State::Opened) {
                    DListWidget *playlist = plw->get_playlist();
                    for (int loop = 0; loop < playlist->count(); loop++) {
                        auto piw = dynamic_cast<PlayItemWidget *>(playlist->itemWidget(playlist->item(loop)));
                        if (piw->getBIsSelect()) {
                            piw->doubleClicked();
                            return false;
                        }
                    }

                }
            }
            return false;
        }*/ else {
            // standard event processing
            return QObject::eventFilter(obj, event);
        }
    }

private:
    bool m_bClicked {false};
    QPoint m_lastPoint;
};

PlaylistWidget::PlaylistWidget(QWidget *mw, PlayerEngine *mpv)
    : QWidget(mw), _engine(mpv), _mw(static_cast<MainWindow *>(mw))
{
    bool composited = CompositingManager::get().composited();
    setAttribute(Qt::WA_TranslucentBackground, false);
    //NOTE: set fixed will affect geometry animation
    this->setObjectName(PLAYLIST_WIDGET);

    paOpen = nullptr;
    paClose = nullptr;
    pSelectItemWgt = nullptr;

    QVBoxLayout *mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(0, 0, 0, 0);
    mainVLayout->setSpacing(0);
    setLayout(mainVLayout);

    QHBoxLayout *mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(10, 0, 10, 0);
    mainLayout->setSpacing(0);
    mainLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QWidget *topspec = new QWidget;
    topspec->setFixedHeight(20);

    mainVLayout->addWidget(topspec);
    mainVLayout->addLayout(mainLayout);

    QWidget *left = new QWidget();
    left->setObjectName(LEFT_WIDGET);
    left->setFixedSize(197, 288);
    left->setContentsMargins(0, 0, 0, 0);
    left->setAttribute(Qt::WA_TranslucentBackground, false);
    left->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred));

    _title = new DLabel();
    DFontSizeManager::instance()->bind(_title, DFontSizeManager::T3);
    _title->setForegroundRole(DPalette::ToolTipText);
    _title->setText(tr("Playlist"));

    _num = new DLabel();
    _num->setForegroundRole(DPalette::BrightText);
    QGraphicsOpacityEffect *opacityEffect = new QGraphicsOpacityEffect;
    _num->setGraphicsEffect(opacityEffect);
    opacityEffect->setOpacity(0.5);
    _num->setText("");
    DFontSizeManager::instance()->bind(_num, DFontSizeManager::T6);
    _num->setContentsMargins(0, 0, 0, 0);

    mainLayout->addWidget(left);
    auto *leftinfo = new QVBoxLayout;
    leftinfo->setContentsMargins(0, 0, 0, 0);
    leftinfo->setSpacing(0);
    left->setLayout(leftinfo);
    leftinfo->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    leftinfo->addWidget(_title);
    leftinfo->addSpacing(4);
    leftinfo->addWidget(_num);
    leftinfo->addSpacing(6);

    m_pClearButton = new DPushButton();
    m_pClearButton->setObjectName(CLEAR_PLAYLIST_BUTTON);
    m_pClearButton->setAccessibleName(CLEAR_PLAYLIST_BUTTON);
    m_pClearButton->setIcon(QIcon::fromTheme("dcc_clearlist"));
    m_pClearButton->setText(tr("Empty"));
    m_pClearButton->setFocusPolicy(Qt::TabFocus);
    m_pClearButton->installEventFilter(this);
    DFontSizeManager::instance()->bind(m_pClearButton, DFontSizeManager::T6);

    DPalette pa_cb = DApplicationHelper::instance()->palette(m_pClearButton);
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        pa_cb.setBrush(QPalette::Light, QColor(100, 100, 100, 255));
        pa_cb.setBrush(QPalette::Dark, QColor(92, 92, 92, 255));
    } else {
        pa_cb.setBrush(QPalette::Light, QColor(85, 84, 84, 255));
        pa_cb.setBrush(QPalette::Dark, QColor(65, 65, 65, 255));
    }
    pa_cb.setBrush(QPalette::ButtonText, QColor(255, 255, 255, 255));
    m_pClearButton->setPalette(pa_cb);
    m_pClearButton->setContentsMargins(0, 0, 0, 0);

    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::paletteTypeChanged, m_pClearButton, [ = ]() {
        DPalette pa_cBtn = DApplicationHelper::instance()->palette(m_pClearButton);
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            pa_cBtn.setBrush(QPalette::Light, QColor(100, 100, 100, 255));
            pa_cBtn.setBrush(QPalette::Dark, QColor(92, 92, 92, 255));
        } else {
            pa_cBtn.setBrush(QPalette::Light, QColor(85, 84, 84, 255));
            pa_cBtn.setBrush(QPalette::Dark, QColor(65, 65, 65, 255));
        }
        m_pClearButton->setPalette(pa_cb);
    });

    leftinfo->addWidget(m_pClearButton);
    connect(m_pClearButton, &QPushButton::clicked, _engine, &PlayerEngine::clearPlaylist);

    left->setContentsMargins(36, 0, 0, 0);
    m_pClearButton->setContentsMargins(0, 0, 0, 70);
    _num->setContentsMargins(0, 0, 0, 0);

    QWidget *right = new QWidget();
    right->setObjectName(RIGHT_LIST_WIDGET);
    auto *rightinfo = new QVBoxLayout;
    rightinfo->setContentsMargins(0, 0, 0, 0);
    rightinfo->setSpacing(0);
    right->setLayout(rightinfo);
    right->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(right);

    _playlist = new DListWidget();
    _playlist->setAttribute(Qt::WA_DeleteOnClose);
    _playlist->setFixedSize(width() - 205, 288);
    _playlist->setContentsMargins(0, 0, 0, 0);
    _playlist->installEventFilter(this);
    _playlist->viewport()->setAutoFillBackground(false);
    _playlist->setAutoFillBackground(false);
    _playlist->setObjectName(PLAYLIST);
    _playlist->viewport()->setObjectName(FILE_LIST);

    rightinfo->addWidget(_playlist);
    _playlist->setAttribute(Qt::WA_TranslucentBackground, false);
    _playlist->setFrameShape(QFrame::NoFrame);
    _playlist->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
    _playlist->setSelectionMode(QListView::NoSelection);
    _playlist->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _playlist->setResizeMode(QListView::Adjust);
    _playlist->setDragDropMode(QListView::InternalMove);
    _playlist->setSpacing(0);
    _playlist->viewport()->setAcceptDrops(true);
    _playlist->setDragEnabled(true);

    connect(_playlist, &DListWidget::itemClicked, this, &PlaylistWidget::slotShowSelectItem);
    connect(_playlist, &DListWidget::currentItemChanged, this, &PlaylistWidget::OnItemChanged);

    _playlist->setContentsMargins(0, 0, 0, 0);

    if (!composited) {
        _playlist->setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
        _playlist->setAttribute(Qt::WA_NativeWindow);
    }

    setTabOrder(m_pClearButton, _playlist);

#ifndef USE_DXCB
    auto *mwl = new MainWindowListener(this);
    mw->installEventFilter(mwl);
#endif

    if (!_closeMapper) {
        _closeMapper = new QSignalMapper(this);
        connect(_closeMapper,
                static_cast<void(QSignalMapper::*)(QWidget *)>(&QSignalMapper::mapped), this, &PlaylistWidget::slotCloseItem);

    }

    if (!_activateMapper) {
        _activateMapper = new QSignalMapper(this);
        connect(_activateMapper,
                static_cast<void(QSignalMapper::*)(QWidget *)>(&QSignalMapper::mapped), this, &PlaylistWidget::slotDoubleClickedItem);

    }

    connect(&_engine->playlist(), &PlaylistModel::emptied, this, &PlaylistWidget::clear);
    connect(&_engine->playlist(), &PlaylistModel::itemsAppended, this, &PlaylistWidget::appendItems);
    connect(&_engine->playlist(), &PlaylistModel::itemRemoved, this, &PlaylistWidget::removeItem);
    connect(&_engine->playlist(), &PlaylistModel::currentChanged, this, &PlaylistWidget::updateItemStates);
    connect(&_engine->playlist(), &PlaylistModel::itemInfoUpdated, this, &PlaylistWidget::updateItemInfo);

    QTimer::singleShot(10, this, &PlaylistWidget::loadPlaylist);

    connect(ActionFactory::get().playlistContextMenu(), &DMenu::aboutToShow, [ = ]() {
        QTimer::singleShot(20, [ = ]() {
            if (_mouseItem) {
                _clickedItem = _mouseItem;
                (static_cast<PlayItemWidget *>(_mouseItem))->setHovered(true);
            }
        });
    });
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::aboutToHide, [ = ]() {
        if (_mouseItem) {
            (static_cast<PlayItemWidget *>(_mouseItem))->setHovered(false);
        }
    });

    connect(_playlist->model(), &QAbstractItemModel::rowsMoved, this, &PlaylistWidget::slotRowsMoved);
}

PlaylistWidget::~PlaylistWidget()
{
}

void PlaylistWidget::updateSelectItem(const int key)
{
    auto curItem = _playlist->currentItem();
    auto curRow = _playlist->row(curItem);
    qInfo() << "prevRow..." << curRow;
    PlayItemWidget *prevItemWgt = nullptr;
    if (curItem) {
        prevItemWgt = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(curItem));
    }

    if (key == Qt::Key_Up) {
        if (curRow == -1) {
            _index = curRow + 1;
        } else {
            _index = curRow - 1;
        }
        if (_index < 0) {
            return;
        }

        _playlist->setCurrentRow(_index);
        qInfo() << "Enter Key_Up..." << _index;
        auto curItemWgt = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(_index)));
        if (prevItemWgt) {
            prevItemWgt->setBIsSelect(false);
        }
        if (curItemWgt) {
            curItemWgt->setBIsSelect(true);
        }

    } else if (key == Qt::Key_Down) {
        if (_index >= _playlist->count() - 1) {
            return;
        }
        _index = curRow + 1;
        _playlist->setCurrentRow(_index);
        qInfo() << "Enter Key_Down..." << _index;
        auto curItemWgt = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(_index)));
        if (prevItemWgt) {
            prevItemWgt->setBIsSelect(false);
        }
        if (curItemWgt) {
            curItemWgt->setBIsSelect(true);
        }
    } else if (key == Qt::Key_Enter || key == Qt::Key_Return) {
        if (m_pClearButton == focusWidget()) {   //focus在清空按钮上则清空列表
            _engine->clearPlaylist();
        } else {
            slotDoubleClickedItem(prevItemWgt);  //Enter键播放
        }
    }
}

void PlaylistWidget::clear()
{
    _playlist->clear();
    QString s = QString(tr("%1 videos")).arg(_playlist->count());
    _num->setText(s);
    _engine->getplaylist()->clearLoad();
}

void PlaylistWidget::updateItemInfo(int id)
{
    auto piw = dynamic_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(id)));
    if (piw == nullptr)     //update info thx
        return ;
    piw->updateInfo(_engine->playlist().items()[id]);
}

void PlaylistWidget::updateItemStates()
{
    qInfo() << __func__ << _playlist->count() << "current = " << _engine->playlist().current();

    for (int i = 0; i < _playlist->count(); i++) {
        auto piw = dynamic_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(i)));

        auto old = piw->state();
        piw->setState(ItemState::Normal);
        if (!piw->_pif.valid) {
            piw->setState(ItemState::Invalid);
        }

        if (i == _engine->playlist().current()) {
            if (piw->state() != ItemState::Playing) {
                //scrollToItem只能更新scroll位置，不能同步列表项
                //_playlist->scrollToItem(_playlist->item(i));
                _playlist->setCurrentRow(i);
                piw->setState(ItemState::Playing);

            }
        }
    }
}

void PlaylistWidget::showItemInfo()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget *>(_mouseItem);
    if (item) {
        MovieInfoDialog mid(item->_pif, _mw);
        mid.exec();
    }
}

void PlaylistWidget::openItemInFM()
{
    if (!_mouseItem) return;
    auto item = dynamic_cast<PlayItemWidget *>(_mouseItem);
    if (item) {
        utils::ShowInFileManager(item->_pif.mi.filePath);
    }
}

void PlaylistWidget::removeClickedItem(bool isShortcut)
{
    if (isShortcut) {
        for (int i = 0; i < _playlist->count(); i++) {
            auto piw = dynamic_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(i)));
            qInfo() << piw->getBIsSelect();
            if (piw->getBIsSelect()) {
                _engine->playlist().remove(i);
                return;
            }
        }
    }

    if (!_clickedItem) return;
    auto piw = dynamic_cast<PlayItemWidget *>(_clickedItem);
    if (piw) {
        qInfo() << __func__;
        for (int i = 0; i < _playlist->count(); i++) {
            if (_clickedItem == _playlist->itemWidget(_playlist->item(i))) {
                _engine->playlist().remove(i);
                break;
            }
        }
    }
}

void PlaylistWidget::slotCloseTimeTimeOut()
{
    QTimer *pCloselistTimer = dynamic_cast<QTimer *>(sender());
    pCloselistTimer->deleteLater();
    togglePopup(false);
    _mw->reflectActionToUI(ActionFactory::TogglePlaylist);
}

void PlaylistWidget::slotCloseItem(QWidget *w)
{
    qInfo() << "item close clicked";
    _clickedItem = w;
    _mw->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);
}

void PlaylistWidget::slotDoubleClickedItem(QWidget *w)
{
    qInfo() << "item double clicked";
    QList<QVariant> args;
    for (int i = 0; i < _playlist->count(); i++) {
        if (w == _playlist->itemWidget(_playlist->item(i))) {
            args << i;
            _mw->requestAction(ActionFactory::ActionKind::GotoPlaylistSelected,
                               false, args);

            QTimer *closelistTImer = new QTimer;
            closelistTImer->start(500);
            connect(closelistTImer, &QTimer::timeout, this, &PlaylistWidget::slotCloseTimeTimeOut);
            break;
        }
    }
}

void PlaylistWidget::slotRowsMoved()
{
    if (_lastDragged.first >= 0) {
        int target = -1;
        for (int i = 0; i < _playlist->count(); i++) {
            auto piw = dynamic_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(i)));
            if (piw == _lastDragged.second) {
                target = i;
                break;
            }
        }
        qInfo() << "swap " << _lastDragged.first << target;
        if (target >= 0 && _lastDragged.first != target) {
            _engine->playlist().switchPosition(_lastDragged.first, target);
            _lastDragged = {-1, nullptr};
        }
    }
}

/*void PlaylistWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    auto md = ev->mimeData();
    qInfo() << md->formats();
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
}*/

/*void PlaylistWidget::dragMoveEvent(QDragMoveEvent *ev)
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
}*/

/*void PlaylistWidget::dropEvent(QDropEvent *ev)
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
            auto piw = dynamic_cast<PlayItemWidget *>(_playlist->itemWidget(_playlist->item(row)));
            _lastDragged = qMakePair(row, piw);
            qInfo() << "drag to move " << row << piw->_pif.url;
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
}*/


void PlaylistWidget::contextMenuEvent(QContextMenuEvent *cme)
{
    bool on_item = false;
    _mouseItem = nullptr;
    QPoint itempos(cme->pos().x() - 235, cme->pos().y() - 20);

    if (_playlist->itemAt(itempos)) {
        _mouseItem = _playlist->itemWidget(_playlist->itemAt(itempos));
        on_item = true;
    }

    if (CompositingManager::get().isPadSystem()) {
        if (pSelectItemWgt) {
            pSelectItemWgt->setBIsSelect(false);
        }
        auto piw = dynamic_cast<PlayItemWidget *>(_mouseItem);
        piw->setBIsSelect(true);
        pSelectItemWgt = piw;
    } else {
        auto piw = dynamic_cast<PlayItemWidget *>(_mouseItem);
        auto menu = ActionFactory::get().playlistContextMenu();
        for (auto act : menu->actions()) {
            auto prop = static_cast<ActionFactory::ActionKind>(act->property("kind").toInt());
            bool on = true;
            if (prop == ActionFactory::ActionKind::PlaylistOpenItemInFM) {
                on = on_item && piw->_pif.valid && piw->_pif.url.isLocalFile();
            } else if (prop == ActionFactory::ActionKind::PlaylistRemoveItem) {
                on = on_item;
            } else if (prop == ActionFactory::ActionKind::PlaylistItemInfo) {
                on = on_item && piw->_pif.valid;
            } else {
                on = _playlist->count() > 0 ? true : false;
            }
            act->setEnabled(on);
        }

        ActionFactory::get().playlistContextMenu()->popup(cme->globalPos());
    }
#ifdef USE_TEST
    ActionFactory::get().playlistContextMenu()->hide();
    ActionFactory::get().playlistContextMenu()->clear();
#endif
}

void PlaylistWidget::showEvent(QShowEvent *se)
{
    batchUpdateSizeHints();
    adjustSize();

    QWidget::showEvent(se);
}

void PlaylistWidget::removeItem(int idx)
{
    qInfo() << "idx = " << idx;
    auto item_remove = this->_playlist->item(idx);
    if (item_remove) {
        QWidget *pItem = _playlist->itemWidget(item_remove);
        PlayItemWidget *pCurItem = dynamic_cast<PlayItemWidget *>(pItem);
        if (pCurItem == pSelectItemWgt) {
            pSelectItemWgt = nullptr;            //如果删除的是原来选中的则置空
        }
        item_remove = this->_playlist->takeItem(idx);
        delete item_remove;
    }

    this->_playlist->update();
    for (int i = 0; i < _playlist->count(); i++) {
        QWidget *item = _playlist->itemWidget(_playlist->item(i));
        if (item) {
            (dynamic_cast<PlayItemWidget *>(item))->setIndex(i);
        }
    }

    if (_playlist->count() != 0 && _playlist->count() != idx) {
        QWidget *item = _playlist->itemWidget(_playlist->item(idx));
        PlayItemWidget *curItem = dynamic_cast<PlayItemWidget *>(item);
        curItem->setCurItemHovered(true);
    } else if (_playlist->count() != 0 && _playlist->count() == idx) {
        QWidget *item = _playlist->itemWidget(_playlist->item(--idx));
        PlayItemWidget *curItem = dynamic_cast<PlayItemWidget *>(item);
        curItem->setCurItemHovered(true);
    }

    QString s = QString(tr("%1 videos")).arg(_playlist->count());
    _num->setText(s);
}

void PlaylistWidget::appendItems()
{
    qInfo() << __func__;

    auto items = _engine->playlist().items();
    auto p = items.begin() + this->_playlist->count();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this->_playlist, p - items.begin(), this);

        auto item = new QListWidgetItem;
        item->setFlags(Qt::NoItemFlags);
        _playlist->addItem(item);
        _playlist->setItemWidget(item, w);

        connect(w, SIGNAL(closeButtonClicked()), _closeMapper, SLOT(map()));
        connect(w, SIGNAL(doubleClicked()), _activateMapper, SLOT(map()));
        _closeMapper->setMapping(w, w);
        _activateMapper->setMapping(w, w);
        ++p;

    }
    QString s = QString(tr("%1 videos")).arg(_playlist->count());
    _num->setText(s);
    batchUpdateSizeHints();
    updateItemStates();
//    _playlist->setStyleSheet(styleSheet());
//    setStyleSheet(styleSheet());
}

void PlaylistWidget::slotShowSelectItem(QListWidgetItem *item)
{
    PlayItemWidget *pWidget = nullptr;

    if (item) {
        _playlist->setCurrentItem(item);
        pWidget = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(item));
        if (!pWidget) {
            return;
        }
    }

    if (CompositingManager::get().isPadSystem()) {
        pWidget->doDoubleClick();
        if (pSelectItemWgt) {
            pSelectItemWgt->setBIsSelect(false);
            pSelectItemWgt = nullptr;
        }
    } else {
        pWidget->setBIsSelect(true);
    }
}

void PlaylistWidget::OnItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    PlayItemWidget *prevItemWgt = nullptr;
    PlayItemWidget *curItemWgt = nullptr;
    bool bIsPad = CompositingManager::get().isPadSystem();

    if (previous) {
        prevItemWgt = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(previous));
        if (!bIsPad && prevItemWgt) {
            prevItemWgt->setBIsSelect(false);
        }
    }

    if (current) {
        curItemWgt = reinterpret_cast<PlayItemWidget *>(_playlist->itemWidget(current));
        if (!bIsPad && curItemWgt) {
            curItemWgt->setBIsSelect(true);
        }
    }
}

void PlaylistWidget::resetFocusAttribute(bool &atr)
{
    m_bButtonFocusOut = atr;
}

void PlaylistWidget::loadPlaylist()
{
    qInfo() << __func__;
    _playlist->clear();


    auto items = _engine->playlist().items();
    auto p = items.begin();
    while (p != items.end()) {
        auto w = new PlayItemWidget(*p, this->_playlist, p - items.begin(), this);
        auto item = new QListWidgetItem;
        item->setFlags(Qt::NoItemFlags);
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
    QString s = QString(tr("%1 videos")).arg(_playlist->count());
    _num->setText(s);
//    setStyleSheet(styleSheet());
}

void PlaylistWidget::batchUpdateSizeHints()
{
    if (isVisible()) {
        for (int i = 0; i < this->_playlist->count(); i++) {
            auto item = this->_playlist->item(i);
            auto w = this->_playlist->itemWidget(item);
            //auto t = w->size();
            item->setSizeHint(w->size());
        }
    }
}

void PlaylistWidget::endAnimation()
{
    if (paOpen != nullptr && paClose != nullptr) {
        paOpen -> setDuration(0);
        paClose->setDuration(0);
    }
}

bool PlaylistWidget::isFocusInPlaylist()
{
    if (m_pClearButton == focusWidget() || _playlist == focusWidget()) {
        return true;
    } else {
        return false;
    }
}

void PlaylistWidget::togglePopup(bool isShortcut)
{
    if (paOpen != nullptr || paClose != nullptr) {
        return ;
    }

    QRect main_rect = _mw->rect();
#ifdef USE_DXCB
    QRect view_rect = main_rect;
#else
    QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));
#endif

    QRect fixed;
#if defined(__arrch64__) || defined(__mips__)
    // y坐标下移5个像素点,避免播放列表上部超出toolbox范围
    fixed.setRect(10, (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 10) + 5),
                  view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT + 10);
#else
    if (!CompositingManager::get().composited()) {
        fixed.setRect(10, (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 10) + 5),
                      view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT + 10);
    } else {
        fixed.setRect(10, (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 10)),
                      view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT + 10);
    }
#endif

    QRect shrunk = fixed;
    shrunk.setHeight(0);
    shrunk.moveBottom(fixed.bottom());

    if (_toggling) return;

    if (_state == State::Opened) {
        Q_ASSERT(isVisible());

        //Set this judgment to false when the playlist is collapsed
        m_bButtonFocusOut = false;
        if (isFocusInPlaylist()) {
            //以除Esc以外的其它方式收起播放列表，焦点切换到主窗口，防止随机出现在其它控件上
            _mw->setFocus();
        }
#ifdef __x86_64__
        paOpen = new QPropertyAnimation(this, "geometry");
        paOpen->setEasingCurve(QEasingCurve::Linear);
        paOpen->setDuration(POPUP_DURATION);
        paOpen->setStartValue(fixed);
        paOpen->setEndValue(shrunk);;
        _toggling = false;
        _state = State::Closed;
        emit stateChange(isShortcut);
        paOpen->start();    //down
        connect(paOpen, &QPropertyAnimation::finished, [ = ]() {
            paOpen->deleteLater();
            paOpen = nullptr;
            setVisible(!isVisible());
        });
#else
        _toggling = false;
        _state = State::Closed;
        emit stateChange(isShortcut);
        setVisible(!isVisible());
#endif
    } else {
        _playlist->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setVisible(!isVisible());
#ifdef __x86_64__
        paClose = new QPropertyAnimation(this, "geometry");
        paClose->setEasingCurve(QEasingCurve::Linear);
        paClose->setDuration(POPUP_DURATION);
        paClose->setStartValue(shrunk);
        paClose->setEndValue(fixed);
        _toggling = false;
        _state = State::Opened;
        emit stateChange(isShortcut);
        paClose->start();   //up
        connect(paClose, &QPropertyAnimation::finished, [ = ]() {
            paClose->deleteLater();
            paClose = nullptr;
            _playlist->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            if (_mw->toolbox()->getListBtnFocus()) {
                m_pClearButton->setFocus();
            }
        });
#else
        _toggling = false;  //条件编译误报(cppcheck)
        _state = State::Opened;
        emit stateChange(isShortcut);
        setGeometry(fixed);
#endif
    }
}

void PlaylistWidget::paintEvent(QPaintEvent *pe)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF bgRect;
    bgRect.setSize(size());
    QPainterPath pp;
    pp.addRoundedRect(bgRect, 18, 18);
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
//#ifdef __aarch64__
//    QRect fixed(15, (view_rect.height() - 394),
//                view_rect.width() - 20, (384 - 100));

////    _playlist->setFixedWidth(width() - 235);
//    //_playlist->setFixedWidth(fixed.width() - 235);
//    _playlist->setFixedSize(fixed.width() - 221, fixed.height());
//    move(fixed.topLeft() - QPoint(10, 0));
//#else
    QRect fixed((view_rect.width() - 10), (view_rect.height() - 394),
                view_rect.width() - 20, (384 - 70));

//    _playlist->setFixedWidth(width() - 235);
    _playlist->setFixedWidth(fixed.width() - 205);
//#endif
    emit sizeChange();

    QTimer::singleShot(100, this, &PlaylistWidget::batchUpdateSizeHints);

    QWidget::resizeEvent(ev);
}

bool PlaylistWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_pClearButton) {
        //焦点在清空按键上禁用上下键
        if (event->type() == QEvent::KeyPress) {
            if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Up ||
                    static_cast<QKeyEvent *>(event)->key() == Qt::Key_Down) {
                return true;
            }
        }
        switch (event->type()) {
        case QEvent::FocusIn:
            _mw->toolbox()->setBtnFocusSign(true);
            break;
        case QEvent::FocusOut:
            if (_playlist->count() <= 0) {
                //如果播放列表为空，清空按钮上的焦点不向后传递
                return true;
            }
            break;
        }
    } else if (obj == _playlist) {
        switch (event->type()) {
        case QEvent::FocusIn: {
            if (_playlist->count()) {
                //The judgment here is to prevent the focus from shifting during mouse operation
                if (!m_bButtonFocusOut) {
                    return true;
                }
                //焦点切换到播放列表，选中第一个条目
                if (_playlist->currentRow() != 0) {
                    _playlist->setCurrentRow(0);
                    _index = 0;
                    m_bButtonFocusOut = false;
                }
            }
            return true;
        }
        case QEvent::KeyPress: {
            QKeyEvent *keyPressEv = static_cast<QKeyEvent *>(event);
            if (keyPressEv->key() == Qt::Key_Tab) {
                //将焦点设置到清空按钮上，实现焦点循环
                m_pClearButton->setFocus();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QObject::eventFilter(obj, event); // standard event processing
}
}

#include "playlist_widget.moc"
