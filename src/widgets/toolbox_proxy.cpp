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
#include "dbus_adpator.h"

//#include <QtWidgets>
#include <DImageButton>
#include <DThemeManager>
#include <DArrowRectangle>
#include <DApplication>
#include <QThread>
#include <DSlider>
#include <DUtil>
#include <QDBusInterface>
#include <dthememanager.h>
#include <iostream>
#include "../accessibility/ac-deepin-movie-define.h"
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
                QPalette palette(qApp->palette());\
                this->setAutoFillBackground(true);\
                this->setPalette(palette);\
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
                QPalette palette(qApp->palette());\
                palette.setColor(QPalette::Background,Qt::black);\
                this->setAutoFillBackground(true);\
                this->setPalette(palette);\
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

#define THEME_TYPE(colortype) do { \
        if (colortype == DGuiApplicationHelper::LightType){\
            QColor backMaskColor(255, 255, 255, 140);\
            this->blurBackground()->setMaskColor(backMaskColor);\
            QColor maskColor(255, 255, 255, 76);\
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

namespace dmr {

class ImageButton: public QPushButton
{
    Q_OBJECT
public:
    explicit ImageButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {

    }

    void setImage(QString strUrl)
    {
        m_strImageUrl = strUrl;
        repaint();
    }

protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        QImage image(m_strImageUrl);

        painter.setRenderHints(QPainter::Antialiasing | QPainter::HighQualityAntialiasing);
        painter.drawImage(rect(), image);
    }

private:
    QString m_strImageUrl;
};

class KeyPressBubbler: public QObject
{
public:
    explicit KeyPressBubbler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() != Qt::Key_Tab) {
                event->setAccepted(false);
                return true;
            } else {
                return false;
            }
        } else {
            // standard event processing
            return QObject::eventFilter(obj, event);
        }
    }
};

