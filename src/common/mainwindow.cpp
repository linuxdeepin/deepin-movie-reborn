// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include "mainwindow.h"
#include "toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"
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
#include "vendor/presenter.h"
#include "filefilter.h"
#include "eventlogutils.h"

//#include <QtWidgets>
#include <QtDBus>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtX11Extras>
#include <QX11Info>
#else
#include <QtGui/private/qtx11extras_p.h>    
#include <QtGui/private/qtguiglobal_p.h>
#endif

#include <DLabel>
#include <DApplication>
#include <DTitlebar>
#include <DSettingsDialog>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DThemeManager>
#endif

#include <DAboutDialog>
#include <DInputDialog>

#include <DWidgetUtil>
#include <DWindowManagerHelper>
#ifdef DTKCORE_CLASS_DConfigFile
#include <DConfig>
#endif
#include <DSettingsWidgetFactory>
#include <DLineEdit>
#include <DFileDialog>
#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#include "moviewidget.h"
#include <QtConcurrent>
#include <QFileSystemWatcher>

#include "../accessibility/ac-deepin-movie-define.h"

#define XCB_Platform     //to distinguish xcb or wayland
#ifdef XCB_Platform
#include "utility.h"
#endif

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
#define AUTOHIDE_TIME_PAD 5000   //time of the toolbox auto hide

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DToast>
#endif

DWIDGET_USE_NAMESPACE

using namespace dmr;

#define MOUSE_MARGINS 6

int MainWindow::m_nRetryTimes = 0;

static void workaround_updateStyle(QWidget *pParent, const QString &sTheme)
{
    qDebug() << "Entering workaround_updateStyle for parent:" << pParent->objectName() << "with theme:" << sTheme;
    pParent->setStyle(QStyleFactory::create(sTheme));
    qDebug() << "Style set for parent.";
    for (auto obj : pParent->children()) {
        QWidget *pWidget = qobject_cast<QWidget *>(obj);
        if (pWidget) {
            qDebug() << "Recursively updating style for child widget:" << pWidget->objectName();
            workaround_updateStyle(pWidget, sTheme);
        }
    }
    qDebug() << "Exiting workaround_updateStyle for parent:" << pParent->objectName();
}

static QString ElideText(const QString &sText, const QSize &size,
                         QTextOption::WrapMode wordWrap, const QFont &font,
                         Qt::TextElideMode mode, int nLineHeight, int nLastLineWidth)
{
    qDebug() << "Entering ElideText. Text length:" << sText.length() << ", Size:" << size << ", WordWrap:" << wordWrap << ", LineHeight:" << nLineHeight << ", LastLineWidth:" << nLastLineWidth;
    int nHeight = 0;

    QTextLayout textLayout(sText);
    QString sElideText = nullptr;
    QFontMetrics fontMetrics(font);

    textLayout.setFont(font);
    const_cast<QTextOption *>(&textLayout.textOption())->setWrapMode(wordWrap);

    textLayout.beginLayout();

    QTextLine line = textLayout.createLine();
    qDebug() << "TextLayout started. Initial line valid:" << line.isValid();

    while (line.isValid()) {
        nHeight += nLineHeight;
        qDebug() << "Processing line. Current height:" << nHeight << ", Line text start:" << line.textStart() << ", Line text length:" << line.textLength();

        if (nHeight + nLineHeight >= size.height()) {
            qDebug() << "Height limit reached. Eliding remaining text.";
            sElideText += fontMetrics.elidedText(sText.mid(line.textStart() + line.textLength() + 1),
                                                 mode, nLastLineWidth);
            break;
        }

        line.setLineWidth(size.width());
        const QString &sTmpText = sText.mid(line.textStart(), line.textLength());

        if (sTmpText.indexOf('\n')) {
            qDebug() << "Newline character found in current line segment, adding line height.";
            nHeight += nLineHeight;
        }

        sElideText += sTmpText;
        line = textLayout.createLine();

        if (line.isValid()) {
            sElideText.append("\n");
            qDebug() << "Appended newline. Next line valid.";
        } else {
            qDebug() << "Last line processed. No more valid lines.";
        }
    }

    textLayout.endLayout();
    qDebug() << "TextLayout ended. Final line count:" << textLayout.lineCount();

    if (textLayout.lineCount() == 1) {
        qDebug() << "Single line text, performing final elision.";
        sElideText = fontMetrics.elidedText(sElideText, mode, nLastLineWidth);
    }

    qDebug() << "Exiting ElideText. Returning elided text (length:" << sElideText.length() << ").";
    return sElideText;
}

static QWidget *createDecodeOptionHandle(QObject *pObj)
{
    qDebug() << "Entering createDecodeOptionHandle";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mainWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mainWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("decodeOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mainWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mainWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value){
        if (dataType == "items") {
            combobox->clear();
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &){
        pSettingOption->setValue(combobox->currentIndex());
    });


    return pOptionWidget;
}

static QWidget *createVoOptionHandle(QObject *pObj)
{
    qDebug() << "Entering createVoOptionHandle";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mainWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mainWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("videoOutOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mainWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mainWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value){
        if (dataType == "items") {
            combobox->clear();
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &){
        pSettingOption->setValue(combobox->currentIndex());
    });

    pSettingOption->connect(pSettingOption, &DSettingsOption::valueChanged, [=](QVariant value){
        combobox->setCurrentIndex(value.toInt());
    });

    return pOptionWidget;
}

static QWidget *createEffectOptionHandle(QObject *pObj)
{
    qDebug() << "Entering createEffectOptionHandle";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mainWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mainWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("effectOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mainWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mainWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value){
        if (dataType == "items") {
            combobox->clear();
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &){
        pSettingOption->setValue(combobox->currentIndex());
    });

    pSettingOption->connect(pSettingOption, &DSettingsOption::valueChanged, [=](QVariant value){
        combobox->setCurrentIndex(value.toInt());
    });

    return pOptionWidget;
}

static QWidget *createSelectableLineEditOptionHandle(QObject *pObj)
{
    qDebug() << "Entering createSelectableLineEditOptionHandle";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);

    DLineEdit *pLineEdit = new DLineEdit();
    DWidget *pMainWid = new DWidget;
    QHBoxLayout *pLayout = new QHBoxLayout;

    static QString sNameLast = nullptr;

    pMainWid->setLayout(pLayout);
    DIconButton *pIconButton = new DIconButton(nullptr);
    pIconButton->setIcon(DStyle::SP_SelectElement);

    pLineEdit->setObjectName("OptionSelectableLineEdit");
    pLineEdit->setText(pSettingOption->value().toString());
    QFontMetrics fontMetrics = pLineEdit->fontMetrics();
    QString sElideText = ElideText(pLineEdit->text(), {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                                   pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
    pSettingOption->connect(pLineEdit, &DLineEdit::focusChanged, [ = ](bool bRet) {
        if (bRet) {
            qDebug() << "Entering DLineEdit::focusChanged";
            pLineEdit->setText(pSettingOption->value().toString());
        }
    });
    pLineEdit->setText(sElideText);
    sNameLast = sElideText;
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(pLineEdit);
    pLayout->addWidget(pIconButton);

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("OptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    pMainWid->setMinimumWidth(240);
    QLabel *title = new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str()));
    title->setContentsMargins(0, 0, 16, 0);
    pOptionLayout->addRow(title, pMainWid);

    workaround_updateStyle(pOptionWidget, "light");

    DDialog *pPrompt = new DDialog(pMainWid);
    pPrompt->setIcon(QIcon(":/resources/icons/warning.svg"));
    pPrompt->setMessage(QObject::tr("You don't have permission to operate this folder"));
    pPrompt->setWindowFlags(pPrompt->windowFlags() | Qt::WindowStaysOnTopHint);
    pPrompt->addButton(QObject::tr("OK"), true, DDialog::ButtonRecommend);

    auto validate = [ = ](QString sName, bool bAlert = true) -> bool {
        sName = sName.trimmed();
        if (sName.isEmpty()) return false;

        if (sName.size() && sName[0] == '~')
        {
            sName.replace(0, 1, QDir::homePath());
        }

        QFileInfo fi(sName);
        QDir dir(sName);
        if (fi.exists())
        {
            if (!fi.isDir()) {
                if (bAlert) pLineEdit->showAlertMessage(QObject::tr("Invalid folder"));
                return false;
            }

            if (!fi.isReadable() || !fi.isWritable()) {
                return false;
            }
        } else {
            if (dir.cdUp()) {
                QFileInfo ch(dir.path());
                if (!ch.isReadable() || !ch.isWritable())
                    return false;
            }
        }

        return true;
    };

    pSettingOption->connect(pIconButton, &DPushButton::clicked, [ = ]() {
#ifndef USE_TEST
        QString sName = DFileDialog::getExistingDirectory(nullptr, QObject::tr("Open folder"),
                                                          MainWindow::lastOpenedPath(),
                                                          DFileDialog::ShowDirsOnly | DFileDialog::DontResolveSymlinks);
#else
        QString sName = "/data/source/deepin-movie-reborn/movie/DMovie";
#endif
        if (validate(sName, false)) {
            pSettingOption->setValue(sName);
            sNameLast = sName;
        }
        QFileInfo fileinfo(sName);
        if ((!fileinfo.isReadable() || !fileinfo.isWritable()) && !sName.isEmpty()) {
            pPrompt->show();
        }
    });

    pSettingOption->connect(pLineEdit, &DLineEdit::editingFinished, pSettingOption, [ = ]() {
        qDebug() << "Entering DLineEdit::editingFinished";
        QString name = pLineEdit->text();
        QDir dir(name);

        auto pn = ElideText(name, {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                            pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
        auto nmls = ElideText(sNameLast, {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                              pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);

        if (!validate(pLineEdit->text(), false)) {
            QFileInfo fn(dir.path());
            if ((!fn.isReadable() || !fn.isWritable()) && !name.isEmpty()) {
                pPrompt->show();
            }
        }
        if (!pLineEdit->lineEdit()->hasFocus()) {
            if (validate(pLineEdit->text(), false)) {
                pSettingOption->setValue(pLineEdit->text());
                pLineEdit->setText(pn);
                sNameLast = name;
            } else if (pn == sElideText) {
                pLineEdit->setText(sElideText);
            } else {
                pSettingOption->setValue(sNameLast);
                pLineEdit->setText(nmls);
            }
        }
    });

    pSettingOption->connect(pLineEdit, &DLineEdit::textEdited, pSettingOption, [ = ](const QString & sNewStr) {
        qDebug() << "Entering DLineEdit::textEdited";
        validate(sNewStr);
    });

    pSettingOption->connect(pSettingOption, &DTK_CORE_NAMESPACE::DSettingsOption::valueChanged, pLineEdit,
    [ = ](const QVariant & value) {
        qDebug() << "Entering DSettingsOption::valueChanged";
        auto pi = ElideText(value.toString(), {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                            pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
        pLineEdit->setText(pi);
        pLineEdit->update();
    });

    qDebug() << "Exiting createSelectableLineEditOptionHandle";
    return  pOptionWidget;
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
                    qInfo() << "---------  leave " << dne->event << dne->child;
                    emit _source->windowLeaved();
                }
                break;
            }

            case XCB_ENTER_NOTIFY: {
                xcb_enter_notify_event_t *dne = (xcb_enter_notify_event_t *)event;
                auto w = _source->windowHandle();
                if (dne->event == w->winId()) {
                    qInfo() << "---------  enter " << dne->event << dne->child;
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

class MainWindowEventListener : public QObject
{
    Q_OBJECT
public:
    explicit MainWindowEventListener(QWidget *pTarget)
        : QObject(pTarget)
    {
        qDebug() << "Entering MainWindowEventListener constructor";
        lastCornerEdge = CornerEdge::NoneEdge;
        m_pMainWindow = static_cast<MainWindow *>(pTarget);
        m_pWindow = pTarget->windowHandle();
    }

    void setEnabled(bool bEnable)
    {
        qDebug() << "Entering MainWindowEventListener setEnabled";
        m_bEnabled = bEnable;
    }

protected:
    bool eventFilter(QObject *pObj, QEvent *pEvent) Q_DECL_OVERRIDE {
        qDebug() << "Entering MainWindowEventListener eventFilter";
        QWindow *pWindow = qobject_cast<QWindow *>(pObj);
        if (!pWindow) return false;

        MainWindow *pMainWindow = static_cast<MainWindow *>(parent());

        switch (static_cast<int>(pEvent->type()))
        {
        case QEvent::MouseMove+1: { //响应tab按钮
            QKeyEvent *pKeyEvent = static_cast<QKeyEvent *>(pEvent);
            //根据需求迷你模式不响应tab键交互
            if (pKeyEvent->key() == Qt::Key_Tab) {
                if (!m_pMainWindow->getMiniMode()) {
                    qDebug() << "is not mini mode";
                    pMainWindow->capturedKeyEvent(pKeyEvent);
                    //Only the tab key interactive response is set to the first
                    if (m_pMainWindow->playlist()->isFocusInPlaylist()) {
                        bool bFocusAttribute = true;
                        m_pMainWindow->playlist()->resetFocusAttribute(bFocusAttribute);
                    }
                } else {
                    qDebug() << "is mini mode,return true";
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            if (!m_pMainWindow->playlist()) {
                qDebug() << "is not playlist,return true";
                return true;
            }
            if (m_pMainWindow->playlist()->state() == PlaylistWidget::State::Opened) {
                m_pMainWindow->toolbox()->clearPlayListFocus();
            }
            //Mouse operation does not respond to the first item
            bool bFocusAttribute = false;
            m_pMainWindow->playlist()->resetFocusAttribute(bFocusAttribute);
            if (!m_bEnabled) return false;
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            setLeftButtonPressed(true);
            if (pMainWindow->insideResizeArea(pMouseEvent->globalPos()) && lastCornerEdge != CornerEdge::NoneEdge)
                m_bStartResizing = true;

            pMainWindow->capturedMousePressEvent(pMouseEvent);
            if (m_bStartResizing) {
                qDebug() << "is resizing,return true";
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_bEnabled) {
                qDebug() << "is not enabled,return false";
                return false;
            }
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            setLeftButtonPressed(false);
            qApp->setOverrideCursor(pWindow->cursor());

            pMainWindow->capturedMouseReleaseEvent(pMouseEvent);
            if (m_bStartResizing) {
                m_bStartResizing = false;
                qDebug() << "is resizing,return true";
                return true;
            }
            m_bStartResizing = false;
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            qDebug() << "mouse move";
            pMainWindow->resumeToolsWindow();

            /* If the focus is on the playlist button, move the mouse to cancel the focus
             * In order to avoid the enter key to expand and the mouse click to expand the playlist
             * There is a problem here, if the mouse does not move, click directly,
             * Will cause focus to appear on the clear list button
             * Please refer to the maintainer whether to add an event filter to the ListBtn
             */
            if (m_pMainWindow->toolbox()->getListBtnFocus()) {
                qDebug() << "toolbox getListBtnFocus";
                m_pMainWindow->setFocus();
            }
            //If window is maximized ,need quit maximize state when resizing
            if (m_bStartResizing && (pMainWindow->windowState() & Qt::WindowMaximized)) {
                qDebug() << "window is maximized";
                pMainWindow->setWindowState(pMainWindow->windowState() & (~Qt::WindowMaximized));
            } else if (m_bStartResizing && (pMainWindow->windowState() & Qt::WindowFullScreen)) {
                qDebug() << "window is fullscreen";
                pMainWindow->setWindowState(pMainWindow->windowState() & (~Qt::WindowFullScreen));
            }

            if (!m_bEnabled) {
                qDebug() << "is not enabled,return false";
                return false;
            }
            const QRect window_visible_rect = m_pWindow->frameGeometry() - pMainWindow->dragMargins();

            if (!m_bLeftButtonPressed) {
                //add by heyi  拦截鼠标移动事件
                qDebug() << "judgeMouseInWindow";
                pMainWindow->judgeMouseInWindow(QCursor::pos());
                CornerEdge mouseCorner = CornerEdge::NoneEdge;
                QRect cornerRect;

                /// begin set cursor corner type
                cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                cornerRect.moveTopLeft(m_pWindow->frameGeometry().topLeft());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    mouseCorner = CornerEdge::TopLeftCorner;
                    qDebug() << "top left corner, goto set_cursor";
                    goto set_cursor;
                }

                cornerRect.moveTopRight(m_pWindow->frameGeometry().topRight());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    mouseCorner = CornerEdge::TopRightCorner;
                    qDebug() << "top right corner, goto set_cursor";
                    goto set_cursor;
                }

                cornerRect.moveBottomRight(m_pWindow->frameGeometry().bottomRight());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    mouseCorner = CornerEdge::BottomRightCorner;
                    qDebug() << "bottom right corner, goto set_cursor";
                    goto set_cursor;
                }

                cornerRect.moveBottomLeft(m_pWindow->frameGeometry().bottomLeft());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    mouseCorner = CornerEdge::BottomLeftCorner;
                    qDebug() << "bottom left corner, goto set_cursor";
                    goto set_cursor;
                }

                goto skip_set_cursor; // disable edges

                /// begin set cursor edge type
                if (pMouseEvent->globalX() <= window_visible_rect.x()) {
                    qDebug() << "global x <= window_visible_rect.x()";
                    mouseCorner = CornerEdge::LeftEdge;
                } else if (pMouseEvent->globalX() < window_visible_rect.right()) {
                    qDebug() << "global x < window_visible_rect.right()";
                    if (pMouseEvent->globalY() <= window_visible_rect.y()) {
                        qDebug() << "global y <= window_visible_rect.y()";
                        mouseCorner = CornerEdge::TopEdge;
                    } else if (pMouseEvent->globalY() >= window_visible_rect.bottom()) {
                        qDebug() << "global y >= window_visible_rect.bottom()";
                        mouseCorner = CornerEdge::BottomEdge;
                    } else {
                        goto skip_set_cursor;
                    }
                } else if (pMouseEvent->globalX() >= window_visible_rect.right()) {
                    qDebug() << "global x >= window_visible_rect.right()";
                    mouseCorner = CornerEdge::RightEdge;
                } else {
                    qDebug() << "goto skip_set_cursor";
                    goto skip_set_cursor;
                }
set_cursor:
#ifdef USE_DXCB
#ifdef __mips__
                if (pWindow->property("_d_real_winId").isValid()) {
                    auto real_wid = pWindow->property("_d_real_winId").toUInt();
                    qDebug() << "real_wid:" << real_wid;
                    Utility::setWindowCursor(real_wid, mouseCorner);
                } else {
                    qDebug() << "pWindow->winId():" << pWindow->winId();
                    Utility::setWindowCursor(static_cast<quint32>(pWindow->winId()), mouseCorner);
                }
#endif
#endif

                if (qApp->mouseButtons() == Qt::LeftButton) {
                    qDebug() << "updateGeometry";
                    updateGeometry(mouseCorner, pMouseEvent);
                }
                lastCornerEdge = mouseCorner;
                return true;

skip_set_cursor:
                qDebug() << "skip_set_cursor";
                lastCornerEdge = mouseCorner = CornerEdge::NoneEdge;
                return false;
            } else {
                if (m_bStartResizing) {
                    qDebug() << "m_bStartResizing";
                    updateGeometry(lastCornerEdge, pMouseEvent);
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
    void setLeftButtonPressed(bool bPressed)
    {
        qDebug() << "setLeftButtonPressed";
        if (m_bLeftButtonPressed == bPressed) {
            qDebug() << "m_bLeftButtonPressed == bPressed,return";
            return;
        }

        if (!bPressed) {
#ifdef USE_DXCB
            qDebug() << "USE_DXCB";
            Utility::cancelWindowMoveResize(static_cast<quint32>(_window->winId()));
#endif
        }

        m_bLeftButtonPressed = bPressed;
        qDebug() << "setLeftButtonPressed finished";
    }

    void updateGeometry(CornerEdge edge, QMouseEvent *pEvent)
    {
        qDebug() << "updateGeometry";
        MainWindow *pMainWindow = static_cast<MainWindow *>(parent());
        pMainWindow->updateGeometry(edge, pEvent->globalPos());
    }

    bool m_bLeftButtonPressed = false;
    bool m_bStartResizing = false;
    bool m_bEnabled {true};
    CornerEdge lastCornerEdge;
    QWindow *m_pWindow;
    MainWindow *m_pMainWindow;
};

#ifdef USE_DXCB
/// shadow
#define SHADOW_COLOR_NORMAL QColor(0, 0, 0, 255 * 0.35)
#define SHADOW_COLOR_ACTIVE QColor(0, 0, 0, 255 * 0.6)
#endif

struct SessionInfo {
    QString sessionId;
    uint userId;
    QString userName;
    QString seatId;
    QDBusObjectPath sessionPath;
};
typedef QList<SessionInfo> SessionInfoList;

Q_DECLARE_METATYPE(SessionInfoList);
Q_DECLARE_METATYPE(SessionInfo);

inline QDBusArgument &operator<<(QDBusArgument &argument, const SessionInfo &sessionInfo)
{
    argument.beginStructure();
    argument << sessionInfo.sessionId;
    argument << sessionInfo.userId;
    argument << sessionInfo.userName;
    argument << sessionInfo.seatId;
    argument << sessionInfo.sessionPath;
    argument.endStructure();

    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, SessionInfo &sessionInfo)
{
    argument.beginStructure();
    argument >> sessionInfo.sessionId;
    argument >> sessionInfo.userId;
    argument >> sessionInfo.userName;
    argument >> sessionInfo.seatId;
    argument >> sessionInfo.sessionPath;
    argument.endStructure();

    return argument;
}

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(nullptr)
{
    qDebug() << "Initializing MainWindow";
    initMember();
    qDebug() << "initMember() finished.";

    QJsonObject obj{
        {"tid", EventLogUtils::Start},
        {"mode", 1}, //冷启动
        {"version", VERSION}
    };
    qDebug() << "Writing start event log.";
    EventLogUtils::get().writeLogs(obj);
    qDebug() << "Start event log written.";

    //add bu heyi
    this->setAttribute(Qt::WA_AcceptTouchEvents);
    qDebug() << "Set Qt::WA_AcceptTouchEvents attribute.";
    m_mousePressTimer.setInterval(1300);
    qDebug() << "Set mousePressTimer interval to 1300ms.";
    connect(&m_mousePressTimer, &QTimer::timeout, this, &MainWindow::slotmousePressTimerTimeOut);
    qDebug() << "Connected mousePressTimer timeout signal.";

#ifdef USE_DXCB
    qDebug() << "Using DXCB platform";
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowTitleHint | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#else
    qDebug() << "Using standard window flags";
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
#ifdef Q_OS_MACOS
    qDebug() << "Q_OS_MACOS defined. Setting WindowFullscreenButtonHint.";
    setWindowFlags(Qt::WindowFullscreenButtonHint);
#endif
#endif
    setAcceptDrops(true);
    qDebug() << "Set acceptDrops to true.";
    setAttribute(Qt::WA_NoSystemBackground, false);
    qDebug() << "Set Qt::WA_NoSystemBackground to false.";

#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform()) {
        qDebug() << "DApplication::isDXcbPlatform() is true. Initializing DPlatformWindowHandle.";
        _handle = new DPlatformWindowHandle(this, this);
        _handle->setEnableSystemResize(false);
        _handle->setEnableSystemMove(false);
        _handle->setWindowRadius(4);
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &MainWindow::updateShadow);
        updateShadow();
    }
#endif

    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    setContentsMargins(0, 0, 0, 0);
    setupTitlebar();

    CommandLineManager &commanLineManager = dmr::CommandLineManager::get();
    if (commanLineManager.debug()) {
        qDebug() << "Setting debug level to Debug";
        Backend::setDebugLevel(Backend::DebugLevel::Debug);
    } else if (commanLineManager.verbose()) {
        qDebug() << "Setting debug level to Verbose";
        Backend::setDebugLevel(Backend::DebugLevel::Verbose);
    }
    qRegisterMetaType<QList<QUrl>>("QList<QUrl>");
    m_pEngine = new PlayerEngine(this);

#ifndef USE_DXCB
    m_pEngine->move(0, 0);
#endif
    //初始化显示音量与音量条控件一致
    m_nDisplayVolume = 100;
    m_pToolbox = new ToolboxProxy(this, m_pEngine);
    m_pToolbox->setObjectName(BOTTOM_TOOL_BOX);

    titlebar()->deleteLater();

    connect(m_pToolbox, &ToolboxProxy::sigUnsupported, this, &MainWindow::slotUnsupported);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &MainWindow::slotPlayerStateChanged);
    connect(m_pEngine, &PlayerEngine::sigInvalidFile, this, &MainWindow::slotInvalidFile);
    connect(ActionFactory::get().mainContextMenu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);
    connect(this, &MainWindow::frameMenuEnable, &ActionFactory::get(), &ActionFactory::frameMenuEnable);
    connect(this, &MainWindow::playSpeedMenuEnable, &ActionFactory::get(), &ActionFactory::playSpeedMenuEnable);
    connect(this, &MainWindow::subtitleMenuEnable, &ActionFactory::get(), &ActionFactory::subtitleMenuEnable);
    connect(this, &MainWindow::soundMenuEnable, &ActionFactory::get(), &ActionFactory::soundMenuEnable);
    connect(qApp, &QGuiApplication::focusWindowChanged, this, &MainWindow::slotFocusWindowChanged);

    connect(m_pToolbox, &ToolboxProxy::sigVolumeChanged, this, &MainWindow::slotVolumeChanged);
    connect(m_pToolbox, &ToolboxProxy::sigMuteStateChanged, this, &MainWindow::slotMuteChanged);
    connect(m_pEngine, &PlayerEngine::sigMediaError, this, &MainWindow::slotMediaError);
    connect(m_pEngine, &PlayerEngine::finishedAddFiles, this, &MainWindow::slotFinishedAddFiles);
    qDebug() << "Connected PlayerEngine::finishedAddFiles signal.";

    //Initialization is performed at normal conditions
    if (CompositingManager::get().platform() != Platform::Mips) {
        qDebug() << "Platform is not Mips. Initializing MovieProgressIndicator and QLabel for full screen time.";
        m_pProgIndicator = new MovieProgressIndicator(this);
        m_pFullScreenTimeLable = new QLabel;
        qDebug() << "MovieProgressIndicator and QLabel initialized.";
    } else {
        qDebug() << "Platform is Mips. Skipping MovieProgressIndicator and QLabel initialization.";
    }

    if (m_pProgIndicator) {
        qDebug() << "MovieProgressIndicator is valid. Setting visibility and connecting elapsedChanged.";
        m_pProgIndicator->setVisible(false);
        connect(m_pEngine, &PlayerEngine::elapsedChanged, [ = ]() {
            qDebug() << "PlayerEngine elapsedChanged signal received. Updating movie progress.";
            m_pProgIndicator->updateMovieProgress(m_pEngine->duration(), m_pEngine->elapsed());
        });

        m_pFullScreenTimeLable->setAttribute(Qt::WA_TranslucentBackground);
        m_pFullScreenTimeLable->setWindowFlags(Qt::FramelessWindowHint);
        m_pFullScreenTimeLable->setParent(this);
        m_pFullScreenTimeLayout = new QHBoxLayout;
        m_pFullScreenTimeLayout->addStretch();
        m_pFullScreenTimeLayout->addWidget(m_pToolbox->getfullscreentimeLabel());
        m_pFullScreenTimeLayout->addWidget(m_pToolbox->getfullscreentimeLabelend());
        m_pFullScreenTimeLayout->addStretch();
        m_pFullScreenTimeLable->setLayout(m_pFullScreenTimeLayout);
        m_pFullScreenTimeLable->close();
        qDebug() << "FullScreenTimeLabel configured and closed.";
    } else {
        qDebug() << "MovieProgressIndicator is null. Skipping related configurations.";
    }

    // mini ui
    qDebug() << "Initializing mini UI components.";
    QSignalMapper *pSignalMapper = new QSignalMapper(this);
    qDebug() << "QSignalMapper initialized.";

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        // Qt5版本使用mapped信号
        qDebug() << "Qt5 version detected. Connecting QSignalMapper::mapped signal.";
        connect(pSignalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped), 
                this, &MainWindow::miniButtonClicked);
#else
        // Qt6版本使用mappedString信号
        qDebug() << "Qt6 version detected. Connecting QSignalMapper::mappedString signal.";
        connect(pSignalMapper, &QSignalMapper::mappedString,
                this, &MainWindow::miniButtonClicked); 
#endif
    qDebug() << "miniButtonClicked signal connected to QSignalMapper.";

    m_pMiniPlayBtn = new DIconButton(this);
    m_pMiniQuitMiniBtn = new DIconButton(this);
    m_pMiniCloseBtn = new DIconButton(this);
    qDebug() << "Mini UI buttons (play, quit, close) initialized.";

    m_pMiniPlayBtn->setFlat(true);
    m_pMiniCloseBtn->setFlat(true);
    m_pMiniQuitMiniBtn->setFlat(true);
    qDebug() << "Mini UI buttons set to flat style.";

    m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
    m_pMiniPlayBtn->setIconSize(QSize(30, 30));
    m_pMiniPlayBtn->setFixedSize(QSize(35, 35));
    m_pMiniPlayBtn->setObjectName("MiniPlayBtn");
    connect(m_pMiniPlayBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniPlayBtn, "play");
    qDebug() << "MiniPlayBtn configured and mapped to 'play' action.";

    connect(m_pEngine, &PlayerEngine::stateChanged, [ = ]() {
        qInfo() << "Player state changed to:" << m_pEngine->state();

        if (m_pEngine->state() == PlayerEngine::CoreState::Playing
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "Current file is raw format, disabling subtitle menu";
            emit subtitleMenuEnable(false);
        } else {
            qDebug() << "Current file is not raw format or not playing, enabling subtitle menu";
            emit subtitleMenuEnable(true);
        }

        if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
            qInfo() << "Player entered idle state";
            //播放切换时，更新音量dbus 当前的sinkInputPath
            if (m_pProgIndicator) {
                qDebug() << "Player idle: Hiding full screen time label and progress indicator.";
                m_pFullScreenTimeLable->close();
                m_pProgIndicator->setVisible(false);
            }
            qDebug() << "Player idle: Disabling frame and play speed menus.";
            emit frameMenuEnable(false);
            emit playSpeedMenuEnable(false);
        }

        if (m_pEngine->state() == PlayerEngine::CoreState::Playing) {
            qInfo() << "Player started playing";
            m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/pause-normal-mini.svg"));
            m_pMiniPlayBtn->setObjectName("MiniPauseBtn");

            if (m_pEngine->playlist().count() > 0 && !m_pEngine->currFileIsAudio()) {
                qDebug() << "Enabling frame menu for video playback";
                emit frameMenuEnable(true);
                setMusicShortKeyState(true);
            } else {
                qDebug() << "Disabling frame menu for audio playback";
                emit frameMenuEnable(false);
                setMusicShortKeyState(false);
            }
            emit playSpeedMenuEnable(true);
            if (m_nLastCookie > 0) {
                utils::UnInhibitStandby(m_nLastCookie);
                qInfo() << "Uninhibiting standby with cookie:" << m_nLastCookie;
                m_nLastCookie = 0;
            }
            if (m_nPowerCookie > 0) {
                utils::UnInhibitPower(m_nPowerCookie);
                m_nPowerCookie = 0;
            }
            m_nLastCookie = utils::InhibitStandby();
            m_nPowerCookie = utils::InhibitPower();
        } else {
            if (m_pMircastShowWidget->isVisible())
                return;
            qInfo() << "Player stopped playing";
            m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
            m_pMiniPlayBtn->setObjectName("MiniPlayBtn");

            if (m_nLastCookie > 0) {
                utils::UnInhibitStandby(m_nLastCookie);
                qInfo() << "Uninhibiting standby with cookie:" << m_nLastCookie;
                m_nLastCookie = 0;
            }
            if (m_nPowerCookie > 0) {
                utils::UnInhibitPower(m_nPowerCookie);
                m_nPowerCookie = 0;
            }
        }
    });

    m_pMiniCloseBtn->setIcon(QIcon(":/resources/icons/light/mini/close-normal.svg"));
    m_pMiniCloseBtn->setIconSize(QSize(30, 30));
    m_pMiniCloseBtn->setFixedSize(QSize(35, 35));
    m_pMiniCloseBtn->setObjectName("MiniCloseBtn");
    connect(m_pMiniCloseBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniCloseBtn, "close");

    m_pMiniQuitMiniBtn->setIcon(QIcon(":/resources/icons/light/mini/restore-normal-mini.svg"));
    m_pMiniQuitMiniBtn->setIconSize(QSize(30, 30));
    m_pMiniQuitMiniBtn->setFixedSize(QSize(35, 35));
    m_pMiniQuitMiniBtn->setObjectName("MiniQuitMiniBtn");
    connect(m_pMiniQuitMiniBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniQuitMiniBtn, "quit_mini");

    m_pMiniPlayBtn->setVisible(m_bMiniMode);
    m_pMiniCloseBtn->setVisible(m_bMiniMode);
    m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);

    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();          //绑定要放在connect后
    connect(m_pEngine, SIGNAL(stateChanged()), this, SLOT(update()));
    connect(m_pEngine, &PlayerEngine::tracksChanged, this, &MainWindow::updateActionsState);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &MainWindow::updateActionsState);
    updateActionsState();

#ifdef DTKWIDGET_CLASS_DSizeMode
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        m_pTitlebar->setFixedHeight(40);
        m_pMiniPlayBtn->setIconSize(QSize(19, 19));
        m_pMiniPlayBtn->setFixedSize(QSize(23, 23));
        m_pMiniCloseBtn->setIconSize(QSize(19, 19));
        m_pMiniCloseBtn->setFixedSize(QSize(23, 23));
        m_pMiniQuitMiniBtn->setIconSize(QSize(19, 19));
        m_pMiniQuitMiniBtn->setFixedSize(QSize(23, 23));
    }

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (sizeMode == DGuiApplicationHelper::NormalMode) {
            m_pTitlebar->setFixedHeight(50);
            m_pMiniPlayBtn->setIconSize(QSize(30, 30));
            m_pMiniPlayBtn->setFixedSize(QSize(35, 35));
            m_pMiniCloseBtn->setIconSize(QSize(30, 30));
            m_pMiniCloseBtn->setFixedSize(QSize(35, 35));
            m_pMiniQuitMiniBtn->setIconSize(QSize(30, 30));
            m_pMiniQuitMiniBtn->setFixedSize(QSize(35, 35));
        } else {
            m_pTitlebar->setFixedHeight(40);
            m_pMiniPlayBtn->setIconSize(QSize(19, 19));
            m_pMiniPlayBtn->setFixedSize(QSize(23, 23));
            m_pMiniCloseBtn->setIconSize(QSize(19, 19));
            m_pMiniCloseBtn->setFixedSize(QSize(23, 23));
            m_pMiniQuitMiniBtn->setIconSize(QSize(19, 19));
            m_pMiniQuitMiniBtn->setFixedSize(QSize(23, 23));
        }
    });

    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::sizeModeChanged, this, [=](DGuiApplicationHelper::SizeMode sizeMode) {
        if (m_bMiniMode) return;
        m_pCommHintWid->hide();
        updateProxyGeometry();
    });
