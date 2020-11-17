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
 * In addition, as a special exception, the copyright hemiters give
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
#include "vendor/movieapp.h"

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

#include <QtX11Extras/QX11Info>

#include <X11/cursorfont.h>
#include <X11/Xlib.h>

//add by heyi
//#define _NET_WM_MOVERESIZE_MOVE              8   /* movement only */
//#define _NET_WM_MOVERESIZE_CANCEL           11   /* cancel operation */

#define XATOM_MOVE_RESIZE "_NET_WM_MOVERESIZE"
#define XDEEPIN_BLUR_REGION "_NET_WM_DEEPIN_BLUR_REGION"
#define XDEEPIN_BLUR_REGION_ROUNDED "_NET_WM_DEEPIN_BLUR_REGION_ROUNDED"

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

const char kAtomNameHidden[] = "_NET_WM_STATE_HIDDEN";
const char kAtomNameFullscreen[] = "_NET_WM_STATE_FULLSCREEN";
const char kAtomNameMaximizedHorz[] = "_NET_WM_STATE_MAXIMIZED_HORZ";
const char kAtomNameMaximizedVert[] = "_NET_WM_STATE_MAXIMIZED_VERT";
const char kAtomNameMoveResize[] = "_NET_WM_MOVERESIZE";
const char kAtomNameWmState[] = "_NET_WM_STATE";
const char kAtomNameWmStateAbove[] = "_NET_WM_STATE_ABOVE";
const char kAtomNameWmStateStaysOnTop[] = "_NET_WM_STATE_STAYS_ON_TOP";
const char kAtomNameWmSkipTaskbar[] = "_NET_WM_STATE_SKIP_TASKBAR";
const char kAtomNameWmSkipPager[] = "_NET_WM_STATE_SKIP_PAGER";

#define AUTOHIDE_TIMEOUT 2000
#include <DToast>
DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 6

int MainWindow::_retryTimes = 0;

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
    icon->setIcon(QIcon(":resources/icons/select-normal.svg"));
    icon->setFixedHeight(21);
    layout->addWidget(le);
    layout->addWidget(icon);

    /**
     * createTwoColumWidget在dtk中已被弃用
     * 修改警告重新创建窗口
     */
    //DSettingsWidgetFactory *settingWidget = new DSettingsWidgetFactory(main);
    //auto optionWidget = DSettingsWidgetFactory::createTwoColumWidget(option, main);

    auto optionWidget = new QWidget;
    optionWidget->setObjectName("OptionFrame");

    auto optionLayout = new QFormLayout(optionWidget);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(0);

    main->setMinimumWidth(240);
    optionLayout->addRow(new DLabel(QObject::tr(option->name().toStdString().c_str())), main);

    //auto optionWidget = settingWidget->createWidget(option);
    workaround_updateStyle(optionWidget, "light");

    DDialog *prompt = new DDialog(main);
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
        QString name = DFileDialog::getExistingDirectory(nullptr, QObject::tr("Open folder"),
                                                         MainWindow::lastOpenedPath(),
                                                         DFileDialog::ShowDirsOnly | DFileDialog::DontResolveSymlinks);
        if (validate(name, false)) {
            option->setValue(name);
            nameLast = name;
        }
        QFileInfo fileinfo(name);
        if ((!fileinfo.isReadable() || !fileinfo.isWritable()) && !name.isEmpty()) {
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

#ifdef USE_DXCB
class MainWindowFocusMonitor: public QAbstractNativeEventFilter
{
public:
    explicit MainWindowFocusMonitor(MainWindow *src) : QAbstractNativeEventFilter(), _source(src)
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
#endif


#ifndef USE_DXCB
//窗管返回事件过滤器
class MainWindowPropertyMonitor: public QAbstractNativeEventFilter
{
public:
    explicit MainWindowPropertyMonitor(MainWindow *src)
        : QAbstractNativeEventFilter(), _mw(src)
    {
        //安装事件过滤器
        qApp->installNativeEventFilter(this);
    }

    ~MainWindowPropertyMonitor()
    {
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *)
    {
        xcb_generic_event_t *xevent = (xcb_generic_event_t *)message;
        uint response_type = xevent->response_type & ~0x80;
        if(XCB_PROPERTY_NOTIFY == response_type)
        {
            auto propertyNotify = reinterpret_cast<xcb_property_notify_event_t *>(xevent);
            //经过观察发现【专业版】设置窗口总在最前会返回一个351、一个483和一堆327
            //目前先按照此方法修改，后期在个人版和社区版可能会存在问题
            //个人版参考378
            //p.s. 360推测是鼠标点击事件，327推测窗口刷新事件
            switch (propertyNotify->atom) {
            case 351:
                m_start = true;
                break;
            case 483:
                break;
            case 327:
                m_start = false;
                if (!m_list.isEmpty() && m_list.size() == 2) {
                    //判断是否符合标志位
                    QList<unsigned int> temp {351, 483};
                    if (m_list == temp) {
                        //切换窗口置顶，此处需注意，应用切换可以传递至窗管，窗管无法传递至应用
                        //所以此处只需单向传递即可
                        _mw->requestAction(ActionFactory::ActionKind::WindowAbove);
                    }
                }
                m_list.clear();
                break;
            default:
                break;
            }
            if (m_start) {
                m_list << propertyNotify->atom;
            }
        }
        return false;
    }

    MainWindow *_mw {nullptr};
    xcb_atom_t _atomWMState;
    QList<unsigned int> m_list;
    bool m_start {false};
};
#endif

class MainWindowEventListener : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowEventListener(QWidget *target)
        : QObject(target), _window(target->windowHandle())
    {
        lastCornerEdge = CornerEdge::NoneEdge;
    }

    ~MainWindowEventListener() override
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

        switch (static_cast<int>(event->type()))
        {
        case QEvent::MouseButtonPress: {
            if (!enabled) return false;
            QMouseEvent *e = static_cast<QMouseEvent *>(event);
            setLeftButtonPressed(true);
            auto mw = static_cast<MainWindow *>(parent());
            if (mw->insideResizeArea(e->globalPos()) && lastCornerEdge != CornerEdge::NoneEdge)
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
                //add by heyi  拦截鼠标移动事件
                mw->judgeMouseInWindow(QCursor::pos());
                //                if (mw->insideResizeArea(e->globalPos())) {
                CornerEdge mouseCorner = CornerEdge::NoneEdge;
                QRect cornerRect;

                /// begin set cursor corner type
                cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                cornerRect.moveTopLeft(_window->frameGeometry().topLeft());
                if (cornerRect.contains(e->globalPos())) {
                    mouseCorner = CornerEdge::TopLeftCorner;
                    goto set_cursor;
                }

                cornerRect.moveTopRight(_window->frameGeometry().topRight());
                if (cornerRect.contains(e->globalPos())) {
                    mouseCorner = CornerEdge::TopRightCorner;
                    goto set_cursor;
                }

                cornerRect.moveBottomRight(_window->frameGeometry().bottomRight());
                if (cornerRect.contains(e->globalPos())) {
                    mouseCorner = CornerEdge::BottomRightCorner;
                    goto set_cursor;
                }

                cornerRect.moveBottomLeft(_window->frameGeometry().bottomLeft());
                if (cornerRect.contains(e->globalPos())) {
                    mouseCorner = CornerEdge::BottomLeftCorner;
                    goto set_cursor;
                }

                goto skip_set_cursor; // disable edges

                /// begin set cursor edge type
                if (e->globalX() <= window_visible_rect.x()) {
                    mouseCorner = CornerEdge::LeftEdge;
                } else if (e->globalX() < window_visible_rect.right()) {
                    if (e->globalY() <= window_visible_rect.y()) {
                        mouseCorner = CornerEdge::TopEdge;
                    } else if (e->globalY() >= window_visible_rect.bottom()) {
                        mouseCorner = CornerEdge::BottomEdge;
                    } else {
                        goto skip_set_cursor;
                    }
                } else if (e->globalX() >= window_visible_rect.right()) {
                    mouseCorner = CornerEdge::RightEdge;
                } else {
                    goto skip_set_cursor;
                }
set_cursor:
#ifdef USE_DXCB
#ifdef __mips__
                if (window->property("_d_real_winId").isValid()) {
                    auto real_wid = window->property("_d_real_winId").toUInt();
                    Utility::setWindowCursor(real_wid, mouseCorner);
                } else {
                    Utility::setWindowCursor(static_cast<quint32>(window->winId()), mouseCorner);
                }
#endif
#endif

                if (qApp->mouseButtons() == Qt::LeftButton) {
                    updateGeometry(mouseCorner, e);
                }
                lastCornerEdge = mouseCorner;
                return true;

skip_set_cursor:
                lastCornerEdge = mouseCorner = CornerEdge::NoneEdge;
                return false;
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

        if (!pressed) {
#ifdef USE_DXCB
            Utility::cancelWindowMoveResize(static_cast<quint32>(_window->winId()));
#endif
        }

        leftButtonPressed = pressed;
    }

    void updateGeometry(CornerEdge edge, QMouseEvent *e)
    {
        auto mw = static_cast<MainWindow *>(parent());
        mw->updateGeometry(edge, e->globalPos());
    }

    bool leftButtonPressed {false};
    bool startResizing {false};
    bool enabled {true};
    bool ttt{false};//内存对齐
    CornerEdge lastCornerEdge;
    QWindow *_window;
};

#ifdef USE_DXCB
/// shadow
#define SHADOW_COLOR_NORMAL QColor(0, 0, 0, 255 * 0.35)
#define SHADOW_COLOR_ACTIVE QColor(0, 0, 0, 255 * 0.6)
#endif

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(nullptr)
{
    //add bu heyi
    this->setAttribute(Qt::WA_AcceptTouchEvents);
    _mousePressTimer.setInterval(1300);
    connect(&_mousePressTimer, &QTimer::timeout, this, &MainWindow::slotmousePressTimerTimeOut);

    m_lastVolume = Settings::get().internalOption("last_volume").toInt();
    bool composited = CompositingManager::get().composited();
    qDebug() << "composited = " << composited;

#ifdef USE_DXCB
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#else
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MACOS
    setWindowFlags(Qt::WindowFullscreenButtonHint);
#endif
#endif
    setAcceptDrops(true);

    if (composited) {
        //setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, false);
    }

#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
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
    if (volume > 100) {
        Settings::get().setInternalOption("global_volume", 100);
        volume = 100;
    }

    m_displayVolume = volume;

    if(utils::check_wayland_env()){
        _engine->changeVolume(100);
        if (Settings::get().internalOption("mute").toBool()) {
            _engine->toggleMute();
            Settings::get().setInternalOption("mute", _engine->muted());
        }
    }

    _toolbox = new ToolboxProxy(this, _engine);
    _toolbox->setFocusPolicy(Qt::NoFocus);

    titlebar()->deleteLater();

    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::slotPlayerStateChanged);
    connect(ActionFactory::get().mainContextMenu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);

    connect(this, &MainWindow::frameMenuEnable, &ActionFactory::get(), &ActionFactory::frameMenuEnable);
    connect(this, &MainWindow::playSpeedMenuEnable, &ActionFactory::get(), &ActionFactory::playSpeedMenuEnable);
    connect(qApp, &QGuiApplication::focusWindowChanged, this, &MainWindow::slotFocusWindowChanged);

#ifndef __mips__
    _progIndicator = new MovieProgressIndicator(this);
    _progIndicator->setVisible(false);
    connect(_engine, &PlayerEngine::elapsedChanged, [ = ]() {
        if (!_isJinJia) {
            _progIndicator->updateMovieProgress(_engine->duration(), _engine->elapsed());
        } else {
            _progIndicator->updateMovieProgress(oldDuration, oldElapsed);
        }
        //及时刷新_isFileLoadNotFinished状态
        if(_isFileLoadNotFinished && utils::check_wayland_env()){
            qDebug()<<"_isFileLoadNotFinished = false";
            _isFileLoadNotFinished = false;
        }
    });
#endif

    // mini ui
    auto *signalMapper = new QSignalMapper(this);
    connect(signalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped), this, &MainWindow::miniButtonClicked);

#ifdef __mips__
    _miniPlayBtn = new IconButton(this);
    _miniCloseBtn = new IconButton(this);
    _miniQuitMiniBtn = new IconButton(this);

    dynamic_cast<IconButton *>(_miniPlayBtn)->setFlat(true);
    dynamic_cast<IconButton *>(_miniCloseBtn)->setFlat(true);
    dynamic_cast<IconButton *>(_miniQuitMiniBtn)->setFlat(true);
#else
    _miniPlayBtn = new DIconButton(this);
    _miniQuitMiniBtn = new DIconButton(this);
    _miniCloseBtn = new DIconButton(this);

    if (!composited) {
        _labelCover = new QLabel(this);
        _labelCover->setFixedSize(QSize(30, 30));
        _labelCover->setVisible(_miniMode);
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(255, 255, 255));
        _labelCover->setAutoFillBackground(true);
        _labelCover->setPalette(palette);
    }

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
#ifndef __mips__
        if (_engine->state() == PlayerEngine::CoreState::Idle) {
            _fullscreentimelable->close();
            _progIndicator->setVisible(false);
            emit frameMenuEnable(false);
            emit playSpeedMenuEnable(false);
        }