class TooltipHandler: public QObject
{
public:
    explicit TooltipHandler(QObject *parent): QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        switch (event->type()) {
        case QEvent::ToolTip:
        case QEvent::Enter: {
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
        case QEvent::MouseMove: {
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            tip->hide();
        }
        default:
            break;
        }
        // standard event processing
        return QObject::eventFilter(obj, event);
    }
};
//not used class
/**
class SubtitlesView;
class SubtitleItemWidget: public QWidget
{
    Q_OBJECT
public:
    friend class SubtitlesView;
    SubtitleItemWidget(QWidget *, SubtitleInfo si): QWidget()
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
            //auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(qApp->theme());
            auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(DThemeManager::instance()->theme());
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
    ///this code is doesn't seem to be in use now,comment out it temporarily
//    void showEvent(QShowEvent *se) override
//    {
//        auto fm = _title->fontMetrics();
//        auto shorted = fm.elidedText(_msg, Qt::ElideMiddle, 140 * 2);
//        int h = fm.height();
//        if (fm.width(shorted) > 140) {
//            h *= 2;
//        } else {
//        }
//        _title->setFixedHeight(h);
//        _title->setText(shorted);

//        QWidget::showEvent(se);
//    }

private slots:
    void onThemeChanged()
    {
        if (property("current").toBool()) {
            //auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(qApp->theme());
            auto name = QString(":/resources/icons/%1/subtitle-selected.svg").arg(DThemeManager::instance()->theme());
            _selectedLabel->setPixmap(QPixmap(name));
        }
    }

private:
    QLabel *_selectedLabel {nullptr};
    QLabel *_title {nullptr};
    int _sid {-1};
    QString _msg;
};
*/
//not used class
/**
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
        if (DThemeManager::instance()->theme() == "dark") {
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
*/
class SliderTime: public DArrowRectangle
{
    Q_OBJECT
public:
    SliderTime(): DArrowRectangle(DArrowRectangle::ArrowBottom)
    {
        //setFocusPolicy(Qt::NoFocus);
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
        connect(qApp, &QGuiApplication::fontChanged, this, &SliderTime::slotFontChanged);

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
public slots:
    void slotFontChanged(const QFont &font)
    {
        _font = font;
        _bFontChanged = true;
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

        _indicator = new IndicatorItem(this);
        _indicator->resize(6, 60);
        _indicator->setObjectName("indicator");

        _sliderTime = new SliderTime;
        _sliderTime->hide();

        QMatrix matrix;
        matrix.rotate(180);
        QPixmap pixmap = utils::LoadHiDPIPixmap(SLIDER_ARROW);
        _sliderArrowUp = new DArrowRectangle(DArrowRectangle::ArrowTop);
        //_sliderArrowUp->setFocusPolicy(Qt::NoFocus);
        _sliderArrowUp->setAttribute(Qt::WA_DeleteOnClose);
        _sliderArrowUp->setWindowFlag(Qt::WindowStaysOnTopHint);
        _sliderArrowUp->setArrowWidth(10);
        _sliderArrowUp->setArrowHeight(7);
        const QPalette pa = QGuiApplication::palette();
        QColor bgColor = pa.color(QPalette::Highlight);
        _sliderArrowUp->setBackgroundColor(bgColor);
        _sliderArrowUp->setFixedSize(10, 7);
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
        if (v < m_nStartPoint) {
            v = m_nStartPoint;
        } else if (v > (m_nStartPoint + m_nViewLength)) {
            v = (m_nStartPoint + m_nViewLength);
        }
        _indicatorPos = {v, rect().y()};
    }
    int getValue()
    {
        return _indicator->x();
    }
    int getTimePos()
    {
        return position2progress(QPoint(_indicator->x(), 0));
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
        _engine = engine;

        _viewProgBarLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        _viewProgBarLayout->setSpacing(1);

        int pixWidget = 40/*_progBar->width() / 100*/;

        m_nViewLength = (pixWidget + 1) * pm_list.count() - 1;
        m_nStartPoint = (_progBar->width() - m_nViewLength) / 2; //开始位置
        for (int i = 0; i < pm_list.count(); i++) {
            ImageItem *label = new ImageItem(pm_list.at(i), false, _back);
            label->setMouseTracking(true);
            label->move(i * (pixWidget + 1) + m_nStartPoint, 5);
            label->setFixedSize(pixWidget, 50);

            ImageItem *label_black = new ImageItem(pm_black_list.at(i), true, _front);
            label_black->setMouseTracking(true);
            label_black->move(i * (pixWidget + 1) + m_nStartPoint, 5);
            label_black->setFixedSize(pixWidget, 50);
        }
        update();
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

    int getViewLength()
    {
        return m_nViewLength;
    }

    int getStartPoint()
    {
        return m_nStartPoint;
    }

private:
    void changeStyle(bool press)
    {
        if (!isVisible()) return;

        if (press) {
            _indicator->setPressed(press);
            _indicator->resize(2, 60);

        } else {
            _indicator->setPressed(press);
            _indicator->resize(6, 60);
        }
    }

signals:
    void leave();
    void hoverChanged(int);
    void sliderMoved(int);
    void indicatorMoved(int);
    void mousePressed(bool pressed);

protected:
    void leaveEvent(QEvent *e) override
    {
        emit leave();

        DWidget::leaveEvent(e);
    }
    void showEvent(QShowEvent *se) override
    {
        DWidget::showEvent(se);
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!isEnabled()) return;

        if (e->pos().x() >= 0 && e->pos().x() <= contentsRect().width()) {
            int v = position2progress(e->pos());
            if (e->buttons() & Qt::LeftButton) {
                int distance = (e->pos() - _startPos).manhattanLength();
                if (distance >= QApplication::startDragDistance()) {
                    _engine->seekAbsolute(v);
                    emit sliderMoved(v);
                    emit hoverChanged(v);
                    emit mousePressed(true);
                    setValue(e->pos().x());
                    repaint();
                }
            } else {
                if (_vlastHoverValue != v) {
                    emit hoverChanged(v);
                }
                _vlastHoverValue = v;
            }
        }
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override
    {

        if (!_press && e->buttons() == Qt::LeftButton && isEnabled()) {
            _startPos = e->pos();

            int v = position2progress(e->pos());
            _engine->seekAbsolute(v);
            emit sliderMoved(v);
            emit hoverChanged(v);
            emit mousePressed(true);
            setValue(e->pos().x());
            changeStyle(!_press);
            _press = !_press;
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        emit mousePressed(false);
        if (_press && isEnabled()) {
            changeStyle(!_press);
            _press = !_press;
        }

        DWidget::mouseReleaseEvent(e);
    }
    void paintEvent(QPaintEvent *e) override
    {
        _indicator->move(_indicatorPos.x(), _indicatorPos.y());
        QPoint pos = this->mapToGlobal(QPoint(0, 0));
        _sliderArrowUp->move(pos.x() + _indicatorPos.x() + 1, pos.y() + _indicator->height() - 5);
        _front->setFixedWidth(_indicatorPos.x());

        _sliderArrowUp->setVisible(_press);
        setTimeVisible(_press);

        DWidget::paintEvent(e);
    }
    void resizeEvent(QResizeEvent *event) override
    {
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
    viewProgBarLoad *_viewProgBarLoad{nullptr};
    QWidget *_back{nullptr};
    QWidget *_front{nullptr};
    IndicatorItem *_indicator {nullptr};
    SliderTime *_sliderTime{nullptr};
    DLabel *_sliderArrowDown{nullptr};
    DArrowRectangle *_sliderArrowUp{nullptr};
    bool _press{false};
    QGraphicsColorizeEffect *m_effect{nullptr};
    QList<QLabel *> labelList ;
    QHBoxLayout *_indicatorLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout_black{nullptr};
    DMRSlider *_progBar{nullptr};
    int m_nViewLength;
    int m_nStartPoint;
    int  position2progress(const QPoint &p)
    {
        int nPosition = 0;

        if (!_engine) {
            return 0;
        }

        if (p.x() < m_nStartPoint) {
            nPosition = m_nStartPoint;
        } else if (p.x() > (m_nViewLength + m_nStartPoint)) {
            nPosition = (m_nViewLength + m_nStartPoint);
        } else {
            nPosition = p.x();
        }

        auto total = _engine->duration();
        int span = static_cast<int>(total * (nPosition - m_nStartPoint) / m_nViewLength);
        return span/* * (p.x())*/;
    }

};

class ThumbnailPreview: public QWidget
{
    Q_OBJECT
public:
    ThumbnailPreview()
    {
        setAttribute(Qt::WA_DeleteOnClose);
        // FIXME(hualet): Qt::Tooltip will cause Dock to show up even
        // the player is in fullscreen mode.
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);

        setObjectName("ThumbnailPreview");

//        setFixedSize(ThumbnailWorker::thumbSize().width(),ThumbnailWorker::thumbSize().height()+10);

//        setWidth(ThumbnailWorker::thumbSize().width());
//        setHeight(ThumbnailWorker::thumbSize().height());
//        resize(QSize(106, 66));
//        setShadowBlurRadius(2);
//        setRadius(2);
//        setRadius(16);
//        setBorderWidth(1);
//        setBorderColor(QColor(255, 255, 255, 26));

//        setShadowYOffset(4);
//        setShadowXOffset(0);
//        setShadowBlurRadius(6);
//        setArrowWidth(0);
//        setArrowHeight(0);

        auto *l = new QVBoxLayout;
//        l->setContentsMargins(0, 0, 0, 10);
        l->setContentsMargins(1, 0, 0, 0);

        _thumb = new DFrame(this);
        DStyle::setFrameRadius(_thumb, 8);

        //_thumb->setFixedSize(ThumbnailWorker::thumbSize());
        l->addWidget(_thumb/*,Qt::AlignTop*/);
        setLayout(l);

//        connect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &ThumbnailPreview::updateTheme);
//        updateTheme();

//        winId(); // force backed window to be created
        m_shadow_effect = new QGraphicsDropShadowEffect(this);
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
        //resizeWithContent();
        if (utils::check_wayland_env()) {
            move(pos.x() - this->width() / 2, pos.y() + 10);
        } else {
            move(pos.x() - this->width() / 2, pos.y() - this->height() + 10);
        }

        show();
        raise();
    }

signals:
    void leavePreview();

protected slots:
    /*void updateTheme()
    {
        if (qApp->theme() == "dark") {
            setBackgroundColor(QColor(23, 23, 23, 255 * 8 / 10));
            setBorderColor(QColor(255, 255 ,255, 25));
            _time->setStyleSheet(R"(
                border-radius: 3px;
                background-color: rgba(23, 23, 23, 0.8);
                font-size: 12px;
                color: #ffffff;
            )");
        } else {
            setBackgroundColor(QColor(255, 255, 255, 255 * 8 / 10));
            setBorderColor(QColor(0, 0 ,0, 25));
            _time->setStyleSheet(R"(
                border-radius: 3px;
                background-color: rgba(255, 255, 255, 0.8);
                font-size: 12px;
                color: #303030;
            )");
        }
    }*/

protected:
    void paintEvent(QPaintEvent *e) Q_DECL_OVERRIDE{
        m_shadow_effect->setOffset(0, 0);
        m_shadow_effect->setColor(Qt::gray);
        m_shadow_effect->setBlurRadius(8);
        setGraphicsEffect(m_shadow_effect);

        QWidget::paintEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        emit leavePreview();
    }

    void showEvent(QShowEvent *se) override
    {
        QWidget::showEvent(se);
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
    int m_thumbnailFixed = 106;
    QGraphicsDropShadowEffect *m_shadow_effect{nullptr};
};

class VolumeSlider: public DArrowRectangle
{
    Q_OBJECT

    enum State {
        Open,
        Close
    };

public:
    VolumeSlider(PlayerEngine *eng, MainWindow *mw, QWidget *parent)
        : DArrowRectangle(DArrowRectangle::ArrowBottom, DArrowRectangle::FloatWidget, parent), _engine(eng), _mw(mw)
    {
#ifdef __mips__
        if (!CompositingManager::get().composited()) {
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
        }
#elif __aarch64__
        if (!utils::check_wayland_env())
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
#elif __sw_64__
        setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
        setAttribute(Qt::WA_NativeWindow);
#else
        if (CompositingManager::get().isSpecialControls()) {
            setAttribute(Qt::WA_NativeWindow);
            //            setWindowFlags(Qt::WindowStaysOnTopHint);
        }
#endif
        setShadowBlurRadius(4);
        setRadius(18);
        setShadowYOffset(0);
        setShadowXOffset(0);
        setArrowWidth(20);
        setArrowHeight(15);
        hide();

        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(0, 10, 0, 5);
        l->setSpacing(0);

        setLayout(l);

        _slider = new DSlider(Qt::Vertical, this);
        _slider->setFixedWidth(24);
        _slider->setIconSize(QSize(12, 12));
        _slider->installEventFilter(this);
        _slider->show();
        _slider->slider()->setRange(0, 100);
        _slider->setObjectName(VOLUME_SLIDER);
        _slider->slider()->setObjectName(SLIDER);
        _slider->slider()->setAccessibleName(SLIDER);
        _slider->slider()->setMinimumHeight(132);
        _slider->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        //修改打开时音量条显示不正确
        int vol = 0;
        if (utils::check_wayland_env()) {
            vol = Settings::get().internalOption("global_volume").toInt();
        } else {
            vol = _engine->volume();
        }
        _slider->setValue(vol);

        QFont font;
        font.setFamily("SourceHanSansSC");
        font.setPixelSize(14);
        font.setWeight(500);
        m_pLabShowVolume = new QLabel(this);
        m_pLabShowVolume->setFont(font);
        m_pLabShowVolume->setAlignment(Qt::AlignCenter);
        m_pLabShowVolume->setText("0%");
        l->addWidget(m_pLabShowVolume, 0, Qt::AlignCenter);

        l->addWidget(_slider, 1, Qt::AlignCenter);

        m_pBtnChangeMute = new ImageButton(this);
        m_pBtnChangeMute->setObjectName(MUTE_BTN);
        m_pBtnChangeMute->setAccessibleName(MUTE_BTN);
        m_pBtnChangeMute->setFixedWidth(36);
        m_sThemeType = DGuiApplicationHelper::instance()->themeType();
        setVolumeIcon(m_sThemeType);
        m_pBtnChangeMute->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred));
        connect(m_pBtnChangeMute, SIGNAL(clicked()), this, SLOT(changeSate()));

        l->addWidget(m_pBtnChangeMute, 0, Qt::AlignHCenter);
//        m_pBtnChangeMute->hide();
        connect(_slider, &DSlider::valueChanged, this, &VolumeSlider::slotValueChanged);
        _autoHideTimer.setSingleShot(true);
#ifdef __x86_64__
        connect(&_autoHideTimer, &QTimer::timeout, this, &VolumeSlider::popup);
#else
        if (utils::check_wayland_env()) {
            connect(&_autoHideTimer, &QTimer::timeout, this, &VolumeSlider::hide);
        }
#endif
    }

    ~VolumeSlider()
    {
//        disconnect(DThemeManager::instance(), &DThemeManager::themeChanged,
//                this, &VolumeSlider::updateBg);
    }
    void stopTimer()
    {
        _autoHideTimer.stop();
    }
    int value()
    {
        return _slider->value();
    }
    void setMute(bool bMute)
    {
        if (m_bIsMute == bMute) {
            return;
        }
        m_bIsMute = bMute;

        setVolumeIcon(DGuiApplicationHelper::instance()->themeType());
        m_pBtnChangeMute->repaint();
    }

public slots:
    void updatePoint(QPoint point)
    {
        QRect main_rect = _mw->rect();
        QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));
        m_point = point + QPoint(view_rect.width() - (TOOLBOX_BUTTON_WIDTH * 2 + 30 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2),
                                 view_rect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT);
    }

