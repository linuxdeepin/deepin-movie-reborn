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

#include "mainwindow.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"
#include "compositing_manager.h"
#include "shortcut_manager.h"
#include "dmr_settings.h"
#include "utility.h"
#include "movieinfo_dialog.h"
#include "burst_screenshots_dialog.h"
#include "playlist_widget.h"
#include "notification_widget.h"
#include "player_engine.h"
#include "url_dialog.h"
#include "movie_progress_indicator.h"
#include "options.h"
#include "titlebar.h"
#include "utils.h"
#include "dvd_utils.h"
#include "dbus_adpator.h"
#include "threadpool.h"

//#include <QtWidgets>
#include <QtDBus>
#include <QtX11Extras>
#include <QX11Info>
#include <DLabel>
#include <DApplication>
#include <DTitlebar>
#include <DSettingsDialog>
#include <DThemeManager>
#include <DAboutDialog>
#include <DInputDialog>
#include <DImageButton>
#include <DWidgetUtil>
#include <DSettingsWidgetFactory>
#include <DLineEdit>
#include <DFileDialog>

#define AUTOHIDE_TIMEOUT 2000
#include <DToast>
DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 6

static void workaround_updateStyle(QWidget *parent, const QString &theme)
{
    parent->setStyle(QStyleFactory::create(theme));
    for (auto obj : parent->children()) {
        auto w = qobject_cast<QWidget *>(obj);
        if (w) {
            workaround_updateStyle(w, theme);
        }
    }
}

static QString ElideText(const QString &text, const QSize &size,
                         QTextOption::WrapMode wordWrap, const QFont &font,
                         Qt::TextElideMode mode, int lineHeight, int lastLineWidth)
{
    int height = 0;

    QTextLayout textLayout(text);
    QString str = nullptr;
    QFontMetrics fontMetrics(font);

    textLayout.setFont(font);
    const_cast<QTextOption *>(&textLayout.textOption())->setWrapMode(wordWrap);

    textLayout.beginLayout();

    QTextLine line = textLayout.createLine();

    while (line.isValid()) {
        height += lineHeight;

        if (height + lineHeight >= size.height()) {
            str += fontMetrics.elidedText(text.mid(line.textStart() + line.textLength() + 1),
                                          mode, lastLineWidth);

            break;
        }

        line.setLineWidth(size.width());

        const QString &tmp_str = text.mid(line.textStart(), line.textLength());

        if (tmp_str.indexOf('\n'))
            height += lineHeight;

        str += tmp_str;

        line = textLayout.createLine();

        if (line.isValid())
            str.append("\n");
    }

    textLayout.endLayout();

    if (textLayout.lineCount() == 1) {
        str = fontMetrics.elidedText(str, mode, lastLineWidth);
    }

    return str;
}

static QWidget *createSelectableLineEditOptionHandle(QObject *opt)
{
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(opt);

    auto le = new DLineEdit();
    auto main = new DWidget;
    auto layout = new QHBoxLayout;

    static QString nameLast = nullptr;

    main->setLayout(layout);
    DPushButton *icon = new DPushButton;
    icon->setAutoDefault(false);
    le->setFixedHeight(21);
    le->setObjectName("OptionSelectableLineEdit");
    le->setText(option->value().toString());
    auto fm = le->fontMetrics();
    auto pe = ElideText(le->text(), {285, fm.height()}, QTextOption::WrapAnywhere,
                        le->font(), Qt::ElideMiddle, fm.height(), 285);
    option->connect(le, &DLineEdit::focusChanged, [ = ](bool on) {
        if (on)
            le->setText(option->value().toString());

    });
    le->setText(pe);
    nameLast = pe;
//    icon->setIconVisible(true);
    icon->setIcon(QIcon(":resources/icons/select-normal.svg"));
    icon->setFixedHeight(21);
    layout->addWidget(le);
    layout->addWidget(icon);
//    icon->setNormalIcon(":resources/icons/select-normal.svg");
//    icon->setHoverIcon(":resources/icons/select-hover.svg");
//    icon->setPressIcon(":resources/icons/select-press.svg");

    auto optionWidget = DSettingsWidgetFactory::createTwoColumWidget(option, main);
    workaround_updateStyle(optionWidget, "light");

    DDialog *prompt = new DDialog(optionWidget);
    prompt->setIcon(QIcon(":/resources/icons/warning.svg"));
    //prompt->setTitle(QObject::tr("Permissions prompt"));
    prompt->setMessage(QObject::tr("You don't have permission to operate this folder"));
    prompt->setWindowFlags(prompt->windowFlags() | Qt::WindowStaysOnTopHint);
    prompt->addButton(QObject::tr("OK"), true, DDialog::ButtonRecommend);

    auto validate = [ = ](QString name, bool alert = true) -> bool {
        name = name.trimmed();
        if (name.isEmpty()) return false;

        if (name.size() && name[0] == '~')
        {
            name.replace(0, 1, QDir::homePath());
        }

        QFileInfo fi(name);
        QDir dir(name);
        if (fi.exists())
        {
            if (!fi.isDir()) {
                if (alert) le->showAlertMessage(QObject::tr("Invalid folder"));
                return false;
            }

            if (!fi.isReadable() || !fi.isWritable()) {
//                if (alert) le->showAlertMessage(QObject::tr("You don't have permission to operate this folder"));
                return false;
            }
        } else
        {
            if (dir.cdUp()) {
                QFileInfo ch(dir.path());
                if (!ch.isReadable() || !ch.isWritable())
                    return false;
            }
        }

        return true;
    };

    option->connect(icon, &DPushButton::clicked, [ = ]() {
        QString name = DFileDialog::getExistingDirectory(0, QObject::tr("Open folder"),
                                                         MainWindow::lastOpenedPath(),
                                                         DFileDialog::ShowDirsOnly | DFileDialog::DontResolveSymlinks);
        if (validate(name, false)) {
            option->setValue(name);
            nameLast = name;
        }
        QFileInfo fm(name);
        if ((!fm.isReadable() || !fm.isWritable()) && !name.isEmpty()) {
            prompt->show();
        }
    });



    option->connect(le, &DLineEdit::editingFinished, option, [ = ]() {

        QString name = le->text();
        QDir dir(name);

        auto pn = ElideText(name, {285, fm.height()}, QTextOption::WrapAnywhere,
                            le->font(), Qt::ElideMiddle, fm.height(), 285);
        auto nmls = ElideText(nameLast, {285, fm.height()}, QTextOption::WrapAnywhere,
                              le->font(), Qt::ElideMiddle, fm.height(), 285);

        if (!validate(le->text(), false)) {
            QFileInfo fn(dir.path());
            if ((!fn.isReadable() || !fn.isWritable()) && !name.isEmpty()) {
                prompt->show();
            }
        }
        if (!le->lineEdit()->hasFocus()) {
            if (validate(le->text(), false)) {
                option->setValue(le->text());
                le->setText(pn);
                nameLast = name;
            } else if (pn == pe) {
                le->setText(pe);
            } else {
//                option->setValue(option->defaultValue());//设置为默认路径
//                le->setText(option->defaultValue().toString());
                option->setValue(nameLast);
                le->setText(nmls);
            }
        }
    });

    option->connect(le, &DLineEdit::textEdited, option, [ = ](const QString & newStr) {
        validate(newStr);
    });

    option->connect(option, &DTK_CORE_NAMESPACE::DSettingsOption::valueChanged, le,
    [ = ](const QVariant & value) {
        auto pi = ElideText(value.toString(), {285, fm.height()}, QTextOption::WrapAnywhere,
                            le->font(), Qt::ElideMiddle, fm.height(), 285);
        le->setText(pi);
        le->update();
    });

    return  optionWidget;
}

class MainWindowFocusMonitor: public QAbstractNativeEventFilter
{
public:
    MainWindowFocusMonitor(MainWindow *src) : QAbstractNativeEventFilter(), _source(src)
    {
        qApp->installNativeEventFilter(this);
    }

    ~MainWindowFocusMonitor()
    {
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *)
    {
        if (Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
            case XCB_LEAVE_NOTIFY: {
                xcb_leave_notify_event_t *dne = (xcb_leave_notify_event_t *)event;
                auto w = _source->windowHandle();
                if (dne->event == w->winId()) {
                    qDebug() << "---------  leave " << dne->event << dne->child;
                    emit _source->windowLeaved();
                }
                break;
            }

            case XCB_ENTER_NOTIFY: {
                xcb_enter_notify_event_t *dne = (xcb_enter_notify_event_t *)event;
                auto w = _source->windowHandle();
                if (dne->event == w->winId()) {
                    qDebug() << "---------  enter " << dne->event << dne->child;
                    emit _source->windowEntered();
                }
                break;
            }
            default:
                break;
            }
        }

        return false;
    }

    MainWindow *_source;
};

class MainWindowPropertyMonitor: public QAbstractNativeEventFilter
{
public:
    MainWindowPropertyMonitor(MainWindow *src)
        : QAbstractNativeEventFilter(), _mw(src), _source(src->windowHandle())
    {
        qApp->installNativeEventFilter(this);

        _atomWMState = Utility::internAtom("_NET_WM_STATE");
    }

    ~MainWindowPropertyMonitor()
    {
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *)
    {
        if (Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
            case XCB_PROPERTY_NOTIFY: {
                xcb_property_notify_event_t *pne = (xcb_property_notify_event_t *)(event);
                if (pne->atom == _atomWMState && pne->window == (xcb_window_t)_source->winId()) {
                    _mw->syncStaysOnTop();
                }
                break;
            }

            default:
                break;
            }
        }

        return false;
    }

    MainWindow *_mw {nullptr};
    QWindow *_source {nullptr};
    xcb_atom_t _atomWMState;
};


class MainWindowEventListener : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowEventListener(QWidget *target)
        : QObject(target), _window(target->windowHandle())
    {
    }

    ~MainWindowEventListener()
    {
    }

    void setEnabled(bool v)
    {
        enabled = v;
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE {
        QWindow *window = qobject_cast<QWindow *>(obj);
        if (!window) return false;

        switch ((int)event->type())
        {
        case QEvent::MouseButtonPress: {
            if (!enabled) return false;
            QMouseEvent *e = static_cast<QMouseEvent *>(event);
            setLeftButtonPressed(true);
            auto mw = static_cast<MainWindow *>(parent());
            if (mw->insideResizeArea(e->globalPos()) && lastCornerEdge != Utility::NoneEdge)
                startResizing = true;

            mw->capturedMousePressEvent(e);
            if (startResizing) {
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (!enabled) return false;
            QMouseEvent *e = static_cast<QMouseEvent *>(event);
            setLeftButtonPressed(false);
            qApp->setOverrideCursor(window->cursor());

            auto mw = static_cast<MainWindow *>(parent());
            mw->capturedMouseReleaseEvent(e);
            if (startResizing) {
                startResizing = false;
                return true;
            }
            startResizing = false;
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent *e = static_cast<QMouseEvent *>(event);
            auto mw = static_cast<MainWindow *>(parent());
            mw->resumeToolsWindow();

            //If window is maximized ,need quit maximize state when resizing
            if (startResizing && (mw->windowState() & Qt::WindowMaximized)) {
                mw->setWindowState(mw->windowState() & (~Qt::WindowMaximized));
            } else if (startResizing && (mw->windowState() & Qt::WindowFullScreen)) {
                mw->setWindowState(mw->windowState() & (~Qt::WindowFullScreen));
            }

            if (!enabled) return false;
            const QRect window_visible_rect = _window->frameGeometry() - mw->dragMargins();

            if (!leftButtonPressed) {
                if (mw->insideResizeArea(e->globalPos())) {
                    Utility::CornerEdge mouseCorner = Utility::NoneEdge;
                    QRect cornerRect;

                    /// begin set cursor corner type
                    cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                    cornerRect.moveTopLeft(_window->frameGeometry().topLeft());
                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::TopLeftCorner;
                        goto set_cursor;
                    }

                    cornerRect.moveTopRight(_window->frameGeometry().topRight());
                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::TopRightCorner;
                        goto set_cursor;
                    }

                    cornerRect.moveBottomRight(_window->frameGeometry().bottomRight());
                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::BottomRightCorner;
                        goto set_cursor;
                    }

                    cornerRect.moveBottomLeft(_window->frameGeometry().bottomLeft());
                    if (cornerRect.contains(e->globalPos())) {
                        mouseCorner = Utility::BottomLeftCorner;
                        goto set_cursor;
                    }

                    goto skip_set_cursor; // disable edges

                    /// begin set cursor edge type
                    if (e->globalX() <= window_visible_rect.x()) {
                        mouseCorner = Utility::LeftEdge;
                    } else if (e->globalX() < window_visible_rect.right()) {
                        if (e->globalY() <= window_visible_rect.y()) {
                            mouseCorner = Utility::TopEdge;
                        } else if (e->globalY() >= window_visible_rect.bottom()) {
                            mouseCorner = Utility::BottomEdge;
                        } else {
                            goto skip_set_cursor;
                        }
                    } else if (e->globalX() >= window_visible_rect.right()) {
                        mouseCorner = Utility::RightEdge;
                    } else {
                        goto skip_set_cursor;
                    }
set_cursor:
                    if (window->property("_d_real_winId").isValid()) {
                        auto real_wid = window->property("_d_real_winId").toUInt();
                        Utility::setWindowCursor(real_wid, mouseCorner);
                    } else {
                        Utility::setWindowCursor(window->winId(), mouseCorner);
                    }

                    if (qApp->mouseButtons() == Qt::LeftButton) {
                        updateGeometry(mouseCorner, e);
                    }
                    lastCornerEdge = mouseCorner;
                    return true;

skip_set_cursor:
                    lastCornerEdge = mouseCorner = Utility::NoneEdge;
                    return false;
                } else {
                    qApp->setOverrideCursor(window->cursor());
                }
            } else {
                if (startResizing) {
                    updateGeometry(lastCornerEdge, e);
#ifdef __aarch64__
                    mw->syncPostion();
#elif  __mips__
                    mw->syncPostion();
#endif
                    return true;
                }
            }

            break;
        }

        default:
            break;
        }

        return false;
    }

private:
    void setLeftButtonPressed(bool pressed)
    {
        if (leftButtonPressed == pressed)
            return;

        if (!pressed)
            Utility::cancelWindowMoveResize(_window->winId());

        leftButtonPressed = pressed;
    }

    void updateGeometry(Utility::CornerEdge edge, QMouseEvent *e)
    {
        auto mw = static_cast<MainWindow *>(parent());
        bool keep_ratio = mw->engine()->state() != PlayerEngine::CoreState::Idle;
        auto old_geom = mw->frameGeometry();
        auto geom = mw->frameGeometry();
        qreal ratio = (qreal)geom.width() / geom.height();

        // disable edges
        switch (edge) {
        case Utility::BottomEdge:
        case Utility::TopEdge:
        case Utility::LeftEdge:
        case Utility::RightEdge:
        case Utility::NoneEdge:
            return;
        default:
            break;
        }

        if (keep_ratio) {
            auto sz = mw->engine()->videoSize();
            if (sz.isEmpty()) {
                const auto &mi = mw->engine()->playlist().currentInfo().mi;
                sz = QSize(mi.width, mi.height);
            }

            ratio = sz.width() / (qreal)sz.height();
            switch (edge) {
            case Utility::TopLeftCorner:
                geom.setLeft(e->globalX());
                geom.setTop(geom.bottom() - geom.width() / ratio);
                break;
            case Utility::BottomLeftCorner:
            case Utility::LeftEdge:
                geom.setLeft(e->globalX());
                geom.setHeight(geom.width() / ratio);
                break;
            case Utility::BottomRightCorner:
            case Utility::RightEdge:
                geom.setRight(e->globalX());
                geom.setHeight(geom.width() / ratio);
                break;
            case Utility::TopRightCorner:
            case Utility::TopEdge:
                geom.setTop(e->globalY());
                geom.setWidth(geom.height() * ratio);
                break;
            case Utility::BottomEdge:
                geom.setBottom(e->globalY());
                geom.setWidth(geom.height() * ratio);
                break;
            default:
                break;
            }
        } else {
            switch (edge) {
            case Utility::BottomLeftCorner:
                geom.setBottomLeft(e->globalPos());
                break;
            case Utility::TopLeftCorner:
                geom.setTopLeft(e->globalPos());
                break;
            case Utility::LeftEdge:
                geom.setLeft(e->globalX());
                break;
            case Utility::BottomRightCorner:
                geom.setBottomRight(e->globalPos());
                break;
            case Utility::RightEdge:
                geom.setRight(e->globalX());
                break;
            case Utility::TopRightCorner:
                geom.setTopRight(e->globalPos());
                break;
            case Utility::TopEdge:
                geom.setTop(e->globalY());
                break;
            case Utility::BottomEdge:
                geom.setBottom(e->globalY());
                break;
            default:
                break;
            }
        }

        auto min = mw->minimumSize();
        if (old_geom.width() <= min.width() && geom.left() > old_geom.left()) {
            geom.setLeft(old_geom.left());
        }
        if (old_geom.height() <= min.height() && geom.top() > old_geom.top()) {
            geom.setTop(old_geom.top());
        }

        geom.setWidth(qMax(geom.width(), min.width()));
        geom.setHeight(qMax(geom.height(), min.height()));
        mw->updateContentGeometry(geom);
        mw->updateGeometryNotification(geom.size());
    }

    bool leftButtonPressed {false};
    bool startResizing {false};
    bool enabled {true};
    Utility::CornerEdge lastCornerEdge;
    QWindow *_window;
};

