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
#include "config.h"

#include "toolbox_proxy.h"
#include "mainwindow.h"
#include "event_relayer.h"
#include "compositing_manager.h"
#include "player_engine.h"
#include "toolbutton.h"
#include "dmr_settings.h"
#include "actions.h"
#include "slider.h"
#include "thumbnail_worker.h"
#include "tip.h"
#include "utils.h"

//#include <QtWidgets>
#include <DImageButton>
#include <DThemeManager>
#include <DArrowRectangle>
#include <DApplication>
#include <QThread>
#include <DSlider>
#include <DUtil>
#include <QDBusInterface>
#include <iostream>
static const int LEFT_MARGIN = 10;
static const int RIGHT_MARGIN = 10;
static const int PROGBAR_SPEC = 10 + 120 + 17 + 54 + 10 + 54 + 10 + 170 + 10 + 20;

static const QString SLIDER_ARROW = ":resources/icons/slider.svg";

#define POPUP_DURATION 350

DWIDGET_USE_NAMESPACE

//thx  wayland chuang kou bai kuai
#define WAYLAND_BLACK_WINDOW \
do {\
    auto systemEnv = QProcessEnvironment::systemEnvironment();\
    QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));\
    QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));\
    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||\
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {\
        auto colortype = DGuiApplicationHelper::instance()->themeType();\
        if(colortype == DGuiApplicationHelper::LightType)\
        {\
            QPalette pal(qApp->palette());\
            this->setAutoFillBackground(true);\
            this->setPalette(pal);\
            if(_playlist)\
            {\
                QPalette pal(qApp->palette());\
                _playlist->setAutoFillBackground(true);\
                _playlist->setPalette(pal);\
            }\
            if(_engine )\
            {\
                QPalette pal(qApp->palette());\
                _engine->setAutoFillBackground(true);\
                _engine->setPalette(pal);\
            }\
        }\
        else\
        {\
            QPalette pal(qApp->palette());\
            pal.setColor(QPalette::Background,Qt::black);\
            this->setAutoFillBackground(true);\
            this->setPalette(pal);\
            if(_playlist)\
            {\
                QPalette pal(qApp->palette());\
                pal.setColor(QPalette::Background,Qt::black);\
                _playlist->setAutoFillBackground(true);\
                _playlist->setPalette(pal);\
            }\
            if(_engine)\
            {\
                QPalette pal(qApp->palette());\
                pal.setColor(QPalette::Background,Qt::black);\
                _engine->setAutoFillBackground(true);\
                _engine->setPalette(pal);\
            }\
        }\
    }\
}while(0)



namespace dmr {
class KeyPressBubbler: public QObject
{
public:
    KeyPressBubbler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        if (event->type() == QEvent::KeyPress) {
            //QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            event->setAccepted(false);
            return false;
        } else {
            // standard event processing
            return QObject::eventFilter(obj, event);
        }
    }
};

class TooltipHandler: public QObject
{
public:
    TooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        switch (event->type()) {
        case QEvent::ToolTip: {
            //QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            auto btn = tip->property("for").value<QWidget *>();
            tip->setText(btn->toolTip());
            tip->show();
            tip->raise();
            tip->adjustSize();

            QPoint pos = btn->parentWidget()->mapToGlobal(btn->pos());
            pos.rx() = pos.x() + (btn->width() - tip->width()) / 2;
            pos.ry() = pos.y() - 40;
            tip->move(pos);
            return true;
        }

        case QEvent::Leave: {
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();
            break;
        }
        default:
            break;
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
};

class SubtitlesView;
class SubtitleItemWidget: public QWidget
{
    Q_OBJECT
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *parent, SubtitleInfo si): QWidget()
    {
        _sid = si["id"].toInt();

//        DThemeManager::instance()->registerWidget(this, QStringList() << "current");

        setFixedWidth(200);

        auto *l = new QHBoxLayout(this);
        setLayout(l);
        l->setContentsMargins(0, 0, 0, 0);

        _msg = si["title"].toString();
        auto shorted = fontMetrics().elidedText(_msg, Qt::ElideMiddle, 140 * 2);
        _title = new QLabel(shorted);
        _title->setWordWrap(true);
        l->addWidget(_title, 1);

        _selectedLabel = new QLabel(this);
        l->addWidget(_selectedLabel);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
                this, &SubtitleItemWidget::onThemeChanged);
        onThemeChanged();
    }

    int sid() const
    {
        return _sid;
    }

    void setCurrent(bool v)
    {
        if (v) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        } else {
            _selectedLabel->clear();
        }

        setProperty("current", v ? "true" : "false");
//        setStyleSheet(this->styleSheet());
        style()->unpolish(_title);
        style()->polish(_title);
    }

protected:
    void showEvent(QShowEvent *se) override
    {
        auto fm = _title->fontMetrics();
        auto shorted = fm.elidedText(_msg, Qt::ElideMiddle, 140 * 2);
        int h = fm.height();
        if (fm.width(shorted) > 140) {
            h *= 2;
        } else {
        }
        _title->setFixedHeight(h);
        _title->setText(shorted);

        QWidget::showEvent(se);
    }

private slots:
    void onThemeChanged()
    {
        if (property("current").toBool()) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        }
    }

private:
    QLabel *_selectedLabel {nullptr};
    QLabel *_title {nullptr};
    int _sid {-1};
    QString _msg;
};

class SubtitlesView: public DArrowRectangle
{
    Q_OBJECT
public:
    SubtitlesView(QWidget *p, PlayerEngine *e)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, p), _engine{e}
    {
        setWindowFlags(Qt::Popup);

//        DThemeManager::instance()->registerWidget(this);

        setMinimumHeight(20);
        setShadowBlurRadius(4);
        setRadius(4);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(8);
        setArrowHeight(6);

        QSizePolicy sz_policy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        setSizePolicy(sz_policy);

        setFixedWidth(220);

        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(8, 2, 8, 2);
        l->setSpacing(0);
        setLayout(l);

        _subsView = new QListWidget(this);
        _subsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        _subsView->setResizeMode(QListView::Adjust);
        _subsView->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        _subsView->setSelectionMode(QListWidget::SingleSelection);
        _subsView->setSelectionBehavior(QListWidget::SelectItems);
        l->addWidget(_subsView);

        connect(_subsView, &QListWidget::itemClicked, this, &SubtitlesView::onItemClicked);
        connect(_engine, &PlayerEngine::tracksChanged, this, &SubtitlesView::populateSubtitles);
        connect(_engine, &PlayerEngine::sidChanged, this, &SubtitlesView::onSidChanged);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
                this, &SubtitlesView::onThemeChanged);
        onThemeChanged();
    }

protected:
    void showEvent(QShowEvent *se) override
    {
        ensurePolished();
        populateSubtitles();
        setFixedHeight(_subsView->height() + 4);
    }

protected slots:
    void onThemeChanged()
    {
        if (qApp->theme() == "dark") {
            setBackgroundColor(DBlurEffectWidget::DarkColor);
        } else {
            setBackgroundColor(DBlurEffectWidget::LightColor);
        }
    }

    void batchUpdateSizeHints()
    {
        QSize sz(0, 0);
        if (isVisible()) {
            for (int i = 0; i < _subsView->count(); i++) {
                auto item = _subsView->item(i);
                auto w = _subsView->itemWidget(item);
                item->setSizeHint(w->sizeHint());
                sz += w->sizeHint();
                sz += QSize(0, 2);
            }
        }
        sz += QSize(0, 2);
        _subsView->setFixedHeight(sz.height());
    }

    void populateSubtitles()
    {
        _subsView->clear();
        _subsView->adjustSize();
        adjustSize();

        auto pmf = _engine->playingMovieInfo();
        auto sid = _engine->sid();
        qDebug() << "sid" << sid;

        for (const auto &sub : pmf.subs) {
            auto item = new QListWidgetItem();
            auto siw = new SubtitleItemWidget(this, sub);
            _subsView->addItem(item);
            _subsView->setItemWidget(item, siw);
            auto v = (sid == sub["id"].toInt());
            siw->setCurrent(v);
            if (v) {
                _subsView->setCurrentItem(item);
            }
        }

        batchUpdateSizeHints();
    }

    void onSidChanged()
    {
        auto sid = _engine->sid();
        for (int i = 0; i < _subsView->count(); ++i) {
            auto siw = static_cast<SubtitleItemWidget *>(_subsView->itemWidget(_subsView->item(i)));
            siw->setCurrent(siw->sid() == sid);
        }

        qDebug() << "current " << _subsView->currentRow();
    }

    void onItemClicked(QListWidgetItem *item)
    {
        auto id = _subsView->row(item);
        _engine->selectSubtitle(id);
    }

private:
    PlayerEngine *_engine {nullptr};
    QListWidget *_subsView {nullptr};
};

class SliderTime: public DArrowRectangle
{
    Q_OBJECT
public:
    SliderTime(): DArrowRectangle(DArrowRectangle::ArrowBottom)
    {
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlag(Qt::WindowStaysOnTopHint);
        resize(_miniSize);
        setRadius(4);
        setArrowWidth(10);
        setArrowHeight(5);
        const QPalette pal = QGuiApplication::palette();
        QColor bgColor = pal.color(QPalette::Highlight);
        setBorderWidth(1);
        setBorderColor(bgColor);
        setBackgroundColor(bgColor);

        auto *l = new QHBoxLayout;
        l->setContentsMargins(0, 0, 0, 5);
        _time = new DLabel(this);
        _time->setAlignment(Qt::AlignCenter);
//        _time->setFixedSize(_size);
        _time->setForegroundRole(DPalette::Text);
        DPalette pa = DApplicationHelper::instance()->palette(_time);
        QColor color = pa.textLively().color();
        qDebug() << color.name();
        pa.setColor(DPalette::Text, color);
        _time->setPalette(pa);
        _time->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));
        l->addWidget(_time, Qt::AlignCenter);
        setLayout(l);

        connect(qApp, &QGuiApplication::fontChanged, this, [ = ](const QFont & font) {
            _font = font;
            _bFontChanged = true;
        });
    }

    void setTime(const QString &time)
    {
        _time->setText(time);

        if (!_bFontChanged) {
            QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T8));
            _time->setFixedSize(fm.width(_time->text()) + 5, fm.height());
        } else {
            QFontMetrics fm(_font);
            _time->setFont(_font);
            _time->setFixedSize(fm.width(_time->text()) + 10, fm.height());
        }
        this->setWidth(_time->width());
        this->setHeight(_time->height() + 5);
        this->setMinimumSize(_miniSize);
    }