    void popup()
    {
        QRect main_rect = _mw->rect();
        QRect view_rect = main_rect.marginsRemoved(QMargins(1, 1, 1, 1));

        int x = view_rect.width() - (TOOLBOX_BUTTON_WIDTH * 2 + 30 + (VOLSLIDER_WIDTH - TOOLBOX_BUTTON_WIDTH) / 2);
        int y = view_rect.height() - TOOLBOX_HEIGHT - VOLSLIDER_HEIGHT;
#ifndef __x86_64__
        //在arm及mips平台下音量条上移了10个像素
        y += 10;
#endif
        QRect end(x, y, VOLSLIDER_WIDTH, VOLSLIDER_HEIGHT);
        QRect start = end;
        QRect media = start;

        start.setWidth(start.width() + 16);
        start.setHeight(start.height() + 14);

        media.setWidth(media.width() - 10);
        media.setHeight(media.height() - 10);
#ifdef __x86_64__
        start.moveTo(start.topLeft() - QPoint(8, 14));
        media.moveTo(media.topLeft() + QPoint(5, 10));
#else
        end.moveTo(m_point);
        start.moveTo(m_point - QPoint(8, 14));
        media.moveTo(m_point + QPoint(5, 10));
#endif

        if (state == State::Close && isVisible()) {
            pVolAnimation = new QPropertyAnimation(this, "geometry");
            pVolAnimation->setEasingCurve(QEasingCurve::Linear);
            pVolAnimation->setKeyValueAt(0, end);
            pVolAnimation->setKeyValueAt(0.3, start);
            pVolAnimation->setKeyValueAt(0.75, media);
            pVolAnimation->setKeyValueAt(1, end);
            pVolAnimation->setDuration(230);
            m_bFinished = true;
            raise();
            pVolAnimation->start();
            connect(pVolAnimation, &QPropertyAnimation::finished, [ = ] {
                pVolAnimation->deleteLater();
                pVolAnimation = nullptr;
                state = Open;
                m_bFinished = false;
            });
        } else {
            state = Close;
            hide();
        }
    }
    void hideSlider()
    {
        popup();
    }
    void slotVolumeChanged()
    {
        setVolumeIcon(DGuiApplicationHelper::instance()->themeType());
    }
    void delayedHide()
    {
#ifdef __x86_64__
        if (!isHidden())
            _autoHideTimer.start(500);
#else
        m_mouseIn = false;
        DUtil::TimerSingleShot(100, [this]() {
            if (!m_mouseIn)
                popup();
        });
#endif
    }
    void setValue(int v)
    {
        _slider->setValue(v);
        m_pLabShowVolume->setText(QString("%1%").arg(v * 1.0 / _slider->maximum() * 100));
    }
    void changeSate()
    {
        _mw->requestAction(ActionFactory::ToggleMute);
    }
    void slotValueChanged()
    {
        if (m_bIsMute) {
            changeSate();
        }
        auto var = _slider->value();
        m_pLabShowVolume->setText(QString("%1%").arg(var * 1.0 / _slider->maximum() * 100));
        _mw->requestAction(ActionFactory::ChangeVolume, false, QList<QVariant>() << var);
    }
    bool getsliderstate()
    {
        return m_bFinished;
    }
    void setThemeSlot(int type)
    {
        setVolumeIcon(type);
    }

protected:
    void enterEvent(QEvent *e)
    {
#ifdef __x86_64__
        _autoHideTimer.stop();
#else
        m_mouseIn = true;
        QWidget::leaveEvent(e);
#endif
    }
    void showEvent(QShowEvent *se)
    {
#ifdef __x86_64__
        _autoHideTimer.stop();
#else
        //m_mouseIn = true;   //fix bug 49617
        QWidget::showEvent(se);
#endif
    }
    void leaveEvent(QEvent *e)
    {
#ifdef __x86_64__
        _autoHideTimer.start(500);
#else
        m_mouseIn = false;
        delayedHide();
        QWidget::leaveEvent(e);
#endif
    }
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::HighQualityAntialiasing);
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);

        QPalette palette = this->palette();

//        float penWidthf = 1.0;
        QBrush background =  palette.background();
        QColor borderColor = m_borderColor;

        const qreal radius = m_radius;
        const qreal triHeight = 12;
//        const qreal triWidth = 16;
        const qreal height = this->height();
        const qreal width = this->width();

        QRectF topLeftRect(QPointF(0, 0),
                           QSizeF(2 * radius, 2 * radius));
        QRectF bottomLeftRect(QPointF(0, height - 2 * radius - triHeight),
                              QSizeF(2 * radius, 2 * radius));
        QRectF bottomRightRect(QPointF(width - 2 * radius, height - 2 * radius - triHeight),
                               QSizeF(2 * radius, 2 * radius));
        QRectF topRightRect(QPointF(width - 2 * radius, 0),
                            QSizeF(2 * radius, 2 * radius));


        path.moveTo(width, height - radius - triHeight);
        path.lineTo(width,  radius);
        path.arcTo(topRightRect, 0.0, 90.0);
        path.lineTo(radius,  0.0);
        path.arcTo(topLeftRect, 90.0, 90.0);
        path.lineTo(0, height - radius - triHeight);    //0,173
        path.arcTo(bottomLeftRect, 180.0, 30.0);
        path.lineTo(width / 2, height);

        path.lineTo(width, height - radius - triHeight);
        path.arcTo(bottomRightRect, 0.0, -30.0);
//        path.arcTo(width - 2 * radius, height - triHeight -2 * radius, 2 * radius, 2 * radius, 0.0, -30);
        path.lineTo(width / 2, height);
        path.lineTo(width, height - radius - triHeight);

        /*
        FIXME: light: white
        painter.fillPath(path, QColor(49, 49, 49));
        FIXME: light: QColor(0, 0, 0, 51)
        QPen pen(QColor(0, 0, 0, 0.1 * 255));
        */

        QPen pen(QColor(43, 43, 43));
        pen.setWidthF(1.0);
        if (m_sThemeType == DGuiApplicationHelper::DarkType) {
            painter.setPen(pen);
            painter.setBrush(QBrush(QColor(43, 43, 43)));
            painter.drawPath(path);
//            painter.fillPath(path, QColor(43, 43, 43));
        } else {
            pen.setColor(background.color());
            painter.setPen(pen);
            painter.setBrush(background);
            painter.drawPath(path);
//            painter.fillPath(path, background);
        }

//        QPen pen(borderColor);
//        pen.setWidth(penWidthf);
        //painter.strokePath(path, pen);
    }

private:
    void setVolumeIcon(int type)
    {
        m_sThemeType = type;
        if (m_sThemeType == DGuiApplicationHelper::DarkType) {
            if (m_bIsMute) {
                m_pBtnChangeMute->setImage(":/icons/deepin/builtin/light/actions/mute_checked.svg");
            } else {
                if (_slider->value() >= 66)
                    m_pBtnChangeMute->setImage(":/resources/icons/dark/normal/volume_normal.svg");
                else if (_slider->value() >= 33)
                    m_pBtnChangeMute->setImage(":/resources/icons/dark/normal/volume_mid_normal.svg");
                else
                    m_pBtnChangeMute->setImage(":/resources/icons/dark/normal/volume_low_normal.svg");
            }
        } else {
            if (m_bIsMute) {
                m_pBtnChangeMute->setImage(":/icons/deepin/builtin/dark/texts/dcc_mute_36px.svg");
            } else {
                if (_slider->value() >= 66)
                    m_pBtnChangeMute->setImage(":/icons/deepin/builtin/dark/texts/dcc_volume_36px.svg");
                else if (_slider->value() >= 33)
                    m_pBtnChangeMute->setImage(":/icons/deepin/builtin/dark/texts/dcc_volumemid_36px.svg");
                else
                    m_pBtnChangeMute->setImage(":/icons/deepin/builtin/dark/texts/dcc_volumelow_36px.svg");
            }
        }
    }

private slots:
    bool eventFilter(QObject *obj, QEvent *e)
    {
        if (e->type() == QEvent::Wheel) {
            QWheelEvent *we = static_cast<QWheelEvent *>(e);
            qDebug() << we->angleDelta() << we->modifiers() << we->buttons();
            if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
                if (_slider->value() == _slider->maximum() && we->angleDelta().y() > 0) {
                    //keep increasing volume
                    _mw->requestAction(ActionFactory::VolumeUp);
                } else {
                    _mw->requestAction(we->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
                }
            }
            return false;
        } else {
            return QObject::eventFilter(obj, e);
        }
    }

private:
    ImageButton *m_pBtnChangeMute {nullptr};
    QLabel *m_pLabShowVolume {nullptr};
    PlayerEngine *_engine;
    DSlider *_slider;
    MainWindow *_mw;
    QTimer _autoHideTimer;
//    bool m_composited = false;
    bool m_mouseIn = false;
    bool m_bIsMute {false};
    bool m_bFinished {false};
    QPropertyAnimation *pVolAnimation {nullptr};
//    QPropertyAnimation *pVolAnimTran {nullptr};
//    QParallelAnimationGroup *m_anima {nullptr};
    State state {Close};

    QColor m_borderColor = QColor(0, 0, 0,  255 * 2 / 10);
    int m_radius = 20;
    int m_sThemeType = 0;
    QPoint m_point {0, 0};
};

viewProgBarLoad::viewProgBarLoad(PlayerEngine *engine, DMRSlider *progBar, ToolboxProxy *parent)
{
    _parent = parent;
    _engine = engine;
    _progBar = progBar;
    m_seekTime = new char[12];
    initThumb();
}

void viewProgBarLoad::setListPixmapMutex(QMutex *pMutex)
{
    pListPixmapMutex = pMutex;
}

void viewProgBarLoad::run()
{
    loadViewProgBar(_parent->size());
}