#endif

    //勾选右键菜单默认选项
    reflectActionToUI(ActionFactory::ActionKind::OneTimes);
    reflectActionToUI(ActionFactory::ActionKind::ChangeSubCodepage);
    reflectActionToUI(ActionFactory::ActionKind::DefaultFrame);
    reflectActionToUI(ActionFactory::ActionKind::Stereo);

    prepareSplashImages();

    connect(m_pEngine, &PlayerEngine::sidChanged, [ = ]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    });
    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    connect(m_pEngine, &PlayerEngine::aidChanged, [ = ]() {
        reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    });
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &MainWindow::slotFileLoaded);

    connect(m_pEngine, &PlayerEngine::videoSizeChanged, [ = ]() {
        this->resizeByConstraints();
    });
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &MainWindow::animatePlayState);

    connect(m_pEngine, &PlayerEngine::loadOnlineSubtitlesFinished,
            [this](const QUrl & url, bool success) {//不能去掉 url参数
        qInfo() << "Online subtitle loading finished for URL:" << url.toString() << "Success:" << success;
        m_pCommHintWid->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
    });

    connect(&m_autoHideTimer, &QTimer::timeout, this, &MainWindow::suspendToolsWindow);
    m_autoHideTimer.setSingleShot(true);

    connect(&m_delayedMouseReleaseTimer, &QTimer::timeout, this, &MainWindow::delayedMouseReleaseHandler);
    m_delayedMouseReleaseTimer.setSingleShot(true);

    m_pCommHintWid = new NotificationWidget(this);
    m_pCommHintWid->setFixedHeight(30);
    m_pCommHintWid->setAnchor(NotificationWidget::ANCHOR_NORTH_WEST);
    m_pCommHintWid->setAnchorPoint(QPoint(30, 58));
    m_pCommHintWid->hide();
    m_pDVDHintWid = new NotificationWidget(this);
    m_pDVDHintWid->setFixedHeight(30);
    m_pDVDHintWid->setAnchor(NotificationWidget::ANCHOR_NORTH_WEST);
    m_pDVDHintWid->setAnchorPoint(QPoint(30, 58));
    m_pDVDHintWid->hide();

    m_backgroundWidget = new QOpenGLWidget(this);
    m_backgroundWidget->hide();

#ifdef USE_DXCB
    m_pEventListener = new MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(m_pEventListener);

    //auto mwfm = new MainWindowFocusMonitor(this);
    auto mwpm = new MainWindowPropertyMonitor(this);

    connect(this, &MainWindow::windowEntered, &MainWindow::resumeToolsWindow);
    connect(this, &MainWindow::windowLeaved, &MainWindow::suspendToolsWindow);

#else
    winId();
    m_pEventListener = new MainWindowEventListener(this);
    QTimer::singleShot(500, [this](){
        this->windowHandle()->installEventFilter(m_pEventListener);

        connect(this, &MainWindow::windowEntered, &MainWindow::resumeToolsWindow);
        connect(this, &MainWindow::windowLeaved, &MainWindow::suspendToolsWindow);
        qInfo() << "event listener";
    } );

#endif

    m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
    m_pCommHintWid->setWM(m_bIsWM);
    connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasBlurWindowChanged, this, &MainWindow::slotWMChanged);
    m_pAnimationlable = new AnimationLabel(this, this);
    m_pAnimationlable->setWM(m_bIsWM);

    m_pPopupWid = new MessageWindow(this);
    m_pPopupWid->hide();
    defaultplaymodeinit();

    connect(&Settings::get(), &Settings::defaultplaymodechanged, this, &MainWindow::slotdefaultplaymodechanged);
    connect(&Settings::get(), &Settings::setDecodeModel, this, &MainWindow::onSetDecodeModel,Qt::DirectConnection);
    connect(&Settings::get(), &Settings::refreshDecode, this, &MainWindow::onRefreshDecode,Qt::DirectConnection);
    connect(m_pEngine, &PlayerEngine::onlineStateChanged, this, &MainWindow::checkOnlineState);
    connect(&OnlineSubtitle::get(), &OnlineSubtitle::onlineSubtitleStateChanged, this, &MainWindow::checkOnlineSubtitle);
    connect(m_pEngine, &PlayerEngine::mpvErrorLogsChanged, this, &MainWindow::checkErrorMpvLogsChanged);
    connect(m_pEngine, &PlayerEngine::mpvWarningLogsChanged, this, &MainWindow::checkWarningMpvLogsChanged);
    connect(m_pEngine, &PlayerEngine::urlpause, this, &MainWindow::slotUrlpause);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance, this, [ = ] {
        qInfo() << "New process instance detected, activating window";
        this->activateWindow();
    });
    connect(qApp, &QGuiApplication::fontChanged, this, &MainWindow::slotFontChanged);

    ThreadPool::instance()->moveToNewThread(&m_diskCheckThread);
    m_diskCheckThread.start();
    connect(&m_diskCheckThread, &Diskcheckthread::diskRemove, this, &MainWindow::diskRemoved);

    QTimer::singleShot(300, [this]() {
        qInfo() << "Loading playlist after initialization";
        loadPlayList();
    });

    m_pDBus = new QDBusInterface("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
    connect(m_pDBus, SIGNAL(PrepareForSleep(bool)), this, SLOT(sleepStateChanged(bool)));

    QDBusConnection::sessionBus().connect("org.deepin.dde.ShutdownFront1", "/org/deepin/dde/lockFront1",
                                          "org.deepin.dde.lockFront1", "Visible", this,
                                          SLOT(lockStateChanged(bool)));

    m_pMovieWidget = new MovieWidget(this);
    m_pMovieWidget->hide();

    m_pMircastShowWidget = new MircastShowWidget(this);
    m_pMircastShowWidget->hide();
    connect(m_pToolbox, &ToolboxProxy::sigMircastState, this, &MainWindow::slotUpdateMircastState);
    connect(m_pMircastShowWidget, &MircastShowWidget::exitMircast, this, &MainWindow::slotExitMircast);

    qDBusRegisterMetaType<SessionInfo>();
    qDBusRegisterMetaType<SessionInfoList>();
    QDBusPendingReply<SessionInfoList> reply = m_pDBus->call("ListSessions");
    QString path = reply.value().last().sessionPath.path();

    QDBusConnection::systemBus().connect("org.freedesktop.login1", path,
                                         "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                                         SLOT(slotProperChanged(QString, QVariantMap, QStringList)));
    qInfo() << "session Path is :" << path;
    connect(dynamic_cast<MpvProxy *>(m_pEngine->getMpvProxy()),&MpvProxy::crashCheck,&Settings::get(),&Settings::crashCheck);
    //解码初始化
   decodeInit();
}

void MainWindow::setupTitlebar()
{
    qDebug() << "Setting up titlebar";
    m_pTitlebar = new Titlebar(this);
    m_pTitlebar->move(0, 0);
    m_pTitlebar->setFixedHeight(50);
    setTitlebarShadowEnabled(false);
    m_pTitlebar->titlebar()->setMenu(ActionFactory::get().titlebarMenu());
    connect(m_pTitlebar->titlebar()->menu(), &DMenu::triggered, this, &MainWindow::menuItemInvoked);
    qDebug() << "Titlebar setup completed";
}

void MainWindow::updateContentGeometry(const QRect &rect)
{
    qDebug() << "updateContentGeometry";
#ifdef USE_DXCB
    auto frame = QWindow::fromWinId(windowHandle()->winId());

    QRect frame_rect = rect;
    if (_handle) {
        frame_rect += _handle->frameMargins();
        qDebug() << "Frame margins applied:" << frame_rect;
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
    qDebug() << "Window moved and resized:" << rect;
#endif
}

#ifdef USE_DXCB
void MainWindow::updateShadow()
{
    qDebug() << "Updating window shadow, active:" << isActiveWindow();
    if (isActiveWindow()) {
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_ACTIVE);
        qDebug() << "Set active shadow";
    } else {
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_NORMAL);
        qDebug() << "Set normal shadow";
    }
}
#endif

bool MainWindow::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::UpdateRequest || pEvent->type() == QEvent::Paint)
        return DMainWindow::event(pEvent);

    if (pEvent->type() == QEvent::TouchBegin) {
        qInfo() << "Touch event detected";
        this->m_posMouseOrigin = mapToGlobal(QCursor::pos());
        m_bIsTouch = true;
    }

    //add by heyi
    //判断是否停止右键菜单定时器
    if (m_bMousePressed) {
        if (qAbs(m_nLastPressX - mapToGlobal(QCursor::pos()).x()) > 50 || qAbs(m_nLastPressY - mapToGlobal(QCursor::pos()).y()) > 50) {
            if (m_mousePressTimer.isActive()) {
                qInfo() << "Stopping mouse press timer due to movement";
                m_mousePressTimer.stop();
                m_bMousePressed = false;
            }
        }
    }

    if (pEvent->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *pWindowStateChangeEvent = dynamic_cast<QWindowStateChangeEvent *>(pEvent);
        m_lastWindowState = pWindowStateChangeEvent->oldState();
        qInfo() << "Window state changed - Previous:" << m_lastWindowState << "Current:" << windowState();
        
        if (windowState() & Qt::WindowMinimized) {   //fix bug 53683
            qDebug() << "Window minimized";
            if (Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (m_pEngine && m_pEngine->state() == PlayerEngine::Playing) {
                    qDebug() << "Pausing playback due to minimize";
                    requestAction(ActionFactory::TogglePause);
                    m_bQuitfullscreenflag = true;
                }
                QList<QAction *> listActs = ActionFactory::get().findActionsByKind(ActionFactory::TogglePlaylist);
                listActs.at(0)->setChecked(false);
            }
        } else if (m_lastWindowState & Qt::WindowMinimized) {
            qDebug() << "Window restored from minimized state";
            if (Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (m_bQuitfullscreenflag) {
                    qDebug() << "Resuming playback after restore";
                    requestAction(ActionFactory::TogglePause);
                    m_bQuitfullscreenflag = false;
                }
            }
        }
        onWindowStateChanged();
    }
    return DMainWindow::event(pEvent);
}

void MainWindow::leaveEvent(QEvent *)
{
    m_autoHideTimer.stop();
    this->suspendToolsWindow();
}

void MainWindow::onWindowStateChanged()
{
    qInfo() << __func__ << windowState();
    if (!m_bMiniMode && !isFullScreen()) {
        qDebug() << "Setting titlebar visibility based on toolbox:" << m_pToolbox->isVisible();
        m_pTitlebar->setVisible(m_pToolbox->isVisible());
    } else {
        qDebug() << "Hiding titlebar in mini mode or fullscreen";
        m_pTitlebar->setVisible(false);
    }

    //The X86 platform draws on GiWidget, and the MIPS platform does not need to draw
    if (CompositingManager::get().platform() == Platform::Arm64 || CompositingManager::get().platform() == Platform::Alpha) {
        bool showIndicator = isFullScreen() && m_pEngine && m_pEngine->state() != PlayerEngine::Idle;
        qDebug() << "Setting progress indicator visibility:" << showIndicator;
        m_pProgIndicator->setVisible(showIndicator);
    }

#ifndef USE_DXCB
    qDebug() << "Moving titlebar and engine to (0,0)";
    m_pTitlebar->move(0, 0);
    m_pEngine->move(0, 0);
#endif

    if (!isFullScreen() && !isMaximized()) {
        if (m_bMovieSwitchedInFsOrMaxed || !m_lastRectInNormalMode.isValid()) {
            if (m_bMousePressed || m_bMouseMoved) {
                qDebug() << "Delaying resize due to mouse activity";
                m_bDelayedResizeByConstraint = true;
            } else {
                qDebug() << "Resizing window immediately";
                setMinimumSize({0, 0});
                resizeByConstraints(true);
            }
        }

        m_bMovieSwitchedInFsOrMaxed = false;
    }
    qDebug() << "Updating window";
    update();

    if (isMinimized()) {
        if (m_pPlaylist->state() == PlaylistWidget::Opened) {
            qDebug() << "Closing playlist on minimize";
            m_pPlaylist->togglePopup(false);
        }
    }
}

#ifdef USE_DXCB
static QPoint lastm_pEngine_pos;
static QPoint last_wm_pos;
static bool bClicked = false;
void MainWindow::onMonitorButtonPressed(int nX, int nY)
{
    qDebug() << "onMonitorButtonPressed";
    QPoint pos(nX, nY);
    int nPoint = 2;
    QMargins m(nPoint, nPoint, nPoint, nPoint);
    if (geometry().marginsRemoved(m).contains(pos)) {
        QWidget *pWidget = qApp->topLevelAt(pos);
        if (pWidget && pWidget == this) {
            qInfo() << __func__ << "click inside main window";
            last_wm_pos = QPoint(nX, nY);
            lastm_pEngine_pos = windowHandle()->framePosition();
            bClicked = true;
            qDebug() << "Window position saved:" << lastm_pEngine_pos;
        }
    }
}

void MainWindow::onMonitorButtonReleased(int nX, int nY)
{
    qDebug() << "onMonitorButtonReleased";
    if (bClicked) {
        qInfo() << __func__;
        bClicked = false;
        qDebug() << "Click state reset";
    }
}

void MainWindow::onMonitorMotionNotify(int nX, int nY)
{
    qDebug() << "onMonitorMotionNotify";
    if (bClicked) {
        QPoint pos = QPoint(nX, nY) - last_wm_pos;
        windowHandle()->setFramePosition(lastm_pEngine_pos + pos);
        qDebug() << "Window moved to:" << lastm_pEngine_pos + pos;
    }
}
#endif

bool MainWindow::judgeMouseInWindow(QPoint pos)
{
    qDebug() << "judgeMouseInWindow";
    bool bRet = false;
    QRect rect = frameGeometry();
    QPoint topLeft = rect.topLeft();
    QPoint bottomRight = rect.bottomRight();
    pos = mapToGlobal(pos);
    topLeft = mapToGlobal(topLeft);
    bottomRight = mapToGlobal(bottomRight);

    if ((pos.x() == topLeft.x()) || (pos.x() == bottomRight.x()) || (pos.y() == topLeft.y()) || (pos.y() == bottomRight.y())) {
        qDebug() << "Mouse at window edge, triggering leave event";
        leaveEvent(nullptr);
    }

    return bRet;
}

#ifdef USE_DXCB
void MainWindow::onApplicationStateChanged(Qt::ApplicationState e)
{
    qDebug() << "onApplicationStateChanged";
    switch (e) {
    case Qt::ApplicationActive:
        if (qApp->focusWindow())
            qInfo() << QString("focus window 0x%1").arg(qApp->focusWindow()->winId(), 0, 16);
        qApp->setActiveWindow(this);
        _evm->resumeRecording();
        resumeToolsWindow();
        qDebug() << "Application activated, recording resumed";
        break;

    case Qt::ApplicationInactive:
        _evm->suspendRecording();
        suspendToolsWindow();
        qDebug() << "Application deactivated, recording suspended";
        break;

    default:
        break;
    }
}
#endif

void MainWindow::animatePlayState()
{
    qDebug() << "animatePlayState";
    if (m_bMiniMode) {
        qDebug() << "Skipping animation in mini mode";
        return;
    }

    if (!m_bInBurstShootMode && m_pEngine->state() == PlayerEngine::CoreState::Paused
            && !m_bMiniMode && !m_pMircastShowWidget->isVisible()) {
        qDebug() << "Pausing animation";
        m_pAnimationlable->pauseAnimation();
    }
}

void MainWindow::onBindingsChanged()
{
    qDebug() << "Clearing existing actions";
    {
        QList<QAction *> listActions = this->actions();
        this->actions().clear();
        for (auto *pAct : listActions) {
            delete pAct;
        }
    }

    ShortcutManager &shortcutManager = ShortcutManager::get();
    vector<QAction *> vecActions = shortcutManager.actionsForBindings();
    qDebug() << "Adding" << vecActions.size() << "new actions";
    for (auto *pAct : vecActions) {
        this->addAction(pAct);
        connect(pAct, &QAction::triggered, [ = ]() {
            ActionFactory::ActionKind actionKind = ActionFactory::actionKind(pAct);
            //正在投屏时，某些快捷键设置为不能用
            if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ){
                if(actionKind == ActionFactory::ToggleFullscreen  //全屏 alt+enter
                        || actionKind == ActionFactory::QuitFullscreen //退出全屏/迷你模式esc
                        || actionKind == ActionFactory::AccelPlayback //加速播放 ctrl+right
                        || actionKind == ActionFactory::DecelPlayback //减速播放 ctrl+left
                        || actionKind == ActionFactory::ResetPlayback //还原播放 R
                        || actionKind == ActionFactory::ToggleMiniMode //迷你模式 F2
                        || actionKind == ActionFactory::VolumeUp //增大音量 ctrl+alt+up
                        || actionKind == ActionFactory::VolumeDown //减少音量 ctrl+alt+down
                        || actionKind == ActionFactory::ToggleMute //静音 M
                        || actionKind == ActionFactory::PreviousFrame //上一帧 ctrl+shift+left
                        || actionKind == ActionFactory::NextFrame //下一帧 ctrl+shift+right
                        || actionKind == ActionFactory::Screenshot //影片截图 alt+a
                        || actionKind == ActionFactory::BurstScreenshot //连拍截图 alt+s
                        || actionKind == ActionFactory::SubForward //字幕提前0.5s shift+right
                        || actionKind == ActionFactory::SubDelay //字幕延迟0.5s shift+left
                        || actionKind == ActionFactory::ViewShortcut //显示快捷键 ctrl + shift + ?
                  ){
                    qDebug() << "Skipping disabled shortcut during casting:" << actionKind;
                    return;
                }
            }
            this->menuItemInvoked(pAct);
        });
    }
}

void MainWindow::updateActionsState()
{
    qDebug() << "Updating actions state";
    //投屏时不处理播放状态切换菜单项是否可用，由右键菜单入口统一处理。
    if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) {
        qDebug() << "Skipping action state update during casting";
        return;
    }
    PlayingMovieInfo movieInfo = m_pEngine->playingMovieInfo();
    auto update = [ = ](QAction * pAct) {
        ActionFactory::ActionKind actionKind = ActionFactory::actionKind(pAct);
        bool bRet = true;
        switch (actionKind) {
        case ActionFactory::ActionKind::Screenshot:
        case ActionFactory::ActionKind::MatchOnlineSubtitle:
        case ActionFactory::ActionKind::ToggleMiniMode:
        case ActionFactory::ActionKind::ToggleFullscreen:
        case ActionFactory::ActionKind::WindowAbove:
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            qDebug() << "Action" << actionKind << "enabled:" << bRet;
            break;
        case ActionFactory::ActionKind::BurstScreenshot:
            if(!CompositingManager::isMpvExists()) {
                bRet = false;
            } else {
                bRet = m_pEngine->duration() > 40;
            }
            qDebug() << "Burst screenshot enabled:" << bRet;
            break;
        case ActionFactory::ActionKind::MovieInfo:
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            if (bRet) {
                bRet = bRet && m_pEngine->playlist().count();
                if (bRet) {
                    PlayItemInfo playItemInfo = m_pEngine->playlist().currentInfo();
                    bRet = bRet && playItemInfo.loaded;
                }
            }
            qDebug() << "Movie info enabled:" << bRet;
            break;

        case ActionFactory::ActionKind::HideSubtitle:
        case ActionFactory::ActionKind::SelectSubtitle:
            bRet = movieInfo.subs.size() > 0;
            qDebug() << "Subtitle actions enabled:" << bRet;
            break;
        default:
            break;
        }
        pAct->setEnabled(bRet);
    };

    ActionFactory::get().updateMainActionsForMovie(movieInfo);
    ActionFactory::get().forEachInMainMenu(update);

    //NOTE: mpv does not always send a aid-change signal the first time movie is loaded.
    //so we need to workaround it.
    qDebug() << "Reflecting track and subtitle selection to UI";
    reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
}