private:
    DLabel *_time {nullptr};
    QSize _miniSize = QSize(58, 25);
    QFont _font {QFont()};
    bool _bFontChanged {false};
};

class ViewProgBarItem: public QLabel
{
    Q_OBJECT
public:
    ViewProgBarItem(QImage *image, QWidget *parent = nullptr)
    {

    }
};
class ViewProgBar: public DWidget
{
    Q_OBJECT
public:
    ViewProgBar(DMRSlider *_progBar, QWidget *parent = nullptr)
    {
        //传入进度条，以便重新获取胶片进度条长度 by ZhuYuliang
        this->_progBar = _progBar;
        _parent = parent;
        setFixedHeight(70);
//       setFixedWidth(584);
//        setFixedWidth(parent->width() - PROGBAR_SPEC);
//       setFixedWidth(1450);
        _vlastHoverValue = 0;
        _isBlockSignals = false;
        setMouseTracking(true);

        _back = new QWidget(this);
        _back->setFixedHeight(60);
        _back->setFixedWidth(this->width());
        _back->setContentsMargins(0, 0, 0, 0);

        _front = new QWidget(this);
        _front->setFixedHeight(60);
        _front->setFixedWidth(0);
        _front->setContentsMargins(0, 0, 0, 0);

//       _indicator = new IndicatorBar(this);
        _indicator = new DBlurEffectWidget(this);
        _indicator->resize(4, 60);
        _indicator->setObjectName("indicator");
        _indicator->setMaskColor(QColor(255, 255, 255));
        _indicator->setBlurRectXRadius(2);
        _indicator->setBlurRectYRadius(2);

        _sliderTime = new SliderTime;
        _sliderTime->hide();

        QMatrix matrix;
        matrix.rotate(180);
        QPixmap pixmap = utils::LoadHiDPIPixmap(SLIDER_ARROW);
        _sliderArrowUp = new DLabel(this);
        _sliderArrowUp->setFixedSize(20, 18);
        _sliderArrowUp->setPixmap(pixmap);
        _sliderArrowUp->hide();
        _sliderArrowDown = new DLabel(this);
        _sliderArrowDown->setFixedSize(20, 18);
        _sliderArrowDown->setPixmap(pixmap.transformed(matrix, Qt::SmoothTransformation));
        _sliderArrowDown->hide();

        _back->setMouseTracking(true);
        _front->setMouseTracking(true);
        _indicator->setMouseTracking(true);

        _viewProgBarLayout = new QHBoxLayout(_back);
        _viewProgBarLayout->setContentsMargins(0, 5, 0, 5);
        _back->setLayout(_viewProgBarLayout);

        _viewProgBarLayout_black = new QHBoxLayout(_front);
        _viewProgBarLayout_black->setContentsMargins(0, 5, 0, 5);
        _front->setLayout(_viewProgBarLayout_black);

    }
//    virtual ~ViewProgBar();
    void setIsBlockSignals(bool isBlockSignals)
    {
        _isBlockSignals = isBlockSignals;
    }
    bool getIsBlockSignals()
    {
        return _isBlockSignals;
    }
    void setValue(int v)
    {
//        _indicatorPos = {v < 5 ? 5 : v, rect().y()};
        if (_press) {
            if (v < 3) {
                v = 3;
            } else if (v > width() - 2) {
                v = width() - 2;
            }
        }
        _indicatorPos = {v, rect().y()};
        update();
    }

    void setTime(qint64 pos)
    {
        QTime time(0, 0, 0);
        QString strTime = time.addSecs(static_cast<int>(pos)).toString("hh:mm:ss");
        _sliderTime->setTime(strTime);
    }

    void setTimeVisible(bool visible)
    {
        if (visible) {
            auto pos = this->mapToGlobal(QPoint(0, 0));
            _sliderTime->show(pos.x() + _indicatorPos.x() + 1, pos.y() + _indicatorPos.y() + 4);
        } else {
            _sliderTime->hide();
        }
    }

    void setViewProgBar(PlayerEngine *engine, QList<QPixmap>pm_list, QList<QPixmap>pm_black_list)
    {

//        _viewProgBarLoad =new viewProgBarLoad(engine);
        _engine = engine;
        QLayoutItem *child;
        while ((child = _viewProgBarLayout->takeAt(0)) != nullptr) {
            //setParent为NULL，防止删除之后界面不消失
            if (child->widget()) {
                child->widget()->setParent(nullptr);
            }

            delete child;
        }

        while ((child = _viewProgBarLayout_black->takeAt(0)) != nullptr) {
            //setParent为NULL，防止删除之后界面不消失
            if (child->widget()) {
                child->widget()->setParent(nullptr);
            }

            delete child;
        }


//        auto *viewProgBarLayout = new QHBoxLayout();
//        viewProgBarLayout->setContentsMargins(0,5,0,5);
//        auto tmp = _engine->duration()/64?_engine->duration()/64:1;
        /*
        int num = (_parent->width()-PROGBAR_SPEC+1)/9;
        auto tmp = (_engine->duration()*1000)/num;
        auto dpr = qApp->devicePixelRatio();
        QPixmap pm;
        pm.setDevicePixelRatio(dpr);
        QPixmap pm_black;
        pm_black.setDevicePixelRatio(dpr);
        VideoThumbnailer thumber;
        //        QTime d(0, 0, 0);
        QTime d(0, 0, 0,0);
        thumber.setThumbnailSize(_engine->videoSize().width() * qApp->devicePixelRatio());
        thumber.setMaintainAspectRatio(true);
        thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
        auto url = _engine->playlist().currentInfo().url;
        auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

        //    for(auto i=0;i<(_engine->duration() - tmp);){
        //          for(auto i=0;i<65;i++){
         for(auto i=0;i<num;i++){
        //          for(auto i=0;i<163;i++){
        //            d = d.addSecs(tmp);
             d = d.addMSecs(tmp);
           thumber.setSeekTime(d.toString("hh:mm:ss:ms").toStdString());
           try {
               std::vector<uint8_t> buf;
               thumber.generateThumbnail(file.toUtf8().toStdString(),
                       ThumbnailerImageType::Png, buf);

               auto img = QImage::fromData(buf.data(), buf.size(), "png");
        //                auto img_black = QImage::fromData(buf.data(), buf.size(), "png");
               auto img_tmp = img.scaledToHeight(50);
               img.scaledToHeight(50);

               QImage img_black = GraizeImage(img_tmp);
        //                QImage img_black = img_tmp.convertToFormat(QImage::Format_Indexed8);

        //                    img_black.setColorCount(256);
        //                    for(int i = 0; i < 256; i++)
        //                    {
        //                        img_black.setColor(i, qRgb(i, i, i));
        //                }
               pm = QPixmap::fromImage(img_tmp.copy(img_tmp.size().width()/2-4,0,8,50));
        //                pm.setDevicePixelRatio(dpr);
               pm_black = QBitmap::fromImage(img_black.copy(img_black.size().width()/2-4,0,8,50));
        //                pm_black.setDevicePixelRatio(dpr);


               ImageItem *label = new ImageItem(img_tmp);
        //                label->setPixmap(pm);
               label->setFixedSize(8,50);
        //                label->setBackgroundRole(QPalette::ColorRole::Base);
               _viewProgBarLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
               _viewProgBarLayout->addWidget(label, 0 , Qt::AlignLeft );
               _viewProgBarLayout->setSpacing(1);

               ImageItem *label_black = new ImageItem(img_tmp,true,_front);
               label_black->move(i*9,5);
        //                label_black->setPixmap(pm_black);
               label_black->setFixedSize(8,50);
           } catch (const std::logic_error&) {
           }


        //            _viewProgBarLayout_black->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        //            _viewProgBarLayout_black->addWidget(label_black, 0 , Qt::AlignLeft );
        //            _viewProgBarLayout_black->setSpacing(1);

        }
        */
//        _back->setLayout(_viewProgBarLayout);
        _viewProgBarLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        _viewProgBarLayout->setSpacing(1);

        //重新获取胶片进度条长度 by ZhuYuliang
        int pixWidget = _progBar->width() / 100;
        //当宽度比较宽的时候，就插入两次相同图片
        if (this->width() > 500) {
            pixWidget /= 2;
            for (int i = 0; i < pm_list.count() * 2 ; i++) {

                ImageItem *label = new ImageItem(pm_list.at(i / 2), false, _back);
                label->setMouseTracking(true);
                label->move(i * (pixWidget + 1) + 3, 5);
                label->setFixedSize(pixWidget, 50);

                ImageItem *label_black = new ImageItem(pm_black_list.at(i / 2), true, _front);
                label_black->setMouseTracking(true);
                label_black->move(i * (pixWidget + 1) + 3, 5);
                label_black->setFixedSize(pixWidget, 50);


            }
        } else {
            for (int i = 0; i < pm_list.count(); i++) {
                ImageItem *label = new ImageItem(pm_list.at(i), false, _back);
                label->setMouseTracking(true);
                label->move(i * (pixWidget + 1) + 3, 5);
                label->setFixedSize(pixWidget, 50);

                ImageItem *label_black = new ImageItem(pm_black_list.at(i), true, _front);
                label_black->setMouseTracking(true);
                label_black->move(i * (pixWidget + 1) + 3, 5);
                label_black->setFixedSize(pixWidget, 50);
            }
        }

        update();


    }
    void setWidth()
    {
//        setFixedWidth(_parent->width() - PROGBAR_SPEC);
//        _back->setFixedWidth(_parent->width() - PROGBAR_SPEC);

    }

    void clear()
    {
        foreach (QLabel *label, _front->findChildren<QLabel *>()) {
            if (label) {
                label->deleteLater();
                label = nullptr;
            }
        }

        foreach (QLabel *label, _back->findChildren<QLabel *>()) {
            if (label) {
                label->deleteLater();
                label = nullptr;
            }
        }

        _sliderTime->setVisible(false);
        _sliderArrowDown->setVisible(false);
        _sliderArrowUp->setVisible(false);
    }

private:
    void changeStyle(bool press)
    {
        if (!isVisible()) return;

        if (press) {
//            _indicator->changeStyle(press);
            _indicator->resize(2, 60);
            _indicator->setMaskColor(QColor(255, 138, 0));
            _indicator->setBlurRectXRadius(2);
            _indicator->setBlurRectYRadius(2);
            _sliderTime->setVisible(press);
            _sliderArrowUp->setVisible(press);
//            _sliderArrowDown->setVisible(press);
        } else {
//            _indicator->changeStyle(press);
            _indicator->resize(4, 60);
            _indicator->setMaskColor(QColor(255, 255, 255));
            _indicator->setBlurRectXRadius(2);
            _indicator->setBlurRectYRadius(2);
            _sliderTime->setVisible(press);
            _sliderArrowUp->setVisible(press);
//            _sliderArrowDown->setVisible(press);
        }
    }

signals:
    void leaveViewProgBar();
    void hoverChanged(int);
    void sliderMoved(int);
    void indicatorMoved(int);
    void mousePressed(bool pressed);

protected:

    void leaveEvent(QEvent *e) override
    {
        emit leaveViewProgBar();

        DWidget::leaveEvent(e);
    }

    void showEvent(QShowEvent *se) override
    {
//        _time->move((width() - _time->width())/2, 69);
        DWidget::showEvent(se);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!isEnabled()) return;

        int v = position2progress(e->pos());
        if (e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()) {
            if (e->buttons() & Qt::LeftButton) {
                int distance = (e->pos() - _startPos).manhattanLength();
                if (distance >= QApplication::startDragDistance()) {
                    _engine->seekAbsolute(v);
                    emit sliderMoved(v);
                    emit hoverChanged(v);
                    emit mousePressed(true);
                    setValue(e->pos().x());
                    setTimeVisible(_press);
                }
            } else {
                if (_vlastHoverValue != v) {
                    emit hoverChanged(v);
                }
                _vlastHoverValue = v;
            }
        }
        e->accept();

        DWidget::mouseMoveEvent(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (!_press && e->buttons() == Qt::LeftButton && isEnabled()) {
//            QSlider::mousePressEvent(e);
            _startPos = e->pos();

            int v = position2progress(e->pos());
//            setSliderPosition(v);
            _engine->seekAbsolute(v);
            emit sliderMoved(v);
            emit hoverChanged(v);
            emit mousePressed(true);
            setValue(e->pos().x());
            setTimeVisible(!_press);
            changeStyle(!_press);
            _press = !_press;
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        emit mousePressed(false);
        if (_press && isEnabled()) {
            changeStyle(!_press);
//            setTimeVisible(!_press);
            _press = !_press;
        }

        DWidget::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent *e) override
    {
        _indicator->move(_indicatorPos.x(), _indicatorPos.y());
//        _sliderArrowDown->move(_indicatorPos.x() + _indicator->width() / 2 - _sliderArrowDown->width() / 2,
//                               _indicatorPos.y() - 10);
        _sliderArrowUp->move(_indicatorPos.x() + _indicator->width() / 2 - _sliderArrowUp->width() / 2, 55);
        _front->setFixedWidth(_indicatorPos.x());

        if (_press) {
            setTimeVisible(_press);
        }

        DWidget::paintEvent(e);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        //auto i = _parent->width();
        //auto j = this->width();
//        setFixedWidth(_parent->width() - PROGBAR_SPEC);
        _back->setFixedWidth(this->width());

        DWidget::resizeEvent(event);
    }
private:
    PlayerEngine *_engine {nullptr};
    QWidget *_parent{nullptr};
    int _vlastHoverValue;
    QPoint _startPos;
    bool _isBlockSignals;
    QPoint _indicatorPos {0, 0};
    QColor _indicatorColor;
//    QLabel *_indicator;
    viewProgBarLoad *_viewProgBarLoad{nullptr};
    QWidget *_back{nullptr};
    QWidget *_front{nullptr};
//    IndicatorBar *_indicator{nullptr};
    DBlurEffectWidget *_indicator{nullptr};
    SliderTime *_sliderTime{nullptr};
    DLabel *_sliderArrowDown{nullptr};
    DLabel *_sliderArrowUp{nullptr};
    bool _press{false};
    QGraphicsColorizeEffect *m_effect{nullptr};
    QList<QLabel *> labelList ;
    QHBoxLayout *_indicatorLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout_black{nullptr};
    DMRSlider *_progBar{nullptr};
    int position2progress(const QPoint &p)
    {
        auto total = _engine->duration();
        //qreal span = (qreal)total * p.x() / (contentsRect().width() - 4);
        int span = static_cast<int>(total * p.x() / (contentsRect().width() - 4));
        return span/* * (p.x())*/;
    }

};

class ThumbnailPreview: public DArrowRectangle
{
    Q_OBJECT
public:
    ThumbnailPreview(): DArrowRectangle(DArrowRectangle::ArrowBottom)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        // FIXME(hualet): Qt::Tooltip will cause Dock to show up even
        // the player is in fullscreen mode.
//        setWindowFlags(Qt::Tool);

        setObjectName("ThumbnailPreview");

//        setFixedSize(ThumbnailWorker::thumbSize().width(),ThumbnailWorker::thumbSize().height()+10);

//        setWidth(ThumbnailWorker::thumbSize().width());
//        setHeight(ThumbnailWorker::thumbSize().height());
//        resize(QSize(106, 66));
//        setShadowBlurRadius(2);
//        setRadius(2);
        setRadius(16);
        setBorderWidth(1);
        setBorderColor(QColor(255, 255, 255, 26));

        setShadowYOffset(4);
        setShadowXOffset(0);
        setShadowBlurRadius(6);
        setArrowWidth(0);
        setArrowHeight(0);

        auto *l = new QVBoxLayout;
//        l->setContentsMargins(0, 0, 0, 10);
        l->setContentsMargins(1, 0, 0, 0);

        _thumb = new DFrame(this);
        DStyle::setFrameRadius(_thumb, 16);

        //_thumb->setFixedSize(ThumbnailWorker::thumbSize());
        l->addWidget(_thumb/*,Qt::AlignTop*/);
        setLayout(l);

//        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &ThumbnailPreview::updateTheme);
//        updateTheme();

        winId(); // force backed window to be created
    }

    void updateWithPreview(const QPixmap &pm, qint64 secs, int rotation)
    {
        auto rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);

        if (rounded.width() == 0)
            return;
        if (rounded.width() > rounded.height()) {
            static int roundedH = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.width()))
                                      * rounded.height());
            QSize size(m_thumbnailFixed, roundedH);
            resizeThumbnail(rounded, size);
        } else {
            static int roundedW = static_cast<int>(
                                      (static_cast<double>(m_thumbnailFixed)
                                       / static_cast<double>(rounded.height()))
                                      * rounded.width());
            QSize size(roundedW, m_thumbnailFixed);
            resizeThumbnail(rounded, size);
        }

        QImage image;
        QPalette palette;
        image = rounded.toImage();
        palette.setBrush(_thumb->backgroundRole(),
                         QBrush(image.scaled(// 缩放背景图.
                                    QSize(_thumb->width(), _thumb->height()),
                                    Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation)));
        _thumb->setPalette(palette);


        if (isVisible()) {
//            move(QCursor::pos().x(), frameGeometry().y() + height()+0);
        }
    }

    void updateWithPreview(const QPoint &pos)
    {
        resizeWithContent();
        show(pos.x(), pos.y() + 10);
    }

signals:
    void leavePreview();

protected slots:
//    void updateTheme()
//    {
//        if (qApp->theme() == "dark") {
//            setBackgroundColor(QColor(23, 23, 23, 255 * 8 / 10));
//            setBorderColor(QColor(255, 255 ,255, 25));
//            _time->setStyleSheet(R"(
//                border-radius: 3px;
//                background-color: rgba(23, 23, 23, 0.8);
//                font-size: 12px;
//                color: #ffffff;
//            )");
//        } else {
//            setBackgroundColor(QColor(255, 255, 255, 255 * 8 / 10));
//            setBorderColor(QColor(0, 0 ,0, 25));
//            _time->setStyleSheet(R"(
//                border-radius: 3px;
//                background-color: rgba(255, 255, 255, 0.8);
//                font-size: 12px;
//                color: #303030;
//            )");
//        }
//    }

protected:
    void paintEvent(QPaintEvent *e) Q_DECL_OVERRIDE{
        DArrowRectangle::paintEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        emit leavePreview();

        DArrowRectangle::leaveEvent(e);
    }

    void showEvent(QShowEvent *se) override
    {
        DArrowRectangle::showEvent(se);
    }

private:
    void resizeThumbnail(QPixmap &pixmap, const QSize &size)
    {
        auto dpr = qApp->devicePixelRatio();
        pixmap.setDevicePixelRatio(dpr);
        pixmap = pixmap.scaled(size * dpr, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        _thumb->setFixedSize(size);
//        this->setFixedWidth(_thumb->width());
//        this->setFixedHeight(_thumb->height() + 10);
        this->setFixedWidth(_thumb->width() + 2);
        this->setFixedHeight(_thumb->height() + 2);
    }

private:
    DFrame *_thumb {nullptr};
    int m_thumbnailFixed = 178;
};

class VolumeSlider: public DArrowRectangle
{
    Q_OBJECT
public:
    VolumeSlider(PlayerEngine *eng, MainWindow *mw, QWidget *parent)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, DArrowRectangle::FloatWidget, parent), _engine(eng), _mw(mw)
    {
        setFixedSize(QSize(62, 201));
#ifdef __mips__
        if (!CompositingManager::get().composited()) {
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        }
#elif  defined (__aarch64__)
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
#endif
        setShadowBlurRadius(4);
        setRadius(18);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(20);
        setArrowHeight(15);
        setFocusPolicy(Qt::NoFocus);
        hide();

//        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &VolumeSlider::updateBg);

//        updateBg();

        auto *l = new QHBoxLayout;
        l->setContentsMargins(0, 4, 0, 10);
        setLayout(l);

        _slider = new DSlider(Qt::Vertical, this);
        _slider->setLeftIcon(QIcon());
        _slider->setRightIcon(QIcon());
        _slider->setIconSize(QSize(12, 12));
        _slider->installEventFilter(this);
        _slider->show();
        _slider->slider()->setRange(0, 100);
//        _slider->slider()->setOrientation(Qt::Vertical);

        auto vol = _engine->volume();
        /*if (vol != 0) {
            vol -= VOLUME_OFFSET;
        }*/
        _slider->setValue(vol);
        l->addWidget(_slider, Qt::AlignHCenter);


        connect(_slider, &DSlider::valueChanged, [ = ]() {
            auto var = _slider->value();
            _mw->requestAction(ActionFactory::ChangeVolume, false, QList<QVariant>() << var);
        });

        _autoHideTimer.setSingleShot(true);
#ifdef __x86_64__
        connect(&_autoHideTimer, &QTimer::timeout, this, &VolumeSlider::hide);
#endif

        //Modify by xiepengfei 2020.5.5
        //修改音量显示逻辑，使之发生变化时从变化源处直接修改显示值而不是从系统中获取
        //此处修改有风险，但为了音量变化曲线问题
        //后期如有方法解决音量变化曲线问题请启用此部分代码并删除setDiaplayValue等相关部分
        /*connect(_engine, &PlayerEngine::volumeChanged, [ = ]() {
            auto vol = _engine->volume();
            auto var = _slider->value();
            if (abs(var - vol) < 5) {
                _slider->setValue(var);
            } else {
                _slider->setValue(vol);
            }
        });*/
        m_composited = CompositingManager::get().composited();
    }


    ~VolumeSlider()
    {
//        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &VolumeSlider::updateBg);
    }