QString libPath(const QString &strlib)
{
    QDir  dir;
    QString path  = QLibraryInfo::location(QLibraryInfo::LibrariesPath);
    dir.setPath(path);
    QStringList list = dir.entryList(QStringList() << (strlib + "*"), QDir::NoDotAndDotDot | QDir::Files); //filter name with strlib
    if (list.contains(strlib)) {
        return strlib;
    } else {
        list.sort();
    }

    Q_ASSERT(list.size() > 0);
    return list.last();
}

void viewProgBarLoad::initThumb()
{
//#ifdef __x86_64__
//    const char *path = "/usr/lib/x86_64-linux-gnu/libffmpegthumbnailer.so.4";
//#elif __mips__
//    const char *path = "/usr/lib/mips64el-linux-gnuabi64/libffmpegthumbnailer.so.4";
//#elif __aarch64__
//    const char *path = "/usr/lib/aarch64-linux-gnu/libffmpegthumbnailer.so.4";
//#elif __sw_64__
//    const char *path = "/usr/lib/sw_64-linux-gnu/libffmpegthumbnailer.so.4";
//#else
//    const char *path = "/usr/lib/i386-linux-gnu/libffmpegthumbnailer.so.4";
//#endif

    QLibrary library(libPath("libffmpegthumbnailer.so"));
    m_mvideo_thumbnailer = (mvideo_thumbnailer) library.resolve("video_thumbnailer_create");
    m_mvideo_thumbnailer_destroy = (mvideo_thumbnailer_destroy) library.resolve("video_thumbnailer_destroy");
    m_mvideo_thumbnailer_create_image_data = (mvideo_thumbnailer_create_image_data) library.resolve("video_thumbnailer_create_image_data");
    m_mvideo_thumbnailer_destroy_image_data = (mvideo_thumbnailer_destroy_image_data) library.resolve("video_thumbnailer_destroy_image_data");
    m_mvideo_thumbnailer_generate_thumbnail_to_buffer = (mvideo_thumbnailer_generate_thumbnail_to_buffer) library.resolve("video_thumbnailer_generate_thumbnail_to_buffer");
    if (m_mvideo_thumbnailer == nullptr || m_mvideo_thumbnailer_destroy == nullptr
            || m_mvideo_thumbnailer_create_image_data == nullptr || m_mvideo_thumbnailer_destroy_image_data == nullptr
            || m_mvideo_thumbnailer_generate_thumbnail_to_buffer == nullptr)

    {
        return;
    }
    m_video_thumbnailer = m_mvideo_thumbnailer();
}

void viewProgBarLoad::loadViewProgBar(QSize size)
{
    auto pixWidget =  40;
    auto num = int(_progBar->width() / (40 + 1)); //number of thumbnails
    auto tmp = (_engine->duration() * 1000) / num;

    QList<QPixmap> pm;
    QList<QPixmap> pm_black;

    QTime time(0, 0, 0, 0);
    qDebug() << _engine->videoSize().width();
    qDebug() << _engine->videoSize().height();
    qDebug() << qApp->devicePixelRatio();
    if (_engine->videoSize().width() > 0 && _engine->videoSize().height() > 0) {
        m_video_thumbnailer->thumbnail_size = (static_cast<int>(50 * (_engine->videoSize().width() / _engine->videoSize().height() * 50)
                                                                * qApp->devicePixelRatio()));
    }

    if (m_image_data == nullptr) {
        m_image_data = m_mvideo_thumbnailer_create_image_data();
    }

//    m_video_thumbnailer->seek_time = d.toString("hh:mm:ss").toLatin1().data();
    int length = strlen(time.toString("hh:mm:ss").toLatin1().data());
    memcpy(m_seekTime, time.toString("hh:mm:ss").toLatin1().data(), length + 1);
    m_video_thumbnailer->seek_time = m_seekTime;

    auto url = _engine->playlist().currentInfo().url;
    auto file = QFileInfo(url.toLocalFile()).absoluteFilePath();

    for (auto i = 0; i < num ; i++) {
        if (isInterruptionRequested()) {
            qDebug() << "isInterruptionRequested";
            return;
        }

        time = time.addMSecs(tmp);

//        m_video_thumbnailer->seek_time = d.toString("hh:mm:ss").toLatin1().data();
        memcpy(m_seekTime, time.toString("hh:mm:ss").toLatin1().data(), length + 1);
        m_video_thumbnailer->seek_time = m_seekTime;
        try {

            m_mvideo_thumbnailer_generate_thumbnail_to_buffer(m_video_thumbnailer, file.toUtf8().data(),  m_image_data);
            auto img = QImage::fromData(m_image_data->image_data_ptr, static_cast<int>(m_image_data->image_data_size), "png");
            if (img.format() == QImage::Format_Invalid) {
                return;
            }
            auto img_tmp = img.scaledToHeight(50);


            pm.append(QPixmap::fromImage(img_tmp.copy(img_tmp.size().width() / 2 - 4, 0, pixWidget, 50))); //-2 为了1px的内边框
            QImage img_black = img_tmp.convertToFormat(QImage::Format_Grayscale8);
            pm_black.append(QPixmap::fromImage(img_black.copy(img_black.size().width() / 2 - 4, 0, pixWidget, 50)));

        } catch (const std::logic_error &) {

        }
    }

    m_mvideo_thumbnailer_destroy_image_data(m_image_data);
    m_image_data = nullptr;

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
    if (!composited) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
        setContentsMargins(0, 0, 0, 0);
        setAttribute(Qt::WA_NativeWindow);
    }

    /*    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect(this);
        shadowEffect->setOffset(0, 4);
        shadowEffect->setBlurRadius(8);
        shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
        connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
            if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::LightType)
            {
                shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
                shadowEffect->setOffset(0, 4);
                shadowEffect->setBlurRadius(8);
            } else if (DGuiApplicationHelper::instance()->themeType() == DGuiApplicationHelper::DarkType)
            {
                shadowEffect->setColor(QColor(0, 0, 0, 0.2 * 255));
                shadowEffect->setOffset(0, 2);
                shadowEffect->setBlurRadius(4);
            } else
            {
                shadowEffect->setColor(QColor(0, 0, 0, 0.1 * 255));
                shadowEffect->setOffset(0, 4);
                shadowEffect->setBlurRadius(8);
            }
        });
        setGraphicsEffect(shadowEffect);


        DThemeManager::instance()->registerWidget(this);
    */

    paopen = nullptr;
    paClose = nullptr;

    label_list.clear();
    label_black_list.clear();
    pm_list.clear();
    pm_black_list.clear();

    _previewer = new ThumbnailPreview;
//    _previewer->hide();

    _previewTime = new SliderTime;
    _previewTime->hide();