/*void MainWindow::syncStaysOnTop()
{
#ifdef USE_DXCB
    static xcb_atom_t atomStateAbove = Utility::internAtom("_NET_WM_STATE_ABOVE");
    auto atoms = Utility::windowNetWMState(static_cast<quint32>(windowHandle()->winId()));

#ifndef __mips__
    bool window_is_above = atoms.contains(atomStateAbove);
    if (window_is_above != m_bWindowAbove) {
        qInfo() << "syncStaysOnTop: window_is_above" << window_is_above;
        requestAction(ActionFactory::WindowAbove);
    }
#endif
#endif
}*/

void MainWindow::reflectActionToUI(ActionFactory::ActionKind actionKind)
{
    qDebug() << "reflectActionToUI";
    QList<QAction *> listActs;
    listActs = ActionFactory::get().findActionsByKind(actionKind);
    if(listActs.size()<=0) {
        qDebug() << "No actions found for kind:" << actionKind;
        return;
    }

    switch (actionKind) {
    case ActionFactory::ActionKind::WindowAbove:
    case ActionFactory::ActionKind::ToggleFullscreen:
    case ActionFactory::ActionKind::TogglePlaylist:
    case ActionFactory::ActionKind::HideSubtitle: {
        qInfo() << "Reflecting UI state for action:" << actionKind;
        auto p = listActs.begin();
        while (p != listActs.end()) {
            bool bOld = (*p)->isEnabled();
            (*p)->setEnabled(false);
            if (actionKind == ActionFactory::TogglePlaylist) {
                // here what we read is the last state of playlist
                if (m_pPlaylist->state() != PlaylistWidget::Opened) {
                    (*p)->setChecked(false);
                } else {
                    (*p)->setChecked(true);
                }
                qDebug() << "Playlist state changed to:" << (*p)->isChecked();
            } else {
                (*p)->setChecked(!(*p)->isChecked());
                qDebug() << "Action state toggled to:" << (*p)->isChecked();
            }
            (*p)->setEnabled(bOld);
            ++p;
        }
        break;
    }

    case ActionFactory::ActionKind::ToggleMiniMode: {
        auto p = listActs[0];
        qInfo() << "Toggling mini mode to:" << !p->isChecked();
        p->setEnabled(false);
        p->setChecked(!p->isChecked());
        p->setEnabled(true);
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        //mpv未初始化时返回默认值auto
        QString sCodePage;
        sCodePage = m_pEngine->subCodepage();
        qInfo() << "Changing subtitle codepage to:" << sCodePage;
        auto p = listActs.begin();
        while (p != listActs.end()) {
            auto args = ActionFactory::actionArgs(*p);
            if (args[0].toString() == sCodePage) {
                (*p)->setEnabled(false);
                if (!(*p)->isChecked())
                    (*p)->setChecked(true);
                (*p)->setEnabled(true);
                break;
            }
            ++p;
        }
        break;
    }

    case ActionFactory::ActionKind::SelectTrack:
    case ActionFactory::ActionKind::SelectSubtitle: {
        if (m_pEngine->state() == PlayerEngine::Idle) {
            qDebug() << "Player is idle, skipping track/subtitle selection";
            break;
        }

        PlayingMovieInfo pmf = m_pEngine->playingMovieInfo();
        int nId = -1;
        int nIdx = -1;
        if (actionKind == ActionFactory::ActionKind::SelectTrack) {
            nId = m_pEngine->aid();
            for (nIdx = 0; nIdx < pmf.audios.size(); nIdx++) {
                if (nId == pmf.audios[nIdx]["id"].toInt()) {
                    break;
                }
            }
            qInfo() << "Selecting audio track, ID:" << nId << "Index:" << nIdx;
        } else if (actionKind == ActionFactory::ActionKind::SelectSubtitle) {
            nId = m_pEngine->sid();
            for (nIdx = 0; nIdx < pmf.subs.size(); nIdx++) {
                if (nId == pmf.subs[nIdx]["id"].toInt()) {
                    break;
                }
            }
            qInfo() << "Selecting subtitle track, ID:" << nId << "Index:" << nIdx;
        }

        auto p = listActs.begin();
        while (p != listActs.end()) {
            auto args = ActionFactory::actionArgs(*p);
            (*p)->setEnabled(false);
            if (args[0].toInt() == nIdx) {
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
        qInfo() << "Setting default state for action:" << actionKind;
        auto p = listActs.begin();
        (*p)->setChecked(true);
        break;
    }
    case ActionFactory::ActionKind::DefaultFrame: {
        qInfo() << "Toggling default frame state";
        auto p = listActs.begin();
        bool bOld = (*p)->isEnabled();
        (*p)->setEnabled(false);
        (*p)->setChecked(!(*p)->isChecked());
        (*p)->setEnabled(bOld);
        break;
    }
    case ActionFactory::ActionKind::OrderPlay:
    case ActionFactory::ActionKind::ShufflePlay:
    case ActionFactory::ActionKind::SinglePlay:
    case ActionFactory::ActionKind::SingleLoop:
    case ActionFactory::ActionKind::ListLoop: {
        qInfo() << "Setting play mode to:" << actionKind;
        auto p = listActs.begin();
        (*p)->setChecked(true);
        break;
    }
    default:
        qDebug() << "Unhandled action kind:" << actionKind;
        break;
    }
}

/*NotificationWidget *MainWindow::getm_pCommHintWid()
{
    return m_pCommHintWid;
}*/

//排列判断(主要针对光驱)
static bool compareBarData(const QUrl &url1, const QUrl &url2)
{
    qDebug() << "compareBarData";
    QString sFileName1 = QFileInfo(url1.path()).fileName();
    QString sFileName2 = QFileInfo(url2.path()).fileName();
    if (sFileName1.length() > 0 && sFileName2.length() > 0) {
        if (sFileName1[0] < sFileName2[0]) {
            qDebug() << "sFileName1 < sFileName2, return true";
            return true;
        }
    }
    qDebug() << "compareBarData, return false";
    return false;
}

bool MainWindow::addCdromPath()
{
    qDebug() << "addCdromPath";
    QStringList strCDMountlist;

    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        qWarning() << "Failed to open /proc/mounts file";
        return false;
    }
    do {
        QString strLine = mountFile.readLine();
        if (strLine.indexOf("/dev/sr") != -1 || strLine.indexOf("/dev/cdrom") != -1) {  //说明存在光盘的挂载。
            strCDMountlist.append(strLine.split(" ").at(1));        //A B C 这样的格式，取中间的
        }
    } while (!mountFile.atEnd());
    mountFile.close();

    if (strCDMountlist.size() == 0) {
        qInfo() << "No CD/DVD drive found";
        return false;
    }

    qInfo() << "Found CD/DVD drive at:" << strCDMountlist[0];
    play({strCDMountlist[0]});

    return true;
}

void MainWindow::loadPlayList()
{
    qInfo() << "Loading playlist...";
    m_pPlaylist = nullptr;
    m_pPlaylist = new PlaylistWidget(this, m_pEngine);
    m_pPlaylist->hide();
    m_pToolbox->setPlaylist(m_pPlaylist);
    m_pEngine->getplaylist()->loadPlaylist();

    if(CompositingManager::isMpvExists()) {
        qDebug() << "Initializing thumbnail thread for mpv backend";
        m_pToolbox->initThumbThread();
    }

    if (!m_listOpenFiles.isEmpty()) {
        qInfo() << "Playing files from open files list, count:" << m_listOpenFiles.size();
        play(m_listOpenFiles);
    }
}

void MainWindow::setOpenFiles(QStringList &list)
{
    qInfo() << "Setting open files list, count:" << list.size();
    m_listOpenFiles = list;
}

QString MainWindow::padLoadPath()
{
    qInfo() << "Getting pad load path";
    QString sLoadPath = Settings::get().generalOption("pad_load_path").toString();
    QDir lastDir(sLoadPath);
    if (sLoadPath.isEmpty() || !lastDir.exists()) {
        sLoadPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir newLastDir(sLoadPath);
        if (!newLastDir.exists()) {
            qInfo() << "Movies location does not exist, using current path";
            sLoadPath = QDir::currentPath();
        }
    }

    qInfo() << "Pad load path:" << sLoadPath;
    return sLoadPath;
}

#ifdef USE_TEST
void MainWindow::testCdrom()
{
    this->addCdromPath();
    diskRemoved("sd3/uos");
    sleepStateChanged(true);
    sleepStateChanged(false);
    subtitleMatchVideo("/data/home/uos/Videos/subtitle/Hachiko.A.Dog's.Story.ass");
}
void MainWindow::setCurrentHwdec(QString str)
{
    m_sCurrentHwdec = str;
}
#endif

void MainWindow::mipsShowFullScreen()
{
    qDebug() << "mipsShowFullScreen";
    QPropertyAnimation *pAn = new QPropertyAnimation(this, "windowOpacity");
    pAn->setDuration(100);
    pAn->setEasingCurve(QEasingCurve::Linear);
    pAn->setEndValue(1);
    pAn->setStartValue(0);
    pAn->start(QAbstractAnimation::DeleteWhenStopped);

    setWindowState(windowState() | Qt::WindowFullScreen);
}

void MainWindow::menuItemInvoked(QAction *pAction)
{
    qDebug() << "menuItemInvoked";
    ActionFactory::ActionKind actionKind = ActionFactory::actionKind(pAction);
    if (actionKind == dmr::ActionFactory::Invalid || !m_pEngine || !m_pPlaylist) {  //如果未初始化触发快捷键会导致崩溃
        return;
    }
    bool bIsShortcut = ActionFactory::isActionFromShortcut(pAction);
    if (ActionFactory::actionHasArgs(pAction)) {
        qDebug() << "ActionFactory::actionHasArgs";
        requestAction(actionKind, !bIsShortcut, ActionFactory::actionArgs(pAction), bIsShortcut);
    } else {
        QVariant var = pAction->property("kind");
        if (var == ActionFactory::ActionKind::Settings) {
            qDebug() << "var == ActionFactory::ActionKind::Settings";
            requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
        } else {
            if (m_pPlaylist->state() == PlaylistWidget::State::Opened) {
                qDebug() << "m_pPlaylist->state() == PlaylistWidget::State::Opened";
                BindingMap bdMap = ShortcutManager::get().map();
                QHash<QKeySequence, ActionFactory::ActionKind>::const_iterator iter = bdMap.constBegin();
                bool bIsiter = false;
                while (iter != bdMap.constEnd()) {
                    if (iter.value() == actionKind) {
                        qDebug() << "iter.value() == actionKind";
                        bIsiter = true;
                        if ((iter.key() == QKeySequence("Return")
                                || iter.key() == QKeySequence("Enter")
                                || iter.key() == QKeySequence("Up")
                                || iter.key() == QKeySequence("Down")) && bIsShortcut) {
                            if (iter.key() == QKeySequence("Up") || iter.key() == QKeySequence("Down")) {
                                qDebug() << "iter.key() == QKeySequence(\"Up\") || iter.key() == QKeySequence(\"Down\")";
                                int key;
                                if (iter.key() == QKeySequence("Up")) {
                                    qDebug() << "iter.key() == QKeySequence(\"Up\")";
                                    key = Qt::Key_Up;
                                } else {
                                    qDebug() << "iter.key() == QKeySequence(\"Down\")";
                                    key = Qt::Key_Down;
                                }
                                m_pPlaylist->updateSelectItem(key);
                            }
                            break;
                        }
                        qDebug() << "requestAction";
                        requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
                        break;
                    }
                    ++iter;
                }
                if (bIsiter == false) {
                    qDebug() << "bIsiter == false";
                    requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
                }
            } else {
                qDebug() << "m_pPlaylist->state() != PlaylistWidget::State::Opened";
                requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
            }
        }
    }
    //菜单操作完成后，标题栏获取焦点
    m_pTitlebar->setFocus();
}

bool MainWindow::isActionAllowed(ActionFactory::ActionKind actionKind, bool fromUI, bool isShortcut)
{
    qDebug() << "isActionAllowed";
    if (m_bInBurstShootMode) {
        qDebug() << "m_bInBurstShootMode, return false";
        return false;
    }

    if (m_bMiniMode) {
        qDebug() << "m_bMiniMode";
        if (fromUI || isShortcut) {
            switch (actionKind) {
            case ActionFactory::ToggleFullscreen:
            case ActionFactory::TogglePlaylist:
            case ActionFactory::BurstScreenshot:
                qDebug() << "m_bMiniMode, return false";
                return false;

            case ActionFactory::ToggleMiniMode:
                qDebug() << "m_bMiniMode, return true";
                return true;

            default:
                qDebug() << "m_bMiniMode, default";
                break;
            }
        }
    }

    if (isMaximized()) {
        switch (actionKind) {
        case ActionFactory::ToggleMiniMode:
            qDebug() << "isMaximized, return true";
            return true;
        default:
            qDebug() << "isMaximized, default";
            break;
        }
    }

    if (isShortcut) {
        qDebug() << "isShortcut";
        PlayingMovieInfo pmf = m_pEngine->playingMovieInfo();
        bool bRet = true;//cppcheck 误报
        switch (actionKind) {
        case ActionFactory::Screenshot:
        case ActionFactory::ToggleMiniMode:
        case ActionFactory::MatchOnlineSubtitle:
        case ActionFactory::BurstScreenshot:
            qDebug() << "isShortcut, return false";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            break;

        case ActionFactory::MovieInfo:
            qDebug() << "isShortcut, MovieInfo";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            if (bRet) {
                bRet = bRet && m_pEngine->playlist().count();
                if (bRet) {
                    auto pif = m_pEngine->playlist().currentInfo();
                    bRet = bRet && pif.loaded;
                }
            }
            break;

        case ActionFactory::HideSubtitle:
        case ActionFactory::SelectSubtitle:
            qDebug() << "isShortcut, SelectSubtitle";
            bRet = pmf.subs.size() > 0;
            break;
        default:
            break;
        }
        if (!bRet) {
            qDebug() << "isShortcut, return false";
            return bRet;
        }
    }

    qDebug() << "isShortcut, return true";
    return true;
}

void MainWindow::requestAction(ActionFactory::ActionKind actionKind, bool bFromUI,
                               QList<QVariant> args, bool bIsShortcut)
{
    qInfo() << "Requesting action:" << actionKind << "fromUI:" << bFromUI << (bIsShortcut ? "shortcut" : "");

    if (!m_pToolbox->getbAnimationFinash() || m_bStartAnimation) {
        qDebug() << "Animation in progress, action ignored";
        return;
    }

    if (!isActionAllowed(actionKind, bFromUI, bIsShortcut)) {
        qInfo() << "Action" << actionKind << "not allowed in current state";
        return;
    }

    switch (actionKind) {
    case ActionFactory::ActionKind::Exit:
        qInfo() << "Application exit requested";
        qApp->quit();
        break;

    case ActionFactory::ActionKind::OpenCdrom: {
        QString sDev = dmr::CommandLineManager::get().dvdDevice();
        if (sDev.isEmpty()) {
            sDev = probeCdromDevice();
        }
        if (sDev.isEmpty()) {
            qWarning() << "No DVD device found";
            m_pCommHintWid->updateWithMessage(tr("Cannot play the disc"));
            break;
        }

        if (addCdromPath() == false) {
            qInfo() << "Playing DVD from device:" << sDev;
            play({QString("dvd:///%1").arg(sDev)});
        }
        break;
    }

    case ActionFactory::ActionKind::OpenUrl: {
        UrlDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            QUrl url = dlg.url();
            if (url.isValid()) {
                qInfo() << "Playing URL:" << url.toString();
                play({url.toString()});
            } else {
                qWarning() << "Invalid URL provided";
                m_pCommHintWid->updateWithMessage(tr("Parse failed"));
            }
        }
        break;
    }

    case ActionFactory::ActionKind::OpenDirectory: {
#ifndef USE_TEST
        QString name = DFileDialog::getExistingDirectory(this, tr("Open folder"),
                                                         lastOpenedPath(),
                                                         DFileDialog::DontResolveSymlinks);
#else
        QString name("/data/source/deepin-movie-reborn/movie");
#endif

        QFileInfo fi(name);
        if (fi.isDir() && fi.exists()) {
            Settings::get().setGeneralOption("last_open_path", fi.path());

            play({name});
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFileList: {
        if (QDateTime::currentMSecsSinceEpoch() - m_pToolbox->getMouseTime() < 500) {
            qDebug() << "mouse time < 500, return";
            return;
        }
        if (m_pEngine->getplaylist()->items().isEmpty() && m_pEngine->getplaylist()->getThumanbilRunning()) {
            qDebug() << "playlist is empty and thumanbil is running, return";
            return;
        }
        //允许影院打开音乐文件进行播放
#ifndef USE_TEST
        qDebug() << "USE_TEST";
        DFileDialog fileDialog;
        QStringList filenames;

        QString strVideoTypes = m_pEngine->video_filetypes.join(" ");
        QString strAudioTypes = m_pEngine->audio_filetypes.join(" ");
        if(!CompositingManager::isMpvExists()) {
            strVideoTypes = QString("*.ogg *.dv *.avi *.webm");
            strAudioTypes = QString("*.wv *.flac *.mp3");
        }

        fileDialog.setParent(this);
        fileDialog.setNameFilters({tr("All (*)"), QString("Video (%1)").arg(strVideoTypes),
                                   QString("Audio (%1)").arg(strAudioTypes)});
        fileDialog.selectNameFilter(QString("Video (%1)").arg(strVideoTypes));
        fileDialog.setDirectory(lastOpenedPath());
        fileDialog.setFileMode(QFileDialog::ExistingFiles);

        if (fileDialog.exec() == QDialog::Accepted) {
            qDebug() << "fileDialog.exec() == QDialog::Accepted";
            filenames = fileDialog.selectedFiles();
        } else {
            qDebug() << "fileDialog.exec() != QDialog::Accepted, break";
            break;
        }
#else
        QStringList filenames;
        filenames << QString("/data/source/deepin-movie-reborn/movie/demo.mp4")\
                  << QString("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3");
#endif
        if (filenames.size()) {
            qDebug() << "filenames.size() > 0";
            QFileInfo fileInfo(filenames[0]);
            if (fileInfo.exists()) {
                qDebug() << "fileInfo.exists()";
                Settings::get().setGeneralOption("last_open_path", fileInfo.path());
            }
            play(filenames);
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFile: {
        qDebug() << "ActionFactory::ActionKind::OpenFile";
        DFileDialog fileDialog(this);
        QStringList filename;
        QString strVideoTypes = m_pEngine->video_filetypes.join(" ");
        QString strAudioTypes = m_pEngine->audio_filetypes.join(" ");
        if(!CompositingManager::isMpvExists()) {
            qDebug() << "CompositingManager::isMpvExists()";
            strVideoTypes = QString("ogg dv avi webm");
            strAudioTypes = QString("wv flac mp3");
        }

        fileDialog.setParent(this);
        fileDialog.setNameFilters({tr("All (*)"), QString("Video (%1)").arg(strVideoTypes),
                                   QString("Audio (%1)").arg(strAudioTypes)});
        fileDialog.selectNameFilter(QString("Video (%1)").arg(strVideoTypes));
        fileDialog.setDirectory(lastOpenedPath());
        fileDialog.setFileMode(QFileDialog::ExistingFiles);

        if (fileDialog.exec() == QDialog::Accepted) {
            qDebug() << "fileDialog.exec() == QDialog::Accepted";
            filename = fileDialog.selectedFiles();
        } else {
            qDebug() << "fileDialog.exec() != QDialog::Accepted, break";
            break;
        }
        QFileInfo fileInfo(filename[0]);
        if (fileInfo.exists()) {
            qDebug() << "fileInfo.exists()";
            Settings::get().setGeneralOption("last_open_path", fileInfo.path());

            play({filename[0]});
        }
        break;
    }

    case ActionFactory::ActionKind::StartPlay: {
        qDebug() << "ActionFactory::ActionKind::StartPlay";
        if (m_pEngine->playlist().count() == 0) {
            qDebug() << "m_pEngine->playlist().count() == 0";
            requestAction(ActionFactory::ActionKind::OpenFileList);
        } else {
            qDebug() << "m_pEngine->playlist().count() != 0";
            if (m_pEngine->state() == PlayerEngine::CoreState::Idle  && m_bIsFree) {
                qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Idle  && m_bIsFree";
                //先显示分辨率，再显示静音
                QSize sz = geometry().size();
                auto msg = QString("%1x%2").arg(sz.width()).arg(sz.height());
                QTimer::singleShot(500, [ = ]() {
                    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
                        m_pCommHintWid->updateWithMessage(msg);
                    }
                });
                QVariant panscan = m_pEngine->getBackendProperty("panscan");
                if ((panscan.isNull() || !CompositingManager::isMpvExists()) && Settings::get().isSet(Settings::ResumeFromLast)) {
                    qDebug() << "Settings::get().isSet(Settings::ResumeFromLast)";
                    int restore_pos = Settings::get().internalOption("playlist_pos").toInt();
                    //Playback when the playlist is not loaded, this will result in the 
                    //last exit item without playing, because the playlist has not been 
                    //loaded into that file, so adding a thread waiting here.  
                    //TODO(xxxxp):It will cause direct opening of the cartoon? May need to optimize Model View
                    while (m_pEngine->getplaylist()->getThumanbilRunning()) {
                        QCoreApplication::processEvents();
                    }
                    qInfo() << "playlist_pos: " << restore_pos << " current: " << m_pEngine->playlist().current();
                    if(m_pEngine->playlist().current() == -1) { //第一次直接启动影院(不是双击视频启动的影院)，点击播放按钮时启动上次退出影院时播放的视频
                        qDebug() << "m_pEngine->playlist().current() == -1";
                        restore_pos = qMax(qMin(restore_pos, m_pEngine->playlist().count() - 1), 0);
                        requestAction(ActionFactory::ActionKind::GotoPlaylistSelected, false, {restore_pos});
                    }
                } else {
                    qDebug() << "m_pEngine->playlist().current() != -1  >> play";
                    m_pEngine->play();
                }
            }
        }
        break;
    }

    case ActionFactory::ActionKind::EmptyPlaylist: {
        qDebug() << "ActionFactory::ActionKind::EmptyPlaylist";
        //play list context menu empty playlist
        m_pEngine->clearPlaylist();
        break;
    }

    case ActionFactory::ActionKind::TogglePlaylist: {
        qDebug() << "ActionFactory::ActionKind::TogglePlaylist";
        if (m_bStartMini || m_bMiniMode) {
            qDebug() << "m_bStartMini || m_bMiniMode, return";
            return;
        }
        //快捷键操作不置回焦点
        if (bIsShortcut) {
            qDebug() << "bIsShortcut";
            m_pToolbox->clearPlayListFocus();
        }
        /* The focus of the clear list button when the playlist is raised is also handled here.
         * Cancel the focus of the shortcut key when it is raised to avoid this problem
         */
        m_bStartAnimation = true;
        QTimer::singleShot(150, [ = ]() {    //延时是为了解决在窗口变化同时操作时，因窗口size未确定导致显示异常
            m_bStartAnimation = false;
            qDebug() << "m_bStartAnimation = false";
            if (bIsShortcut && toolbox()->getListBtnFocus()) {
                qDebug() << "bIsShortcut && toolbox()->getListBtnFocus(), setFocus";
                setFocus();
            }
            if (m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Closed && !m_pToolbox->isVisible()) {
                qDebug() << "m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Closed && !m_pToolbox->isVisible(), m_pToolbox->show()";
                m_pToolbox->show();
            }
            m_pPlaylist->togglePopup(bIsShortcut);
            if (!bFromUI) {
                qDebug() << "!bFromUI, reflectActionToUI";
                reflectActionToUI(actionKind);
            }
            this->resumeToolsWindow();
        });

        break;
    }

    case ActionFactory::ActionKind::ToggleMiniMode: {
        qDebug() << "ActionFactory::ActionKind::ToggleMiniMode";
        if (m_bMouseMoved) { // can't toggle minimode,when window is moving
            qDebug() << "m_bMouseMoved, break";
            break;
        }
        bool boardVendorFlag = false;
        int miniModeSpecialHandling = -1;
#ifdef DTKCORE_CLASS_DConfigFile
        //需要查询是否支持特殊特殊机型打开迷你模式，例如hw机型
        DConfig *dconfig = DConfig::create("org.deepin.movie","org.deepin.movie.minimode");
        if(dconfig && dconfig->isValid() && dconfig->keyList().contains("miniModeSpecialHandling")){
            miniModeSpecialHandling = dconfig->value("miniModeSpecialHandling").toInt();
        }
#endif
        qInfo() << "miniModeSpecialHandling value is:" << miniModeSpecialHandling;
        if(miniModeSpecialHandling != -1){
            qDebug() << "miniModeSpecialHandling != -1";
            boardVendorFlag = miniModeSpecialHandling? true:false;
        }else{
            qDebug() << "miniModeSpecialHandling == -1";
            QString result(cpuHardwareByDBus());
            boardVendorFlag = result.contains("PGUW", Qt::CaseInsensitive)
                          || result.contains("PANGU M900", Qt::CaseInsensitive);
    //                    || result.contains("KLVU", Qt::CaseInsensitive)
    //                    || result.contains("PGUV", Qt::CaseInsensitive)
    //                    || result.contains("KLVV", Qt::CaseInsensitive)
    //                    || result.contains("L540", Qt::CaseInsensitive);

            QProcess process;
            process.start("bash", QStringList() << "-c" << "dmidecode | grep -i \"String 4\"");
            process.waitForStarted();
            process.waitForFinished();
            result = process.readAll();
            boardVendorFlag = boardVendorFlag || result.contains("PWC30", Qt::CaseInsensitive);    //w525
            process.close();
        }
        qInfo() << "Whether special mini mode is supported? " << boardVendorFlag;

        int nDelayTime = 0;
        if (m_pPlaylist->state() == PlaylistWidget::Opened) {
            qDebug() << "m_pPlaylist->state() == PlaylistWidget::Opened";
            requestAction(ActionFactory::TogglePlaylist);
            nDelayTime = 500;
        }

        m_bStartMini = true;

        QTimer::singleShot(nDelayTime, this, [ = ] {
            qDebug() << "nDelayTime";
            if (m_pFullScreenTimeLable && !isFullScreen())
            {
                m_pFullScreenTimeLable->close();
            }
            if (!bFromUI)
            {
                qDebug() << "!bFromUI, reflectActionToUI";
                reflectActionToUI(actionKind);
            }
            if (boardVendorFlag)
            {
                qDebug() << "boardVendorFlag, m_pEngine->makeCurrent()";
                m_pEngine->makeCurrent();
            }
            toggleUIMode();
        });
        //Prevent abnormal focus position due to window state changes
        setFocus();
        break;
    }

    case ActionFactory::ActionKind::MovieInfo: {
        qDebug() << "ActionFactory::ActionKind::MovieInfo";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
            //fix 107355
            //Add a mouse display to prevent the target is hidden
            qApp->setOverrideCursor(Qt::ArrowCursor);
            MovieInfoDialog mid(m_pEngine->playlist().currentInfo(), this);
            mid.exec();
        }
        break;
    }

    case ActionFactory::ActionKind::WindowAbove: {
        qDebug() << "ActionFactory::ActionKind::WindowAbove";
        m_bWindowAbove = !m_bWindowAbove;
        if (!utils::check_wayland_env()) {
            qDebug() << "!utils::check_wayland_env()";
            my_setStayOnTop(this, m_bWindowAbove);
        } else {
            //wayland 置顶实现
            QFunctionPointer setWindowProperty = qApp->platformFunction("_d_setWindowProperty");
            qDebug() << "setWindowProperty";
            if (m_bWindowAbove) { //置顶
                qDebug() << "m_bWindowAbove";
                reinterpret_cast<void(*)(QWindow *, const char *, const QVariant &)>(setWindowProperty)(windowHandle(), "_d_dwayland_staysontop", true);

            } else {//取消置顶
                qDebug() << "!m_bWindowAbove";
                reinterpret_cast<void(*)(QWindow *, const char *, const QVariant &)>(setWindowProperty)(windowHandle(), "_d_dwayland_staysontop", false);
            }
        }
        if (!bFromUI) {
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::QuitFullscreen: {
        qDebug() << "ActionFactory::ActionKind::QuitFullscreen";
        if (!m_pToolbox->getVolSliderIsHided()) {
            qDebug() << "!m_pToolbox->getVolSliderIsHided()";
            m_pToolbox->setVolSliderHide();       // esc降下音量条
            break;
        }

        if (m_bMiniMode) {
            qDebug() << "m_bMiniMode";
            if (!bFromUI) {
                reflectActionToUI(ActionFactory::ToggleMiniMode);
            }
            toggleUIMode();
        } else if (isFullScreen()) {
            qDebug() << "isFullScreen()";
            requestAction(ActionFactory::ToggleFullscreen);
            if (m_pFullScreenTimeLable && !isFullScreen()) {
                m_pFullScreenTimeLable->close();
            }
        } else {
            //当焦点在播放列表上按下Esc键，播放列表收起，焦点回到列表按钮上
            if (m_pPlaylist->state() == PlaylistWidget::Opened) {
                qDebug() << "m_pPlaylist->state() == PlaylistWidget::Opened";
                m_pToolbox->playlistClosedByEsc();
            }
        }
        break;
    }

    case ActionFactory::ActionKind::ToggleFullscreen: {
        qDebug() << "ActionFactory::ActionKind::ToggleFullscreen";
        if (QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600) {
            qDebug() << "QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600, return";
            return;
        } else {
            m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
        }

        //音量条控件打开时全屏位置异常，全屏时关掉音量条
        m_pAnimationlable->hide();
        m_pToolbox->closeAnyPopup();

        if (isFullScreen()) {
            qDebug() << "isFullScreen()";
            setWindowState(windowState() & ~Qt::WindowFullScreen);
            if (m_bMaximized) {
                qDebug() << "m_bMaximized";
                showMaximized();
            } else {
                if (m_lastRectInNormalMode.isValid() && !m_bMiniMode && !isMaximized()) {
                    qDebug() << "m_lastRectInNormalMode.isValid() && !m_bMiniMode && !isMaximized()";
                    setGeometry(m_lastRectInNormalMode);
                    move(m_lastRectInNormalMode.x(), m_lastRectInNormalMode.y());
                    resize(m_lastRectInNormalMode.width(), m_lastRectInNormalMode.height());
                    if (utils::check_wayland_env())
                        m_pTitlebar->setFixedWidth(m_lastRectInNormalMode.width());             //bug 39991
                }
            }
            if (m_pFullScreenTimeLable && !isFullScreen()) {
                qDebug() << "m_pFullScreenTimeLable && !isFullScreen(), close";
                m_pFullScreenTimeLable->close();
            }
        } else {
            qDebug() << "!isFullScreen()";
            if (utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                m_pToolbox->setVolSliderHide();
                m_pToolbox->setButtonTooltipHide();
            }
            //可能存在更好的方法（全屏后更新toolbox状态），后期修改
            if (!m_pToolbox->getbAnimationFinash())
            {
                qDebug() << "!m_pToolbox->getbAnimationFinash(), return";
                return;
            }
            m_bMaximized = isMaximized();  // 记录全屏前是否是最大化窗口
            mipsShowFullScreen();
            if (isFullScreen()) {
                qDebug() << "isFullScreen()";
                //The X86 platform draws on GiWidget, and the MIPS platform does not need to draw
                if (CompositingManager::get().platform() == Platform::Arm64 || CompositingManager::get().platform() == Platform::Alpha) {
                    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
                        qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
                        int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                        QRect deskRect = QApplication::desktop()->availableGeometry();
#else
                        QRect deskRect = qApp->primaryScreen()->availableGeometry();
#endif
                        qDebug() << "pixelsWidth";
                        pixelsWidth = qMax(117, pixelsWidth);
                        m_pFullScreenTimeLable->setGeometry(deskRect.width() - pixelsWidth - 60, 40, pixelsWidth + 60, 36);
                        qDebug() << "m_pFullScreenTimeLable->setGeometry";
                        m_pFullScreenTimeLable->show();
                    }
                }
            }
        }
        if (!bFromUI) {
            qDebug() << "!bFromUI, reflectActionToUI";
            reflectActionToUI(actionKind);
        }

        activateWindow();
        //Set focus back to main window after full screen, Prevent focus from going to the toolbar
        setFocus();

        // fixed bug 103560
        // the window state change signal is not sent under wayland, so call directly here
        // if the problem is fixed in the future, please remove this code
        if (utils::check_wayland_env()) {
            m_pToolbox->updateFullState();
        }

        break;
    }

    case ActionFactory::ActionKind::PlaylistRemoveItem: {
        qDebug() << "ActionFactory::ActionKind::PlaylistRemoveItem";
        m_pPlaylist->removeClickedItem(bIsShortcut);
        break;
    }

    case ActionFactory::ActionKind::PlaylistOpenItemInFM: {
        qDebug() << "ActionFactory::ActionKind::PlaylistOpenItemInFM";
        m_pPlaylist->openItemInFM();
        break;
    }

    case ActionFactory::ActionKind::PlaylistItemInfo: {
        qDebug() << "ActionFactory::ActionKind::PlaylistItemInfo";
        m_pPlaylist->showItemInfo();
        break;
    }

    case ActionFactory::ActionKind::ClockwiseFrame: {
        qDebug() << "ActionFactory::ActionKind::ClockwiseFrame";
        auto old = m_pEngine->videoRotation();
        m_pEngine->setVideoRotation((old + 90) % 360);
        break;
    }
    case ActionFactory::ActionKind::CounterclockwiseFrame: {
        qDebug() << "ActionFactory::ActionKind::CounterclockwiseFrame";
        auto old = m_pEngine->videoRotation();
        m_pEngine->setVideoRotation(((old - 90) + 360) % 360);
        break;
    }

    case ActionFactory::ActionKind::OrderPlay: {
        qDebug() << "ActionFactory::ActionKind::OrderPlay";
        Settings::get().setInternalOption("playmode", 0);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::OrderPlay);
        break;
    }
    case ActionFactory::ActionKind::ShufflePlay: {
        qDebug() << "ActionFactory::ActionKind::ShufflePlay";
        Settings::get().setInternalOption("playmode", 1);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::ShufflePlay);
        break;
    }
    case ActionFactory::ActionKind::SinglePlay: {
        qDebug() << "ActionFactory::ActionKind::SinglePlay";
        Settings::get().setInternalOption("playmode", 2);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::SinglePlay);
        break;
    }
    case ActionFactory::ActionKind::SingleLoop: {
        qDebug() << "ActionFactory::ActionKind::SingleLoop";
        Settings::get().setInternalOption("playmode", 3);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::SingleLoop);
        break;
    }
    case ActionFactory::ActionKind::ListLoop: {
        qDebug() << "ActionFactory::ActionKind::ListLoop";
        Settings::get().setInternalOption("playmode", 4);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::ListLoop);
        break;
    }

    case ActionFactory::ActionKind::ZeroPointFiveTimes: {
        qDebug() << "ActionFactory::ActionKind::ZeroPointFiveTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::ZeroPointFiveTimes, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 0.5;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OneTimes: {
        qDebug() << "ActionFactory::ActionKind::OneTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::OneTimes, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointTwoTimes: {
        qDebug() << "ActionFactory::ActionKind::OnePointTwoTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::OnePointTwoTimes, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.2;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointFiveTimes: {
        qDebug() << "ActionFactory::ActionKind::OnePointFiveTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::OnePointFiveTimes, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.5;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::Double: {
        qDebug() << "ActionFactory::ActionKind::Double";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::Double, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 2.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::Stereo: {
        qDebug() << "ActionFactory::ActionKind::Stereo";
        m_pEngine->changeSoundMode(Backend::SoundMode::Stereo);
        m_pCommHintWid->updateWithMessage(tr("Stereo"));
        break;
    }
    case ActionFactory::ActionKind::LeftChannel: {
        qDebug() << "ActionFactory::ActionKind::LeftChannel";
        m_pEngine->changeSoundMode(Backend::SoundMode::Left);
        m_pCommHintWid->updateWithMessage(tr("Left channel"));
        break;
    }
    case ActionFactory::ActionKind::RightChannel: {
        qDebug() << "ActionFactory::ActionKind::RightChannel";
        m_pEngine->changeSoundMode(Backend::SoundMode::Right);
        m_pCommHintWid->updateWithMessage(tr("Right channel"));
        break;
    }

    case ActionFactory::ActionKind::DefaultFrame: {
        qDebug() << "ActionFactory::ActionKind::DefaultFrame";
        m_pEngine->setVideoAspect(-1.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio4x3Frame: {
        qDebug() << "ActionFactory::ActionKind::Ratio4x3Frame";
        m_pEngine->setVideoAspect(4.0 / 3.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x9Frame: {
        qDebug() << "ActionFactory::ActionKind::Ratio16x9Frame";
        m_pEngine->setVideoAspect(16.0 / 9.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x10Frame: {
        qDebug() << "ActionFactory::ActionKind::Ratio16x10Frame";
        m_pEngine->setVideoAspect(16.0 / 10.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio185x1Frame: {
        qDebug() << "ActionFactory::ActionKind::Ratio185x1Frame";
        m_pEngine->setVideoAspect(1.85);
        break;
    }
    case ActionFactory::ActionKind::Ratio235x1Frame: {
        qDebug() << "ActionFactory::ActionKind::Ratio235x1Frame";
        m_pEngine->setVideoAspect(2.35);
        break;
    }

    case ActionFactory::ActionKind::ToggleMute: {
        qDebug() << "ActionFactory::ActionKind::ToggleMute";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qDebug() << "ActionFactory::ActionKind::ToggleMute, slotUnsupported";
            slotUnsupported();
        } else {
            qDebug() << "ActionFactory::ActionKind::ToggleMute, m_pToolbox->changeMuteState()";
            m_pToolbox->changeMuteState();
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeUp: {
        qDebug() << "ActionFactory::ActionKind::VolumeUp";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qDebug() << "ActionFactory::ActionKind::VolumeUp, slotUnsupported";
            slotUnsupported();
        } else {
            //使用鼠标滚轮调节音量时会执行此步骤
            if (m_iAngleDelta != 0) m_pToolbox->calculationStep(m_iAngleDelta);
            qDebug() << "ActionFactory::ActionKind::VolumeUp, m_pToolbox->volumeUp()";
            m_pToolbox->volumeUp();
            m_iAngleDelta = 0;
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeDown: {
        qDebug() << "ActionFactory::ActionKind::VolumeDown";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qDebug() << "ActionFactory::ActionKind::VolumeDown, slotUnsupported";
            slotUnsupported();
        } else {
            //使用鼠标滚轮调节音量时会执行此步骤
            if (m_iAngleDelta != 0) m_pToolbox->calculationStep(m_iAngleDelta);
            qDebug() << "ActionFactory::ActionKind::VolumeDown, m_pToolbox->volumeDown()";
            m_pToolbox->volumeDown();
            m_iAngleDelta = 0;
        }
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistSelected: {
        qDebug() << "ActionFactory::ActionKind::GotoPlaylistSelected";
        m_pEngine->playSelected(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistNext: {
        qDebug() << "ActionFactory::ActionKind::GotoPlaylistNext";
        //防止焦点在上/下一曲按钮上切换时焦点跳到下一个按钮上
        //下同
        setFocus();
        if (m_bIsFree == false)
            return ;

        m_bIsFree = false;
        if (isFullScreen() || isMaximized()) {
            qDebug() << "ActionFactory::ActionKind::GotoPlaylistNext, isFullScreen() || isMaximized()";
            m_bMovieSwitchedInFsOrMaxed = true;
        }
        m_pEngine->next();

        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistPrev: {
        qDebug() << "ActionFactory::ActionKind::GotoPlaylistPrev";
        setFocus();
        if (m_bIsFree == false) {
            qDebug() << "ActionFactory::ActionKind::GotoPlaylistPrev, m_bIsFree == false, return";
            return ;
        }

        m_bIsFree = false;
        if (isFullScreen() || isMaximized()) {
            qDebug() << "ActionFactory::ActionKind::GotoPlaylistPrev, isFullScreen() || isMaximized()";
            m_bMovieSwitchedInFsOrMaxed = true;
        }
        m_pEngine->prev();
        break;
    }

    case ActionFactory::ActionKind::SelectTrack: {
        qDebug() << "ActionFactory::ActionKind::SelectTrack";
        Q_ASSERT(args.size() == 1);
        m_pEngine->selectTrack(args[0].toInt());
        m_pCommHintWid->updateWithMessage(tr("Track: %1").arg(args[0].toInt() + 1));
        if (!bFromUI) {
            qDebug() << "ActionFactory::ActionKind::SelectTrack, reflectActionToUI(actionKind)";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::MatchOnlineSubtitle: {
        qDebug() << "ActionFactory::ActionKind::MatchOnlineSubtitle";
        m_pEngine->loadOnlineSubtitle(m_pEngine->playlist().currentInfo().url);
        break;
    }

    case ActionFactory::ActionKind::SelectSubtitle: {
        qDebug() << "ActionFactory::ActionKind::SelectSubtitle";
        Q_ASSERT(args.size() == 1);
        m_pEngine->selectSubtitle(args[0].toInt());
        if (!bFromUI) {
            qDebug() << "ActionFactory::ActionKind::SelectSubtitle, reflectActionToUI(actionKind)";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        qDebug() << "ActionFactory::ActionKind::ChangeSubCodepage";
        Q_ASSERT(args.size() == 1);
        m_pEngine->setSubCodepage(args[0].toString());
        if (!bFromUI) {
            qDebug() << "ActionFactory::ActionKind::ChangeSubCodepage, reflectActionToUI(actionKind)";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::HideSubtitle: {
        qDebug() << "ActionFactory::ActionKind::HideSubtitle";
        m_pEngine->toggleSubtitle();
        break;
    }

    case ActionFactory::ActionKind::SubDelay: {
        qDebug() << "ActionFactory::ActionKind::SubDelay";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "ActionFactory::ActionKind::SubDelay, slotUnsupported";
            slotUnsupported();
            break;
        }
        if (m_pEngine->playingMovieInfo().subs.isEmpty()) {
            qDebug() << "ActionFactory::ActionKind::SubDelay, m_pEngine->playingMovieInfo().subs.isEmpty()";
            m_pCommHintWid->updateWithMessage(tr("Unable to adjust the subtitle"));
            break;
        }
        m_pEngine->setSubDelay(0.5);
        double dDelay = m_pEngine->subDelay();
        m_pCommHintWid->updateWithMessage(tr("Subtitle %1: %2s")
                                          .arg(dDelay > 0.0 ? tr("delayed") : tr("advanced")).arg(dDelay > 0.0 ? dDelay : -dDelay));
        break;
    }

    case ActionFactory::ActionKind::SubForward: {
        qDebug() << "ActionFactory::ActionKind::SubForward";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "ActionFactory::ActionKind::SubForward, slotUnsupported";
            slotUnsupported();
            break;
        }
        if (m_pEngine->playingMovieInfo().subs.isEmpty()) {
            qDebug() << "ActionFactory::ActionKind::SubForward, m_pEngine->playingMovieInfo().subs.isEmpty()";
            m_pCommHintWid->updateWithMessage(tr("Unable to adjust the subtitle"));
            break;
        }
        m_pEngine->setSubDelay(-0.5);
        double dDelay = m_pEngine->subDelay();
        m_pCommHintWid->updateWithMessage(tr("Subtitle %1: %2s")
                                          .arg(dDelay > 0.0 ? tr("delayed") : tr("advanced")).arg(dDelay > 0.0 ? dDelay : -dDelay));
        break;
    }

    case ActionFactory::ActionKind::AccelPlayback: {
        qDebug() << "ActionFactory::ActionKind::AccelPlayback";
        adjustPlaybackSpeed(ActionFactory::ActionKind::AccelPlayback);
        break;
    }

    case ActionFactory::ActionKind::DecelPlayback: {
        qDebug() << "ActionFactory::ActionKind::DecelPlayback";
        adjustPlaybackSpeed(ActionFactory::ActionKind::DecelPlayback);
        break;
    }

    case ActionFactory::ActionKind::ResetPlayback: {
        qDebug() << "ActionFactory::ActionKind::ResetPlayback";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "ActionFactory::ActionKind::ResetPlayback, m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::LoadSubtitle: {
        qDebug() << "ActionFactory::ActionKind::LoadSubtitle";
        QStringList filename;
#ifndef USE_TEST
        DFileDialog fileDialog(this);
        fileDialog.setNameFilter(tr("Subtitle (*.ass *.aqt *.jss *.gsub *.ssf *.srt *.sub *.ssa *.smi *.usf *.idx)","All (*)"));
        fileDialog.setDirectory(lastOpenedPath());

        if (fileDialog.exec() == QDialog::Accepted) {
            qDebug() << "ActionFactory::ActionKind::LoadSubtitle, fileDialog.exec() == QDialog::Accepted";
            filename = fileDialog.selectedFiles();
        } else {
            qDebug() << "ActionFactory::ActionKind::LoadSubtitle, fileDialog.exec() == QDialog::Rejected, break";
            break;
        }
#else
        filename = QStringList({"/data/source/deepin-movie-reborn/Hachiko.A.Dog's.Story.ass"});
#endif
        if (QFileInfo(filename[0]).exists()) {
            if (m_pEngine->state() == PlayerEngine::Idle) {
                qDebug() << "ActionFactory::ActionKind::LoadSubtitle, m_pEngine->state() == PlayerEngine::Idle";
                subtitleMatchVideo(filename[0]);
            }
            else {
                qDebug() << "ActionFactory::ActionKind::LoadSubtitle, m_pEngine->state() != PlayerEngine::Idle";
                auto success = m_pEngine->loadSubtitle(QFileInfo(filename[0]));
                m_pCommHintWid->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
            }
        } else {
            qDebug() << "ActionFactory::ActionKind::LoadSubtitle, QFileInfo(filename[0]).exists() == false";
            m_pCommHintWid->updateWithMessage(tr("Load failed"));
        }
        break;
    }

    case ActionFactory::ActionKind::TogglePause: {
        qDebug() << "ActionFactory::ActionKind::TogglePause";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            qDebug() << "Pausing mircast playback";
            m_pToolbox->getMircast()->slotPauseDlnaTp();
            break;
        }
        if (windowState() == Qt::WindowFullScreen && QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 500) {
            qDebug() << "Ignoring pause request - too soon after fullscreen";
            return;
        } else if(windowState() == Qt::WindowFullScreen) {
            m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
        }
        if (m_pEngine->state() == PlayerEngine::Idle && bIsShortcut) {
            if (m_pEngine->getplaylist()->getthreadstate()) {
                qInfo() << "Playlist load thread is running, ignoring pause request";
                break;
            }
            requestAction(ActionFactory::StartPlay);
        } else {
            if (m_pEngine->state() == PlayerEngine::Paused) {
                qDebug() << "Resuming playback";
                if (!m_bMiniMode) {
                    m_pAnimationlable->playAnimation();
                }
                QTimer::singleShot(160, [ = ]() {
                    m_pEngine->pauseResume();
                });
            } else {
                qDebug() << "Pausing playback";
                m_pEngine->pauseResume();
            }
        }
        break;
    }

    case ActionFactory::ActionKind::SeekBackward: {
        qDebug() << "ActionFactory::ActionKind::SeekBackward";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            m_pToolbox->getMircast()->seekMircast(-5);
            qDebug() << "ActionFactory::ActionKind::SeekBackward, m_pToolbox->getMircast()->seekMircast(-5)";
            break;
        }
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "ActionFactory::ActionKind::SeekBackward, slotUnsupported";
            slotUnsupported();
        } else {
            qDebug() << "ActionFactory::ActionKind::SeekBackward, m_pEngine->seekBackward(5)";
            m_pEngine->seekBackward(5);
        }
        break;
    }

    case ActionFactory::ActionKind::SeekForward: {
        qDebug() << "ActionFactory::ActionKind::SeekForward";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            m_pToolbox->getMircast()->seekMircast(5);
            qDebug() << "ActionFactory::ActionKind::SeekForward, m_pToolbox->getMircast()->seekMircast(5)";
            break;
        }
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "ActionFactory::ActionKind::SeekForward, slotUnsupported";
            slotUnsupported();
        } else {
            qDebug() << "ActionFactory::ActionKind::SeekForward, m_pEngine->seekForward(5)";
            m_pEngine->seekForward(5);
        }
        break;
    }

    case ActionFactory::ActionKind::SeekAbsolute: {
        qDebug() << "ActionFactory::ActionKind::SeekAbsolute";
        Q_ASSERT(args.size() == 1);
        m_pEngine->seekAbsolute(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::Settings: {
        qDebug() << "ActionFactory::ActionKind::Settings";
        handleSettings(initSettings());
        break;
    }

    case ActionFactory::ActionKind::Screenshot: {
        qDebug() << "ActionFactory::ActionKind::Screenshot";
        QImage img = m_pEngine->takeScreenshot();

        QString filePath = Settings::get().screenshotNameTemplate();
        bool bSuccess = false;
        if (img.isNull()) {
            qWarning() << "Failed to capture screenshot - null image";
        } else {
            bSuccess = img.save(filePath);
            qInfo() << "Screenshot saved to:" << filePath << "success:" << bSuccess;
        }

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
    m_pPopupWid->setIcon(icon);\
    DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);\
    QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);\
    QFontMetrics fm(font);\
    auto w = fm.boundingRect(text).width();\
    m_pPopupWid->setMessage(text);\
    m_pPopupWid->resize(w + 70, 52);\
    m_pPopupWid->move((width() - m_pPopupWid->width()) / 2, height() - 127);\
    m_pPopupWid->show();\
        } while (0)
        if (bSuccess) {
            const QIcon icon = QIcon(":/resources/icons/short_ok.svg");
            QString sText = QString(tr("The screenshot is saved"));
            popupAdapter(icon, sText);
        } else {
            const QIcon icon = QIcon(":/resources/icons/short_fail.svg");
            QString sText = QString(tr("Failed to save the screenshot"));
            popupAdapter(icon, sText);
        }

#undef POPUP_ADAPTER

#endif
        break;
    }

    case ActionFactory::ActionKind::GoToScreenshotSolder: {
        qDebug() << "ActionFactory::ActionKind::GoToScreenshotSolder";
        QString filePath = Settings::get().screenshotLocation();
        qInfo() << __func__ << filePath;
        QDBusInterface iface("org.freedesktop.FileManager1",
                             "/org/freedesktop/FileManager1",
                             "org.freedesktop.FileManager1",
                             QDBusConnection::sessionBus());
        if (iface.isValid()) {
            // Convert filepath to URI first.
            const QStringList uris = { filePath };
            qInfo() << "freedesktop.FileManager";
            // StartupId is empty here.
            QDBusPendingCall call = iface.asyncCall("Open", uris);
            Q_UNUSED(call);
        }
        break;
    }

    case ActionFactory::ActionKind::BurstScreenshot: {
        qDebug() << "ActionFactory::ActionKind::BurstScreenshot";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qDebug() << "ActionFactory::ActionKind::BurstScreenshot, slotUnsupported";
            slotUnsupported();
        } else {
            qDebug() << "ActionFactory::ActionKind::BurstScreenshot, startBurstShooting()";
            startBurstShooting();
        }
        break;
    }

    case ActionFactory::ActionKind::ViewShortcut: {
        qDebug() << "ActionFactory::ActionKind::ViewShortcut";
        QRect rect = window()->geometry();
        QPoint pos(rect.x() + rect.width() / 2, rect.y() + rect.height() / 2);
        QStringList shortcutString;
        QString param1 = "-j=" + ShortcutManager::get().toJson();
        param1.replace("Return", "Enter");
        param1.replace("PgDown", "PageDown");
        param1.replace("PgUp", "PageUp");
        QString param2 = "-p=" + QString::number(pos.x()) + "," + QString::number(pos.y());
        shortcutString << param1 << param2;

        if (!m_pShortcutViewProcess) {
            qDebug() << "ActionFactory::ActionKind::ViewShortcut, m_pShortcutViewProcess == nullptr";
            m_pShortcutViewProcess = new QProcess();
        }
        m_pShortcutViewProcess->startDetached("deepin-shortcut-viewer", shortcutString);

        connect(m_pShortcutViewProcess, SIGNAL(finished(int)),
                m_pShortcutViewProcess, SLOT(deleteLater()));
        break;
    }

    case ActionFactory::ActionKind::NextFrame: {
        qDebug() << "ActionFactory::ActionKind::NextFrame";
        m_pEngine->nextFrame();

        break;
    }

    case ActionFactory::ActionKind::PreviousFrame: {
        qDebug() << "ActionFactory::ActionKind::PreviousFrame";
        m_pEngine->previousFrame();

        break;
    }

    default:
        qDebug() << "ActionFactory::ActionKind::Default";
        break;
    }
    qDebug() << "";
}

void MainWindow::onBurstScreenshot(const QImage &frame, qint64 timestamp)
{
    qInfo() << "Starting burst screenshot capture, current count:" << m_listBurstShoots.size();
#define POPUP_ADAPTER(icon, text)  do { \
        m_pPopupWid->setIcon(icon);\
        DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);\
        QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);\
        QFontMetrics fm(font);\
        auto w = fm.boundingRect(text).width();\
        m_pPopupWid->setMessage(text);\
        m_pPopupWid->resize(w + 70, 52);\
        m_pPopupWid->move((width() - m_pPopupWid->width()) / 2, height() - 127);\
        m_pPopupWid->show();\
    } while (0)

    if (!frame.isNull()) {
        qDebug() << "!frame.isNull()";
        QString sMsg = QString(tr("Taking the screenshots, please wait..."));
        m_pCommHintWid->updateWithMessage(sMsg);

        m_listBurstShoots.append(qMakePair(frame, timestamp));
    }

    if (m_listBurstShoots.size() >= 15 || frame.isNull()) {
        qInfo() << "Burst screenshot capture completed, total frames:" << m_listBurstShoots.size();
        disconnect(m_pEngine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
        m_pEngine->stopBurstScreenshot();
        m_bInBurstShootMode = false;
        m_pToolbox->setEnabled(true);
        //fix bug:129205
//        m_pTitlebar->titlebar()->setEnabled(true);
        if (m_pEventListener) m_pEventListener->setEnabled(!m_bMiniMode);

        if (frame.isNull()) {
            qDebug() << "frame.isNull()";
            m_listBurstShoots.clear();
            if (!m_bPausedBeforeBurst) {
                qDebug() << "!m_bPausedBeforeBurst";
                m_pEngine->pauseResume();
            }
            qDebug() << "m_listBurstShoots.clear() return";
            return;
        }

        int nRet = -1;
        BurstScreenshotsDialog burstScreenshotsDialog(m_pEngine->playlist().currentInfo());
        burstScreenshotsDialog.updateWithFrames(m_listBurstShoots);
#ifdef USE_TEST
        burstScreenshotsDialog.show();
#else
        nRet = burstScreenshotsDialog.exec();
#endif
        qInfo() << "BurstScreenshot done";

        m_listBurstShoots.clear();
        if (!m_bPausedBeforeBurst) {
            qDebug() << "m_bPausedBeforeBurst";
            m_pEngine->pauseResume();
        }

        if (nRet == QDialog::Accepted) {
            qDebug() << "nRet == QDialog::Accepted";
            QString sPosterPath = burstScreenshotsDialog.savedPosterPath();
            if (QFileInfo::exists(sPosterPath)) {
                qDebug() << "QFileInfo::exists(sPosterPath)";
                const QIcon icon = QIcon(":/resources/icons/short_ok.svg");
                QString sText = QString(tr("The screenshot is saved"));
                popupAdapter(icon, sText);
            } else {
                qDebug() << "QFileInfo::exists(sPosterPath) == false";
                const QIcon icon = QIcon(":/resources/icons/short_fail.svg");
                QString sText = QString(tr("Failed to save the screenshot"));
                popupAdapter(icon, sText);
            }
        }
    }
}

void MainWindow::startBurstShooting()
{
    qDebug() << "Starting burst shooting mode";
    //Repair 40S video corresponding to the corresponding connected screenshot
    if (m_pEngine->duration() <= 40) return;
    m_bInBurstShootMode = true;
    m_pToolbox->setEnabled(false);
//    m_pTitlebar->titlebar()->setEnabled(false);
    if (m_pEventListener) m_pEventListener->setEnabled(false);

    m_bPausedBeforeBurst = m_pEngine->paused();

    connect(m_pEngine, &PlayerEngine::notifyScreenshot, this, &MainWindow::onBurstScreenshot);
    m_pEngine->burstScreenshot();
}

void MainWindow::handleSettings(DSettingsDialog *dsd)
{
    qDebug() << "Opening settings dialog";
    int decodeType = Settings::get().settings()->getOption(QString("base.decode.select")).toInt();
    int decodeMode = Settings::get().settings()->getOption(QString("base.decode.Decodemode")).toInt();
    int voMode = Settings::get().settings()->getOption(QString("base.decode.Videoout")).toInt();
    int effectMode = Settings::get().settings()->getOption("base.decode.Effect").toInt();

#ifndef USE_TEST
    dsd->exec();
#else
    dsd->setObjectName("DSettingsDialog");
    dsd ->show();
#endif

    static QEventLoop loop;
    QFileSystemWatcher fileWatcher;
    fileWatcher.addPath(Settings::get().configPath());
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, [=](){
        loop.quit();
    });
    if (Settings::get().settings()->getOption("base.decode.select").toInt() != decodeType &&
            (Settings::get().settings()->getOption("base.decode.select").toInt() == 3 || decodeType == 3)) {
        qWarning() << "Custom decoding method change detected, restart required";
        DDialog msgBox;
        msgBox.setIcon(QIcon(":/resources/icons/warning.svg"));
        msgBox.setMessage(QObject::tr("The custom decoding method needs to be restarted before it can take effect,\nand whether to restart it?"));
        msgBox.addButton(tr("Cancel"), DDialog::ButtonType::ButtonNormal);
        msgBox.addButton(tr("Restart"), true, DDialog::ButtonType::ButtonWarning);
        msgBox.setOnButtonClickedClose(true);
        if (msgBox.exec() == 1) {
            qDebug() << "msgBox exec =1";
            if (Settings::get().settings()->getOption("base.decode.select").toInt() != 3) {
                Settings::get().settings()->setOption("base.decode.Decodemode", 0);
                Settings::get().settings()->setOption("base.decode.Videoout", 0);
                Settings::get().settings()->setOption("base.decode.Effect", 0);
            }
            Settings::get().settings()->setOption(QString("set.start.crash"), 2);
            Settings::get().settings()->sync();
            loop.exec();
            qDebug() << "qApp->exit(2)";
            qApp->exit(2);
        } else {
            qDebug() << "msgBox exec !=1";
            if (decodeType != 3) {
                qDebug() << "decodeType != 3";
                Settings::get().settings()->setOption("base.decode.select", decodeMode);
            }
            Settings::get().settings()->setOption("base.decode.Decodemode", decodeMode);
            Settings::get().settings()->setOption("base.decode.Videoout", voMode);
        }
    } else {
        if (decodeType == 3) {
            int newDecodeMode = Settings::get().settings()->getOption(QString("base.decode.Decodemode")).toInt();
            int newVoMode = Settings::get().settings()->getOption(QString("base.decode.Videoout")).toInt();
            int newEffectMode = Settings::get().settings()->getOption("base.decode.Effect").toInt();
            if (newEffectMode != effectMode || newVoMode != voMode || newDecodeMode != decodeMode) {
                Settings::get().crashCheck();
                DDialog msgBox;
                msgBox.setIcon(QIcon(":/resources/icons/warning.svg"));
                msgBox.setMessage(QObject::tr("The custom decoding method needs to be restarted before it can take effect,\nand whether to restart it?"));
                msgBox.addButton(tr("Cancel"), DDialog::ButtonType::ButtonNormal);
                msgBox.addButton(tr("Restart"), true, DDialog::ButtonType::ButtonWarning);
                msgBox.setOnButtonClickedClose(true);
                if (msgBox.exec() == 1) {
                    Settings::get().settings()->setOption(QString("set.start.crash"), 2);
                    Settings::get().settings()->sync();
                    loop.exec();
                    qDebug() << "msgBox exec !=1, decodeType == 3, qApp->exit(2)";
                    qApp->exit(2);
                } else {
                    qDebug() << "msgBox exec !=1, decodeType == 3";
                    if (decodeType != 3) {
                        Settings::get().settings()->setOption("base.decode.select", decodeType);
                    }
                    Settings::get().settings()->setOption("base.decode.Decodemode", decodeMode);
                    Settings::get().settings()->setOption("base.decode.Videoout", voMode);
                }
            }
        }
    }

    Settings::get().settings()->sync();
}

DSettingsDialog *MainWindow::initSettings()
{
    qDebug() << "initSettings";
    if (m_pDSettingDilog) {
        qDebug() << "m_pDSettingDilog";
        return m_pDSettingDilog;
    }
    qDebug() << "m_pDSettingDilog == nullptr";
    m_pDSettingDilog = new DSettingsDialog(this);
    m_pDSettingDilog->widgetFactory()->registerWidget("selectableEdit", createSelectableLineEditOptionHandle);
    m_pDSettingDilog->widgetFactory()->registerWidget("effectCombobox", createEffectOptionHandle);
    m_pDSettingDilog->widgetFactory()->registerWidget("videoOutCombobox", createVoOptionHandle);
    m_pDSettingDilog->widgetFactory()->registerWidget("decoderCombobox", createDecodeOptionHandle);

    m_pDSettingDilog->setProperty("_d_QSSThemename", "dark");
    m_pDSettingDilog->setProperty("_d_QSSFilename", "DSettingsDialog");
    m_pDSettingDilog->updateSettings(Settings::get().settings());

    //hack:
    QSpinBox *pSpinBox = m_pDSettingDilog->findChild<QSpinBox *>("OptionDSpinBox");
    if (pSpinBox) {
        qDebug() << "pSpinBox";
        pSpinBox->setMinimum(8);
    }

    // hack: reset is set to default by QDialog, which makes lineedit's enter
    // press is responded by reset button
    QPushButton *pPushButton = m_pDSettingDilog->findChild<QPushButton *>("SettingsContentReset");
    qDebug() << "pPushButton";
    pPushButton->setDefault(false);
    pPushButton->setAutoDefault(false);

    int decodeType = Settings::get().settings()->getOption(QString("base.decode.select")).toInt();
    if (decodeType != 3) {
        qDebug() << "decodeType != 3";
        QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
        QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
        dynamic_cast<QWidget*>(effectFrame->parent())->hide();
        dynamic_cast<QWidget*>(videoFrame->parent())->hide();
        dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
    } else {
        qDebug() << "decodeType == 3";
        if (utils::check_wayland_env()) {
            qDebug() << "utils::check_wayland_env()";
            QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
            QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
            QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
            dynamic_cast<QWidget*>(effectFrame->parent())->hide();
            dynamic_cast<QWidget*>(videoFrame->parent())->hide();
            dynamic_cast<QWidget*>(decodeFrame->parent())->show();
        } else {
            int effectIndex = Settings::get().settings()->getOption(QString("base.decode.Effect")).toInt();
            qDebug() << "effectIndex" << effectIndex;
            if (effectIndex == 0) {
                qDebug() << "effectIndex == 0";
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                dynamic_cast<QWidget*>(videoFrame->parent())->hide();
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
            } else if (effectIndex == 1) {
                qDebug() << "effectIndex == 1";
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                dynamic_cast<QWidget*>(videoFrame->parent())->show();
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                dynamic_cast<QWidget*>(decodeFrame->parent())->show();
            } else {
                qDebug() << "effectIndex == 2";
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                dynamic_cast<QWidget*>(videoFrame->parent())->show();
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                if (Settings::get().settings()->getOption("base.decode.Videoout").toInt() == 0  &&
                        Settings::get().settings()->getOption("base.decode.Effect").toInt() == 2) {
                    dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                } else
                    dynamic_cast<QWidget*>(decodeFrame->parent())->show();
            }
        }
    }

    connect(&Settings::get(), &Settings::setDecodeModel, this, [=](QString key, QVariant value){
        qDebug() << "setDecodeModel";
        if (key == "base.decode.select") {
            int decodeType = Settings::get().settings()->getOption(QString("base.decode.select")).toInt();
            qDebug() << "decodeType" << decodeType;
            if (decodeType != 3) {
                QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                dynamic_cast<QWidget*>(effectFrame->parent())->hide();
                dynamic_cast<QWidget*>(videoFrame->parent())->hide();
                dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
            } else {
                qDebug() << "decodeType == 3";
                if (utils::check_wayland_env()) {
                    qDebug() << "utils::check_wayland_env()";
                    QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                    dynamic_cast<QWidget*>(decodeFrame->parent())->show();
                } else {
                    qDebug() << "!utils::check_wayland_env()";
                    QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
                    dynamic_cast<QWidget*>(effectFrame->parent())->show();
                    int effectIndex = Settings::get().settings()->getOption(QString("base.decode.Effect")).toInt();
                    qDebug() << "effectIndex" << effectIndex;
                    if (effectIndex == 0) {
                        QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                        dynamic_cast<QWidget*>(videoFrame->parent())->hide();
                        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                        dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                    } else {
                        qDebug() << "effectIndex == 1";
                        QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                        dynamic_cast<QWidget*>(videoFrame->parent())->show();
                        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                        if (Settings::get().settings()->getOption("base.decode.Videoout").toInt() == 0 &&
                                Settings::get().settings()->getOption("base.decode.Effect").toInt() == 2)
                            dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                        else
                            dynamic_cast<QWidget*>(decodeFrame->parent())->show();
                    }
                }
            }
        }
    }, Qt::DirectConnection);

    connect(&Settings::get(), &Settings::baseChanged, this, [=](QString key, QVariant value) {
        if (!utils::check_wayland_env()) {
            qDebug() << "!utils::check_wayland_env()";
            int visable = value.toInt();
            if (key == "base.decode.Effect") {
                qDebug() << "key == base.decode.Effect";
                if (visable == 0) {
                    qDebug() << "visable == 0";
                    QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                    dynamic_cast<QWidget*>(videoFrame->parent())->hide();
                    QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                    dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                } else {
                    qDebug() << "visable == 1";
                    QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                    dynamic_cast<QWidget*>(videoFrame->parent())->show();
                    if (Settings::get().settings()->getOption(QString("base.decode.Videoout")).toInt() != 0 || visable == 1) {
                        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                        dynamic_cast<QWidget*>(decodeFrame->parent())->show();
                    } else {
                        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                        dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                    }
                }
            } else if (key == "base.decode.Videoout") {
                qDebug() << "key == base.decode.Videoout";
                int eff = Settings::get().settings()->getOption("base.decode.Effect").toInt();
                if (visable || eff == 1) {
                    qDebug() << "visable || eff == 1";
                    QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                    dynamic_cast<QWidget*>(decodeFrame->parent())->show();
                } else {
                    qDebug() << "visable || eff != 1";
                    QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                    dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
                }
            }
        }
    }, Qt::DirectConnection);
    return m_pDSettingDilog;
}

void MainWindow::play(const QList<QString> &listFiles)
{
    qInfo() << "Starting playback with" << listFiles.size() << "files";
    QList<QUrl> lstValid;
    QList<QString> lstDir;
    QList<QString> lstFile;

    if (listFiles.isEmpty())
        m_pEngine->play();

    if (listFiles.count() == 1 && QUrl(listFiles[0]).scheme().startsWith("dvd")) {
        qDebug() << "Playing DVD content";
        m_dvdUrl = QUrl(listFiles[0]);
        if (!m_pEngine->addPlayFile(m_dvdUrl)) {
            auto msg = QString(tr("Cannot play the disc"));
            m_pCommHintWid->updateWithMessage(msg);
            qDebug() << "Cannot play the disc";
            return;
        } else {
            // todo: Disable toolbar buttons
            auto msg = QString(tr("Reading DVD files..."));
            m_pDVDHintWid->updateWithMessage(msg, true);
            qDebug() << "Reading DVD files...";
        }

        m_pEngine->playByName(m_dvdUrl);
        qDebug() << "m_pEngine->playByName(m_dvdUrl)";
        return;
    }

    for (QString strFile : listFiles) {
        if(QFileInfo(QUrl(strFile).toString()).isDir()){
            lstDir << strFile;
            qDebug() << "QFileInfo(QUrl(strFile).toString()).isDir()";
        } else {
            lstFile << strFile;
            qDebug() << "QFileInfo(QUrl(strFile).toString()).isDir() == false";
        }
    }

    lstValid = m_pEngine->addPlayFiles(lstFile);  // 先添加到播放列表再播放
    qDebug() << "lstValid" << lstValid;

    m_bHaveFile = !lstValid.isEmpty();
    qDebug() << "m_bHaveFile" << m_bHaveFile;
    if (m_bHaveFile) {
        qDebug() << "m_bHaveFile";
        //The disposal is false here to prevent the introduction of the folder from blocking
        m_pEngine->playByName(lstValid[0]);
    }

    if (!lstDir.isEmpty()) {
        qDebug() << "!lstDir.isEmpty()";
        m_pEngine->blockSignals(true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QtConcurrent::run(m_pEngine, &PlayerEngine::addPlayFs, lstDir);
#else
        QtConcurrent::run([this, lstDir]() {
            m_pEngine->addPlayFs(lstDir);
        });
#endif
    } else {
        qDebug() << "lstDir.isEmpty()";
        m_bHaveFile = false;
    }
}

void MainWindow::slotFinishedAddFiles(QList<QUrl> lstValid)
{
    qDebug() << "slotFinishedAddFiles";
    if(lstValid.count() > 0 && !m_bHaveFile) {
        qDebug() << "lstValid.count() > 0 && !m_bHaveFile";
        if (!isHidden()) {
            activateWindow();
        }
        qDebug() << "m_pEngine->playByName(lstValid[0])";
        m_pEngine->playByName(lstValid[0]);
    }
    //The disposal is false here to prevent the introduction of the folder from blocking
    m_bHaveFile = false;
}

void MainWindow::updateProxyGeometry()
{
    qDebug() << "updateProxyGeometry";
    QRect view_rect = rect();

    m_pEngine->resize(view_rect.size());

    if (!m_bMiniMode) {
        qDebug() << "!m_bMiniMode";
        if (m_pTitlebar) {
            m_pTitlebar->setFixedWidth(view_rect.width());
        }

        if (m_pToolbox) {
            qDebug() << "m_pToolbox";
            QRect rfs;
            if (m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::State::Opened && !utils::check_wayland_env()) {
                rfs = QRect(5, height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT) - rect().top() - 5,
                            rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7));
            } else {
                rfs = QRect(5, height() - TOOLBOX_HEIGHT - rect().top() - 5,
                            rect().width() - 10, TOOLBOX_HEIGHT);
            }

#ifdef DTKWIDGET_CLASS_DSizeMode
            qDebug() << "DTKWIDGET_CLASS_DSizeMode";
    if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
        if (m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::State::Opened && !utils::check_wayland_env()) {
            rfs = QRect(5, height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT) - rect().top() - 5,
                        rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT + 7));
        } else {
            rfs = QRect(5, height() - TOOLBOX_DSIZEMODE_HEIGHT - rect().top() - 5,
                        rect().width() - 10, TOOLBOX_DSIZEMODE_HEIGHT);
        }
    }
#endif
            m_pToolbox->setGeometry(rfs);
            m_pToolbox->updateMircastWidget(rfs.topRight());
        }

        if (m_pPlaylist && !m_pPlaylist->toggling()) {
            qDebug() << "m_pPlaylist && !m_pPlaylist->toggling()";
            int toolbox_height = TOOLBOX_HEIGHT;
#ifdef DTKWIDGET_CLASS_DSizeMode
            if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
                toolbox_height = TOOLBOX_DSIZEMODE_HEIGHT;
            }
#endif

#ifndef __sw_64__
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + toolbox_height + 5)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            if (utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                fixed = QRect((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + toolbox_height)),
                              view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            }
#else
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + toolbox_height - 1)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
#endif
            m_pPlaylist->setGeometry(fixed);
        }
    }
}