#ifdef __mips__
    void paintEvent(QPaintEvent *event)
    {
        bool composited = CompositingManager::get().composited();
        if (!m_composited) {
            QPainter painter(this);
            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ) {
                painter.fillRect(rect(), Qt::white);
            } else {
                painter.fillRect(rect(), Qt::black);
            }
        } else {
            DArrowRectangle::paintEvent(event);
        }
    }
#endif
    void stopTimer()
    {
        _autoHideTimer.stop();
    }
    int value()
    {
        return _slider->value();
    }

public slots:
    void delayedHide()
    {
#ifdef __x86_64__
        _autoHideTimer.start(500);
#else
        m_mouseIn = false;
        DUtil::TimerSingleShot(100, [this]() {
            if (!m_mouseIn)
                hide();
        });
#endif
    }
    void setValue(int v)
    {
        _slider->setValue(v);
    }

protected:
#ifdef __x86_64__
    void enterEvent(QEvent *e)
    {
        _autoHideTimer.stop();

        DArrowRectangle::enterEvent(e);
    }

    void showEvent(QShowEvent *se)
    {
        _autoHideTimer.stop();

        DArrowRectangle::showEvent(se);
    }

    void leaveEvent(QEvent *e)
    {
        _autoHideTimer.start(500);

        DArrowRectangle::leaveEvent(e);
    }
#else
    void enterEvent(QEvent *e)
    {
        m_mouseIn = true;
        QWidget::leaveEvent(e);
    }

    void showEvent(QShowEvent *se)
    {
        m_mouseIn = true;
        QWidget::showEvent(se);
    }

    void leaveEvent(QEvent *e)
    {
        m_mouseIn = false;
        delayedHide();
        QWidget::leaveEvent(e);
    }
#endif

private slots:
    void updateBg()
    {
//        if (qApp->theme() == "dark") {
//            setBackgroundColor(QColor(49, 49, 49, 255 * 9 / 10));
//        } else {
//            setBackgroundColor(QColor(255, 255, 255, 255 * 9 / 10));
//        }
    }

    bool eventFilter(QObject *obj, QEvent *e)
    {
        if (e->type() == QEvent::Wheel) {
            QWheelEvent *we = static_cast<QWheelEvent *>(e);
            qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
            if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
                if (_slider->value() == _slider->maximum() && we->angleDelta().y() > 0) {
                    //keep increasing volume
                    _mw->requestAction(ActionFactory::VolumeUp);
                }
#if defined (__aarch64__) || defined (__mips__)
                else {
                    _mw->requestAction(we->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
                }
#endif
            }
            return false;
        } else {
            return QObject::eventFilter(obj, e);
        }
    }

private:
    PlayerEngine *_engine;
    DSlider *_slider;
    MainWindow *_mw;
    QTimer _autoHideTimer;
    bool m_composited = false;
    bool m_mouseIn = false;
};

viewProgBarLoad::viewProgBarLoad(PlayerEngine *engine, DMRSlider *progBar, ToolboxProxy *parent)
{
    _parent = parent;
    _engine = engine;
    _progBar = progBar;
}

void viewProgBarLoad::quitLoad()
{
    m_bQuit = true;
}

void viewProgBarLoad::load()
{
    m_mutex.lock();
    //停止
    m_bisload = true;

    m_mutex.unlock();
}

void viewProgBarLoad::setListPixmapMutex(QMutex *pMutex)
{
    pListPixmapMutex = pMutex;
}

void viewProgBarLoad::run()
{
    while (!m_bQuit) {
        if (m_bisload) {

            m_mutex.lock();

            m_bisload = false;
            m_mutex.unlock();

            loadViewProgBar(_parent->size());
        } else {
            this->sleep(1);
        }
    }

}

void viewProgBarLoad::loadViewProgBar(QSize size)
{
    auto num = /*qreal(_progBar->width()) / 9*/100;
    auto pixWidget =  _progBar->width() / 100;
    auto tmp = (_engine->duration() * 1000) / num;
    //auto dpr = qApp->devicePixelRatio();
    QList<QPixmap> pm;
//    pm.setDevicePixelRatio(dpr);
    QList<QPixmap> pm_black;
//    pm_black.setDevicePixelRatio(dpr);
    VideoThumbnailer thumber;
    QTime d(0, 0, 0, 0);
    qDebug() << _engine->videoSize().width();
    qDebug() << _engine->videoSize().height();
    qDebug() << qApp->devicePixelRatio();
    if (_engine->videoSize().width() > 0 && _engine->videoSize().height() > 0) {
        thumber.setThumbnailSize(static_cast<int>(50 * (_engine->videoSize().width() / _engine->videoSize().height() * 50)
                                                  * qApp->devicePixelRatio()));
    }

    thumber.setMaintainAspectRatio(true);
    thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
    auto url = _engine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    for (auto i = 0; i < num; i++) {

        if (m_bQuit == true) {
            qDebug() << "load return";
            return;
        }
        if (isInterruptionRequested()) {
            qDebug() << "isInterruptionRequested";
            return;
        }
        d = d.addMSecs(tmp);
//        qDebug()<<d;
        thumber.setSeekTime(d.toString("hh:mm:ss:ms").toStdString());
        try {
            std::vector<uint8_t> buf;
            thumber.generateThumbnail(file.toUtf8().toStdString(), ThumbnailerImageType::Jpeg, buf);

            auto img = QImage::fromData(buf.data(), static_cast<int>(buf.size()), "jpg");
            auto img_tmp = img.scaledToHeight(50);


            pm.append(QPixmap::fromImage(img_tmp.copy(img_tmp.size().width() / 2 - 4, 0, pixWidget, 50)));
            QImage img_black = img_tmp.convertToFormat(QImage::Format_Grayscale8);
            pm_black.append(QPixmap::fromImage(img_black.copy(img_black.size().width() / 2 - 4, 0, pixWidget, 50)));

        } catch (const std::logic_error &) {
        }

    }
    pListPixmapMutex->lock();
    _parent->addpm_list(pm);
    _parent->addpm_black_list(pm_black);
    pListPixmapMutex->unlock();
    emit sigFinishiLoad(size);
    emit finished();


}

ToolboxProxy::ToolboxProxy(QWidget *mainWindow, PlayerEngine *proxy)
    : DFloatingWidget(mainWindow),
      _mainWindow(static_cast<MainWindow *>(mainWindow)),
      _engine(proxy)
{
    auto systemEnv = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = systemEnv.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = systemEnv.value(QStringLiteral("WAYLAND_DISPLAY"));

    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
        _bthumbnailmode = false;
    }

    bool composited = CompositingManager::get().composited();
//    setFrameShape(QFrame::NoFrame);
//    setFrameShadow(QFrame::Plain);
//    setLineWidth(0);
    //setFixedHeight(TOOLBOX_HEIGHT);
//    setAutoFillBackground(false);
//    setAttribute(Qt::WA_TranslucentBackground);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

//    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect(this);
//    shadowEffect->setOffset(0, 4);
//    shadowEffect->setBlurRadius(8);
//    shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
//    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
//        if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
//        {
//            shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
//            shadowEffect->setOffset(0, 4);
//            shadowEffect->setBlurRadius(8);
//        } else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
//        {
//            shadowEffect->setColor(QColor(0, 0, 0, 0.2 * 255));
//            shadowEffect->setOffset(0, 2);
//            shadowEffect->setBlurRadius(4);
//        } else
//        {
//            shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
//            shadowEffect->setOffset(0, 4);
//            shadowEffect->setBlurRadius(8);
//        }
//    });
//    setGraphicsEffect(shadowEffect);


//    DThemeManager::instance()->registerWidget(this);


    paopen = nullptr;
    paClose = nullptr;

    label_list.clear();
    label_black_list.clear();
    pm_list.clear();
    pm_black_list.clear();

    _previewer = new ThumbnailPreview;
    _previewer->hide();

    _previewTime = new SliderTime;
    _previewTime->hide();

    _subView = new SubtitlesView(nullptr, _engine);
    _subView->hide();
    setup();

//    _viewProgBarLoad= new viewProgBarLoad(_engine,_progBar,this);
//    _loadThread = new QThread();

//    _viewProgBarLoad->moveToThread(_loadThread);
//    _loadThread->start();

//    connect(this, SIGNAL(sigstartLoad(QSize)), _viewProgBarLoad, SLOT(loadViewProgBar(QSize)));
//    connect(this,&ToolboxProxy::sigstartLoad,this,[=]{
//        _viewProgBarLoad->loadViewProgBar();
//    });
//    connect(_viewProgBarLoad, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged,
            this, &ToolboxProxy::updatePlayState);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged,
            this, &ToolboxProxy::updateplaylisticon);
}
void ToolboxProxy::finishLoadSlot(QSize size)
{
    qDebug() << "humbnail has finished";

    if (pm_list.isEmpty()) return;

    if (!_bthumbnailmode) {
        return;
    }
    _viewProgBar->setViewProgBar(_engine, pm_list, pm_black_list);

    if (CompositingManager::get().composited()/* && _loadsize == size*/ && _engine->state() != PlayerEngine::CoreState::Idle) {
        PlayItemInfo info = _engine->playlist().currentInfo();
        if (!info.url.isLocalFile()) {
            // Url and DVD without thumbnail
//            if (!info.url.scheme().startsWith("dvd")) {
            return;
//            }
        }
        _progBar_Widget->setCurrentIndex(2);
    }
}

void ToolboxProxy::setthumbnailmode()
{
    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        return;
    }

#if !defined (__mips__ ) && !defined(__aarch64__)
    if (Settings::get().isSet(Settings::ShowThumbnailMode)) {
        _bthumbnailmode = true;
        updateThumbnail();
    } else {
        _bthumbnailmode = false;
        updateMovieProgress();
        _progBar_Widget->setCurrentIndex(1);   //恢复进度条模式 by zhuyuliang
    }
#else
    bool composited = CompositingManager::get().composited();
    if (composited) {
        _bthumbnailmode = true;
        updateThumbnail();
    } else {
        _bthumbnailmode = false;
        updateMovieProgress();
    }