#ifdef USE_DXCB
/// shadow
#define SHADOW_COLOR_NORMAL QColor(0, 0, 0, 255 * 0.35)
#define SHADOW_COLOR_ACTIVE QColor(0, 0, 0, 255 * 0.6)
#endif

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(NULL)
{
    m_lastVolume = Settings::get().internalOption("last_volume").toInt();;
    bool composited = CompositingManager::get().composited();
#ifdef USE_DXCB
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#else
//    setWindowFlags(Qt::FramelessWindowHint);
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MACOS
    setWindowFlags(Qt::WindowFullscreenButtonHint);
#endif
#endif
    setAcceptDrops(true);

    if (composited) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        //setAttribute(Qt::WA_NoSystemBackground, false);
    }

//    DThemeManager::instance()->registerWidget(this);
//    setFrameShape(QFrame::NoFrame);

#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
        //setAttribute(Qt::WA_TranslucentBackground, true);
        //if (composited)
        //_handle->setTranslucentBackground(true);_miniPlayBtn
        _handle->setEnableSystemResize(false);
        _handle->setEnableSystemMove(false);
        _handle->setWindowRadius(4);
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &MainWindow::updateShadow);
        updateShadow();
    }
#else
    winId();
#endif

    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);

    qDebug() << "composited = " << composited;

    setContentsMargins(0, 0, 0, 0);

    setupTitlebar();

    auto &clm = dmr::CommandLineManager::get();
    if (clm.debug()) {
        Backend::setDebugLevel(Backend::DebugLevel::Debug);
    } else if (clm.verbose()) {
        Backend::setDebugLevel(Backend::DebugLevel::Verbose);
    }
    _engine = new PlayerEngine(this);
#ifndef USE_DXCB
    _engine->move(0, 0);
#endif

    int volume = Settings::get().internalOption("global_volume").toInt();
    /*if (VOLUME_OFFSET == volume) {
        volume = 0;
    }*/
    _engine->changeVolume(volume);
    if (Settings::get().internalOption("mute").toBool()) {
        _engine->toggleMute();
        Settings::get().setInternalOption("mute", _engine->muted());
    }

    _toolbox = new ToolboxProxy(this, _engine);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    _playlist = new PlaylistWidget(this, _engine);
    _playlist->hide();
//    _playlist->setParent(_toolbox);

    _toolbox->setPlaylist(_playlist);

    connect(_engine, &PlayerEngine::stateChanged, [ = ]() {
        setInit(_engine->state() != PlayerEngine::Idle);
        resumeToolsWindow();
        updateWindowTitle();

        // delayed checking if engine is still idle, in case other videos are schedulered (next/prev req)
        // and another resize event will happen after that
        QTimer::singleShot(100, [ = ]() {
            if (_engine->state() == PlayerEngine::Idle && !_miniMode && windowState() == Qt::WindowNoState) {
                this->setMinimumSize(QSize(614, 500));
                this->resize(850, 600);
            }
        });
    });

    connect(ActionFactory::get().mainContextMenu(), &DMenu::triggered,
            this, &MainWindow::menuItemInvoked);
    connect(this, &MainWindow::frameMenuEnable,
            &ActionFactory::get(), &ActionFactory::frameMenuEnable);
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::triggered,
            this, &MainWindow::menuItemInvoked);
    connect(qApp, &QGuiApplication::focusWindowChanged, [ = ]() {
        if (qApp->focusWindow() != windowHandle())
            suspendToolsWindow();
        else
            resumeToolsWindow();
    });

    /*_playState = new DIconButton(this);
    //    _playState->setScaledContents(true);
    _playState->setIcon(QIcon(":/resources/icons/dark/normal/play-big_normal.svg"));
    _playState->setIconSize(QSize(128, 128));
    _playState->setObjectName("PlayState");
    _playState->setFixedSize(128, 128);
    DPalette pa_cb = DApplicationHelper::instance()->palette(_playState);
    pa_cb.setBrush(QPalette::Light, QColor(0, 0, 0, 0));
    pa_cb.setBrush(QPalette::Dark, QColor(0, 0, 0, 0));
    _playState->setPalette(pa_cb);
    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ) {
        _playState->setIcon(QIcon(":/resources/icons/light/normal/play-big_normal.svg"));
    }
    _playState->setVisible(false);
    connect(_playState, &DIconButton::clicked, [ = ]() {
        requestAction(ActionFactory::TogglePause, false, {}, true);
    });
    QObject::connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::paletteTypeChanged, _playState,
    [ = ] (DGuiApplicationHelper::ColorType type) {

        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType() ) {
            _playState->setIcon(QIcon(":/resources/icons/light/normal/play-big_normal.svg"));
        } else {
            _playState->setIcon(QIcon(":/resources/icons/dark/normal/play-big_normal.svg"));
        }
    });*/

    _progIndicator = new MovieProgressIndicator(this);
    _progIndicator->setVisible(false);
    connect(_engine, &PlayerEngine::elapsedChanged, [ = ]() {
        _progIndicator->updateMovieProgress(_engine->duration(), _engine->elapsed());
    });

    // mini ui
    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper,
            static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped),
            this, &MainWindow::miniButtonClicked);

#ifdef __mips__
    _miniPlayBtn = new IconButton(this);
    _miniCloseBtn = new IconButton(this);
    _miniQuitMiniBtn = new IconButton(this);

    dynamic_cast<IconButton *>(_miniPlayBtn)->setFlat(true);
    dynamic_cast<IconButton *>(_miniCloseBtn)->setFlat(true);
    dynamic_cast<IconButton *>(_miniQuitMiniBtn)->setFlat(true);
#else
    _miniPlayBtn = new DIconButton(this);
    _miniCloseBtn = new DIconButton(this);
    _miniQuitMiniBtn = new DIconButton(this);

    _miniPlayBtn->setFlat(true);
    _miniCloseBtn->setFlat(true);
    _miniQuitMiniBtn->setFlat(true);
#endif
    _miniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
    _miniPlayBtn->setIconSize(QSize(30, 30));
    _miniPlayBtn->setFixedSize(QSize(30, 30));
    _miniPlayBtn->setObjectName("MiniPlayBtn");
    connect(_miniPlayBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniPlayBtn, "play");

    connect(_engine, &PlayerEngine::stateChanged, [ = ]() {
        qDebug() << __func__ << _engine->state();
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            emit frameMenuEnable(false);
        }
        if (_engine->state() == PlayerEngine::CoreState::Playing) {
            _miniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/pause-normal-mini.svg"));
            _miniPlayBtn->setObjectName("MiniPauseBtn");

            emit frameMenuEnable(true);
            if (_lastCookie > 0) {
                utils::UnInhibitStandby(_lastCookie);
                qDebug() << "uninhibit cookie" << _lastCookie;
                _lastCookie = 0;
            }
            if (_powerCookie > 0) {
                utils::UnInhibitPower(_powerCookie);
                _powerCookie = 0;
            }
            _lastCookie = utils::InhibitStandby();
            _powerCookie = utils::InhibitPower();
        } else {
            _miniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
            _miniPlayBtn->setObjectName("MiniPlayBtn");

            if (_lastCookie > 0) {
                utils::UnInhibitStandby(_lastCookie);
                qDebug() << "uninhibit cookie" << _lastCookie;
                _lastCookie = 0;
            }
            if (_powerCookie > 0) {
                utils::UnInhibitPower(_powerCookie);
                _powerCookie = 0;
            }
        }
//        _miniPlayBtn->setStyleSheet(_miniPlayBtn->styleSheet());
    });

    _miniCloseBtn->setIcon(QIcon(":/resources/icons/light/mini/close-normal.svg"));
    _miniCloseBtn->setIconSize(QSize(30, 30));
    _miniCloseBtn->setFixedSize(QSize(30, 30));
    _miniCloseBtn->setObjectName("MiniCloseBtn");
    connect(_miniCloseBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniCloseBtn, "close");

    _miniQuitMiniBtn->setIcon(QIcon(":/resources/icons/light/mini/restore-normal-mini.svg"));
    _miniQuitMiniBtn->setIconSize(QSize(30, 30));
    _miniQuitMiniBtn->setFixedSize(QSize(30, 30));
    _miniQuitMiniBtn->setObjectName("MiniQuitMiniBtn");
    connect(_miniQuitMiniBtn, SIGNAL(clicked()), signalMapper, SLOT(map()));
    signalMapper->setMapping(_miniQuitMiniBtn, "quit_mini");

    _miniPlayBtn->setVisible(_miniMode);
    _miniCloseBtn->setVisible(_miniMode);
    _miniQuitMiniBtn->setVisible(_miniMode);
    if (!composited) {
        _miniPlayBtn->setAttribute(Qt::WA_NativeWindow);
        _miniCloseBtn->setAttribute(Qt::WA_NativeWindow);
        _miniQuitMiniBtn->setAttribute(Qt::WA_NativeWindow);
    }
    // ~

    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();

    connect(_engine, &PlayerEngine::tracksChanged, this, &MainWindow::updateActionsState);
    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::updateActionsState);
    updateActionsState();

    reflectActionToUI(ActionFactory::DefaultFrame);
    //reflectActionToUI(ActionFactory::OrderPlay);
    reflectActionToUI(ActionFactory::Stereo);
    requestAction(ActionFactory::ChangeSubCodepage, false, {"auto"});

    _lightTheme = Settings::get().internalOption("light_theme").toBool();
    if (_lightTheme) reflectActionToUI(ActionFactory::LightTheme);
    prepareSplashImages();

    connect(_engine, &PlayerEngine::sidChanged, [ = ]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    });
    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    connect(_engine, &PlayerEngine::aidChanged, [ = ]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    });
    connect(_engine, &PlayerEngine::subCodepageChanged, [ = ]() {
        reflectActionToUI(ActionFactory::ActionKind::ChangeSubCodepage);
    });

    connect(_engine, &PlayerEngine::fileLoaded, [ = ]() {
        if (windowState() == Qt::WindowNoState && _lastRectInNormalMode.isValid()) {
            const auto &mi = _engine->playlist().currentInfo().mi;
            _lastRectInNormalMode.setSize({mi.width, mi.height});
        }
        this->resizeByConstraints();
        QDesktopWidget desktop;
        if (desktop.screenCount() > 1) {
            if (!isFullScreen() && !isMaximized() && !_miniMode) {
                auto geom = qApp->desktop()->availableGeometry(this);
                move((geom.width() - this->width()) / 2, (geom.height() - this->height()) / 2);
            }
        } else {
            utils::MoveToCenter(this);
        }

        m_IsFree = true;
    });
    connect(_engine, &PlayerEngine::videoSizeChanged, [ = ]() {
        this->resizeByConstraints();
    });

    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::animatePlayState);
    syncPlayState();

    connect(_engine, &PlayerEngine::loadOnlineSubtitlesFinished,
    [this](const QUrl & url, bool success) {
        _nwComm->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
    });

    connect(&_autoHideTimer, &QTimer::timeout, this, &MainWindow::suspendToolsWindow);
    _autoHideTimer.setSingleShot(true);

    connect(&_delayedMouseReleaseTimer, &QTimer::timeout, this, &MainWindow::delayedMouseReleaseHandler);
    _delayedMouseReleaseTimer.setSingleShot(true);

    _nwComm = new NotificationWidget(this);
    _nwComm->setFixedHeight(30);
    _nwComm->setAnchor(NotificationWidget::AnchorNorthWest);
    _nwComm->setAnchorPoint(QPoint(30, 58));
    _nwComm->hide();
    _nwDvd = new NotificationWidget(this);
    _nwDvd->setFixedHeight(30);
    _nwDvd->setAnchor(NotificationWidget::AnchorNorthWest);
    _nwDvd->setAnchorPoint(QPoint(30, 58));
    _nwDvd->hide();

#ifdef USE_DXCB
    if (!composited) {
        connect(qApp, &QGuiApplication::applicationStateChanged,
                this, &MainWindow::onApplicationStateChanged);

        _evm = new EventMonitor(this);
        connect(_evm, &EventMonitor::buttonedPress, this, &MainWindow::onMonitorButtonPressed);
        connect(_evm, &EventMonitor::buttonedDrag, this, &MainWindow::onMonitorMotionNotify);
        connect(_evm, &EventMonitor::buttonedRelease, this, &MainWindow::onMonitorButtonReleased);
        _evm->start();
    }

    _listener = new MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(_listener);

    //auto mwfm = new MainWindowFocusMonitor(this);
    auto mwpm = new MainWindowPropertyMonitor(this);

    connect(this, &MainWindow::windowEntered, &MainWindow::resumeToolsWindow);
    connect(this, &MainWindow::windowLeaved, &MainWindow::suspendToolsWindow);

#else
    _listener = new MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(_listener);

    //auto mwfm = new MainWindowFocusMonitor(this);
    //auto mwpm = new MainWindowPropertyMonitor(this);

    connect(this, &MainWindow::windowEntered, &MainWindow::resumeToolsWindow);
    connect(this, &MainWindow::windowLeaved, &MainWindow::suspendToolsWindow);

    if (!composited) {
        if (_engine->windowHandle())
            _engine->windowHandle()->installEventFilter(_listener);
        _titlebar->windowHandle()->installEventFilter(_listener);
        _toolbox->windowHandle()->installEventFilter(_listener);
    }
    qDebug() << "event listener";
#endif

    _fullscreentimelable = new QLabel;
    _fullscreentimelable->setAttribute(Qt::WA_TranslucentBackground);
    _fullscreentimelable->setWindowFlags(Qt::FramelessWindowHint);
    _fullscreentimelable->setParent(this);
//    _fullscreentimelable->setWindowFlags(_fullscreentimelable->windowFlags()|Qt::Dialog);
    _fullscreentimebox = new QHBoxLayout;
    _fullscreentimebox->addStretch();
    _fullscreentimebox->addWidget(_toolbox->getfullscreentimeLabel());
    _fullscreentimebox->addWidget(_toolbox->getfullscreentimeLabelend());
    _fullscreentimebox->addStretch();
    _fullscreentimelable->setLayout(_fullscreentimebox);
    _fullscreentimelable->close();

    _animationlable = new AnimationLabel;
    _animationlable->setAttribute(Qt::WA_TranslucentBackground);
    _animationlable->setWindowFlags(Qt::FramelessWindowHint);
    _animationlable->setParent(this);
    _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);

    popup = new DFloatingMessage(DFloatingMessage::TransientType, this);
    popup->resize(0, 0);