void MainWindow::suspendToolsWindow()
{
    qDebug() << "suspendToolsWindow";
    if (!m_bMiniMode) {
        qDebug() << "!m_bMiniMode";
        if (m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Opened) {
            qDebug() << "m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Opened";
            return;
        }

//        if (qApp->applicationState() == Qt::ApplicationInactive) {

//        } else {
        // menus  are popped up
        // NOTE: menu keeps focus while hidden, so focusWindow is not used
        if (ActionFactory::get().mainContextMenu()->isVisible() ||
                ActionFactory::get().titlebarMenu()->isVisible()) {
            qDebug() << "ActionFactory::get().mainContextMenu()->isVisible() || ActionFactory::get().titlebarMenu()->isVisible()";
            return;
        }

        QPoint cursor = mapFromGlobal(QCursor::pos());
        if (m_pToolbox->isVisible()) {
            qDebug() << "m_pToolbox->isVisible()";
            if (m_pToolbox->getMircast()->isVisible() &&
                    m_pToolbox->getMircast()->geometry().contains(cursor) && !m_bLastIsTouch) {
                qDebug() << "m_pToolbox->getMircast()->isVisible() && m_pToolbox->getMircast()->geometry().contains(cursor) && !m_bLastIsTouch";
                return;
            }
            if (insideToolsArea(cursor) && !m_bLastIsTouch) {
                qDebug() << "insideToolsArea(cursor) && !m_bLastIsTouch";
                return;
            }
        } else {
            if (m_pToolbox->geometry().contains(cursor)) {
                qDebug() << "m_pToolbox->geometry().contains(cursor)";
                return;
            }
        }
//        }

        if (m_pToolbox->anyPopupShown()) {
            qDebug() << "m_pToolbox->anyPopupShown()";
            return;
        }

        if (m_pEngine->state() == PlayerEngine::Idle) {
            qDebug() << "m_pEngine->state() == PlayerEngine::Idle";
            return;
        }

        if (m_autoHideTimer.isActive()) {
            qDebug() << "m_autoHideTimer.isActive()";
            return;
        }

        if (isFullScreen()) {
            qDebug() << "isFullScreen()";
            if (qApp->focusWindow() == this->windowHandle()) {
                qDebug() << "qApp->focusWindow() == this->windowHandle()";
                qApp->setOverrideCursor(Qt::BlankCursor);
            } else {
                qDebug() << "qApp->focusWindow() != this->windowHandle()";
                qApp->setOverrideCursor(Qt::ArrowCursor);
            }
        }

        if (m_pToolbox->getbAnimationFinash()) {
            qDebug() << "m_pToolbox->getbAnimationFinash()";
            m_pToolbox->hide();

            if (m_pToolbox->getMircast()->isVisible()) {
                qDebug() << "m_pToolbox->getMircast()->isVisible()";
                m_pToolbox->hideMircastWidget();
            }
        }
        //reset focus to mainWindow when the titlebar and toolbox is hedden
        //the tab focus will be re-executed in the order set
        m_pTitlebar->setFocus();
        m_pTitlebar->hide();        //隐藏操作应放在设置焦点后
    } else {
        qDebug() << "!m_bMiniMode";
        if (m_autoHideTimer.isActive()) {
            qDebug() << "m_autoHideTimer.isActive()";
            return;
        }

        m_pMiniPlayBtn->hide();
        m_pMiniCloseBtn->hide();
        m_pMiniQuitMiniBtn->hide();
    }
}