#endif

}

void ToolboxProxy::setDisplayValue(int v)
{
    _volSlider->setValue(v);
}

void ToolboxProxy::updateplaylisticon()
{
    if (_listBtn->isChecked() && DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        _listBtn->setIcon(QIcon(":/icons/deepin/builtin/light/checked/episodes_checked.svg"));
    } else {
        _listBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    }
}
ToolboxProxy::~ToolboxProxy()
{
    ThumbnailWorker::get().stop();
//    _loadThread->exit();
//    _loadThread->terminate();
//    _loadThread->exit();
//    delete _loadThread;
    delete _subView;
    delete _previewer;
    delete _previewTime;

    if (m_worker) {
        m_worker->quitLoad();
        m_worker->wait();
        m_worker->quit();
        m_worker->deleteLater();
    }
}

void ToolboxProxy::setup()
{
    auto *stacked = new QStackedLayout(this);
    stacked->setContentsMargins(0, 0, 0, 0);
    stacked->setStackingMode(QStackedLayout::StackAll);
    setLayout(stacked);

    this->setBlurBackgroundEnabled(true);
    this->blurBackground()->setRadius(30);
    this->blurBackground()->setBlurEnabled(true);
    this->blurBackground()->setMode(DBlurEffectWidget::GaussianBlur);

    auto bot_widget = new DBlurEffectWidget(this);
//    bot_widget->setBlurBackgroundEnabled(true);
    bot_widget->setBlurRectXRadius(18);
    bot_widget->setBlurRectYRadius(18);
    bot_widget->setRadius(30);
    bot_widget->setBlurEnabled(true);
    bot_widget->setMode(DBlurEffectWidget::GaussianBlur);

#define THEME_TYPE(colortype) do { \
    if (colortype == DGuiApplicationHelper::LightType){\
        QColor backMaskColor(255, 255, 255, 255);\
        this->blurBackground()->setMaskColor(backMaskColor);\
        QColor maskColor(255, 255, 255, 255);\
        bot_widget->setMaskColor(maskColor);\
    } else if (colortype == DGuiApplicationHelper::DarkType){\
        QColor backMaskColor(37, 37, 37, 140);\
        blurBackground()->setMaskColor(backMaskColor);\
        QColor maskColor(37, 37, 37, 76);\
        bot_widget->setMaskColor(maskColor);\
    } else {\
        QColor backMaskColor(255, 255, 255, 140);\
        this->blurBackground()->setMaskColor(backMaskColor);\
        QColor maskColor(255, 255, 255, 76);\
        bot_widget->setMaskColor(maskColor);\
    }\
} while(0);

    auto type = DGuiApplicationHelper::instance()->themeType();
    THEME_TYPE(type);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
        auto type = DGuiApplicationHelper::instance()->themeType();
        WAYLAND_BLACK_WINDOW;
        THEME_TYPE(type);
    });
#undef THEME_TYPE

//    auto *bot_widget = new QWidget(this);
    bot_widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *botv = new QVBoxLayout(bot_widget);
    botv->setContentsMargins(0, 0, 0, 0);
    botv->setSpacing(10);
//    auto *bot = new QHBoxLayout();

    _bot_spec = new QWidget(bot_widget);
    _bot_spec->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    _bot_spec->setFixedWidth(width());
//    _bot_spec->setFixedHeight(TOOLBOX_SPACE_HEIGHT);
    _bot_spec->setVisible(false);
    botv->addWidget(_bot_spec);
    botv->addStretch();

    bot_toolWgt = new QWidget(bot_widget);
    bot_toolWgt->setFixedHeight(TOOLBOX_HEIGHT - 10);
    bot_toolWgt->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *bot_layout = new QHBoxLayout(bot_toolWgt);
    bot_layout->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    bot_layout->setSpacing(0);
    bot_toolWgt->setLayout(bot_layout);
    botv->addWidget(bot_toolWgt);

    bot_widget->setLayout(botv);
    stacked->addWidget(bot_widget);
//    QPalette palette;
//    palette.setColor(QPalette::Background, QColor(0,0,0,255)); // 最后一项为透明度
//    bot_widget->setPalette(palette);

    _timeLabel = new QLabel(bot_toolWgt);
    _timeLabel->setAlignment(Qt::AlignCenter);
    _fullscreentimelable = new QLabel("");
    _fullscreentimelable->setAttribute(Qt::WA_DeleteOnClose);
    _fullscreentimelable->setForegroundRole(DPalette::Text);

    DFontSizeManager::instance()->bind(_timeLabel, DFontSizeManager::T6);
    _timeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    _fullscreentimelable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(_fullscreentimelable, DFontSizeManager::T6);
    _timeLabelend = new QLabel(bot_toolWgt);
    _timeLabelend->setAlignment(Qt::AlignCenter);
    _fullscreentimelableend = new QLabel("");
    _fullscreentimelableend->setAttribute(Qt::WA_DeleteOnClose);
    _fullscreentimelableend->setForegroundRole(DPalette::Text);
    _timeLabelend->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(_timeLabelend, DFontSizeManager::T6);
    _fullscreentimelableend->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    DFontSizeManager::instance()->bind(_fullscreentimelableend, DFontSizeManager::T6);

    _progBar = new DMRSlider(bot_toolWgt);
    _progBar->setObjectName("MovieProgress");
    _progBar->slider()->setOrientation(Qt::Horizontal);
    _progBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
//    _progBar->setFixedHeight(60);
//    _progBar->setFixedWidth(584);
//    _progBar->setFixedWidth(1450);
    _progBar->slider()->setRange(0, 100);
    _progBar->setValue(0);
    _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);
//    _progBar->hide();
    connect(_previewer, &ThumbnailPreview::leavePreview, [ = ]() {
        auto pos =
            _progBar->mapFromGlobal(QCursor::pos());
        if (!_progBar->geometry().contains(pos)) {
            _previewer->hide();
            _previewTime->hide();
            _progBar->forceLeave();
        }
    });
    connect(&Settings::get(), &Settings::baseChanged, this, &ToolboxProxy::setthumbnailmode);
    connect(_engine, &PlayerEngine::siginitthumbnailseting, this, &ToolboxProxy::setthumbnailmode);

    connect(_progBar, &DSlider::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DSlider::valueChanged, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::leave, [ = ]() {
        _previewer->hide();
        _previewTime->hide();
        m_mouseFlag = false;
    });
    connect(_progBar, &DMRSlider::sliderPressed, [ = ]() {
        m_mousePree = true;
    });
    connect(_progBar, &DMRSlider::sliderReleased, [ = ]() {
        m_mousePree = false;
        _engine->seekAbsolute(m_mouseRelesePos);

    });
    connect(&Settings::get(), &Settings::baseMuteChanged,
    [ = ](QString sk, const QVariant & val) {
        if (sk == "base.play.mousepreview") {
            _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);
        }
    });
    connect(_progBar, &DMRSlider::enter, [ = ]() {
        if (_engine->state() == PlayerEngine::CoreState::Playing || _engine->state() == PlayerEngine::CoreState::Paused) {
//            _viewProgBar->show();
//            _progBar->hide();
//            _progBar_stacked->setCurrentIndex(2);
//            _progBar_Widget->setCurrentIndex(2);
        }

    });
//    stacked->addWidget(_progBar);

    _viewProgBar = new ViewProgBar(_progBar, bot_toolWgt);
//    _viewProgBar->hide();
    _viewProgBar->setFocusPolicy(Qt::NoFocus);
//    _viewProgBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    connect(_viewProgBar, &ViewProgBar::leaveViewProgBar, [ = ]() {
//        _viewProgBar->hide();
//        _progBar->show();
//        _progBarspec->hide();
//        _progBar_stacked->setCurrentIndex(1);
//        _progBar_Widget->setCurrentIndex(1);
        _previewer->hide();
        _previewTime->hide();
        m_mouseFlag = false;
    });

    connect(_viewProgBar, &ViewProgBar::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_viewProgBar, &ViewProgBar::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_viewProgBar, &ViewProgBar::mousePressed, this, &ToolboxProxy::updateTimeVisible);

    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

    _mid = new QHBoxLayout(bot_toolWgt);
    _mid->setContentsMargins(0, 0, 0, 0);
    _mid->setSpacing(0);
    _mid->setAlignment(Qt::AlignLeft);
    bot_layout->addLayout(_mid);

    QHBoxLayout *time = new QHBoxLayout(bot_toolWgt);
    time->setContentsMargins(10, 10, 10, 10);
    time->setSpacing(0);
    time->setAlignment(Qt::AlignLeft);
    bot_layout->addLayout(time);
    time->addWidget(_timeLabel);

//    bot->addStretch();


    QHBoxLayout *progBarspec = new QHBoxLayout(bot_toolWgt);
    progBarspec->setContentsMargins(0, 5, 0, 0);
    progBarspec->setSpacing(0);
    progBarspec->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(progBarspec);
//    progBarspec->addWidget(_progBarspec);
    _progBar_Widget = new QStackedWidget(bot_toolWgt);
    _progBar_Widget->setContentsMargins(0, 0, 0, 0);
    _progBar_Widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    _progBarspec = new DWidget(_progBar_Widget);
    _progBarspec->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
//    _progBarspec->setFixedHeight(12 + TOOLBOX_TOP_EXTENT);
//    _progBarspec->setFixedWidth(584);
//    _progBarspec->setFixedWidth(1450);

    QHBoxLayout *progBar = new QHBoxLayout(_progBar_Widget);
    progBar->setContentsMargins(0, 0, 0, 0);
    progBar->setSpacing(0);
    progBar->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(progBar);
    progBar->addWidget(_progBar);

    QHBoxLayout *viewProgBar = new QHBoxLayout(_progBar_Widget);
    viewProgBar->setContentsMargins(0, 0, 0, 0);
    viewProgBar->setSpacing(0);
    viewProgBar->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(viewProgBar);
    viewProgBar->addWidget(_viewProgBar);