//    popup->hide(); //This causes the first screenshot icon to move down

    defaultplaymodeinit();
    connect(&Settings::get(), &Settings::defaultplaymodechanged, this, &MainWindow::slotdefaultplaymodechanged);

    connect(this, &MainWindow::playlistchanged, _toolbox, &ToolboxProxy::updateplaylisticon);

    connect(_engine, &PlayerEngine::onlineStateChanged, this, &MainWindow::checkOnlineState);
    connect(&OnlineSubtitle::get(), &OnlineSubtitle::onlineSubtitleStateChanged, this, &MainWindow::checkOnlineSubtitle);
    connect(_engine, &PlayerEngine::mpvErrorLogsChanged, this, &MainWindow::checkErrorMpvLogsChanged);
    connect(_engine, &PlayerEngine::mpvWarningLogsChanged, this, &MainWindow::checkWarningMpvLogsChanged);
    connect(_engine, &PlayerEngine::urlpause, this, [ = ](bool status) {
        if (status) {
            auto msg = QString(tr("Buffering..."));
            _nwComm->updateWithMessage(msg);
        }

    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance, this, [ = ] {
        this->activateWindow();
    });

    connect(qApp, &QGuiApplication::fontChanged, this, [ = ](const QFont & font) {
        QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
        _toolbox->getfullscreentimeLabel()->setMinimumWidth(fm.width(_toolbox->getfullscreentimeLabel()->text()));
        _toolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.width(_toolbox->getfullscreentimeLabelend()->text()));

        int pixelsWidth = _toolbox->getfullscreentimeLabel()->width() + _toolbox->getfullscreentimeLabelend()->width();
        QRect deskRect = QApplication::desktop()->availableGeometry();
        _fullscreentimelable->setGeometry(deskRect.width() - pixelsWidth - 32, 40, pixelsWidth + 32, 36);
    });

    connect(dmr::dvd::RetrieveDvdThread::get(), &dmr::dvd::RetrieveDvdThread::sigData, this, &MainWindow::onDvdData);

    {
        loadWindowState();
    }

    /*QString playlistFile = QString("%1/%2/%3/playlist")
                           .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                           .arg(qApp->organizationName())
                           .arg(qApp->applicationName());
    QSettings cfg(playlistFile, QSettings::NativeFormat);
    cfg.beginGroup("playlist");
    auto keys = cfg.childKeys();
    if (Settings::get().isSet(Settings::ResumeFromLast) && keys.size()) {
        _delayedMouseReleaseTimer.start(1000);
    }*/

    //****************************************
    ThreadPool::instance()->moveToNewThread(&volumeMonitoring);
    volumeMonitoring.start();
    connect(&volumeMonitoring, &VolumeMonitoring::volumeChanged, this, [ = ](int vol) {
        changedVolumeSlot(vol);
        //_engine->changeVolume(vol);
        //requestAction(ActionFactory::ChangeVolume);
    });

    connect(&volumeMonitoring, &VolumeMonitoring::muteChanged, this, [ = ](bool mute) {
        changedMute(mute);
    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &MainWindow::updateMiniBtnTheme);
}

void MainWindow::setupTitlebar()
{
    _titlebar = new Titlebar(this);
#ifdef USE_DXCB
    _titlebar->move(0, 0);
#else
    _titlebar->move(0, 0);
#endif
    _titlebar->setFixedHeight(50);
    setTitlebarShadowEnabled(false);
    if (!CompositingManager::get().composited()) {
        _titlebar->setAttribute(Qt::WA_NativeWindow);
        _titlebar->winId();
    }
    _titlebar->titlebar()->setMenu(ActionFactory::get().titlebarMenu());
    connect(_titlebar->titlebar()->menu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);

}

void MainWindow::updateContentGeometry(const QRect &rect)
{
#ifdef USE_DXCB
    auto frame = QWindow::fromWinId(windowHandle()->winId());

    QRect frame_rect = rect;
    if (_handle) {
        frame_rect += _handle->frameMargins();
    }

    const uint32_t values[] = { (uint32_t)frame_rect.x(), (uint32_t)frame_rect.y(),
                                (uint32_t)frame_rect.width(), (uint32_t)frame_rect.height()
                              };
    // manually configure frame window which will in turn update content window
    xcb_configure_window(QX11Info::connection(),
                         windowHandle()->winId(),
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
                         XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_X,
                         values);

#else
//    setGeometry(rect);
    move(rect.x(), rect.y());
    resize(rect.width(), rect.height());
#endif
}

#ifdef USE_DXCB
void MainWindow::updateShadow()
{
    if (isActiveWindow()) {
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_ACTIVE);
    } else {
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_NORMAL);
    }
}
#endif

bool MainWindow::event(QEvent *ev)
{
    if (ev->type() == QEvent::WindowStateChange) {
        auto wse = dynamic_cast<QWindowStateChangeEvent *>(ev);
        _lastWindowState = wse->oldState();
        qDebug() << "------------ _lastWindowState" << _lastWindowState
                 << "current " << windowState();
        //NOTE: windowStateChanged won't be emitted if by draggint to restore. so we need to
        //check window state here.
        //connect(windowHandle(), &QWindow::windowStateChanged, this, &MainWindow::onWindowStateChanged);
        onWindowStateChanged();
    }
    return DMainWindow::event(ev);
}

void MainWindow::leaveEvent(QEvent *)
{
    _autoHideTimer.stop();
    this->suspendToolsWindow();
}

void MainWindow::onWindowStateChanged()
{
    qDebug() << windowState();

    if (!_miniMode && !isFullScreen()) {
        _titlebar->setVisible(_toolbox->isVisible());
    } else {
        _titlebar->setVisible(false);
        auto e = QProcessEnvironment::systemEnvironment();
        QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
        QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

        if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
                WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            if (_miniMode) {
                this->toggleUIMode();
                this->setWindowState(Qt::WindowMaximized);      //mini model need
            }
        }

    }
    //WTF: this->geometry() is not size of fullscreen !
    //_progIndicator->move(geometry().width() - _progIndicator->width() - 18, 14);
    _progIndicator->setVisible(isFullScreen());
    toggleShapeMask();

#ifndef USE_DXCB
    if (isFullScreen()) {
        _titlebar->move(0, 0);
        _engine->move(0, 0);
    } else {
        _titlebar->move(0, 0);
        _engine->move(0, 0);
    }
#endif

    if (!isFullScreen() && !isMaximized()) {
        if (_movieSwitchedInFsOrMaxed || !_lastRectInNormalMode.isValid()) {
            if (_mousePressed || _mouseMoved) {
                _delayedResizeByConstraint = true;
            } else {
                setMinimumSize({0, 0});
                resizeByConstraints(true);
            }
        }
        _movieSwitchedInFsOrMaxed = false;
    }
    update();

    if (!isMaximized() && !isFullScreen() && !_miniMode) {
        if (_maxfornormalflag) {
            setWindowState(windowState() & ~Qt::WindowFullScreen);
            if (_lastRectInNormalMode.isValid() && !_miniMode && !isMaximized()) {
                setGeometry(_lastRectInNormalMode);
                move(_lastRectInNormalMode.x(), _lastRectInNormalMode.y());
                resize(_lastRectInNormalMode.width(), _lastRectInNormalMode.height());
            }
            _maxfornormalflag = false;
        } else {
            _maxfornormalflag = false;
        }
    }

    if (isMinimized()) {
        if (_playlist->state() == PlaylistWidget::Opened) {
            _playlist->togglePopup();
            emit playlistchanged();
        }
    }
    if (isMaximized()) {
        _animationlable->move(QPoint(QApplication::desktop()->availableGeometry().width() / 2 - 100
                                     , QApplication::desktop()->availableGeometry().height() / 2 - 100));
    }
    if (!isFullScreen() && !isMaximized() && !_miniMode) {
        _animationlable->move(QPoint((_lastRectInNormalMode.width() - _animationlable->width()) / 2,
                                     (_lastRectInNormalMode.height() - _animationlable->height()) / 2));
    }
}

void MainWindow::handleHelpAction()
{
    class _DApplication : public DApplication
    {
    public:
        inline void showHelp()
        {
            DApplication::handleHelpAction();
        }
    };

    DApplication *dapp = qApp;
    reinterpret_cast<_DApplication *>(dapp)->_DApplication::showHelp();
}

void MainWindow::changedVolume(int vol)
{
    _engine->changeVolume(vol);
    Settings::get().setInternalOption("global_volume", vol);
    if (vol != 0)
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(vol));
    else
        _nwComm->updateWithMessage(tr("Mute"));
}

void MainWindow::changedVolumeSlot(int vol)
{
    if (_engine->muted()) {
        _engine->toggleMute();
        Settings::get().setInternalOption("mute", _engine->muted());
    }
    if (_engine->volume() <= 100 || vol < 100) {
        _engine->changeVolume(vol);
        Settings::get().setInternalOption("global_volume", vol);
#ifndef __aarch64__
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(vol));
#endif
    }
}

void MainWindow::changedMute()
{
    bool mute = _engine->muted();
    _engine->toggleMute();
    Settings::get().setInternalOption("mute", _engine->muted());
}

void MainWindow::changedMute(bool mute)
{
    bool oldMute = Settings::get().internalOption("mute").toBool();
    if (oldMute == mute) {
        return;
    }
    _engine->toggleMute();
    Settings::get().setInternalOption("mute", mute);
    if (mute)
        _nwComm->updateWithMessage(tr("Mute"));
    else {
        _engine->changeVolume(m_lastVolume);
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(_engine->volume()));
    }
}

#ifdef USE_DXCB
static QPoint last_engine_pos;
static QPoint last_wm_pos;
static bool clicked = false;
void MainWindow::onMonitorButtonPressed(int x, int y)
{
    QPoint p(x, y);
    int d = 2;
    QMargins m(d, d, d, d);
    if (geometry().marginsRemoved(m).contains(p)) {
        auto w = qApp->topLevelAt(p);
        if (w && w == this) {
            qDebug() << __func__ << "click inside main window";
            last_wm_pos = QPoint(x, y);
            last_engine_pos = windowHandle()->framePosition();
            clicked = true;
        }
    }
}

void MainWindow::onMonitorButtonReleased(int x, int y)
{
    if (clicked) {
        qDebug() << __func__;
        clicked = false;
    }
}

void MainWindow::onMonitorMotionNotify(int x, int y)
{
    if (clicked) {
        QPoint d = QPoint(x, y) - last_wm_pos;
        windowHandle()->setFramePosition(last_engine_pos + d);
    }
}

#endif

MainWindow::~MainWindow()
{
    qDebug() << __func__;
    disconnect(_engine, 0, 0, 0);
    disconnect(&_engine->playlist(), 0, 0, 0);

    if (_lastCookie > 0) {
        utils::UnInhibitStandby(_lastCookie);
        qDebug() << "uninhibit cookie" << _lastCookie;
        _lastCookie = 0;
    }
    if (_powerCookie > 0) {
        utils::UnInhibitPower(_powerCookie);
        _powerCookie = 0;
    }

#ifdef USE_DXCB
    if (_evm) {
        disconnect(_evm, 0, 0, 0);
        delete _evm;
    }
#endif
}

void MainWindow::onApplicationStateChanged(Qt::ApplicationState e)
{
    switch (e) {
    case Qt::ApplicationActive:
        if (qApp->focusWindow())
            qDebug() << QString("focus window 0x%1").arg(qApp->focusWindow()->winId(), 0, 16);
        qApp->setActiveWindow(this);
        _evm->resumeRecording();
        resumeToolsWindow();
        break;

    case Qt::ApplicationInactive:
        _evm->suspendRecording();
        suspendToolsWindow();
        break;

    default:
        break;
    }
}

//void MainWindow::startPlayStateAnimation(bool play)
//{
//    auto r = QRect(QPoint(0, 0), QSize(128, 128));
//    r.moveCenter(rect().center());

//    if (!_playState->graphicsEffect()) {
//        auto *effect = new QGraphicsOpacityEffect(_playState);
//        effect->setOpacity(1.0);
//        _playState->setGraphicsEffect(effect);
//    }

//    auto duration = 160;
//    auto curve = QEasingCurve::InOutCubic;

//    auto pa = new QPropertyAnimation(_playState, "geometry");
//    if (play) {
//        QRect r2 = r;
//        pa->setStartValue(r);
//        r2.setSize({r.width() * 2, r.height() * 2});
//        r2.moveCenter(r.center());
//        pa->setEndValue(r2);
//    } else {
//        pa->setEndValue(r);
//        pa->setStartValue(QRect{r.center(), QSize{0, 0}});
//    }
//    pa->setDuration(duration);
//    pa->setEasingCurve(curve);


//    auto va = new QVariantAnimation(_playState);
//    va->setStartValue(0.0);
//    va->setEndValue(1.0);
//    va->setDuration(duration);
//    va->setEasingCurve(curve);

//    connect(va, &QVariantAnimation::valueChanged, [ = ](const QVariant & v) {
//        if (!play) _playState->setVisible(true);
//        auto d = v.toFloat();
//        auto effect = dynamic_cast<QGraphicsOpacityEffect *>(_playState->graphicsEffect());
//        effect->setOpacity(play ? 1.0 - d : d);
//        _playState->update();
//    });

//    if (play) {
//        connect(va, &QVariantAnimation::stateChanged, [ = ]() {
//            if (va->state() == QVariantAnimation::Stopped) {
//                _playState->setVisible(false);
//            }
//        });
//    }


//    auto pag = new QParallelAnimationGroup;
//    pag->addAnimation(va);
//    pag->addAnimation(pa);
//    pag->start(QVariantAnimation::DeleteWhenStopped);
//}

void MainWindow::animatePlayState()
{
    if (_miniMode) {
        return;
    }

    if (!_inBurstShootMode && _engine->state() == PlayerEngine::CoreState::Paused) {
        // startPlayStateAnimation(false);
        if (!_miniMode) {
            _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
            _animationlable->stop();
        }
        //_playState->raise();

    } else if (_engine->state() == PlayerEngine::CoreState::Idle) {
        //_playState->setVisible(false);

    } else {
        //do nothing here, startPlayStateAnimation(true) should be started before playback
        //is restored, or animation will get slow
    }
}

void MainWindow::syncPlayState()
{
    auto r = QRect(QPoint(0, 0), QSize(128, 128));
    r.moveCenter(rect().center());
    //_playState->move(r.topLeft());

    if (_miniMode) {
        //_playState->setVisible(false);
        return;
    }

    if (!_inBurstShootMode && _engine->state() == PlayerEngine::CoreState::Paused) {
        //_playState->setGeometry(r);
        //_playState->setVisible(true);
        //auto effect = dynamic_cast<QGraphicsOpacityEffect *>(_playState->graphicsEffect());
        //if (effect) effect->setOpacity(1.0);

    } else {
        //_playState->setVisible(false);
    }
}

void MainWindow::onBindingsChanged()
{
    qDebug() << __func__;
    {
        auto actions = this->actions();
        this->actions().clear();
        for (auto *act : actions) {
            delete act;
        }
    }

    auto &scmgr = ShortcutManager::get();
    auto actions = scmgr.actionsForBindings();
    for (auto *act : actions) {
        this->addAction(act);
        connect(act, &QAction::triggered, [ = ]() {
            this->menuItemInvoked(act);
        });
    }
}