void MainWindow::resumeToolsWindow()
{
    qDebug() << "resumeToolsWindow";
    if (m_pEngine->state() != PlayerEngine::Idle &&
            qApp->applicationState() == Qt::ApplicationActive) {
        // playlist's previous state was Opened
        qDebug() << "m_pEngine->state() != PlayerEngine::Idle && qApp->applicationState() == Qt::ApplicationActive";
        if (m_pPlaylist && m_pPlaylist->state() != PlaylistWidget::Closed &&
                !frameGeometry().contains(QCursor::pos())) {
            qDebug() << "m_pPlaylist && m_pPlaylist->state() != PlaylistWidget::Closed && !frameGeometry().contains(QCursor::pos())";
            goto _finish;
        }
    }

    qApp->restoreOverrideCursor();
    setCursor(Qt::ArrowCursor);

    if (!m_bMiniMode) {
        qDebug() << "!m_bMiniMode";
        if (!m_bTouchChangeVolume) {
            qDebug() << "!m_bTouchChangeVolume";
            m_pTitlebar->setVisible(!isFullScreen());
            m_pToolbox->show();
            qDebug() << "m_pToolbox->show()";
        } else {
            qDebug() << "m_bTouchChangeVolume";
            m_pToolbox->hide();
            qDebug() << "m_pToolbox->hide()";
        }
    } else {
	    //迷你模式根据半屏模式显示控件
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        qDebug() << "QT_VERSION < QT_VERSION_CHECK(6, 0, 0)";
        int nScreenHeight = QApplication::desktop()->availableGeometry().height();
#else
        qDebug() << "QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)";
        int nScreenHeight = qApp->primaryScreen()->availableGeometry().height();
#endif
        qDebug() << "nScreenHeight" << nScreenHeight;
        QRect rt = rect();
        qDebug() << "rt.height()" << rt.height();
        if(rt.height() >= nScreenHeight-100){
            qDebug() << "rt.height() >= nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(false);
            m_pMiniCloseBtn->setVisible(false);
            m_pMiniQuitMiniBtn->setVisible(false);
            m_pToolbox->setVisible(false);
        }else {
            qDebug() << "rt.height() < nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(m_bMiniMode);
            m_pMiniCloseBtn->setVisible(m_bMiniMode);
            m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);
        }
    }

_finish:
    qDebug() << "_finish";
    m_autoHideTimer.start(AUTOHIDE_TIMEOUT);
}

void MainWindow::checkOnlineState(const bool bIsOnline)
{
    qDebug() << "checkOnlineState";
    if (!bIsOnline) {
        qDebug() << "!bIsOnline";
        this->sendMessage(QIcon(":/icons/deepin/builtin/icons/ddc_warning_30px.svg"), QObject::tr("Network disconnected"));
    }
}

void MainWindow::checkOnlineSubtitle(const OnlineSubtitle::FailReason reason)
{
    qDebug() << "checkOnlineSubtitle";
    if (OnlineSubtitle::FailReason::NoSubFound == reason) {
        qDebug() << "OnlineSubtitle::FailReason::NoSubFound == reason";
        m_pCommHintWid->updateWithMessage(tr("No matching online subtitles"));
    }
}

void MainWindow::checkWarningMpvLogsChanged(const QString sPrefix, const QString sText)
{
    qDebug() << "checkWarningMpvLogsChanged";
    QString warningMessage(sText);
    qInfo() << "checkWarningMpvLogsChanged" << sText;
    if (warningMessage.contains(QString("Hardware does not support image size 3840x2160"))) {
        qDebug() << "warningMessage.contains(QString(\"Hardware does not support image size 3840x2160\")";
        requestAction(ActionFactory::TogglePause);

        DDialog *pDialog = new DDialog;
        pDialog->setFixedWidth(440);
        QImage icon = utils::LoadHiDPIImage(":/resources/icons/warning.svg");
        QPixmap pix = QPixmap::fromImage(icon);
        pDialog->setIcon(QIcon(pix));
        pDialog->setMessage(tr("4K video may be stuck"));
        pDialog->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect();
        effect->setOffset(0, 4);
        effect->setColor(QColor(0, 145, 255, 76));
        effect->setBlurRadius(4);
        pDialog->getButton(0)->setFixedWidth(340);
        pDialog->getButton(0)->setGraphicsEffect(effect);
#ifndef USE_TEST
        pDialog->exec();
#else
        pDialog->show();
        pDialog->deleteLater();
#endif
        QTimer::singleShot(500, [ = ]() {
            //startPlayStateAnimation(true);
            if (!m_bMiniMode) {
                qDebug() << "!m_bMiniMode";
                m_pAnimationlable->playAnimation();
            }
            m_pEngine->pauseResume();
        });
    }

}

