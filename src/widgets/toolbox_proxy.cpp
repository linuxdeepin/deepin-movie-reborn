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

static const int LEFT_MARGIN = 10;
static const int RIGHT_MARGIN = 10;
static const int PROGBAR_SPEC = 10+120+17+54+10+54+10+170+10;

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

class TooltipHandler: public QObject {
public:
    TooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        switch (event->type()) {
            case QEvent::ToolTip: {
                QHelpEvent *he = static_cast<QHelpEvent *>(event);
                auto tip = obj->property("HintWidget").value<Tip*>();
                auto btn = tip->property("for").value<QWidget*>();
                tip->setText(btn->toolTip());
                tip->show();
                tip->raise();
                tip->adjustSize();

                auto mw = tip->parentWidget();
                auto sz = tip->size();

                QPoint pos = btn->parentWidget()->mapToParent(btn->pos());
                pos.ry() = mw->rect().bottom() - 65 - sz.height();
                pos.rx() = pos.x() - sz.width()/2 + btn->width()/2;
                tip->move(pos);
                return true;
            }

            case QEvent::Leave: {
                auto parent = obj->property("HintWidget").value<Tip*>();
                parent->hide();
                event->ignore();

            }
            default: break;
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
};

class SubtitlesView;
class SubtitleItemWidget: public QWidget {
    Q_OBJECT
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *parent, SubtitleInfo si): QWidget() {
        _sid = si["id"].toInt();

//        DThemeManager::instance()->registerWidget(this, QStringList() << "current");

        setFixedWidth(200);

        auto *l = new QHBoxLayout(this);
        setLayout(l);
        l->setContentsMargins(0, 0, 0, 0);

        _msg = si["title"].toString();
        auto shorted = fontMetrics().elidedText(_msg, Qt::ElideMiddle, 140*2);
        _title = new QLabel(shorted);
        _title->setWordWrap(true);
        l->addWidget(_title, 1);

        _selectedLabel = new QLabel(this);
        l->addWidget(_selectedLabel);

        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
                this, &SubtitleItemWidget::onThemeChanged);
        onThemeChanged();
    }

    int sid() const { return _sid; }

    void setCurrent(bool v)
    {
        if (v) {
            auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(qApp->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        } else {
            _selectedLabel->clear();
        }

        setProperty("current", v?"true":"false");
//        setStyleSheet(this->styleSheet());
        style()->unpolish(_title);
        style()->polish(_title);
    }

protected:
    void showEvent(QShowEvent *se) override
    {
        auto fm = _title->fontMetrics();
        auto shorted = fm.elidedText(_msg, Qt::ElideMiddle, 140*2);
        int h = fm.height();
        if (fm.width(shorted) > 140) {
            h *= 2;
        } else {
        }
        _title->setFixedHeight(h);
        _title->setText(shorted);
    }

private slots:
    void onThemeChanged() {
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

class SubtitlesView: public DArrowRectangle {
    Q_OBJECT
public:
    SubtitlesView(QWidget *p, PlayerEngine* e)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, p), _engine{e} {
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

        for (const auto& sub: pmf.subs) {
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
            auto siw = static_cast<SubtitleItemWidget*>(_subsView->itemWidget(_subsView->item(i)));
            siw->setCurrent(siw->sid() == sid);
        }

        qDebug() << "current " << _subsView->currentRow();
    }

    void onItemClicked(QListWidgetItem* item)
    {
        auto id = _subsView->row(item);
        _engine->selectSubtitle(id);
    }

private:
    PlayerEngine *_engine {nullptr};
    QListWidget *_subsView {nullptr};
};
class IndicatorLayout: public QHBoxLayout{
    Q_OBJECT
public:
    IndicatorLayout(QWidget *parent = 0){

    }
protected:
    void paintEvent(QPaintEvent *e)
    {
//        QPainter p(this);
//        QRect r(_indicatorPos, QSize{4, 60});
////        p.drawText(this->rect(),Qt::AlignCenter,"this is my widget");
//        p.fillRect(r, QBrush(_indicatorColor));
    }
};
class ViewProgBarItem: public QLabel{
    Q_OBJECT
public:
    ViewProgBarItem(QImage *image, QWidget *parent = 0){

    }
};
class ViewProgBar: public DWidget{
    Q_OBJECT
public:
    ViewProgBar(QWidget *parent = 0){
        _parent = parent;
       setFixedHeight(70);
//       setFixedWidth(584);
       setFixedWidth(parent->width() - PROGBAR_SPEC);
//       setFixedWidth(1450);
       _vlastHoverValue = 0;
       _isBlockSignals=false;
       setMouseTracking(true);

       _back = new QWidget(this);
       _back->setFixedHeight(60);
       _back->setFixedWidth(this->width());
       _back->setContentsMargins(0,0,0,0);

       _front = new QWidget(this);
       _front->setFixedHeight(60);
       _front->setFixedWidth(0);
       _front->setContentsMargins(0,0,0,0);

       _indicator = new DBlurEffectWidget(this);
       _indicator->setFixedHeight(60);
       _indicator->setFixedWidth(2);
       _indicator->setObjectName("indicator");
       _indicator->setMaskAlpha(153);
       _indicator->setMaskColor(QColor(255,138,0));
       _indicator->setBlurRectXRadius(2);
       _indicator->setBlurRectYRadius(2);
       _slider = new DLabel(this);
       _slider->setFixedSize(10,7);
       _slider->setPixmap(QPixmap(":resources/icons/slider.svg").copy(5,0,10,7));
//       DBlurEffectWidget *indin = new DBlurEffectWidget(_indicator);
//       _indicator->setContentsMargins(1,1,0,0);
//       indin.setTopMargin(1);
//       indin->move(1,1);
//       indin.setLeftMargin(1);
//       indin->setFixedHeight(58);
//       indin->setFixedWidth(2);
//       indin->setMaskAlpha(255);
//       indin->setMaskColor(QColor(255,255,255));
//       indin->setBlurRectXRadius(2);
//       indin->setBlurRectYRadius(2);
//       _indicator->setStyleSheet("QWidget#indicator{border: 1px solid #000000; border-radius: 5px;};");//needtomodify
       _back->setMouseTracking(true);
       _front->setMouseTracking(true);
       _indicator->setMouseTracking(true);
       _viewProgBarLayout = new QHBoxLayout();
       _viewProgBarLayout->setContentsMargins(5,5,5,5);
       _back->setLayout(_viewProgBarLayout);

       _viewProgBarLayout_black = new QHBoxLayout();
       _viewProgBarLayout_black->setContentsMargins(5,5,5,5);
       _front->setLayout(_viewProgBarLayout_black);

    };
//    virtual ~ViewProgBar();
    void setIsBlockSignals(bool isBlockSignals){
        _isBlockSignals = isBlockSignals;
    }
    bool getIsBlockSignals(){return _isBlockSignals;}
    void setValue(int v){
        _indicatorPos = {v<5?5:v,rect().y()};
        update();
    }
    QImage GraizeImage( const QImage& image ){
        int w =image.width();
        int h = image.height();
        QImage iGray(w,h, QImage::Format_ARGB32);

        for(int i=0; i<w;i++)
        {
            for(int j=0; j<h;j++)
            {
                QRgb pixel = image.pixel(i,j);
                int gray = qGray(pixel);
                QRgb grayPixel = qRgb(gray,gray,gray);
                QColor color(gray,gray,gray,qAlpha(pixel));
                iGray.setPixel(i,j,color.rgba());
            }
        }
        return iGray;

    }

    void setViewProgBar(PlayerEngine *engine ,QList<QPixmap>pm_list , QList<QPixmap>pm_black_list ){

//        _viewProgBarLoad =new viewProgBarLoad(engine);
        _engine = engine;
        QLayoutItem *child;
         while ((child = _viewProgBarLayout->takeAt(0)) != 0)
         {
                //setParent为NULL，防止删除之后界面不消失
                if(child->widget())
                {
                    child->widget()->setParent(NULL);
                }

                delete child;
         }

         while ((child = _viewProgBarLayout_black->takeAt(0)) != 0)
         {
                //setParent为NULL，防止删除之后界面不消失
                if(child->widget())
                {
                    child->widget()->setParent(NULL);
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
        for(int i =0; i<pm_list.count();i++){
//            ImageItem *label = new ImageItem(pm_list.at(i));
//            label->setFixedSize(8,50);
//            _viewProgBarLayout->addWidget(label, 0 , Qt::AlignLeft );
            ImageItem *label = new ImageItem(pm_list.at(i),false,_back);
            label->move(i*9+5,5);
            label->setFixedSize(8,50);


            ImageItem *label_black = new ImageItem(pm_black_list.at(i),true,_front);
            label_black->move(i*9+5,5);
            label_black->setFixedSize(8,50);
        }

        labelList = _viewProgBarLayout->findChildren<QLabel*>();
        update();


    }
    void setWidth(){
        setFixedWidth(_parent->width()-PROGBAR_SPEC);
        _back->setFixedWidth(_parent->width()-PROGBAR_SPEC);

    }
signals:
    void leaveViewProgBar();
    void hoverChanged(int);
    void sliderMoved(int);
    void indicatorMoved(int);

protected:

    void leaveEvent(QEvent *e) override
    {
        emit leaveViewProgBar();
    }

    void showEvent(QShowEvent *se) override
    {
//        _time->move((width() - _time->width())/2, 69);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!isEnabled()) return;

        int v = position2progress(e->pos());
        if ( e->pos().x() >= 0 && e->pos().x() <= contentsRect().width() ) {
            if (e->buttons() & Qt::LeftButton){
                int distance = (e->pos() - _startPos).manhattanLength();
                if (distance >= QApplication::startDragDistance()){
                    emit sliderMoved(v);
                    emit hoverChanged(v);
                }
            }else {
                qDebug() << v;
                if (_vlastHoverValue != v) {
                    emit hoverChanged(v);
                }
                _vlastHoverValue = v;
            }
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e)
    {
        if (e->buttons() == Qt::LeftButton && isEnabled()) {
//            QSlider::mousePressEvent(e);
            _startPos = e->pos();

            int v = position2progress(e->pos());;
//            setSliderPosition(v);
            emit sliderMoved(v);
            emit hoverChanged(v);
//            _down = true;
        }
    }
    void paintEvent(QPaintEvent *e)
    {
        _indicator->move(_indicatorPos);
        _slider->move(_indicatorPos.x()-4,56);
        _front->setFixedWidth(_indicatorPos.x());
    }
    void resizeEvent(QResizeEvent *event){
        auto i = _parent->width();
        auto j = this->width();
        setFixedWidth(_parent->width()-PROGBAR_SPEC);
        _back->setFixedWidth(this->width());
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
    DBlurEffectWidget *_indicator{nullptr};
    DLabel *_slider{nullptr};
    QGraphicsColorizeEffect *m_effect{nullptr};
    QList<QLabel*> labelList ;
    QHBoxLayout *_indicatorLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout_black{nullptr};
    int position2progress(const QPoint& p)
    {
        auto total = _engine->duration();
        qreal span = (qreal)total / contentsRect().width();
        return span * (p.x());
    }

};

class ThumbnailTime:public QLabel{
    Q_OBJECT
public:
    ThumbnailTime(QWidget *parent = nullptr):QLabel(parent){

    }
protected:
    void paintEvent(QPaintEvent *pe) override{
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRectF bgRect;
        bgRect.setSize(size());
        const QPalette pal = QGuiApplication::palette();//this->palette();
        QColor bgColor = pal.color(QPalette::Highlight);
        QPainterPath pp;
        pp.addRoundedRect(bgRect, 8, 8);
        painter.fillPath(pp, bgColor);
    }
};
class ThumbnailPreview: public DArrowRectangle {
    Q_OBJECT
public:
    ThumbnailPreview(): DArrowRectangle(DArrowRectangle::ArrowBottom) {
        setAttribute(Qt::WA_DeleteOnClose);
        // FIXME(hualet): Qt::Tooltip will cause Dock to show up even
        // the player is in fullscreen mode.
//        setWindowFlags(Qt::Tool);

        setObjectName("ThumbnailPreview");

        setFixedSize(ThumbnailWorker::thumbSize().width(),ThumbnailWorker::thumbSize().height()+10);

//        setShadowBlurRadius(8);
//        setRadius(8);
        setBorderWidth(1);
        setBorderColor(QColor(0,0,0,25));
        setShadowYOffset(4);
        setShadowXOffset(0);
        setArrowWidth(18);
        setArrowHeight(10);
//        DPalette pa_cb = DApplicationHelper::instance()->palette(this);
//        pa_cb.setBrush(QPalette::Background, QColor(0,129,255,1));
//        pa_cb.setBrush(QPalette::Dark, QColor(0,129,255,1));
//        setPalette(pa_cb);
        setBackgroundColor(QColor(0,129,255,255));

        auto *l = new QVBoxLayout;
        l->setContentsMargins(0, 0, 0, 10);
        setLayout(l);

        _thumb = new QLabel(this);
        _thumb->setFixedSize(ThumbnailWorker::thumbSize());
        l->addWidget(_thumb,Qt::AlignTop);


        _timebg = new ThumbnailTime(this);
        _timebg->setFixedSize(58, 20);
        _time = new QLabel(this);
        _time->setAlignment(Qt::AlignCenter);
        _time->setFixedSize(58, 20);

//        _time->setAutoFillBackground(true);
        DPalette pa_cb = DApplicationHelper::instance()->palette(_time);
        pa_cb.setBrush(QPalette::Text, QColor(255,255,255,255));
//        pa_cb.setBrush(QPalette::Dark, QColor(0,129,255,1));
        _time->setPalette(pa_cb);
        _time->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T8));

        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
                this, &ThumbnailPreview::updateTheme);
        updateTheme();

        winId(); // force backed window to be created
    }

    void updateWithPreview(const QPixmap& pm, qint64 secs, int rotation) {
        auto rounded = utils::MakeRoundedPixmap(pm, 4, 4, rotation);
        _thumb->setPixmap(rounded);

        QTime t(0, 0, 0);
        t = t.addSecs(secs);
        _time->setText(t.toString("hh:mm:ss"));
        _time->move((width() - _time->width())/2, ThumbnailWorker::thumbSize().height()-20);
        _timebg->move((width() - _time->width())/2, ThumbnailWorker::thumbSize().height()-20);

        if (isVisible()) {
//            move(QCursor::pos().x(), frameGeometry().y() + height()+0);
        }
    }

    void updateWithPreview(const QPoint& pos) {
        resizeWithContent();
//        move(pos.x(), pos.y()+0);
        show(pos.x(), pos.y()+18);
    }

signals:
    void leavePreview();

protected slots:
    void updateTheme()
    {
        if (qApp->theme() == "dark") {
//            setBackgroundColor(QColor(23, 23, 23, 255 * 8 / 10));
//            setBorderColor(QColor(255, 255 ,255, 25));
//            _time->setStyleSheet(R"(
//                border-radius: 3px;
//                background-color: rgba(23, 23, 23, 0.8);
//                font-size: 12px;
//                color: #ffffff;
//            )");
        } else {
//            setBackgroundColor(QColor(255, 255, 255, 255 * 8 / 10));
//            setBorderColor(QColor(0, 0 ,0, 25));
//            _time->setStyleSheet(R"(
//                border-radius: 3px;
//                background-color: rgba(255, 255, 255, 0.8);
//                font-size: 12px;
//                color: #303030;
//            )");
        }
    }

protected:
    void leaveEvent(QEvent *e) override
    {
        emit leavePreview();
    }

    void showEvent(QShowEvent *se) override
    {
        _time->move((width() - _time->width())/2, ThumbnailWorker::thumbSize().height()-20);
        _timebg->move((width() - _time->width())/2, ThumbnailWorker::thumbSize().height()-20);
    }

private:
    QLabel *_thumb;
    QLabel *_time;
    ThumbnailTime *_timebg;
};

class VolumeSlider: public DArrowRectangle {
    Q_OBJECT
public:
    VolumeSlider(PlayerEngine* eng, MainWindow* mw)
        :DArrowRectangle(DArrowRectangle::ArrowBottom), _engine(eng), _mw(mw) {
        setFixedSize(QSize(62, 201));
//        setWindowFlags(Qt::Tool);

        setShadowBlurRadius(4);
        setRadius(18);
        setShadowYOffset(3);
        setShadowXOffset(0);
        setArrowWidth(20);
        setArrowHeight(15);
        setFocusPolicy(Qt::NoFocus);

//        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &VolumeSlider::updateBg);

//        updateBg();

        auto *l = new QHBoxLayout;
        l->setContentsMargins(0, 4, 0, 10);
        setLayout(l);

        _slider = new DSlider(Qt::Vertical,this);
        _slider->setLeftIcon(QIcon::fromTheme("dcc_volumelessen"));
        _slider->setRightIcon(QIcon::fromTheme("dcc_volumeadd"));
        _slider->setIconSize(QSize(20,20));
        _slider->installEventFilter(this);
        _slider->show();
        _slider->slider()->setRange(0, 100);
//        _slider->slider()->setOrientation(Qt::Vertical);

        _slider->setValue(_engine->volume());
        l->addWidget(_slider,Qt::AlignHCenter);


        connect(_slider, &DSlider::valueChanged, [=]() {
            _mw->requestAction(ActionFactory::ChangeVolume, false, QList<QVariant>() << _slider->value());
        });

        _autoHideTimer.setSingleShot(true);
        connect(&_autoHideTimer, &QTimer::timeout, this, &VolumeSlider::hide);

        connect(_engine, &PlayerEngine::volumeChanged, [=]() {
            _slider->setValue(_engine->volume());
        });
    }


    ~VolumeSlider() {
//        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &VolumeSlider::updateBg);
    }

    void stopTimer() {
        _autoHideTimer.stop();
    }

public slots:
    void delayedHide() {
        _autoHideTimer.start(500);
    }

protected:
    void enterEvent(QEvent* e) {
        _autoHideTimer.stop();
    }

    void showEvent(QShowEvent* se) {
        _autoHideTimer.stop();
    }

    void leaveEvent(QEvent* e) {
        _autoHideTimer.start(500);
    }

private slots:
    void updateBg() {
//        if (qApp->theme() == "dark") {
//            setBackgroundColor(QColor(49, 49, 49, 255 * 9 / 10));
//        } else {
//            setBackgroundColor(QColor(255, 255, 255, 255 * 9 / 10));
//        }
    }

    bool eventFilter(QObject *obj, QEvent *e) {
    if (e->type() == QEvent::Wheel) {
        QWheelEvent *we = static_cast<QWheelEvent*>(e);
        qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
        if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
            if (_slider->value() == _slider->maximum() && we->angleDelta().y() > 0) {
                //keep increasing volume
                _mw->requestAction(ActionFactory::VolumeUp);
            }
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
};

viewProgBarLoad::viewProgBarLoad(PlayerEngine *engine,DMRSlider *progBar,ToolboxProxy *parent){
       _parent = parent;
       _engine = engine;
       _progBar = progBar;
}

QImage viewProgBarLoad::GraizeImage( const QImage& image ){
    int w =image.width();
    int h = image.height();
    QImage iGray(w,h, QImage::Format_ARGB32);

    for(int i=0; i<w;i++)
    {
        for(int j=0; j<h;j++)
        {
            QRgb pixel = image.pixel(i,j);
            int gray = qGray(pixel);
            QRgb grayPixel = qRgb(gray,gray,gray);
            QColor color(gray,gray,gray,qAlpha(pixel));
            iGray.setPixel(i,j,color.rgba());
        }
    }
    return iGray;
}

void viewProgBarLoad::loadViewProgBar(QSize size){

    if(isLoad) {
        emit finished();
        return;
    }
    isLoad = true;
    int num = (_progBar->width())/9;
    auto tmp = (_engine->duration()*1000)/num;
    auto dpr = qApp->devicePixelRatio();
    QList<QPixmap> pm;
//    pm.setDevicePixelRatio(dpr);
    QList<QPixmap> pm_black;
//    pm_black.setDevicePixelRatio(dpr);
    VideoThumbnailer thumber;
    QTime d(0, 0, 0,0);
    thumber.setThumbnailSize(_engine->videoSize().width() * qApp->devicePixelRatio());
    thumber.setMaintainAspectRatio(true);
    thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
    auto url = _engine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

      for(auto i=0;i<num;i++){
          d = d.addMSecs(tmp);
        thumber.setSeekTime(d.toString("hh:mm:ss:ms").toStdString());
        try {
            std::vector<uint8_t> buf;
            thumber.generateThumbnail(file.toUtf8().toStdString(),
                    ThumbnailerImageType::Png, buf);

            auto img = QImage::fromData(buf.data(), buf.size(), "png");
            auto img_tmp = img.scaledToHeight(50);


            pm.append(QPixmap::fromImage(img_tmp.copy(img_tmp.size().width()/2-4,0,8,50)));
            QImage img_black = GraizeImage(img_tmp);
            pm_black.append(QPixmap::fromImage(img_black.copy(img_black.size().width()/2-4,0,8,50)));



//            ImageItem *label = new ImageItem(img_tmp);
//            label->setFixedSize(8,50);
//            _parent->addLabel_list(label);

//            ImageItem *label_black = new ImageItem(img_tmp,true,_front);
//            label_black->setFixedSize(8,50);
//            _parent->addLabel_black_list(label_black);
        } catch (const std::logic_error&) {
        }

    }
      _parent->addpm_list(pm);
      _parent->addpm_black_list(pm_black);
      emit sigFinishiLoad(size);
      emit finished();


}

ToolboxProxy::ToolboxProxy(QWidget *mainWindow, PlayerEngine *proxy)
    :DFrame(mainWindow),
    _mainWindow(static_cast<MainWindow*>(mainWindow)),
    _engine(proxy)
{
    bool composited = CompositingManager::get().composited();
    setFrameShape(QFrame::NoFrame);
//    setFrameShadow(QFrame::Plain);
    setLineWidth(0);
//    setAutoFillBackground(false);
//    setAttribute(Qt::WA_TranslucentBackground);
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint|Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

//    QGraphicsDropShadowEffect* shadowEffect = new QGraphicsDropShadowEffect(this);
//    shadowEffect->setOffset(0, 2);
//    shadowEffect->setColor(QPalette::Shadow);
//    shadowEffect->setBlurRadius(4);
//    setGraphicsEffect(shadowEffect);



//    DThemeManager::instance()->registerWidget(this);
    label_list.clear();
    label_black_list.clear();
    pm_list.clear();
    pm_black_list.clear();

    _previewer = new ThumbnailPreview;
    _previewer->hide();

    _subView = new SubtitlesView(0, _engine);
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
}
void ToolboxProxy::finishLoadSlot(QSize size){

    _viewProgBar->setViewProgBar(_engine,pm_list,pm_black_list);
    if(CompositingManager::get().composited() && _loadsize == size){
        _progBar_Widget->setCurrentIndex(2);
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
}

void ToolboxProxy::setup()
{
    auto *stacked = new QStackedLayout(this);
    stacked->setContentsMargins(0, 0, 0, 0);
    stacked->setStackingMode(QStackedLayout::StackAll);
    setLayout(stacked);

    _progBarspec = new DWidget();
    _progBarspec->setFixedHeight(12+TOOLBOX_TOP_EXTENT);
//    _progBarspec->setFixedWidth(584);
//    _progBarspec->setFixedWidth(1450);
    _progBar = new DMRSlider();
    _progBar->setObjectName("MovieProgress");
    _progBar->slider()->setOrientation(Qt::Horizontal);
    _progBar->setFixedHeight(60);
//    _progBar->setFixedWidth(584);
//    _progBar->setFixedWidth(1450);
    _progBar->slider()->setRange(0, 100);
    _progBar->setValue(0);
    _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);
//    _progBar->hide();
    connect(_previewer, &ThumbnailPreview::leavePreview, [=]() {
        auto pos = _progBar->mapFromGlobal(QCursor::pos());
        if (!_progBar->geometry().contains(pos)) {
            _previewer->hide();
            _progBar->forceLeave();
        }
    });

    connect(_progBar, &DSlider::sliderMoved, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DSlider::valueChanged, this, &ToolboxProxy::setProgress);
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
//    connect(_progBar, &DMRSlider::leave, [=]() { _previewer->hide(); });
    connect(&Settings::get(), &Settings::baseChanged,
        [=](QString sk, const QVariant& val) {
            if (sk == "base.play.mousepreview") {
                _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);
            }
        });
    connect(_progBar, &DMRSlider::enter,[=](){
        if(_engine->state() == PlayerEngine::CoreState::Playing || _engine->state() == PlayerEngine::CoreState::Paused){
//            _viewProgBar->show();
//            _progBar->hide();
//            _progBar_stacked->setCurrentIndex(2);
//            _progBar_Widget->setCurrentIndex(2);
        }

    });
//    stacked->addWidget(_progBar);


    auto *bot_widget = new QWidget;
    auto *botv = new QVBoxLayout();
    botv->setContentsMargins(0, 0, 0, 0);
//    auto *bot = new QHBoxLayout();
    _bot_spec = new QWidget;
    _bot_spec->setFixedHeight(310);
    _bot_spec->setFixedWidth(width());
    _bot_spec->hide();
    botv->addWidget(_bot_spec);
    auto *bot = new QHBoxLayout();
    bot->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    botv->addLayout(bot);
    bot_widget->setLayout(botv);
    stacked->addWidget(bot_widget);
//    QPalette palette;
//    palette.setColor(QPalette::Background, QColor(0,0,0,255)); // 最后一项为透明度
//    bot_widget->setPalette(palette);

    _timeLabel = new QLabel("");
//    _timeLabel->setFixedWidth(_timeLabel->fontMetrics().width("99:99:99/99:99:99"));
    _timeLabel->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    _timeLabel->setFixedWidth(54);
//    bot->addWidget(_timeLabel);
    _timeLabelend = new QLabel("");
    _timeLabelend->setFixedWidth(54);
//    _timeLabel->setFixedWidth(_timeLabel->fontMetrics().width("99:99:99/99:99:99"));
//    _timeLabelend->setFixedWidth(_timeLabelend->fontMetrics().width("99:99:99"));
    _timeLabelend->setFont(DFontSizeManager::instance()->get(DFontSizeManager::T6));

    _viewProgBar = new ViewProgBar(this);

//    _viewProgBar->hide();
    _viewProgBar->setFocusPolicy(Qt::NoFocus);


    connect(_viewProgBar,&ViewProgBar::leaveViewProgBar,[=](){
//        _viewProgBar->hide();
//        _progBar->show();
//        _progBarspec->hide();
//        _progBar_stacked->setCurrentIndex(1);
//        _progBar_Widget->setCurrentIndex(1);
        _previewer->hide();
    });
    connect(_viewProgBar, &ViewProgBar::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_viewProgBar, &ViewProgBar::sliderMoved, this, &ToolboxProxy::setProgress);

    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString&)>(&QSignalMapper::mapped),
            this, &ToolboxProxy::buttonClicked);

//    bot->addStretch();

    _mid = new QHBoxLayout();
    _mid->setContentsMargins(0, 0, 0, 0);
    _mid->setSpacing(0);
    _mid->setAlignment(Qt::AlignLeft);
    bot->addLayout(_mid);


    QHBoxLayout *time = new QHBoxLayout();
    time->setContentsMargins(17, 0, 0, 0);
    time->setSpacing(0);
    time->setAlignment(Qt::AlignLeft);
    bot->addLayout(time);
    time->addWidget(_timeLabel);

//    bot->addStretch();

    QHBoxLayout *progBarspec = new QHBoxLayout();
    progBarspec->setContentsMargins(0, 5, 0, 0);
    progBarspec->setSpacing(0);
    progBarspec->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(progBarspec);
//    progBarspec->addWidget(_progBarspec);

    QHBoxLayout *progBar = new QHBoxLayout();
    progBar->setContentsMargins(0, 0, 0, 0);
    progBar->setSpacing(0);
    progBar->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(progBar);
    progBar->addWidget(_progBar);

    QHBoxLayout *viewProgBar = new QHBoxLayout();
    viewProgBar->setContentsMargins(0, 0, 0, 0);
    viewProgBar->setSpacing(0);
    viewProgBar->setAlignment(Qt::AlignHCenter);
//    bot->addLayout(viewProgBar);
    viewProgBar->addWidget(_viewProgBar);

    _progBar_stacked = new QStackedLayout(this);
    _progBar_stacked->setContentsMargins(0, 0, 0, 0);
    _progBar_stacked->setStackingMode(QStackedLayout::StackOne);
    _progBar_stacked->setAlignment(Qt::AlignCenter);
    _progBar_stacked->setSpacing(0);
    _progBar_stacked->addWidget(_progBarspec);
    _progBar_stacked->addWidget(_progBar);
    _progBar_stacked->addWidget(_viewProgBar);
//    _progBar_stacked->addChildLayout(viewProgBar);
    _progBar_stacked->setCurrentIndex(0);
//    bot->addLayout(_progBar_stacked);

    _progBar_Widget = new QStackedWidget;
    _progBar_Widget->addWidget(_progBarspec);
    _progBar_Widget->addWidget(_progBar);
    _progBar_Widget->addWidget(_viewProgBar);
    _progBar_Widget->setCurrentIndex(0);
    progBarspec->addWidget(_progBar_Widget);
    bot->addLayout(progBarspec);
//    bot->addWidget(_timeLabel);
//    bot->addWidget(_progBarspec);
//    bot->addWidget(_progBar);
//    bot->addWidget(_viewProgBar);
//    bot->addWidget(_timeLabelend);

//    bot->addStretch();

    QHBoxLayout *timeend = new QHBoxLayout();
    timeend->setContentsMargins(10, 0, 0, 0);
    timeend->setSpacing(0);
    timeend->setAlignment(Qt::AlignRight);
    bot->addLayout(timeend);
    timeend->addWidget(_timeLabelend);

    _palyBox = new DButtonBox(this);
    _palyBox->setFixedWidth(120);
    _mid->addWidget(_palyBox);
    _mid->setAlignment(_palyBox,Qt::AlignLeft);
    QList<DButtonBoxButton*> list;


//    _prevBtn = new DIconButton(this);
    _prevBtn = new DButtonBoxButton(QIcon::fromTheme("dcc_last"));
//    _prevBtn->setIcon(QIcon::fromTheme("dcc_last"));
    _prevBtn->setIconSize(QSize(36,36));
    _prevBtn->setFixedSize(40, 50);
    _prevBtn->setObjectName("PrevBtn");
    connect(_prevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_prevBtn, "prev");
//    _mid->addWidget(_prevBtn);
    list.append(_prevBtn);
    _playBtn = new DButtonBoxButton(QIcon::fromTheme("dcc_play"));
//    _playBtn->setIcon(QIcon::fromTheme("dcc_play"));
    _playBtn->setIconSize(QSize(36,36));
    _playBtn->setFixedSize(40, 50);
    connect(_playBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_playBtn, "play");
//    _mid->addWidget(_playBtn);
    list.append(_playBtn);

    _nextBtn = new DButtonBoxButton(QIcon::fromTheme("dcc_next"));
//    _nextBtn->setIcon(QIcon::fromTheme("dcc_next"));
    _nextBtn->setIconSize(QSize(36,36));
    _nextBtn->setFixedSize(40, 50);
    connect(_nextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_nextBtn, "next");
//    _mid->addWidget(_nextBtn);
    list.append(_nextBtn);
    _palyBox->setButtonList(list,false);
//    _palyBox->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _nextBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _playBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);
    _prevBtn->setFocusPolicy(Qt::FocusPolicy::NoFocus);

//    bot->addStretch();

    _right = new QHBoxLayout();
    _right->setContentsMargins(0, 0, 0, 0);
    _right->setSizeConstraint(QLayout::SetFixedSize);
    _right->setSpacing(0);
    bot->addLayout(_right);

    _subBtn = new DIconButton(this);
    _subBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    _subBtn->setIconSize(QSize(36,36));
    _subBtn->setFixedSize(50, 50);
    connect(_subBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_subBtn, "sub");
    _right->addWidget(_subBtn);

    _subBtn->hide();

    _volBtn = new VolumeButton(this);
    _volBtn->setFixedSize(50, 50);
    connect(_volBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_volBtn, "vol");
//    _right->addWidget(_volBtn);

    _volSlider = new VolumeSlider(_engine, _mainWindow);
    connect(_volBtn, &VolumeButton::entered, [=]() {
        _volSlider->stopTimer();
        QPoint pos = _volBtn->parentWidget()->mapToGlobal(_volBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        _volSlider->show(pos.x() + _volSlider->width()/2-5, pos.y() - 5 + TOOLBOX_TOP_EXTENT+(_bot_spec->isVisible()?314:0));
    });
    connect(_volBtn, &VolumeButton::leaved, _volSlider, &VolumeSlider::delayedHide);
    connect(_volBtn, &VolumeButton::requestVolumeUp, [=]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeUp);
    });
    connect(_volBtn, &VolumeButton::requestVolumeDown, [=]() {
        _mainWindow->requestAction(ActionFactory::ActionKind::VolumeDown);
    });



    _fsBtn = new DIconButton(this);
    _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
    _fsBtn->setIconSize(QSize(36,36));
    _fsBtn->setFixedSize(50, 50);
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");
    _right->addWidget(_fsBtn);
    _right->addSpacing(10);
    _right->addWidget(_volBtn);
    _right->addSpacing(10);

    _listBtn = new DIconButton(this);
    _listBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    _listBtn->setIconSize(QSize(36,36));
    _listBtn->setFixedSize(50, 50);
//    _listBtn->setFocusPolicy(Qt::FocusPolicy::TabFocus);
    _listBtn->setCheckable(true);
//    _listBtn->setChecked(true);
//    connect(_playlist,&PlaylistWidget::stateChange,this,[=](){
//        if (_playlist->state() == PlaylistWidget::State::Opened){
//            _listBtn->setChecked(true);
//        }else {
//            _listBtn->setChecked(false);
//        }
//    });

    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    _right->addWidget(_listBtn);

    // these tooltips is not used due to deepin ui design
    _playBtn->setToolTip(tr("Play/Pause"));
    //_volBtn->setToolTip(tr("Volume"));
    _prevBtn->setToolTip(tr("Previous"));
    _nextBtn->setToolTip(tr("Next"));
    _subBtn->setToolTip(tr("Subtitles"));
    _listBtn->setToolTip(tr("Playlist"));
    _fsBtn->setToolTip(tr("Fullscreen"));

    auto th = new TooltipHandler(this);
    QWidget* btns[] = {
        _playBtn, _prevBtn, _nextBtn, _subBtn, _listBtn, _fsBtn,
    };
    QString hints[] = {
        tr("Play/Pause"), tr("Previous"), tr("Next"),
        tr("Subtitles"), tr("Playlist"), tr("Fullscreen"),
    };

    for (int i = 0; i < sizeof(btns)/sizeof(btns[0]); i++) {
        auto t = new Tip(QPixmap(), hints[i], parentWidget());
        t->setFixedHeight(32);
        t->setProperty("for", QVariant::fromValue<QWidget*>(btns[i]));
        btns[i]->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        btns[i]->installEventFilter(th);
    }

    connect(_engine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(_engine, &PlayerEngine::fileLoaded, [=]() {
        _progBar->slider()->setRange(0, _engine->duration());
//        setViewProgBar();
//        _viewProgBar->hide();
//        _progBar->show();
//        _progBarspec->hide();
        _progBar_stacked->setCurrentIndex(1);
//        _progBar_Widget->setCurrentIndex(1);
        pm_list.clear();
        pm_black_list.clear();

        update();
        QTimer::singleShot(1000, [this]() {
//            if(_loadThread->isRunning()){
//                _loadThread->terminate();
//                _loadThread->exit();
//                _loadThread->deleteLater();
//                _loadThread = new QThread();
//                _viewProgBarLoad->moveToThread(_loadThread);
//                _loadThread->wait();
//                _loadThread->start();
                _loadsize = size();
                QThread *thread = new QThread;
                viewProgBarLoad *worker = new viewProgBarLoad(_engine,_progBar,this);

                connect(worker,SIGNAL(finished()),thread,SLOT(quit()));//新增
//                    connect(thread,SIGNAL(started()),worker,SLOT(doSomething()));
                connect(thread,SIGNAL(finished()),worker,SLOT(deleteLater()));
                connect(thread,SIGNAL(finished()),thread,SLOT(deleteLater()));
                worker->moveToThread(thread);
                thread->start();
                connect(this, SIGNAL(sigstartLoad(QSize)), worker, SLOT(loadViewProgBar(QSize)));
                connect(worker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
                emit sigstartLoad(size());
                _progBar_Widget->setCurrentIndex(1);
//            }

        });
//        QTimer::singleShot(100, [this]() {_viewProgBar->setViewProgBar(_engine);});
//        _viewProgBar->setViewProgBar(_engine);
//        _viewProgBar->show();
//        _progBar->hide();

    });
    connect(_engine, &PlayerEngine::elapsedChanged, [=]() {
        updateTimeInfo(_engine->duration(), _engine->elapsed());
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

    connect(&ThumbnailWorker::get(), &ThumbnailWorker::thumbGenerated,
            this, &ToolboxProxy::updateHoverPreview);

    auto bubbler = new KeyPressBubbler(this);
    this->installEventFilter(bubbler);
    _playBtn->installEventFilter(bubbler);

    connect(qApp, &QGuiApplication::applicationStateChanged, [=](Qt::ApplicationState e) {
        if (e == Qt::ApplicationInactive && anyPopupShown()) {
            closeAnyPopup();
        }
    });

    _autoResizeTimer.setSingleShot(true);
    connect(&_autoResizeTimer, &QTimer::timeout, this, [=]{
        if(_oldsize.width()==width()){
            _viewProgBar->setWidth();
            if(_engine->state() != PlayerEngine::CoreState::Idle && size()!=_loadsize){
//                _viewProgBar->setViewProgBar(_engine);
                QTimer::singleShot(1000, [this]() {
                    pm_list.clear();
                    pm_black_list.clear();
//                    if(_loadThread->isRunning()){
//                        _loadThread->terminate();
//                        _loadThread->exit();
//                    _loadThread->deleteLater();
//                    _loadThread = new QThread();
//                    _viewProgBarLoad->moveToThread(_loadThread);
//                        _loadThread->wait();
//                        _loadThread->start();
//                    QThread *loadThread = new QThread();

//                    _viewProgBarLoad->moveToThread(loadThread);
//                    loadThread->start();
//                    connect(loadThread, SIGNAL(finished()), loadThread, SLOT(deleteLater()));
                    QThread *thread = new QThread;
                    viewProgBarLoad *worker = new viewProgBarLoad(_engine,_progBar,this);

                    connect(worker,SIGNAL(finished()),thread,SLOT(quit()));//新增
//                    connect(thread,SIGNAL(started()),worker,SLOT(doSomething()));
                    connect(thread,SIGNAL(finished()),worker,SLOT(deleteLater()));
                    connect(thread,SIGNAL(finished()),thread,SLOT(deleteLater()));
                    worker->moveToThread(thread);
                    thread->start();
                    connect(this, SIGNAL(sigstartLoad(QSize)), worker, SLOT(loadViewProgBar(QSize)));
                    connect(worker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
                    emit sigstartLoad(size());
                    _progBar_Widget->setCurrentIndex(1);
//                    }

                });
//                _progBar_Widget->setCurrentIndex(1);

                _loadsize = size();
            }
        }
    });
}
void ToolboxProxy::setViewProgBar(){
    auto *viewProgBarLayout = new QHBoxLayout();
//    viewProgBarLayout->setSpacing(1);
    auto width = _viewProgBar->width();
    auto tmp = _engine->duration()/62;
    auto dpr = qApp->devicePixelRatio();
    QPixmap pm;
    pm.setDevicePixelRatio(dpr);
    VideoThumbnailer thumber;
    QTime d(0, 0, 0);
    thumber.setThumbnailSize(8 * qApp->devicePixelRatio());
    thumber.setMaintainAspectRatio(false);
    thumber.setSeekTime(d.toString("hh:mm:ss").toStdString());
    auto url = _engine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();
//    for(auto i=0;i<(_engine->duration() - tmp);){
      for(auto i=0;i<63;i++){
        d = d.addSecs(tmp);
        try {
            std::vector<uint8_t> buf;
            thumber.generateThumbnail(file.toUtf8().toStdString(),
                    ThumbnailerImageType::Png, buf);

            auto img = QImage::fromData(buf.data(), buf.size(), "png");

            pm = QPixmap::fromImage(img.scaled(QSize(8,50) * dpr, Qt::IgnoreAspectRatio, Qt::FastTransformation));
            pm.setDevicePixelRatio(dpr);
        } catch (const std::logic_error&) {
        }
        QLabel *label = new QLabel();
        label->setPixmap(pm);
        label->setFixedSize(8,50);
        viewProgBarLayout->addWidget(label, 0 , Qt::AlignLeft );
        viewProgBarLayout->setSpacing(1);
//        i += tmp;
    }
    _viewProgBar->setLayout(viewProgBarLayout);

//    _viewProgBar->show();
//    _progBar->hide();
//    _progBarspec->hide();

}

void ToolboxProxy::closeAnyPopup()
{
    if (_previewer->isVisible()) {
        _previewer->hide();
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
    return _previewer->isVisible() || _subView->isVisible() || _volSlider->isVisible();
}

void ToolboxProxy::updateHoverPreview(const QUrl& url, int secs)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    if (_engine->playlist().currentInfo().url != url)
        return;

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);

    _previewer->updateWithPreview(pm, secs, _engine->videoRotation());
}

void ToolboxProxy::progressHoverChanged(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

    if (!Settings::get().isSet(Settings::PreviewOnMouseover))
        return;

    if (_volSlider->isVisible())
        return;

    const auto& pif = _engine->playlist().currentInfo();
    if (!pif.url.isLocalFile())
        return;

    const auto& absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        _previewer->hide();
        return;
    }

    _lastHoverValue = v;
    ThumbnailWorker::get().requestThumb(pif.url, v);

//    auto pos = _progBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
    auto pos = _viewProgBar->mapToGlobal(QPoint(0, TOOLBOX_TOP_EXTENT - 10));
    QPoint p { QCursor::pos().x(), pos.y() };

    _previewer->updateWithPreview(p);
}

void ToolboxProxy::setProgress(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
        return;

//    _engine->seekAbsolute(_progBar->sliderPosition());
    _engine->seekAbsolute(v);
    if (_progBar->slider()->sliderPosition() != _lastHoverValue) {
        progressHoverChanged(_progBar->slider()->sliderPosition());
    }
    updateMovieProgress();
}

void ToolboxProxy::updateMovieProgress()
{



    auto d = _engine->duration();
    auto e = _engine->elapsed();
    int v = 0;
    int v2 = 0;
    if (d != 0 && e != 0) {
        v = _progBar->maximum() * ((double)e / d);
        v2 = _viewProgBar->rect().width()*((double)e / d);
    }
    if (!_progBar->signalsBlocked()){
        _progBar->blockSignals(true);
        _progBar->setValue(v);
        _progBar->blockSignals(false);
    }
    if(!_viewProgBar->getIsBlockSignals()){
        _viewProgBar->setIsBlockSignals(true);
        _viewProgBar->setValue(v2);
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
        //_volBtn->setToolTip(tr("Volume"));
        if (v >= 80)
            _volBtn->changeLevel(VolumeButton::High);
        else if (v >= 40)
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
        _fsBtn->setToolTip(tr("Exit fullscreen"));
    } else {
//        _fsBtn->setObjectName("FsBtn");
        _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
        _fsBtn->setToolTip(tr("Fullscreen"));
    }
//    _fsBtn->setStyleSheet(_playBtn->styleSheet());
}

void ToolboxProxy::updatePlayState()
{
    qDebug() << __func__ << _engine->state();
    if (_engine->state() == PlayerEngine::CoreState::Playing) {
//        _playBtn->setObjectName("PauseBtn");
        _playBtn->setIcon(QIcon::fromTheme("dcc_suspend"));
        _playBtn->setToolTip(tr("Pause"));
    } else {
//        _playBtn->setObjectName("PlayBtn");
        _playBtn->setIcon(QIcon::fromTheme("dcc_play"));
        _playBtn->setToolTip(tr("Play"));
    }

    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        if (_subView->isVisible())
            _subView->hide();

        if (_previewer->isVisible()) {
            _previewer->hide();
        }
        if( _progBar->isVisible()){
            _progBar->setVisible(false);
        }
//        _progBarspec->show();
//        _progBar->hide();
        _progBar_stacked->setCurrentIndex(0);
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

void ToolboxProxy::updateTimeInfo(qint64 duration, qint64 pos)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle) {
        _timeLabel->setText("");
        _timeLabelend->setText("");

    } else {
        //mpv returns a slightly different duration from movieinfo.duration
        //_timeLabel->setText(QString("%2/%1").arg(utils::Time2str(duration))
                //.arg(utils::Time2str(pos)));
        _timeLabel->setText(QString("%1")
                .arg(utils::Time2str(pos)));
        _timeLabelend->setText(QString("%1")
                .arg(utils::Time2str(duration)));
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
    } else if (id == "sub") {
        _subView->setVisible(true);

        QPoint pos = _subBtn->parentWidget()->mapToGlobal(_subBtn->pos());
        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
        _subView->show(pos.x() + _subBtn->width()/2, pos.y() - 5 + TOOLBOX_TOP_EXTENT);
    }
}

void ToolboxProxy::updatePosition(const QPoint& p)
{
    QPoint pos(p);
    pos.ry() += _mainWindow->height() - height();
    windowHandle()->setFramePosition(pos);
}

void ToolboxProxy::paintEvent(QPaintEvent *pe)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    QColor bgColor = pal.color(QPalette::ToolTipBase);

    QPainterPath pp;
    pp.addRoundedRect(bgRect, RADIUS_MV, RADIUS_MV);
    painter.fillPath(pp, QColor(0,0,0,22));

    {
        auto view_rect = bgRect.marginsRemoved(QMargins(1, 1, 1, 1));
        QPainterPath pp;
        pp.addRoundedRect(view_rect, RADIUS_MV, RADIUS_MV);
        painter.fillPath(pp, bgColor);
    }

    QWidget::paintEvent(pe);
}

void ToolboxProxy::showEvent(QShowEvent *event)
{
    updateTimeLabel();
}

void ToolboxProxy::resizeEvent(QResizeEvent *event)
{

    if(_autoResizeTimer.isActive()){
        _autoResizeTimer.stop();
    }
    if(event->oldSize().width() != event->size().width()){
        _autoResizeTimer.start(1000);
        _oldsize = event->size();
        _progBar->setFixedWidth(width()-PROGBAR_SPEC);
        if(_engine->state()!=PlayerEngine::CoreState::Idle){
            _progBar_Widget->setCurrentIndex(1);
        }

    }



//    _oldsize = event->size();
//    if (!_isresize){
//        QTimer::singleShot(2000, [this]() {

//            _viewProgBar->setWidth();
//            _isresize = false;
//        });
//    }


    updateTimeLabel();
}

void ToolboxProxy::updateTimeLabel()
{
    // to keep left and right of the same width. which makes play button centered
    _listBtn->setVisible(width() > 300);
    _timeLabel->setVisible(width() > 450);
    _timeLabelend->setVisible(width() > 450);
//    _viewProgBar->setVisible(width() > 350);
//    _progBar->setVisible(width() > 350);
    if(_mainWindow->width() < 1050){
//        _progBar->hide();
    }
    if(width() <= 300){
        _progBar->setFixedWidth(width()-PROGBAR_SPEC+50+54+10+54+10+10);
        _progBarspec->setFixedWidth(width()-PROGBAR_SPEC+50+54+10+54+10+10);
    }else if (width() <= 450) {
        _progBar->setFixedWidth(width()-PROGBAR_SPEC+54+54+10);
        _progBarspec->setFixedWidth(width()-PROGBAR_SPEC+54+54+10);
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
}

void ToolboxProxy::setViewProgBarWidth()
{
    _viewProgBar->setWidth();
}

}

#include "toolbox_proxy.moc"