void MainWindow::updateActionsState()
{
    auto pmf = _engine->playingMovieInfo();
    auto update = [ = ](QAction * act) {
        auto kd = ActionFactory::actionKind(act);
        bool v = true;
        switch (kd) {
        case ActionFactory::ActionKind::Screenshot:
        case ActionFactory::ActionKind::MatchOnlineSubtitle:
        case ActionFactory::ActionKind::BurstScreenshot:
        case ActionFactory::ActionKind::ToggleMiniMode:
        case ActionFactory::ActionKind::ToggleFullscreen:
        case ActionFactory::ActionKind::WindowAbove:
            v = _engine->state() != PlayerEngine::Idle;
            break;

        case ActionFactory::ActionKind::MovieInfo:
            v = _engine->state() != PlayerEngine::Idle;
            if (v) {
                v = v && _engine->playlist().count();
                if (v) {
                    auto pif = _engine->playlist().currentInfo();
                    v = v && pif.loaded;
                }
            }
            break;

        case ActionFactory::ActionKind::HideSubtitle:
        case ActionFactory::ActionKind::SelectSubtitle:
            v = pmf.subs.size() > 0;
        default:
            break;
        }
        act->setEnabled(v);
    };

    ActionFactory::get().updateMainActionsForMovie(pmf);
    ActionFactory::get().forEachInMainMenu(update);

    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    //so we need to workaround it.
    reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
}

void MainWindow::syncStaysOnTop()
{
    static xcb_atom_t atomStateAbove = Utility::internAtom("_NET_WM_STATE_ABOVE");

    auto atoms = Utility::windowNetWMState(windowHandle()->winId());
    bool window_is_above = atoms.contains(atomStateAbove);
    if (window_is_above != _windowAbove) {
        qDebug() << "syncStaysOnTop: window_is_above" << window_is_above;
        requestAction(ActionFactory::WindowAbove);
    }
}

void MainWindow::reflectActionToUI(ActionFactory::ActionKind kd)
{
    QList<QAction *> acts;
    switch (kd) {
    case ActionFactory::ActionKind::WindowAbove:
    case ActionFactory::ActionKind::ToggleFullscreen:
    case ActionFactory::ActionKind::LightTheme:
    case ActionFactory::ActionKind::ToggleMiniMode:
    case ActionFactory::ActionKind::TogglePlaylist:
    case ActionFactory::ActionKind::HideSubtitle: {
        qDebug() << __func__ << kd;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        while (p != acts.end()) {
            auto old = (*p)->isEnabled();
            (*p)->setEnabled(false);
            if (kd == ActionFactory::TogglePlaylist) {
                // here what we read is the last state of playlist
                if (_playlist->state() != PlaylistWidget::Opened) {
                    (*p)->setChecked(false);
                } else {
                    (*p)->setChecked(true);
                }
//                (*p)->setChecked(_playlist->state() != PlaylistWidget::Opened);
            } else {
                (*p)->setChecked(!(*p)->isChecked());
            }
            (*p)->setEnabled(old);
            ++p;
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        auto cp = _engine->subCodepage();
        qDebug() << "codepage" << cp;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        while (p != acts.end()) {
            auto args = ActionFactory::actionArgs(*p);
            if (args[0].toString() == cp) {
                (*p)->setEnabled(false);
                if (!(*p)->isChecked())(*p)->setChecked(true);
                (*p)->setEnabled(true);
                break;
            }

            ++p;
        }
        break;
    }

    case ActionFactory::ActionKind::SelectTrack:
    case ActionFactory::ActionKind::SelectSubtitle: {
        if (_engine->state() == PlayerEngine::Idle)
            break;

        auto pmf = _engine->playingMovieInfo();
        int id = -1;
        int idx = -1;
        if (kd == ActionFactory::ActionKind::SelectTrack) {
            id = _engine->aid();
            for (idx = 0; idx < pmf.audios.size(); idx++) {
                if (id == pmf.audios[idx]["id"].toInt()) {
                    break;
                }
            }
        } else if (kd == ActionFactory::ActionKind::SelectSubtitle) {
            id = _engine->sid();
            for (idx = 0; idx < pmf.subs.size(); idx++) {
                if (id == pmf.subs[idx]["id"].toInt()) {
                    break;
                }
            }
        }

        qDebug() << __func__ << kd << "idx = " << idx;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        while (p != acts.end()) {
            auto args = ActionFactory::actionArgs(*p);
            (*p)->setEnabled(false);
            if (args[0].toInt() == idx) {
                if (!(*p)->isChecked())(*p)->setChecked(true);
            } else {
                (*p)->setChecked(false);
            }
            (*p)->setEnabled(true);

            ++p;
        }
        break;
    }

    case ActionFactory::ActionKind::Stereo:
    case ActionFactory::ActionKind::DefaultFrame: {
        qDebug() << __func__ << kd;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        auto old = (*p)->isEnabled();
        (*p)->setEnabled(false);
        (*p)->setChecked(!(*p)->isChecked());
        //(*p)->setChecked(true);
        (*p)->setEnabled(old);
        break;
    }
    case ActionFactory::ActionKind::OrderPlay:
    case ActionFactory::ActionKind::ShufflePlay:
    case ActionFactory::ActionKind::SinglePlay:
    case ActionFactory::ActionKind::SingleLoop:
    case ActionFactory::ActionKind::ListLoop: {
        qDebug() << __func__ << kd;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        //auto old = (*p)->isEnabled();
        //(*p)->setEnabled(false);
        //(*p)->setChecked(!(*p)->isChecked());
        (*p)->setChecked(true);
        //(*p)->setEnabled(old);
        break;
    }
    default:
        break;
    }

}

bool MainWindow::set_playlistopen_clicktogglepause(bool playlistopen)
{
    _playlistopen_clicktogglepause = playlistopen;
    return _playlistopen_clicktogglepause;
}

NotificationWidget *MainWindow::get_nwComm()
{
    return _nwComm;
}
//排列判断(主要针对光驱)
static bool compareBarData(const QUrl &url1, const QUrl &url2)
{
    QString str1 = QFileInfo(url1.path()).fileName();
    QString str2 = QFileInfo(url2.path()).fileName();
    if (str1.length() > 0 && str2.length() > 0) {
        if (str1[0] < str2[0] ) {
            return true;
        }
    }
    return false;
}
bool MainWindow::addCdromPath()
{
    QStringList strCDMountlist;

    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        return false;
    }
    do {
        QString strLine = mountFile.readLine();
        if ( strLine.indexOf("/dev/sr") != -1 || strLine.indexOf("/dev/cdrom") != -1) { //说明存在光盘的挂载。
            strCDMountlist.append(strLine.split(" ").at(1));        //A B C 这样的格式，取中间的
        }
    } while (!mountFile.atEnd() );

    if (strCDMountlist.size() == 0)
        return false;


    QList<QUrl> urls = _engine->addPlayDir(strCDMountlist[0]);  //目前只是针对第一个光盘
    qSort(urls.begin(), urls.end(), compareBarData);
    if (urls.size()) {
        if (_engine->state() == PlayerEngine::CoreState::Idle)
            _engine->playByName(QUrl("playlist://0"));
        _engine->playByName(urls[0]);
    }
    return true;
}

void MainWindow::menuItemInvoked(QAction *action)
{
    auto kd = ActionFactory::actionKind(action);
    auto isShortcut = ActionFactory::isActionFromShortcut(action);
    if (ActionFactory::actionHasArgs(action)) {
        requestAction(kd, !isShortcut, ActionFactory::actionArgs(action), isShortcut);
    } else {
        auto var = action->property("kind");
        if (var == ActionFactory::ActionKind::Settings) {
            requestAction(kd, !isShortcut, {0}, isShortcut);
        } else {
            if (_playlist->state() == PlaylistWidget::State::Opened) {
                BindingMap bdMap = ShortcutManager::get().map();
                QHash<QKeySequence, ActionFactory::ActionKind>::const_iterator iter = bdMap.constBegin();
                bool isiter = false;
                while (iter != bdMap.constEnd()) {
                    if (iter.value() == kd) {
                        isiter = true;
                        if ((iter.key() == QKeySequence("Return")
                                || iter.key() == QKeySequence("Num+Enter")
                                || iter.key() == QKeySequence("Up")
                                || iter.key() == QKeySequence("Down")) && isShortcut) {
                            if (iter.key() == QKeySequence("Up") || iter.key() == QKeySequence("Down")) {
                                int key;
                                if (iter.key() == QKeySequence("Up")) {
                                    key = Qt::Key_Up;
                                } else {
                                    key = Qt::Key_Down;
                                }
                                _playlist->updateSelectItem(key);
                            }
                            break;
                        }
                        requestAction(kd, !isShortcut, {0}, isShortcut);
                        break;
                    }
                    ++iter;
                }
                if (isiter == false) {
                    requestAction(kd, !isShortcut, {0}, isShortcut);
                }
            } else {
                requestAction(kd, !isShortcut, {0}, isShortcut);
            }
        }
    }

    if (!isShortcut) {
        suspendToolsWindow();
    }
}

void MainWindow::switchTheme()
{
    _lightTheme = !_lightTheme;
    qApp->setTheme(_lightTheme ? "light" : "dark");
    Settings::get().setInternalOption("light_theme", _lightTheme);
}


bool MainWindow::isActionAllowed(ActionFactory::ActionKind kd, bool fromUI, bool isShortcut)
{
    if (_inBurstShootMode) {
        return false;
    }

    if (_miniMode) {
        if (fromUI || isShortcut) {
            switch (kd) {
            case ActionFactory::ToggleFullscreen:
            case ActionFactory::TogglePlaylist:
            case ActionFactory::BurstScreenshot:
                return false;

            case ActionFactory::ToggleMiniMode:
                return true;

            default:
                break;
            }
        }
    }

    if (isMaximized()) {
        switch (kd) {
        case ActionFactory::ToggleMiniMode:
            return true;
        default:
            break;
        }
    }

    if (isShortcut) {
        auto pmf = _engine->playingMovieInfo();
        bool v = true;
        switch (kd) {
        case ActionFactory::Screenshot:
        case ActionFactory::ToggleMiniMode:
        case ActionFactory::MatchOnlineSubtitle:
        case ActionFactory::BurstScreenshot:
            v = _engine->state() != PlayerEngine::Idle;
            break;

        case ActionFactory::MovieInfo:
            v = _engine->state() != PlayerEngine::Idle;
            if (v) {
                v = v && _engine->playlist().count();
                if (v) {
                    auto pif = _engine->playlist().currentInfo();
                    v = v && pif.loaded && pif.url.isLocalFile();
                }
            }
            break;

        case ActionFactory::HideSubtitle:
        case ActionFactory::SelectSubtitle:
            v = pmf.subs.size() > 0;
        default:
            break;
        }
        if (!v) return v;
    }

    return true;
}