//    _subView = new SubtitlesView(nullptr, _engine);
//    _subView->hide();
    setup();

    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged,
            this, &ToolboxProxy::updatePlayState);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged,
            this, &ToolboxProxy::updateplaylisticon);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged,
            _volSlider, &VolumeSlider::setThemeSlot);


    QFileInfo fi("/dev/mwv206_0");              //景嘉微显卡
    if (utils::check_wayland_env() && fi.exists()) {
        _isJinJia = true;
        connect(&_progressTimer, &QTimer::timeout, [ = ]() {
            oldDuration = _engine->duration();
            oldElapsed = _engine->elapsed();
        });
    }
}
void ToolboxProxy::finishLoadSlot(QSize size)
{
    qDebug() << "thumbnail has finished";

    if (pm_list.isEmpty()) return;

    if (!_bthumbnailmode) {
        return;
    }
    //    if (isStillShowThumbnail) {
    _viewProgBar->setViewProgBar(_engine, pm_list, pm_black_list);
    //    }

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
        isStillShowThumbnail = true;
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

void ToolboxProxy::setInitVolume(int v)
{
    m_initVolume = v;
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
    delete _previewer;
    delete _previewTime;

    if (m_worker) {
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

    bot_widget = new DBlurEffectWidget(this);
    bot_widget->setObjectName(BOTTOM_WIDGET);
//    bot_widget->setBlurBackgroundEnabled(true);
    bot_widget->setBlurRectXRadius(18);
    bot_widget->setBlurRectYRadius(18);
    bot_widget->setRadius(30);
    bot_widget->setBlurEnabled(true);
    bot_widget->setMode(DBlurEffectWidget::GaussianBlur);
    auto type = DGuiApplicationHelper::instance()->themeType();
    THEME_TYPE(type);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, &ToolboxProxy::slotThemeTypeChanged);



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
    bot_toolWgt->setObjectName(BOTTOM_TOOL_BUTTON_WIDGET);
    bot_toolWgt->setFixedHeight(TOOLBOX_HEIGHT - 10);
    bot_toolWgt->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *bot_layout = new QHBoxLayout(bot_toolWgt);
    bot_layout->setContentsMargins(LEFT_MARGIN, 0, RIGHT_MARGIN, 0);
    bot_layout->setSpacing(0);
    bot_toolWgt->setLayout(bot_layout);
    botv->addWidget(bot_toolWgt);

    bot_widget->setLayout(botv);
    stacked->addWidget(bot_widget);

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
    _progBar->slider()->setFocusPolicy(Qt::NoFocus);
    _progBar->setObjectName(MOVIE_PROGRESS_WIDGET);
    _progBar->slider()->setOrientation(Qt::Horizontal);
    _progBar->slider()->setObjectName(PROGBAR_SLIDER);
    _progBar->slider()->setAccessibleName(PROGBAR_SLIDER);
    _progBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _progBar->slider()->setRange(0, 100);
    _progBar->setValue(0);
    _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);

    connect(_previewer, &ThumbnailPreview::leavePreview, this, &ToolboxProxy::slotLeavePreview);
    connect(&Settings::get(), &Settings::baseChanged, this, &ToolboxProxy::setthumbnailmode);
    connect(_engine, &PlayerEngine::siginitthumbnailseting, this, &ToolboxProxy::setthumbnailmode);

    //刷新显示预览当前时间的label
    connect(_progBar, &DMRSlider::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::sliderMoved, this, &ToolboxProxy::progressHoverChanged);
    connect(_progBar, &DMRSlider::leave, this, &ToolboxProxy::slotHidePreviewTime);

    connect(_progBar, &DMRSlider::sliderPressed, this, &ToolboxProxy::slotSliderPressed);
    connect(_progBar, &DMRSlider::sliderReleased, this, &ToolboxProxy::slotSliderReleased);
    connect(&Settings::get(), &Settings::baseMuteChanged, this, &ToolboxProxy::slotBaseMuteChanged);

    _viewProgBar = new ViewProgBar(_progBar, bot_toolWgt);


    //刷新显示预览当前时间的label
    connect(_viewProgBar, &ViewProgBar::hoverChanged, this, &ToolboxProxy::progressHoverChanged);
    connect(_viewProgBar, &ViewProgBar::leave, this, &ToolboxProxy::slotHidePreviewTime);
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
    time->setContentsMargins(11, 9, 11, 9);
    time->setSpacing(0);
    time->setAlignment(Qt::AlignLeft);
    bot_layout->addLayout(time);
    time->addWidget(_timeLabel);
//    bot->addStretch();
    QHBoxLayout *progBarspec = new QHBoxLayout(bot_toolWgt);
    progBarspec->setContentsMargins(0, 5, 0, 0);
    progBarspec->setSpacing(0);
    progBarspec->setAlignment(Qt::AlignHCenter);

    if (utils::check_wayland_env()) {
        //lmh0706,延时
        connect(_nextBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
        connect(_playBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
        connect(_prevBtn, &DButtonBoxButton::clicked, this, &ToolboxProxy::waitPlay);
    }

    _progBar_Widget = new QStackedWidget(bot_toolWgt);
    _progBar_Widget->setObjectName(PROGBAR_WIDGET);
    _progBar_Widget->setAccessibleName(PROGBAR_WIDGET);
    _progBar_Widget->setContentsMargins(0, 0, 0, 0);
    _progBar_Widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    _progBarspec = new DWidget(_progBar_Widget);
    _progBarspec->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

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
    _palyBox->setObjectName(PLAY_BUTTOB_BOX);
    _palyBox->setFocusPolicy(Qt::NoFocus);
    _mid->addWidget(_palyBox);
    _mid->setAlignment(_palyBox, Qt::AlignLeft);
    QList<DButtonBoxButton *> list;

    if (utils::check_wayland_env()) {
        _prevBtn = new ButtonBoxButton("", this);
        _playBtn = new ButtonBoxButton("", this);
        _nextBtn = new ButtonBoxButton("", this);
    } else {
        _prevBtn = new DButtonBoxButton("", this);
        _playBtn = new DButtonBoxButton("", this);
        _nextBtn = new DButtonBoxButton("", this);
    }

    _prevBtn->setIcon(QIcon::fromTheme("dcc_last", QIcon(":/icons/deepin/builtin/light/normal/last_normal.svg")));
    _prevBtn->setIconSize(QSize(36, 36));
    _prevBtn->setFixedSize(40, 50);
    _prevBtn->setObjectName(PREV_BUTTON);
    _prevBtn->setAccessibleName(PREV_BUTTON);
    _prevBtn->setFocusPolicy(Qt::TabFocus);
    connect(_prevBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_prevBtn, "prev");
    list.append(_prevBtn);

    _playBtn->setIcon(QIcon::fromTheme("dcc_play", QIcon(":/icons/deepin/builtin/light/normal/play_normal.svg")));
    _playBtn->setIconSize(QSize(36, 36));
    _playBtn->setFixedSize(40, 50);
    _playBtn->setFocusPolicy(Qt::TabFocus);
    _playBtn->setObjectName(PLAY_BUTTON);
    _playBtn->setAccessibleName(PLAY_BUTTON);
    connect(_playBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_playBtn, "play");
    list.append(_playBtn);

    _nextBtn->setIcon(QIcon::fromTheme("dcc_next", QIcon(":/icons/deepin/builtin/light/normal/next_normal.svg")));
    _nextBtn->setIconSize(QSize(36, 36));
    _nextBtn->setFixedSize(40, 50);
    _nextBtn->setFocusPolicy(Qt::TabFocus);
    _nextBtn->setObjectName(NEXT_BUTTON);
    _nextBtn->setAccessibleName(NEXT_BUTTON);
    connect(_nextBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_nextBtn, "next");
    list.append(_nextBtn);
    _palyBox->setButtonList(list, false);

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
    _subBtn->hide();

    _fsBtn = new ToolButton(bot_toolWgt);
    _fsBtn->setObjectName(FS_BUTTON);
    _fsBtn->setAccessibleName(FS_BUTTON);
    _fsBtn->setFocusPolicy(Qt::TabFocus);
    _fsBtn->setIcon(QIcon::fromTheme("dcc_zoomin"));
    _fsBtn->setIconSize(QSize(36, 36));
    _fsBtn->setFixedSize(50, 50);
    _fsBtn->initToolTip();
    connect(_fsBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_fsBtn, "fs");

    _volBtn = new VolumeButton(bot_toolWgt);
    _volBtn->setFixedSize(50, 50);
    _volBtn->setFocusPolicy(Qt::TabFocus);
    _volBtn->setObjectName(VOLUME_BUTTON);
    _volBtn->setAccessibleName(VOLUME_BUTTON);
    if (CompositingManager::get().composited()) {
        _volSlider = new VolumeSlider(_engine, _mainWindow, _mainWindow);
        _volSlider->setObjectName(VOLUME_SLIDER_WIDGET);
        connect(_volBtn, &VolumeButton::clicked, this, &ToolboxProxy::slotVolumeButtonClicked);
        connect(_mainWindow, &MainWindow::volumeChanged, _volSlider, &VolumeSlider::slotVolumeChanged);

    } else {
#if defined (__mips__) || defined (__aarch64__)
        _volSlider = new VolumeSlider(_engine, _mainWindow, _mainWindow);
        _volSlider->setObjectName(VOLUME_SLIDER_WIDGET);
        connect(_volBtn, &VolumeButton::clicked, this, &ToolboxProxy::slotVolumeButtonClicked);

//        _volSlider->setProperty("DelayHide", true);
//        _volSlider->setProperty("NoDelayShow", true);
//        installHint(_volBtn, _volSlider);
#else
        _volSlider = new VolumeSlider(_engine, _mainWindow, _mainWindow);
        _volSlider->setObjectName(VOLUME_SLIDER_WIDGET);;
        connect(_volBtn, &VolumeButton::clicked, this, &ToolboxProxy::slotVolumeButtonClicked);
        connect(_mainWindow, &MainWindow::volumeChanged, _volSlider, &VolumeSlider::slotVolumeChanged);
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
    connect(_volBtn, &VolumeButton::requestVolumeUp, this, &ToolboxProxy::slotRequestVolumeUp);
    connect(_volBtn, &VolumeButton::requestVolumeDown, this, &ToolboxProxy::slotRequestVolumeDown);

    _right->addWidget(_subBtn);
    _right->addWidget(_fsBtn);
    _right->addSpacing(10);
    _right->addWidget(_volBtn);
    _right->addSpacing(10);

    _listBtn = new ToolButton(bot_toolWgt);
    _listBtn->setIcon(QIcon::fromTheme("dcc_episodes"));
    _listBtn->setIconSize(QSize(36, 36));
    _listBtn->setFocusPolicy(Qt::TabFocus);
    _listBtn->setFixedSize(50, 50);
    _listBtn->initToolTip();
    _listBtn->setCheckable(true);
    _listBtn->setObjectName(PLAYLIST_BUTTON);
    _listBtn->setAccessibleName(PLAYLIST_BUTTON);

    connect(_listBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_listBtn, "list");
    _right->addWidget(_listBtn);

//    setTabOrder(_nextBtn, _progBar->slider());
//    setTabOrder(_prevBtn, _prevBtn);

    // these tooltips is not used due to deepin ui design
    //lmh0910wayland下用这一套tooltip
    if (utils::check_wayland_env()) {
        initToolTip();
    } else {
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
    }

    connect(_engine, &PlayerEngine::stateChanged, this, &ToolboxProxy::updatePlayState);
    connect(_engine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::slotFileLoaded);

    connect(_engine, &PlayerEngine::elapsedChanged, this, &ToolboxProxy::slotElapsedChanged);
    connect(_engine, &PlayerEngine::updateDuration, this, &ToolboxProxy::slotElapsedChanged);


    connect(window()->windowHandle(), &QWindow::windowStateChanged, this, &ToolboxProxy::updateFullState);
    connect(_engine, &PlayerEngine::muteChanged, this, &ToolboxProxy::updateVolumeState);
    connect(_engine, &PlayerEngine::volumeChanged, this, &ToolboxProxy::updateVolumeState);

    connect(_engine, &PlayerEngine::tracksChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_engine, &PlayerEngine::fileLoaded, this, &ToolboxProxy::updateButtonStates);
    connect(&_engine->playlist(), &PlaylistModel::countChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_mainWindow, &MainWindow::initChanged, this, &ToolboxProxy::updateButtonStates);
    connect(_mainWindow, &MainWindow::volumeChanged, this, &ToolboxProxy::updateVolumeState);

    updatePlayState();
    updateFullState();
    updateButtonStates();

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &ToolboxProxy::slotApplicationStateChanged);

    PlaylistModel *playListModel = _engine->getplaylist();
    connect(playListModel, &PlaylistModel::currentChanged, this, &ToolboxProxy::slotPlayListCurrentChanged);
}

void ToolboxProxy::updateThumbnail()
{
    disconnect(m_worker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));

    if (utils::check_wayland_env()) {
        return;
    }
    //如果打开的是音乐
    QString suffix = _engine->playlist().currentInfo().info.suffix();
    foreach (QString sf, _engine->audio_filetypes) {
        if (sf.right(sf.size() - 2) == suffix) {
            return;
        }
    }

    qDebug() << "worker" << m_worker;
    QTimer::singleShot(1000, this, &ToolboxProxy::slotUpdateThumbnailTimeOut);

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
        qDebug() << "hide previewer";
    }

    if (_previewTime->isVisible()) {
        _previewTime->hide();
    }

//    if (_subView->isVisible()) {
//        _subView->hide();
//    }

    if (_volSlider->isVisible()) {
        _volSlider->stopTimer();
        _volSlider->popup();
    }
}