//    _progBar_stacked = new QStackedLayout(bot_widget);
//    _progBar_stacked->setContentsMargins(0, 0, 0, 0);
//    _progBar_stacked->setStackingMode(QStackedLayout::StackOne);
//    _progBar_stacked->setAlignment(Qt::AlignCenter);
//    _progBar_stacked->setSpacing(0);
//    _progBar_stacked->addWidget(_progBarspec);
//    _progBar_stacked->addWidget(_progBar);
//    _progBar_stacked->addWidget(_viewProgBar);
////    _progBar_stacked->addChildLayout(viewProgBar);
//    _progBar_stacked->setCurrentIndex(0);
////    bot->addLayout(_progBar_stacked);

    _progBar_Widget->addWidget(_progBarspec);
    _progBar_Widget->addWidget(_progBar);
    _progBar_Widget->addWidget(_viewProgBar);
    _progBar_Widget->setCurrentIndex(0);
    progBarspec->addWidget(_progBar_Widget);
    bot_layout->addLayout(progBarspec);

    QHBoxLayout *timeend = new QHBoxLayout(bot_toolWgt);
    timeend->setContentsMargins(10, 10, 10, 10);
    timeend->setSpacing(0);
    timeend->setAlignment(Qt::AlignRight);
    bot_layout->addLayout(timeend);
    timeend->addWidget(_timeLabelend);

    _palyBox = new DButtonBox(bot_toolWgt);
    _palyBox->setFixedWidth(120);
    _mid->addWidget(_palyBox);
    _mid->setAlignment(_palyBox, Qt::AlignLeft);
    QList<DButtonBoxButton *> list;


//    _prevBtn = new DIconButton(this);
    _prevBtn = new VideoBoxButton("", ":/icons/deepin/builtin/light/normal/last_normal.svg",
                                  ":/icons/deepin/builtin/light/normal/last_normal.svg",
                                  ":/icons/deepin/builtin/light/press/last_press.svg");
//    _prevBtn->setIcon(QIcon::fromTheme("dcc_last"));
    _prevBtn->setIconSize(QSize(36, 36));
    _prevBtn->setFixedSize(40, 50);
    _prevBtn->setObjectName("PrevBtn");
    connect(_prevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_prevBtn, "prev");
//    _mid->addWidget(_prevBtn);
    list.append(_prevBtn);
    _playBtn = new VideoBoxButton("", ":/resources/icons/light/normal/play_normal2.svg",
                                  ":/resources/icons/light/normal/play_normal2.svg",
                                  ":/icons/deepin/builtin/light/press/play_press.svg");
//    _playBtn->setIcon(QIcon::fromTheme("dcc_play"));
    _playBtn->setIconSize(QSize(36, 36));
    _playBtn->setFixedSize(40, 50);
    connect(_playBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_playBtn, "play");
//    _mid->addWidget(_playBtn);
    list.append(_playBtn);

    _nextBtn = new VideoBoxButton("", ":/icons/deepin/builtin/light/normal/next_normal.svg",
                                  ":/icons/deepin/builtin/light/normal/next_normal.svg",
                                  ":/icons/deepin/builtin/light/press/next_press.svg");
//    _nextBtn->setIcon(QIcon::fromTheme("dcc_next"));
    _nextBtn->setIconSize(QSize(36, 36));
    _nextBtn->setFixedSize(40, 50);
    connect(_nextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_nextBtn, "next");
//    _mid->addWidget(_nextBtn);
    list.append(_nextBtn);
    _palyBox->setButtonList(list, false);
//    _palyBox->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _nextBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _playBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _prevBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);

//    bot->addStretch();

    _right = new QHBoxLayout(bot_toolWgt);
    _right->setContentsMargins(0, 0, 0, 0);
    _right->setSizeConstraint(QLayout::SetFixedSize);
    _right->setSpacing(0);
    bot_layout->addLayout(_right);

    _subBtn = new ToolButton(bot_toolWgt);
    _subBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    _subBtn->setIconSize(QSize(36, 36));
    _subBtn->setFixedSize(50, 50);
    _subBtn->initToolTip();
    connect(_subBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_subBtn, "sub");
    _right->addWidget(_subBtn);

    _subBtn->hide();

    _volBtn = new VolumeButton(bot_toolWgt);
    _volBtn->setFixedSize(50, 50);
    connect(_volBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_volBtn, "vol");
//    _right->addWidget(_volBtn);
    if (CompositingManager::get().composited()) {
        _volSlider = new VolumeSlider(_engine, _mainWindow, _mainWindow);
        connect(_volBtn, &VolumeButton::entered, [ = ]() {
            _volSlider->stopTimer();
            _volSlider->show(_mainWindow->width() - _volBtn->width() / 2 - _playBtn->width() - 43,
                             _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            _volSlider->raise();
        });
    } else {
#if defined (__mips__) || defined (__aarch64__)
        _volSlider = new VolumeSlider(_engine, _mainWindow, nullptr);
        hintFilter = new HintFilter;
        _volSlider->setProperty("DelayHide", true);
        _volSlider->setProperty("NoDelayShow", true);
        installHint(_volBtn, _volSlider);
#else
        _volSlider = new VolumeSlider(_engine, _mainWindow, _mainWindow);
        connect(_volBtn, &VolumeButton::entered, [ = ]() {
            _volSlider->stopTimer();
            _volSlider->show(_mainWindow->width() - _volBtn->width() / 2 - _playBtn->width() - 43,
                             _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            _volSlider->raise();
        });
#endif
    }
#ifdef __x86_64__
    connect(_volBtn, &VolumeButton::leaved, _volSlider, &VolumeSlider::delayedHide);
#else
    connect(_volBtn, &VolumeButton::leaved, [ = ]() {
        m_isMouseIn = false;
        _volSlider->delayedHide();
    });
#endif
    connect(_volBtn, &VolumeButton::requestVolumeUp, [ = ]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeUp);
    });
    connect(_volBtn, &VolumeButton::requestVolumeDown, [ = ]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeDown);
    });


    _fsBtn = new ToolButton(bot_toolWgt);
    _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
    _fsBtn->setIconSize(QSize(36, 36));
    _fsBtn->setFixedSize(50, 50);
    _fsBtn->initToolTip();
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");

    _right->addWidget(_fsBtn);
    _right->addSpacing(10);
    _right->addWidget(_volBtn);
    _right->addSpacing(10);

    _listBtn = new ToolButton(bot_toolWgt);
    _listBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    _listBtn->setIconSize(QSize(36, 36));
    _listBtn->setFixedSize(50, 50);
    _listBtn->initToolTip();
//    _listBtn->setFocusPolicy(Qt::FocusPolicy::TabFocus);
    _listBtn->setCheckable(true);

    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    _right->addWidget(_listBtn);

    // these tooltips is not used due to deepin ui design
    auto th = new TooltipHandler(this);
    QWidget *btns[] = {
        _playBtn, _prevBtn, _nextBtn, _subBtn, _fsBtn, _listBtn
    };
    QString hints[] = {
        tr("Play/Pause"), tr("Previous"), tr("Next"),
        tr("Subtitles"), tr("Fullscreen"), tr("Playlist")
    };
    QString attrs[] = {
        tr("play"), tr("prev"), tr("next"),
        "sub", tr("fs"), tr("list")
    };

    for (unsigned int i = 0; i < sizeof(btns) / sizeof(btns[0]); i++) {
        if (i < sizeof(btns) / sizeof(btns[0]) / 2) {
            btns[i]->setToolTip(hints[i]);
            auto t = new Tip(QPixmap(), hints[i], parentWidget());
            t->setProperty("for", QVariant::fromValue<QWidget *>(btns[i]));
            btns[i]->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
            btns[i]->installEventFilter(th);
        } else {
            auto btn = dynamic_cast<ToolButton *>(btns[i]);
            btn->setTooTipText(hints[i]);
            btn->setProperty("TipId", attrs[i]);
            connect(btn, &ToolButton::entered, this, &ToolboxProxy::buttonEnter);
            connect(btn, &ToolButton::leaved, this, &ToolboxProxy::buttonLeave);
        }
    }

    connect(_engine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(_engine, &PlayerEngine::fileLoaded, [ = ]() {
        _viewProgBar->clear();
        _progBar->slider()->setRange(0, static_cast<int>(_engine->duration()));
//        _progBar_stacked->setCurrentIndex(1);
        _progBar_Widget->setCurrentIndex(1);
        _loadsize = size();
        update();
//        updateThumbnail();
    });
    connect(_engine, &PlayerEngine::elapsedChanged, [ = ]() {
        quint64 url = static_cast<quint64>(-1);
        if (_engine->playlist().current() != -1) {
            url = static_cast<quint64>(_engine->playlist().items()[_engine->playlist().current()].mi.duration);
        }
        updateTimeInfo(static_cast<qint64>(url), _engine->elapsed(), _timeLabel, _timeLabelend, true);
        updateMovieProgress();
    });
    connect(_engine, &PlayerEngine::elapsedChanged, [ = ]() {
        quint64 _url = static_cast<quint64>(-1);
        if (_engine->playlist().current() != -1) {
            _url = static_cast<quint64>(_engine->playlist().items()[_engine->playlist().current()].mi.duration);
        }
        updateTimeInfo(static_cast<qint64>(_url), _engine->elapsed(), _fullscreentimelable, _fullscreentimelableend, false);
        QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
        _fullscreentimelable->setMinimumWidth(fm.width(_fullscreentimelable->text()));
        _fullscreentimelableend->setMinimumWidth(fm.width(_fullscreentimelableend->text()));
        updateMovieProgress();
    });

    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &ToolboxProxy::updateFullState);
    connect(_engine, &PlayerEngine::muteChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_engine, &PlayerEngine::volumeChanged, this, &ToolboxProxy::updateVolumeState);

    connect(_engine, &PlayerEngine::tracksChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_engine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::updateButtonStates);
    connect(&_engine->playlist(), &PlaylistModel::countChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_mainWindow, &MainWindow::initChanged, this, &ToolboxProxy::updateButtonStates);

    updatePlayState();
    updateFullState();
    updateButtonStates();

    ThumbnailWorker::get().setPlayerEngine(_engine);
    connect(&ThumbnailWorker::get(), &ThumbnailWorker::thumbGenerated,
            this, &ToolboxProxy::updateHoverPreview);

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);

    connect(qApp, &QGuiApplication::applicationStateChanged, [ = ](Qt::ApplicationState e) {
        if (e == Qt::ApplicationInactive && anyPopupShown()) {
            closeAnyPopup();
        }
    });

    /* _autoResizeTimer.setSingleShot(true);
     connect(&_autoResizeTimer, &QTimer::timeout, this, [ = ] {
         if (_oldsize.width() == width())
         {
             _viewProgBar->setWidth();
             if (_engine->state() != PlayerEngine::CoreState::Idle && size() != _loadsize) {
    #ifdef __mips__
                 bool bRet = QDBusInterface("com.deepin.wm", "/com/deepin/wm", "com.deepin.wm").property("compositingAllowSwitch").toBool();
                 if (bRet) { //龙芯平台存在显卡才加载缩略图
                     updateThumbnail();
                 }
    #else
                 updateThumbnail();
    #endif
                 _loadsize = size();
             }
         }
     });*/
    PlaylistModel *playListModel = _engine->getplaylist();
    connect(playListModel, &PlaylistModel::currentChanged, this, [ = ] {
        _autoResizeTimer.start(1000);
    });
}