void MainWindow::requestAction(ActionFactory::ActionKind kd, bool fromUI,
                               QList<QVariant> args, bool isShortcut)
{
    qDebug() << "kd = " << kd << "fromUI " << fromUI << (isShortcut ? "shortcut" : "");

    if (!isActionAllowed(kd, fromUI, isShortcut)) {
        qDebug() << kd << "disallowed";
        return;
    }

    switch (kd) {
    case ActionFactory::ActionKind::Exit:
        qApp->quit();
        break;

    case ActionFactory::ActionKind::LightTheme:
        if (fromUI) switchTheme();
        break;

    case ActionFactory::ActionKind::OpenCdrom: {
        auto dev = dmr::CommandLineManager::get().dvdDevice();
        if (dev.isEmpty()) {
            dev = probeCdromDevice();
        }
        if (dev.isEmpty()) {
            _nwComm->updateWithMessage(tr("No device found"));
            break;
        }

        if ( addCdromPath() == false) {
            _nwComm->updateWithMessage(tr("No device found"));
        }
        /*_engine->setDVDDevice(dev);  Comment by thx
        //FIXME: how to tell if it's bluray
        //QUrl url(QString("dvdread:///%1").arg(dev));
        QUrl url(QString("dvd://%1").arg(dev));
        //QUrl url(QString("dvdnav://"));
        play(url);*/
        break;
    }

    case ActionFactory::ActionKind::OpenUrl: {
        UrlDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            auto url = dlg.url();
            if (url.isValid()) {
                play(url);
            } else {
                _nwComm->updateWithMessage(tr("Parse failed"));
            }
        }
        break;
    }

    case ActionFactory::ActionKind::OpenDirectory: {
        QString name = DFileDialog::getExistingDirectory(this, tr("Open folder"),
                                                         lastOpenedPath(),
                                                         DFileDialog::DontResolveSymlinks);

        QFileInfo fi(name);
        if (fi.isDir() && fi.exists()) {
            Settings::get().setGeneralOption("last_open_path", fi.path());

            QList<QUrl> urls = _engine->addPlayDir(name);
            if (urls.size()) {
                _engine->playByName(QUrl("playlist://0"));
            }
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFileList: {
        //允许影院打开音乐文件进行播放
        QStringList filenames = DFileDialog::getOpenFileNames(this, tr("Open File"),
                                                              lastOpenedPath(),
                                                              tr("All videos (%2 %1)").arg(_engine->video_filetypes.join(" "))
                                                              .arg(_engine->audio_filetypes.join(" ")), 0,
                                                              DFileDialog::HideNameFilterDetails);

        QList<QUrl> urls;
        if (filenames.size()) {
            QFileInfo fileInfo(filenames[0]);
            if (fileInfo.exists()) {
                Settings::get().setGeneralOption("last_open_path", fileInfo.path());
            }

            for (const auto &filename : filenames) {
                urls.append(QUrl::fromLocalFile(filename));
            }
            const auto &valids = _engine->addPlayFiles(urls);
            _engine->playByName(valids[0]);
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFile: {
        QString filename = DFileDialog::getOpenFileName(this, tr("Open File"),
                                                        lastOpenedPath(),
                                                        tr("All videos (%1)").arg(_engine->video_filetypes.join(" ")), 0,
                                                        DFileDialog::HideNameFilterDetails);
        QFileInfo fileInfo(filename);
        if (fileInfo.exists()) {
            Settings::get().setGeneralOption("last_open_path", fileInfo.path());

            play(QUrl::fromLocalFile(filename));
        }
        break;
    }

    case ActionFactory::ActionKind::StartPlay: {
        if (_engine->playlist().count() == 0) {
            requestAction(ActionFactory::ActionKind::OpenFileList);
        } else {
            if (_engine->state() == PlayerEngine::CoreState::Idle) {
                if (Settings::get().isSet(Settings::ResumeFromLast)) {
                    int restore_pos = Settings::get().internalOption("playlist_pos").toInt();
                    restore_pos = qMax(qMin(restore_pos, _engine->playlist().count() - 1), 0);
                    requestAction(ActionFactory::ActionKind::GotoPlaylistSelected, false, {restore_pos});
                } else {
                    _engine->play();
                }
            }
        }
        break;
    }

    case ActionFactory::ActionKind::EmptyPlaylist: {
        _engine->clearPlaylist();
        break;
    }

    case ActionFactory::ActionKind::TogglePlaylist: {
        if (_playlist->state() == PlaylistWidget::Closed && !_toolbox->isVisible()) {
            _toolbox->show();
        }
        _playlist->togglePopup();
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        this->resumeToolsWindow();
        emit playlistchanged();
        break;
    }

    case ActionFactory::ActionKind::ToggleMiniMode: {
        if (_playlist->state() == PlaylistWidget::Opened && !isFullScreen()) {
            requestAction(ActionFactory::TogglePlaylist);
        }
        this->setWindowState(Qt::WindowNoState);
        if (isFullScreen()) {
            requestAction(ActionFactory::ToggleFullscreen);
            /*if (!fromUI) {
                reflectActionToUI(ActionFactory::ToggleFullscreen);
            }*/
            if (!isFullScreen()) {
                _fullscreentimelable->close();
            }
        }

        if (!fromUI) {
            reflectActionToUI(kd);
        }
        toggleUIMode();

        break;
    }

    case ActionFactory::ActionKind::MovieInfo: {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            //if (_engine->isPlayableFile())
            MovieInfoDialog mid(_engine->playlist().currentInfo());
            mid.exec();
        }
        break;
    }

    case ActionFactory::ActionKind::WindowAbove:
        _windowAbove = !_windowAbove;
        /**
         * switch above state by change windowFlags is unacceptable, since it'll
         * toggle visibility of window.
         * ```
            auto flags = windowFlags();
            if (_windowAbove) {
                flags |= Qt::WindowStaysOnTopHint;
            } else {
                flags &= ~Qt::WindowStaysOnTopHint;
            }
            setWindowFlags(flags);
            show();
            ```
        */
        Utility::setStayOnTop(this, _windowAbove);
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        break;

    case ActionFactory::ActionKind::QuitFullscreen: {
        if (_miniMode) {
            if (!fromUI) {
                reflectActionToUI(ActionFactory::ToggleMiniMode);
                //reflectActionToUI(kd);
            }
            toggleUIMode();
        } else if (isFullScreen()) {
//            if (_lastWindowState == Qt::WindowMaximized) {
//                showMaximized();
//            } else {
            requestAction(ActionFactory::ToggleFullscreen);
//            }
            /*if (!fromUI) {
                reflectActionToUI(ActionFactory::ToggleFullscreen);
            }*/
            if (!isFullScreen()) {
                _fullscreentimelable->close();
            }
        }
        break;
    }

    case ActionFactory::ActionKind::ToggleFullscreen: {
//        if(_playlist->state() == PlaylistWidget::State::Opened)
//        {
//            BindingMap map = ShortcutManager::get().map();
//            if(map.value(QKeySequence("Return")) == ActionFactory::ToggleFullscreen
//                 || map.value(QKeySequence("Num+Enter")) == ActionFactory::ToggleFullscreen)
//            {
//                return;
//            }
//        }
        if (isFullScreen()) {
            _quitfullscreenstopflag = true;
            if (_lastWindowState == Qt::WindowMaximized) {
                _maxfornormalflag = true;
                setWindowFlags(Qt::Window);
                showMaximized();
            } else {
                setWindowState(windowState() & ~Qt::WindowFullScreen);
                if (_lastRectInNormalMode.isValid() && !_miniMode && !isMaximized()) {
                    setGeometry(_lastRectInNormalMode);
                    move(_lastRectInNormalMode.x(), _lastRectInNormalMode.y());
                    resize(_lastRectInNormalMode.width(), _lastRectInNormalMode.height());
                }
            }
            if (!isFullScreen()) {
                _fullscreentimelable->close();
            }
        } else {
            if (/*!_miniMode && (fromUI || isShortcut) && */windowState() == Qt::WindowNoState) {
                _lastRectInNormalMode = geometry();
            }
            //可能存在更好的方法（全屏后更新toolbox状态），后期修改
            if (!_toolbox->getbAnimationFinash())
                return;
            showFullScreen();
            if (isFullScreen()) {
                _maxfornormalflag = false;
                int pixelsWidth = _toolbox->getfullscreentimeLabel()->width() + _toolbox->getfullscreentimeLabelend()->width();
                QRect deskRect = QApplication::desktop()->availableGeometry();
                _fullscreentimelable->setGeometry(deskRect.width() - pixelsWidth - 60, 40, pixelsWidth + 60, 36);
                _fullscreentimelable->show();

            }
        }
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        if (isFullScreen()) {
            _animationlable->move(QPoint(QApplication::desktop()->availableGeometry().width() / 2 - _animationlable->width() / 2
                                         , QApplication::desktop()->availableGeometry().height() / 2 - _animationlable->height() / 2));
        } else {
            _animationlable->move(QPoint((width() - _animationlable->width()) / 2,
                                         (height() - _animationlable->height()) / 2));
        }
        break;
    }

    case ActionFactory::ActionKind::PlaylistRemoveItem: {
        _playlist->removeClickedItem(isShortcut);
        break;
    }

    case ActionFactory::ActionKind::PlaylistOpenItemInFM: {
        _playlist->openItemInFM();
        break;
    }

    case ActionFactory::ActionKind::PlaylistItemInfo: {
        _playlist->showItemInfo();
        break;
    }

    case ActionFactory::ActionKind::ClockwiseFrame: {
        auto old = _engine->videoRotation();
        _engine->setVideoRotation((old + 90) % 360);
        break;
    }
    case ActionFactory::ActionKind::CounterclockwiseFrame: {
        auto old = _engine->videoRotation();
        _engine->setVideoRotation(((old - 90) + 360) % 360);
        break;
    }

    case ActionFactory::ActionKind::OrderPlay: {
        Settings::get().setInternalOption("playmode", 0);
        _engine->playlist().setPlayMode(PlaylistModel::PlayMode::OrderPlay);
        break;
    }
    case ActionFactory::ActionKind::ShufflePlay: {
        Settings::get().setInternalOption("playmode", 1);
        _engine->playlist().setPlayMode(PlaylistModel::PlayMode::ShufflePlay);
        break;
    }
    case ActionFactory::ActionKind::SinglePlay: {
        Settings::get().setInternalOption("playmode", 2);
        _engine->playlist().setPlayMode(PlaylistModel::PlayMode::SinglePlay);
        break;
    }
    case ActionFactory::ActionKind::SingleLoop: {
        Settings::get().setInternalOption("playmode", 3);
        _engine->playlist().setPlayMode(PlaylistModel::PlayMode::SingleLoop);
        break;
    }
    case ActionFactory::ActionKind::ListLoop: {
        Settings::get().setInternalOption("playmode", 4);
        _engine->playlist().setPlayMode(PlaylistModel::PlayMode::ListLoop);
        break;
    }

    case ActionFactory::ActionKind::Stereo: {
        _engine->changeSoundMode(Backend::SoundMode::Stereo);
        _nwComm->updateWithMessage(tr("Stereo"));
        break;
    }
    case ActionFactory::ActionKind::LeftChannel: {
        _engine->changeSoundMode(Backend::SoundMode::Left);
        _nwComm->updateWithMessage(tr("Left channel"));
        break;
    }
    case ActionFactory::ActionKind::RightChannel: {
        _engine->changeSoundMode(Backend::SoundMode::Right);
        _nwComm->updateWithMessage(tr("Right channel"));
        break;
    }

    case ActionFactory::ActionKind::DefaultFrame: {
        _engine->setVideoAspect(-1.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio4x3Frame: {
        _engine->setVideoAspect(4.0 / 3.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x9Frame: {
        _engine->setVideoAspect(16.0 / 9.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x10Frame: {
        _engine->setVideoAspect(16.0 / 10.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio185x1Frame: {
        _engine->setVideoAspect(1.85);
        break;
    }
    case ActionFactory::ActionKind::Ratio235x1Frame: {
        _engine->setVideoAspect(2.35);
        break;
    }
    /*
            if (!_engine->muted()) {
                _engine->changeVolume(0);
                setAudioVolume(0);

                _engine->toggleMute();
                Settings::get().setInternalOption("mute", _engine->muted());
                setMusicMuted(_engine->muted());
                if (_engine->muted()) {
                    _nwComm->updateWithMessage(tr("Mute"));
                    //_engine->changeVolume(0);
                    //_engine->toggleMute();
                } else {
                    double pert = _engine->volume();
                    _engine->changeVolume(pert);
                    _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
                }
            }

    */
    case ActionFactory::ActionKind::ToggleMute: {
        /*if (_engine->muted()) {
            //此处存在修改风险，注意！
            if (m_lastVolume == 0) {
                return;
            } else if (m_lastVolume == -1) {
                changedMute();
                int savedVolume = Settings::get().internalOption("global_volume").toInt();
                changedVolume(savedVolume);
                setMusicMuted(false);
            } else {
                changedMute();
                changedVolume(m_lastVolume);
                setMusicMuted(false);
            }
        } else {
            m_lastVolume = _engine->volume();
            Settings::get().setInternalOption("last_volume", _engine->volume());
            changedMute();
            changedVolume(0);
            setMusicMuted(true);
        }*/
        changedMute();
        setMusicMuted(_engine->muted());
        if (_engine->muted()) {
            _nwComm->updateWithMessage(tr("Mute"));
        } else {
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(_engine->volume()));
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeVolume: {
        if (!args.isEmpty()) {
            int nVol = args[0].toInt();
            if (m_lastVolume == nVol) {
                _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
                setAudioVolume(nVol);
                return;
            }
            _engine->changeVolume(nVol);
            //当音量与当前静音状态不符时切换静音状态
            /*if (nVol == 0 && !_engine->muted()) {
                changedMute();
                _nwComm->updateWithMessage(tr("Mute"));
                m_lastVolume = _engine->volume();
                Settings::get().setInternalOption("last_volume", _engine->volume());
            } else if (_engine->muted()) {
                changedMute();
                _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
                m_lastVolume = _engine->volume();
                Settings::get().setInternalOption("last_volume", _engine->volume());
            } else {
                _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
                m_lastVolume = _engine->volume();
                Settings::get().setInternalOption("last_volume", _engine->volume());
                setAudioVolume(nVol);
            }*/
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
            m_lastVolume = _engine->volume();
            Settings::get().setInternalOption("last_volume", _engine->volume());
            setAudioVolume(nVol);
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeUp: {
        if (_engine->muted()) {
            changedMute();
            setMusicMuted(_engine->muted());
        }
        _engine->volumeUp();
        m_lastVolume = _engine->volume();
        int pert = _engine->volume();
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(pert));
        break;
    }

    case ActionFactory::ActionKind::VolumeDown: {
        _engine->volumeDown();
        int pert = _engine->volume();
        if (pert == 0 && !_engine->muted()) {
            changedMute();
            _nwComm->updateWithMessage(tr("Mute"));
            setAudioVolume(0);
        } else if (pert > 0 && _engine->muted()) {
            changedMute();
            setMusicMuted(_engine->muted());
        }
        m_lastVolume = _engine->volume();
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_lastVolume));
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistSelected: {
        _engine->playSelected(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistNext: {

        /* if (_engine->state() == PlayerEngine::CoreState::Idle) {
             //为了解决快速切换下一曲卡顿的问题
             QTimer *timer = new QTimer;
             connect(timer, &QTimer::timeout, [ = ]() {
                 timer->deleteLater();
                 if (_engine->state() == PlayerEngine::CoreState::Idle) {
                     if (isFullScreen() || isMaximized()) {
                         _movieSwitchedInFsOrMaxed = true;
                     }
                     _engine->next();
                 }
             });
             timer->start(500);
             return ;
         }*/
        if (m_IsFree == false)
            return ;

        m_IsFree = false;
        if (isFullScreen() || isMaximized()) {
            _movieSwitchedInFsOrMaxed = true;
        }
        _engine->next();

        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistPrev: {

        /* static bool sContinuous = false;

         if (sContinuous == true)
             return ;

         sContinuous = true;

         QTimer *timer = new QTimer;
         connect(timer, &QTimer::timeout, [ = ]() {
             timer->deleteLater();

             sContinuous = false;
         });
         timer->start(1000);*/

        /*if (_engine->state() == PlayerEngine::CoreState::Idle) {
            //为了解决快速切换下一曲卡顿的问题
            QTimer *timer = new QTimer;
            connect(timer, &QTimer::timeout, [ = ]() {
                timer->deleteLater();
                if (_engine->state() == PlayerEngine::CoreState::Idle) {
                    if (isFullScreen() || isMaximized()) {
                        _movieSwitchedInFsOrMaxed = true;
                    }
                    _engine->prev();
                }
            });
            timer->start(500);
            return ;
        }*/

        if (m_IsFree == false)
            return ;

        m_IsFree = false;
        if (isFullScreen() || isMaximized()) {
            _movieSwitchedInFsOrMaxed = true;
        }
        _engine->prev();
        break;
    }

    case ActionFactory::ActionKind::SelectTrack: {
        Q_ASSERT(args.size() == 1);
        _engine->selectTrack(args[0].toInt());
        _nwComm->updateWithMessage(tr("Track: %1").arg(args[0].toInt() + 1));
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        break;
    }

    case ActionFactory::ActionKind::MatchOnlineSubtitle: {
        _engine->loadOnlineSubtitle(_engine->playlist().currentInfo().url);
        break;
    }

    case ActionFactory::ActionKind::SelectSubtitle: {
        Q_ASSERT(args.size() == 1);
        _engine->selectSubtitle(args[0].toInt());
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        Q_ASSERT(args.size() == 1);
        _engine->setSubCodepage(args[0].toString());
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        break;
    }

    case ActionFactory::ActionKind::HideSubtitle: {
        _engine->toggleSubtitle();
        break;
    }

    case ActionFactory::ActionKind::SubDelay: {
        _engine->setSubDelay(0.5);
        auto d = _engine->subDelay();
        _nwComm->updateWithMessage(tr("Subtitle %1: %2s")
                                   .arg(d > 0.0 ? tr("delayed") : tr("advanced")).arg(d > 0.0 ? d : -d));
        break;
    }

    case ActionFactory::ActionKind::SubForward: {
        _engine->setSubDelay(-0.5);
        auto d = _engine->subDelay();
        _nwComm->updateWithMessage(tr("Subtitle %1: %2s")
                                   .arg(d > 0.0 ? tr("delayed") : tr("advanced")).arg(d > 0.0 ? d : -d));
        break;
    }

    case ActionFactory::ActionKind::AccelPlayback: {
        _playSpeed = qMin(2.0, _playSpeed + 0.1);
        _engine->setPlaySpeed(_playSpeed);
        _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        break;
    }

    case ActionFactory::ActionKind::DecelPlayback: {
        _playSpeed = qMax(0.1, _playSpeed - 0.1);
        _engine->setPlaySpeed(_playSpeed);
        _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        break;
    }

    case ActionFactory::ActionKind::ResetPlayback: {
        _playSpeed = 1.0;
        _engine->setPlaySpeed(_playSpeed);
        _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        break;
    }

    case ActionFactory::ActionKind::LoadSubtitle: {
        QString filename = DFileDialog::getOpenFileName(this, tr("Open File"),
                                                        lastOpenedPath(),
                                                        tr("Subtitle (*.ass *.aqt *.jss *.gsub *.ssf *.srt *.sub *.ssa *.smi *.usf *.idx)"));
        if (QFileInfo(filename).exists()) {
            if (_engine->state() == PlayerEngine::Idle)
                subtitleMatchVideo(filename);
            else {
                auto success = _engine->loadSubtitle(QFileInfo(filename));
                _nwComm->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
            }
        } else {
            _nwComm->updateWithMessage(tr("Load failed"));
        }
        break;
    }

    case ActionFactory::ActionKind::TogglePause: {
        if (!_playlistopen_clicktogglepause) {
            if (_engine->state() == PlayerEngine::Idle && isShortcut) {
                requestAction(ActionFactory::StartPlay);
            } else {
                if (_engine->state() == PlayerEngine::Paused) {
                    //startPlayStateAnimation(true);
                    if (!_miniMode) {
                        _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                        _animationlable->start();
                    }
                    QTimer::singleShot(160, [ = ]() {
                        _engine->pauseResume();
                    });
                } else {
                    _engine->pauseResume();
                }
            }
        } else {
            _playlistopen_clicktogglepause = false;
        }
        break;
    }

    case ActionFactory::ActionKind::SeekBackward: {
        _engine->seekBackward(5);
        break;
    }

    case ActionFactory::ActionKind::SeekForward: {
        _engine->seekForward(5);
        break;
    }

    case ActionFactory::ActionKind::SeekAbsolute: {
        Q_ASSERT(args.size() == 1);
        _engine->seekAbsolute(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::Settings: {
        handleSettings();
        break;
    }

    case ActionFactory::ActionKind::Screenshot: {
        auto img = _engine->takeScreenshot();

        QString filePath = Settings::get().screenshotNameTemplate();
        bool success = false;
        if (img.isNull())
            qDebug() << __func__ << "pixmap is null";
        else
            success = img.save(filePath);

#ifdef USE_SYSTEM_NOTIFY
        // Popup notify.
        QDBusInterface notification("org.freedesktop.Notifications",
                                    "/org/freedesktop/Notifications",
                                    "org.freedesktop.Notifications",
                                    QDBusConnection::sessionBus());

        QStringList actions;
        actions << "_open" << tr("View");


        QVariantMap hints;
        hints["x-deepin-action-_open"] = QString("xdg-open,%1").arg(filePath);


        QList<QVariant> arg;
        arg << (QCoreApplication::applicationName())                 // appname
            << ((unsigned int) 0)                                    // id
            << QString("deepin-movie")                               // icon
            << tr("Film screenshot")                                // summary
            << QString("%1 %2").arg(tr("Saved to")).arg(filePath) // body
            << actions                                               // actions
            << hints                                                 // hints
            << (int) -1;                                             // timeout
        notification.callWithArgumentList(QDBus::AutoDetect, "Notify", arg);

#else

#define POPUP_ADAPTER(icon, text)  do { \
popup->setIcon(icon);\
DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);\
QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);\
QFontMetrics fm(font);\
auto w = fm.boundingRect(text).width();\
popup->setMessage(text);\
popup->resize(w + 70, 52);\
popup->move((width() - popup->width()) / 2, height() - 127);\
popup->show();\
} while (0)

//        if (!popup) {
//            popup = new DFloatingMessage(DFloatingMessage::TransientType, this);
//        }
        if (success) {
            const QIcon icon = QIcon(":/resources/icons/icon_toast_sucess.svg");
            QString text = QString(tr("The screenshot is saved"));
            POPUP_ADAPTER(icon, text);
        } else {
            const QIcon icon = QIcon(":/resources/icons/icon_toast_fail.svg");
            QString text = QString(tr("Failed to save the screenshot"));
            POPUP_ADAPTER(icon, text);
        }

#undef POPUP_ADAPTER

#endif
        break;
    }

    case ActionFactory::ActionKind::GoToScreenshotSolder: {
        QString filePath = Settings::get().screenshotLocation();
        qDebug() << __func__ << filePath;
        QProcess *fp = new QProcess();
        QObject::connect(fp, SIGNAL(finished(int)), fp, SLOT(deleteLater()));
        fp->start("dde-file-manager", QStringList(filePath));
        fp->waitForStarted(3000);
        break;
    }

    case ActionFactory::ActionKind::BurstScreenshot: {
        startBurstShooting();
        break;
    }

    case ActionFactory::ActionKind::ViewShortcut: {
        QRect rect = window()->geometry();
        QPoint pos(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
        QStringList shortcutString;
        QString param1 = "-j=" + ShortcutManager::get().toJson();
        param1.replace("Return", "Enter");
        param1.replace("PgDown", "PageDown");
        param1.replace("PgUp", "PageUp");
        QString param2 = "-p=" + QString::number(pos.x()) + "," + QString::number(pos.y());
        shortcutString << param1 << param2;

        if (!shortcutViewProcess) {
            shortcutViewProcess = new QProcess();
        }
        shortcutViewProcess->startDetached("deepin-shortcut-viewer", shortcutString);

        connect(shortcutViewProcess, SIGNAL(finished(int)),
                shortcutViewProcess, SLOT(deleteLater()));

        break;
    }

    case ActionFactory::ActionKind::NextFrame: {
        _engine->nextFrame();

        break;
    }

    case ActionFactory::ActionKind::PreviousFrame: {
        _engine->previousFrame();

        break;
    }

    default:
        break;
    }
}

void MainWindow::onBurstScreenshot(const QImage &frame, qint64 timestamp)
{
#define POPUP_ADAPTER(icon, text)  do { \
        popup->setIcon(icon);\
        DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);\
        QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);\
        QFontMetrics fm(font);\
        auto w = fm.boundingRect(text).width();\
        popup->setMessage(text);\
        popup->resize(w + 70, 52);\
        popup->move((width() - popup->width()) / 2, height() - 127);\
        popup->show();\
    } while (0)

    qDebug() << _burstShoots.size();
    if (!frame.isNull()) {
        auto msg = QString(tr("Taking the screenshots, please wait..."));
        _nwComm->updateWithMessage(msg);

        _burstShoots.append(qMakePair(frame, timestamp));
    }

    if (_burstShoots.size() >= 15 || frame.isNull()) {
        disconnect(_engine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
        _engine->stopBurstScreenshot();
        _inBurstShootMode = false;
        _toolbox->setEnabled(true);
        if (_listener) _listener->setEnabled(!_miniMode);

        if (frame.isNull()) {
            _burstShoots.clear();
            if (!_pausedBeforeBurst)
                _engine->pauseResume();
            return;
        }

        BurstScreenshotsDialog bsd(_engine->playlist().currentInfo());
        bsd.updateWithFrames(_burstShoots);
        auto ret = bsd.exec();
        qDebug() << "BurstScreenshot done";

        _burstShoots.clear();
        if (!_pausedBeforeBurst)
            _engine->pauseResume();

        if (ret == QDialog::Accepted) {
            auto poster_path = bsd.savedPosterPath();
            if (QFileInfo::exists(poster_path)) {
                const QIcon icon = QIcon(":/resources/icons/icon_toast_sucess.svg");
                QString text = QString(tr("The screenshot is saved"));
                POPUP_ADAPTER(icon, text);
            } else {
                const QIcon icon = QIcon(":/resources/icons/icon_toast_fail.svg");
                QString text = QString(tr("Failed to save the screenshot"));
                POPUP_ADAPTER(icon, text);
            }
        }
    }
}

void MainWindow::startBurstShooting()
{
    _inBurstShootMode = true;
    _toolbox->setEnabled(false);
    if (_listener) _listener->setEnabled(false);

    _pausedBeforeBurst = _engine->paused();

    connect(_engine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
    _engine->burstScreenshot();
}

void MainWindow::handleSettings()
{
    auto dsd = new DSettingsDialog(this);
    dsd->widgetFactory()->registerWidget("selectableEdit", createSelectableLineEditOptionHandle);

    dsd->setProperty("_d_QSSThemename", "dark");
    dsd->setProperty("_d_QSSFilename", "DSettingsDialog");
    dsd->updateSettings(Settings::get().settings());

    //hack:
    auto subft = dsd->findChild<QSpinBox *>("OptionDSpinBox");
    if (subft) {
        subft->setMinimum(8);
    }

    // hack: reset is set to default by QDialog, which makes lineedit's enter
    // press is responded by reset button
    auto reset = dsd->findChild<QPushButton *>("SettingsContentReset");
    reset->setDefault(false);
    reset->setAutoDefault(false);

    dsd->exec();
    delete dsd;
    Settings::get().settings()->sync();
}

void MainWindow::playList(const QList<QString> &l)
{
    static QRegExp url_re("\\w+://");

    QList<QUrl> urls;
    for (const auto &filename : l) {
        qDebug() << filename;
        QUrl url;
        if (url_re.indexIn(filename) == 0) {
            url = QUrl::fromPercentEncoding(filename.toUtf8());
            if (!url.isValid())
                url = QUrl(filename);
        } else {
            url = QUrl::fromLocalFile(filename);
        }
        if (url.isValid())
            urls.append(url);
    }
    const auto &valids = _engine->addPlayFiles(urls);
    if (valids.size()) {
        if (!isHidden()) {
            activateWindow();
        }
        _engine->playByName(valids[0]);
    }
}

void MainWindow::play(const QUrl &url)
{
    if (!url.isValid())
        return;

    if (!isHidden()) {
        activateWindow();
    }

//    if (!_engine->addPlayFile(url)) {
//        auto msg = QString(tr("Invalid file: %1").arg(url.fileName()));
//        _nwComm->updateWithMessage(msg);
//        return;
//    }
    if (url.scheme().startsWith("dvd")) {
        m_dvdUrl = url;
        if (!_engine->addPlayFile(url)) {
            auto msg = QString(tr("No video file found"));
            _nwComm->updateWithMessage(msg);
            return;
        } else {
            // todo: Disable toolbar buttons
            auto msg = QString(tr("Reading DVD files..."));
//            _nwDvd->updateWithMessage(msg, false);
            _nwDvd->updateWithMessage(msg, true);
//            return;
        }
    } else {
        if (!_engine->addPlayFile(url)) {
            auto msg = QString(tr("Invalid file: %1").arg(url.fileName()));
            _nwComm->updateWithMessage(msg);
            return;
        }
    }
    _engine->playByName(url);
}

void MainWindow::toggleShapeMask()
{
//    if (CompositingManager::get().composited()) {
//        return;
//    }
    return;

#ifndef USE_DXCB
    if (isFullScreen() || isMaximized()) {
        clearMask();
    } else {
        QPixmap shape(size());
        shape.setDevicePixelRatio(windowHandle()->devicePixelRatio());
        shape.fill(Qt::transparent);

        QPainter p(&shape);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath pp;
        pp.addRoundedRect(rect(), RADIUS, RADIUS);
        p.fillPath(pp, QBrush(Qt::white));
        p.end();

        setMask(shape.mask());
    }
#endif
}

void MainWindow::updateProxyGeometry()
{
    toggleShapeMask();

#ifdef USE_DXCB
    // border is drawn by dxcb
    auto view_rect = rect();
#else
    // leave one pixel for border
//    auto view_rect = rect().marginsRemoved(QMargins(1, 1, 1, 1));
//    if (isFullScreen()) view_rect = rect();
    auto view_rect = rect();
#endif
    _engine->resize(view_rect.size());

    if (!_miniMode) {
        if (_titlebar) {
            _titlebar->setFixedWidth(view_rect.width());
        }

        if (_toolbox) {
            QRect rfs(5, height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT) - rect().top() - 5,
                      rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT));
            QRect rct(5, height() - TOOLBOX_HEIGHT - rect().top() - 5,
                      rect().width() - 10, TOOLBOX_HEIGHT);
            if (isFullScreen()) {
                if (_playlist->state() == PlaylistWidget::State::Opened) {
#ifndef __aarch64__
                    _toolbox->setGeometry(rfs);
#else
                    _toolbox->setGeometry(rct);
#endif
                } else {
                    _toolbox->setGeometry(rct);
                }
            } else {
                if (_playlist->state() == PlaylistWidget::State::Opened) {
#ifndef __aarch64__
                    _toolbox->setGeometry(rfs);
#else
                    _toolbox->setGeometry(rct);
#endif
                } else {
                    _toolbox->setGeometry(rct);
                }
            }
        }

        if (_playlist && !_playlist->toggling()) {
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 10)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            _playlist->setGeometry(fixed);
        }
    }


    syncPlayState();
//    if (_playState) {
//        auto r = QRect(QPoint(0, 0), QSize(128, 128));
//        r.moveCenter(rect().center());
//        _playState->move(r.topLeft());
//    }

}

void MainWindow::suspendToolsWindow()
{
    if (!_miniMode) {
        if (_playlist && _playlist->state() == PlaylistWidget::Opened)
            return;

        if (qApp->applicationState() == Qt::ApplicationInactive) {

        } else {
            // menus  are popped up
            // NOTE: menu keeps focus while hidden, so focusWindow is not used
            if (ActionFactory::get().mainContextMenu()->isVisible() ||
                    ActionFactory::get().titlebarMenu()->isVisible())
                return;
            //if (qApp->focusWindow() != windowHandle())
            //return;

            if (_titlebar->isVisible()) {
                if (insideToolsArea(mapFromGlobal(QCursor::pos())))
                    return;
            } else {
                if (_toolbox->geometry().contains(mapFromGlobal(QCursor::pos()))) {
                    return;
                }
            }
        }

        if (_toolbox->anyPopupShown())
            return;

        if (_engine->state() == PlayerEngine::Idle)
            return;

        if (_autoHideTimer.isActive())
            return;

        if (isFullScreen()) {
            if (qApp->focusWindow() == this->windowHandle())
                qApp->setOverrideCursor(Qt::BlankCursor);
        }

        _titlebar->hide();
        _toolbox->hide();
    } else {
        if (_autoHideTimer.isActive())
            return;

        _miniPlayBtn->hide();
        _miniCloseBtn->hide();
        _miniQuitMiniBtn->hide();
    }
}

void MainWindow::resumeToolsWindow()
{
    if (_engine->state() != PlayerEngine::Idle &&
            qApp->applicationState() == Qt::ApplicationActive) {
        // playlist's previous state was Opened
        if (_playlist->state() != PlaylistWidget::Closed &&
                !frameGeometry().contains(QCursor::pos())) {
            goto _finish;
        }
    }

    qApp->restoreOverrideCursor();
    setCursor(Qt::ArrowCursor);

    if (!_miniMode) {
        _titlebar->setVisible(!isFullScreen());
        _toolbox->show();
    } else {
        _miniPlayBtn->show();
        _miniCloseBtn->show();
        _miniQuitMiniBtn->show();
    }

_finish:
    _autoHideTimer.start(AUTOHIDE_TIMEOUT);
}

void MainWindow::checkOnlineState(const bool isOnline)
{
    if (!isOnline) {
        this->sendMessage(QIcon(":/icons/deepin/builtin/icons/ddc_warning_30px.svg"), QObject::tr("Network disconnected"));
    }
}

void MainWindow::checkOnlineSubtitle(const OnlineSubtitle::FailReason reason)
{
    if (OnlineSubtitle::FailReason::NoSubFound == reason) {
        _nwComm->updateWithMessage(tr("No matching online subtitles"));
    }
}

void MainWindow::checkWarningMpvLogsChanged(const QString prefix, const QString text)
{
    QString warningMessage(text);
    qDebug() << "checkWarningMpvLogsChanged" << text;
    if (warningMessage.contains(QString("Hardware does not support image size 3840x2160"))) {
        requestAction(ActionFactory::TogglePause);

        DDialog *dialog = new DDialog;
        dialog->setFixedWidth(440);
        QImage icon = utils::LoadHiDPIImage(":/resources/icons/warning.svg");
        QPixmap pix = QPixmap::fromImage(icon);
        dialog->setIcon(QIcon(pix));
        dialog->setMessage(tr("4K video may be stuck"));
        dialog->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect();
        effect->setOffset(0, 4);
        effect->setColor(QColor(0, 145, 255, 76));
        effect->setBlurRadius(4);
        dialog->getButton(0)->setFixedWidth(340);
        dialog->getButton(0)->setGraphicsEffect(effect);
        dialog->exec();
        QTimer::singleShot(500, [ = ]() {
            //startPlayStateAnimation(true);
            if (!_miniMode) {
                _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                _animationlable->start();
            }
            _engine->pauseResume();
        });
    }

}

void MainWindow::slotdefaultplaymodechanged(const QString &key, const QVariant &value)
{
    if (key != "base.play.playmode") {
        qDebug() << "Settings key error";
        return;
    }
    auto mode_opt = Settings::get().settings()->option("base.play.playmode");
    //auto mode_id = mode_opt->value().toInt();
    auto mode = mode_opt->data("items").toStringList()[value.toInt()];
    if (mode == tr("Order play")) {
        //requestAction(ActionFactory::OrderPlay);
        _engine->playlist().setPlayMode(PlaylistModel::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (mode == tr("Shuffle play")) {
        //requestAction(ActionFactory::ShufflePlay);
        _engine->playlist().setPlayMode(PlaylistModel::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (mode == tr("Single play")) {
        //requestAction(ActionFactory::SinglePlay);
        _engine->playlist().setPlayMode(PlaylistModel::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (mode == tr("Single loop")) {
        //requestAction(ActionFactory::SingleLoop);
        _engine->playlist().setPlayMode(PlaylistModel::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (mode == tr("List loop")) {
        //requestAction(ActionFactory::ListLoop);
        _engine->playlist().setPlayMode(PlaylistModel::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
}

void MainWindow::syncPostion()
{
    _nwComm->syncPosition();
}

void MainWindow::checkErrorMpvLogsChanged(const QString prefix, const QString text)
{
    QString errorMessage(text);
    qDebug() << "checkErrorMpvLogsChanged" << text;
    if (errorMessage.toLower().contains(QString("avformat_open_input() failed"))) {
        //do nothing
    } else if (errorMessage.toLower().contains(QString("fail")) && errorMessage.toLower().contains(QString("open"))) {
        _nwComm->updateWithMessage(tr("Cannot open file or stream"));
        _engine->playlist().remove(_engine->playlist().count() - 1);
    } else if (errorMessage.toLower().contains(QString("fail")) &&
               (errorMessage.toLower().contains(QString("format")))
              ) {
        _nwComm->updateWithMessage(tr("Invalid file"));
        _engine->playlist().remove(_engine->playlist().count() - 1);
//        _engine->playlist().clear();
    } else if (errorMessage.toLower().contains(QString("moov atom not found"))) {
        _nwComm->updateWithMessage(tr("Invalid file"));
//        _engine->playlist().clear();
    } else if (errorMessage.toLower().contains(QString("couldn't open dvd device"))) {
        _nwComm->updateWithMessage(tr("Please insert a CD/DVD"));
//        _engine->playlist().clear();
    } else if (errorMessage.toLower().contains(QString("incomplete frame")) ||
               errorMessage.toLower().contains(QString("MVs not available"))) {
        _nwComm->updateWithMessage(tr("The CD/DVD has been ejected"));
//        _engine->playlist().remove(_engine->playlist().current());
    } else if ((errorMessage.toLower().contains(QString("can't"))) &&
               (errorMessage.toLower().contains(QString("open")))) {
        _nwComm->updateWithMessage(tr("No video file found"));
//        _engine->playlist().clear();
    } else if (errorMessage.contains(QString("Hardware does not support image size 3840x2160"))) {
        requestAction(ActionFactory::TogglePause);

        DDialog *dialog = new DDialog;
        dialog->setFixedWidth(440);
        QImage icon = utils::LoadHiDPIImage(":/resources/icons/warning.svg");
        QPixmap pix = QPixmap::fromImage(icon);
        dialog->setIcon(QIcon(pix));
        dialog->setMessage(tr("4K video may be stuck"));
        dialog->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect();
        effect->setOffset(0, 4);
        effect->setColor(QColor(0, 145, 255, 76));
        effect->setBlurRadius(4);
        dialog->getButton(0)->setFixedWidth(340);
        dialog->getButton(0)->setGraphicsEffect(effect);
        dialog->exec();
        QTimer::singleShot(500, [ = ]() {
            //startPlayStateAnimation(true);
            if (!_miniMode) {
                _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                _animationlable->start();
            }
            _engine->pauseResume();
        });
    }

}

void MainWindow::hideEvent(QHideEvent *event)
{
    if (Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_engine && _engine->state() == PlayerEngine::Playing) {
            if (!_quitfullscreenstopflag) {
                _pausedOnHide = true;
                requestAction(ActionFactory::TogglePause);
                _quitfullscreenstopflag = false;
            } else {
                _quitfullscreenstopflag = false;
            }
        }
        QList<QAction *> acts = ActionFactory::get().findActionsByKind(ActionFactory::TogglePlaylist);
        acts.at(0)->setChecked(false);
    }
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    qDebug() << __func__;
    if (_lastCookie > 0) {
        utils::UnInhibitStandby(_lastCookie);
        qDebug() << "uninhibit cookie" << _lastCookie;
        _lastCookie = 0;
    }

    int cur = 0;
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        cur = _engine->playlist().current();
        if (cur >= 0) {
            Settings::get().setInternalOption("playlist_pos", cur);
        }
    }

    _engine->savePlaybackPosition();

    {
        saveWindowState();
    }

    ev->accept();
}

void MainWindow::wheelEvent(QWheelEvent *we)
{
    if (insideToolsArea(we->pos()) || insideResizeArea(we->globalPos()))
        return;

    if (_playlist->state() == PlaylistWidget::Opened) {
        we->ignore();
        return;
    }

    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier) {
        requestAction(we->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
    }
}

void MainWindow::focusInEvent(QFocusEvent *fe)
{
    resumeToolsWindow();
}

void MainWindow::showEvent(QShowEvent *event)
{
    qDebug() << __func__;
    if (_pausedOnHide || Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_pausedOnHide && _engine && _engine->state() != PlayerEngine::Playing) {
            if (!_quitfullscreenstopflag) {
                requestAction(ActionFactory::TogglePause);
                _pausedOnHide = false;
                _quitfullscreenstopflag = false;
            } else {
                _quitfullscreenstopflag = false;
            }
        }
    }

    _titlebar->raise();
    _toolbox->raise();
    _playlist->raise();
    resumeToolsWindow();

    if (!qgetenv("FLATPAK_APPID").isEmpty()) {
        qDebug() << "workaround for flatpak";
        if (_playlist->isVisible())
            updateProxyGeometry();
    }
}

void MainWindow::resizeByConstraints(bool forceCentered)
{
    if (_engine->state() == PlayerEngine::Idle || _engine->playlist().count() == 0) {
        _titlebar->setTitletxt(QString());
        return;
    }

    if (_miniMode || isFullScreen() || isMaximized()) {
        return;
    }

    qDebug() << __func__;
    updateWindowTitle();

    const auto &mi = _engine->playlist().currentInfo().mi;
    auto sz = _engine->videoSize();
#ifdef __mips__
    if (!CompositingManager::get().composited()) {
        float w = (float)sz.width();
        float h = (float)sz.height();
        if ((w / h) > 0.56 && (w / h) < 0.75) {
            _engine->setVideoZoom(-(w / h) - 0.1);
        }

        //3.26修改，初始分辨率大于1080P时缩小一半
        while (sz.width() >= 1080) {
            sz = sz / 2;
        }
    }
    _nwComm->syncPosition();
#endif
    if (sz.isEmpty()) {
        sz = QSize(mi.width, mi.height);
        qDebug() << mi.width << mi.height;
    }

    auto geom = qApp->desktop()->availableGeometry(this);
    if (sz.width() > geom.width() || sz.height() > geom.height()) {
        sz.scale(geom.width(), geom.height(), Qt::KeepAspectRatio);
    }

    qDebug() << "original: " << size() << "requested: " << sz;
#ifdef __aarch64
    _nwComm->syncPosition(this->geometry());
    QRect rect = this->geometry();
#endif
    if (size() == sz)
        return;

    if (forceCentered) {
        QRect r;
        r.setSize(sz);
        r.moveTopLeft({(geom.width() - r.width()) / 2, (geom.height() - r.height()) / 2});
        this->setGeometry(r);
        this->move(r.x(), r.y());
        this->resize(r.width(), r.height());
#ifdef __aarch64
        _nwComm->syncPosition(r);
#endif
    } else {
        QRect r = this->geometry();
        r.setSize(sz);
        this->setGeometry(r);
        this->move(r.x(), r.y());
        this->resize(r.width(), r.height());
#ifdef __aarch64
        _nwComm->syncPosition();
#endif
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    auto m = size();

    if (_miniMode) {
        m = QSize(40, 40);
    } else {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto dRect = DApplication::desktop()->availableGeometry();
            auto sz = _engine->videoSize();
            if (sz.width() == 0 || sz.height() == 0) {
                m = QSize(614, 500);
            } else {
                qreal ratio = (qreal)sz.width() / sz.height();
                if (sz.width() > sz.height()) {
                    int w = 500 * ratio;
//                    if (w > dRect.width()) {
//                        w = dRect.width();
//                    }
                    m = QSize(w, 500);
                } else {
                    int h = 614 / ratio;
                    if (h > dRect.height()) {
                        h = dRect.height();
                    }
                    m = QSize(614, h);
                }
            }
        } else {
            m = QSize(614, 500);
        }
        m = QSize(614, 500);
    }

    qDebug() << __func__ << m;
    this->setMinimumSize(m);
}

void MainWindow::updateGeometryNotification(const QSize &sz)
{
    auto msg = QString("%1x%2").arg(sz.width()).arg(sz.height());
    if (_engine->state() != PlayerEngine::CoreState::Idle) {
        _nwComm->updateWithMessage(msg);
    }

    if (windowState() == Qt::WindowNoState && !_miniMode) {
        _lastRectInNormalMode = geometry();
    }
}

void MainWindow::LimitWindowize()
{
    if (!_miniMode && (geometry().width() == 380 || geometry().height() == 380)) {
        setGeometry(_lastRectInNormalMode);
    }
}

void MainWindow::resizeEvent(QResizeEvent *ev)
{
    qDebug() << __func__ << geometry();
    if (isFullScreen()) {
        _progIndicator->move(geometry().width() - _progIndicator->width() - 18, 8);
    }
    // modify 4.1  Limit video to mini mode size by thx
    LimitWindowize();

    //2020.4.30前重新实现 xpf
    updateSizeConstraints();
    updateProxyGeometry();
    QTimer::singleShot(0, [ = ]() {
        updateWindowTitle();
    });

    updateGeometryNotification(geometry().size());
}

void MainWindow::updateWindowTitle()
{
    if (_engine->state() != PlayerEngine::Idle) {
        const auto &mi = _engine->playlist().currentInfo().mi;
        auto title = _titlebar->fontMetrics().elidedText(mi.title,
                                                         Qt::ElideMiddle, _titlebar->contentsRect().width() - 400);
        _titlebar->setTitletxt(title);
        if (!CompositingManager::get().composited()) {
            _titlebar->setTitleBarBackground(false);
        } else {
            _titlebar->setTitleBarBackground(true);
        }
    } else {
        _titlebar->setTitletxt(QString());
        _titlebar->setTitleBarBackground(false);
    }
    _titlebar->setProperty("idle", _engine->state() == PlayerEngine::Idle);
//    _titlebar->setStyleSheet(styleSheet());
}

void MainWindow::moveEvent(QMoveEvent *ev)
{
#ifdef __aarch64__
    if (windowState() == Qt::WindowNoState && !_miniMode) {
        _lastRectInNormalMode = geometry();
    }
    _nwComm->syncPosition();
#elif  __mips__
    _nwComm->syncPosition();
#else
    updateGeometryNotification(geometry().size());
#endif
}

void MainWindow::keyPressEvent(QKeyEvent *ev)
{
    if ((_playlist->state() == PlaylistWidget::Opened) && ev->modifiers() == Qt::NoModifier) {
        if (ev) {
            _playlist->updateSelectItem(ev->key());
        }
        ev->setAccepted(true);
    }

    QWidget::keyPressEvent(ev);
}

void MainWindow::keyReleaseEvent(QKeyEvent *ev)
{
    QWidget::keyReleaseEvent(ev);
}

void MainWindow::capturedMousePressEvent(QMouseEvent *me)
{
    _mouseMoved = false;
#ifdef __aarch64__
    _nwComm->hide();
#elif __mips__
    _nwComm->hide();
#endif
    if (qApp->focusWindow() == 0) return;

    if (me->buttons() == Qt::LeftButton) {
        _mousePressed = true;
    }
}

void MainWindow::capturedMouseReleaseEvent(QMouseEvent *me)
{
    if (_delayedResizeByConstraint) {
        _delayedResizeByConstraint = false;

        QTimer::singleShot(0, [ = ]() {
            this->setMinimumSize({0, 0});
            this->resizeByConstraints(true);
        });
    }
}

static bool _afterDblClick = false;
void MainWindow::mousePressEvent(QMouseEvent *ev)
{
    _mouseMoved = false;
#ifdef __aarch64__
    _nwComm->hide();
#elif __mips__
    _nwComm->hide();
#endif

    if (qApp->focusWindow() == 0) return;
    if (ev->buttons() == Qt::LeftButton) {
        _mousePressed = true;
        /*if (_playState->isVisible()) {
            //_playState->setState(DImageButton::Press);
            QMouseEvent me(QEvent::MouseButtonPress, {}, ev->button(), ev->buttons(), ev->modifiers());
            qApp->sendEvent(_playState, &me);
        }*/
    }
}


void MainWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!_miniMode && !_inBurstShootMode) {
        _delayedMouseReleaseTimer.stop();
        if (_engine->state() == PlayerEngine::Idle) {
            requestAction(ActionFactory::StartPlay);
        } else {
            requestAction(ActionFactory::ToggleFullscreen, false, {}, true);
        }
        ev->accept();
        _afterDblClick = true;
    }
}

bool MainWindow::insideToolsArea(const QPoint &p)
{
    return _titlebar->geometry().contains(p) || _toolbox->geometry().contains(p);
}

QMargins MainWindow::dragMargins() const
{
    return QMargins {MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS};
}

bool MainWindow::insideResizeArea(const QPoint &global_p)
{
    const QRect window_visible_rect = frameGeometry() - dragMargins();
    return !window_visible_rect.contains(global_p);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!_mousePressed) {
        _afterDblClick = false;
        _mouseMoved = false;
    }

    if (qApp->focusWindow() == 0 || !_mousePressed) return;

    _mousePressed = false;
    /*if (_playState->isVisible()) {
        //QMouseEvent me(QEvent::MouseButtonRelease, {}, ev->button(), ev->buttons(), ev->modifiers());
        //qApp->sendEvent(_playState, &me);
    //        _playState->setState(DImageButton::Normal);
    }*/

    // dtk has a bug, DImageButton propagates mouseReleaseEvent event when it responded to.
    if (!insideResizeArea(ev->globalPos()) && !_mouseMoved && (_playlist->state() != PlaylistWidget::Opened)) {
        if (!insideToolsArea(ev->pos())) {
            _delayedMouseReleaseTimer.start(120);
        } else {
            if (_engine->state() == PlayerEngine::CoreState::Idle) {
                _delayedMouseReleaseTimer.start(120);
            }
        }
    }

    Utility::cancelWindowMoveResize(winId());
    _mouseMoved = false;
}

void MainWindow::delayedMouseReleaseHandler()
{
    if (!_afterDblClick)
        requestAction(ActionFactory::TogglePause, false, {}, true);
    _afterDblClick = false;
}

void MainWindow::onDvdData(const QString &title)
{
    auto mi = _engine->playlist().currentInfo().mi;

    if ("dvd open failed" == title) {
        mi.valid = false;
    } else {
        mi.title = title;
        if (mi.title.isEmpty()) {
            mi.title = "DVD";
        }
        mi.valid = true;
    }

    if (!mi.valid) {
        _nwDvd->setVisible(false);
        auto msg = QString(tr("No video file found"));
        _nwComm->updateWithMessage(msg);

        if (m_dvdUrl.scheme().startsWith("dvd")) {
            int cur = _engine->playlist().indexOf(m_dvdUrl);
            _engine->playlist().remove(cur);
        }
        return;
    }
    PlayItemInfo info = _engine->playlist().currentInfo();
    _engine->playByName(info.url);
}

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
    if (_mouseMoved) {
        return Utility::updateMousePointForWindowMove(this->winId(), ev->globalPos() * devicePixelRatioF());
    }

    _mouseMoved = true;

    if (windowState() == Qt::WindowNoState || isMaximized()) {
        Utility::startWindowSystemMove(this->winId());
    }
    QWidget::mouseMoveEvent(ev);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    if (_miniMode || _inBurstShootMode)
        return;

    if (insideToolsArea(cme->pos()))
        return;

    resumeToolsWindow();
    QTimer::singleShot(0, [ = ]() {
        qApp->restoreOverrideCursor();
        ActionFactory::get().mainContextMenu()->popup(QCursor::pos());
    });
    cme->accept();
}

void MainWindow::prepareSplashImages()
{
    bg_dark = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
    bg_light = utils::LoadHiDPIImage(":/resources/icons/light/init-splash.svg");
}

void MainWindow::saveWindowState()
{
//    QSettings settings;
//    settings.beginGroup(objectName());
//    settings.setValue("geometry", saveGeometry());
//    settings.endGroup();
}

void MainWindow::loadWindowState()
{
//    QSettings settings;
//    settings.beginGroup(objectName());
//    restoreGeometry(settings.value("geometry", saveGeometry()).toByteArray());
    //    settings.endGroup();
}

void MainWindow::subtitleMatchVideo(const QString &fileName)
{
    auto videoName = fileName;
    // Search for video files with the same name as the subtitles and play the video file.
    QFileInfo subfileInfo(fileName);
    QDir dir(subfileInfo.canonicalPath());
    dir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Size | QDir::Reversed);
    QStringList videofile_suffixs = _engine->video_filetypes;
    dir.setNameFilters(videofile_suffixs);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo info = list.at(i);
        qDebug() << info.absoluteFilePath() << endl;
//        if (info.completeBaseName() == subfileInfo.completeBaseName()) {
        if (subfileInfo.fileName().contains(info.completeBaseName())) {
            videoName = info.absoluteFilePath();
        } else {
            videoName = nullptr;
        }
    }

    QFileInfo vfileInfo(videoName);
    if (vfileInfo.exists()) {
        Settings::get().setGeneralOption("last_open_path", vfileInfo.path());

        play(QUrl::fromLocalFile(videoName));

        // Select the current subtitle display
        const PlayingMovieInfo &pmf = _engine->playingMovieInfo();
        for (const SubtitleInfo &sub : pmf.subs) {
            if (sub["external"].toBool()) {
                QString path = sub["external-filename"].toString();
                if (path == subfileInfo.canonicalFilePath()) {
                    _engine->selectSubtitle(pmf.subs.indexOf(sub));
                    break;
                }
            }
        }
    } else {
        _nwComm->updateWithMessage(tr("Please load the video first"));
    }
}

void MainWindow::defaultplaymodeinit()
{
    auto mode_opt = Settings::get().settings()->option("base.play.playmode");
    auto mode_id = mode_opt->value().toInt();
    auto mode = mode_opt->data("items").toStringList()[mode_id];
    if (mode == tr("Order play")) {
        requestAction(ActionFactory::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (mode == tr("Shuffle play")) {
        requestAction(ActionFactory::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (mode == tr("Single play")) {
        requestAction(ActionFactory::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (mode == tr("Single loop")) {
        requestAction(ActionFactory::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (mode == tr("List loop")) {
        requestAction(ActionFactory::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
}

void MainWindow::readSinkInputPath()
{
    QVariant v = ApplicationAdaptor::redDBusProperty("com.deepin.daemon.Audio", "/com/deepin/daemon/Audio",
                                                     "com.deepin.daemon.Audio", "SinkInputs");

    if (!v.isValid())
        return;

    QList<QDBusObjectPath> allSinkInputsList = v.value<QList<QDBusObjectPath> >();
//    qDebug() << "allSinkInputsListSize: " << allSinkInputsList.size();

    for (auto curPath : allSinkInputsList) {
//        qDebug() << "path: " << curPath.path();

        QVariant nameV = ApplicationAdaptor::redDBusProperty("com.deepin.daemon.Audio", curPath.path(),
                                                             "com.deepin.daemon.Audio.SinkInput", "Name");

        if (!nameV.isValid() || (!nameV.toString().contains( "mpv", Qt::CaseInsensitive) && !nameV.toString().contains("deepin-movie", Qt::CaseInsensitive)))
            continue;

        sinkInputPath = curPath.path();
        break;
    }
}

void MainWindow::setAudioVolume(int volume)
{
    double tVolume = 0.0;
    if (volume == 100 ) {
        tVolume = (volume ) / 100.0 ;
        //tVolume += 0.000005;
    } else if (volume != 0 ) {
        tVolume = (volume + 1) / 100.0 ;
        //tVolume += 0.000005;
    }

    readSinkInputPath();

    if (!sinkInputPath.isEmpty()) {
        QDBusInterface ainterface("com.deepin.daemon.Audio", sinkInputPath,
                                  "com.deepin.daemon.Audio.SinkInput", QDBusConnection::sessionBus());
        if (!ainterface.isValid()) {
            return;
        }
        //调用设置音量
        ainterface.call(QLatin1String("SetVolume"), tVolume, false);

        if (qFuzzyCompare(tVolume, 0.0))
            ainterface.call(QLatin1String("SetMute"), true);

        //获取是否静音
        QVariant muteV = ApplicationAdaptor::redDBusProperty("com.deepin.daemon.Audio", sinkInputPath,
                                                             "com.deepin.daemon.Audio.SinkInput", "Mute");
        if (tVolume > 0.0) {
            if (muteV.toBool())
                ainterface.call(QLatin1String("SetMute"), false);
        } else if (tVolume < 0.01 && !muteV.toBool())
            ainterface.call(QLatin1String("SetMute"), true);
    }
}

void MainWindow::setMusicMuted(bool muted)
{
    readSinkInputPath();
    if (!sinkInputPath.isEmpty()) {
        QDBusInterface ainterface("com.deepin.daemon.Audio", sinkInputPath,
                                  "com.deepin.daemon.Audio.SinkInput",
                                  QDBusConnection::sessionBus());
        if (!ainterface.isValid()) {
            return;
        }

        //调用设置音量
        ainterface.call(QLatin1String("SetMute"), muted);
    }

    return;
}


QString MainWindow::lastOpenedPath()
{
    QString lastPath = Settings::get().generalOption("last_open_path").toString();
    QDir lastDir(lastPath);
    if (lastPath.isEmpty() || !lastDir.exists()) {
        lastPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir newLastDir(lastPath);
        if (!newLastDir.exists()) {
            lastPath = QDir::currentPath();
        }
    }

    return lastPath;
}

void MainWindow::paintEvent(QPaintEvent *pe)
{
    QPainter painter(this);
//    painter.setRenderHint(QPainter::Antialiasing);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    QColor bgColor = pal.color(QPalette::Window);

//    QPainterPath path;
//    path.addRoundedRect(bgRect, 18, 18);
//    // drawbackground color
//    painter.setRenderHint(QPainter::Antialiasing, true);
//    painter.fillPath(path, bgColor);
//    painter.setRenderHint(QPainter::Antialiasing, false);

    QImage &bg = bg_dark;
//    bool rounded = !isFullScreen() && !isMaximized();
//    if (rounded) {
//        QPainterPath pp;
//        pp.addRoundedRect(bgRect, RADIUS, RADIUS);
//        painter.fillPath(pp, bgColor);

//        {
//            auto view_rect = bgRect.marginsRemoved(QMargins(1, 1, 1, 1));
//            QPainterPath pp;
//            pp.addRoundedRect(view_rect, RADIUS, RADIUS);
//            painter.fillPath(pp, bgColor);
//        }
//    } else {
//        QPainterPath pp;
//        pp.addRect(bgRect);
//        painter.fillPath(pp, bgColor);
//    }
    auto pt = bgRect.center() - QPoint(bg.width() / 2, bg.height() / 2) / devicePixelRatioF();
    painter.drawImage(pt, bg);

    /*
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        bool light = ("light" == qApp->theme());
        auto bg_clr = QColor(16, 16, 16);
        QImage& bg = bg_dark;
        if (light) {
            bg = bg_light;
            bg_clr = QColor(252, 252, 252);
        }

    #ifdef USE_DXCB
        QPainterPath pp;
        pp.addRect(rect());
        p.fillPath(pp, bg_clr);
    #else
        bool rounded = !isFullScreen() && !isMaximized();

        p.fillRect(rect(), Qt::transparent);

        if (rounded) {
            QPainterPath pp;
            pp.addRoundedRect(rect(), RADIUS, RADIUS);
            p.fillPath(pp, QColor(0, 0, 0, light ? 255 * 0.1: 255));

            {
                auto view_rect = rect().marginsRemoved(QMargins(1, 1, 1, 1));
                QPainterPath pp;
                pp.addRoundedRect(view_rect, RADIUS, RADIUS);
                p.fillPath(pp, bg_clr);
            }
        } else {
            QPainterPath pp;
            pp.addRect(rect());
            p.fillPath(pp, bg_clr);
        }

    #endif

        auto pt = rect().center() - QPoint(bg.width()/2, bg.height()/2)/devicePixelRatioF();
        p.drawImage(pt, bg);
    */
}

void MainWindow::toggleUIMode()
{

    //判断窗口是否靠边停靠（靠边停靠不支持MINI模式）thx
    QRect deskrect = QApplication::desktop()->availableGeometry();
    QPoint windowPos = pos();
    if (this->geometry() != deskrect) {
        if (windowPos.x() == 0 && (windowPos.y() == 0 ||
                                   (abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50))) {
            if (abs(this->geometry().width() - deskrect.width() / 2 ) < 50) {
                _nwComm->updateWithMessage(tr("Please exit smart dock"));
                return ;
            }

        }
        if ( (abs(windowPos.x() + this->geometry().width() - deskrect.width()) < 50)  &&
                (windowPos.y()  == 0 || abs(windowPos.y() + this->geometry().height() - deskrect.height() ) < 50 )) {
            if (abs(this->geometry().width() - deskrect.width() / 2) < 50) {
                _nwComm->updateWithMessage(tr("Please exit smart dock"));
                return ;
            }
        }
    }

    _miniMode = !_miniMode;
    qDebug() << __func__ << _miniMode;

    if (_miniMode) {
        _titlebar->titlebar()->setDisableFlags(Qt::WindowMaximizeButtonHint);
    } else {
        _titlebar->titlebar()->setDisableFlags(0);
    }
    if (_listener) _listener->setEnabled(!_miniMode);

    _titlebar->setVisible(!_miniMode);
    //_toolbox->setVisible(!_miniMode);

    _miniPlayBtn->setVisible(_miniMode);
    _miniCloseBtn->setVisible(_miniMode);
    _miniQuitMiniBtn->setVisible(_miniMode);

    _miniPlayBtn->setEnabled(_miniMode);
    _miniCloseBtn->setEnabled(_miniMode);
    _miniQuitMiniBtn->setEnabled(_miniMode);



    resumeToolsWindow();

    if (_miniMode) {

        updateSizeConstraints();
        syncPlayState();
        //设置等比缩放
        setEnableSystemResize(false);


        _stateBeforeMiniMode = SBEM_None;

        if (!_windowAbove) {
            _stateBeforeMiniMode |= SBEM_Above;
            requestAction(ActionFactory::WindowAbove);
        }

        if (_playlist->state() == PlaylistWidget::Opened) {
            _stateBeforeMiniMode |= SBEM_PlaylistOpened;
            requestAction(ActionFactory::TogglePlaylist);
        }

        if (isFullScreen()) {
            _stateBeforeMiniMode |= SBEM_Fullscreen;
            requestAction(ActionFactory::QuitFullscreen);
            reflectActionToUI(ActionFactory::ToggleMiniMode);
        } else if (isMaximized()) {
            _stateBeforeMiniMode |= SBEM_Maximized;
            showNormal();
        } else {
            _lastRectInNormalMode = geometry();
        }

        auto sz = QSize(380, 380);
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto vid_size = _engine->videoSize();
            qreal ratio = vid_size.width() / (qreal)vid_size.height();

            if (vid_size.width() > vid_size.height()) {
                sz = QSize(380, 380 / ratio);
            } else {
                sz = QSize(380 * ratio, 380);
            }
        }

        QRect geom = {0, 0, 0, 0};
        if (_lastRectInNormalMode.isValid()) {
            geom = _lastRectInNormalMode;
        }

        geom.setSize(sz);
        setGeometry(geom);
        if (geom.x() < 0 ) {
            geom.moveTo(0, geom.y());
        }
        if (geom.y() < 0 ) {
            geom.moveTo(geom.x(), 0);
        }


        move(geom.x(), geom.y());
        resize(geom.width(), geom.height());

        _miniPlayBtn->move(sz.width() - 12 - _miniPlayBtn->width(),
                           sz.height() - 10 - _miniPlayBtn->height());
        _miniCloseBtn->move(sz.width() - 15 - _miniCloseBtn->width(), 10);
        _miniQuitMiniBtn->move(14, sz.height() - 10 - _miniQuitMiniBtn->height());


    } else {
        setEnableSystemResize(true);
        if (_stateBeforeMiniMode & SBEM_Above) {
            requestAction(ActionFactory::WindowAbove);
        }
        if (_stateBeforeMiniMode & SBEM_Maximized) {
            showMaximized();
        } else if (_stateBeforeMiniMode & SBEM_Fullscreen) {
            requestAction(ActionFactory::ToggleFullscreen);
        } else {
            if (_engine->state() == PlayerEngine::Idle && windowState() == Qt::WindowNoState) {
//                this->setMinimumSize(QSize(1070, 680));
                this->resize(850, 600);
            } else {
                if (_lastRectInNormalMode.isValid() && _engine->videoRotation() == 0) {
                    resize(_lastRectInNormalMode.size());
                } else {
                    resizeByConstraints();
                }
            }
        }

        syncPlayState();

        if (_stateBeforeMiniMode & SBEM_PlaylistOpened &&
                _playlist->state() == PlaylistWidget::Closed) {
            if (_stateBeforeMiniMode & SBEM_Fullscreen) {
                QTimer::singleShot(100, [ = ]() {
                    requestAction(ActionFactory::TogglePlaylist);
                });
            }
        }

        _stateBeforeMiniMode = SBEM_None;
    }
}

void MainWindow::miniButtonClicked(QString id)
{
    qDebug() << id;
    if (id == "play") {
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            requestAction(ActionFactory::ActionKind::TogglePause);
        }

    } else if (id == "close") {
        close();

    } else if (id == "quit_mini") {
        requestAction(ActionFactory::ActionKind::ToggleMiniMode);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *ev)
{
    qDebug() << ev->mimeData()->formats();
    if (!ev->mimeData()->hasUrls()) {
        return;
    }

    QList<QUrl> urls = ev->mimeData()->urls();
    QList<QUrl> valids = _engine->addPlayFiles(urls);

    if (urls.count() == 1 && valids.count() == 0) {
        // check if the dropped file is a subtitle.
        QFileInfo fileInfo(urls.first().toLocalFile());
        if (_engine->subtitle_suffixs.contains(fileInfo.suffix())) {
            // notice that the file loaded but won't automatically selected.
//            const PlayingMovieInfo &pmf = _engine->playingMovieInfo();
//            for (const SubtitleInfo &sub : pmf.subs) {
//                if (sub["external"].toBool()) {
//                    QString path = sub["external-filename"].toString();
//                    if (path == fileInfo.canonicalFilePath()) {
//                        _engine->selectSubtitle(pmf.subs.indexOf(sub));
//                        break;
//                    }
//                }
//            }

//            QPixmap icon = utils::LoadHiDPIPixmap(QString(":/resources/icons/%1.svg").arg(succ ? "success" : "fail"));
//            _nwComm->popupWithIcon(succ ? tr("Load successfully") : tr("Load failed"), icon);

            // Search for video files with the same name as the subtitles and play the video file.
            if (_engine->state() == PlayerEngine::Idle)
                subtitleMatchVideo(urls.first().toLocalFile());
            else {
                bool succ = _engine->loadSubtitle(fileInfo);
                _nwComm->updateWithMessage(succ ? tr("Load successfully") : tr("Load failed"));
            }

            return;
        }
    }

    {
        auto all = urls.toSet();
        auto accepted = valids.toSet();
        auto invalids = all.subtract(accepted).toList();
        int ms = 0;
        for (const auto &url : invalids) {
            QTimer::singleShot(ms, [ = ]() {
                auto msg = QString(tr("Invalid file: %1").arg(url.fileName()));
                _nwComm->updateWithMessage(msg);
            });

            ms += 1000;
        }
    }

    if (valids.size()) {
        if (valids.size() == 1) {
            _engine->playByName(valids[0]);
        } else {
            _engine->playByName(QUrl("playlist://0"));
        }
    }
    ev->acceptProposedAction();
}

void MainWindow::setInit(bool v)
{
    if (_inited != v) {
        _inited = v;
        emit initChanged();
    }
}

QString MainWindow::probeCdromDevice()
{
    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        return QString();
    }
    do {
        QString strLine = mountFile.readLine();
        if ( strLine.indexOf("/dev/sr") != -1 || strLine.indexOf("/dev/cdrom") != -1) { //说明存在光盘的挂载。
            return strLine.split(" ").at(0);        //A B C 这样的格式，取部分
        }
    } while (!mountFile.atEnd() );
    return QString();
}

void MainWindow::updateMiniBtnTheme(int a)
{
#ifdef __mips__
    dynamic_cast<IconButton *>(_miniPlayBtn)->changeTheme(a);
    dynamic_cast<IconButton *>(_miniCloseBtn)->changeTheme(a);
    dynamic_cast<IconButton *>(_miniQuitMiniBtn)->changeTheme(a);
#endif
}

#include "mainwindow.moc"