bool ToolboxProxy::anyPopupShown() const
{
    return _previewer->isVisible() || _previewTime->isVisible() || _volSlider->isVisible();
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

    const PlayItemInfo &pif = _engine->playlist().currentInfo();
    if (!pif.url.isLocalFile())
        return;

    const QString &absPath = pif.info.canonicalFilePath();
    if (!QFile::exists(absPath)) {
        _previewer->hide();
        _previewTime->hide();
        return;
    }

    if (!m_mouseFlag) {
        return;
    }

    int nPosition = 0;
    qint64 nDuration = _engine->duration();
    QPoint showPoint = {0, 0};

    if (_progBar->isVisible()) {
        nPosition = (secs * _progBar->slider()->width()) / nDuration;
        showPoint = _progBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    } else {
        nPosition = secs * _viewProgBar->getViewLength() / nDuration + _viewProgBar->getStartPoint();
        showPoint = _viewProgBar->mapToGlobal(QPoint(nPosition, TOOLBOX_TOP_EXTENT - 10));
    }

    QPixmap pm = ThumbnailWorker::get().getThumb(url, secs);


    if (!pm.isNull()) {
        QPoint point { showPoint.x(), showPoint.y() };
        _previewer->updateWithPreview(pm, secs, _engine->videoRotation());
        _previewer->updateWithPreview(point);
    }
}

void ToolboxProxy::waitPlay()
{
    if (_playBtn) {
        _playBtn->setEnabled(false);
    }
    if (_prevBtn) {
        _prevBtn->setEnabled(false);
    }
    if (_nextBtn) {
        _nextBtn->setEnabled(false);
    }
    QTimer::singleShot(500, [ = ] {
        if (_playBtn)
        {
            _playBtn->setEnabled(true);
        }
        if (_prevBtn && _engine->playlist().count() > 1)
        {
            _prevBtn->setEnabled(true);
        }
        if (_nextBtn && _engine->playlist().count() > 1)
        {
            _nextBtn->setEnabled(true);
        }
    });
}

void ToolboxProxy::slotThemeTypeChanged()
{
    auto type = DGuiApplicationHelper::instance()->themeType();
    WAYLAND_BLACK_WINDOW;
    THEME_TYPE(type);
}

void ToolboxProxy::slotLeavePreview()
{
    auto pos = _progBar->mapFromGlobal(QCursor::pos());
    if (!_progBar->geometry().contains(pos)) {
        _previewer->hide();
        _previewTime->hide();
        _progBar->forceLeave();
    }
}

void ToolboxProxy::slotHidePreviewTime()
{
    _previewer->hide();
    _previewTime->hide();
    m_mouseFlag = false;
}

void ToolboxProxy::slotSliderPressed()
{
    m_mousePree = true;
}

void ToolboxProxy::slotSliderReleased()
{
    m_mousePree = false;
    _engine->seekAbsolute(_progBar->slider()->sliderPosition());
}

void ToolboxProxy::slotBaseMuteChanged(QString sk, const QVariant &/*val*/)
{
    if (sk == "base.play.mousepreview") {
        _progBar->setEnableIndication(_engine->state() != PlayerEngine::Idle);
    }
}