#endif
        if (_engine->state() == PlayerEngine::CoreState::Playing) {
#ifndef __mips__
            if(m_bIsFullSreen)
                _fullscreentimelable->show();
#endif
            _miniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/pause-normal-mini.svg"));
            _miniPlayBtn->setObjectName("MiniPauseBtn");

            emit frameMenuEnable(true);
            emit playSpeedMenuEnable(true);
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

    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);

    ShortcutManager::get().buildBindings();

    connect(_engine, SIGNAL(stateChanged()), this, SLOT(update()));
    connect(_engine, &PlayerEngine::tracksChanged, this, &MainWindow::updateActionsState);
    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::updateActionsState);
    updateActionsState();

    reflectActionToUI(ActionFactory::ActionKind::OneTimes); //重置播放速度为1倍速
    reflectActionToUI(ActionFactory::ActionKind::DefaultFrame);
    reflectActionToUI(ActionFactory::ActionKind::Stereo);

    _lightTheme = Settings::get().internalOption("light_theme").toBool();
    if (_lightTheme)
        reflectActionToUI(ActionFactory::LightTheme);
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
    connect(_engine, &PlayerEngine::fileLoaded, this, &MainWindow::slotFileLoaded);

    connect(_engine, &PlayerEngine::videoSizeChanged, [ = ]() {
        this->resizeByConstraints();
    });
    connect(_engine, &PlayerEngine::stateChanged, this, &MainWindow::animatePlayState);
    syncPlayState();

    connect(_engine, &PlayerEngine::loadOnlineSubtitlesFinished,
            [this](const QUrl & url, bool success) {//不能去掉 url参数
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

    MainWindowPropertyMonitor* p = new MainWindowPropertyMonitor(this);
    QAbstractEventDispatcher::instance()->installNativeEventFilter(p);

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

#ifndef __mips__
    _fullscreentimelable = new QLabel;
    _fullscreentimelable->setAttribute(Qt::WA_TranslucentBackground);
    _fullscreentimelable->setWindowFlags(Qt::FramelessWindowHint);
    _fullscreentimelable->setParent(this);
    if (!composited && !utils::check_wayland_env()) {
        _fullscreentimelable->setWindowFlags(_fullscreentimelable->windowFlags() | Qt::Dialog);
    }
    _fullscreentimebox = new QHBoxLayout;
    _fullscreentimebox->addStretch();
    _fullscreentimebox->addWidget(_toolbox->getfullscreentimeLabel());
    _fullscreentimebox->addWidget(_toolbox->getfullscreentimeLabelend());
    _fullscreentimebox->addStretch();
    _fullscreentimelable->setLayout(_fullscreentimebox);
    _fullscreentimelable->close();
#endif

    _animationlable = new AnimationLabel;
    _animationlable->setAttribute(Qt::WA_TranslucentBackground);
    _animationlable->setWindowFlags(Qt::FramelessWindowHint);
    _animationlable->setParent(this);
    _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);

#if defined (__aarch64__) || defined (__mips__)
    if(utils::check_wayland_env()){
        popup = new DFloatingMessage(DFloatingMessage::TransientType, this);
    }else{
        popup = new DFloatingMessage(DFloatingMessage::TransientType, nullptr);
    }
    popup->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
#else
    popup = new DFloatingMessage(DFloatingMessage::TransientType, this);
#endif
    popup->resize(0, 0);

    defaultplaymodeinit();
    setHwaccelMode();
    connect(&Settings::get(), &Settings::defaultplaymodechanged, this, &MainWindow::slotdefaultplaymodechanged);
    connect(&Settings::get(), &Settings::hwaccelModeChanged, this, &MainWindow::slotAwaacelModeChanged);

    connect(this, &MainWindow::playlistchanged, _toolbox, &ToolboxProxy::updateplaylisticon);

    connect(_engine, &PlayerEngine::onlineStateChanged, this, &MainWindow::checkOnlineState);
    connect(&OnlineSubtitle::get(), &OnlineSubtitle::onlineSubtitleStateChanged, this, &MainWindow::checkOnlineSubtitle);
    connect(_engine, &PlayerEngine::mpvErrorLogsChanged, this, &MainWindow::checkErrorMpvLogsChanged);
    connect(_engine, &PlayerEngine::mpvWarningLogsChanged, this, &MainWindow::checkWarningMpvLogsChanged);
    connect(_engine, &PlayerEngine::urlpause, this, &MainWindow::slotUrlpause);

    //    connect(_engine, &PlayerEngine::checkMuted, this, [=](bool mute) {
    //        this->setMusicMuted(!mute);
    //        volumeMonitoring.start();
    //        disconnect(_engine, &PlayerEngine::checkMuted, nullptr, nullptr);
    //    });
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance, this, [ = ] {
        this->activateWindow();
    });
    connect(qApp, &QGuiApplication::fontChanged, this, &MainWindow::slotFontChanged);

    {
        loadWindowState();
    }

    ThreadPool::instance()->moveToNewThread(&volumeMonitoring);
    volumeMonitoring.start();
    connect(&volumeMonitoring, &VolumeMonitoring::volumeChanged, this, [ = ](int vol) {
        changedVolumeSlot(vol);
    });

    connect(&volumeMonitoring, &VolumeMonitoring::muteChanged, this, &MainWindow::slotMuteChanged);

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &MainWindow::updateMiniBtnTheme);

    ThreadPool::instance()->moveToNewThread(&m_diskCheckThread);
    m_diskCheckThread.start();

    connect(&m_diskCheckThread, &Diskcheckthread::diskRemove, this, &MainWindow::diskRemoved);

    //_engine->firstInit();

    _toolbox->setDisplayValue(volume);

    if (Settings::get().internalOption("mute").toBool()) {
        _engine->toggleMute();
        Settings::get().setInternalOption("mute", _engine->muted());
    }

    QTimer::singleShot(50, [this]() {
        loadPlayList();
    });

    m_pDBus = new QDBusInterface("org.freedesktop.login1","/org/freedesktop/login1","org.freedesktop.login1.Manager",QDBusConnection::systemBus());
    connect(m_pDBus, SIGNAL(PrepareForSleep(bool)), this, SLOT(sleepStateChanged(bool)));
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
    if (ev->type() == QEvent::TouchBegin) {
        //判定是否是触屏
        this->posMouseOrigin = mapToGlobal(QCursor::pos());
        _isTouch = true;
    }

    //add by heyi
    //判断是否停止右键菜单定时器
    if (_mousePressed) {
//        qDebug() << "mapToGlobal(QCursor::pos()).x() = " << mapToGlobal(QCursor::pos()).x() << "mapToGlobal(QCursor::pos()).y()= " << mapToGlobal(QCursor::pos()).y();
        if (qAbs(nX - mapToGlobal(QCursor::pos()).x()) > 50 || qAbs(nY - mapToGlobal(QCursor::pos()).y()) > 50) {
            if (_mousePressTimer.isActive()) {
                qDebug() << "结束定时器";
                _mousePressTimer.stop();
                _mousePressed = false;
            }
        }
    }

    if (ev->type() == QEvent::WindowStateChange) {
        auto wse = dynamic_cast<QWindowStateChangeEvent *>(ev);
        _lastWindowState = wse->oldState();
        qDebug() << "------------ _lastWindowState" << _lastWindowState
                 << "current " << windowState();
        //NOTE: windowStateChanged won't be emitted if by draggint to restore. so we need to
        //check window state here.
        if (_lastWindowState == Qt::WindowNoState && windowState() == Qt::WindowMinimized) {
            if (Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (_engine && _engine->state() == PlayerEngine::Playing) {
                    requestAction(ActionFactory::TogglePause);
                    _quitfullscreenflag = true;
                }
                QList<QAction *> acts = ActionFactory::get().findActionsByKind(ActionFactory::TogglePlaylist);
                acts.at(0)->setChecked(false);
            }
        } else if (_lastWindowState == Qt::WindowMinimized && windowState() == Qt::WindowNoState) {
            if ( Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (_quitfullscreenflag) {
                    requestAction(ActionFactory::TogglePause);
                    _quitfullscreenflag = false;
                }
            }
        }
        onWindowStateChanged();
    }

    if (utils::check_wayland_env() && m_bClosed && _isJinJia && ev->type() == QEvent::MetaCall) {
        return true;
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

    if (!_miniMode && !m_bIsFullSreen) {
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
#ifndef __mips__
    _progIndicator->setVisible(m_bIsFullSreen && _engine && _engine->state() != PlayerEngine::Idle);
#endif
    toggleShapeMask();

#ifndef USE_DXCB
    if (m_bIsFullSreen) {
        _titlebar->move(0, 0);
        _engine->move(0, 0);
    } else {
        _titlebar->move(0, 0);
        _engine->move(0, 0);
    }
#endif

    if (!m_bIsFullSreen && !isMaximized()) {
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

    if (!isMaximized() && !m_bIsFullSreen && !_miniMode) {
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
    if (!m_bIsFullSreen && !isMaximized() && !_miniMode) {
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

/*void MainWindow::changedVolume(int vol)
{
    _engine->changeVolume(vol);
    Settings::get().setInternalOption("global_volume", vol);
    if (vol != 0)
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(vol));
    else
        _nwComm->updateWithMessage(tr("Mute"));
}*/

void MainWindow::changedVolumeSlot(int vol)
{
    setAudioVolume(qMin(vol, 100));
    if (_engine->muted()) {
        Settings::get().setInternalOption("mute", _engine->muted());
    }
    if (_engine->volume() <= 100 || vol < 100) {
        _engine->changeVolume(vol);
        Settings::get().setInternalOption("global_volume", vol);
    }
    _toolbox->setDisplayValue(qMin(vol, 100));
    if (m_oldDisplayVolume == m_displayVolume) {
        return;
    }
    //fix bug 24816 by ZhuYuliang
    if (!_engine->muted()) {
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_displayVolume));
    } else {
        QTimer::singleShot(1000, [ = ]() {
            _nwComm->updateWithMessage(tr("Mute"));
        });
    }
}

void MainWindow::changedMute()
{
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
    if (mute) {
        _nwComm->updateWithMessage(tr("Mute"));
    } else {
        _engine->changeVolume(m_lastVolume);
        _nwComm->updateWithMessage(tr("Volume: %1%").arg(_toolbox->DisplayVolume()));
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
    if(!utils::check_wayland_env()){
        disconnect(_engine, 0, 0, 0);
        disconnect(&_engine->playlist(), 0, 0, 0);
    }

    if (_lastCookie > 0) {
        utils::UnInhibitStandby(_lastCookie);
        qDebug() << "uninhibit cookie" << _lastCookie;
        _lastCookie = 0;
    }
    if (_powerCookie > 0) {
        utils::UnInhibitPower(_powerCookie);
        _powerCookie = 0;
    }
    delete _engine;
    _engine = nullptr;

#ifdef USE_DXCB
    if (_evm) {
        disconnect(_evm, 0, 0, 0);
        delete _evm;
    }
#endif
}

void MainWindow::firstPlayInit()
{
//    if (m_bMpvFunsLoad) return;

//    _engine->firstInit();

//    int volume = Settings::get().internalOption("global_volume").toInt();
//    if (volume > 100) {
//        Settings::get().setInternalOption("global_volume", 100);
//        volume = 100;
//    }

//    //heyi need
//    _engine->changeVolume(volume);

//    if (_engine->muted()) {
//        _nwComm->updateWithMessage(tr("Mute"));
//    }

//    requestAction(ActionFactory::ChangeSubCodepage, false, {"auto"});
//    m_bMpvFunsLoad = true;
}

bool MainWindow::judgeMouseInWindow(QPoint pos)
{
    bool bRet = false;
    QRect rect = frameGeometry();
    QPoint topLeft = rect.topLeft();
    QPoint bottomRight = rect.bottomRight();
    pos = mapToGlobal(pos);
    topLeft = mapToGlobal(topLeft);
    bottomRight = mapToGlobal(bottomRight);

    if ((pos.x() == topLeft.x()) || (pos.x() == bottomRight.x()) || (pos.y() == topLeft.y()) || (pos.y() == bottomRight.y())) {
        leaveEvent(nullptr);
    }

    return bRet;
}

#ifdef USE_DXCB
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
#endif

void MainWindow::animatePlayState()
{
    if (_miniMode) {
        return;
    }

    if (!_inBurstShootMode && _engine->state() == PlayerEngine::CoreState::Paused) {
        if (!_miniMode) {
            _animationlable->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
            _animationlable->stop();
        }
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

    if (_miniMode) {
        return;
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
            break;
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
#ifdef USE_DXCB
    static xcb_atom_t atomStateAbove = Utility::internAtom("_NET_WM_STATE_ABOVE");
    auto atoms = Utility::windowNetWMState(static_cast<quint32>(windowHandle()->winId()));

#ifndef __mips__
    bool window_is_above = atoms.contains(atomStateAbove);
    if (window_is_above != _windowAbove) {
        qDebug() << "syncStaysOnTop: window_is_above" << window_is_above;
        requestAction(ActionFactory::WindowAbove);
    }
#endif
#endif
}

void MainWindow::reflectActionToUI(ActionFactory::ActionKind kd)
{
    QList<QAction *> acts;
    switch (kd) {
    case ActionFactory::ActionKind::WindowAbove:
    case ActionFactory::ActionKind::ToggleFullscreen:
    case ActionFactory::ActionKind::LightTheme:
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
            } else {
                (*p)->setChecked(!(*p)->isChecked());
            }
            (*p)->setEnabled(old);
            ++p;
        }
        break;
    }

    //迷你模式下判断是否全屏，恢复菜单状态 by zhuyuliang
    case ActionFactory::ActionKind::ToggleMiniMode: {
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts[0];

        QAction *act = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::ToggleFullscreen)[0];
        bool bFlag = act->isChecked();
        if (bFlag) {
            act->setChecked(false);
        }

        p->setEnabled(false);
        p->setChecked(!p->isChecked());
        p->setEnabled(true);
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
    case ActionFactory::ActionKind::OneTimes: {
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        (*p)->setChecked(true);
        break;
    }
    case ActionFactory::ActionKind::DefaultFrame: {
        qDebug() << __func__ << kd;
        acts = ActionFactory::get().findActionsByKind(kd);
        auto p = acts.begin();
        auto old = (*p)->isEnabled();
        (*p)->setEnabled(false);
        (*p)->setChecked(!(*p)->isChecked());
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
        (*p)->setChecked(true);
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

/*NotificationWidget *MainWindow::get_nwComm()
{
    return _nwComm;
}*/

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
    mountFile.close();

    if (strCDMountlist.size() == 0)
        return false;

    QList<QUrl> urls = _engine->addPlayDir(strCDMountlist[0]);  //目前只是针对第一个光盘
    qSort(urls.begin(), urls.end(), compareBarData);
    if (urls.size()) {
        if (_engine->state() == PlayerEngine::CoreState::Idle)
            _engine->playByName(QUrl("playlist://0"));
        _engine->playByName(urls[0]);
    } else {
        return false;
    }
    return true;
}

void MainWindow::loadPlayList()
{
    _playlist = nullptr;
    _playlist = new PlaylistWidget(this, _engine);
    _playlist->hide();
    _toolbox->setPlaylist(_playlist);
    _engine->getplaylist()->loadPlaylist();
    _toolbox->initThumb();

    if (!m_openFiles.isEmpty()) {
        if (m_openFiles.size() == 1) {
            play(m_openFiles[0]);
        } else {
            playList(m_openFiles);
        }
    }
}

void MainWindow::setOpenFiles(QStringList &list)
{
    m_openFiles = list;
}

void MainWindow::testMprisapp()
{
    MovieApp *mpris = new MovieApp(this);

    mpris->initMpris("movie");
    mpris->show();

    mpris->deleteLater();
}

void MainWindow::setShowSetting(bool b)
{
    m_bisShowSettingDialog = b;
}

void MainWindow::mipsShowFullScreen()
{
    QPropertyAnimation *pAn = new QPropertyAnimation(this, "windowOpacity");
    pAn->setDuration(100);
    pAn->setEasingCurve(QEasingCurve::Linear);
    pAn->setEndValue(1);
    pAn->setStartValue(0);
    pAn->start(QAbstractAnimation::DeleteWhenStopped);

    showFullScreen();
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
            break;
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
            //_nwComm->updateWithMessage(tr("No device found"));
            QUrl url(QString("dvd:///%1").arg(dev));
            play(url);
        }
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
                                                              tr("All videos (*)(%2 %1)").arg(_engine->video_filetypes.join(" "))
                                                              .arg(_engine->audio_filetypes.join(" ")), nullptr,
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
                                                        tr("All videos (%1)").arg(_engine->video_filetypes.join(" ")), nullptr,
                                                        DFileDialog::HideNameFilterDetails);
        QFileInfo fileInfo(filename);
        if (fileInfo.exists()) {
            Settings::get().setGeneralOption("last_open_path", fileInfo.path());

            play(QUrl::fromLocalFile(filename));
        }
        break;
    }

    case ActionFactory::ActionKind::StartPlay: {
        if (m_bisOverhunderd) {
            _engine->changeVolume(100);
            Settings::get().setInternalOption("global_volume", m_lastVolume);
            m_bisOverhunderd = false;
        }
        if (_engine->playlist().count() == 0) {
            requestAction(ActionFactory::ActionKind::OpenFileList);
        } else {
            if (_engine->state() == PlayerEngine::CoreState::Idle) {
                //先显示分辨率，再显示静音
                QSize sz = geometry().size();
                auto msg = QString("%1x%2").arg(sz.width()).arg(sz.height());
                QTimer::singleShot(500, [ = ]() {
                    if (_engine->state() != PlayerEngine::CoreState::Idle) {
                        _nwComm->updateWithMessage(msg);
                    }
                });
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
        //play list context menu empty playlist
        _engine->clearPlaylist();
        break;
    }

    case ActionFactory::ActionKind::TogglePlaylist: {
        if (_playlist && _playlist->state() == PlaylistWidget::Closed && !_toolbox->isVisible()) {
            _toolbox->show();
        }
        _playlist->togglePopup();
        if(utils::check_wayland_env()){
            //lmh0710,修复playlist大小不正确
            updateProxyGeometry();
        }
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        this->resumeToolsWindow();
        emit playlistchanged();
        break;
    }

    case ActionFactory::ActionKind::ToggleMiniMode: {
        if (_playlist->state() == PlaylistWidget::Opened && !m_bIsFullSreen) {
            requestAction(ActionFactory::TogglePlaylist);
        }

#ifndef __mips__
        if (!m_bIsFullSreen) {
            _fullscreentimelable->close();
        }
#endif

        if (!fromUI) {
            reflectActionToUI(kd);
        }
        toggleUIMode();

        break;
    }

    case ActionFactory::ActionKind::MovieInfo: {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            MovieInfoDialog mid(_engine->playlist().currentInfo(),this);
            mid.exec();
        }
        break;
    }

    case ActionFactory::ActionKind::WindowAbove: {
        _windowAbove = !_windowAbove;
        my_setStayOnTop(this, _windowAbove);
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        break;
    }

    case ActionFactory::ActionKind::QuitFullscreen: {
        if (_miniMode) {
            if (!fromUI) {
                reflectActionToUI(ActionFactory::ToggleMiniMode);
            }
            toggleUIMode();
        } else if (m_bIsFullSreen) {
            requestAction(ActionFactory::ToggleFullscreen);
#ifndef __mips__
            if (!m_bIsFullSreen) {
                _fullscreentimelable->close();
            }
#endif
        }
        break;
    }

    case ActionFactory::ActionKind::ToggleFullscreen: {
        if (m_bIsFullSreen) {
            //感觉这个参数没什么用，后期观察没有其他用处可以酌情删除
            //_quitfullscreenstopflag = true;
            m_bIsFullSreen = false;
            if (_lastWindowState == Qt::WindowMaximized) {
                _maxfornormalflag = true;
                if(!utils::check_wayland_env()){
                    //setWindowFlags(Qt::Window);//wayland 代码
                    showNormal();           //直接最大化会失败
                }
                showMaximized();
            } else {
                setWindowState(windowState() & ~Qt::WindowFullScreen);
                if (_lastRectInNormalMode.isValid() && !_miniMode && !isMaximized()) {
                    setGeometry(_lastRectInNormalMode);
                    move(_lastRectInNormalMode.x(), _lastRectInNormalMode.y());
                    resize(_lastRectInNormalMode.width(), _lastRectInNormalMode.height());
                    if(utils::check_wayland_env())
                        _titlebar->setFixedWidth(_lastRectInNormalMode.width());             //bug 39991
                }
            }
#ifndef __mips__
            if (!m_bIsFullSreen) {
                _fullscreentimelable->close();
            }
#endif
        } else {
            if(utils::check_wayland_env()){
                _toolbox->setVolSliderHide();
                _toolbox->setButtonTooltipHide();
            }
//            if (/*!_miniMode && (fromUI || isShortcut) && */windowState() == Qt::WindowNoState) {
//                _lastRectInNormalMode = geometry();
//            }
            //可能存在更好的方法（全屏后更新toolbox状态），后期修改
            if (!_toolbox->getbAnimationFinash())
                return;
            m_bIsFullSreen = true;
            mipsShowFullScreen();
            if (m_bIsFullSreen) {
                _maxfornormalflag = false;
#ifndef __mips__
                if(_engine->state() != PlayerEngine::CoreState::Idle){
                    int pixelsWidth = _toolbox->getfullscreentimeLabel()->width() + _toolbox->getfullscreentimeLabelend()->width();
                    QRect deskRect = QApplication::desktop()->availableGeometry();
                    pixelsWidth = qMax(117, pixelsWidth);
                    _fullscreentimelable->setGeometry(deskRect.width() - pixelsWidth - 60, 40, pixelsWidth + 60, 36);
                    _fullscreentimelable->show();
                }
#endif
            }
        }
        if (!fromUI) {
            reflectActionToUI(kd);
        }
        if (m_bIsFullSreen) {
            _animationlable->move(QPoint(QApplication::desktop()->availableGeometry().width() / 2 - _animationlable->width() / 2
                                         , QApplication::desktop()->availableGeometry().height() / 2 - _animationlable->height() / 2));
        } else {
            _animationlable->move(QPoint((width() - _animationlable->width()) / 2,
                                         (height() - _animationlable->height()) / 2));
        }

        activateWindow();
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

    case ActionFactory::ActionKind::ZeroPointFiveTimes: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 0.5;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OneTimes: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 1.0;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointTwoTimes: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 1.2;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointFiveTimes: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 1.5;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::Double: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 2.0;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
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
            if(utils::check_wayland_env()){
                //wayland 下 先显示0再显示静音
                QTimer::singleShot(500, [ = ]() {
                    _nwComm->updateWithMessage(tr("Mute"));
                });
            }else{
                _nwComm->updateWithMessage(tr("Mute"));
            }
        } else {
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_displayVolume));
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeVolume: {
        if (!args.isEmpty()) {
            int nVol = args[0].toInt();
            m_displayVolume = nVol;
            if (m_lastVolume == nVol) {
                if (!_engine->muted()) {
                    _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
                }
                setAudioVolume(qMin(nVol, 100));
                //音量调整为超过100关闭，再次启动后不会重新设置mpv音量问题
                if (!m_bFirstInit && nVol >= 100) {
                    m_bisOverhunderd = true;
                    //首次启动不初始化mpv
//                    _engine->changeVolume(nVol);
//                    Settings::get().setInternalOption("global_volume", m_lastVolume);
                }
                return;
            }
            if(nVol >= 100) {
                _engine->changeVolume(nVol);
                Settings::get().setInternalOption("global_volume", m_lastVolume);
            }

            if (!_engine->muted()) {
                _nwComm->updateWithMessage(tr("Volume: %1%").arg(nVol));
            }
            m_lastVolume = _engine->volume();
            Settings::get().setInternalOption("global_volume", _toolbox->DisplayVolume());
            setAudioVolume(qMin(nVol, 100));
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeUp: {
        m_displayVolume = qMin(m_displayVolume + 10, 200);
        m_oldDisplayVolume = m_displayVolume;
         if(m_displayVolume > 100 && m_displayVolume <= 200)
            _engine->changeVolume(m_displayVolume);
        else
            setAudioVolume(m_displayVolume);
        m_lastVolume = _engine->volume();
        if (!_engine->muted()) {
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_displayVolume));
        } else if (_engine->muted() && m_displayVolume < 200) {
            changedMute();
            setMusicMuted(_engine->muted());
        }
        _toolbox->setDisplayValue(qMin(m_displayVolume, 100));
        Settings::get().setInternalOption("global_volume", m_displayVolume);
        break;
    }

    case ActionFactory::ActionKind::VolumeDown: {
        m_displayVolume = qMax(m_displayVolume - 10, 0);
        m_oldDisplayVolume = m_displayVolume;
        if(m_displayVolume > 100 && m_displayVolume <= 200)
            _engine->changeVolume(m_displayVolume);
        else
            setAudioVolume(m_displayVolume);
        if (m_displayVolume == 0 && !_engine->muted()) {
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_displayVolume));
            changedMute();
            QTimer::singleShot(500, [ = ]() {
                _nwComm->updateWithMessage(tr("Mute"));
            });
            setAudioVolume(0);
        } else if (m_displayVolume > 0 && _engine->muted()) {
            changedMute();
            setMusicMuted(_engine->muted());
        }

        m_lastVolume = _engine->volume();
        if (!_engine->muted()) {
            _nwComm->updateWithMessage(tr("Volume: %1%").arg(m_displayVolume));
        }
        _toolbox->setDisplayValue(qMin(m_displayVolume, 100));
        Settings::get().setInternalOption("global_volume", m_displayVolume);
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistSelected: {
        if (m_bisOverhunderd) {
            _engine->changeVolume(100);
            Settings::get().setInternalOption("global_volume", m_lastVolume);
            m_bisOverhunderd = false;
        }
        _engine->playSelected(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistNext: {
        if (m_IsFree == false)
            return ;

        m_IsFree = false;
        if (m_bIsFullSreen || isMaximized()) {
            _movieSwitchedInFsOrMaxed = true;
        }
        _engine->next();

        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistPrev: {
        if (m_IsFree == false)
            return ;

        m_IsFree = false;
        if (m_bIsFullSreen || isMaximized()) {
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
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = qMin(2.0, _playSpeed + 0.1);
            _engine->setPlaySpeed(_playSpeed);
            if(qFuzzyCompare(0.5, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::ZeroPointFiveTimes);
            } else if (qFuzzyCompare(1.0, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
            } else if (qFuzzyCompare(1.2, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointTwoTimes);
            } else if (qFuzzyCompare(1.5, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointFiveTimes);
            } else if (qFuzzyCompare(2.0, _playSpeed)) {
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::Double);
            } else {
                setPlaySpeedMenuUnchecked();
            }
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::DecelPlayback: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = qMax(0.1, _playSpeed - 0.1);
            _engine->setPlaySpeed(_playSpeed);
            if(qFuzzyCompare(0.5, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::ZeroPointFiveTimes);
            } else if (qFuzzyCompare(1.0, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
            } else if (qFuzzyCompare(1.2, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointTwoTimes);
            } else if (qFuzzyCompare(1.5, _playSpeed)){
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointFiveTimes);
            } else if (qFuzzyCompare(2.0, _playSpeed)) {
                setPlaySpeedMenuChecked(ActionFactory::ActionKind::Double);
            } else {
                setPlaySpeedMenuUnchecked();
            }
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::ResetPlayback: {
        if(_engine->state() != PlayerEngine::CoreState::Idle){
            _playSpeed = 1.0;
            _engine->setPlaySpeed(_playSpeed);
            _nwComm->updateWithMessage(tr("Speed: %1x").arg(_playSpeed));
        }
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
                if (_engine->getplaylist()->getthreadstate()) {
                    qDebug() << "playlist loadthread is running";
                    break;
                }
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
        handleSettings(initSettings());
        break;
    }

    case ActionFactory::ActionKind::Screenshot: {
        auto img = _engine->takeScreenshot();

        QString filePath = Settings::get().screenshotNameTemplate();
        bool success = false;   //条件编译产生误报(cppcheck)
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
            popupAdapter(icon, text);
        } else {
            const QIcon icon = QIcon(":/resources/icons/icon_toast_fail.svg");
            QString text = QString(tr("Failed to save the screenshot"));
            popupAdapter(icon, text);
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
                popupAdapter(icon, text);
            } else {
                const QIcon icon = QIcon(":/resources/icons/icon_toast_fail.svg");
                QString text = QString(tr("Failed to save the screenshot"));
                popupAdapter(icon, text);
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

void MainWindow::handleSettings(DSettingsDialog* dsd)
{
    if (m_bisShowSettingDialog) {
        dsd->exec();
    }
    delete dsd;
    Settings::get().settings()->sync();
}

DSettingsDialog *MainWindow::initSettings()
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
    return dsd;
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
    if(_isFileLoadNotFinished && utils::check_wayland_env()){
        qDebug()<<__func__ <<"File Load Not Finished!";
        return;
    }
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
    if (m_bisOverhunderd) {
        _engine->changeVolume(100);
        Settings::get().setInternalOption("global_volume", m_lastVolume);
        m_bisOverhunderd = false;
    }
    _engine->playByName(url);
}

void MainWindow::toggleShapeMask()
{

    return;

    //this code will never be executed
//#ifndef USE_DXCB
//    if (m_bIsFullSreen || isMaximized()) {
//        clearMask();
//    } else {
//        QPixmap shape(size());
//        shape.setDevicePixelRatio(windowHandle()->devicePixelRatio());
//        shape.fill(Qt::transparent);

//        QPainter p(&shape);
//        p.setRenderHint(QPainter::Antialiasing);
//        QPainterPath pp;
//        pp.addRoundedRect(rect(), RADIUS, RADIUS);
//        p.fillPath(pp, QBrush(Qt::white));
//        p.end();

//        setMask(shape.mask());
//    }
//#endif
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
            if (m_bIsFullSreen) {
                if (_playlist->state() == PlaylistWidget::State::Opened) {
#if !defined(__aarch64__) && !defined (__sw_64__)
                    _toolbox->setGeometry(rfs);
#else
                    _toolbox->setGeometry(rct);
#endif
                } else {
                    _toolbox->setGeometry(rct);
                }
            } else {
                if (_playlist && _playlist->state() == PlaylistWidget::State::Opened) {
#if !defined(__aarch64__) && !defined (__sw_64__)
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
#ifndef __sw_64__
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 10)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            if(utils::check_wayland_env()){
                fixed = QRect((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT)),
                              view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            }
#else
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT - 1)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
#endif
            _playlist->setGeometry(fixed);
        }
    }


    syncPlayState();
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

            if (_toolbox->isVisible()) {
                if (insideToolsArea(mapFromGlobal(QCursor::pos())) && !m_bLastIsTouch)
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

        if (m_bIsFullSreen) {
            if (qApp->focusWindow() == this->windowHandle()){
                qApp->setOverrideCursor(Qt::BlankCursor);
            }
            else {
                qApp->setOverrideCursor(Qt::ArrowCursor);
            }
        }

        _titlebar->hide();
        _toolbox->hide();
    } else {
        if (_autoHideTimer.isActive())
            return;

        _miniPlayBtn->hide();
        _miniCloseBtn->hide();
        _miniQuitMiniBtn->hide();
        if (_labelCover) {
            _labelCover->hide();
        }
    }
}

void MainWindow::resumeToolsWindow()
{
    if (_engine->state() != PlayerEngine::Idle &&
            qApp->applicationState() == Qt::ApplicationActive) {
        // playlist's previous state was Opened
        if (_playlist && _playlist->state() != PlaylistWidget::Closed &&
                !frameGeometry().contains(QCursor::pos())) {
            goto _finish;
        }
    }

    qApp->restoreOverrideCursor();
    setCursor(Qt::ArrowCursor);

    if (!_miniMode) {
        if(!m_bTouchChangeVolume) {
            _titlebar->setVisible(!m_bIsFullSreen);
            _toolbox->show();
        }
        else {
            _toolbox->hide();
        }
    } else {
        _miniPlayBtn->show();
        _miniCloseBtn->show();
        _miniQuitMiniBtn->show();
        if (_labelCover) {
            _labelCover->show();
        }
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

void MainWindow::my_setStayOnTop(const QWidget *widget, bool on)
{
    Q_ASSERT(widget);
    if(utils::check_wayland_env())
        return;

    const auto display = QX11Info::display();
    const auto screen = QX11Info::appScreen();

    const auto wmStateAtom = XInternAtom(display, kAtomNameWmState, false);
    const auto stateAboveAtom = XInternAtom(display, kAtomNameWmStateAbove, false);
    const auto stateStaysOnTopAtom = XInternAtom(display,
                                                 kAtomNameWmStateStaysOnTop,
                                                 false);

    XEvent xev;
    memset(&xev, 0, sizeof(xev));

    xev.xclient.type = ClientMessage;
    xev.xclient.message_type = wmStateAtom;
    xev.xclient.display = display;
    xev.xclient.window = widget->winId();
    xev.xclient.format = 32;

    xev.xclient.data.l[0] = on ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    xev.xclient.data.l[1] = stateAboveAtom;
    xev.xclient.data.l[2] = stateStaysOnTopAtom;
    xev.xclient.data.l[3] = 1;

    XSendEvent(display,
               QX11Info::appRootWindow(screen),
               false,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &xev);
    XFlush(display);
}

void MainWindow::slotmousePressTimerTimeOut()
{
    _mousePressTimer.stop();
    if (_miniMode || _inBurstShootMode || !_mousePressed)
        return;

    if (insideToolsArea(QCursor::pos()))
        return;

    resumeToolsWindow();
    _mousePressed = false;
    _isTouch = false;
}

void MainWindow::slotPlayerStateChanged()
{
    PlayerEngine *engine = dynamic_cast<PlayerEngine *>(sender());
    if(!engine) return;
    setInit(engine->state() != PlayerEngine::Idle);
    resumeToolsWindow();
    updateWindowTitle();

    // delayed checking if engine is still idle, in case other videos are schedulered (next/prev req)
    // and another resize event will happen after that
    QTimer::singleShot(100, [ = ]() {
        if (engine->state() == PlayerEngine::Idle && !_miniMode
                && windowState() == Qt::WindowNoState && !m_bIsFullSreen) {
            this->setMinimumSize(QSize(614, 500));
            this->resize(850, 600);
        }
    });
}

void MainWindow::slotFocusWindowChanged()
{
    if (qApp->focusWindow() != windowHandle())
        suspendToolsWindow();
    else
        resumeToolsWindow();
}

void MainWindow::slotElapsedChanged()
{
#ifndef __mips__
    PlayerEngine *engine = dynamic_cast<PlayerEngine *>(sender());
    if(engine)
    {
        _progIndicator->updateMovieProgress(engine->duration(), engine->elapsed());
    }
#endif
}

void MainWindow::slotFileLoaded()
{
    PlayerEngine *engine = dynamic_cast<PlayerEngine *>(sender());
    if(!engine) return;
    _retryTimes = 0;
    if (utils::check_wayland_env() && windowState() == Qt::WindowNoState && _lastRectInNormalMode.isValid()) {
        const auto &mi = engine->playlist().currentInfo().mi;
        if(!_miniMode){
            if(utils::check_wayland_env()){
                //wayland下存在最大化>全屏->全屏->最小化，窗口超出界面问题。且现在用不着videosize大小窗口
                _lastRectInNormalMode.setSize({850,600});
            }else{
                _lastRectInNormalMode.setSize({mi.width, mi.height});
            }
        }
     }
    this->resizeByConstraints();

    if(utils::check_wayland_env()){
    QDesktopWidget desktop;
        if (desktop.screenCount() > 1) {
            if (!isFullScreen() && !isMaximized() && !_miniMode) {
                auto geom = qApp->desktop()->availableGeometry(this);
                move((geom.width() - this->width()) / 2, (geom.height() - this->height()) / 2);
            }
        } else {
//                utils::MoveToCenter(this);
        }
    }

    m_IsFree = true;
}

void MainWindow::slotUrlpause(bool status)
{
    if (status) {
        auto msg = QString(tr("Buffering..."));
        _nwComm->updateWithMessage(msg);
    }
}

void MainWindow::slotFontChanged(const QFont &/*font*/)
{
#ifndef __mips__
    QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    _toolbox->getfullscreentimeLabel()->setMinimumWidth(fm.width(_toolbox->getfullscreentimeLabel()->text()));
    _toolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.width(_toolbox->getfullscreentimeLabelend()->text()));

    int pixelsWidth = _toolbox->getfullscreentimeLabel()->width() + _toolbox->getfullscreentimeLabelend()->width();
    QRect deskRect = QApplication::desktop()->availableGeometry();
    _fullscreentimelable->setGeometry(deskRect.width() - pixelsWidth - 32, 40, pixelsWidth + 32, 36);
#endif
}

void MainWindow::slotMuteChanged(bool mute)
{
    //首次启动的时候先判断一次设置的静音状态
    if (!m_bFirstInit) {
        this->setMusicMuted(_engine->muted());
        m_bFirstInit = true;
    }
    changedMute(mute);
}

void MainWindow::slotAwaacelModeChanged(const QString &key, const QVariant &value)
{
    if (key != "base.play.hwaccel") {
        qDebug() << "Settings key error";
        return;
    }

    setHwaccelMode(value);
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
        if (_retryTimes < 10) {
            _retryTimes++;
            requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            _retryTimes = 0;
            _nwComm->updateWithMessage(tr("Invalid file"));
            _engine->playlist().remove(_engine->playlist().count() - 1);
        }
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
    }
    //4k播放不显示提示框
    /*else if (errorMessage.contains(QString("Hardware does not support image size 3840x2160"))) {
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
    }*/

}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    qDebug() << __func__;
    if (_lastCookie > 0) {
        utils::UnInhibitStandby(_lastCookie);
        qDebug() << "uninhibit cookie" << _lastCookie;
        _lastCookie = 0;
    }

    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        int cur = 0;
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
    if(utils::check_wayland_env()){
#ifndef _LIBDMR_
    if (Settings::get().isSet(Settings::ClearWhenQuit)) {
        _engine->playlist().clearPlaylist();
    } else {
        //persistently save current playlist
        _engine->playlist().savePlaylist();
    }
#endif
    // xcb close slow so add this for wayland  by xxj
//    _quitfullscreenstopflag = true;
    DMainWindow::closeEvent(ev);
    _engine->stop();
    disconnect(_engine,nullptr,nullptr,nullptr);
    disconnect(&_engine->playlist(),nullptr,nullptr,nullptr);
    if(_engine){
        delete _engine;
        _engine = nullptr;
    }
    CompositingManager::get().setTestFlag(true);
    /*lmh0724临时规避退出崩溃问题*/
        QApplication::quit();
        _Exit(0);
    }
}

void MainWindow::wheelEvent(QWheelEvent *we)
{
    if (insideToolsArea(we->pos()) || insideResizeArea(we->globalPos()))
        return;

    if (_playlist && _playlist->state() == PlaylistWidget::Opened) {
        we->ignore();
        return;
    }

    if (we->buttons() == Qt::NoButton && we->modifiers() == Qt::NoModifier && _toolbox->getVolSliderIsHided()) {
        requestAction(we->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
    }
}

void MainWindow::focusInEvent(QFocusEvent *fe)
{
    resumeToolsWindow();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    if(_maxfornormalflag)
        return;
    /*if (Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_engine && _engine->state() == PlayerEngine::Playing) {
            requestAction(ActionFactory::TogglePause);
            _quitfullscreenflag = true;
//            if (!_quitfullscreenstopflag) {
//                _pausedOnHide = true;
//                requestAction(ActionFactory::TogglePause);
//                _quitfullscreenstopflag = false;
//            } else {
//                _quitfullscreenstopflag = false;
//            }
        }
        QList<QAction *> acts = ActionFactory::get().findActionsByKind(ActionFactory::TogglePlaylist);
        acts.at(0)->setChecked(false);
    }*/
}
void MainWindow::showEvent(QShowEvent *event)
{
    qDebug() << __func__;
    /*最大化，全屏，取消全屏，会先调用hideevent,再调用showevent，此时播放状态尚未切换，导致逻辑出错*/
    if(_maxfornormalflag)
        return;
    /*if ( Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_quitfullscreenflag) {
            requestAction(ActionFactory::TogglePause);
            _quitfullscreenflag = false;
        }
#ifdef __aarch64__
                QVariant l = ApplicationAdaptor::redDBusProperty("com.deepin.SessionManager", "/com/deepin/SessionManager",
                                                                 "com.deepin.SessionManager", "Locked");
                if (l.isValid() && !l.toBool()) {
                    qDebug() << "locked_____________" << l;
                    //是否锁屏
                    if(_engine && _engine->state() != PlayerEngine::Playing){
                        requestAction(ActionFactory::TogglePause);
                    }
                }
#endif
    }*/
    /*if (_pausedOnHide || Settings::get().isSet(Settings::PauseOnMinimize)) {
        if (_pausedOnHide && _engine && _engine->state() != PlayerEngine::Playing) {
            if (_quitfullscreenflag) {
                requestAction(ActionFactory::TogglePause);
                _quitfullscreenflag = false;
            }

            if (!_quitfullscreenstopflag) {
#ifdef __aarch64__
                QVariant l = ApplicationAdaptor::redDBusProperty("com.deepin.SessionManager", "/com/deepin/SessionManager",
                                                                 "com.deepin.SessionManager", "Locked");
                if (l.isValid() && !l.toBool()) {
                    qDebug() << "locked_____________" << l;
                    //是否锁屏
                    if(_engine && _engine->state() != PlayerEngine::Playing){
                        requestAction(ActionFactory::TogglePause);
                    }
                }
#endif
                    requestAction(ActionFactory::TogglePause);
                    _pausedOnHide = false;
                    _quitfullscreenstopflag = false;
#ifdef __aarch64__
                }
#endif
            } else {
                _quitfullscreenstopflag = false;
            }
        }
    }*/

    _titlebar->raise();
    _toolbox->raise();
    if (_playlist) {
        _playlist->raise();
    }
    resumeToolsWindow();

    if (!qgetenv("FLATPAK_APPID").isEmpty()) {
        qDebug() << "workaround for flatpak";
        if (_playlist->isVisible())
            updateProxyGeometry();
    }
//    if(this->focusWidget())
//    {
//        qDebug() << this->focusWidget()->objectName();
//    }
}

void MainWindow::resizeByConstraints(bool forceCentered)
{
    if (_engine->state() == PlayerEngine::Idle || _engine->playlist().count() == 0) {
        _titlebar->setTitletxt(QString());
        return;
    }

    if (_miniMode || m_bIsFullSreen || isMaximized()) {
        return;
    }

    qDebug() << __func__;
    updateWindowTitle();
    //lmh0710修复窗口变成影片分辨率问题
    if(utils::check_wayland_env()){
        return;
    }

    const auto &mi = _engine->playlist().currentInfo().mi;
    auto sz = _engine->videoSize();
#ifdef __mips__
//这段代码现在看来没有意义，暂时注释
//    if (!CompositingManager::get().composited()) {
//        float w = (float)sz.width();
//        float h = (float)sz.height();
//        if ((w / h) > 0.56 && (w / h) < 0.75) {
//            _engine->setVideoZoom(-(w / h) - 0.1);
//        } else {
//            _engine->setVideoZoom(0);
//        }

//        //3.26修改，初始分辨率大于1080P时缩小一半
//        while (sz.width() >= 1080) {
//            sz = sz / 2;
//        }
//    }
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
        if(utils::check_wayland_env()){
            this->setGeometry(r);
            this->move(r.x(), r.y());
            this->resize(r.width(), r.height());
        }

#ifdef __aarch64
        _nwComm->syncPosition(r);
#endif
    } else {
        if(utils::check_wayland_env()){
            QRect r = this->geometry();
            r.setSize(sz);
            this->setGeometry(r);
            this->move(r.x(), r.y());
            this->resize(r.width(), r.height());
        }

#ifdef __aarch64
        _nwComm->syncPosition();
#endif
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    QSize m;

    if (_miniMode) {
        m = QSize(40, 40);
    } else {
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto dRect = DApplication::desktop()->availableGeometry();
            auto sz = _engine->videoSize();
            if (sz.width() == 0 || sz.height() == 0) {
                m = QSize(614, 500);
            } else {
                qreal ratio = static_cast<qreal>(sz.width()) / sz.height();
                if (sz.width() > sz.height()) {
                    int w = static_cast<int>(500 * ratio);
                    m = QSize(w, 500);
                } else {
                    int h = static_cast<int>(614 / ratio);
                    if (h > dRect.height()) {
                        h = dRect.height();
                    }
                    m = QSize(614, h);
                }
            }
        } else {
            m = QSize(614, 500);
        }
//        m = QSize(614, 500);
    }
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
    if(utils::check_wayland_env()){
    //    if (_playlist) {
    //        _playlist->setFixedWidth(this->width() - 20);
    //    }
        if (_toolbox) {
            _toolbox->setFixedWidth(this->width() - 10);
        }
    }
#ifndef __mips__
    if (m_bIsFullSreen) {
        _progIndicator->move(geometry().width() - _progIndicator->width() - 18, 8);
    }
#endif
    // modify 4.1  Limit video to mini mode size by thx
    LimitWindowize();

    updateSizeConstraints();
    updateProxyGeometry();
    QTimer::singleShot(0, [ = ]() {
        updateWindowTitle();
    });
    updateGeometryNotification(geometry().size());
    //add by heyi
    /*******
     * 之前为修改全屏下呼出右键菜单任务栏不消失问题
     * 此处修改存在逻辑错误，未判断窗口初始状态是否为置顶
     * 此处先注释掉完成当前版本功能，sp3开发人员根据后期开发状态进行修改
     *******/
//    if (!isFullScreen()) {
//        my_setStayOnTop(this, false);
//    }
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
    qDebug() << "进入moveEvent";
    QWidget::moveEvent(ev);
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
#ifdef QT_DEBUG
    //加入一个在调试环境下切换软硬解码的快捷键
    if (ev->key() == Qt::Key_H) {
        if ( QApplication::keyboardModifiers () == Qt::ControlModifier)
        {
            if (m_currentHwdec == "") {
                m_currentHwdec = _engine->getBackendProperty("hwdec").toString();
            }
            if (m_currentHwdec == "off") {
                _nwComm->popup("current is off");
                QWidget::keyPressEvent(ev);
                return;
            }

            QString str = _engine->getBackendProperty("hwdec").toString();
            if (str == "off") {
                _engine->setBackendProperty("hwdec", m_currentHwdec);
            } else {
                _engine->setBackendProperty("hwdec", "off");
            }
            _nwComm->popup(QString("hwdec is %1").arg(_engine->getBackendProperty("hwdec").toString()));
        }
    }
#endif

    QWidget::keyPressEvent(ev);
}

void MainWindow::keyReleaseEvent(QKeyEvent *ev)
{
    QWidget::keyReleaseEvent(ev);
}

void MainWindow::capturedMousePressEvent(QMouseEvent *me)
{
    _mouseMoved = false;
    _mousePressed = false;
#ifdef __aarch64__
    _nwComm->hide();
#elif __mips__
    _nwComm->hide();
#endif
    if (qApp->focusWindow() == nullptr) return;

    if (me->buttons() == Qt::LeftButton) {
        _mousePressed = true;
    }

    //add by heyi
//此代码解决全屏时dock未隐藏的问题，但是会修改引入其他问题，dock的问题应由dock解决
//    if (_isTouch) {
//        if (isFullScreen()) {
//            my_setStayOnTop(this, true);
//        } else {
//            my_setStayOnTop(this, false);
//        }
//    }

    posMouseOrigin = mapToGlobal(me->pos());
}

void MainWindow::capturedMouseReleaseEvent(QMouseEvent *me)
{
    if(_isTouch)
    {
        m_bLastIsTouch = true;
         _isTouch = false;

         if(m_bTouchChangeVolume)
         {
             m_bTouchChangeVolume = false;
             _toolbox->setVisible(true);
         }

        if(m_bProgressChanged)
        {
            _toolbox->updateSlider();   //手势释放时改变影片进度
            m_bProgressChanged = false;
        }
    }
    else {
         m_bLastIsTouch = false;
    }

    if (_delayedResizeByConstraint) {
        _delayedResizeByConstraint = false;

        QTimer::singleShot(0, [ = ]() {
            this->setMinimumSize({0, 0});
            this->resizeByConstraints(true);
        });
    }

    //add by heyi
    /********
    **触摸屏呼出右键菜单相关
    **此处逻辑与窗口置顶存在冲突，先处理窗口置顶问题
    **sp3开发人员请重新梳理此处代码
    ** add by xiepengfei
    ********/
    //my_setStayOnTop(this, false);
}

static bool _afterDblClick = false;
void MainWindow::mousePressEvent(QMouseEvent *ev)
{
    _mouseMoved = false;
    _mousePressed = false;
#ifdef __aarch64__
    _nwComm->hide();
#elif __mips__
    _nwComm->hide();
#endif

    if (qApp->focusWindow() == nullptr) return;
    if (ev->buttons() == Qt::LeftButton) {
        _mousePressed = true;
        //add by heyi
        if (!_mousePressTimer.isActive() && _isTouch) {
            _mousePressTimer.stop();

            nX = mapToGlobal(QCursor::pos()).x();
            nY = mapToGlobal(QCursor::pos()).y();
            qDebug() << "已经进入触屏按下事件" << nX << nY;
            _mousePressTimer.start();
        }
        /*if (_playState->isVisible()) {
            //_playState->setState(DImageButton::Press);
            QMouseEvent me(QEvent::MouseButtonPress, {}, ev->button(), ev->buttons(), ev->modifiers());
            qApp->sendEvent(_playState, &me);
        }*/
        //posMouseOrigin = QCursor::pos();
    }

    posMouseOrigin = mapToGlobal(ev->pos());
    m_pressPoint = ev->pos();
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *ev)
{
    qDebug() << "进入mouseDoubleClickEvent";
    if (!_miniMode && this->_engine->getplaylist()->getthreadstate()) {
        qDebug() << "playlist loadthread is running";
        return;
    }
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
    //add by heyi
    static bool bFlags = true;
    if (bFlags) {
        //firstPlayInit();
        repaint();
        bFlags = false;
    }

    qDebug() << "进入mouseReleaseEvent";
    QWidget::mouseReleaseEvent(ev);
    if (!_mousePressed) {
        _afterDblClick = false;
        _mouseMoved = false;
    }

    if (qApp->focusWindow() == nullptr || !_mousePressed) return;

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

    //heyiUtility::cancelWindowMoveResize(static_cast<quint32>(winId()));
    _mouseMoved = false;
}

void MainWindow::delayedMouseReleaseHandler()
{
    if ((!_afterDblClick && !m_bLastIsTouch) || _miniMode)
        requestAction(ActionFactory::TogglePause, false, {}, true);
    _afterDblClick = false;
}

/*not used yet*/
/*void MainWindow::onDvdData(const QString &title)
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
}*/

void MainWindow::mouseMoveEvent(QMouseEvent *ev)
{
    QPoint ptCurr = mapToGlobal(ev->pos());
    QPoint ptDelta = ptCurr-this->posMouseOrigin;

    if(qAbs(ptDelta.x())<5 && qAbs(ptDelta.y())<5){  //避免误触
        return;
    }

    if(_isTouch&&m_bIsFullSreen)     //全屏时才触发滑动改变音量和进度的操作
    {
        if(qAbs(ptDelta.x())>qAbs(ptDelta.y())
                && _engine->state() != PlayerEngine::CoreState::Idle){
            m_bTouchChangeVolume = false;
            _toolbox->updateProgress(ptDelta.x());     //改变进度条显示
            this->posMouseOrigin = ptCurr;
            m_bProgressChanged = true;
            return;
        }
        else if(qAbs(ptDelta.x())<qAbs(ptDelta.y())){
            if(ptDelta.y()>0){
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeDown);
            }
            else {
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeUp);
            }

            this->posMouseOrigin = ptCurr;
            return;
        }
    }

    if (!CompositingManager::get().composited() && !m_bIsFullSreen) {
        move(pos() + ev->pos() - m_pressPoint);
    } else {
        QWidget::mouseMoveEvent(ev);
    }

     this->posMouseOrigin = ptCurr;
    _mouseMoved = true;
}

void MainWindow::contextMenuEvent(QContextMenuEvent *cme)
{
    qDebug() << "进入contextMenuEvent";
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

        QString strMovie = QObject::tr("Movie");
        if (!nameV.isValid() || (!nameV.toString().contains( strMovie, Qt::CaseInsensitive) && !nameV.toString().contains("deepin-movie", Qt::CaseInsensitive)))
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
    } else if (volume != 0 ) {
        tVolume = (volume + 1) / 100.0 ;
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

void MainWindow::popupAdapter(QIcon icon, QString text)
{
    popup->setIcon(icon);
    DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);
    QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);
    QFontMetrics fm(font);
    auto w = fm.boundingRect(text).width();
    popup->setMessage(text);
    popup->resize(w + 70, 52);
#ifdef __x86_64__
    popup->move((width() - popup->width()) / 2, height() - 127);
#else
    popup->move((width() - popup->width()) / 2 + geometry().x(), height() - 137 + geometry().y());
#endif
    popup->show();
}

void MainWindow::setHwaccelMode(const QVariant &value)
{
    QString strHeaccelMode;
    auto mode_opt = Settings::get().settings()->option("base.play.hwaccel");

    if(value == -1){
        strHeaccelMode = mode_opt->data("items").toStringList()[mode_opt->value().toInt()];
    }
    else {
        strHeaccelMode = mode_opt->data("items").toStringList()[value.toInt()];
    }
    if (strHeaccelMode == tr("Auto")) {
        _engine->changehwaccelMode(Backend::hwaccelAuto);
    }
    else if (strHeaccelMode == tr("Open")) {
        _engine->changehwaccelMode(Backend::hwaccelOpen);
    }
    else if (strHeaccelMode == tr("Close")) {
        _engine->changehwaccelMode(Backend::hwaccelClose);
    }
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
#ifdef __x86_64__
    QPainterPath pp;
    if(DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()){
        if(_engine->state() != PlayerEngine::Idle && !_toolbox->isVisible()){
            pp.addRect(bgRect);
            painter.fillPath(pp, Qt::black);
        }else{
            pp.addRect(bgRect);
            painter.fillPath(pp, Qt::white);
        }
    }
#endif
    if (_engine->state() == PlayerEngine::Idle) {
        QImage &bg = bg_dark;
        if(DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            QImage img=utils::LoadHiDPIImage(":/resources/icons/dark/init-splash-bac.svg");
            auto bt = bgRect.center() - QPoint(img.width() / 2, img.height() / 2) / devicePixelRatioF();
            painter.drawImage(bt, img);
        }
        auto pt = bgRect.center() - QPoint(bg.width() / 2, bg.height() / 2) / devicePixelRatioF();
        painter.drawImage(pt, bg);
    }

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
    if(utils::check_wayland_env()){
        auto flags = windowFlags();
        if (_miniMode) {
            flags |= Qt::X11BypassWindowManagerHint;
        } else {
            flags &= ~Qt::X11BypassWindowManagerHint;
        }
        //wayland下opengl窗口使用之前必须先调用makeCurrent;
        _engine->MakeCurrent();
        setWindowFlags(flags);
        show();
    }

    qInfo() << __func__ << _miniMode;

    if (_miniMode) {
        _titlebar->titlebar()->setDisableFlags(Qt::WindowMaximizeButtonHint);
    } else {
        _titlebar->titlebar()->setDisableFlags(nullptr);
    }
    if (_listener) _listener->setEnabled(!_miniMode);

    _titlebar->setVisible(!_miniMode);
    //_toolbox->setVisible(!_miniMode);

    _miniPlayBtn->setVisible(_miniMode);
    _miniCloseBtn->setVisible(_miniMode);
    _miniQuitMiniBtn->setVisible(_miniMode);
    if (_labelCover) {
        _labelCover->setVisible(_miniMode);
    }

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

        if (_playlist->state() == PlaylistWidget::Opened) {
            _stateBeforeMiniMode |= SBEM_PlaylistOpened;
            requestAction(ActionFactory::TogglePlaylist);
        }

        if (m_bIsFullSreen) {
            _stateBeforeMiniMode |= SBEM_Fullscreen;
            if(!utils::check_wayland_env()){
                requestAction(ActionFactory::ToggleFullscreen);
                //requestAction(ActionFactory::QuitFullscreen);
                //reflectActionToUI(ActionFactory::ToggleMiniMode);
                this->setWindowState(Qt::WindowNoState);
            }
        } else if (isMaximized()) {
            _stateBeforeMiniMode |= SBEM_Maximized;
            showNormal();
        } else {
            _lastRectInNormalMode = geometry();
        }

        if (!_windowAbove) {
            _stateBeforeMiniMode |= SBEM_Above;
            requestAction(ActionFactory::WindowAbove);
        }

        auto sz = QSize(380, 380);
        if (_engine->state() != PlayerEngine::CoreState::Idle) {
            auto vid_size = _engine->videoSize();
            qreal ratio = vid_size.width() / static_cast<qreal>(vid_size.height());

            if (vid_size.width() > vid_size.height()) {
                sz = QSize(380, static_cast<int>(380 / ratio));
            } else {
                sz = QSize(380, static_cast<int>(380 * ratio));   //by thx 这样修改mini模式也存在黑边
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

        auto deskGeom = qApp->desktop()->availableGeometry(this);
        move((deskGeom.width() - this->width()) / 2, (deskGeom.height() - this->height()) / 2); //迷你模式下窗口居中 by zhuyuliang
        resize(geom.width(), geom.height());

        _miniPlayBtn->move(sz.width() - 12 - _miniPlayBtn->width(),
                           sz.height() - 10 - _miniPlayBtn->height());
        if (_labelCover) {
            _labelCover->move(sz.width() - 15 - _miniCloseBtn->width(), 10);
        }
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
                this->resize(850, 600);
            } else {
                if (_lastRectInNormalMode.isValid() /*&& _engine->videoRotation() == 0  by thx*/) {
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
    //add by heyi 拖动进来时先初始化窗口
    //firstPlayInit();
    qDebug() << ev->mimeData()->formats();
    if (!ev->mimeData()->hasUrls()) {
        return;
    }

    if (m_bisOverhunderd) {
        _engine->changeVolume(100);
        Settings::get().setInternalOption("global_volume", m_lastVolume);
        m_bisOverhunderd = false;
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
    mountFile.close();
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

void MainWindow::diskRemoved(QString strDiskName)
{
    QString strCurrFile;
    if(_engine->getplaylist()->count()<=0)
    {
        return;
    }
    strCurrFile = _engine->getplaylist()->currentInfo().url.toString();

    if (strCurrFile.contains(strDiskName)/* && _engine->state() == PlayerEngine::Playing*/)
        _nwComm->updateWithMessage(tr("The CD/DVD has been ejected"));
}

void MainWindow::sleepStateChanged(bool bSleep)
{
    if(bSleep && _engine->state() == PlayerEngine::CoreState::Playing){
        requestAction(ActionFactory::ActionKind::TogglePause);
    }
    else if(!bSleep && windowState()!= Qt::WindowMinimized && _engine->state() == PlayerEngine::CoreState::Paused) {
        _engine->seekAbsolute(static_cast<int>(_engine->elapsed()));                //在龙芯下需要重新seek下，不然影片会卡住反复横跳
        requestAction(ActionFactory::ActionKind::TogglePause);
    }
    else if (!bSleep && windowState() == Qt::WindowMinimized && _engine->state() == PlayerEngine::CoreState::Paused) {
        _engine->seekAbsolute(static_cast<int>(_engine->elapsed()));
    }
}

void MainWindow::setPlaySpeedMenuChecked(ActionFactory::ActionKind kd)
{
    QList<QAction *> acts = ActionFactory::get().findActionsByKind(kd);
    auto p = acts.begin();
    (*p)->setChecked(true);
}

void MainWindow::setPlaySpeedMenuUnchecked()
{
    QList<QAction *> acts;
    {
        acts = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::ZeroPointFiveTimes);
        auto p = acts.begin();
        if((*p)->isChecked()){
            (*p)->setChecked(false);
        }
    }
    {
        acts = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OneTimes);
        auto p = acts.begin();
        if((*p)->isChecked()){
            (*p)->setChecked(false);
        }
    }
    {
        acts = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OnePointTwoTimes);
        auto p = acts.begin();
        if((*p)->isChecked()){
            (*p)->setChecked(false);
        }
    }
    {
        acts = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OnePointFiveTimes);
        auto p = acts.begin();
        if((*p)->isChecked()){
            (*p)->setChecked(false);
        }
    }
    {
        acts = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::Double);
        auto p = acts.begin();
        if((*p)->isChecked()){
            (*p)->setChecked(false);
        }
    }

}

void MainWindow::updateGeometry(CornerEdge edge, QPoint p)
{
    bool keep_ratio = engine()->state() != PlayerEngine::CoreState::Idle;
    auto old_geom = frameGeometry();
    auto geom = frameGeometry();
    qreal ratio = static_cast<qreal>(geom.width()) / geom.height();

    // disable edges
    switch (edge) {
    case CornerEdge::BottomEdge:
    case CornerEdge::TopEdge:
    case CornerEdge::LeftEdge:
    case CornerEdge::RightEdge:
    case CornerEdge::NoneEdge:
        return;
    default:
        break;
    }

    if (keep_ratio) {
        auto sz = engine()->videoSize();
        if (sz.isEmpty()) {
            const auto &mi = engine()->playlist().currentInfo().mi;
            sz = QSize(mi.width, mi.height);
        }

        ratio = sz.width() / static_cast<qreal>(sz.height());
        switch (edge) {
        case CornerEdge::TopLeftCorner:
            geom.setLeft(p.x());
            geom.setTop(static_cast<int>(geom.bottom() - geom.width() / ratio));
            break;
        case CornerEdge::BottomLeftCorner:
        case CornerEdge::LeftEdge:
            geom.setLeft(p.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case CornerEdge::BottomRightCorner:
        case CornerEdge::RightEdge:
            geom.setRight(p.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case CornerEdge::TopRightCorner:
        case CornerEdge::TopEdge:
            geom.setTop(p.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        case CornerEdge::BottomEdge:
            geom.setBottom(p.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        default:
            break;
        }
    } else {
        switch (edge) {
        case CornerEdge::BottomLeftCorner:
            geom.setBottomLeft(p);
            break;
        case CornerEdge::TopLeftCorner:
            geom.setTopLeft(p);
            break;
        case CornerEdge::LeftEdge:
            geom.setLeft(p.x());
            break;
        case CornerEdge::BottomRightCorner:
            geom.setBottomRight(p);
            break;
        case CornerEdge::RightEdge:
            geom.setRight(p.x());
            break;
        case CornerEdge::TopRightCorner:
            geom.setTopRight(p);
            break;
        case CornerEdge::TopEdge:
            geom.setTop(p.y());
            break;
        case CornerEdge::BottomEdge:
            geom.setBottom(p.y());
            break;
        default:
            break;
        }
    }

    auto min = minimumSize();
    if (old_geom.width() <= min.width() && geom.left() > old_geom.left()) {
        geom.setLeft(old_geom.left());
    }
    if (old_geom.height() <= min.height() && geom.top() > old_geom.top()) {
        geom.setTop(old_geom.top());
    }

    geom.setWidth(qMax(geom.width(), min.width()));
    geom.setHeight(qMax(geom.height(), min.height()));
    updateContentGeometry(geom);
    updateGeometryNotification(geom.size());
}
#include "mainwindow.moc"