void MainWindow::slotdefaultplaymodechanged(const QString &sKey, const QVariant &value)
{
    qDebug() << "slotdefaultplaymodechanged";
    if (sKey != "base.play.playmode") {
        qInfo() << "Settings key error";
        return;
    }
    QPointer<DSettingsOption> modeOpt = Settings::get().settings()->option("base.play.playmode");
    QString sMode = modeOpt->data("items").toStringList()[value.toInt()];
    if (sMode == tr("Order play")) {
        qDebug() << "sMode == tr(\"Order play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (sMode == tr("Shuffle play")) {
        qDebug() << "sMode == tr(\"Shuffle play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (sMode == tr("Single play")) {
        qDebug() << "sMode == tr(\"Single play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (sMode == tr("Single loop")) {
        qDebug() << "sMode == tr(\"Single loop\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (sMode == tr("List loop")) {
        qDebug() << "sMode == tr(\"List loop\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
}

void MainWindow::onSetDecodeModel(const QString &key, const QVariant &value)
{
    Q_UNUSED(key);
    qDebug() << "onSetDecodeModel";
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy = dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());
    if(pMpvProxy && value.toInt() != 3) {
        qDebug() << "pMpvProxy && value.toInt() != 3";
        pMpvProxy->setDecodeModel(value);
    }
}

void MainWindow::onRefreshDecode()
{
    qDebug() << "onRefreshDecode";
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy =  dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());
    if(pMpvProxy) {
        qDebug() << "pMpvProxy";
        pMpvProxy->refreshDecode();
    }
}

void MainWindow::my_setStayOnTop(const QWidget *pWidget, bool bOn)
{
    qDebug() << "my_setStayOnTop";
    Q_ASSERT(pWidget);

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
    xev.xclient.window = pWidget->winId();
    xev.xclient.format = 32;

    xev.xclient.data.l[0] = bOn ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
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
    qDebug() << "slotmousePressTimerTimeOut";
    m_mousePressTimer.stop();
    if (m_bMiniMode || m_bInBurstShootMode || !m_bMousePressed) {
        qDebug() << "m_bMiniMode || m_bInBurstShootMode || !m_bMousePressed";
        return;
    }

    if (insideToolsArea(QCursor::pos())) {
        qDebug() << "insideToolsArea(QCursor::pos())";
        return;
    }

    resumeToolsWindow();
    m_bMousePressed = false;
    m_bIsTouch = false;
}

void MainWindow::slotPlayerStateChanged()
{
    qDebug() << "slotPlayerStateChanged";
    bool bAudio = false;
    PlayerEngine *pEngine = dynamic_cast<PlayerEngine *>(sender());
    if (!pEngine) return;
    setInit(pEngine->state() != PlayerEngine::Idle);
    resumeToolsWindow();
    updateWindowTitle();

    // delayed checking if engine is still idle, in case other videos are schedulered (next/prev req)
    // and another resize event will happen after that
    QTimer::singleShot(100, [ = ]() {
        if (pEngine->state() == PlayerEngine::Idle && !m_bMiniMode
                && windowState() == Qt::WindowNoState && !isFullScreen()) {
            this->setMinimumSize(QSize(614, 500));
            this->resize(850, 600);
        }
    });

    if (m_pEngine->playlist().count() > 0) {
        bAudio =  m_pEngine->currFileIsAudio();
    }
    if (m_pEngine->state() == PlayerEngine::CoreState::Playing && bAudio) {
        qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Playing && bAudio";
        m_pMovieWidget->startPlaying();
    } else if ((m_pEngine->state() == PlayerEngine::CoreState::Paused) && bAudio) {
        qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Paused && bAudio";
        m_pMovieWidget->pausePlaying();
    } else if (pEngine->state() == PlayerEngine::CoreState::Idle) {
        qDebug() << "pEngine->state() == PlayerEngine::CoreState::Idle";
        m_pMovieWidget->stopPlaying();
    }
}

void MainWindow::slotFocusWindowChanged()
{
    qDebug() << "slotFocusWindowChanged";
    if (qApp->focusWindow() != windowHandle()) {
        qDebug() << "qApp->focusWindow() != windowHandle()";
        suspendToolsWindow();
    } else {
        qDebug() << "qApp->focusWindow() == windowHandle()";
        resumeToolsWindow();
    }
}

/*void MainWindow::slotElapsedChanged()
{
#ifndef __mips__
    PlayerEngine *engine = dynamic_cast<PlayerEngine *>(sender());
    if (engine) {
        m_pProgIndicator->updateMovieProgress(engine->duration(), engine->elapsed());
    }
#endif
}*/

void MainWindow::slotFileLoaded()
{
    qDebug() << "slotFileLoaded";
    PlayerEngine *pEngine = dynamic_cast<PlayerEngine *>(sender());
    if (!pEngine) return;
    qDebug() << "pEngine";
    m_nRetryTimes = 0;
    if (utils::check_wayland_env() && windowState() == Qt::WindowNoState && m_lastRectInNormalMode.isValid()) {
        qDebug() << "utils::check_wayland_env() && windowState() == Qt::WindowNoState && m_lastRectInNormalMode.isValid()";
        const MovieInfo &mi = pEngine->playlist().currentInfo().mi;
        if (!m_bMiniMode) {
            qDebug() << "!m_bMiniMode";
            if (utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                //wayland下存在最大化>全屏->全屏->最小化，窗口超出界面问题。且现在用不着videosize大小窗口
                m_lastRectInNormalMode.setSize({850, 600});
            } else {
                qDebug() << "!utils::check_wayland_env()";
                m_lastRectInNormalMode.setSize({mi.width, mi.height});
            }
        }
    }
    this->resizeByConstraints();
    m_bIsFree = true;
}

void MainWindow::slotUrlpause(bool bStatus)
{
    qDebug() << "slotUrlpause";
    if (bStatus) {
        qDebug() << "bStatus";
        auto msg = QString(tr("Buffering..."));
        m_pCommHintWid->updateWithMessage(msg);
    }
}

void MainWindow::slotFontChanged(const QFont &/*font*/)
{
    qDebug() << "slotFontChanged";
#ifndef __mips__
    QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
    qDebug() << "fm";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "QT_VERSION < QT_VERSION_CHECK(6, 0, 0)";
    m_pToolbox->getfullscreentimeLabel()->setMinimumWidth(fm.width(m_pToolbox->getfullscreentimeLabel()->text()));
    m_pToolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.width(m_pToolbox->getfullscreentimeLabelend()->text()));
#else
    m_pToolbox->getfullscreentimeLabel()->setMinimumWidth(fm.horizontalAdvance(m_pToolbox->getfullscreentimeLabel()->text()));
    m_pToolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.horizontalAdvance(m_pToolbox->getfullscreentimeLabelend()->text()));
#endif
    qDebug() << "pixelsWidth";
    int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
    qDebug() << "pixelsWidth";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qDebug() << "QT_VERSION < QT_VERSION_CHECK(6, 0, 0)";
    QRect deskRect = QApplication::desktop()->availableGeometry();
#else
    QRect deskRect = qApp->primaryScreen()->availableGeometry();
#endif
    qDebug() << "deskRect";
    m_pFullScreenTimeLable->setGeometry(deskRect.width() - pixelsWidth - 32, 40, pixelsWidth + 32, 36);
#endif
}

void MainWindow::slotMuteChanged(bool bMute)
{
    qDebug() << "slotMuteChanged";
    m_pEngine->setMute(bMute);

    if (bMute) {
        qDebug() << "bMute";
        m_pCommHintWid->updateWithMessage(tr("Mute"));
    } else {
        qDebug() << "!bMute";
        m_pCommHintWid->updateWithMessage(tr("Volume: %1%").arg(m_nDisplayVolume));   // 取消静音时显示音量提示
    }
}

/*void MainWindow::slotAwaacelModeChanged(const QString &sKey, const QVariant &value)
{
    if (sKey != "base.play.hwaccel") {
        qInfo() << "Settings key error";
        return;
    }

    setHwaccelMode(value);
}*/

void MainWindow::slotVolumeChanged(int nVolume)
{
    qDebug() << "slotVolumeChanged";
    m_nDisplayVolume = nVolume;
    m_pEngine->changeVolume(nVolume);
    if (m_pPresenter) {
        m_pPresenter->slotvolumeChanged();
    }

    if (nVolume == 0) {
        m_pCommHintWid->updateWithMessage(tr("Mute"));
    } else {
        m_pCommHintWid->updateWithMessage(tr("Volume: %1%").arg(nVolume));
    }
}

void MainWindow::slotWMChanged()
{
    qDebug() << "slotWMChanged";
    m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();

    m_pAnimationlable->setWM(m_bIsWM);
    m_pCommHintWid->setWM(m_bIsWM);
}

void MainWindow::slotMediaError()
{
    qDebug() << "slotMediaError";
    qWarning() << "Media playback error occurred";
    m_pCommHintWid->updateWithMessage(tr("Cannot open file or stream"));
    m_pEngine->playlist().remove(m_pEngine->playlist().current());
}

void MainWindow::mircastSuccess(QString name)
{
    qDebug() << "mircastSuccess";
    if (m_pEngine->state() == PlayerEngine::Playing) {
        qDebug() << "m_pEngine->state() == PlayerEngine::Playing";
        m_pEngine->pauseResume();
    }
    updateActionsState();
    m_pMircastShowWidget->setDeviceName(name);
    m_pMircastShowWidget->show();
    m_pToolbox->hideMircastWidget();
}

void MainWindow::exitMircast()
{
    qDebug() << "exitMircast";
    if (m_pEngine->state() == PlayerEngine::Playing) {
        qDebug() << "m_pEngine->state() == PlayerEngine::Playing";
        m_pEngine->pauseResume();
    }
    m_pEngine->seekAbsolute(m_pToolbox->getSlider()->value());
    updateActionsState();
    m_pToolbox->getMircast()->slotExitMircast();
    m_pMircastShowWidget->hide();
}

QString MainWindow::cpuHardwareByDBus()
{
    qDebug() << "cpuHardwareByDBus";
    QString validFrequency = "CurrentSpeed";
    QDBusInterface systemInfoInterface("org.deepin.dde.SystemInfo1",
                                       "/org/deepin/dde/SystemInfo1",
                                       "org.freedesktop.DBus.Properties",
                                       QDBusConnection::sessionBus());
    qDebug() << "systemInfoInterface.isValid: " << systemInfoInterface.isValid();
    QDBusMessage replyCpu = systemInfoInterface.call("Get", "org.deepin.dde.SystemInfo1", "CPUHardware");
    QList<QVariant> outArgsCPU = replyCpu.arguments();
    if (outArgsCPU.count()) {
        qDebug() << "outArgsCPU.count()";
        QString CPUHardware = outArgsCPU.at(0).value<QDBusVariant>().variant().toString();
        qInfo() << __FUNCTION__ << __LINE__ << "Current CPUHardware: " << CPUHardware;

        return CPUHardware;
    }
    qDebug() << "outArgsCPU.count() == 0";
    return "";
}

void MainWindow::checkErrorMpvLogsChanged(const QString sPrefix, const QString sText)
{
    qDebug() << "checkErrorMpvLogsChanged";
    QString sErrorMessage(sText);
    if (sErrorMessage.toLower().contains(QString("avformat_open_input() failed"))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"avformat_open_input() failed\")";
        //do nothing
    } else if (sErrorMessage.toLower().contains(QString("fail")) && sErrorMessage.toLower().contains(QString("open"))
               && !sErrorMessage.toLower().contains(QString("dlopen"))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"fail\") && sErrorMessage.toLower().contains(QString(\"open\") && !sErrorMessage.toLower().contains(QString(\"dlopen\")";
        m_pCommHintWid->updateWithMessage(tr("Cannot open file or stream"));
        m_pEngine->playlist().remove(m_pEngine->playlist().current());
    } else if (sErrorMessage.toLower().contains(QString("fail")) &&
               (sErrorMessage.toLower().contains(QString("format")))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"fail\") && sErrorMessage.toLower().contains(QString(\"format\")";
        //Open the URL there is three cases of legal paths, illegal paths, and semi-legal
        //paths, which only processes the prefix legality, the suffix is not legal
        //please refer to other places to modify
        //powered by xxxxp
        if (m_pEngine->playlist().currentInfo().mi.title.isEmpty()) {
            qDebug() << "m_pEngine->playlist().currentInfo().mi.title.isEmpty()";
            m_pCommHintWid->updateWithMessage(tr("Parse failed"));
            m_pEngine->playlist().remove(m_pEngine->playlist().current());
        } else {
            if (m_nRetryTimes < 10) {
                qDebug() << "m_nRetryTimes < 10";
                m_nRetryTimes++;
                requestAction(ActionFactory::ActionKind::StartPlay);
            } else {
                qDebug() << "m_nRetryTimes >= 10";
                m_nRetryTimes = 0;
                m_pCommHintWid->updateWithMessage(tr("Invalid file"));
                m_pEngine->playlist().remove(m_pEngine->playlist().current());
            }
        }
    } else if (sErrorMessage.toLower().contains(QString("moov atom not found"))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"moov atom not found\")";
        m_pCommHintWid->updateWithMessage(tr("Invalid file"));
    } else if (sErrorMessage.toLower().contains(QString("couldn't open dvd device"))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"couldn't open dvd device\")";
        m_pCommHintWid->updateWithMessage(tr("Please insert a CD/DVD"));
    } else if (sErrorMessage.toLower().contains(QString("incomplete frame")) ||
               sErrorMessage.toLower().contains(QString("MVs not available"))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"incomplete frame\") || sErrorMessage.toLower().contains(QString(\"MVs not available\")";
    } else if ((sErrorMessage.toLower().contains(QString("can't"))) &&
               (sErrorMessage.toLower().contains(QString("open")))) {
        qDebug() << "sErrorMessage.toLower().contains(QString(\"can't\") && sErrorMessage.toLower().contains(QString(\"open\")";
        m_pCommHintWid->updateWithMessage(tr("No video file found"));
    }
}

void MainWindow::closeEvent(QCloseEvent *pEvent)
{
    qDebug() << "closeEvent";
    qInfo() << "Application closing, saving state";
    if(m_pMircastShowWidget&&m_pMircastShowWidget->isVisible()) {
        qDebug() << "m_pMircastShowWidget&&m_pMircastShowWidget->isVisible()";
        slotExitMircast();
    }
    if (m_bInBurstShootMode) {
        qDebug() << "m_bInBurstShootMode";
        pEvent->ignore();
        return;
    }

    if (m_nLastCookie > 0) {
        qInfo() << "Releasing system standby inhibition, cookie:" << m_nLastCookie;
        utils::UnInhibitStandby(m_nLastCookie);
        qInfo() << "uninhibit cookie" << m_nLastCookie;
        m_nLastCookie = 0;
    }

    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        qDebug() << "Settings::get().isSet(Settings::ResumeFromLast)";
        int nCur = 0;
        nCur = m_pEngine->playlist().current();
        if (nCur >= 0) {
            qDebug() << "nCur >= 0";
            Settings::get().setInternalOption("playlist_pos", nCur);
        }
    }

    qDebug() << "static QEventLoop loop";
    static QEventLoop loop;
    QFileSystemWatcher fileWatcher;
    bool needWait = false;
    fileWatcher.addPath(Settings::get().configPath());
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, [=](){
        qDebug() << "fileWatcher.addPath(Settings::get().configPath())";
        loop.quit();
    });
    //关闭窗口时保存音量值
    int volume = Settings::get().internalOption("global_volume").toInt();
    if (m_nDisplayVolume != volume) {
        qDebug() << "m_nDisplayVolume != volume";
        Settings::get().setInternalOption("global_volume", m_nDisplayVolume > 100 ? 100 : m_nDisplayVolume);
        needWait = true;
    }
    if (Settings::get().settings()->getOption("set.start.crash").toInt() != 0) {
        qDebug() << "Settings::get().settings()->getOption(\"set.start.crash\").toInt() != 0";
        Settings::get().onSetCrash();
        needWait = true;
    }
    if (needWait) {
        qDebug() << "needWait";
        loop.exec();
    }
    m_pEngine->savePlaybackPosition();

    pEvent->accept();
    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
#ifndef _LIBDMR_
        if (Settings::get().isSet(Settings::ClearWhenQuit)) {
            qDebug() << "Settings::get().isSet(Settings::ClearWhenQuit)";
            m_pEngine->playlist().clearPlaylist();
        } else {
            //persistently save current playlist
            qDebug() << "!Settings::get().isSet(Settings::ClearWhenQuit)";
            m_pEngine->playlist().savePlaylist();
        }
#endif
        // xcb close slow so add this for wayland  by xxj
        DMainWindow::closeEvent(pEvent);
        m_pEngine->stop();
        disconnect(m_pEngine, nullptr, nullptr, nullptr);
        disconnect(&m_pEngine->playlist(), nullptr, nullptr, nullptr);
        if (m_pEngine) {
            delete m_pEngine;
            m_pEngine = nullptr;
        }
        CompositingManager::get().setTestFlag(true);
        /*lmh0724临时规避退出崩溃问题*/
        QApplication::quit();
        _Exit(0);
    }
}

void MainWindow::wheelEvent(QWheelEvent *pEvent)
{
    qDebug() << "wheelEvent";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (insideToolsArea(pEvent->pos()) || insideResizeArea(pEvent->globalPos())) {
        qDebug() << "insideToolsArea(pEvent->pos()) || insideResizeArea(pEvent->globalPos())";
        return;
    }
    if (m_pToolbox->isInMircastWidget(pEvent->pos())) {
        qDebug() << "m_pToolbox->isInMircastWidget(pEvent->pos())";
        return;
    }
#else
    if (insideToolsArea(pEvent->position().toPoint()) || insideResizeArea(pEvent->globalPosition().toPoint())) {
        qDebug() << "insideToolsArea(pEvent->position().toPoint()) || insideResizeArea(pEvent->globalPosition().toPoint())";
        return;
    }

    if (m_pToolbox->isInMircastWidget(pEvent->position().toPoint())) {
        qDebug() << "m_pToolbox->isInMircastWidget(pEvent->position().toPoint())";
        return;
    }
#endif

    if (m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Opened) {
        qDebug() << "m_pPlaylist && m_pPlaylist->state() == PlaylistWidget::Opened";
        pEvent->ignore();
        return;
    }

    if (pEvent->buttons() == Qt::NoButton && pEvent->modifiers() == Qt::NoModifier && m_pToolbox->getVolSliderIsHided()) {
        qDebug() << "pEvent->buttons() == Qt::NoButton && pEvent->modifiers() == Qt::NoModifier && m_pToolbox->getVolSliderIsHided()";
        m_iAngleDelta = pEvent->angleDelta().y() ;
        if( m_iAngleDelta < -240){     //对滚轮距离出现异常值时的约束处理
            qDebug() << "m_iAngleDelta < -240";
            m_iAngleDelta = -120;
        }else if(m_iAngleDelta > 240 ){
            qDebug() << "m_iAngleDelta > 240";
            m_iAngleDelta = 120;
        }
        requestAction(pEvent->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
    }
}

void MainWindow::focusInEvent(QFocusEvent *pEvent)
{
    qDebug() << "focusInEvent";
    resumeToolsWindow();
}

void MainWindow::hideEvent(QHideEvent *pEvent)
{
    qDebug() << "hideEvent";
    QMainWindow::hideEvent(pEvent);
}

void MainWindow::showEvent(QShowEvent *pEvent)
{
    qDebug() << "showEvent";
    m_pAnimationlable->raise();
    m_pTitlebar->raise();
    m_pToolbox->raise();
    if (m_pPlaylist) {
        m_pPlaylist->raise();
    }
    //判断屏幕可用坐标与应用的geometry是否有交集，没有就设置到可用屏幕坐标中
    QRect geoRect = geometry();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect deskRect = QApplication::desktop()->availableGeometry(geoRect.topLeft());
#else
    QRect deskRect = qApp->screenAt(geoRect.topLeft())->availableGeometry();
#endif

    if(!deskRect.intersects(geoRect)) {
        setGeometry(QRect(deskRect.x(), deskRect.y(), geoRect.width(), geoRect.width()));
    }
    resumeToolsWindow();

    if (!qgetenv("FLATPAK_APPID").isEmpty()) {
        qInfo() << "workaround for flatpak";
        if (m_pPlaylist->isVisible())
            updateProxyGeometry();
    }

    QMainWindow::showEvent(pEvent);
}

void MainWindow::resizeByConstraints(bool bForceCentered)
{
    qDebug() << "resizeByConstraints";
    if (m_pEngine->state() == PlayerEngine::Idle || m_pEngine->playlist().count() == 0) {
        qInfo() << "Window resize skipped: Player is idle or playlist is empty";
        m_pTitlebar->setTitletxt(QString());
        return;
    }

    if (m_bMiniMode || isFullScreen() || isMaximized()) {
        qInfo() << "Window resize skipped: Window is in mini mode, fullscreen or maximized";
        return;
    }

    qInfo() << "Resizing window with constraints";
    updateWindowTitle();
    //lmh0710修复窗口变成影片分辨率问题
    if (utils::check_wayland_env()) {
        qInfo() << "Window resize skipped: Running in Wayland environment";
        return;
    }

    const MovieInfo &mi = m_pEngine->playlist().currentInfo().mi;
    QSize vidoeSize = m_pEngine->videoSize();
    if (CompositingManager::get().platform() == Platform::Mips)
        m_pCommHintWid->syncPosition();
    if (vidoeSize.isEmpty()) {
        vidoeSize = QSize(mi.width, mi.height);
        qInfo() << "Using movie info dimensions:" << mi.width << "x" << mi.height;
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto geom = qApp->desktop()->availableGeometry(this);
#else
    auto geom = QGuiApplication::primaryScreen()->availableGeometry();
#endif
    if (vidoeSize.width() > geom.width() || vidoeSize.height() > geom.height()) {
        qInfo() << "Scaling video size to fit screen:" << vidoeSize << "->" << geom.size();
        vidoeSize.scale(geom.width(), geom.height(), Qt::KeepAspectRatio);
    }

    qInfo() << "Window resize:" << "original:" << size() << "requested:" << vidoeSize;
    if (size() == vidoeSize) {
        qInfo() << "Window size unchanged, skipping resize";
        return;
    }

    if (bForceCentered) {
        qDebug() << "bForceCentered";
        QRect r;
        r.setSize(vidoeSize);
        r.moveTopLeft({(geom.width() - r.width()) / 2, (geom.height() - r.height()) / 2});
        if (utils::check_wayland_env()) {
            qDebug() << "utils::check_wayland_env()";
            this->setGeometry(r);
            this->move(r.x(), r.y());
            this->resize(r.width(), r.height());
        }
    } else {
        if (utils::check_wayland_env()) {
            qDebug() << "utils::check_wayland_env()";
            QRect r = this->geometry();
            r.setSize(vidoeSize);
            this->setGeometry(r);
            this->move(r.x(), r.y());
            this->resize(r.width(), r.height());
        }
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void MainWindow::updateSizeConstraints()
{
    qDebug() << "updateSizeConstraints";
    QSize size;

    if (m_bMiniMode) {
        size = QSize(40, 40);
    } else {
        size = QSize(614, 500);
    }
    this->setMinimumSize(size);
}

void MainWindow::updateGeometryNotification(const QSize &sz)
{
    qDebug() << "updateGeometryNotification";
    QString sMsg = QString("%1x%2").arg(sz.width()).arg(sz.height());
    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << "updateGeometryNotification" << sMsg;
        m_pCommHintWid->updateWithMessage(sMsg);
    }

    //Wayland's player is normal to active, so add a judgment here
    if ((windowState() == Qt::WindowNoState || utils::check_wayland_env() && windowState() == Qt::WindowActive)
            &&  !m_isSettingMiniMode && !m_bMiniMode && !m_bMaximized) {
        qDebug() << "updateGeometryNotification" << sMsg;
        m_lastRectInNormalMode = geometry();
    }
}

void MainWindow::LimitWindowize()
{
    qDebug() << "LimitWindowize";
    if (!m_bMiniMode && (geometry().width() == 380 || geometry().height() == 380)) {
        qDebug() << "LimitWindowize" << geometry().width() << geometry().height();
        setGeometry(m_lastRectInNormalMode);
    }
}

void MainWindow::resizeEvent(QResizeEvent *pEvent)
{
    qInfo() << __func__ << geometry();
    if (utils::check_wayland_env()) {
        if (m_pToolbox) {
            m_pToolbox->setFixedWidth(this->width() - 10);
        }
    }
    if (m_pProgIndicator && isFullScreen()) {
        m_pProgIndicator->move(geometry().width() - m_pProgIndicator->width() - 18, 8);
    }
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
    if (CompositingManager::get().platform() != Platform::X86) {
        QPoint relativePoint = mapToGlobal(QPoint(0, 0));
        m_pToolbox->updateSliderPoint(relativePoint);
    }
    m_pMovieWidget->resize(rect().size());
    m_pMovieWidget->move(0, 0);

    m_pMircastShowWidget->resize(rect().size());
    m_pMircastShowWidget->updateView();
    m_pMircastShowWidget->move(0, 0);

    m_pAnimationlable->move(QPoint((width() - m_pAnimationlable->width()) / 2,
                                   (height() - m_pAnimationlable->height()) / 2));
    if(m_bMiniMode) {//迷你模式显示与半屏模式处理
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        int nScreenHeight = QApplication::desktop()->availableGeometry().height();
#else
        int nScreenHeight = qApp->primaryScreen()->availableGeometry().height();
#endif
        QRect rt = rect();
        if(rt.height() >= nScreenHeight-100){
            m_pMiniPlayBtn->setVisible(false);
            m_pMiniCloseBtn->setVisible(false);
            m_pMiniQuitMiniBtn->setVisible(false);
            m_pToolbox->setVisible(false);
        }else {
            m_pMiniPlayBtn->setVisible(m_bMiniMode);
            m_pMiniCloseBtn->setVisible(m_bMiniMode);
            m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);
        }
    }
}

void MainWindow::updateWindowTitle()
{
    qDebug() << "updateWindowTitle";
    if (m_pEngine->state() != PlayerEngine::Idle) {
        qDebug() << "State is not idle";
        const MovieInfo &mi = m_pEngine->playlist().currentInfo().mi;
        QString sTitle = m_pTitlebar->fontMetrics().elidedText(mi.title,
                                                               Qt::ElideMiddle, m_pTitlebar->contentsRect().width() - 400);
        m_pTitlebar->setTitletxt(sTitle);
        setWindowTitle(mi.filePath);
        m_pTitlebar->setTitleBarBackground(true);
    } else {
        qDebug() << "State is idle";
        m_pTitlebar->setTitletxt(QString());
        setWindowTitle(QString());
        m_pTitlebar->setTitleBarBackground(false);
    }
    m_pTitlebar->setProperty("idle", m_pEngine->state() == PlayerEngine::Idle);
}

void MainWindow::moveEvent(QMoveEvent *pEvent)
{
    qInfo() << __func__ << "进入moveEvent";
    QWidget::moveEvent(pEvent);

    if (CompositingManager::get().platform() != Platform::X86) {
        QPoint relativePoint = mapToGlobal(QPoint(0, 0));
        m_pToolbox->updateSliderPoint(relativePoint);
        m_pCommHintWid->syncPosition();
    }

    updateGeometryNotification(geometry().size());
}

void MainWindow::keyPressEvent(QKeyEvent *pEvent)
{
    if (m_pPlaylist && (m_pPlaylist->state() == PlaylistWidget::Opened) && pEvent->modifiers() == Qt::NoModifier) {
        if (pEvent) {
            m_pPlaylist->updateSelectItem(pEvent->key());
        }
        pEvent->setAccepted(true);
    }
    QWidget::keyPressEvent(pEvent);
}

void MainWindow::keyReleaseEvent(QKeyEvent *pEvent)
{
    QWidget::keyReleaseEvent(pEvent);
}

static bool s_bAfterDblClick = false;

void MainWindow::capturedMousePressEvent(QMouseEvent *pEvent)
{
    qDebug() << "capturedMousePressEvent";
    m_bMouseMoved = false;
    m_bMousePressed = false;
    if (CompositingManager::get().platform() != Platform::X86) {
        qDebug() << "Platform is not X86";
        m_pCommHintWid->hide();
        m_pPopupWid->hide();
    }
    if (qApp->focusWindow() == nullptr) {
        qDebug() << "focusWindow is nullptr";
        return;
    }

    if (pEvent->buttons() == Qt::LeftButton) {
        qDebug() << "LeftButton is pressed";
        m_bMousePressed = true;
    }

    m_posMouseOrigin = mapToGlobal(pEvent->pos());
}

void MainWindow::capturedMouseReleaseEvent(QMouseEvent *pEvent)
{
    qDebug() << "capturedMouseReleaseEvent";
    if (m_bIsTouch) {
        qDebug() << "m_bIsTouch is true";
        m_bLastIsTouch = true;
        m_bIsTouch = false;

        if (m_bTouchChangeVolume) {
            qDebug() << "m_bTouchChangeVolume is true";
            m_bTouchChangeVolume = false;
            m_pToolbox->setVisible(true);
        }

        if (m_bProgressChanged) {
            qDebug() << "m_bProgressChanged is true";
            m_pToolbox->updateSlider();   //手势释放时改变影片进度
            m_bProgressChanged = false;
        }
    } else {
        qDebug() << "m_bIsTouch is false";
        m_bLastIsTouch = false;
    }

    if (m_bDelayedResizeByConstraint) {
        qDebug() << "m_bDelayedResizeByConstraint is true";
        m_bDelayedResizeByConstraint = false;

        QTimer::singleShot(0, [ = ]() {
            this->setMinimumSize({0, 0});
            this->resizeByConstraints(true);
        });
    }

    if (!m_bMousePressed) {
        qDebug() << "m_bMousePressed is false";
        s_bAfterDblClick = false;
        m_bMouseMoved = false;
    }

    if (qApp->focusWindow() == nullptr || !m_bMousePressed) {
        qDebug() << "qApp->focusWindow() is nullptr || m_bMousePressed is false";
        return;
    }

    m_bMousePressed = false;

    //NOTE: If the mouseMoveEvent of the titlebar is triggered
    // reset the status here, otherwise it cannot respond to the mini mode shortcut
    if (m_pTitlebar->geometry().contains(pEvent->pos())) {
        qDebug() << "m_pTitlebar->geometry().contains(pEvent->pos())";
        m_bMouseMoved = false;
    }
}

void MainWindow::capturedKeyEvent(QKeyEvent *pEvent)
{
    qDebug() << "capturedKeyEvent";
    if (pEvent->key() == Qt::Key_Tab) {
        if (!isFullScreen()) {
            qDebug() << "!isFullScreen()";
            m_pTitlebar->show();
        }
        qDebug() << "m_pToolbox->show()";
        m_pToolbox->show();
        m_autoHideTimer.start(AUTOHIDE_TIMEOUT);  //如果点击tab键，重置计时器
    }
}

void MainWindow::mousePressEvent(QMouseEvent *pEvent)
{
    qDebug() << "mousePressEvent";
    m_bMouseMoved = false;
    m_bMousePressed = false;
    if (CompositingManager::get().platform() != Platform::X86) {
        qDebug() << "Platform is not X86";
        m_pCommHintWid->hide();
        m_pPopupWid->hide();
    }
    if (qApp->focusWindow() == nullptr) {
        qDebug() << "focusWindow is nullptr";
        return;
    }
    if (pEvent->buttons() == Qt::LeftButton) {
        qDebug() << "LeftButton is pressed";
        m_bMousePressed = true;
        if (!m_mousePressTimer.isActive() && m_bIsTouch) {
            qDebug() << "m_mousePressTimer.isActive() && m_bIsTouch";
            m_mousePressTimer.stop();

            m_nLastPressX = mapToGlobal(QCursor::pos()).x();
            m_nLastPressY = mapToGlobal(QCursor::pos()).y();
            qInfo() << __func__ << "已经进入触屏按下事件" << m_nLastPressX << m_nLastPressY;
            m_mousePressTimer.start();
        }
    }

    m_posMouseOrigin = mapToGlobal(pEvent->pos());
    m_pressPoint = pEvent->pos();
}

void MainWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    /// NOTE: 为了其它控件的鼠标操作与MainWindow一致，统一使用capturedMouseReleaseEvent()捕获鼠标释放
    /// 事件，若无特殊要求，请尽量在capturedMouseReleaseEvent()进行处理。

    qInfo() << __func__ << "进入mouseReleaseEvent";

    if (!insideResizeArea(ev->globalPos()) && !m_bMouseMoved && (m_pPlaylist->state() != PlaylistWidget::Opened)) {
        qDebug() << "!insideResizeArea(ev->globalPos()) && !m_bMouseMoved && (m_pPlaylist->state() != PlaylistWidget::Opened)";
        if (!insideToolsArea(ev->pos())) {
            qDebug() << "!insideToolsArea(ev->pos())";
            m_delayedMouseReleaseTimer.start(120);
        } else {
            if (m_pEngine->state() == PlayerEngine::CoreState::Idle && !insideToolsArea(ev->pos())) {
                qDebug() << "m_pEngine->state() == PlayerEngine::CoreState::Idle && !insideToolsArea(ev->pos())";
                m_delayedMouseReleaseTimer.start(120);
            }
        }
    }

    m_bMouseMoved = false;

    QWidget::mouseReleaseEvent(ev);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *pEvent)
{
    qInfo() << __func__ << "进入mouseDoubleClickEvent";
    if (!m_bMiniMode && this->m_pEngine->getplaylist()->getthreadstate()) {
        qInfo() << "playlist loadthread is running";
        return;
    }
    //投屏时双击操作不做处理
    if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) return;
    if (!m_bMiniMode && !m_bInBurstShootMode) {
        qDebug() << "!m_bMiniMode && !m_bInBurstShootMode";
        m_delayedMouseReleaseTimer.stop();
        if (m_pEngine->state() == PlayerEngine::Idle) {
            qDebug() << "m_pEngine->state() == PlayerEngine::Idle";
            requestAction(ActionFactory::StartPlay);
        } else {
            qDebug() << "m_pEngine->state() != PlayerEngine::Idle";
            requestAction(ActionFactory::ToggleFullscreen, false, {}, true);
        }
        pEvent->accept();
        s_bAfterDblClick = true;
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *pEvent)
{
    qDebug() << "mouseDoubleClickEvent";
    if (m_bStartMini) {
        qDebug() << "m_bStartMini";
        return;
    }
    qInfo() << __func__ << "进入mouseMoveEvent";
    QPoint ptCurr = mapToGlobal(pEvent->pos());
    QPoint ptDelta = ptCurr - this->m_posMouseOrigin;

    if (qAbs(ptDelta.x()) < 5 && qAbs(ptDelta.y()) < 5) { //避免误触
        qDebug() << "qAbs(ptDelta.x()) < 5 && qAbs(ptDelta.y()) < 5";
        return;
    }

    if (m_bIsTouch && isFullScreen()) { //全屏时才触发滑动改变音量和进度的操作
        if (qAbs(ptDelta.x()) > qAbs(ptDelta.y())
                && m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qDebug() << "qAbs(ptDelta.x()) > qAbs(ptDelta.y()) && m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_bTouchChangeVolume = false;
            m_pToolbox->updateProgress(ptDelta.x());     //改变进度条显示
            this->m_posMouseOrigin = ptCurr;
            m_bProgressChanged = true;
            return;
        } else if (qAbs(ptDelta.x()) < qAbs(ptDelta.y())) {
            qDebug() << "qAbs(ptDelta.x()) < qAbs(ptDelta.y())";
            if (ptDelta.y() > 0) {
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeDown);
            } else {
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeUp);
            }

            this->m_posMouseOrigin = ptCurr;
            return;
        }
    }
    qDebug() << "QWidget::mouseMoveEvent(pEvent)";
    QWidget::mouseMoveEvent(pEvent);

    this->m_posMouseOrigin = ptCurr;
    m_bMouseMoved = true;
}