void ToolboxProxy::slotVolumeButtonClicked()
{
    if (_volSlider->getsliderstate())
        return;
    /*
     * 设置-2为已经完成第一次打开设置音量
     * -1为初始化数值
     * 大于等于零表示为已完成初始化
     */
    if (m_initVolume >= 0) {
        setDisplayValue(m_initVolume);
        m_initVolume = -2;
    }
    if (CompositingManager::get().composited()) {
        if (!_volSlider->isVisible()) {
            _volSlider->show(_mainWindow->width() - _volBtn->width() / 2 - _playBtn->width() - 40,
                             _mainWindow->height() - TOOLBOX_HEIGHT - 2);
            _volSlider->popup();
        } else {
            _volSlider->popup();
        }
    } else {
#if defined (__mips__) || defined (__aarch64__)
        if (!_volSlider->isVisible()) {
            auto pPoint = mapToGlobal(QPoint(this->rect().width(), this->rect().height()));
            _volSlider->adjustSize();

            pPoint.setX(pPoint.x()  - _volBtn->width() / 2 - _playBtn->width() - 43);
            pPoint.setY(pPoint.y() - TOOLBOX_HEIGHT - 5);
            _volSlider->show(pPoint.x(), pPoint.y());
            _volSlider->popup();
        } else {
            _volSlider->popup();
        }
#else
        if (!_volBtn->isVisible()) {
            _volSlider->show(_mainWindow->width() - _volBtn->width() / 2 - _playBtn->width() - 43,
                             _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            _volSlider->raise();
            _volSlider->popup();
        } else {
            _volSlider->popup();
        }
#endif
    }
}

void ToolboxProxy::slotRequestVolumeUp()
{
    _mainWindow->requestAction(ActionFactory::ActionKind::VolumeUp);
}

void ToolboxProxy::slotRequestVolumeDown()
{
    _mainWindow->requestAction(ActionFactory::ActionKind::VolumeDown);
}

void ToolboxProxy::slotFileLoaded()
{
//    _viewProgBar->clear();
    _progBar->slider()->setRange(0, static_cast<int>(_engine->duration()));
//        _progBar_stacked->setCurrentIndex(1);
    _progBar_Widget->setCurrentIndex(1);
    _loadsize = size();
    update();
    //        updateThumbnail();
}

void ToolboxProxy::slotElapsedChanged()
{
    quint64 url = static_cast<quint64>(-1);
    if (_engine->playlist().current() != -1) {
        url = static_cast<quint64>(_engine->duration());
    }
    updateTimeInfo(static_cast<qint64>(url), _engine->elapsed(), _timeLabel, _timeLabelend, true);
    updateTimeInfo(static_cast<qint64>(url), _engine->elapsed(), _fullscreentimelable, _fullscreentimelableend, false);
    QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    _fullscreentimelable->setMinimumWidth(fm.width(_fullscreentimelable->text()));
    _fullscreentimelableend->setMinimumWidth(fm.width(_fullscreentimelableend->text()));
    updateMovieProgress();
}

void ToolboxProxy::slotApplicationStateChanged(Qt::ApplicationState e)
{
    if (e == Qt::ApplicationInactive && anyPopupShown()) {
        closeAnyPopup();
    }
}

void ToolboxProxy::slotPlayListCurrentChanged()
{
    _autoResizeTimer.start(1000);
}

void ToolboxProxy::slotPlayListStateChange()
{
    if (bAnimationFinash == false) {
        return ;
    }

    if (_playlist->state() == PlaylistWidget::State::Opened) {
#ifdef __x86_64__
        QRect rcBegin = this->geometry();
        QRect rcEnd = rcBegin;
        rcEnd.setY(rcBegin.y() - TOOLBOX_SPACE_HEIGHT - 7);
        bAnimationFinash = false;
        paopen = new QPropertyAnimation(this, "geometry");
        paopen->setEasingCurve(QEasingCurve::Linear);
        paopen->setDuration(POPUP_DURATION) ;
        paopen->setStartValue(rcBegin);
        paopen->setEndValue(rcEnd);
        paopen->start();
        connect(paopen, &QPropertyAnimation::finished, this, &ToolboxProxy::slotProAnimationFinished);
#else
        QRect rcBegin = this->geometry();
        QRect rcEnd = rcBegin;
        rcEnd.setY(rcBegin.y() - TOOLBOX_SPACE_HEIGHT - 7);
        setGeometry(rcEnd);
#endif
        _listBtn->setChecked(true);
    } else {
        _listBtn->setChecked(false);
#ifdef __x86_64__
        bAnimationFinash = false;

        QRect rcBegin = this->geometry();
        QRect rcEnd = rcBegin;
        rcEnd.setY(rcBegin.y() + TOOLBOX_SPACE_HEIGHT + 7);
        paClose = new QPropertyAnimation(this, "geometry");
        paClose->setEasingCurve(QEasingCurve::Linear);
        paClose->setDuration(POPUP_DURATION);
        paClose->setStartValue(rcBegin);
        paClose->setEndValue(rcEnd);
        paClose->start();
        connect(paClose, &QPropertyAnimation::finished, this, &ToolboxProxy::slotProAnimationFinished);
#else
        QRect rcBegin = this->geometry();
        QRect rcEnd = rcBegin;
        rcEnd.setY(rcBegin.y() + TOOLBOX_SPACE_HEIGHT + 7);
        setGeometry(rcEnd);
#endif
    }
}

void ToolboxProxy::slotUpdateThumbnailTimeOut()
{
    //如果视频长度小于1s应该直接返回不然会UI错误
    if (_engine->playlist().currentInfo().mi.duration < 1) {
        return;
    }

    _viewProgBar->clear();  //清除前一次进度条中的缩略图,以便显示新的缩略图
    m_listPixmapMutex.lock();
    pm_list.clear();
    pm_black_list.clear();
    m_listPixmapMutex.unlock();

    if (m_worker == nullptr) {
        m_worker = new viewProgBarLoad(_engine, _progBar, this);
        m_worker->setListPixmapMutex(&m_listPixmapMutex);
    }
    m_worker->requestInterruption();
    QTimer::singleShot(500, this, [ = ] {m_worker->start();});
    connect(m_worker, SIGNAL(sigFinishiLoad(QSize)), this, SLOT(finishLoadSlot(QSize)));
    _progBar_Widget->setCurrentIndex(1);
}

void ToolboxProxy::slotProAnimationFinished()
{
    QObject *pProAnimation = sender();
    if (pProAnimation == paopen) {
        paopen->deleteLater();
        paopen = nullptr;
        bAnimationFinash = true;
    } else if (pProAnimation == paClose) {
        paClose->deleteLater();
        paClose = nullptr;
        bAnimationFinash = true;
    }
}

void ToolboxProxy::progressHoverChanged(int v)
{
    if (_engine->state() == PlayerEngine::CoreState::Idle)
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

    m_mouseFlag = true;

    ThumbnailWorker::get().requestThumb(pif.url, v);
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
    if (d > _progBar->maximum()) {
        d = _progBar->maximum();
    }
    int v = 0;
    int v2 = 0;
    if (d != 0 && e != 0) {
        v = static_cast<int>(_progBar->maximum() * e / d);
        v2 = static_cast<int>(_viewProgBar->getViewLength() * e / d + _viewProgBar->getStartPoint());
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

    _prevBtn->setDisabled(!vis);
    _nextBtn->setDisabled(!vis);
}

void ToolboxProxy::updateVolumeState()
{
    if (_engine->muted()) {
        _volBtn->changeLevel(VolumeButton::Mute);
        _volBtn->setToolTip(tr("Mute"));
        _volSlider->setMute(true);
    } else {
        int v = _volSlider->value();
        if (v > 0) {
            _volSlider->setMute(false);
        }
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
    if (isFullscreen || _fullscreentimelable->isVisible()) {
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
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
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
            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 255));
            _palyBox->setPalette(pa);

            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _listBtn->setPalette(pa);
        }
        _playBtn->setIcon(QIcon::fromTheme("dcc_suspend", QIcon(":/icons/deepin/builtin/light/normal/suspend_normal.svg")));
        //lmh0910wayland下用这一套tooltip
        if (utils::check_wayland_env()) {
            m_playBtnTip->setText(tr("Pause"));
        } else {
            _playBtn->setToolTip(tr("Pause"));
        }
    } else {
        //        _playBtn->setObjectName("PlayBtn");
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
//            _playBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/play_normal.svg",
//                                     ":/icons/deepin/builtin/light/normal/play_normal.svg",
//                                     ":/icons/deepin/builtin/light/press/play_press.svg");
//            _prevBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/last_normal.svg",
//                                     ":/icons/deepin/builtin/light/normal/last_normal.svg",
//                                     ":/icons/deepin/builtin/light/press/last_press.svg");
//            _nextBtn->setPropertyPic(":/icons/deepin/builtin/light/normal/next_normal.svg",
//                                     ":/icons/deepin/builtin/light/normal/next_normal.svg",
//                                     ":/icons/deepin/builtin/light/press/next_press.svg");

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
//            _playBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/play_normal.svg",
//                                     ":/icons/deepin/builtin/dark/normal/play_normal.svg",
//                                     ":/icons/deepin/builtin/dark/press/play_press.svg");
//            _prevBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/last_normal.svg",
//                                     ":/icons/deepin/builtin/dark/normal/last_normal.svg",
//                                     ":/icons/deepin/builtin/dark/press/last_press.svg");
//            _nextBtn->setPropertyPic(":/icons/deepin/builtin/dark/normal/next_normal.svg",
//                                     ":/icons/deepin/builtin/dark/normal/next_normal.svg",
//                                     ":/icons/deepin/builtin/dark/press/next_press.svg");
            DPalette pa;
            pa = _palyBox->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Button, QColor(0, 0, 0, 255));
            _palyBox->setPalette(pa);

            pa = _volBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _volBtn->setPalette(pa);

            pa = _fsBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _fsBtn->setPalette(pa);

            pa = _listBtn->palette();
            pa.setColor(DPalette::Light, QColor(0, 0, 0, 255));
            pa.setColor(DPalette::Dark, QColor(0, 0, 0, 255));
            _listBtn->setPalette(pa);

        }
        //lmh0910wayland下用这一套tooltip
        if (utils::check_wayland_env()) {
            m_playBtnTip->setText(tr("Play"));
        } else {
            _playBtn->setToolTip(tr("Play"));
        }
        _playBtn->setIcon(QIcon::fromTheme("dcc_play", QIcon(":/icons/deepin/builtin/light/normal/play_normal.svg")));
    }

    if (_engine->state() == PlayerEngine::CoreState::Idle) {
//        if (_subView->isVisible())
//            _subView->hide();

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
    //add by heyi
    static bool bFlags = true;
    if (bFlags) {
//        _mainWindow->firstPlayInit();
        _mainWindow->repaint();
        bFlags = false;
    }

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
    }
//    } else if (id == "sub") {
//        _subView->setVisible(true);

//        QPoint pos = _subBtn->parentWidget()->mapToGlobal(_subBtn->pos());
//        pos.ry() = parentWidget()->mapToGlobal(this->pos()).y();
//        _subView->show(pos.x() + _subBtn->width() / 2, pos.y() - 5 + TOOLBOX_TOP_EXTENT);
//    }
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

/*void ToolboxProxy::updatePosition(const QPoint &p)
{
    QPoint pos(p);
    pos.ry() += _mainWindow->height() - height();
    windowHandle()->setFramePosition(pos);
}*/

/*void ToolboxProxy::paintEvent(QPaintEvent *pe)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    static int offset = 14;

    DGuiApplicationHelper::ColorType themeType = DGuiApplicationHelper::instance()->themeType();
    QColor *bgColor, outBdColor, inBdColor;
    if (themeType == DGuiApplicationHelper::LightType) {
        outBdColor = QColor(0, 0, 0, 25);
        inBdColor = QColor(247, 247, 247, 0.4 * 255);
        bgColor = new QColor(247, 247, 247, 0.8 * 255);
    } else if (themeType == DGuiApplicationHelper::DarkType) {
        outBdColor = QColor(0, 0, 0, 0.8 * 255);
        inBdColor = QColor(255, 255, 255, 0.05 * 255);
        bgColor = new QColor(32, 32, 32, 0.9 * 255);
    } else {
        outBdColor = QColor(0, 0, 0, 25);
        inBdColor = QColor(247, 247, 247, 0.4 * 255);
        bgColor = new QColor(247, 247, 247, 0.8 * 255);
    }

    {
        QPainterPath pp;
        pp.setFillRule(Qt::WindingFill);
        QPen pen(outBdColor, 1);
        painter.setPen(pen);
        pp.addRoundedRect(bgRect, RADIUS_MV, RADIUS_MV);
        painter.fillPath(pp, *bgColor);
//        painter.drawPath(pp);

        painter.drawLine(offset, rect().y(), width() - offset, rect().y());
        painter.drawLine(offset, height(), width() - offset, height());
        painter.drawLine(rect().x(), offset, rect().x(), height() - offset);
        painter.drawLine(width(), offset, width(), height() - offset);
    }

//    {
//        auto view_rect = bgRect.marginsRemoved(QMargins(1, 1, 1, 1));
//        QPainterPath pp;
//        pp.setFillRule(Qt::WindingFill);
//        painter.setPen(inBdColor);
//        pp.addRoundedRect(view_rect, RADIUS_MV, RADIUS_MV);
//        painter.drawPath(pp);
//    }

    QWidget::paintEvent(pe);
}*/

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
            if (_bthumbnailmode) {  //如果进度条为胶片模式，重新加载缩略图并显示
//                isStillShowThumbnail = false;
                updateThumbnail();
//                _bthumbnailmode = false;
                updateMovieProgress();
            }
            _progBar_Widget->setCurrentIndex(1);
        }
    }