void ToolboxProxy::installHint(QWidget *w, QWidget *hint)
{
    w->setProperty("HintWidget", QVariant::fromValue<QWidget *>(hint));
    w->installEventFilter(hintFilter);
}

void ToolboxProxy::updateThumbnail()
{
    //如果打开的是音乐
    QString suffix = _engine->playlist().currentInfo().info.suffix();
    foreach (QString sf, _engine->audio_filetypes) {
        if (sf.right(sf.size() - 2) == suffix) {
            return;
        }
    }

    qDebug() << "worker" << m_worker;

    QTimer::singleShot(1000, [this]() {

        m_listPixmapMutex.lock();
        pm_list.clear();
        pm_black_list.clear();
        m_listPixmapMutex.unlock();

        if (m_worker == nullptr) {
            m_worker = new viewProgBarLoad(_engine, _progBar, this);
            m_worker->setListPixmapMutex(&m_listPixmapMutex);
            /*connect(m_worker, &viewProgBarLoad::finished, this, [ = ] {
                if (m_worker)
                {
                    m_worker->quit();
                    m_worker->wait();
                    delete m_worker;
                    m_worker = nullptr;
                }
            });*/

            connect(m_worker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
            m_worker->start();
        }

        m_worker->load();


        _progBar_Widget->setCurrentIndex(1);
    });
}

void ToolboxProxy::updatePreviewTime(qint64 secs, const QPoint &pos)
{
    QTime time(0, 0, 0);
    QString strTime = time.addSecs(static_cast<int>(secs)).toString("hh:mm:ss");
    _previewTime->setTime(strTime);
    _previewTime->show(pos.x(), pos.y() + 14);
}

void ToolboxProxy::closeAnyPopup()
{
    if (_previewer->isVisible()) {
        _previewer->hide();
    }

    if (_previewTime->isVisible()) {
        _previewTime->hide();
    }

    if (_subView->isVisible()) {
        _subView->hide();
    }

    if (_volSlider->isVisible()) {
        _volSlider->stopTimer();
        _volSlider->hide();
    }
}

bool ToolboxProxy::anyPopupShown() const
{
    return _previewer->isVisible() || _previewTime->isVisible() || _subView->isVisible() || _volSlider->isVisible();
}

void ToolboxProxy::updateHoverPreview(const QUrl &url, int secs)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    if (_engine->playlist().currentInfo().url != url)
        return;

    if (!Settings::get().isSet(Settings::PreviewOnMouseover))
        return;

    if (_volSlider->isVisible())
        return;

    const auto &pif = _engine->playlist().currentInfo();
    if (!pif.url.isLocalFile())
        return;

    const auto &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        _previewer->hide();
        _previewTime->hide();
        return;
    }

    if (!m_mouseFlag) {
        return;
    }

    auto pos = _progBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
//    auto pos = _viewProgBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
    QPoint p { QCursor::pos().x(), pos.y() };

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);
    _previewer->updateWithPreview(pm, secs, _engine->videoRotation());
    _previewer->updateWithPreview(p);

}

void ToolboxProxy::progressHoverChanged(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

//    if (!Settings::get().isSet(Settings::PreviewOnMouseover))
//        return;

    if (_volSlider->isVisible())
        return;

    const auto &pif = _engine->playlist().currentInfo();
    if (!pif.url.isLocalFile())
        return;

    const auto &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        _previewer->hide();
        _previewTime->hide();
        return;
    }

    m_mouseFlag = true;

    _lastHoverValue = v;

    auto pos = _progBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
//    auto pos = _viewProgBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
    QPoint p { QCursor::pos().x(), pos.y() };

    //auto proBar = qobject_cast<ViewProgBar *>(sender());
    bool isAudio = _engine->isAudioFile(pif.info.fileName());
    if (/*proBar == _viewProgBar && */!Settings::get().isSet(Settings::PreviewOnMouseover) || isAudio) {
        updatePreviewTime(v, p);
        return;
    }

    ThumbnailWorker::get().requestThumb(pif.url, v);
}

void ToolboxProxy::setProgress(int v)
{
    m_mouseRelesePos = v;       //记录当前的位置，在鼠标松开的时候使用它设置进度（因为进度条会更新，在鼠标松开的嗯时候不能正确的获取进度）

    progressHoverChanged(_progBar->slider()->sliderPosition()); //更新预览图位置
    //updateMovieProgress();

    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    //    _engine->seekAbsolute(_progBar->sliderPosition());
    // _engine->seekAbsolute(v);
    if (_progBar->slider()->sliderPosition() != _lastHoverValue) {
        progressHoverChanged(_progBar->slider()->sliderPosition());
    }
    updateMovieProgress();
}

void ToolboxProxy::updateTimeVisible(bool visible)
{
    if (Settings::get().isSet(Settings::PreviewOnMouseover))
        return;

    if (_previewTime) {
        _previewTime->setVisible(!visible);
    }
}

void ToolboxProxy::updateMovieProgress()
{
    if (m_mousePree == true)
        return ;
    auto d = _engine->duration();
    auto e = _engine->elapsed();
    int v = 0;
    int v2 = 0;
    if (d != 0 && e != 0) {
        v = static_cast<int>(_progBar->maximum() * e / d);
        v2 = static_cast<int>((_viewProgBar->rect().width() - 4) * e / d);
    }
    if (!_progBar->signalsBlocked()) {
        _progBar->blockSignals(true);
        _progBar->setValue(v);
        _progBar->blockSignals(false);
    }
    if (!_viewProgBar->getIsBlockSignals()) {
        _viewProgBar->setIsBlockSignals(true);
        _viewProgBar->setValue(v2);
        _viewProgBar->setTime(e);
        _viewProgBar->setIsBlockSignals(false);
    }
}

void ToolboxProxy::updateButtonStates()
{
    qDebug() << _engine->playingMovieInfo().subs.size();
    bool vis = _engine->playlist().count() > 1 && _mainWindow->inited();
//    _prevBtn->setVisible(vis);
    _prevBtn->setDisabled(!vis);
//    _nextBtn->setVisible(vis);
    _nextBtn->setDisabled(!vis);

    vis = _engine->state() != PlayerEngine::CoreState::Idle;
    if (vis) {
        vis = _engine->playingMovieInfo().subs.size() > 0;
    }
    //_subBtn->setVisible(vis);
}

void ToolboxProxy::updateVolumeState()
{
    if (_engine->muted()) {
        _volBtn->changeLevel(VolumeButton::Mute);
        //_volBtn->setToolTip(tr("Mute"));
    } else {
        auto v = _engine->volume();
        /*if (v != 0) {
            v -= VOLUME_OFFSET;
        }*/
        //_volBtn->setToolTip(tr("Volume"));
        if (v >= 66)
            _volBtn->changeLevel(VolumeButton::High);
        else if (v >= 33)
            _volBtn->changeLevel(VolumeButton::Mid);
        else if (v == 0)
            _volBtn->changeLevel(VolumeButton::Off);
        else
            _volBtn->changeLevel(VolumeButton::Low);
    }
}

void ToolboxProxy::updateFullState()
{
    bool isFullscreen = window()->isFullScreen();
    if (isFullscreen) {
//        _fsBtn->setObjectName("UnfsBtn");
        _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomout"));
        _fsBtn->setTooTipText(tr("Exit fullscreen"));
    } else {
//        _fsBtn->setObjectName("FsBtn");
        _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
        _fsBtn->setTooTipText(tr("Fullscreen"));
    }
//    _fsBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updatePlayState()
{
    if (_engine->state() == PlayerEngine::CoreState::Playing) {
        //        _playBtn->setObjectName("PauseBtn");
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ) {
            _playBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/suspend_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/suspend_normal.svg",
                                     ":/icons/deepin/builtin/light/press/suspend_press.svg");
            _prevBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/light/press/last_press.svg");
            _nextBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/light/press/next_press.svg");
            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Button, QColor(255, 255, 255, 255));
            _palyBox->setPalette(pa);

            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _listBtn->setPalette(pa);

        } else {
            _playBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/suspend_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/suspend_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/suspend_press.svg");
            _prevBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/last_press.svg");
            _nextBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/next_press.svg");

            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 102));
            _palyBox->setPalette(pa);

            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _listBtn->setPalette(pa);


        }
        _playBtn->setToolTip(tr("Pause"));
    } else {
        //        _playBtn->setObjectName("PlayBtn");
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ) {
            _playBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/play_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/play_normal.svg",
                                     ":/icons/deepin/builtin/light/press/play_press.svg");
            _prevBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/light/press/last_press.svg");
            _nextBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/light/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/light/press/next_press.svg");

            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Button, QColor(255, 255, 255, 255));
            _palyBox->setPalette(pa);


            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(255, 255, 255, 255));
            pa.setColor(DPalette::Dark, QColor(255, 255, 255, 255));
            _listBtn->setPalette(pa);

        } else {
            _playBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/play_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/play_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/play_press.svg");
            _prevBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/last_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/last_press.svg");
            _nextBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/dark/normal/next_normal.svg",
                                     ":/icons/deepin/builtin/dark/press/next_press.svg");
            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 102));
            _palyBox->setPalette(pa);

            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 102));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 102));
            _listBtn->setPalette(pa);

        }
        _playBtn->setToolTip(tr("Play"));
    }

    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        if (_subView->isVisible())
            _subView->hide();

        if (_previewer->isVisible()) {
            _previewer->hide();
        }

        if (_previewTime->isVisible()) {
            _previewTime->hide();
        }

        if (_progBar->isVisible()) {
            _progBar->setVisible(false);
        }
//        _progBarspec->show();
//        _progBar->hide();
//        _progBar_stacked->setCurrentIndex(0);
        _progBar_Widget->setCurrentIndex(0);
        setProperty("idle", true);
    } else {
        setProperty("idle", false);
//        _progBar->show();
//        _progBar->setVisible(true);
//        _progBarspec->hide();
//        _progBar_stacked->setCurrentIndex(1);
//        _progBar_Widget->setCurrentIndex(1);
    }

    auto on = (_engine->state() != PlayerEngine::CoreState::Idle);
    _progBar->setEnabled(on);
    _progBar->setEnableIndication(on);