void MainWindow::contextMenuEvent(QContextMenuEvent *pEvent)
{
    qInfo() << __func__ << "进入contextMenuEvent";
    if (m_bMiniMode || m_bInBurstShootMode) {
        qDebug() << "m_bMiniMode || m_bInBurstShootMode";
        return;
    }

    if (insideToolsArea(pEvent->pos())) {
        qDebug() << "insideToolsArea(pEvent->pos())";
        return;
    }

    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
        if (windowHandle()->property("_d_dwayland_staysontop").toBool() != m_bWindowAbove) {
            m_bWindowAbove = !m_bWindowAbove;
            reflectActionToUI(ActionFactory::WindowAbove);
        }
    } else {
        qDebug() << "!utils::check_wayland_env()";
        //通过窗口id查询窗口状态是否置顶，同步右键菜单中的选项状态
        QProcess above;
        QStringList options;
        options << "-c" << QString("xprop -id %1 | grep '_NET_WM_STATE(ATOM)'").arg(winId());
        above.start("bash", options);
        if (above.waitForStarted() && above.waitForFinished()) {
            QString drv = QString::fromUtf8(above.readAllStandardOutput().trimmed().constData());
            if (drv.contains("_NET_WM_STATE_ABOVE") != m_bWindowAbove) {
                qDebug() << "drv.contains(\"_NET_WM_STATE_ABOVE\") != m_bWindowAbove";
    //            requestAction(ActionFactory::WindowAbove);
                m_bWindowAbove = drv.contains("_NET_WM_STATE_ABOVE");
                reflectActionToUI(ActionFactory::WindowAbove);
            }
        }
    }

    if(m_pMircastShowWidget->isVisible() ) {//投屏中屏蔽全屏、迷你模式，置顶菜单
        qDebug() << "m_pMircastShowWidget->isVisible()";
        QList<ActionFactory::ActionKind> lstActId;
        lstActId << ActionFactory::ToggleFullscreen << ActionFactory::ToggleMiniMode << ActionFactory::WindowAbove;
        for(ActionFactory::ActionKind id: lstActId) {
            QList<QAction *> listActs;
            listActs = ActionFactory::get().findActionsByKind(id);
            if(listActs.size()<=0) {
                continue;
            }
            for(QAction *act: listActs) {
                act->setEnabled(false);
            }
        }
        //倍速播放、画面、声音、字幕、截图
        emit frameMenuEnable(false);
        emit playSpeedMenuEnable(false);
        emit subtitleMenuEnable(false);
        emit soundMenuEnable(false);
    }

    resumeToolsWindow();
    QTimer::singleShot(0, [ = ]() {
        qApp->restoreOverrideCursor();
        ActionFactory::get().mainContextMenu()->popup(QCursor::pos());
    });
    pEvent->accept();

//此段为通过xcb接口查询窗口状态，nItem为状态列表中的个数，properties为返回状态列表
//代码暂时无法实现需求，勿删
//    const auto display = QX11Info::display();
//    const auto screen = QX11Info::appScreen();
//    Atom atom = XInternAtom(display, "_NET_WM_STATE", true);
//    Atom type;
//    int format;
//    unsigned long nItem, bytesAfter;
//    unsigned char *properties = NULL;
//    XGetWindowProperty(display, QX11Info::appRootWindow(screen), atom, 0, (~0L), False, AnyPropertyType, &type, &format, &nItem, &bytesAfter, &properties);
//    qInfo() << atom << nItem;
//    int iItem;
//    for (iItem = 0; iItem < nItem; ++iItem)
//        qInfo() << ((long *)(properties))[iItem];
}

bool MainWindow::insideToolsArea(const QPoint &p)
{
    qDebug() << "insideToolsArea";
    return (m_pTitlebar->geometry().contains(p) && !isFullScreen()) || m_pToolbox->geometry().contains(p) || m_pToolbox->volumeSlider()->geometry().contains(p) ||
            m_pMiniPlayBtn->geometry().contains(p)|| m_pMiniCloseBtn->geometry().contains(p) || m_pMiniQuitMiniBtn->geometry().contains(p);
}

QMargins MainWindow::dragMargins() const
{
    return QMargins {MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS};
}

bool MainWindow::insideResizeArea(const QPoint &globalPos)
{
    qDebug() << "insideResizeArea";
    const QRect window_visible_rect = frameGeometry() - dragMargins();
    return !window_visible_rect.contains(globalPos);
}

void MainWindow::delayedMouseReleaseHandler()
{
    qDebug() << "delayedMouseReleaseHandler";
    if ((!s_bAfterDblClick && !m_bLastIsTouch) || m_bMiniMode) {
        qDebug() << "(!s_bAfterDblClick && !m_bLastIsTouch) || m_bMiniMode";
        requestAction(ActionFactory::TogglePause, false, {}, true);
    }

    s_bAfterDblClick = false;
}

void MainWindow::prepareSplashImages()
{
    qDebug() << "prepareSplashImages";
    m_imgBgDark = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
    m_imgBgLight = utils::LoadHiDPIImage(":/resources/icons/light/init-splash.svg");
}

void MainWindow::subtitleMatchVideo(const QString &sFileName)
{
    qDebug() << "subtitleMatchVideo";
    QString sVideoName = sFileName;
    // Search for video files with the same name as the subtitles and play the video file.
    QFileInfo subfileInfo(sFileName);
    QDir dir(subfileInfo.canonicalPath());
    dir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Size | QDir::Reversed);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo info = list.at(i);
        qInfo() << info.absoluteFilePath();
//        if (info.completeBaseName() == subfileInfo.completeBaseName()) {
        if (subfileInfo.fileName().contains(info.completeBaseName())) {
            sVideoName = info.absoluteFilePath();
        } else {
            sVideoName = nullptr;
        }
    }

    QFileInfo vfileInfo(sVideoName);
    if (vfileInfo.exists()) {
        qDebug() << "vfileInfo.exists()";
        Settings::get().setGeneralOption("last_open_path", vfileInfo.path());

        play({sVideoName});

        // Select the current subtitle display
        const PlayingMovieInfo &pmf = m_pEngine->playingMovieInfo();
        for (const SubtitleInfo &sub : pmf.subs) {
            if (sub["external"].toBool()) {
                QString path = sub["external-filename"].toString();
                if (path == subfileInfo.canonicalFilePath()) {
                    m_pEngine->selectSubtitle(pmf.subs.indexOf(sub));
                    break;
                }
            }
        }
    } else {
        qDebug() << "!vfileInfo.exists()";
        m_pCommHintWid->updateWithMessage(tr("Please load the video first"));
    }
}