#ifndef __sw_64__
    if (!utils::check_wayland_env()) {
        if (bAnimationFinash ==  false && paopen != nullptr && paClose != nullptr) {

            _playlist->endAnimation();
            paopen->setDuration(0);
            paClose->setDuration(0);
        }


        if (_playlist && _playlist->state() == PlaylistWidget::State::Opened && bAnimationFinash == true) {
            QRect r(5, _mainWindow->height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7) - _mainWindow->rect().top() - 5,
                    _mainWindow->rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7));
            this->setGeometry(r);
        } else if (_playlist && _playlist->state() == PlaylistWidget::State::Closed && bAnimationFinash == true) {
            QRect r(5, _mainWindow->height() - TOOLBOX_HEIGHT - _mainWindow->rect().top() - 5,
                    _mainWindow->rect().width() - 10, TOOLBOX_HEIGHT);
            this->setGeometry(r);
        }

        updateTimeLabel();
    }
#endif

    DFloatingWidget::resizeEvent(event);
}

void ToolboxProxy::mouseMoveEvent(QMouseEvent *ev)
{
    setButtonTooltipHide();
    QWidget::mouseMoveEvent(ev);
}

void ToolboxProxy::updateTimeLabel()
{

#ifndef __sw_64__
    if (!utils::check_wayland_env()) {
        // to keep left and right of the same width. which makes play button centered
        _listBtn->setVisible(width() > 300);
        _timeLabel->setVisible(width() > 450);
        _timeLabelend->setVisible(width() > 450);
//    _viewProgBar->setVisible(width() > 350);
//    _progBar->setVisible(width() > 350);
        if (_mainWindow->width() < 1050) {
//        _progBar->hide();
        }
//    if (width() <= 300) {
//        _progBar->setFixedWidth(width() - PROGBAR_SPEC + 50 + 54 + 10 + 54 + 10 + 10);
//        _progBarspec->setFixedWidth(width() - PROGBAR_SPEC + 50 + 54 + 10 + 54 + 10 + 10);
//    } else if (width() <= 450) {
//        _progBar->setFixedWidth(width() - PROGBAR_SPEC + 54 + 54 + 10);
//        _progBarspec->setFixedWidth(width() - PROGBAR_SPEC + 54 + 54 + 10);
//    }

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

///not used function
/*void ToolboxProxy::setViewProgBarWidth()
{
    _viewProgBar->setWidth();
}*/

void ToolboxProxy::setPlaylist(PlaylistWidget *playlist)
{
    _playlist = playlist;
    WAYLAND_BLACK_WINDOW;
    connect(_playlist, &PlaylistWidget::stateChange, this, &ToolboxProxy::slotPlayListStateChange);

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

void ToolboxProxy::setVolSliderHide()
{
    _volSlider->setVisible(false);
}

void ToolboxProxy::setButtonTooltipHide()
{
    if (utils::check_wayland_env()) {
        m_playBtnTip->hide();
        m_prevBtnTip->hide();
        m_nextBtnTip->hide();
        m_fsBtnTip->hide();
        m_listBtnTip->hide();
    } else {
//        _subBtn->hideToolTip();
        _listBtn->hideToolTip();
        _fsBtn->hideToolTip();
    }
}

void ToolboxProxy::initToolTip()
{
    if (utils::check_wayland_env()) {
        //lmh0910播放
        m_playBtnTip = new ButtonToolTip(_mainWindow);
        m_playBtnTip->setText(tr("Play"));
        connect(static_cast<ButtonBoxButton *>(_playBtn), &ButtonBoxButton::entered, [ = ]() {
            m_playBtnTip->move(80, _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_playBtnTip->show();
            m_playBtnTip->QWidget::activateWindow();
            m_playBtnTip->update();
            m_playBtnTip->releaseMouse();

        });
        connect(static_cast<ButtonBoxButton *>(_playBtn), &ButtonBoxButton::leaved, [ = ]() {
            QTimer::singleShot(0, [ = ] {
                m_playBtnTip->hide();
            });
        });
        //lmh0910上一个
        m_prevBtnTip = new ButtonToolTip(_mainWindow);
        m_prevBtnTip->setText(tr("Previous"));
        connect(static_cast<ButtonBoxButton *>(_prevBtn), &ButtonBoxButton::entered, [ = ]() {
            m_prevBtnTip->move(40,
                               _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_prevBtnTip->show();
            m_prevBtnTip->QWidget::activateWindow();
            m_prevBtnTip->update();
            m_prevBtnTip->releaseMouse();

        });
        connect(static_cast<ButtonBoxButton *>(_prevBtn), &ButtonBoxButton::leaved, [ = ]() {
            QTimer::singleShot(0, [ = ] {
                m_prevBtnTip->hide();
            });
        });

        //lmh0910下一个
        m_nextBtnTip = new ButtonToolTip(_mainWindow);
        m_nextBtnTip->setText(tr("Next"));
        connect(static_cast<ButtonBoxButton *>(_nextBtn), &ButtonBoxButton::entered, [ = ]() {
            m_nextBtnTip->move(120,
                               _mainWindow->height() - TOOLBOX_HEIGHT - 5);
            m_nextBtnTip->show();
            m_nextBtnTip->QWidget::activateWindow();
            m_nextBtnTip->update();
            m_nextBtnTip->releaseMouse();

        });
        connect(static_cast<ButtonBoxButton *>(_nextBtn), &ButtonBoxButton::leaved, [ = ]() {
            QTimer::singleShot(0, [ = ] {
                m_nextBtnTip->hide();
            });
        });
    }
    //lmh0910全屏按键
    m_fsBtnTip = new ButtonToolTip(_mainWindow);
    m_fsBtnTip->setText(tr("Fullscreen"));
    connect(_fsBtn, &ToolButton::entered, [ = ]() {
        m_fsBtnTip->move(_mainWindow->width() - _fsBtn->width() / 2 /*- _playBtn->width()*/ - 140,
                         _mainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_fsBtnTip->show();
        m_fsBtnTip->QWidget::activateWindow();
        m_fsBtnTip->update();
        m_fsBtnTip->releaseMouse();

    });
    connect(_fsBtn, &ToolButton::leaved, [ = ]() {
        QTimer::singleShot(0, [ = ] {
            m_fsBtnTip->hide();
        });
    });
    //lmh0910list按键
    m_listBtnTip = new ButtonToolTip(_mainWindow);
    m_listBtnTip->setText(tr("Playlist"));
    connect(_listBtn, &ToolButton::entered, [ = ]() {
        m_listBtnTip->move(_mainWindow->width() - _listBtn->width() / 2 /*- _playBtn->width()*/ - 20,
                           _mainWindow->height() - TOOLBOX_HEIGHT - 5);
        m_listBtnTip->show();
        m_listBtnTip->QWidget::activateWindow();
        m_listBtnTip->update();
        m_listBtnTip->releaseMouse();

    });
    connect(_listBtn, &ToolButton::leaved, [ = ]() {
        QTimer::singleShot(0, [ = ] {
            m_listBtnTip->hide();
        });
    });
}

bool ToolboxProxy::getVolSliderIsHided()
{
    return _volSlider->isHidden();
}

void ToolboxProxy::updateProgress(int nValue)
{
    int nDuration = static_cast<int>(_engine->duration());

    if (_progBar_Widget->currentIndex() == 1) {              //进度条模式

        int nCurrPos = _progBar->value() + nValue * nDuration / _progBar->width();
        if (!_progBar->signalsBlocked()) {
            _progBar->blockSignals(true);
        }

        _progBar->slider()->setSliderPosition(nCurrPos);
        _progBar->slider()->setValue(nCurrPos);
    } else {
        _viewProgBar->setIsBlockSignals(true);
        _viewProgBar->setValue(_viewProgBar->getValue() + nValue);
    }
}

void ToolboxProxy::updateSlider()
{
    if (_progBar_Widget->currentIndex() == 1) {
        _engine->seekAbsolute(_progBar->value());

        _progBar->blockSignals(false);
    } else {
        _engine->seekAbsolute(_viewProgBar->getTimePos());
        _viewProgBar->setIsBlockSignals(false);
    }
}

void ToolboxProxy::initThumb()
{
    ThumbnailWorker::get().setPlayerEngine(_engine);
    connect(&ThumbnailWorker::get(), &ThumbnailWorker::thumbGenerated,
            this, &ToolboxProxy::updateHoverPreview);
}

void ToolboxProxy::updateSliderPoint(QPoint point)
{
    _volSlider->updatePoint(point);
}
}
#undef THEME_TYPE
#include "toolbox_proxy.moc"