//    setStyleSheet(styleSheet());
}

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos, QLabel *_timeLabel, QLabel *_timeLabelend, bool flag)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        _timeLabel->setText("");
        _timeLabelend->setText("");

    } else {
        //mpv returns a slightly different duration from movieinfo.duration
        //_timeLabel->setText(QString("%2/%1").arg(utils::Time2str(duration))
        //.arg(utils::Time2str(pos)));
        if (1 == flag) {
            _timeLabel->setText(QString("%1")
                                .arg(utils::Time2str(pos)));
            _timeLabelend->setText(QString("%1")
                                   .arg(utils::Time2str(duration)));
        } else {
            _timeLabel->setText(QString("%1 %2")
                                .arg(utils::Time2str(pos)).arg("/"));
            _timeLabelend->setText(QString("%1")
                                   .arg(utils::Time2str(duration)));
        }


    }
}

void ToolboxProxy::buttonClicked(QString id)
{
    if (!isVisible()) return;

    qDebug() << __func__ << id;
    if (id == "play") {
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            _mainWindow->requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            _mainWindow->requestAction(ActionFactory::ActionKind::TogglePause);
        }
    } else if (id == "fs") {
        _mainWindow->requestAction(ActionFactory::ActionKind::ToggleFullscreen);
    } else if (id == "vol") {
        _mainWindow->requestAction(ActionFactory::ActionKind::ToggleMute);
    } else if (id == "prev") {
        _mainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistPrev);
    } else if (id == "next") {
        _mainWindow->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);
    } else if (id == "list") {
        _mainWindow->requestAction(ActionFactory::ActionKind::TogglePlaylist);
        _listBtn->hideToolTip();
    } else if (id == "sub") {
        _subView->setVisible(true);

        QPoint pos = _subBtn->parentWidget()->mapToGlobal(_subBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        _subView->show(pos.x() + _subBtn->width() / 2, pos.y() - 5 + TOOLBOX_TOP_EXTENT);
    }
}

void ToolboxProxy::buttonEnter()
{
    if (!isVisible()) return;

    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == tr("sub") || id == tr("fs") || id == tr("list")) {
        updateToolTipTheme(btn);
        btn->showToolTip();
    }
}

void ToolboxProxy::buttonLeave()
{
    if (!isVisible()) return;

    ToolButton *btn = qobject_cast<ToolButton *>(sender());
    QString id = btn->property("TipId").toString();

    if (id == tr("sub") || id == tr("fs") || id == tr("list")) {
        btn->hideToolTip();
    }
}

void ToolboxProxy::updatePosition(const QPoint &p)
{
    QPoint pos(p);
    pos.ry() += _mainWindow->height() - height();
    windowHandle()->setFramePosition(pos);
}

//void ToolboxProxy::paintEvent(QPaintEvent *pe)
//{
//    QPainter painter(this);
//    painter.setRenderHint(QPainter::Antialiasing);
//    QRectF bgRect;
//    bgRect.setSize(size());
//    const QPalette pal = QGuiApplication::palette();//this->palette();
//    static int offset = 14;

//    DGuiApplicationHelper::ColorType themeType = DGuiApplicationHelper::instance()->themeType();
//    QColor *bgColor, outBdColor, inBdColor;
//    if (themeType == DGuiApplicationHelper::LightType) {
//        outBdColor = QColor(0, 0, 0, 25);
//        inBdColor = QColor(247, 247, 247, 0.4 * 255);
//        bgColor = new QColor(247, 247, 247, 0.8 * 255);
//    } else if (themeType == DGuiApplicationHelper::DarkType) {
//        outBdColor = QColor(0, 0, 0, 0.8 * 255);
//        inBdColor = QColor(255, 255, 255, 0.05 * 255);
//        bgColor = new QColor(32, 32, 32, 0.9 * 255);
//    } else {
//        outBdColor = QColor(0, 0, 0, 25);
//        inBdColor = QColor(247, 247, 247, 0.4 * 255);
//        bgColor = new QColor(247, 247, 247, 0.8 * 255);
//    }

//    {
//        QPainterPath pp;
//        pp.setFillRule(Qt::WindingFill);
//        QPen pen(outBdColor, 1);
//        painter.setPen(pen);
//        pp.addRoundedRect(bgRect, RADIUS_MV, RADIUS_MV);
//        painter.fillPath(pp, *bgColor);
////        painter.drawPath(pp);

//        painter.drawLine(offset, rect().y(), width() - offset, rect().y());
//        painter.drawLine(offset, height(), width() - offset, height());
//        painter.drawLine(rect().x(), offset, rect().x(), height() - offset);
//        painter.drawLine(width(), offset, width(), height() - offset);
//    }

////    {
////        auto view_rect = bgRect.marginsRemoved(QMargins(1, 1, 1, 1));
////        QPainterPath pp;
////        pp.setFillRule(Qt::WindingFill);
////        painter.setPen(inBdColor);
////        pp.addRoundedRect(view_rect, RADIUS_MV, RADIUS_MV);
////        painter.drawPath(pp);
////    }

//    QWidget::paintEvent(pe);
//}

void ToolboxProxy::showEvent(QShowEvent *event)
{
    updateTimeLabel();

    DFloatingWidget::showEvent(event);
}

void ToolboxProxy::resizeEvent(QResizeEvent *event)
{

    if (_autoResizeTimer.isActive()) {
        _autoResizeTimer.stop();
    }
    if (event->oldSize().width() != event->size().width()) {
        _autoResizeTimer.start(1000);
        _oldsize = event->size();
//        _progBar->setFixedWidth(width() - PROGBAR_SPEC);
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            _progBar_Widget->setCurrentIndex(1);
        }

    }
#ifndef __sw_64__
    if (bAnimationFinash ==  false && paopen != nullptr && paClose != nullptr) {

        _playlist->endAnimation();
        paopen->setDuration(0);
        paClose->setDuration(0);
    }


    if (_playlist->state() == PlaylistWidget::State::Opened && bAnimationFinash == true) {
        QRect r(5, _mainWindow->height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7) - _mainWindow->rect().top() - 5,
                _mainWindow->rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7));
        this->setGeometry(r);
    } else if (_playlist->state() == PlaylistWidget::State::Closed && bAnimationFinash == true) {
        QRect r(5, _mainWindow->height() - TOOLBOX_HEIGHT - _mainWindow->rect().top() - 5,
                _mainWindow->rect().width() - 10, TOOLBOX_HEIGHT);
        this->setGeometry(r);
    }

    updateTimeLabel();
#endif

    DFloatingWidget::resizeEvent(event);
}


void ToolboxProxy::updateTimeLabel()
{

#ifndef __sw_64__
    // to keep left and right of the same width. which makes play button centered
    _listBtn->setVisible(width() > 300);
    _timeLabel->setVisible(width() > 450);
    _timeLabelend->setVisible(width() > 450);
//    _viewProgBar->setVisible(width() > 350);
//    _progBar->setVisible(width() > 350);
    if (_mainWindow->width() < 1050) {
//        _progBar->hide();
    }
    if (width() <= 300) {
        _progBar->setFixedWidth(width() - PROGBAR_SPEC + 50 + 54 + 10 + 54 + 10 + 10);
        _progBarspec->setFixedWidth(width() - PROGBAR_SPEC + 50 + 54 + 10 + 54 + 10 + 10);
    } else if (width() <= 450) {
        _progBar->setFixedWidth(width() - PROGBAR_SPEC + 54 + 54 + 10);
        _progBarspec->setFixedWidth(width() - PROGBAR_SPEC + 54 + 54 + 10);
    }

//    if (width() > 400) {
//        auto right_geom = _right->geometry();
//        int left_w = 54;
//        _timeLabel->show();
//        _timeLabelend->show();
//        int w = qMax(left_w, right_geom.width());
////        int w = left_w;
//        _timeLabel->setFixedWidth(left_w );
//        _timeLabelend->setFixedWidth(left_w );
//        right_geom.setWidth(w);
//        _right->setGeometry(right_geom);
    //    }
#endif
}

void ToolboxProxy::updateToolTipTheme(ToolButton *btn)
{
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        btn->changeTheme(lightTheme);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        btn->changeTheme(darkTheme);
    } else {
        btn->changeTheme(lightTheme);
    }
}

void ToolboxProxy::setViewProgBarWidth()
{
    _viewProgBar->setWidth();
}

void ToolboxProxy::setPlaylist(PlaylistWidget *playlist)
{
    _playlist = playlist;
    WAYLAND_BLACK_WINDOW;

    connect(_playlist, &PlaylistWidget::stateChange, this, [ = ]() {


        if (bAnimationFinash == false) {
            return ;
        }

        if (_playlist->state() == PlaylistWidget::State::Opened) {
#ifndef __sw_64__
            QRect rcBegin = this->geometry();
            QRect rcEnd = rcBegin;
            rcEnd.setY(rcBegin.y() - TOOLBOX_SPACE_HEIGHT - 7);
            bAnimationFinash = false;
            paopen = new QPropertyAnimation(this, "geometry");
            paopen->setEasingCurve(QEasingCurve::Linear);
            paopen->setDuration(POPUP_DURATION  ) ;
            paopen->setStartValue(rcBegin);
            paopen->setEndValue(rcEnd);
            paopen->start();
            connect(paopen, &QPropertyAnimation::finished, [ = ]() {
                paopen->deleteLater();
                paopen = nullptr;
                bAnimationFinash = true;
            });
#endif
            _listBtn->setChecked(true);
        } else {
            _listBtn->setChecked(false);
#ifndef __sw_64__
            bAnimationFinash = false;

            QRect rcBegin = this->geometry();
            QRect rcEnd = rcBegin;
            rcEnd.setY(rcBegin.y() + TOOLBOX_SPACE_HEIGHT + 7);
            paClose = new QPropertyAnimation(this, "geometry");
            paClose->setEasingCurve(QEasingCurve::Linear);
            paClose->setDuration(POPUP_DURATION );
            paClose->setStartValue(rcBegin);
            paClose->setEndValue(rcEnd);
            paClose->start();
            connect(paClose, &QPropertyAnimation::finished, [ = ]() {
                paClose->deleteLater();
                paClose = nullptr;
                bAnimationFinash = true;
            });
#endif
        }
    });
}
QLabel *ToolboxProxy::getfullscreentimeLabel()
{
    return _fullscreentimelable;
}

QLabel *ToolboxProxy::getfullscreentimeLabelend()
{
    return _fullscreentimelableend;
}

bool ToolboxProxy::getbAnimationFinash()
{
    return  bAnimationFinash;
}

int ToolboxProxy::DisplayVolume()
{
    return _volSlider->value();
}
}

#include "toolbox_proxy.moc"