void MainWindow::defaultplaymodeinit()
{
    qDebug() << "defaultplaymodeinit";
    QPointer<DSettingsOption> modeOpt = Settings::get().settings()->option("base.play.playmode");
    int nModeId = modeOpt->value().toInt();
    QString sMode = modeOpt->data("items").toStringList()[nModeId];
    if (sMode == tr("Order play")) {
        qDebug() << "sMode == Order play";
        requestAction(ActionFactory::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (sMode == tr("Shuffle play")) {
        qDebug() << "sMode == Shuffle play";
        requestAction(ActionFactory::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (sMode == tr("Single play")) {
        qDebug() << "sMode == Single play";
        requestAction(ActionFactory::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (sMode == tr("Single loop")) {
        qDebug() << "sMode == Single loop";
        requestAction(ActionFactory::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (sMode == tr("List loop")) {
        qDebug() << "sMode == List loop";
        requestAction(ActionFactory::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
}

void MainWindow::decodeInit()
{
    qDebug() << "decodeInit";
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy = dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());

    if(!pMpvProxy) {
        qDebug() << "!pMpvProxy";
        return;
    }

    //崩溃检测
    int bcatch = Settings::get().settings()->getOption(QString("set.start.crash")).toInt();
    if (bcatch == 1) {
        qDebug() << "bcatch == 1";
        pMpvProxy->setDecodeModel(DecodeMode::AUTO);
        Settings::get().settings()->setOption(QString("base.decode.select"),DecodeMode::AUTO);
    } else {
        qDebug() << "bcatch != 1";
        if (Settings::get().settings()->option("base.decode.select")->value().toInt() == 3)
            dmr::Settings::get().crashCheck();
    }
}

void MainWindow::popupAdapter(QIcon icon, QString sText)
{
    qDebug() << "popupAdapter";
    m_pPopupWid->setIcon(icon);
    DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);
    QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);
    QFontMetrics fm(font);
    auto w = fm.boundingRect(sText).width();
    m_pPopupWid->setMessage(sText);
    m_pPopupWid->resize(w + 70, 40);
    m_pPopupWid->move((width() - m_pPopupWid->width()) / 2, height() - 127);
    m_pPopupWid->show();
    m_pPopupWid->raise();
}

QString MainWindow::lastOpenedPath()
{
    qDebug() << "lastOpenedPath";
    QString lastPath = Settings::get().generalOption("last_open_path").toString();
    QDir lastDir(lastPath);
    if (lastPath.isEmpty() || !lastDir.exists()) {
        qDebug() << "lastPath.isEmpty() || !lastDir.exists()";
        lastPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir newLastDir(lastPath);
        if (!newLastDir.exists()) {
            qDebug() << "!newLastDir.exists()";
            lastPath = QDir::currentPath();
        }
    }

    return lastPath;
}

void MainWindow::paintEvent(QPaintEvent *pEvent)
{
    qDebug() << "paintEvent";
    QPainter painter(this);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    QColor bgColor = pal.color(QPalette::Window);

    if (CompositingManager::get().platform() == Platform::X86) {
        qDebug() << "CompositingManager::get().platform() == Platform::X86";
        QPainterPath path;
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            qDebug() << "DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()";
            if (m_pEngine->state() != PlayerEngine::Idle && !m_pToolbox->isVisible()) {
                path.addRect(bgRect);
                painter.fillPath(path, Qt::black);
            } else {
                qDebug() << "m_pEngine->state() != PlayerEngine::Idle && !m_pToolbox->isVisible()";
                path.addRect(bgRect);
                painter.fillPath(path, Qt::white);
            }
        }
    }

    if (m_pEngine->state() == PlayerEngine::Idle) {
        qDebug() << "m_pEngine->state() == PlayerEngine::Idle";
        QImage bg = QIcon::fromTheme("deepin-movie").pixmap(130, 130).toImage();
        //和产品、ui商议深色主题下去除深色背景效果
//        if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
//            QImage img = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash-bac.svg");
//            QPointF pos = bgRect.center() - QPoint(img.width() / 2, img.height() / 2) / devicePixelRatioF();
//            painter.drawImage(pos, img);
//        }
        qDebug() << "QImage bg = QIcon::fromTheme(\"deepin-movie\").pixmap(130, 130).toImage()";
        QPointF pos = bgRect.center() - QPoint(bg.width() / 2, bg.height() / 2) / devicePixelRatioF();
        painter.drawImage(pos, bg);
    }

    QMainWindow::paintEvent(pEvent);
}

void MainWindow::toggleUIMode()
{
    qInfo() << "Toggling UI mode, current mini mode:" << m_bMiniMode;
    // 对于最大化的窗口，需要先恢复至正常窗口，再进行迷你模式的操作
    // 因为有个时序的问题，避免窗口跳动引起用户不适，所以加入hide和show操作
    if (!m_bMiniMode && isMaximized() && !isFullScreen()) {
        qInfo() << "Toggle mini mode, now is Maximized, we need show normal first.";
        m_nStateBeforeMiniMode |= SBEM_Maximized;
        showNormal();
        hide();
        QTimer::singleShot(100, [&] {
            show();
            toggleUIMode();
        });
        return;
    }
    
    //迷你模式关闭动画及控件
    m_pAnimationlable->hide();
    m_pToolbox->closeAnyPopup();
    
    //判断窗口是否靠边停靠（靠边停靠不支持MINI模式）thx
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect deskrect = QApplication::desktop()->availableGeometry();
#else
    QRect deskrect = qApp->primaryScreen()->availableGeometry();
#endif
    QPoint windowPos = pos();
    if (this->geometry() != deskrect) {
        if (windowPos.x() == 0 && (windowPos.y() == 0 ||
                                   (abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50))) {
            if (abs(this->geometry().width() - deskrect.width() / 2) < 50) {
                qWarning() << "Cannot enter mini mode: Window is docked to screen edge";
                m_pCommHintWid->updateWithMessage(tr("Please exit smart dock"));
                m_bStartMini = false;
                reflectActionToUI(ActionFactory::ToggleMiniMode);
                return;
            }
        }
        if ((abs(windowPos.x() + this->geometry().width() - deskrect.width()) < 50)  &&
                (windowPos.y()  == 0 || abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50)) {
            if (abs(this->geometry().width() - deskrect.width() / 2) < 50) {
                qWarning() << "Cannot enter mini mode: Window is docked to screen edge";
                m_pCommHintWid->updateWithMessage(tr("Please exit smart dock"));
                m_bStartMini = false;
                reflectActionToUI(ActionFactory::ToggleMiniMode);
                return;
            }
        }
    }

    m_bMiniMode = !m_bMiniMode;
    qInfo() << "Mini mode changed to:" << m_bMiniMode;
    m_isSettingMiniMode = true;
    m_pEngine->toggleRoundedClip(!m_bMiniMode);

    if (utils::check_wayland_env()) {
        qDebug() << "utils::check_wayland_env()";
        // 在拖拽进度等操作，高占用播放时，使用右键菜单可能使 wayland 未能正确切换窗体，导致 surface destroy ，程序闪退
        // 在执行 show 操作前，主动创建上下文，生成 surface 以正常填入数据。
        m_pEngine->makeCurrent();

        Qt::WindowFlags flags = windowFlags();
        if (m_bMiniMode) {
            qDebug() << "m_bMiniMode";
            // 记录之前的状态，尝试saveGeometry()也存在其它问题。
            m_waylandRectInNormalMode = normalGeometry();

            flags |= Qt::X11BypassWindowManagerHint;
            m_preMiniWindowState = windowState();
            setWindowFlags(flags);
            showNormal();
        } else {
            qDebug() << "!m_bMiniMode";
            flags &= ~Qt::X11BypassWindowManagerHint;
            setWindowFlags(flags);

            // 触发更新 windowState() 时可能更新 normalGeometry() 在设置最大化/全屏前还原之前的 normalGeometry() 信息
            setMinimumSize(614, 500);
            setMaximumSize(QSize(QWIDGETSIZE_MAX-1, QWIDGETSIZE_MAX-1));
            // 还原为原始窗口大小，迷你模式前为最大化或/全屏时，m_lastRectInNormalMode记录的是最大化/全屏的窗口大小
            setGeometry(m_waylandRectInNormalMode);
            show();

           if (m_preMiniWindowState == Qt::WindowMaximized) {
               qDebug() << "m_preMiniWindowState == Qt::WindowMaximized";
               // 使用迷你模式前记录的坐标，在多屏中显示正确位置
               move(m_lastRectInNormalMode.topLeft());
               showMaximized();
               setGeometry(m_lastRectInNormalMode);
           } else if (m_preMiniWindowState & Qt::WindowFullScreen) {
               qDebug() << "m_preMiniWindowState & Qt::WindowFullScreen";
               move(m_lastRectInNormalMode.topLeft());
               showFullScreen();
           } else {
               qDebug() << "else";
               showNormal();
           }

           // 复原原始控件记录大小
           qDebug() << "m_lastRectInNormalMode = m_waylandRectInNormalMode";
           m_lastRectInNormalMode = m_waylandRectInNormalMode;
        }
    }
    qDebug() << "m_isSettingMiniMode = false";
    m_isSettingMiniMode = false;

    qDebug() << __func__ << m_bMiniMode;

    if (m_bMiniMode) {
        qDebug() << "m_bMiniMode";
        m_pTitlebar->titlebar()->setDisableFlags(Qt::WindowMaximizeButtonHint);
    } else {
        qDebug() << "!m_bMiniMode";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_pTitlebar->titlebar()->setDisableFlags(nullptr);
#else
        m_pTitlebar->titlebar()->setDisableFlags(Qt::WindowFlags());
#endif
    }

    qDebug() << "m_pTitlebar->setVisible(!m_bMiniMode)";
    m_pTitlebar->setVisible(!m_bMiniMode);

    qDebug() << "m_pMiniPlayBtn->setVisible(m_bMiniMode)";
    m_pMiniPlayBtn->setVisible(m_bMiniMode);
    qDebug() << "m_pMiniCloseBtn->setVisible(m_bMiniMode)";
    m_pMiniCloseBtn->setVisible(m_bMiniMode);
    m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);

    m_pMiniPlayBtn->setEnabled(m_bMiniMode);
    m_pMiniCloseBtn->setEnabled(m_bMiniMode);
    m_pMiniQuitMiniBtn->setEnabled(m_bMiniMode);

    m_pMiniPlayBtn->raise();
    m_pMiniCloseBtn->raise();
    m_pMiniQuitMiniBtn->raise();

    resumeToolsWindow();

    if (m_bMiniMode) {
        qDebug() << "m_bMiniMode";
        m_pCommHintWid->setAnchorPoint(QPoint(15, 11));    //迷你模式下提示位置稍有不同
        updateSizeConstraints();
        //设置等比缩放
        setEnableSystemResize(false);
        //m_nStateBeforeMiniMode = SBEM_None;

        if (isFullScreen()) {
            qDebug() << "isFullScreen()";
            m_nStateBeforeMiniMode |= SBEM_Fullscreen;
            setWindowState(windowState() & ~Qt::WindowFullScreen);
            this->setWindowState(Qt::WindowNoState);
            setFocus();
            if (m_pFullScreenTimeLable) {
                m_pFullScreenTimeLable->close();
            }
            if (utils::check_wayland_env()) {
                m_pToolbox->updateFullState();
            }
        } else {
            qDebug() << "else Max";
            m_lastRectInNormalMode = geometry();
        }

        if (!m_bWindowAbove) {
            qDebug() << "!m_bWindowAbove";
            m_nStateBeforeMiniMode |= SBEM_Above;
            requestAction(ActionFactory::WindowAbove);
        }

        QSize sz = QSize(380, 380);
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qreal ratio = 1920 * 1.0 / 1080;
            auto vid_size = m_pEngine->videoSize();

            if (vid_size.height() > 0 && vid_size.width() >= vid_size.height()) {
                ratio = vid_size.width() / static_cast<qreal>(vid_size.height());
                sz = QSize(380, static_cast<int>(380 / ratio) + 1);
            } else if (vid_size.height() > 0 && vid_size.width() < vid_size.height()) {
                ratio = vid_size.width() / static_cast<qreal>(vid_size.height());
                sz = QSize(380, static_cast<int>(380 * ratio) + 1);
            } else {
                sz = QSize(380, static_cast<int>(380 / ratio) + 1);
            }
        }

        setFixedSize(sz);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QRect deskGeom = QGuiApplication::primaryScreen()->availableGeometry();
#else
        QRect deskGeom = QApplication::desktop()->availableGeometry(this);
#endif
        move(deskGeom.x() + (deskGeom.width() - sz.width()) / 2, deskGeom.y() + (deskGeom.height() - sz.height()) / 2); //迷你模式下窗口居中 by zhuyuliang

        m_pMiniPlayBtn->move(sz.width() - 12 - m_pMiniPlayBtn->width(),
                             sz.height() - 10 - m_pMiniPlayBtn->height());
        m_pMiniCloseBtn->move(sz.width() - 15 - m_pMiniCloseBtn->width(), 10);
        m_pMiniQuitMiniBtn->move(14, sz.height() - 10 - m_pMiniQuitMiniBtn->height());
    } else {
        qDebug() << "!m_bMiniMode";
        m_pCommHintWid->setAnchorPoint(QPoint(30, 58));
        QRect tmp = m_lastRectInNormalMode;
        this->setMinimumSize(614, 500);
        this->setMaximumSize(QSize(QWIDGETSIZE_MAX-1, QWIDGETSIZE_MAX-1));
        m_lastRectInNormalMode = tmp;
        setEnableSystemResize(true);
        if (m_nStateBeforeMiniMode & SBEM_Maximized) {
            qDebug() << "m_nStateBeforeMiniMode & SBEM_Maximized";
            //迷你模式切换最大化时，先恢复原来窗口大小
            if (m_lastRectInNormalMode.isValid()) {
                setGeometry(m_lastRectInNormalMode);
            } else {
                resizeByConstraints();
            }
            // 由于时序问题，延迟最大化
            QTimer::singleShot(100, [&] {
                showMaximized();
            });
        } else if (m_nStateBeforeMiniMode & SBEM_Fullscreen) {
            qDebug() << "m_nStateBeforeMiniMode & SBEM_Fullscreen";
            setWindowState(windowState() | Qt::WindowFullScreen);
            if (CompositingManager::get().platform() == Platform::Arm64 || CompositingManager::get().platform() == Platform::Alpha) {
                if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
                    int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    QRect deskRect = QApplication::desktop()->availableGeometry();
#else
                    QRect deskRect = qApp->primaryScreen()->availableGeometry();
#endif
                    pixelsWidth = qMax(117, pixelsWidth);
                    m_pFullScreenTimeLable->setGeometry(deskRect.width() - pixelsWidth - 60, 40, pixelsWidth + 60, 36);
                    m_pFullScreenTimeLable->show();
                }
            }
            setFocus();
            if (utils::check_wayland_env()) {
                qDebug() << "utils::check_wayland_env()";
                m_pToolbox->updateFullState();
            }
        } else {
            qDebug() << "else Full";
            if (m_pToolbox->listBtn()->isChecked()) {
                m_pToolbox->listBtn()->setChecked(false);
            }

            // Wayland流程区分处理
            if (!utils::check_wayland_env()) {
                qDebug() << "!utils::check_wayland_env()";
                if (m_lastRectInNormalMode.isValid()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                    QRect deskRect = QApplication::desktop()->availableGeometry(m_lastRectInNormalMode.topLeft());
#else
                    QRect deskRect = qApp->primaryScreen()->availableGeometry();
#endif
                    if(m_lastRectInNormalMode.intersects(deskRect)) {
                        qDebug() << "m_lastRectInNormalMode.intersects(deskRect)";
                        setGeometry(m_lastRectInNormalMode);
                    } else {
                        setGeometry(QRect(deskRect.x(), deskRect.y(), m_lastRectInNormalMode.width(), m_lastRectInNormalMode.height()));
                    }
                } else {
                    qDebug() << "!m_lastRectInNormalMode.isValid()";
                    resizeByConstraints();
                }
            }
        }

        if (m_nStateBeforeMiniMode & SBEM_Above) {
            qDebug() << "m_nStateBeforeMiniMode & SBEM_Above";
            requestAction(ActionFactory::WindowAbove);
        }

        if (m_nStateBeforeMiniMode & SBEM_PlaylistOpened &&
                m_pPlaylist->state() == PlaylistWidget::Closed) {
            if (m_nStateBeforeMiniMode & SBEM_Fullscreen) {
                QTimer::singleShot(100, [ = ]() {
                    qDebug() << "QTimer::singleShot(100, [ = ]())";
                    requestAction(ActionFactory::TogglePlaylist);
                });
            } else {
                qDebug() << "!m_nStateBeforeMiniMode & SBEM_Fullscreen";
            }
        }
        m_nStateBeforeMiniMode = SBEM_None;
    }

    m_bStartMini = false;
}

void MainWindow::miniButtonClicked(const QString &id)
{
    qDebug() << "miniButtonClicked";
    if (id == "play") {
        qDebug() << "id == play";
        if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
            requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            requestAction(ActionFactory::ActionKind::TogglePause);
        }

    } else if (id == "close") {
        qDebug() << "id == close";
        close();

    } else if (id == "quit_mini") {
        qDebug() << "id == quit_mini";
        requestAction(ActionFactory::ActionKind::ToggleMiniMode);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    qDebug() << "dragEnterEvent";
    if (ev->mimeData()->hasUrls()) {
        qDebug() << "ev->mimeData()->hasUrls()";
        ev->acceptProposedAction();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    qDebug() << "dragMoveEvent";
    if (ev->mimeData()->hasUrls()) {
        qDebug() << "ev->mimeData()->hasUrls()";
        ev->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *pEvent)
{
    qInfo() << "Drop event received, formats:" << pEvent->mimeData()->formats();
    
    if (!pEvent->mimeData()->hasUrls()) {
        qInfo() << "Drop event ignored: No URLs in mime data";
        return;
    }

    QList<QString> lstFile;
    QList<QUrl> urls = pEvent->mimeData()->urls();

    for (QUrl strUrl : urls) {
        lstFile << strUrl.path();
    }

    if (urls.count() == 1) {
        // check if the dropped file is a subtitle.
        QFileInfo fileInfo(urls.first().toLocalFile());
        if (m_pEngine->isSubtitle(fileInfo.absoluteFilePath())) {
            qInfo() << "Dropped file is a subtitle:" << fileInfo.absoluteFilePath();
            
            if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                    && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
                qInfo() << "Subtitle loading skipped: Playing raw format";
                return;
            }
            else if (m_pEngine->state() == PlayerEngine::Idle) {
                qInfo() << "Attempting to match subtitle with video";
                subtitleMatchVideo(urls.first().toLocalFile());
            }
            else {
                qInfo() << "Loading subtitle for current video";
                bool succ = m_pEngine->loadSubtitle(fileInfo);
                m_pCommHintWid->updateWithMessage(succ ? tr("Load successfully") : tr("Load failed"));
            }
            return;
        }
    }

    qInfo() << "Playing dropped files:" << lstFile;
    play(lstFile);

    pEvent->acceptProposedAction();
}

void MainWindow::setInit(bool bInit)
{
    qDebug() << "setInit" << bInit;
    if (m_bInited != bInit) {
        qDebug() << "m_bInited != bInit";
        m_bInited = bInit;
        emit initChanged();
    }
}

QString MainWindow::probeCdromDevice()
{
    qDebug() << "probeCdromDevice";
    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        qDebug() << "mountFile.open(QIODevice::ReadOnly) == false";
        return QString();
    }
    do {
        QString sLine = mountFile.readLine();
        if (sLine.indexOf("/dev/sr") != -1 || sLine.indexOf("/dev/cdrom") != -1) {  //说明存在光盘的挂载。
            return sLine.split(" ").at(0);        //A B C 这样的格式，取部分
        }
    } while (!mountFile.atEnd());
    mountFile.close();
    qDebug() << "mountFile.close()";
    return QString();
}

void MainWindow::diskRemoved(QString strDiskName)
{
    qDebug() << "diskRemoved";
    QString sCurrFile;
    if (m_pEngine->getplaylist()->count() <= 0) {
        qDebug() << "m_pEngine->getplaylist()->count() <= 0";
        return;
    }
    sCurrFile = m_pEngine->getplaylist()->currentInfo().url.toString();

    if (sCurrFile.contains(strDiskName)/* && m_pEngine->state() == PlayerEngine::Playing*/) {
        qDebug() << "sCurrFile.contains(strDiskName)";
        m_pCommHintWid->updateWithMessage(tr("The CD/DVD has been ejected"));
    }
}

void MainWindow::sleepStateChanged(bool bSleep)
{
    qInfo() << __func__ << bSleep;

    //if (m_bStateInLock) {                //休眠唤醒后会先执行锁屏操作,如果已经进行锁屏操作则忽略休眠唤醒信号
     //   m_bStartSleep = bSleep;
     //   m_pEngine->seekAbsolute(static_cast<int>(m_pEngine->elapsed()));
    //    return;
    //}
    //休眠退出投屏
    if(bSleep && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) {
        qDebug() << "bSleep && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
        slotExitMircast();
    }
    if (bSleep && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qDebug() << "bSleep && m_pEngine->state() == PlayerEngine::CoreState::Playing";
        m_bStartSleep = true;
        //requestAction(ActionFactory::ActionKind::TogglePause);
    } else if (!bSleep && m_pEngine->state() == PlayerEngine::CoreState::Paused) {
        qDebug() << "!bSleep && m_pEngine->state() == PlayerEngine::CoreState::Paused";
        m_bStartSleep = false;
        m_pEngine->seekAbsolute(static_cast<int>(m_pEngine->elapsed()));      //保证休眠后不管是否播放都不会卡帧
    }
}

void MainWindow::lockStateChanged(bool bLock)
{
    qInfo() << __func__ << bLock;
    //锁屏退出投屏
    if(bLock && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) {
        qDebug() << "bLock && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
        slotExitMircast();
    }
    if (bLock && m_pEngine->state() == PlayerEngine::CoreState::Playing && !m_bStateInLock) {
        qDebug() << "bLock && m_pEngine->state() == PlayerEngine::CoreState::Playing && !m_bStateInLock";
        m_bStateInLock = true;
        requestAction(ActionFactory::ActionKind::TogglePause);
    } else if (!bLock && m_pEngine->state() == PlayerEngine::CoreState::Paused && m_bStateInLock) {
        qDebug() << "!bLock && m_pEngine->state() == PlayerEngine::CoreState::Paused && m_bStateInLock";
        m_bStateInLock = false;
        QTimer::singleShot(500, [=](){
            //龙芯5000使用命令sudo rtcwake -l -m mem -s 20, 待机唤醒后无dBus信号"PrepareForSleep"发出,加入seek保证解锁后播放不会卡帧
            m_pEngine->seekAbsolute(static_cast<int>(m_pEngine->elapsed()));
            requestAction(ActionFactory::ActionKind::TogglePause);
        });
    }
}

void MainWindow::initMember()
{
    qDebug() << "initMember";
    m_pPopupWid = nullptr;
    m_pFullScreenTimeLable = nullptr;             //全屏时右上角的影片进度
    m_pFullScreenTimeLayout = nullptr;
    m_pTitlebar = nullptr;
    m_pToolbox = nullptr;
    m_pPlaylist = nullptr;
    m_pEngine = nullptr;
    m_pAnimationlable = nullptr;
    m_pProgIndicator = nullptr;   //全屏时右上角的系统时间
    m_pEventMonitor = nullptr;
    m_pEventListener = nullptr;
    m_pDVDHintWid = nullptr;
    m_pCommHintWid = nullptr;
    m_pShortcutViewProcess = nullptr;
    m_pDBus = nullptr;
    m_pPresenter = nullptr;
    m_pMovieWidget = nullptr;
    m_pMircastShowWidget = nullptr;
    m_bInBurstShootMode = false;
    m_bPausedBeforeBurst = false;

#ifdef __mips__
    m_pMiniPlayBtn = nullptr;
    m_pMiniCloseBtn = nullptr;
    m_pMiniQuitMiniBtn = nullptr;
#else
    m_pMiniPlayBtn = nullptr;
    m_pMiniCloseBtn = nullptr;
    m_pMiniQuitMiniBtn = nullptr;
#endif

    m_bMiniMode = false;
    m_bInited = false;
    m_bMovieSwitchedInFsOrMaxed = false;
    m_bDelayedResizeByConstraint = false;
    m_bWindowAbove = false;
    m_bMouseMoved = false;
    m_bMousePressed = false;
    m_bQuitfullscreenflag = false;
    m_bStartMini = false;
    m_bProgressChanged = false;
    m_bLastIsTouch = false;
    m_bTouchChangeVolume = false;
    m_bIsFree = true;
    m_bIsTouch = false;
    m_bStartAnimation = false;
    m_bStateInLock = false;
    m_bStartSleep = false;
    m_bMaximized = false;
    m_bHaveFile = false;

    m_nDisplayVolume = 100;
    m_nLastPressX = 0;
    m_nLastPressY = 0;
    m_nStateBeforeMiniMode = 0;
    m_nLastCookie = 0;
    m_nPowerCookie = 0;
    m_dPlaySpeed = 1.0;
    m_iAngleDelta = 0;
    m_nFullscreenTime = 0;

    m_lastWindowState = Qt::WindowNoState;

    m_dvdUrl.clear();
    m_listOpenFiles.clear();
    m_sCurrentHwdec.clear();
    m_listBurstShoots.clear();
    qDebug() << "initMember end";
}

void MainWindow::adjustPlaybackSpeed(ActionFactory::ActionKind actionKind)
{
    qDebug() << "adjustPlaybackSpeed";
    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qDebug() << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
        if (actionKind == ActionFactory::ActionKind::AccelPlayback) {
            qDebug() << "actionKind == ActionFactory::ActionKind::AccelPlayback";
            m_dPlaySpeed = qMin(2.0, m_dPlaySpeed + 0.1);
        } else if (actionKind == ActionFactory::ActionKind::DecelPlayback) {
            qDebug() << "actionKind == ActionFactory::ActionKind::DecelPlayback";
            m_dPlaySpeed = qMax(0.1, m_dPlaySpeed - 0.1);
        }

        m_pEngine->setPlaySpeed(m_dPlaySpeed);
        if (qFuzzyCompare(0.5, m_dPlaySpeed)) {
            qDebug() << "qFuzzyCompare(0.5, m_dPlaySpeed)";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::ZeroPointFiveTimes);
        } else if (qFuzzyCompare(1.0, m_dPlaySpeed)) {
            qDebug() << "qFuzzyCompare(1.0, m_dPlaySpeed)";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
        } else if (qFuzzyCompare(1.2, m_dPlaySpeed)) {
            qDebug() << "qFuzzyCompare(1.2, m_dPlaySpeed)";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointTwoTimes);
        } else if (qFuzzyCompare(1.5, m_dPlaySpeed)) {
            qDebug() << "qFuzzyCompare(1.5, m_dPlaySpeed)";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointFiveTimes);
        } else if (qFuzzyCompare(2.0, m_dPlaySpeed)) {
            qDebug() << "qFuzzyCompare(2.0, m_dPlaySpeed)";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::Double);
        } else {
            qDebug() << "setPlaySpeedMenuUnchecked";
            setPlaySpeedMenuUnchecked();
        }
        m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
    }
}

void MainWindow::setPlaySpeedMenuChecked(ActionFactory::ActionKind actionKind)
{
    qDebug() << "setPlaySpeedMenuChecked";
    QList<QAction *> listActs = ActionFactory::get().findActionsByKind(actionKind);
    auto p = listActs.begin();
    (*p)->setChecked(true);
}

void MainWindow::setPlaySpeedMenuUnchecked()
{
    qDebug() << "setPlaySpeedMenuUnchecked";
    QList<QAction *> listActs;
    {
        listActs = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::ZeroPointFiveTimes);
        auto p = listActs.begin();
        if ((*p)->isChecked()) {
            (*p)->setChecked(false);
        }
    }
    {
        listActs = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OneTimes);
        auto p = listActs.begin();
        if ((*p)->isChecked()) {
            (*p)->setChecked(false);
        }
    }
    {
        listActs = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OnePointTwoTimes);
        auto p = listActs.begin();
        if ((*p)->isChecked()) {
            (*p)->setChecked(false);
        }
    }
    {
        listActs = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::OnePointFiveTimes);
        auto p = listActs.begin();
        if ((*p)->isChecked()) {
            (*p)->setChecked(false);
        }
    }
    {
        listActs = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::Double);
        auto p = listActs.begin();
        if ((*p)->isChecked()) {
            (*p)->setChecked(false);
        }
    }

}

void MainWindow::setMusicShortKeyState(bool bState)
{
    qDebug() << "setMusicShortKeyState";
    ActionFactory::ActionKind actionKind;
    foreach (auto action, this->actions()) {
        actionKind = (ActionFactory::ActionKind)action->property("kind").toInt();
        switch (actionKind) {
        case ActionFactory::Screenshot:
        case ActionFactory::BurstScreenshot:
        case ActionFactory::GoToScreenshotSolder:
        case ActionFactory::DefaultFrame:
        case ActionFactory::Ratio4x3Frame:
        case ActionFactory::Ratio16x9Frame:
        case ActionFactory::Ratio16x10Frame:
        case ActionFactory::Ratio185x1Frame:
        case ActionFactory::Ratio235x1Frame:
        case ActionFactory::ClockwiseFrame:
        case ActionFactory::CounterclockwiseFrame:
        case ActionFactory::NextFrame:
        case ActionFactory::PreviousFrame:
            action->setEnabled(bState);
        }
    }
}

void MainWindow::onSysLockState(QString, QVariantMap key2value, QStringList)
{
    qDebug() << "onSysLockState";
    if (m_bStartSleep) {
        m_bStateInLock = true;       //如果进入了休眠状态后进入锁屏,则默认执行了暂停操作
    }

    if (key2value.value("Locked").value<bool>() && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        m_bStateInLock = true;
        requestAction(ActionFactory::TogglePause);
    } else if (!key2value.value("Locked").value<bool>() && m_bStateInLock) {
        m_bStateInLock = false;
        requestAction(ActionFactory::TogglePause);
    }
}

void MainWindow::slotProperChanged(QString, QVariantMap key2value, QStringList)
{
    qInfo() << __func__ << key2value;
    if (key2value.value("Active").value<bool>() && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        m_pEngine->seekAbsolute(m_pEngine->elapsed());
    }
}

void MainWindow::slotUnsupported()
{
    qDebug() << "slotUnsupported";
    m_pCommHintWid->updateWithMessage(tr("The action is not supported in this video"));
}

void MainWindow::slotInvalidFile(QString strFileName)
{
    qDebug() << "slotInvalidFile";
    static int showTime = -1000;

    showTime += 1000;

    QTimer::singleShot(showTime, [=]{
       showTime = showTime - 1000;
       m_pCommHintWid->updateWithMessage(QString(tr("Invalid file: %1").arg(strFileName)));
    });
}

void MainWindow::slotUpdateMircastState(int state, QString msg)
{
    qDebug() << "slotUpdateMircastState";
    switch (state) {
    case MIRCAST_SUCCEEDED: //投屏成功
    {
        qDebug() << "MIRCAST_SUCCESSED";
        mircastSuccess(msg);
        emit frameMenuEnable(false);
        emit playSpeedMenuEnable(false);
        emit subtitleMenuEnable(false);
        emit soundMenuEnable(false);
    }
        break;
    case MIRCAST_EXIT://投屏退出
    {
        qDebug() << "MIRCAST_EXIT";
        slotExitMircast();
    }
        break;
    case MIRCAST_CONNECTION_FAILED://投屏连接失败
    {
        qDebug() << "MIRCAST_CONNECTION_FAILED";
        const QIcon icon = QIcon(":/resources/icons/short_fail.svg");
        QString sText = QString(tr("Connection failed"));
        popupAdapter(icon, sText);
        slotExitMircast();
    }
        break;
    case MIRCAST_DISCONNECTED://投屏丢失连接
    {
        qDebug() << "MIRCAST_DISCONNECTIONED";
        m_pCommHintWid->updateWithMessage(tr("Miracast disconnected"));
        slotExitMircast();
    }
        break;
    default:
        break;
    }
}

void MainWindow::slotExitMircast()
{
    qDebug() << "slotExitMircast";
    exitMircast();
    emit frameMenuEnable(true);
    emit playSpeedMenuEnable(true);
    emit subtitleMenuEnable(true);
    emit soundMenuEnable(true);
}

void MainWindow::updateGeometry(CornerEdge edge, QPoint pos)
{
    qDebug() << "updateGeometry";
    bool bKeepRatio = engine()->state() != PlayerEngine::CoreState::Idle;
    QRect oldGeom = frameGeometry();
    QRect geom = frameGeometry();
    qreal ratio = static_cast<qreal>(geom.width()) / geom.height();

    // disable edges
    switch (edge) {
    case CornerEdge::BottomEdge:
    case CornerEdge::TopEdge:
    case CornerEdge::LeftEdge:
    case CornerEdge::RightEdge:
    case CornerEdge::NoneEdge:
        qDebug() << "return";
        return;
    default:
        break;
    }

    if (bKeepRatio) {
        qDebug() << "bKeepRatio";
        QSize size = engine()->videoSize();
        if (size.isEmpty()) {
            const auto &MovieInfo = engine()->playlist().currentInfo().mi;
            size = QSize(MovieInfo.width, MovieInfo.height);
        }

        ratio = size.width() / static_cast<qreal>(size.height());
        switch (edge) {
        case CornerEdge::TopLeftCorner:
            qDebug() << "CornerEdge::TopLeftCorner";
            geom.setLeft(pos.x());
            geom.setTop(static_cast<int>(geom.bottom() - geom.width() / ratio));
            break;
        case CornerEdge::BottomLeftCorner:
        case CornerEdge::LeftEdge:
            qDebug() << "CornerEdge::LeftEdge";
            geom.setLeft(pos.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case CornerEdge::BottomRightCorner:
        case CornerEdge::RightEdge:
            qDebug() << "CornerEdge::RightEdge";
            geom.setRight(pos.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case CornerEdge::TopRightCorner:
        case CornerEdge::TopEdge:
            qDebug() << "CornerEdge::TopEdge";
            geom.setTop(pos.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        case CornerEdge::BottomEdge:
            qDebug() << "CornerEdge::BottomEdge";
            geom.setBottom(pos.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        default:
            break;
        }
    } else {
        switch (edge) {
        case CornerEdge::BottomLeftCorner:
            qDebug() << "CornerEdge::BottomLeftCorner";
            geom.setBottomLeft(pos);
            break;
        case CornerEdge::TopLeftCorner:
            qDebug() << "CornerEdge::TopLeftCorner";
            geom.setTopLeft(pos);
            break;
        case CornerEdge::LeftEdge:
            qDebug() << "CornerEdge::LeftEdge";
            geom.setLeft(pos.x());
            break;
        case CornerEdge::BottomRightCorner:
            qDebug() << "CornerEdge::BottomRightCorner";
            geom.setBottomRight(pos);
            break;
        case CornerEdge::RightEdge:
            qDebug() << "CornerEdge::RightEdge";
            geom.setRight(pos.x());
            break;
        case CornerEdge::TopRightCorner:
            qDebug() << "CornerEdge::TopRightCorner";
            geom.setTopRight(pos);
            break;
        case CornerEdge::TopEdge:
            qDebug() << "CornerEdge::TopEdge";
            geom.setTop(pos.y());
            break;
        case CornerEdge::BottomEdge:
            qDebug() << "CornerEdge::BottomEdge";
            geom.setBottom(pos.y());
            break;
        default:
            break;
        }
    }

    qDebug() << "QSize min = minimumSize()";
    QSize min = minimumSize();
    if (oldGeom.width() <= min.width() && geom.left() > oldGeom.left()) {
        qDebug() << "oldGeom.width() <= min.width() && geom.left() > oldGeom.left()";
        geom.setLeft(oldGeom.left());
    }
    if (oldGeom.height() <= min.height() && geom.top() > oldGeom.top()) {
        qDebug() << "oldGeom.height() <= min.height() && geom.top() > oldGeom.top()";
        geom.setTop(oldGeom.top());
    }

    geom.setWidth(qMax(geom.width(), min.width()));
    geom.setHeight(qMax(geom.height(), min.height()));
    updateContentGeometry(geom);
    updateGeometryNotification(geom.size());
}

void MainWindow::setPresenter(Presenter *pPresenter)
{
    qDebug() << "setPresenter";
    m_pPresenter = pPresenter;
    m_pPresenter->slotvolumeChanged();
}

int MainWindow::getDisplayVolume()
{
    qDebug() << "getDisplayVolume";
    return m_nDisplayVolume;
}

bool MainWindow::getMiniMode()
{
    qDebug() << "getMiniMode";
    return m_bMiniMode;
}

MainWindow::~MainWindow()
{
    qDebug() << "~MainWindow";
    //Do not enter CloseEvent when exiting from the title bar menu, so add the save function here
    //powered by xxxxp
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        qDebug() << "Settings::get().isSet(Settings::ResumeFromLast)";
        int nCur = 0;
        nCur = m_pEngine->playlist().current();
        if (nCur >= 0) {
            Settings::get().setInternalOption("playlist_pos", nCur);
        }
    }
    m_pEngine->savePlaybackPosition();
    if (m_pEventListener) {
        qDebug() << "m_pEventListener";
        this->windowHandle()->removeEventFilter(m_pEventListener);
        delete m_pEventListener;
        m_pEventListener = nullptr;
    }

    if (!utils::check_wayland_env()) {
        qDebug() << "!utils::check_wayland_env()";
        disconnect(m_pEngine, 0, 0, 0);
        disconnect(&m_pEngine->playlist(), 0, 0, 0);
    }

    if (m_nLastCookie > 0) {
        qDebug() << "m_nLastCookie > 0";
        utils::UnInhibitStandby(m_nLastCookie);
        qDebug() << "uninhibit cookie" << m_nLastCookie;
        m_nLastCookie = 0;
    }
    if (m_nPowerCookie > 0) {
        qDebug() << "m_nPowerCookie > 0";
        utils::UnInhibitPower(m_nPowerCookie);
        m_nPowerCookie = 0;
    }
    qDebug() << "delete m_pEngine";
    delete m_pEngine;
    m_pEngine = nullptr;

    m_diskCheckThread.stop();

    ThreadPool::instance()->quitAll();

#ifdef USE_DXCB
    if (_evm) {
        disconnect(_evm, 0, 0, 0);
        delete _evm;
    }
#endif

    if (m_pShortcutViewProcess) {
        qDebug() << "m_pShortcutViewProcess";
        m_pShortcutViewProcess->deleteLater();
        m_pShortcutViewProcess = nullptr;
    }

    qDebug() << "delete m_backgroundWidget";
    delete m_backgroundWidget;
    m_backgroundWidget = nullptr;
}
#include "mainwindow.moc"
