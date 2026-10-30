// Copyright (C) 2020 ~ 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ddlog.h"

#include "platform_mainwindow.h"
#include "platform/platform_toolbox_proxy.h"
#include "actions.h"
#include "event_monitor.h"
#include "shortcut_manager.h"
#include "dmr_settings.h"
#include "movieinfo_dialog.h"
#include "burst_screenshots_dialog.h"
#include "platform/platform_playlist_widget.h"
#include "platform/platform_notification_widget.h"
#include "player_engine.h"
#include "url_dialog.h"
#include "platform/platform_movie_progress_indicator.h"
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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#else
#include <QGuiApplication>
#include <QScreen>
#endif

//#include <QtWidgets>
#include <QtDBus>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QX11Info>
#include <QtX11Extras>
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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <DImageButton>
#else
#include <QPushButton>  // 使用 QPushButton 作为替代
#endif

#include <DWidgetUtil>
#include <DSettingsWidgetFactory>
#include <DLineEdit>
#include <DFileDialog>
#include <DWindowManagerHelper>
#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#include "moviewidget.h"
#include <QJsonObject>
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

int Platform_MainWindow::m_platform_nRetryTimes = 0;

static void workaround_updateStyle(QWidget *pParent, const QString &sTheme)
{
    qCDebug(appMovie) << "Entering workaround_updateStyle function";
    pParent->setStyle(QStyleFactory::create(sTheme));
    for (auto obj : pParent->children()) {
        QWidget *pWidget = qobject_cast<QWidget *>(obj);
        if (pWidget) {
            qCDebug(appMovie) << "Entering workaround_updateStyle function for child widget";
            workaround_updateStyle(pWidget, sTheme);
        }
    }
    qCDebug(appMovie) << "Exiting workaround_updateStyle function";
}

static QString ElideText(const QString &sText, const QSize &size,
                         QTextOption::WrapMode wordWrap, const QFont &font,
                         Qt::TextElideMode mode, int nLineHeight, int nLastLineWidth)
{
    qCDebug(appMovie) << "Entering ElideText function";
    int nHeight = 0;

    QTextLayout textLayout(sText);
    QString sElideText = nullptr;
    QFontMetrics fontMetrics(font);

    textLayout.setFont(font);
    const_cast<QTextOption *>(&textLayout.textOption())->setWrapMode(wordWrap);

    textLayout.beginLayout();

    QTextLine line = textLayout.createLine();

    while (line.isValid()) {
        nHeight += nLineHeight;

        if (nHeight + nLineHeight >= size.height()) {
            sElideText += fontMetrics.elidedText(sText.mid(line.textStart() + line.textLength() + 1),
                                                 mode, nLastLineWidth);
            qCDebug(appMovie) << "nHeight + nLineHeight >= size.height() break";
            break;
        }

        line.setLineWidth(size.width());
        const QString &sTmpText = sText.mid(line.textStart(), line.textLength());

        if (sTmpText.indexOf('\n')) {
            nHeight += nLineHeight;
            qCDebug(appMovie) << "sTmpText.indexOf('\n') nHeight += nLineHeight";
        }

        sElideText += sTmpText;
        line = textLayout.createLine();

        if (line.isValid()) {
            sElideText.append("\n");
            qCDebug(appMovie) << "line.isValid() sElideText.append(\"\n\")";
        }
    }

    textLayout.endLayout();

    if (textLayout.lineCount() == 1) {
        sElideText = fontMetrics.elidedText(sElideText, mode, nLastLineWidth);
        qCDebug(appMovie) << "textLayout.lineCount() == 1 sElideText = fontMetrics.elidedText(sElideText, mode, nLastLineWidth)";
    }
    qCDebug(appMovie) << "Exiting ElideText function";
    return sElideText;
}

static QWidget *createDecodeOptionHandle(QObject *pObj)
{
    qCDebug(appMovie) << "Entering createDecodeOptionHandle function";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mianWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mianWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    pLayout->setContentsMargins(0, 0, 0, 0);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("decodeOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mianWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mianWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value) {
        if (dataType == "items") {
            combobox->clear();
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &) {
        pSettingOption->setValue(combobox->currentIndex());
    });

    qCDebug(appMovie) << "Exiting createDecodeOptionHandle function";
    return pOptionWidget;
}

static QWidget *createVoOptionHandle(QObject *pObj)
{
    qCDebug(appMovie) << "Entering createVoOptionHandle function";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mianWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mianWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    pLayout->setContentsMargins(0, 0, 0, 0);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("videoOutOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mianWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mianWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value) {
        if (dataType == "items") {
            combobox->clear();
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &) {
        pSettingOption->setValue(combobox->currentIndex());
    });

    pSettingOption->connect(pSettingOption, &DSettingsOption::valueChanged, [=](QVariant value) {
        combobox->setCurrentIndex(value.toInt());
    });

    qCDebug(appMovie) << "Exiting createVoOptionHandle function";
    return pOptionWidget;
}

static QWidget *createEffectOptionHandle(QObject *pObj)
{
    qCDebug(appMovie) << "Entering createEffectOptionHandle function";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);
    QWidget *mianWidget = new QWidget;
    QComboBox *combobox = new QComboBox;
    QHBoxLayout *pLayout = new QHBoxLayout;

    combobox->addItems(pSettingOption->data("items").toStringList());
    mianWidget->setLayout(pLayout);
    pLayout->addStretch();
    pLayout->addWidget(combobox);
    pLayout->setContentsMargins(0, 0, 0, 0);
    combobox->setFixedWidth(245);
    combobox->setCurrentIndex(pSettingOption->value().toInt());

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("effectOptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    mianWidget->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), mianWidget);

    pSettingOption->connect(pSettingOption, &DSettingsOption::dataChanged, [=](const QString &dataType, QVariant value) {
        if (dataType == "items") {
            combobox->addItems(value.toStringList());
        }
    });

    pSettingOption->connect(combobox, &QComboBox::currentTextChanged, [=](const QString &) {
        pSettingOption->setValue(combobox->currentIndex());
    });

    pSettingOption->connect(pSettingOption, &DSettingsOption::valueChanged, [=](QVariant value) {
        combobox->setCurrentIndex(value.toInt());
    });

    qCDebug(appMovie) << "Exiting createEffectOptionHandle function";
    return pOptionWidget;
}

static QWidget *createSelectableLineEditOptionHandle(QObject *pObj)
{
    qCDebug(appMovie) << "Entering createSelectableLineEditOptionHandle function";
    DSettingsOption *pSettingOption = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(pObj);

    DLineEdit *pLineEdit = new DLineEdit();
    DWidget *pMainWid = new DWidget;
    QHBoxLayout *pLayout = new QHBoxLayout;

    static QString sNameLast = nullptr;

    pMainWid->setLayout(pLayout);
    DIconButton *pIconButton = new DIconButton(nullptr);
    pIconButton->setIcon(DStyle::SP_SelectElement);

    pLineEdit->setFixedHeight(21);
    pLineEdit->setObjectName("OptionSelectableLineEdit");
    pLineEdit->setText(pSettingOption->value().toString());
    QFontMetrics fontMetrics = pLineEdit->fontMetrics();
    QString sElideText = ElideText(pLineEdit->text(), {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                                   pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
    pSettingOption->connect(pLineEdit, &DLineEdit::focusChanged, [ = ](bool bRet) {
        if (bRet)
            pLineEdit->setText(pSettingOption->value().toString());

    });
    pLineEdit->setText(sElideText);
    sNameLast = sElideText;

    pLayout->addWidget(pLineEdit);
    pLayout->addWidget(pIconButton);

    QWidget *pOptionWidget = new QWidget;
    pOptionWidget->setObjectName("OptionFrame");

    QFormLayout *pOptionLayout = new QFormLayout(pOptionWidget);
    pOptionLayout->setContentsMargins(0, 0, 0, 0);
    pOptionLayout->setSpacing(0);

    pMainWid->setMinimumWidth(240);
    pOptionLayout->addRow(new DLabel(QObject::tr(pSettingOption->name().toStdString().c_str())), pMainWid);

    //auto optionWidget = settingWidget->createWidget(option);
    workaround_updateStyle(pOptionWidget, "light");

    DDialog *pPrompt = new DDialog(pMainWid);
    pPrompt->setIcon(QIcon(":/resources/icons/warning.svg"));
    //pPrompt->setTitle(QObject::tr("Permissions pPrompt"));
    pPrompt->setMessage(QObject::tr("You don't have permission to operate this folder"));
    pPrompt->setWindowFlags(pPrompt->windowFlags() | Qt::WindowStaysOnTopHint);
    pPrompt->addButton(QObject::tr("OK"), true, DDialog::ButtonRecommend);

    auto validate = [ = ](QString sName, bool bAlert = true) -> bool {
        sName = sName.trimmed();
        if (sName.isEmpty()) {
            qCDebug(appMovie) << "sName.isEmpty() return false";
            return false;
        }

        if (sName.size() && sName[0] == '~') {
            qCDebug(appMovie) << "sName[0] == '~' sName.replace(0, 1, QDir::homePath())";
            sName.replace(0, 1, QDir::homePath());
        }

        QFileInfo fi(sName);
        QDir dir(sName);
        if (fi.exists()) {
            qCDebug(appMovie) << "fi.exists()";
            if (!fi.isDir()) {
                qCDebug(appMovie) << "!fi.isDir() pLineEdit->showAlertMessage(QObject::tr(\"Invalid folder\"))";
                if (bAlert) pLineEdit->showAlertMessage(QObject::tr("Invalid folder"));
                return false;
            }

            if (!fi.isReadable() || !fi.isWritable()) {
                qCDebug(appMovie) << "!fi.isReadable() || !fi.isWritable() return false";
                return false;
            }
        } else {
            qCDebug(appMovie) << "!fi.exists()";
            if (dir.cdUp()) {
                qCDebug(appMovie) << "dir.cdUp()";
                QFileInfo ch(dir.path());
                if (!ch.isReadable() || !ch.isWritable()) {
                    qCDebug(appMovie) << "!ch.isReadable() || !ch.isWritable() return false";
                    return false;
                }
            }
        }

        qCDebug(appMovie) << "return true";
        return true;
    };

    pSettingOption->connect(pIconButton, &DPushButton::clicked, [ = ]() {
#ifndef USE_TEST
        QString sName = DFileDialog::getExistingDirectory(nullptr, QObject::tr("Open folder"),
                                                          Platform_MainWindow::lastOpenedPath(),
                                                          DFileDialog::ShowDirsOnly | DFileDialog::DontResolveSymlinks);
#else
        QString sName = "/data/source/deepin-movie-reborn/movie/DMovie";
#endif
        qCDebug(appMovie) << "sName: " << sName;
        if (validate(sName, false)) {
            pSettingOption->setValue(sName);
            sNameLast = sName;
        }
        QFileInfo fileinfo(sName);
        if ((!fileinfo.isReadable() || !fileinfo.isWritable()) && !sName.isEmpty()) {
            qCDebug(appMovie) << "(!fileinfo.isReadable() || !fileinfo.isWritable()) && !sName.isEmpty() pPrompt->show()";
            pPrompt->show();
        }
    });

    pSettingOption->connect(pLineEdit, &DLineEdit::editingFinished, pSettingOption, [ = ]() {

        QString name = pLineEdit->text();
        QDir dir(name);

        auto pn = ElideText(name, {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                            pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
        auto nmls = ElideText(sNameLast, {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                              pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);

        if (!validate(pLineEdit->text(), false)) {
            qCDebug(appMovie) << "!validate(pLineEdit->text(), false)";
            QFileInfo fn(dir.path());
            if ((!fn.isReadable() || !fn.isWritable()) && !name.isEmpty()) {
                qCDebug(appMovie) << "(!fn.isReadable() || !fn.isWritable()) && !name.isEmpty() pPrompt->show()";
                pPrompt->show();
            }
        }
        if (!pLineEdit->lineEdit()->hasFocus()) {
            qCDebug(appMovie) << "!pLineEdit->lineEdit()->hasFocus()";
            if (validate(pLineEdit->text(), false)) {
                qCDebug(appMovie) << "validate(pLineEdit->text(), false) pSettingOption->setValue(pLineEdit->text())";
                pSettingOption->setValue(pLineEdit->text());
                pLineEdit->setText(pn);
                sNameLast = name;
            } else if (pn == sElideText) {
                qCDebug(appMovie) << "pn == sElideText pLineEdit->setText(sElideText)";
                pLineEdit->setText(sElideText);
            } else {
                qCDebug(appMovie) << "else pSettingOption->setValue(sNameLast) pLineEdit->setText(nmls)";
                pSettingOption->setValue(sNameLast);
                pLineEdit->setText(nmls);
            }
        }
    });

    pSettingOption->connect(pLineEdit, &DLineEdit::textEdited, pSettingOption, [ = ](const QString & sNewStr) {
        qCDebug(appMovie) << "pLineEdit->textEdited(sNewStr) validate(sNewStr)";
        validate(sNewStr);
    });

    pSettingOption->connect(pSettingOption, &DTK_CORE_NAMESPACE::DSettingsOption::valueChanged, pLineEdit,
    [ = ](const QVariant & value) {
        auto pi = ElideText(value.toString(), {285, fontMetrics.height()}, QTextOption::WrapAnywhere,
                            pLineEdit->font(), Qt::ElideMiddle, fontMetrics.height(), 285);
        qCDebug(appMovie) << "pSettingOption->valueChanged(value)";
        pLineEdit->setText(pi);
        pLineEdit->update();
    });

    qCDebug(appMovie) << "Exiting createSelectableLineEditOptionHandle function";
    return  pOptionWidget;
}

#ifdef USE_DXCB
class Platform_MainWindowFocusMonitor: public QAbstractNativeEventFilter
{
public:
    explicit Platform_MainWindowFocusMonitor(Platform_MainWindow *src) : QAbstractNativeEventFilter(), _source(src)
    {
        qCDebug(appMovie) << "Platform_MainWindowFocusMonitor constructor";
        qApp->installNativeEventFilter(this);
    }

    ~Platform_MainWindowFocusMonitor()
    {
        qCDebug(appMovie) << "Platform_MainWindowFocusMonitor destructor";
        qApp->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *)
    {
        qCDebug(appMovie) << "Platform_MainWindowFocusMonitor nativeEventFilter";
        if (Q_LIKELY(eventType == "xcb_generic_event_t")) {
            xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
            switch (event->response_type & ~0x80) {
            case XCB_LEAVE_NOTIFY: {
                qCDebug(appMovie) << "Platform_MainWindowFocusMonitor XCB_LEAVE_NOTIFY";
                xcb_leave_notify_event_t *dne = (xcb_leave_notify_event_t *)event;
                auto w = _source->windowHandle();
                if (dne->event == w->winId()) {
                    qCInfo(appMovie) << "---------  leave " << dne->event << dne->child;
                    emit _source->windowLeaved();
                }
                break;
            }

            case XCB_ENTER_NOTIFY: {
                qCDebug(appMovie) << "Platform_MainWindowFocusMonitor XCB_ENTER_NOTIFY";
                xcb_enter_notify_event_t *dne = (xcb_enter_notify_event_t *)event;
                auto w = _source->windowHandle();
                if (dne->event == w->winId()) {
                    qCInfo(appMovie) << "---------  enter " << dne->event << dne->child;
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

    Platform_MainWindow *_source;
};
#endif

class Platform_MainWindowEventListener : public QObject
{
    Q_OBJECT
public:
    explicit Platform_MainWindowEventListener(QWidget *pTarget)
        : QObject(pTarget)
    {
        qCDebug(appMovie) << "Platform_MainWindowEventListener constructor";
        lastCornerEdge = Platform_CornerEdge::Platform_NoneEdge;
        m_pMainWindow = static_cast<Platform_MainWindow *>(pTarget);
        m_pWindow = pTarget->windowHandle();
    }

    void setEnabled(bool bEnale)
    {
        qCDebug(appMovie) << "Platform_MainWindowEventListener setEnabled";
        m_bEnabled = bEnale;
    }

protected:
    bool eventFilter(QObject *pObj, QEvent *pEvent) Q_DECL_OVERRIDE {
        qCDebug(appMovie) << "Platform_MainWindowEventListener eventFilter";
        QWindow *pWindow = qobject_cast<QWindow *>(pObj);
        if (!pWindow) return false;

        Platform_MainWindow *pMainWindow = static_cast<Platform_MainWindow *>(parent());

        switch (static_cast<int>(pEvent->type()))
        {
        case QEvent::MouseMove+1: { //响应tab按钮
            QKeyEvent *pKeyEvent = static_cast<QKeyEvent *>(pEvent);
            //根据需求迷你模式不响应tab键交互
            if (pKeyEvent->key() == Qt::Key_Tab) {
                qCDebug(appMovie) << "QEvent::MouseMove+1 pKeyEvent->key() == Qt::Key_Tab";
                if (!m_pMainWindow->getMiniMode()) {
                    pMainWindow->capturedKeyEvent(pKeyEvent);
                    //Only the tab key interactive response is set to the first
                    if (m_pMainWindow->playlist()->isFocusInPlaylist()) {
                        bool bFocusAttribute = true;
                        m_pMainWindow->playlist()->resetFocusAttribute(bFocusAttribute);
                    }
                } else {
                    qCDebug(appMovie) << "QEvent::MouseMove+1 else return true";
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            qCDebug(appMovie) << "QEvent::MouseButtonPress";
            if (!m_pMainWindow->playlist()) {
                qCDebug(appMovie) << "QEvent::MouseButtonPress !m_pMainWindow->playlist() return true";
                return true;
            }
            if (m_pMainWindow->playlist()->state() == Platform_PlaylistWidget::State::Opened) {
                qCDebug(appMovie) << "QEvent::MouseButtonPress m_pMainWindow->playlist()->state() == Platform_PlaylistWidget::State::Opened";
                m_pMainWindow->toolbox()->clearPlayListFocus();
            }
            //Mouse operation does not respond to the first item
            bool bFocusAttribute = false;
            m_pMainWindow->playlist()->resetFocusAttribute(bFocusAttribute);
            if (!m_bEnabled) {
                qCDebug(appMovie) << "QEvent::MouseButtonPress !m_bEnabled return false";
                return false;
            }
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            setLeftButtonPressed(true);
            // 迷你模式下，不允许resize
            if (!pMainWindow->getMiniMode() && pMainWindow->insideResizeArea(pMouseEvent->globalPos()) &&
                    lastCornerEdge != Platform_CornerEdge::Platform_NoneEdge)
                m_bStartResizing = true;

            pMainWindow->capturedMousePressEvent(pMouseEvent);
            if (m_bStartResizing) {
                qCDebug(appMovie) << "QEvent::MouseButtonPress m_bStartResizing return true";
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            qCDebug(appMovie) << "QEvent::MouseButtonRelease";
            if (!m_bEnabled) {
                qCDebug(appMovie) << "QEvent::MouseButtonRelease !m_bEnabled return false";
                return false;
            }
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            setLeftButtonPressed(false);
            qApp->setOverrideCursor(pWindow->cursor());

            pMainWindow->capturedMouseReleaseEvent(pMouseEvent);
            if (m_bStartResizing) {
                qCDebug(appMovie) << "QEvent::MouseButtonRelease m_bStartResizing return true";
                m_bStartResizing = false;
                return true;
            }
            m_bStartResizing = false;
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent *pMouseEvent = static_cast<QMouseEvent *>(pEvent);
            pMainWindow->resumeToolsWindow();
            qCDebug(appMovie) << "QEvent::MouseMove";

            /* If the focus is on the playlist button, move the mouse to cancel the focus
             * In order to avoid the enter key to expand and the mouse click to expand the playlist
             * There is a problem here, if the mouse does not move, click directly,
             * Will cause focus to appear on the clear list button
             * Please refer to the maintainer whether to add an event filter to the ListBtn
             */
            if (m_pMainWindow->toolbox()->getListBtnFocus()) {
                qCDebug(appMovie) << "QEvent::MouseMove m_pMainWindow->toolbox()->getListBtnFocus()";
                m_pMainWindow->setFocus();
            }
            //If window is maximized ,need quit maximize state when resizing
            if (m_bStartResizing && (pMainWindow->windowState() & Qt::WindowMaximized)) {
                qCDebug(appMovie) << "QEvent::MouseMove m_bStartResizing && (pMainWindow->windowState() & Qt::WindowMaximized)";
                pMainWindow->setWindowState(pMainWindow->windowState() & (~Qt::WindowMaximized));
            } else if (m_bStartResizing && (pMainWindow->windowState() & Qt::WindowFullScreen)) {
                qCDebug(appMovie) << "QEvent::MouseMove m_bStartResizing && (pMainWindow->windowState() & Qt::WindowFullScreen)";
                pMainWindow->setWindowState(pMainWindow->windowState() & (~Qt::WindowFullScreen));
            }

            if (!m_bEnabled) {
                qCDebug(appMovie) << "QEvent::MouseMove !m_bEnabled return false";
                return false;
            }
            const QRect window_visible_rect = m_pWindow->frameGeometry() - pMainWindow->dragMargins();

            if (!m_bLeftButtonPressed) {
                qCDebug(appMovie) << "QEvent::MouseMove !m_bLeftButtonPressed";
                //add by heyi  拦截鼠标移动事件
                pMainWindow->judgeMouseInWindow(QCursor::pos());
                Platform_CornerEdge mouseCorner = Platform_CornerEdge::Platform_NoneEdge;
                QRect cornerRect;

                /// begin set cursor corner type
                cornerRect.setSize(QSize(MOUSE_MARGINS * 2, MOUSE_MARGINS * 2));
                cornerRect.moveTopLeft(m_pWindow->frameGeometry().topLeft());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    qCDebug(appMovie) << "QEvent::MouseMove cornerRect.contains(pMouseEvent->globalPos()) mouseCorner = Platform_CornerEdge::Platform_TopLeftCorner";
                    mouseCorner = Platform_CornerEdge::Platform_TopLeftCorner;
                    goto set_cursor;
                }

                cornerRect.moveTopRight(m_pWindow->frameGeometry().topRight());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    qCDebug(appMovie) << "QEvent::MouseMove cornerRect.contains(pMouseEvent->globalPos()) mouseCorner = Platform_CornerEdge::Platform_TopRightCorner";
                    mouseCorner = Platform_CornerEdge::Platform_TopRightCorner;
                    goto set_cursor;
                }

                cornerRect.moveBottomRight(m_pWindow->frameGeometry().bottomRight());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    qCDebug(appMovie) << "QEvent::MouseMove cornerRect.contains(pMouseEvent->globalPos()) mouseCorner = Platform_CornerEdge::Platform_BottomRightCorner";
                    mouseCorner = Platform_CornerEdge::Platform_BottomRightCorner;
                    goto set_cursor;
                }

                cornerRect.moveBottomLeft(m_pWindow->frameGeometry().bottomLeft());
                if (cornerRect.contains(pMouseEvent->globalPos())) {
                    qCDebug(appMovie) << "QEvent::MouseMove cornerRect.contains(pMouseEvent->globalPos()) mouseCorner = Platform_CornerEdge::Platform_BottomLeftCorner";
                    mouseCorner = Platform_CornerEdge::Platform_BottomLeftCorner;
                    goto set_cursor;
                }

                goto skip_set_cursor; // disable edges

                /// begin set cursor edge type
                if (pMouseEvent->globalX() <= window_visible_rect.x()) {
                    qCDebug(appMovie) << "QEvent::MouseMove pMouseEvent->globalX() <= window_visible_rect.x() mouseCorner = Platform_CornerEdge::Platform_LeftEdge";
                    mouseCorner = Platform_CornerEdge::Platform_LeftEdge;
                } else if (pMouseEvent->globalX() < window_visible_rect.right()) {
                    if (pMouseEvent->globalY() <= window_visible_rect.y()) {
                        qCDebug(appMovie) << "QEvent::MouseMove pMouseEvent->globalY() <= window_visible_rect.y() mouseCorner = Platform_CornerEdge::Platform_TopEdge";
                        mouseCorner = Platform_CornerEdge::Platform_TopEdge;
                    } else if (pMouseEvent->globalY() >= window_visible_rect.bottom()) {
                        qCDebug(appMovie) << "QEvent::MouseMove pMouseEvent->globalY() >= window_visible_rect.bottom() mouseCorner = Platform_CornerEdge::Platform_BottomEdge";
                        mouseCorner = Platform_CornerEdge::Platform_BottomEdge;
                    } else {
                        goto skip_set_cursor;
                    }
                } else if (pMouseEvent->globalX() >= window_visible_rect.right()) {
                    qCDebug(appMovie) << "QEvent::MouseMove pMouseEvent->globalX() >= window_visible_rect.right() mouseCorner = Platform_CornerEdge::Platform_RightEdge";
                    mouseCorner = Platform_CornerEdge::Platform_RightEdge;
                } else {
                    goto skip_set_cursor;
                }
set_cursor:
#ifdef USE_DXCB
#ifdef __mips__
                if (pWindow->property("_d_real_winId").isValid()) {
                    auto real_wid = pWindow->property("_d_real_winId").toUInt();
                    qCDebug(appMovie) << "QEvent::MouseMove real_wid = " << real_wid;
                    Utility::setWindowCursor(real_wid, mouseCorner);
                } else {
                    qCDebug(appMovie) << "QEvent::MouseMove pWindow->winId() = " << pWindow->winId();
                    Utility::setWindowCursor(static_cast<quint32>(pWindow->winId()), mouseCorner);
                }
#endif
#endif

                if (qApp->mouseButtons() == Qt::LeftButton) {
                    qCDebug(appMovie) << "QEvent::MouseMove qApp->mouseButtons() == Qt::LeftButton";
                    updateGeometry(mouseCorner, pMouseEvent);
                }
                lastCornerEdge = mouseCorner;
                return true;

skip_set_cursor:
                qCDebug(appMovie) << "QEvent::MouseMove goto skip_set_cursor";
                lastCornerEdge = mouseCorner = Platform_CornerEdge::Platform_NoneEdge;
                return false;
            } else {
                if (m_bStartResizing) {
                    qCDebug(appMovie) << "QEvent::MouseMove m_bStartResizing";
                    updateGeometry(lastCornerEdge, pMouseEvent);
                    if (CompositingManager::get().platform() != Platform::X86) {
                        qCDebug(appMovie) << "QEvent::MouseMove CompositingManager::get().platform() != Platform::X86";
                        pMainWindow->syncPostion();
                    }
                    return true;
                }
            }
            break;
        }
        case QEvent::Resize: {
            // 迷你模式下，不允许resize
            if (pMainWindow->getMiniMode()) {
                qCDebug(appMovie) << "Could not resize window in mini mode";
                return true;
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
        qCDebug(appMovie) << "QEvent::MouseMove setLeftButtonPressed";
        if (m_bLeftButtonPressed == bPressed) {
            qCDebug(appMovie) << "QEvent::MouseMove m_bLeftButtonPressed == bPressed return";
            return;
        }

        if (!bPressed) {
            qCDebug(appMovie) << "QEvent::MouseMove !bPressed";
#ifdef USE_DXCB
            Utility::cancelWindowMoveResize(static_cast<quint32>(_window->winId()));
#endif
        }

        m_bLeftButtonPressed = bPressed;
    }

    void updateGeometry(Platform_CornerEdge edge, QMouseEvent *pEvent)
    {
        qCDebug(appMovie) << "QEvent::MouseMove updateGeometry";
        Platform_MainWindow *pMainWindow = static_cast<Platform_MainWindow *>(parent());
        pMainWindow->updateGeometry(edge, pEvent->globalPos());
    }

    bool m_bLeftButtonPressed = false;
    bool m_bStartResizing = false;
    bool m_bEnabled {true};
    Platform_CornerEdge lastCornerEdge;
    QWindow *m_pWindow;
    Platform_MainWindow *m_pMainWindow;
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
Platform_MainWindow::Platform_MainWindow(QWidget *parent)
    : DMainWindow(nullptr)
{
    qCDebug(appMovie) << "Platform_MainWindow constructor";
    initMember();

    QJsonObject obj{
        {"tid", EventLogUtils::Start},
        {"mode", 1}, //冷启动
        {"version", VERSION}
    };
    EventLogUtils::get().writeLogs(obj);

    //add bu heyi
    this->setAttribute(Qt::WA_AcceptTouchEvents);
    m_mousePressTimer.setInterval(1300);
    connect(&m_mousePressTimer, &QTimer::timeout, this, &Platform_MainWindow::slotmousePressTimerTimeOut);

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

#ifdef USE_DXCB
    if (DApplication::isDXcbPlatform()) {
        _handle = new DPlatformWindowHandle(this, this);
        _handle->setEnableSystemResize(false);
        _handle->setEnableSystemMove(false);
        _handle->setWindowRadius(4);
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &Platform_MainWindow::updateShadow);
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
        Backend::setDebugLevel(Backend::DebugLevel::Debug);
    } else if (commanLineManager.verbose()) {
        Backend::setDebugLevel(Backend::DebugLevel::Verbose);
    }
    m_pEngine = new PlayerEngine(this);

#ifndef USE_DXCB
    m_pEngine->move(0, 0);
#endif
    //初始化显示音量与音量条控件一致
    m_nDisplayVolume = 100;
    m_pToolbox = new Platform_ToolboxProxy(this, m_pEngine);
    m_pToolbox->setObjectName(BOTTOM_TOOL_BOX);

    titlebar()->deleteLater();

    connect(m_pToolbox, &Platform_ToolboxProxy::sigUnsupported, this, &Platform_MainWindow::slotUnsupported);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &Platform_MainWindow::slotPlayerStateChanged);
    connect(m_pEngine, &PlayerEngine::sigInvalidFile, this, &Platform_MainWindow::slotInvalidFile);
    connect(ActionFactory::get().mainContextMenu(), &DMenu::triggered, this, &Platform_MainWindow::menuItemInvoked);
    connect(ActionFactory::get().playlistContextMenu(), &DMenu::triggered, this, &Platform_MainWindow::menuItemInvoked);
    connect(this, &Platform_MainWindow::frameMenuEnable, &ActionFactory::get(), &ActionFactory::frameMenuEnable);
    connect(this, &Platform_MainWindow::playSpeedMenuEnable, &ActionFactory::get(), &ActionFactory::playSpeedMenuEnable);
    connect(this, &Platform_MainWindow::subtitleMenuEnable, &ActionFactory::get(), &ActionFactory::subtitleMenuEnable);
    connect(this, &Platform_MainWindow::soundMenuEnable, &ActionFactory::get(), &ActionFactory::soundMenuEnable);
    connect(qApp, &QGuiApplication::focusWindowChanged, this, &Platform_MainWindow::slotFocusWindowChanged);

    connect(m_pToolbox, &Platform_ToolboxProxy::sigVolumeChanged, this, &Platform_MainWindow::slotVolumeChanged);
    connect(m_pToolbox, &Platform_ToolboxProxy::sigMuteStateChanged, this, &Platform_MainWindow::slotMuteChanged);

    //In the case of Platform, this is currently not initialized in the case of
    //MIPS, followed by the situation, and the code is subsequently removed by judging the macro.
    m_pProgIndicator = nullptr;
    m_pFullScreenTimeLabel = nullptr;
    if (CompositingManager::get().platform() != Platform::Mips) {
        qCDebug(appMovie) << "CompositingManager::get().platform() != Platform::Mips";
        //m_pProgIndicator = new Platform_MovieProgressIndicator(this);
        //m_pFullScreenTimeLabel = new QLabel;
    }
    if (m_pProgIndicator && m_pFullScreenTimeLabel) {
        m_pProgIndicator->setVisible(false);
        connect(m_pEngine, &PlayerEngine::elapsedChanged, [ = ]() {
            m_pProgIndicator->updateMovieProgress(m_pEngine->duration(), m_pEngine->elapsed());
        });

        m_pFullScreenTimeLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_pFullScreenTimeLabel->setWindowFlags(Qt::FramelessWindowHint);
        m_pFullScreenTimeLabel->setParent(this);
        m_pFullScreenTimeLabel->setWindowFlags(m_pFullScreenTimeLabel->windowFlags() | Qt::ToolTip);
        m_pFullScreenTimeLayout = new QHBoxLayout;
        m_pFullScreenTimeLayout->addStretch();
        m_pFullScreenTimeLayout->addWidget(m_pToolbox->getfullscreentimeLabel());
        m_pFullScreenTimeLayout->addWidget(m_pToolbox->getfullscreentimeLabelend());
        m_pFullScreenTimeLayout->addStretch();
        m_pFullScreenTimeLabel->setLayout(m_pFullScreenTimeLayout);
        m_pFullScreenTimeLabel->close();
    }

    // mini ui
    qCDebug(appMovie) << "Platform_MainWindow mini ui";
    QSignalMapper *pSignalMapper = new QSignalMapper(this);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5版本使用mapped信号
    connect(pSignalMapper, static_cast<void(QSignalMapper::*)(const QString &)>(&QSignalMapper::mapped), this, &Platform_MainWindow::miniButtonClicked);
#else
    // Qt6版本使用mappedString信号
    connect(pSignalMapper, &QSignalMapper::mappedString, this, &Platform_MainWindow::miniButtonClicked); 
#endif

    m_pMiniPlayBtn = new DIconButton(this);
    m_pMiniQuitMiniBtn = new DIconButton(this);
    m_pMiniCloseBtn = new DIconButton(this);

    m_pMiniPlayBtn->setFlat(true);
    m_pMiniCloseBtn->setFlat(true);
    m_pMiniQuitMiniBtn->setFlat(true);

    QPalette palette;
    palette.setColor(m_pMiniPlayBtn->backgroundRole(), Qt::black);
    m_pMiniPlayBtn->setAutoFillBackground(true);
    m_pMiniQuitMiniBtn->setAutoFillBackground(true);
    m_pMiniCloseBtn->setAutoFillBackground(true);
    m_pMiniPlayBtn->setPalette(palette);
    m_pMiniQuitMiniBtn->setPalette(palette);
    m_pMiniCloseBtn->setPalette(palette);

    m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
    m_pMiniPlayBtn->setIconSize(QSize(30, 30));
    m_pMiniPlayBtn->setFixedSize(QSize(35, 35));
    m_pMiniPlayBtn->setObjectName("MiniPlayBtn");
    m_pMiniPlayBtn->setAccessibleName("MiniPlayBtn");
    connect(m_pMiniPlayBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniPlayBtn, "play");

    connect(m_pEngine, &PlayerEngine::stateChanged, [ = ]() {
        qCInfo(appMovie) << __func__ << m_pEngine->state();

        if (m_pEngine->state() == PlayerEngine::CoreState::Playing
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Playing and ...";
            emit subtitleMenuEnable(false);
        } else {
            qCDebug(appMovie) << "state is not playing";
            emit subtitleMenuEnable(true);
        }

        if (m_pProgIndicator) {
            qCDebug(appMovie) << "m_pProgIndicator";
            if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
                //播放切换时，更新音量dbus 当前的sinkInputPath
                if (m_pProgIndicator && m_pFullScreenTimeLabel) {
                    qCDebug(appMovie) << "m_pProgIndicator";
                    m_pFullScreenTimeLabel->close();
                    m_pProgIndicator->setVisible(false);
                }
                emit frameMenuEnable(false);
                emit playSpeedMenuEnable(false);
            }
        }

        if (m_pEngine->state() == PlayerEngine::CoreState::Playing) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Playing";
            if (CompositingManager::get().platform() != Platform::Mips) {
                qCDebug(appMovie) << "CompositingManager::get().platform() != Platform::Mips";
                if (isFullScreen()) {
                    qCDebug(appMovie) << "isFullScreen()";
                    QRect screenGeo = windowHandle()->screen()->geometry();
                    int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
                    pixelsWidth = qMax(117, pixelsWidth);
                    if (m_pFullScreenTimeLabel) {
                        m_pFullScreenTimeLabel->setGeometry(screenGeo.width() + screenGeo.x() - pixelsWidth - 60, 40 + screenGeo.y(), pixelsWidth + 60, 36);
                        qCDebug(appMovie) << "m_pFullScreenTimeLabel->setGeometry";
                        if(m_bShowTime) {
                            m_pFullScreenTimeLabel->show();
                            m_pProgIndicator->setVisible(true);
                        }
                    }
                    QTimer::singleShot(200, [ = ]() {
                        activateWindow();    // show other window make mainwindow deactivate
                        setFocus();
                    });
                }
            }
            m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/pause-normal-mini.svg"));
            m_pMiniPlayBtn->setObjectName("MiniPauseBtn");

            if (m_pEngine->playlist().count() > 0 && !m_pEngine->currFileIsAudio()) {
                qCDebug(appMovie) << "m_pEngine->playlist().count() > 0 && !m_pEngine->currFileIsAudio()";
                emit frameMenuEnable(true);
                setMusicShortKeyState(true);
            } else {
                qCDebug(appMovie) << "m_pEngine->playlist().count() <= 0 || m_pEngine->currFileIsAudio()";
                emit frameMenuEnable(false);
                setMusicShortKeyState(false);
            }
            emit playSpeedMenuEnable(true);
            if (m_nLastCookie > 0) {
                qCDebug(appMovie) << "m_nLastCookie > 0";
                utils::UnInhibitStandby(m_nLastCookie);
                qCInfo(appMovie) << "uninhibit cookie" << m_nLastCookie;
                m_nLastCookie = 0;
            }
            if (m_nPowerCookie > 0) {
                qCDebug(appMovie) << "Releasing power inhibit cookie in play state change:" << m_nPowerCookie;
                utils::UnInhibitPower(m_nPowerCookie);
                m_nPowerCookie = 0;
            }
            m_nLastCookie = utils::InhibitStandby();
            m_nPowerCookie = utils::InhibitPower();
        } else {
            qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Playing";
            if (m_pMircastShowWidget->isVisible()) {
                qCDebug(appMovie) << "m_pMircastShowWidget->isVisible()";
                return;
            }
            m_pMiniPlayBtn->setIcon(QIcon(":/resources/icons/light/mini/play-normal-mini.svg"));
            m_pMiniPlayBtn->setObjectName("MiniPlayBtn");

            if (m_nLastCookie > 0) {
                qCDebug(appMovie) << "m_nLastCookie > 0";
                utils::UnInhibitStandby(m_nLastCookie);
                qCInfo(appMovie) << "uninhibit cookie" << m_nLastCookie;
                m_nLastCookie = 0;
            }
            if (m_nPowerCookie > 0) {
                qCDebug(appMovie) << "m_nPowerCookie > 0";
                utils::UnInhibitPower(m_nPowerCookie);
                m_nPowerCookie = 0;
            }
        }
    });

    m_pMiniCloseBtn->setIcon(QIcon(":/resources/icons/light/mini/close-normal.svg"));
    m_pMiniCloseBtn->setIconSize(QSize(30, 30));
    m_pMiniCloseBtn->setFixedSize(QSize(35, 35));
    m_pMiniCloseBtn->setObjectName("MiniCloseBtn");
    m_pMiniCloseBtn->setAccessibleName("MiniCloseBtn");
    connect(m_pMiniCloseBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniCloseBtn, "close");

    m_pMiniQuitMiniBtn->setIcon(QIcon(":/resources/icons/light/mini/restore-normal-mini.svg"));
    m_pMiniQuitMiniBtn->setIconSize(QSize(30, 30));
    m_pMiniQuitMiniBtn->setFixedSize(QSize(35, 35));
    m_pMiniQuitMiniBtn->setObjectName("MiniQuitMiniBtn");
    m_pMiniQuitMiniBtn->setAccessibleName("MiniQuitMiniBtn");
    connect(m_pMiniQuitMiniBtn, SIGNAL(clicked()), pSignalMapper, SLOT(map()));
    pSignalMapper->setMapping(m_pMiniQuitMiniBtn, "quit_mini");

    m_pMiniPlayBtn->setVisible(m_bMiniMode);
    m_pMiniCloseBtn->setVisible(m_bMiniMode);
    m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);

    m_pMiniPlayBtn->setAttribute(Qt::WA_NativeWindow);
    m_pMiniCloseBtn->setAttribute(Qt::WA_NativeWindow);
    m_pMiniQuitMiniBtn->setAttribute(Qt::WA_NativeWindow);

    updateProxyGeometry();

    connect(&ShortcutManager::get(), &ShortcutManager::bindingsChanged,
            this, &Platform_MainWindow::onBindingsChanged);
    ShortcutManager::get().buildBindings();          //绑定要放在connect后
    connect(m_pEngine, SIGNAL(stateChanged()), this, SLOT(update()));
    connect(m_pEngine, &PlayerEngine::tracksChanged, this, &Platform_MainWindow::updateActionsState);
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &Platform_MainWindow::updateActionsState);
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
    connect(m_pEngine, &PlayerEngine::fileLoaded, this, &Platform_MainWindow::slotFileLoaded);

    connect(m_pEngine, &PlayerEngine::videoSizeChanged, [ = ]() {
        this->resizeByConstraints();
    });
    connect(m_pEngine, &PlayerEngine::stateChanged, this, &Platform_MainWindow::animatePlayState);

    connect(m_pEngine, &PlayerEngine::loadOnlineSubtitlesFinished,
            [this](const QUrl & url, bool success) {//不能去掉 url参数
        m_pCommHintWid->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
    });

    connect(&m_autoHideTimer, &QTimer::timeout, this, &Platform_MainWindow::suspendToolsWindow);
    m_autoHideTimer.setSingleShot(true);

    connect(&m_delayedMouseReleaseTimer, &QTimer::timeout, this, &Platform_MainWindow::delayedMouseReleaseHandler);
    m_delayedMouseReleaseTimer.setSingleShot(true);

    m_pCommHintWid = new Platform_NotificationWidget(this);
    m_pCommHintWid->setFixedHeight(30);
    m_pCommHintWid->setAnchor(Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    m_pCommHintWid->setAnchorPoint(QPoint(30, 58));
    m_pCommHintWid->hide();
    m_pDVDHintWid = new Platform_NotificationWidget(this);
    m_pDVDHintWid->setFixedHeight(30);
    m_pDVDHintWid->setAnchor(Platform_NotificationWidget::ANCHOR_NORTH_WEST);
    m_pDVDHintWid->setAnchorPoint(QPoint(30, 58));
    m_pDVDHintWid->hide();

#ifdef USE_DXCB
    connect(qApp, &QGuiApplication::applicationStateChanged,
            this, &Platform_MainWindow::onApplicationStateChanged);

    _evm = new EventMonitor(this);
    connect(_evm, &EventMonitor::buttonedPress, this, &Platform_MainWindow::onMonitorButtonPressed);
    connect(_evm, &EventMonitor::buttonedDrag, this, &Platform_MainWindow::onMonitorMotionNotify);
    connect(_evm, &EventMonitor::buttonedRelease, this, &Platform_MainWindow::onMonitorButtonReleased);
    _evm->start();

    m_pEventListener = new Platform_MainWindowEventListener(this);
    this->windowHandle()->installEventFilter(m_pEventListener);

    //auto mwfm = new MainWindowFocusMonitor(this);
    auto mwpm = new MainWindowPropertyMonitor(this);

    connect(this, &Platform_MainWindow::windowEntered, &Platform_MainWindow::resumeToolsWindow);
    connect(this, &Platform_MainWindow::windowLeaved, &Platform_MainWindow::suspendToolsWindow);
#else
    winId();
    m_pEventListener = new Platform_MainWindowEventListener(this);
    QTimer::singleShot(500, [this](){
        this->windowHandle()->installEventFilter(m_pEventListener);

        connect(this, &Platform_MainWindow::windowEntered, &Platform_MainWindow::resumeToolsWindow);
        connect(this, &Platform_MainWindow::windowLeaved, &Platform_MainWindow::suspendToolsWindow);

        if (m_pEngine->windowHandle())
            m_pEngine->windowHandle()->installEventFilter(m_pEventListener);
        m_pTitlebar->windowHandle()->installEventFilter(m_pEventListener);
        m_pToolbox->windowHandle()->installEventFilter(m_pEventListener);
        m_pMircastShowWidget->windowHandle()->installEventFilter(m_pEventListener);
        qCInfo(appMovie) << "event listener";
    } );

#endif

    m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();
    m_pCommHintWid->setWM(m_bIsWM);
    connect(DWindowManagerHelper::instance(), &DWindowManagerHelper::hasBlurWindowChanged, this, &Platform_MainWindow::slotWMChanged);

    m_pAnimationlabel = new Platform_AnimationLabel(this, this);
    m_pAnimationlabel->setWM(m_bIsWM);

    if (CompositingManager::get().platform() != Platform::X86 && m_bIsWM) {
        m_pAnimationlabel->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
    } else {
        m_pAnimationlabel->setGeometry(width() / 2 - 100, height() / 2, 100, 100);
    }
    m_pPopupWid = new Platform_MessageWindow(this);
    m_pPopupWid->hide();
    defaultplaymodeinit();
    m_bShowTime = Settings::get().internalOption("showTimeFullScreen").toBool();
    connect(&Settings::get(), &Settings::defaultplaymodechanged, this, &Platform_MainWindow::slotdefaultplaymodechanged);
    connect(&Settings::get(), &Settings::setDecodeModel, this, &Platform_MainWindow::onSetDecodeModel,Qt::DirectConnection);
    connect(&Settings::get(), &Settings::refreshDecode, this, &Platform_MainWindow::onRefreshDecode,Qt::DirectConnection);
    connect(&Settings::get(), &Settings::showTimeFullScreenChanged, this, [&](const QString &key, const QVariant &value) {
        if(value.isValid()) {
            m_bShowTime = value.toBool();
            //The X86 platform draws on GiWidget, and the MIPS platform does not need to draw
            if (CompositingManager::get().platform() != Platform::Mips) {
                if(m_pEngine && (m_pEngine->state() != PlayerEngine::Idle) && isFullScreen() && m_pProgIndicator && m_pFullScreenTimeLabel) {
                    m_pProgIndicator->setVisible(value.toBool());
                    m_pFullScreenTimeLabel->setVisible(value.toBool());
                }
            }
        }
    },Qt::DirectConnection);
    connect(m_pEngine, &PlayerEngine::onlineStateChanged, this, &Platform_MainWindow::checkOnlineState);
    connect(&OnlineSubtitle::get(), &OnlineSubtitle::onlineSubtitleStateChanged, this, &Platform_MainWindow::checkOnlineSubtitle);
    connect(m_pEngine, &PlayerEngine::mpvErrorLogsChanged, this, &Platform_MainWindow::checkErrorMpvLogsChanged);
    connect(m_pEngine, &PlayerEngine::mpvWarningLogsChanged, this, &Platform_MainWindow::checkWarningMpvLogsChanged);
    connect(m_pEngine, &PlayerEngine::urlpause, this, &Platform_MainWindow::slotUrlpause);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::newProcessInstance, this, [ = ] {
        this->activateWindow();
    });
    connect(qApp, &QGuiApplication::fontChanged, this, &Platform_MainWindow::slotFontChanged);

    ThreadPool::instance()->moveToNewThread(&m_diskCheckThread);
    m_diskCheckThread.start();
    connect(&m_diskCheckThread, &Diskcheckthread::diskRemove, this, &Platform_MainWindow::diskRemoved);

    QTimer::singleShot(300, [this]() {
        loadPlayList();
    });

    m_pDBus = new QDBusInterface("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
    connect(m_pDBus, SIGNAL(PrepareForSleep(bool)), this, SLOT(sleepStateChanged(bool)));

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QDBusConnection::sessionBus().connect("org.deepin.dde.ShutdownFront1", "/org/deepin/dde/lockFront1",
                                          "org.deepin.dde.lockFront1", "Visible", this,
                                          SLOT(lockStateChanged(bool)));
#else
#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
    QDBusConnection::sessionBus().connect("org.deepin.dde.LockFront1", "/org/deepin/dde/LockFront1",
                                          "org.deepin.dde.LockFront1", "Visible", this,
                                          SLOT(lockStateChanged(bool)));
#endif
#endif

    m_pMovieWidget = new MovieWidget(this);
    m_pMovieWidget->hide();
    m_pMircastShowWidget = new MircastShowWidget(this);
    m_pMircastShowWidget->hide();
    connect(m_pToolbox, &Platform_ToolboxProxy::sigMircastState, this, &Platform_MainWindow::slotUpdateMircastState);
    connect(m_pMircastShowWidget, &MircastShowWidget::exitMircast, this, &Platform_MainWindow::slotExitMircast);
    m_pMovieWidget->windowHandle()->installEventFilter(m_pEventListener);

    qDBusRegisterMetaType<SessionInfo>();
    qDBusRegisterMetaType<SessionInfoList>();
    QDBusPendingReply<SessionInfoList> reply = m_pDBus->call("ListSessions");
    QString path = reply.value().last().sessionPath.path();

    QDBusConnection::systemBus().connect("org.freedesktop.login1", path,
                                         "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                                         SLOT(slotProperChanged(QString, QVariantMap, QStringList)));
    qCInfo(appMovie) << "session Path is :" << path;
    connect(dynamic_cast<MpvProxy *>(m_pEngine->getMpvProxy()),&MpvProxy::crashCheck,&Settings::get(),&Settings::crashCheck);
    //解码初始化
    //decodeInit();
    qCDebug(appMovie) << "Platform_MainWindow constructor";
}

void Platform_MainWindow::setupTitlebar()
{
    qCDebug(appMovie) << "Platform_MainWindow setupTitlebar";
    m_pTitlebar = new Titlebar(this);
    m_pTitlebar->move(0, 0);
    setTitlebarShadowEnabled(false);
    m_pTitlebar->setFixedHeight(50);
    m_pTitlebar->setAttribute(Qt::WA_NativeWindow);
    m_pTitlebar->winId();
    m_pTitlebar->titlebar()->setMenu(ActionFactory::get().titlebarMenu());

    connect(m_pTitlebar->titlebar()->menu(), &DMenu::triggered, this, &Platform_MainWindow::menuItemInvoked);
}

void Platform_MainWindow::updateContentGeometry(const QRect &rect)
{
    qCDebug(appMovie) << "Platform_MainWindow updateContentGeometry";
#ifdef USE_DXCB
    auto frame = QWindow::fromWinId(windowHandle()->winId());

    QRect frame_rect = rect;
    if (_handle) {
        qCDebug(appMovie) << "Platform_MainWindow _handle";
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
    qCDebug(appMovie) << "Platform_MainWindow updateContentGeometry end";
}

#ifdef USE_DXCB
void Platform_MainWindow::updateShadow()
{
    qCDebug(appMovie) << "Platform_MainWindow updateShadow";
    if (isActiveWindow()) {
        qCDebug(appMovie) << "Platform_MainWindow isActiveWindow";
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_ACTIVE);
    } else {
        qCDebug(appMovie) << "Platform_MainWindow is not ActiveWindow";
        _handle->setShadowRadius(60);
        _handle->setShadowColor(SHADOW_COLOR_NORMAL);
    }
    qCDebug(appMovie) << "Platform_MainWindow updateShadow end";
}
#endif

bool Platform_MainWindow::event(QEvent *pEvent)
{
    qCDebug(appMovie) << "Platform_MainWindow event";
    if (pEvent->type() == QEvent::UpdateRequest || pEvent->type() == QEvent::Paint) {
        qCDebug(appMovie) << "Platform_MainWindow event UpdateRequest or Paint";
        return DMainWindow::event(pEvent);
    }

    if (pEvent->type() == QEvent::TouchBegin) {
        qCDebug(appMovie) << "Platform_MainWindow event TouchBegin";
        //判定是否是触屏
        this->m_posMouseOrigin = mapToGlobal(QCursor::pos());
        m_bIsTouch = true;
    }

    //add by heyi
    //判断是否停止右键菜单定时器
    if (m_bMousePressed) {
        if (qAbs(m_nLastPressX - mapToGlobal(QCursor::pos()).x()) > 50 || qAbs(m_nLastPressY - mapToGlobal(QCursor::pos()).y()) > 50) {
            if (m_mousePressTimer.isActive()) {
                qCInfo(appMovie) << "结束定时器";
                m_mousePressTimer.stop();
                m_bMousePressed = false;
            }
        }
    }

    if (pEvent->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *pWindowStateChangeEvent = dynamic_cast<QWindowStateChangeEvent *>(pEvent);
        m_lastWindowState = pWindowStateChangeEvent->oldState();
        qCInfo(appMovie) << "------------ m_lastWindowState" << m_lastWindowState
                << "current " << windowState();
        //NOTE: windowStateChanged won't be emitted if by dragging to restore. so we need to
        //check window state here.
        if (windowState() & Qt::WindowMinimized) {   //fix bug 53683
            qCDebug(appMovie) << "current state is WindowMinimized";
            if (Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (m_pEngine && m_pEngine->state() == PlayerEngine::Playing) {
                    qCDebug(appMovie) << "state is playing";
                    requestAction(ActionFactory::TogglePause);
                    m_bQuitfullscreenflag = true;
                }
                QList<QAction *> listActs = ActionFactory::get().findActionsByKind(ActionFactory::TogglePlaylist);
                listActs.at(0)->setChecked(false);
            }
        } else if (m_lastWindowState & Qt::WindowMinimized /*&& windowState() == Qt::WindowNoState*/) {
            qCDebug(appMovie) << "last state is WindowMinimized";
            if (Settings::get().isSet(Settings::PauseOnMinimize)) {
                if (m_bQuitfullscreenflag) {
                    requestAction(ActionFactory::TogglePause);
                    m_bQuitfullscreenflag = false;
                }
            }
        }
        onWindowStateChanged();
    }

    if (pEvent->type() == QEvent::WindowDeactivate) {
        qCDebug(appMovie) << "Platform_MainWindow event WindowDeactivate";
        m_pCommHintWid->hide();
    }
    if (pEvent->type() == QEvent::Enter) {
        m_bMouseMoved = false;
    }
    return DMainWindow::event(pEvent);
}

void Platform_MainWindow::leaveEvent(QEvent *)
{
    m_autoHideTimer.start(AUTOHIDE_TIMEOUT);
}

void Platform_MainWindow::onWindowStateChanged()
{
    qCDebug(appMovie) << "Platform_MainWindow onWindowStateChanged";
    qCInfo(appMovie) << windowState();
    if (!m_bMiniMode && !isFullScreen()) {
        qCDebug(appMovie) << "!m_bMiniMode && !isFullScreen()";
        m_pTitlebar->setVisible(m_pToolbox->isVisible());
    } else {
        qCDebug(appMovie) << "m_bMiniMode || isFullScreen()";
        m_pTitlebar->setVisible(false);
    }
    if (CompositingManager::get().platform() != Platform::Mips) {
        if (m_bShowTime && m_pProgIndicator) {
            m_pProgIndicator->setVisible(isFullScreen() && m_pEngine && m_pEngine->state() != PlayerEngine::Idle);
        }
    }

#ifndef USE_DXCB
    m_pTitlebar->move(0, 0);
    m_pEngine->move(0, 0);
#endif

    if (!isFullScreen() && !isMaximized()) {
        qCDebug(appMovie) << "!isFullScreen() && !isMaximized()";
        if (m_bMovieSwitchedInFsOrMaxed || !m_lastRectInNormalMode.isValid()) {
            if (m_bMousePressed || m_bMouseMoved) {
                qCDebug(appMovie) << "m_bMousePressed || m_bMouseMoved";
                m_bDelayedResizeByConstraint = true;
            } else {
                qCDebug(appMovie) << "else";
                setMinimumSize({0, 0});
                resizeByConstraints(true);
            }
        }

        m_bMovieSwitchedInFsOrMaxed = false;
    }
    update();

    if (isMinimized()) {
        qCDebug(appMovie) << "isMinimized()";
        if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Opened) {
            qCDebug(appMovie) << "m_pPlaylist->state() == Platform_PlaylistWidget::Opened";
            m_pPlaylist->togglePopup(false);
        }
        m_pAnimationlabel->hide();
    }
    if (isMaximized()) {
        qCDebug(appMovie) << "isMaximized()";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_pAnimationlabel->move(QPoint(QApplication::desktop()->availableGeometry().width() / 2 - 100,
                                     QApplication::desktop()->availableGeometry().height() / 2 - 100));
#else
        QScreen *screen = window()->screen();
        if (screen) {
            qCDebug(appMovie) << "screen";
            QRect availableGeometry = screen->availableGeometry();
            m_pAnimationlabel->move(QPoint(availableGeometry.width() / 2 - 100,
                                         availableGeometry.height() / 2 - 100));
        }
#endif
    }
    if (!isFullScreen() && !isMaximized() && !m_bMiniMode) {
        qCDebug(appMovie) << "!isFullScreen() && !isMaximized() && !m_bMiniMode";
        m_pAnimationlabel->move(QPoint((m_lastRectInNormalMode.width() - m_pAnimationlabel->width()) / 2,
                                       (m_lastRectInNormalMode.height() - m_pAnimationlabel->height()) / 2));
    }
}

#ifdef USE_DXCB
static QPoint lastm_pEngine_pos;
static QPoint last_wm_pos;
static bool bClicked = false;
void Platform_MainWindow::onMonitorButtonPressed(int nX, int nY)
{
    qCDebug(appMovie) << "Platform_MainWindow onMonitorButtonPressed";
    QPoint pos(nX, nY);
    int nPoint = 2;
    QMargins m(nPoint, nPoint, nPoint, nPoint);
    if (geometry().marginsRemoved(m).contains(pos)) {
        QWidget *pWidget = qApp->topLevelAt(pos);
        if (pWidget && pWidget == this) {
            qCInfo(appMovie) << __func__ << "click inside main window";
            last_wm_pos = QPoint(nX, nY);
            lastm_pEngine_pos = windowHandle()->framePosition();
            bClicked = true;
        }
    }
}

void Platform_MainWindow::onMonitorButtonReleased(int nX, int nY)
{
    qCDebug(appMovie) << "Platform_MainWindow onMonitorButtonReleased";
    if (bClicked) {
        qCInfo(appMovie) << __func__;
        bClicked = false;
    }
}

void Platform_MainWindow::onMonitorMotionNotify(int nX, int nY)
{
    qCDebug(appMovie) << "Platform_MainWindow onMonitorMotionNotify";
    if (bClicked) {
        qCDebug(appMovie) << "bClicked";
        QPoint pos = QPoint(nX, nY) - last_wm_pos;
        windowHandle()->setFramePosition(lastm_pEngine_pos + pos);
    }
}
#endif

bool Platform_MainWindow::judgeMouseInWindow(QPoint pos)
{
    qCDebug(appMovie) << "Platform_MainWindow judgeMouseInWindow";
    QRect rect = frameGeometry();
    bool bRet = rect.contains(pos);
    QPoint topLeft = rect.topLeft();
    QPoint bottomRight = rect.bottomRight();
    pos = mapToGlobal(pos);
    topLeft = mapToGlobal(topLeft);
    bottomRight = mapToGlobal(bottomRight);

    if ((pos.x() == topLeft.x()) || (pos.x() == bottomRight.x()) || (pos.y() == topLeft.y()) || (pos.y() == bottomRight.y())) {
        qCDebug(appMovie) << "leaveEvent";
        leaveEvent(nullptr);
    }

    return bRet;
}

#ifdef USE_DXCB
void Platform_MainWindow::onApplicationStateChanged(Qt::ApplicationState e)
{
    qCDebug(appMovie) << "Platform_MainWindow onApplicationStateChanged";
    switch (e) {
    case Qt::ApplicationActive:
        qCDebug(appMovie) << "Qt::ApplicationActive";
        if (qApp->focusWindow())
            qCInfo(appMovie) << QString("focus window 0x%1").arg(qApp->focusWindow()->winId(), 0, 16);
        qApp->setActiveWindow(this);
        _evm->resumeRecording();
        resumeToolsWindow();
        break;

    case Qt::ApplicationInactive:
        qCDebug(appMovie) << "Qt::ApplicationInactive";
        _evm->suspendRecording();
        suspendToolsWindow();
        break;

    default:
        break;
    }
}
#endif

void Platform_MainWindow::animatePlayState()
{
    qCDebug(appMovie) << "Platform_MainWindow animatePlayState";
    if (m_bMiniMode || isMinimized()) {
        return;
    }

    if (!m_bInBurstShootMode && m_pEngine->state() == PlayerEngine::CoreState::Paused
            && !m_bMiniMode && !m_pMircastShowWidget->isVisible()) {
            qCDebug(appMovie) << "m_bInBurstShootMode && m_pEngine->state() == PlayerEngine::CoreState::Paused && !m_bMiniMode && !m_pMircastShowWidget->isVisible()";
            if (CompositingManager::get().platform() == Platform::X86) {
                qCDebug(appMovie) << "CompositingManager::get().platform() == Platform::X86";
                m_pAnimationlabel->resize(100, 100);
            } else {
                qCDebug(appMovie) << "CompositingManager::get().platform() != Platform::X86";
                if (!m_bIsWM) {
                    qCDebug(appMovie) << "!m_bIsWM";
                    m_pAnimationlabel->resize(100, 100);
                } else {
                    qCDebug(appMovie) << "m_bIsWM";
                    m_pAnimationlabel->resize(200, 200);
                    m_pAnimationlabel->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                }
            }
            m_pAnimationlabel->pauseAnimation();
        }
}

void Platform_MainWindow::onBindingsChanged()
{
    qCDebug(appMovie) << "Platform_MainWindow onBindingsChanged";
    {
        QList<QAction *> listActions = this->actions();
        this->actions().clear();
        for (auto *pAct : listActions) {
            delete pAct;
        }
    }

    ShortcutManager &shortcutManager = ShortcutManager::get();
    vector<QAction *> vecActions = shortcutManager.actionsForBindings();
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
                    return;
                }
            }
            this->menuItemInvoked(pAct);
        });
    }
}

void Platform_MainWindow::updateActionsState()
{
    qCDebug(appMovie) << "Platform_MainWindow updateActionsState";
    //投屏时不处理播放状态切换菜单项是否可用，由右键菜单入口统一处理。
    if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) return;
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
            qCDebug(appMovie) << "ActionFactory::ActionKind::WindowAbove";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            break;
        case ActionFactory::ActionKind::BurstScreenshot:
            qCDebug(appMovie) << "ActionFactory::ActionKind::BurstScreenshot";
            bRet = m_pEngine->duration() > 40;
            break;
        case ActionFactory::ActionKind::MovieInfo:
            qCDebug(appMovie) << "ActionFactory::ActionKind::MovieInfo";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            if (bRet) {
                qCDebug(appMovie) << "bRet";
                bRet = bRet && m_pEngine->playlist().count();
                if (bRet) {
                    qCDebug(appMovie) << "bRet && m_pEngine->playlist().count()";
                    PlayItemInfo playItemInfo = m_pEngine->playlist().currentInfo();
                    bRet = bRet && playItemInfo.loaded;
                }
            }
            break;

        case ActionFactory::ActionKind::HideSubtitle:
        case ActionFactory::ActionKind::SelectSubtitle:
            qCDebug(appMovie) << "ActionFactory::ActionKind::SelectSubtitle";
            bRet = movieInfo.subs.size() > 0;
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
    reflectActionToUI(ActionFactory::ActionKind::SelectTrack);
    reflectActionToUI(ActionFactory::ActionKind::SelectSubtitle);
    qCDebug(appMovie) << "Platform_MainWindow updateActionsState end";
}

/*void MainWindow::syncStaysOnTop()
{
#ifdef USE_DXCB
    static xcb_atom_t atomStateAbove = Utility::internAtom("_NET_WM_STATE_ABOVE");
    auto atoms = Utility::windowNetWMState(static_cast<quint32>(windowHandle()->winId()));

#ifndef __mips__
    bool window_is_above = atoms.contains(atomStateAbove);
    if (window_is_above != m_bWindowAbove) {
        qCInfo(appMovie) << "syncStaysOnTop: window_is_above" << window_is_above;
        requestAction(ActionFactory::WindowAbove);
    }
#endif
#endif
}*/

void Platform_MainWindow::reflectActionToUI(ActionFactory::ActionKind actionKind)
{
    qCDebug(appMovie) << "Platform_MainWindow reflectActionToUI";
    QList<QAction *> listActs;
    switch (actionKind) {
    case ActionFactory::ActionKind::WindowAbove:
    case ActionFactory::ActionKind::ToggleFullscreen:
    case ActionFactory::ActionKind::TogglePlaylist:
    case ActionFactory::ActionKind::HideSubtitle: {
        qCDebug(appMovie) << "WindowAbove ~ HideSubtitle";
        listActs = ActionFactory::get().findActionsByKind(actionKind);
        auto p = listActs.begin();
        while (p != listActs.end()) {
            bool bOld = (*p)->isEnabled();
            (*p)->setEnabled(false);
            if (actionKind == ActionFactory::TogglePlaylist) {
                // here what we read is the last state of playlist
                if (!m_pPlaylist || m_pPlaylist->state() != Platform_PlaylistWidget::Opened) {
                    (*p)->setChecked(false);
                } else {
                    (*p)->setChecked(true);
                }
            } else {
                (*p)->setChecked(!(*p)->isChecked());
            }
            (*p)->setEnabled(bOld);
            ++p;
        }
        break;
    }

    //迷你模式下判断是否全屏，恢复菜单状态 by zhuyuliang
    case ActionFactory::ActionKind::ToggleMiniMode: {
        qCDebug(appMovie) << "ToggleMiniMode";
        listActs = ActionFactory::get().findActionsByKind(actionKind);
        auto p = listActs[0];

        QAction *pAct = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::ToggleFullscreen)[0];
        bool bFlag = pAct->isChecked();
        if (bFlag) {
            pAct->setChecked(false);
        }

        p->setEnabled(false);
        p->setChecked(!p->isChecked());
        p->setEnabled(true);
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        qCDebug(appMovie) << "ChangeSubCodepage";
        //mpv未初始化时返回默认值auto
        QString sCodePage;
        sCodePage = m_pEngine->subCodepage();
        qCInfo(appMovie) << "codepage" << sCodePage;
        listActs = ActionFactory::get().findActionsByKind(actionKind);
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
        qCDebug(appMovie) << "SelectTrack ~ SelectSubtitle";
        if (m_pEngine->state() == PlayerEngine::Idle) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle";
            break;
        }

        PlayingMovieInfo pmf = m_pEngine->playingMovieInfo();
        int nId = -1;
        int nIdx = -1;
        if (actionKind == ActionFactory::ActionKind::SelectTrack) {
            qCDebug(appMovie) << "ActionFactory::ActionKind::SelectTrack";
            nId = m_pEngine->aid();
            for (nIdx = 0; nIdx < pmf.audios.size(); nIdx++) {
                if (nId == pmf.audios[nIdx]["id"].toInt()) {
                    break;
                }
            }
        } else if (actionKind == ActionFactory::ActionKind::SelectSubtitle) {
            qCDebug(appMovie) << "ActionFactory::ActionKind::SelectSubtitle";
            nId = m_pEngine->sid();
            for (nIdx = 0; nIdx < pmf.subs.size(); nIdx++) {
                if (nId == pmf.subs[nIdx]["id"].toInt()) {
                    break;
                }
            }
        }

        qCInfo(appMovie) << __func__ << actionKind << "idx = " << nIdx;
        listActs = ActionFactory::get().findActionsByKind(actionKind);
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
        qCDebug(appMovie) << "Stereo ~ OneTimes";
        listActs = ActionFactory::get().findActionsByKind(actionKind);
        auto p = listActs.begin();
        (*p)->setChecked(true);
        break;
    }
    case ActionFactory::ActionKind::DefaultFrame: {
        qCDebug(appMovie) << "DefaultFrame";
        listActs = ActionFactory::get().findActionsByKind(actionKind);
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
        qCDebug(appMovie) << "OrderPlay ~ ListLoop";
        listActs = ActionFactory::get().findActionsByKind(actionKind);
        auto p = listActs.begin();
        (*p)->setChecked(true);
        break;
    }
    default:
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
    qCDebug(appMovie) << "compareBarData";
    QString sFileName1 = QFileInfo(url1.path()).fileName();
    QString sFileName2 = QFileInfo(url2.path()).fileName();
    if (sFileName1.length() > 0 && sFileName2.length() > 0) {
        if (sFileName1[0] < sFileName2[0]) {
            qCDebug(appMovie) << "sFileName1[0] < sFileName2[0],return true";
            return true;
        }
    }
    qCDebug(appMovie) << "sFileName1[0] >= sFileName2[0],return false";
    return false;
}

bool Platform_MainWindow::addCdromPath()
{
    qCDebug(appMovie) << "addCdromPath";
    QStringList strCDMountlist;

    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        qCDebug(appMovie) << "mountFile.open(QIODevice::ReadOnly) == false";
        return false;
    }
    do {
        QString strLine = mountFile.readLine();
        if (strLine.indexOf("/dev/sr") != -1 || strLine.indexOf("/dev/cdrom") != -1) {  //说明存在光盘的挂载。
            strCDMountlist.append(strLine.split(" ").at(1));        //A B C 这样的格式，取中间的
        }
    } while (!mountFile.atEnd());
    mountFile.close();

    if (strCDMountlist.size() == 0)
        qCDebug(appMovie) << "strCDMountlist.size() == 0";
        return false;

    play({strCDMountlist[0]});
    qCDebug(appMovie) << "addCdromPath end,return true";
    return true;
}

void Platform_MainWindow::loadPlayList()
{
    qCDebug(appMovie) << "Platform_MainWindow loadPlayList";
    m_pPlaylist = nullptr;
    m_pPlaylist = new Platform_PlaylistWidget(this, m_pEngine);
    m_pPlaylist->hide();
    m_pToolbox->setPlaylist(m_pPlaylist);
    m_pEngine->getplaylist()->loadPlaylist();
    m_pToolbox->initThumbThread();

    play(m_listOpenFiles);
    qCDebug(appMovie) << "Platform_MainWindow loadPlayList end";
}

void Platform_MainWindow::setOpenFiles(QStringList &list)
{
    qCDebug(appMovie) << "Platform_MainWindow setOpenFiles";
    m_listOpenFiles = list;
}

QString Platform_MainWindow::padLoadPath()
{
    qCDebug(appMovie) << "Platform_MainWindow padLoadPath";
    QString sLoadPath = Settings::get().generalOption("pad_load_path").toString();
    QDir lastDir(sLoadPath);
    if (sLoadPath.isEmpty() || !lastDir.exists()) {
        qCDebug(appMovie) << "sLoadPath.isEmpty() || !lastDir.exists()";
        sLoadPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir newLastDir(sLoadPath);
        if (!newLastDir.exists()) {
            qCDebug(appMovie) << "!newLastDir.exists()";
            sLoadPath = QDir::currentPath();
        }
    }

    qCDebug(appMovie) << "Platform_MainWindow padLoadPath end,return" << sLoadPath;
    return sLoadPath;
}

#ifdef USE_TEST
void Platform_MainWindow::testCdrom()
{
    qCDebug(appMovie) << "Platform_MainWindow testCdrom";
    this->addCdromPath();
    diskRemoved("sd3/uos");
    sleepStateChanged(true);
    sleepStateChanged(false);
    subtitleMatchVideo("/data/home/uos/Videos/subtitle/Hachiko.A.Dog's.Story.ass");
}
void Platform_MainWindow::setCurrentHwdec(QString str)
{
    qCDebug(appMovie) << "Platform_MainWindow setCurrentHwdec";
    m_sCurrentHwdec = str;
}
#endif

void Platform_MainWindow::mipsShowFullScreen()
{
    qCDebug(appMovie) << "Platform_MainWindow mipsShowFullScreen";
    ensurePolished();
    // 保留 WindowMinimized 旧状态标识
    setWindowState(windowState() | Qt::WindowFullScreen);
    setVisible(true);

    // 全屏时设置绕过合成器
    if (windowHandle()) {
        Utility::setBypassCompositor(windowHandle()->winId(), true);
    }
}

void Platform_MainWindow::menuItemInvoked(QAction *pAction)
{
    qCDebug(appMovie) << "Platform_MainWindow menuItemInvoked";
    ActionFactory::ActionKind actionKind = ActionFactory::actionKind(pAction);
    if (actionKind == dmr::ActionFactory::Invalid || !m_pEngine || !m_pPlaylist) {  //如果未初始化触发快捷键会导致崩溃
        qCDebug(appMovie) << "ActionFactory::ActionKind::Invalid || !m_pEngine || !m_pPlaylist, return";
        return;
    }
    bool bIsShortcut = ActionFactory::isActionFromShortcut(pAction);
    if (ActionFactory::actionHasArgs(pAction)) {
        qCDebug(appMovie) << "ActionFactory::actionHasArgs(pAction)";
        requestAction(actionKind, !bIsShortcut, ActionFactory::actionArgs(pAction), bIsShortcut);
    } else {
        QVariant var = pAction->property("kind");
        if (var == ActionFactory::ActionKind::Settings) {
            qCDebug(appMovie) << "var == ActionFactory::ActionKind::Settings";
            requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
        } else {
            if (m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened) {
                qCDebug(appMovie) << "m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened";
                BindingMap bdMap = ShortcutManager::get().map();
                QHash<QKeySequence, ActionFactory::ActionKind>::const_iterator iter = bdMap.constBegin();
                bool bIsiter = false;
                while (iter != bdMap.constEnd()) {
                    if (iter.value() == actionKind) {
                        qCDebug(appMovie) << "iter.value() == actionKind";
                        bIsiter = true;
                        if ((iter.key() == QKeySequence("Return")
                                || iter.key() == QKeySequence("Enter")
                                || iter.key() == QKeySequence("Up")
                                || iter.key() == QKeySequence("Down")) && bIsShortcut) {
                            if (iter.key() == QKeySequence("Up") || iter.key() == QKeySequence("Down")) {
                                int key;
                                if (iter.key() == QKeySequence("Up")) {
                                    qCDebug(appMovie) << "iter.key() == QKeySequence(\"Up\")";
                                    key = Qt::Key_Up;
                                } else {
                                    qCDebug(appMovie) << "iter.key() == QKeySequence(\"Down\")";
                                    key = Qt::Key_Down;
                                }
                                m_pPlaylist->updateSelectItem(key);
                            }
                            break;
                        }
                        qCDebug(appMovie) << "requestAction";
                        requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
                        break;
                    }
                    ++iter;
                }
                if (bIsiter == false) {
                    qCDebug(appMovie) << "bIsiter == false";
                    requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
                }
            } else {
                qCDebug(appMovie) << "m_pPlaylist->state() != Platform_PlaylistWidget::State::Opened";
                requestAction(actionKind, !bIsShortcut, {0}, bIsShortcut);
            }
        }
    }
    //菜单操作完成后，标题栏获取焦点
    m_pTitlebar->setFocus();
}

bool Platform_MainWindow::isActionAllowed(ActionFactory::ActionKind actionKind, bool fromUI, bool isShortcut)
{
    qCDebug(appMovie) << "Platform_MainWindow isActionAllowed";
    if (m_bInBurstShootMode) {
        return false;
    }

    if (m_bMiniMode) {
        if (fromUI || isShortcut) {
            qCDebug(appMovie) << "fromUI || isShortcut";
            switch (actionKind) {
            case ActionFactory::ToggleFullscreen:
            case ActionFactory::TogglePlaylist:
            case ActionFactory::BurstScreenshot:
                qCDebug(appMovie) << "ToggleFullscreen ~ TogglePlaylist ~ BurstScreenshot return false";
                return false;

            case ActionFactory::ToggleMiniMode:
                qCDebug(appMovie) << "ToggleMiniMode return true";
                return true;

            default:
                break;
            }
        }
    }

    if (isMaximized()) {
        qCDebug(appMovie) << "isMaximized()";
        switch (actionKind) {
        case ActionFactory::ToggleMiniMode:
            qCDebug(appMovie) << "ToggleMiniMode return true";
            return true;
        default:
            break;
        }
    }

    if (isShortcut) {
        qCDebug(appMovie) << "isShortcut";
        PlayingMovieInfo pmf = m_pEngine->playingMovieInfo();
        bool bRet = true;//cppcheck 误报
        switch (actionKind) {
        case ActionFactory::Screenshot:
        case ActionFactory::ToggleMiniMode:
        case ActionFactory::MatchOnlineSubtitle:
        case ActionFactory::BurstScreenshot:
            qCDebug(appMovie) << "Screenshot ~ ToggleMiniMode ~ MatchOnlineSubtitle ~ BurstScreenshot";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            break;

        case ActionFactory::MovieInfo:
            qCDebug(appMovie) << "MovieInfo";
            bRet = m_pEngine->state() != PlayerEngine::Idle;
            if (bRet) {
                qCDebug(appMovie) << "bRet == true";
                bRet = bRet && m_pEngine->playlist().count();
                if (bRet) {
                    qCDebug(appMovie) << "bRet == true && m_pEngine->playlist().count()";
                    auto pif = m_pEngine->playlist().currentInfo();
                    bRet = bRet && pif.loaded;
                }
            }
            break;

        case ActionFactory::HideSubtitle:
        case ActionFactory::SelectSubtitle:
            qCDebug(appMovie) << "HideSubtitle ~ SelectSubtitle";
            bRet = pmf.subs.size() > 0;
            break;
        default:
            break;
        }
        if (!bRet) {
            qCDebug(appMovie) << "bRet == false,return bRet";
            return bRet;
        }
    }
    qCDebug(appMovie) << "return true";
    return true;
}

void Platform_MainWindow::requestAction(ActionFactory::ActionKind actionKind, bool bFromUI,
                               QList<QVariant> args, bool bIsShortcut)
{
    qCInfo(appMovie) << "actionKind = " << actionKind << "fromUI " << bFromUI << (bIsShortcut ? "shortcut" : "");

    if (!m_pToolbox->getbAnimationFinash() || m_bStartAnimation) {
        qCDebug(appMovie) << "!m_pToolbox->getbAnimationFinash() || m_bStartAnimation";
        return;
    }

    if (!isActionAllowed(actionKind, bFromUI, bIsShortcut)) {
        qCInfo(appMovie) << actionKind << "disallowed";
        return;
    }

    switch (actionKind) {
    case ActionFactory::ActionKind::Exit:
        qCDebug(appMovie) << "ActionFactory::ActionKind::Exit";
        qApp->quit();
        break;

    case ActionFactory::ActionKind::OpenCdrom: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OpenCdrom";
        QString sDev = dmr::CommandLineManager::get().dvdDevice();
        if (sDev.isEmpty()) {
            sDev = probeCdromDevice();
        }
        if (sDev.isEmpty()) {
            qCDebug(appMovie) << "sDev.isEmpty()";
            m_pCommHintWid->updateWithMessage(tr("Cannot play the disc"));
            break;
        }

        if (addCdromPath() == false) {
            qCDebug(appMovie) << "addCdromPath() == false";
            play({QString("dvd:///%1").arg(sDev)});
        }
        break;
    }

    case ActionFactory::ActionKind::OpenUrl: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OpenUrl";
        UrlDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            QUrl url = dlg.url();
            if (url.isValid()) {
                qCDebug(appMovie) << "url.isValid() == true";
                play({url.toString()});
            } else {
                qCDebug(appMovie) << "url.isValid() == false";
                m_pCommHintWid->updateWithMessage(tr("Parse failed"));
            }
        }
        break;
    }

    case ActionFactory::ActionKind::OpenDirectory: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OpenDirectory";
#ifndef USE_TEST
        QString name = DFileDialog::getExistingDirectory(this, tr("Open folder"),
                                                         lastOpenedPath(),
                                                         DFileDialog::DontResolveSymlinks);
#else
        QString name("/data/source/deepin-movie-reborn/movie");
#endif

        // 玲珑环境处理：转换为播放路径
        if (utils::IsLinglongEnvironment()) {
            const QString convertedPath = utils::ConvertLinglongPathForPlayback(name);
            if (convertedPath != name) {
                name = convertedPath;
            }
        }

        QFileInfo fi(name);
        if (fi.isDir() && fi.exists()) {
            qCDebug(appMovie) << "fi.isDir() && fi.exists()";
            Settings::get().setGeneralOption("last_open_path", fi.path());
            qCDebug(appMovie) << "play({name})";
            play({name});
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFileList: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OpenFileList";
        if (QDateTime::currentMSecsSinceEpoch() - m_pToolbox->getMouseTime() < 500) {
            qCDebug(appMovie) << "QDateTime::currentMSecsSinceEpoch() - m_pToolbox->getMouseTime() < 500";
            return;
        }
        if (m_pEngine->getplaylist()->items().isEmpty() && m_pEngine->getplaylist()->getThumbnailRunning()) {
            qCDebug(appMovie) << "m_pEngine->getplaylist()->items().isEmpty() && m_pEngine->getplaylist()->getThumbnailRunning()";
            return;
        }
        //允许影院打开音乐文件进行播放
#ifndef USE_TEST
        DFileDialog fileDialog;
        QStringList filenames;
        fileDialog.setParent(this);
        fileDialog.setNameFilters({tr("All (*)"), QString("Video (%1)").arg(m_pEngine->video_filetypes.join(" ")),
                                   QString("Audio (%1)").arg(m_pEngine->audio_filetypes.join(" "))});
        fileDialog.selectNameFilter(QString("Video (%1)").arg(m_pEngine->video_filetypes.join(" ")));
        fileDialog.setDirectory(lastOpenedPath());
        fileDialog.setFileMode(QFileDialog::ExistingFiles);

        if (fileDialog.exec() == QDialog::Accepted) {
            filenames = fileDialog.selectedFiles();
            qCDebug(appMovie) << "fileDialog.exec() == QDialog::Accepted";

            // 玲珑环境处理：转换为播放路径
            if (utils::IsLinglongEnvironment()) {
                for (int i = 0; i < filenames.size(); i++) {
                    const QString convertedPath = utils::ConvertLinglongPathForPlayback(filenames[i]);
                    if (convertedPath != filenames[i]) {
                        filenames[i] = convertedPath;
                    }
                }
            }
        } else {
            qCDebug(appMovie) << "fileDialog.exec() == QDialog::Rejected";
            break;
        }
#else
        QStringList filenames;
        filenames << QString("/data/source/deepin-movie-reborn/movie/demo.mp4")\
                  << QString("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3");
#endif
        if (filenames.size()) {
            qCDebug(appMovie) << "filenames.size()";
            QFileInfo fileInfo(filenames[0]);
            if (fileInfo.exists()) {
                Settings::get().setGeneralOption("last_open_path", fileInfo.path());
            }
            play(filenames);
        }
        break;
    }

    case ActionFactory::ActionKind::OpenFile: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OpenFile";
        DFileDialog fileDialog(this);
        QStringList filename;
        QString strVideoTypes = m_pEngine->video_filetypes.join(" ");
        QString strAudioTypes = m_pEngine->audio_filetypes.join(" ");
        if(!CompositingManager::isMpvExists()) {
            qCDebug(appMovie) << "CompositingManager::isMpvExists()";
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
            filename = fileDialog.selectedFiles();
            qCDebug(appMovie) << "fileDialog.exec() == QDialog::Accepted";

            // 玲珑环境处理：转换为播放路径
            if (utils::IsLinglongEnvironment()) {
                for (int i = 0; i < filename.size(); i++) {
                    const QString convertedPath = utils::ConvertLinglongPathForPlayback(filename[i]);
                    if (convertedPath != filename[i]) {
                        filename[i] = convertedPath;
                    }
                }
            }
        } else {
            break;
        }
        QFileInfo fileInfo(filename[0]);
        if (fileInfo.exists()) {
            qCDebug(appMovie) << "fileInfo.exists()";
            Settings::get().setGeneralOption("last_open_path", fileInfo.path());

            play({filename[0]});
        }
        break;
    }

    case ActionFactory::ActionKind::StartPlay: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::StartPlay";
        if (m_pEngine->playlist().count() == 0) {
            qCDebug(appMovie) << "m_pEngine->playlist().count() == 0";
            requestAction(ActionFactory::ActionKind::OpenFileList);
        } else {
            qCDebug(appMovie) << "playlist.count() != 0";
            if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
                qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Idle";
                //先显示分辨率，再显示静音
                QSize sz = geometry().size();
                auto msg = QString("%1x%2").arg(sz.width()).arg(sz.height());
                QTimer::singleShot(500, [ = ]() {
                    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
                        qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle,updateWithMessage";
                        m_pCommHintWid->updateWithMessage(msg);
                    }
                });
                QVariant panscan = m_pEngine->getBackendProperty("panscan");
                if (panscan.isNull() && Settings::get().isSet(Settings::ResumeFromLast)) {
                    qCDebug(appMovie) << "panscan.isNull() && Settings::get().isSet(Settings::ResumeFromLast)";
                    int restore_pos = Settings::get().internalOption("playlist_pos").toInt();
                    //Playback when the playlist is not loaded, this will result in the
                    //last exit item without playing, because the playlist has not been
                    //loaded into that file, so adding a thread waiting here.
                    //TODO(xxxxp):It will cause direct opening of the cartoon? May need to optimize Model View
                    while (m_pEngine->getplaylist()->getThumbnailRunning()) {
                        QCoreApplication::processEvents();
                    }
                    qCInfo(appMovie) << "playlist_pos: " << restore_pos << " current: " << m_pEngine->playlist().current();
                    if(m_pEngine->playlist().current() == -1) { //第一次直接启动影院(不是双击视频启动的影院)，点击播放按钮时启动上次退出影院时播放的视频
                        qCDebug(appMovie) << "m_pEngine->playlist().current() == -1";
                        restore_pos = qMax(qMin(restore_pos, m_pEngine->playlist().count() - 1), 0);
                        requestAction(ActionFactory::ActionKind::GotoPlaylistSelected, false, {restore_pos});
                    }
                } else {
                    qCDebug(appMovie) << "panscan.isNull() == false";
                    m_pEngine->play();
                }
            }
        }
        break;
    }

    case ActionFactory::ActionKind::EmptyPlaylist: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::EmptyPlaylist";
        //play list context menu empty playlist
        m_pEngine->clearPlaylist();
        break;
    }

    case ActionFactory::ActionKind::TogglePlaylist: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::TogglePlaylist";
        if (m_bStartMini || m_bMiniMode) {
            qCDebug(appMovie) << "m_bStartMini || m_bMiniMode";
            return;
        }
        //快捷键操作不置回焦点
        if (bIsShortcut) {
            qCDebug(appMovie) << "bIsShortcut";
            m_pToolbox->clearPlayListFocus();
        }
        /* The focus of the clear list button when the playlist is raised is also handled here.
         * Cancel the focus of the shortcut key when it is raised to avoid this problem
         */
        m_bStartAnimation = true;
        QTimer::singleShot(150, [ = ]() {    //延时是为了解决在窗口变化同时操作时，因窗口size未确定导致显示异常
            qCDebug(appMovie) << "QTimer::singleShot(150, [ = ]())";
            m_bStartAnimation = false;
            if (bIsShortcut && toolbox()->getListBtnFocus()) {
                qCDebug(appMovie) << "bIsShortcut && toolbox()->getListBtnFocus()";
                setFocus();
            }
            if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Closed && !m_pToolbox->isVisible()) {
                qCDebug(appMovie) << "m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Closed && !m_pToolbox->isVisible()";
                m_pToolbox->show();
            }
            if (m_pPlaylist) {
                m_pPlaylist->togglePopup(bIsShortcut);
            }
            if (!bFromUI) {
                qCDebug(appMovie) << "!bFromUI";
                reflectActionToUI(actionKind);
            }
            this->resumeToolsWindow();
        });

        break;
    }

    case ActionFactory::ActionKind::ToggleMiniMode: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ToggleMiniMode";
        //Когда вы быстро переключаете мини-режим, переключение полноэкранной
        //задержки не до тех пор, пока есть случай, когда размер окна ненормальный.
        //
        //Обратите внимание, что это приведет к переключению на мини-режим,
        //быстро нажмите кнопку Mini Mode, не снимая мини-режима
        //Когда возникает эта проблема, вы можете попытаться сократить
        //время задержки (пожалуйста, синхронизируйте мини-режим и полную задержку операции экрана)
        //Но будьте осторожны, эта операция может вызвать другие проблемы.
        //Поддерживается xxxxp.
        if (QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600 || m_bMouseMoved) {
            qCDebug(appMovie) << "QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600 || m_bMouseMoved";
            return;
        }

        m_pAnimationlabel->hide();

        int nDelayTime = 0;
        if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Opened) {
            requestAction(ActionFactory::TogglePlaylist);
            nDelayTime = 500;
        }

        m_bStartMini = true;

        QTimer::singleShot(nDelayTime, this, [ = ] {
            qCDebug(appMovie) << "QTimer::singleShot(nDelayTime, this, [ = ]())";
            if (m_pFullScreenTimeLabel && !isFullScreen())
            {
                qCDebug(appMovie) << "m_pFullScreenTimeLable && !isFullScreen()";
                m_pFullScreenTimeLabel->close();
            }
            if (!bFromUI)
            {
                qCDebug(appMovie) << "!bFromUI";
                reflectActionToUI(actionKind);
            }
            toggleUIMode();
        });
        //Prevent abnormal focus position due to window state changes
        setFocus();
        break;
    }

    case ActionFactory::ActionKind::MovieInfo: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::MovieInfo";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            //fix 107355
            //Add a mouse display to prevent the target is hidden
            qApp->setOverrideCursor(Qt::ArrowCursor);
            MovieInfoDialog mid(m_pEngine->playlist().currentInfo(), this);
            mid.exec();
        }
        break;
    }

    case ActionFactory::ActionKind::WindowAbove: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::WindowAbove";
        m_bWindowAbove = !m_bWindowAbove;
        my_setStayOnTop(this, m_bWindowAbove);
        if (!bFromUI) {
            qCDebug(appMovie) << "!bFromUI";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::QuitFullscreen: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::QuitFullscreen";
        if (QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600) {
            return;
        }
        if (!m_pToolbox->getVolSliderIsHided()) {
            m_pToolbox->setVolSliderHide();       // esc降下音量条
            break;
        }

        if (m_bMiniMode) {
            if (!bFromUI) {
                qCDebug(appMovie) << "!bFromUI";
                reflectActionToUI(ActionFactory::ToggleMiniMode);
            }
            toggleUIMode();
        } else if (isFullScreen()) {
            requestAction(ActionFactory::ToggleFullscreen);
            if (m_pFullScreenTimeLabel && !isFullScreen()) {
                qCDebug(appMovie) << "m_pFullScreenTimeLabel && !isFullScreen()";
                m_pFullScreenTimeLabel->close();
            }
        } else {
            //当焦点在播放列表上按下Esc键，播放列表收起，焦点回到列表按钮上
            if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Opened) {
                qCDebug(appMovie) << "m_pPlaylist->state() == Platform_PlaylistWidget::Opened";
                m_pToolbox->playlistClosedByEsc();
            }
        }
        break;
    }

    case ActionFactory::ActionKind::ToggleFullscreen: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ToggleFullscreen";
        if (QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600) {
            qCDebug(appMovie) << "QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 600";
            return;
        } else {
            qCDebug(appMovie) << "QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime >= 600";
            m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
        }

        //音量条控件打开时全屏位置异常，全屏时关掉音量条
        m_pAnimationlabel->hide();
        m_pToolbox->closeAnyPopup();

        if (isFullScreen()) {
            qCDebug(appMovie) << "isFullScreen()";
            // 取消全屏前，先取消绕过合成器
            if (windowHandle()) {
                Utility::setBypassCompositor(windowHandle()->winId(), false);
            }

            // 和 mainwindow.cpp 保持一致，在 mipsShowFullScreen() 时保留 Qt::WindowMaximized 的状态以正常切换。
            setWindowState(windowState() & ~Qt::WindowFullScreen);

            if (m_bMaximized) {
                qCDebug(appMovie) << "m_bMaximized";
                showMaximized();
            } else {
                qCDebug(appMovie) << "!m_bMaximized";
                if (m_lastRectInNormalMode.isValid() && !m_bMiniMode && !isMaximized()) {
                    qCDebug(appMovie) << "m_lastRectInNormalMode.isValid() && !m_bMiniMode && !isMaximized()";
                    setGeometry(m_lastRectInNormalMode);
                    move(m_lastRectInNormalMode.x(), m_lastRectInNormalMode.y());
                    resize(m_lastRectInNormalMode.width(), m_lastRectInNormalMode.height());
                }
            }

            if (m_pFullScreenTimeLabel && !isFullScreen()) {
                qCDebug(appMovie) << "m_pFullScreenTimeLabel && !isFullScreen()";
                m_pFullScreenTimeLabel->close();
            }
        } else {
            qCDebug(appMovie) << "!isFullScreen()";
            //可能存在更好的方法（全屏后更新toolbox状态），后期修改
            if (!m_pToolbox->getbAnimationFinash()) {
                qCDebug(appMovie) << "!m_pToolbox->getbAnimationFinash()";
                return;
            }
            qCDebug(appMovie) << "m_pToolbox->getbAnimationFinash()";
            m_bMaximized = isMaximized();  // 记录全屏前是否是最大化窗口
            mipsShowFullScreen();
            if (m_pProgIndicator && isFullScreen()) {
                qCDebug(appMovie) << "m_pProgIndicator && isFullScreen()";
                QRect screenGeo = windowHandle()->screen()->geometry();
                m_pProgIndicator->move(screenGeo.width() + screenGeo.x() - m_pProgIndicator->width() - 18, 8 + screenGeo.y());
                if (CompositingManager::get().platform() != Platform::Mips) {
                    if (m_pEngine->state() != PlayerEngine::CoreState::Idle && m_pFullScreenTimeLabel) {
                        int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
                        pixelsWidth = qMax(117, pixelsWidth);
                        m_pFullScreenTimeLabel->setGeometry(screenGeo.width() + screenGeo.x() - pixelsWidth - 60, 40 + screenGeo.y(), pixelsWidth + 60, 36);
                        if(m_bShowTime) {
                            m_pFullScreenTimeLabel->show();
                        }
                    }
                }
            }
        }
        if (!bFromUI) {
            qCDebug(appMovie) << "!bFromUI";
            reflectActionToUI(actionKind);
        }
        if (isFullScreen()) {
            qCDebug(appMovie) << "isFullScreen()";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
             m_pAnimationlabel->move(QPoint(QApplication::desktop()->availableGeometry().width() / 2 - m_pAnimationlabel->width() / 2,
                                  QApplication::desktop()->availableGeometry().height() / 2 - m_pAnimationlabel->height() / 2));
#else
            QScreen *screen = window()->screen();
            if (screen) {
                QRect availableGeometry = screen->availableGeometry();
                m_pAnimationlabel->move(QPoint(availableGeometry.width() / 2 - m_pAnimationlabel->width() / 2,
                                            availableGeometry.height() / 2 - m_pAnimationlabel->height() / 2));
            }
#endif
        } else {
            qCDebug(appMovie) << "!isFullScreen()";
            m_pAnimationlabel->move(QPoint((width() - m_pAnimationlabel->width()) / 2,
                                           (height() - m_pAnimationlabel->height()) / 2));
        }

        QTimer::singleShot(200, [ = ]() {
            qCDebug(appMovie) << "QTimer::singleShot(200, [ = ]())";
            activateWindow();
            //Set focus back to main window after full screen, Prevent focus from going to the toolbar
            setFocus();
        });
        break;
    }

    case ActionFactory::ActionKind::PlaylistRemoveItem: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::PlaylistRemoveItem";
        if (m_pPlaylist) {
            m_pPlaylist->removeClickedItem(bIsShortcut);
        }
        break;
    }

    case ActionFactory::ActionKind::PlaylistOpenItemInFM: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::PlaylistOpenItemInFM";
        if (m_pPlaylist) {
            m_pPlaylist->openItemInFM();
        }
        break;
    }

    case ActionFactory::ActionKind::PlaylistItemInfo: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::PlaylistItemInfo";
        if (m_pPlaylist) {
            m_pPlaylist->showItemInfo();
        }
        break;
    }

    case ActionFactory::ActionKind::ClockwiseFrame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ClockwiseFrame";
        auto old = m_pEngine->videoRotation();
        m_pEngine->setVideoRotation((old + 90) % 360);
        break;
    }
    case ActionFactory::ActionKind::CounterclockwiseFrame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::CounterclockwiseFrame";
        auto old = m_pEngine->videoRotation();
        m_pEngine->setVideoRotation(((old - 90) + 360) % 360);
        break;
    }

    case ActionFactory::ActionKind::OrderPlay: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OrderPlay";
        Settings::get().setInternalOption("playmode", 0);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::OrderPlay);
        break;
    }
    case ActionFactory::ActionKind::ShufflePlay: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ShufflePlay";
        Settings::get().setInternalOption("playmode", 1);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::ShufflePlay);
        break;
    }
    case ActionFactory::ActionKind::SinglePlay: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SinglePlay";
        Settings::get().setInternalOption("playmode", 2);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::SinglePlay);
        break;
    }
    case ActionFactory::ActionKind::SingleLoop: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SingleLoop";
        Settings::get().setInternalOption("playmode", 3);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::SingleLoop);
        break;
    }
    case ActionFactory::ActionKind::ListLoop: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ListLoop";
        Settings::get().setInternalOption("playmode", 4);
        m_pEngine->playlist().setPlayMode(PlaylistModel::PlayMode::ListLoop);
        break;
    }

    case ActionFactory::ActionKind::ZeroPointFiveTimes: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ZeroPointFiveTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 0.5;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OneTimes: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OneTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointTwoTimes: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OnePointTwoTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.2;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::OnePointFiveTimes: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::OnePointFiveTimes";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.5;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }
    case ActionFactory::ActionKind::Double: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Double";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 2.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::Stereo: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Stereo";
        m_pEngine->changeSoundMode(Backend::SoundMode::Stereo);
        m_pCommHintWid->updateWithMessage(tr("Stereo"));
        break;
    }
    case ActionFactory::ActionKind::LeftChannel: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::LeftChannel";
        m_pEngine->changeSoundMode(Backend::SoundMode::Left);
        m_pCommHintWid->updateWithMessage(tr("Left channel"));
        break;
    }
    case ActionFactory::ActionKind::RightChannel: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::RightChannel";
        m_pEngine->changeSoundMode(Backend::SoundMode::Right);
        m_pCommHintWid->updateWithMessage(tr("Right channel"));
        break;
    }

    case ActionFactory::ActionKind::DefaultFrame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::DefaultFrame";
        m_pEngine->setVideoAspect(-1.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio4x3Frame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Ratio4x3Frame";
        m_pEngine->setVideoAspect(4.0 / 3.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x9Frame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Ratio16x9Frame";
        m_pEngine->setVideoAspect(16.0 / 9.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio16x10Frame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Ratio16x10Frame";
        m_pEngine->setVideoAspect(16.0 / 10.0);
        break;
    }
    case ActionFactory::ActionKind::Ratio185x1Frame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Ratio185x1Frame";
        m_pEngine->setVideoAspect(1.85);
        break;
    }
    case ActionFactory::ActionKind::Ratio235x1Frame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Ratio235x1Frame";
        m_pEngine->setVideoAspect(2.35);
        break;
    }

    case ActionFactory::ActionKind::ToggleMute: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ToggleMute";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat() && !m_pEngine->currFileIsAudio()";
            slotUnsupported();
        } else {
            qCDebug(appMovie) << "else > m_pToolbox->changeMuteState()";
            m_pToolbox->changeMuteState();
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeUp: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::VolumeUp";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat() && !m_pEngine->currFileIsAudio()";
            slotUnsupported();
        } else {
            //使用鼠标滚轮调节音量时会执行此步骤
            qCDebug(appMovie) << "else > m_pToolbox->calculationStep(m_iAngleDelta)";
            if (m_iAngleDelta != 0) m_pToolbox->calculationStep(m_iAngleDelta);
            m_pToolbox->volumeUp();
            m_iAngleDelta = 0;
        }
        break;
    }

    case ActionFactory::ActionKind::VolumeDown: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::VolumeDown";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()
                && !m_pEngine->currFileIsAudio()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat() && !m_pEngine->currFileIsAudio()";
            slotUnsupported();
        } else {
            //使用鼠标滚轮调节音量时会执行此步骤
            qCDebug(appMovie) << "else > m_pToolbox->calculationStep(m_iAngleDelta)";
            if (m_iAngleDelta != 0) m_pToolbox->calculationStep(m_iAngleDelta);
            m_pToolbox->volumeDown();
            m_iAngleDelta = 0;
        }
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistSelected: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::GotoPlaylistSelected";
        m_pEngine->playSelected(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistNext: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::GotoPlaylistNext";
        //防止焦点在上/下一曲按钮上切换时焦点跳到下一个按钮上
        //下同
        setFocus();
        if (m_bIsFree == false) {
            qCDebug(appMovie) << "m_bIsFree == false, return";
            return ;
        }

        m_bIsFree = false;
        if (isFullScreen() || isMaximized()) {
            qCDebug(appMovie) << "isFullScreen() || isMaximized()";
            m_bMovieSwitchedInFsOrMaxed = true;
        }
        m_pEngine->next();

        break;
    }

    case ActionFactory::ActionKind::GotoPlaylistPrev: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::GotoPlaylistPrev";
        setFocus();
        if (m_bIsFree == false) {
            qCDebug(appMovie) << "m_bIsFree == false, return";
            return ;
        }

        m_bIsFree = false;
        if (isFullScreen() || isMaximized()) {
            qCDebug(appMovie) << "isFullScreen() || isMaximized()";
            m_bMovieSwitchedInFsOrMaxed = true;
        }
        m_pEngine->prev();
        break;
    }

    case ActionFactory::ActionKind::SelectTrack: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SelectTrack";
        Q_ASSERT(args.size() == 1);
        m_pEngine->selectTrack(args[0].toInt());
        m_pCommHintWid->updateWithMessage(tr("Track: %1").arg(args[0].toInt() + 1));
        if (!bFromUI) {
            qCDebug(appMovie) << "!bFromUI";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::MatchOnlineSubtitle: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::MatchOnlineSubtitle";
        m_pEngine->loadOnlineSubtitle(m_pEngine->playlist().currentInfo().url);
        break;
    }

    case ActionFactory::ActionKind::SelectSubtitle: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SelectSubtitle";
        Q_ASSERT(args.size() == 1);
        m_pEngine->selectSubtitle(args[0].toInt());
        if (!bFromUI) {
            qCDebug(appMovie) << "!bFromUI";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::ChangeSubCodepage: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ChangeSubCodepage";
        Q_ASSERT(args.size() == 1);
        m_pEngine->setSubCodepage(args[0].toString());
        if (!bFromUI) {
            qCDebug(appMovie) << "!bFromUI";
            reflectActionToUI(actionKind);
        }
        break;
    }

    case ActionFactory::ActionKind::HideSubtitle: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::HideSubtitle";
        m_pEngine->toggleSubtitle();
        break;
    }

    case ActionFactory::ActionKind::SubDelay: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SubDelay";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
            slotUnsupported();
            break;
        }
        if (m_pEngine->playingMovieInfo().subs.isEmpty()) {
            qCDebug(appMovie) << "m_pEngine->playingMovieInfo().subs.isEmpty()";
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
        qCDebug(appMovie) << "ActionFactory::ActionKind::SubForward";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
            slotUnsupported();
            break;
        }
        if (m_pEngine->playingMovieInfo().subs.isEmpty()) {
            qCDebug(appMovie) << "m_pEngine->playingMovieInfo().subs.isEmpty()";
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
        qCDebug(appMovie) << "ActionFactory::ActionKind::AccelPlayback";
        adjustPlaybackSpeed(ActionFactory::ActionKind::AccelPlayback);
        break;
    }

    case ActionFactory::ActionKind::DecelPlayback: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::DecelPlayback";
        adjustPlaybackSpeed(ActionFactory::ActionKind::DecelPlayback);
        break;
    }

    case ActionFactory::ActionKind::ResetPlayback: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ResetPlayback";
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle";
            m_dPlaySpeed = 1.0;
            m_pEngine->setPlaySpeed(m_dPlaySpeed);
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
            m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
        }
        break;
    }

    case ActionFactory::ActionKind::LoadSubtitle: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::LoadSubtitle";
        QStringList filename;
#ifndef USE_TEST
        DFileDialog fileDialog(this);
        fileDialog.setNameFilter(tr("Subtitle (*.ass *.aqt *.jss *.gsub *.ssf *.srt *.sub *.ssa *.smi *.usf *.idx)","All (*)"));
        fileDialog.setDirectory(lastOpenedPath());

        if (fileDialog.exec() == QDialog::Accepted) {
            qCDebug(appMovie) << "fileDialog.exec() == QDialog::Accepted";
            filename = fileDialog.selectedFiles();
        } else {
            qCDebug(appMovie) << "fileDialog.exec() == QDialog::Rejected";
            break;
        }
#else
        filename = QStringList({"/data/source/deepin-movie-reborn/Hachiko.A.Dog's.Story.ass"});
#endif
        if (QFileInfo(filename[0]).exists()) {
            qCDebug(appMovie) << "QFileInfo(filename[0]).exists()";
            if (m_pEngine->state() == PlayerEngine::Idle) {
                qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle";
                subtitleMatchVideo(filename[0]);
            } else {
                qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::Idle";
                auto success = m_pEngine->loadSubtitle(QFileInfo(filename[0]));
                m_pCommHintWid->updateWithMessage(success ? tr("Load successfully") : tr("Load failed"));
            }
        } else {
            qCDebug(appMovie) << "!QFileInfo(filename[0]).exists()";
            m_pCommHintWid->updateWithMessage(tr("Load failed"));
        }
        break;
    }

    case ActionFactory::ActionKind::TogglePause: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::TogglePause";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            m_pToolbox->getMircast()->slotPauseDlnaTp();
            break;
        }
        if (windowState() == Qt::WindowFullScreen && QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 500) {
            qCDebug(appMovie) << "windowState() == Qt::WindowFullScreen && QDateTime::currentMSecsSinceEpoch() - m_nFullscreenTime < 500";
            return;
        } else if(windowState() == Qt::WindowFullScreen) {
            qCDebug(appMovie) << "windowState() == Qt::WindowFullScreen";
            m_nFullscreenTime = QDateTime::currentMSecsSinceEpoch();
        }
        if (m_pEngine->state() == PlayerEngine::Idle && bIsShortcut) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle && bIsShortcut";
            if (m_pEngine->getplaylist()->getthreadstate()) {
                qCInfo(appMovie) << "playlist loadthread is running";
                break;
            }
            requestAction(ActionFactory::StartPlay);
        } else {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Paused";
            if (m_pEngine->state() == PlayerEngine::Paused) {
                qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Paused";
                //startPlayStateAnimation(true);
                if (!m_bMiniMode) {
                    qCDebug(appMovie) << "!m_bMiniMode";
                    if (CompositingManager::get().platform() == Platform::X86) {
                        m_pAnimationlabel->resize(100, 100);
                    } else {
                        qCDebug(appMovie) << "!m_bMiniMode";
                        if (!m_bIsWM) {
                            qCDebug(appMovie) << "!m_bIsWM";
                            m_pAnimationlabel->resize(100, 100);
                        } else {
                            qCDebug(appMovie) << "m_bIsWM";
                            m_pAnimationlabel->resize(200, 200);
                            m_pAnimationlabel->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                        }
                    }
                    m_pAnimationlabel->playAnimation();
                }
                QTimer::singleShot(160, [ = ]() {
                    m_pEngine->pauseResume();
                });
            } else {
                qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::Paused";
                m_pEngine->pauseResume();
            }
        }
        break;
    }

    case ActionFactory::ActionKind::SeekBackward: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SeekBackward";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            qCDebug(appMovie) << "m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
            m_pToolbox->getMircast()->seekMircast(-5);
            break;
        }
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
            slotUnsupported();
        } else {
            qCDebug(appMovie) << "else > m_pEngine->seekBackward(5)";
            m_pEngine->seekBackward(5);
        }
        break;
    }

    case ActionFactory::ActionKind::SeekForward: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SeekForward";
        if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible() ) {
            qCDebug(appMovie) << "m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
            m_pToolbox->getMircast()->seekMircast(5);
            break;
        }
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
            slotUnsupported();
        } else {
            qCDebug(appMovie) << "else > m_pEngine->seekForward(5)";
            m_pEngine->seekForward(5);
        }
        break;
    }

    case ActionFactory::ActionKind::SeekAbsolute: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::SeekAbsolute";
        Q_ASSERT(args.size() == 1);
        m_pEngine->seekAbsolute(args[0].toInt());
        break;
    }

    case ActionFactory::ActionKind::Settings: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Settings";
        handleSettings(initSettings());
        break;
    }

    case ActionFactory::ActionKind::Screenshot: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::Screenshot";
        QImage img = m_pEngine->takeScreenshot();

        QString filePath = Settings::get().screenshotNameTemplate();
        bool bSuccess = false;   //条件编译产生误报(cppcheck)
        if (img.isNull())
            qCInfo(appMovie) << __func__ << "pixmap is null";
        else
            bSuccess = img.save(filePath);

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
        qCDebug(appMovie) << "ActionFactory::ActionKind::GoToScreenshotSolder";
        QString filePath = Settings::get().screenshotLocation();
        qCInfo(appMovie) << __func__ << filePath;
        QDBusInterface iface("org.freedesktop.FileManager1",
                             "/org/freedesktop/FileManager1",
                             "org.freedesktop.FileManager1",
                             QDBusConnection::sessionBus());
        if (iface.isValid()) {
            qCDebug(appMovie) << "iface.isValid()";
            // Convert filepath to URI first.
            const QStringList uris = { filePath };
            qCInfo(appMovie) << "freedesktop.FileManager";
            // StartupId is empty here.
            QDBusPendingCall call = iface.asyncCall("Open", uris);
            Q_UNUSED(call);
        }
        break;
    }

    case ActionFactory::ActionKind::BurstScreenshot: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::BurstScreenshot";
        if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
            qCDebug(appMovie) << "state != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
            slotUnsupported();
        } else {
            qCDebug(appMovie) << "else > startBurstShooting()";
            startBurstShooting();
        }
        break;
    }

    case ActionFactory::ActionKind::ViewShortcut: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::ViewShortcut";
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
            qCDebug(appMovie) << "m_pShortcutViewProcess is null";
            m_pShortcutViewProcess = new QProcess();
        }
        m_pShortcutViewProcess->startDetached("deepin-shortcut-viewer", shortcutString);

        connect(m_pShortcutViewProcess, SIGNAL(finished(int)),
                m_pShortcutViewProcess, SLOT(deleteLater()));
        break;
    }

    case ActionFactory::ActionKind::NextFrame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::NextFrame";
        m_pEngine->nextFrame();

        break;
    }

    case ActionFactory::ActionKind::PreviousFrame: {
        qCDebug(appMovie) << "ActionFactory::ActionKind::PreviousFrame";
        m_pEngine->previousFrame();

        break;
    }

    default:
        break;
    }
}

void Platform_MainWindow::onBurstScreenshot(const QImage &frame, qint64 timestamp)
{
    qCDebug(appMovie) << "Platform_MainWindow::onBurstScreenshot";
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

    qCInfo(appMovie) << m_listBurstShoots.size();
    if (!frame.isNull()) {
        qCDebug(appMovie) << "!frame.isNull()";
        QString sMsg = QString(tr("Taking the screenshots, please wait..."));
        m_pCommHintWid->updateWithMessage(sMsg);

        m_listBurstShoots.append(qMakePair(frame, timestamp));
    }

    if (m_listBurstShoots.size() >= 15 || frame.isNull()) {
        qCDebug(appMovie) << "m_listBurstShoots.size() >= 15 || frame.isNull()";
        disconnect(m_pEngine, &PlayerEngine::notifyScreenshot, this, &Platform_MainWindow::onBurstScreenshot);
        m_pEngine->stopBurstScreenshot();
        m_bInBurstShootMode = false;
        m_pToolbox->setEnabled(true);
        m_pTitlebar->titlebar()->setEnabled(true);
        if (m_pEventListener) m_pEventListener->setEnabled(!m_bMiniMode);

        if (frame.isNull()) {
            qCDebug(appMovie) << "frame.isNull()";
            m_listBurstShoots.clear();
            if (!m_bPausedBeforeBurst) {
                qCDebug(appMovie) << "!m_bPausedBeforeBurst";
                m_pEngine->pauseResume();
            }
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
        qCInfo(appMovie) << "BurstScreenshot done";

        m_listBurstShoots.clear();
        if (!m_bPausedBeforeBurst) {
            qCDebug(appMovie) << "!m_bPausedBeforeBurst";
            m_pEngine->pauseResume();
        }

        if (nRet == QDialog::Accepted) {
            qCDebug(appMovie) << "nRet == QDialog::Accepted";
            QString sPosterPath = burstScreenshotsDialog.savedPosterPath();
            if (QFileInfo::exists(sPosterPath)) {
                qCDebug(appMovie) << "QFileInfo::exists(sPosterPath)";
                const QIcon icon = QIcon(":/resources/icons/short_ok.svg");
                QString sText = QString(tr("The screenshot is saved"));
                popupAdapter(icon, sText);
            } else {
                qCDebug(appMovie) << "!QFileInfo::exists(sPosterPath)";
                const QIcon icon = QIcon(":/resources/icons/short_fail.svg");
                QString sText = QString(tr("Failed to save the screenshot"));
                popupAdapter(icon, sText);
            }
        }
    }
}

void Platform_MainWindow::startBurstShooting()
{
    qCDebug(appMovie) << "Platform_MainWindow::startBurstShooting";
    //Repair 40S video corresponding to the corresponding connected screenshot
    if (m_pEngine->duration() <= 40) return;
    m_bInBurstShootMode = true;
    m_pToolbox->setEnabled(false);
    m_pTitlebar->titlebar()->setEnabled(false);
    if (m_pEventListener) m_pEventListener->setEnabled(false);

    m_bPausedBeforeBurst = m_pEngine->paused();

    connect(m_pEngine, &PlayerEngine::notifyScreenshot, this, &Platform_MainWindow::onBurstScreenshot);
    m_pEngine->burstScreenshot();
}

void Platform_MainWindow::handleSettings(DSettingsDialog *dsd)
{
    qCDebug(appMovie) << "Platform_MainWindow::handleSettings";
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

    if (Settings::get().settings()->getOption("base.decode.select").toInt() != decodeType &&
            (Settings::get().settings()->getOption("base.decode.select").toInt() == 3 || decodeType == 3)) {
        qCDebug(appMovie) << "Settings::get().settings()->getOption(\"base.decode.select\").toInt() != decodeType";

        DDialog msgBox;
        msgBox.setIcon(QIcon(":/resources/icons/warning.svg"));
        msgBox.setMessage(QObject::tr("The custom decoding method needs to be restarted before it can take effect,\nand whether to restart it?"));
        msgBox.addButton(tr("Cancel"), DDialog::ButtonType::ButtonNormal);
        msgBox.addButton(tr("Restart"), true, DDialog::ButtonType::ButtonWarning);
        msgBox.setOnButtonClickedClose(true);
        if (msgBox.exec() == 1) {
            qCDebug(appMovie) << "msgBox.exec() == 1";
            Settings::get().settings()->setOption("set.start.crash", "2");
            qApp->exit();
            QProcess::startDetached(qApp->applicationFilePath(), QStringList() << "--restart");
        } else {
            qCDebug(appMovie) << "msgBox.exec() != 1";
            if (decodeType != 3) {
                qCDebug(appMovie) << "decodeType != 3";
                Settings::get().settings()->setOption("base.decode.select", decodeType);
            }
            Settings::get().settings()->setOption("base.decode.Effect", effectMode);
            Settings::get().settings()->setOption("base.decode.Decodemode", decodeMode);
            Settings::get().settings()->setOption("base.decode.Videoout", voMode);
        }
    } else {
        qCDebug(appMovie) << "Settings::get().settings()->getOption(\"base.decode.select\").toInt() == decodeType";
        if (decodeType == 3) {
            qCDebug(appMovie) << "decodeType == 3";
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
                    qCDebug(appMovie) << "msgBox.exec() == 1";
                    Settings::get().settings()->setOption("set.start.crash", "2");
                    qApp->exit();
                    QProcess::startDetached(qApp->applicationFilePath(), QStringList() << "--restart");
                } else {
                    qCDebug(appMovie) << "msgBox.exec() != 1";
                    if (decodeType != 3) {
                        qCDebug(appMovie) << "decodeType != 3";
                        Settings::get().settings()->setOption("base.decode.select", decodeType);
                    }
                    Settings::get().settings()->setOption("base.decode.Effect", effectMode);
                    Settings::get().settings()->setOption("base.decode.Decodemode", decodeMode);
                    Settings::get().settings()->setOption("base.decode.Videoout", voMode);
                }
            }
        }
    }
    Settings::get().settings()->sync();
}

DSettingsDialog *Platform_MainWindow::initSettings()
{
    qCDebug(appMovie) << "Platform_MainWindow::initSettings";
    if (m_pDSettingDilog)
        return m_pDSettingDilog;
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
        qCDebug(appMovie) << "pSpinBox";
        pSpinBox->setMinimum(8);
    }

    // hack: reset is set to default by QDialog, which makes lineedit's enter
    // press is responded by reset button
    QPushButton *pPushButton = m_pDSettingDilog->findChild<QPushButton *>("SettingsContentReset");
    pPushButton->setDefault(false);
    pPushButton->setAutoDefault(false);

    int decodeType = Settings::get().settings()->getOption(QString("base.decode.select")).toInt();
    if (decodeType != 3) {
        qCDebug(appMovie) << "decodeType != 3";
        QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
        QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
        dynamic_cast<QWidget*>(effectFrame->parent())->hide();
        dynamic_cast<QWidget*>(videoFrame->parent())->hide();
        dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
    } else {
        QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
        QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
        QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
        dynamic_cast<QWidget*>(effectFrame->parent())->show();
        dynamic_cast<QWidget*>(videoFrame->parent())->show();
        dynamic_cast<QWidget*>(decodeFrame->parent())->show();
    }

    connect(&Settings::get(), &Settings::setDecodeModel, this, [=](QString key, QVariant value){
        if (key == "base.decode.select" && value.toInt() == 3){
            DDialog msgBox;
            msgBox.setIcon(QIcon(":/resources/icons/warning.svg"));
            msgBox.setTitle(tr("Professional mode is only for professionals to use"));
            msgBox.setMessage(tr("During the adaptation process, there may be issues such as screen distortion, freezing, stuttering, torn images,\n"
                                 "unsynchronized audio and video, and player crashes. Please use with caution."));
            msgBox.addButton(tr("OK"), true, DDialog::ButtonType::ButtonRecommend);
            msgBox.setOnButtonClickedClose(true);
            msgBox.exec();
        }
        if (key == "base.decode.select") {
            int decodeType = Settings::get().settings()->getOption(QString("base.decode.select")).toInt();
            if (decodeType != 3) {
                QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                dynamic_cast<QWidget*>(effectFrame->parent())->hide();
                dynamic_cast<QWidget*>(videoFrame->parent())->hide();
                dynamic_cast<QWidget*>(decodeFrame->parent())->hide();
            } else {
                QWidget *effectFrame = m_pDSettingDilog->findChild<QWidget*>("effectOptionFrame");
                QWidget *videoFrame = m_pDSettingDilog->findChild<QWidget*>("videoOutOptionFrame");
                QWidget *decodeFrame = m_pDSettingDilog->findChild<QWidget*>("decodeOptionFrame");
                dynamic_cast<QWidget*>(effectFrame->parent())->show();
                dynamic_cast<QWidget*>(videoFrame->parent())->show();
                dynamic_cast<QWidget*>(decodeFrame->parent())->show();
            }
        }
    }, Qt::DirectConnection);

    return m_pDSettingDilog;
}

void Platform_MainWindow::play(const QList<QString> &listFiles)
{
    qCDebug(appMovie) << "Enter play function with" << listFiles.size() << "files";

    // Guard: m_pPlaylist is created asynchronously via QTimer::singleShot(300, loadPlayList).
    // If play() is called before the playlist is ready (e.g. early DBus openFile),
    // defer by storing files so loadPlayList() will play them once the playlist exists.
    if (!m_pPlaylist) {
        qCInfo(appMovie) << "Playlist not yet loaded, deferring playback for" << listFiles.size() << "files";
        if (!listFiles.isEmpty()) {
            m_listOpenFiles = listFiles;
        }
        return;
    }

    QList<QUrl> lstValid;
    QList<QString> lstDir;
    QList<QString> lstFile;

    if (listFiles.isEmpty()) {
        qCDebug(appMovie) << "listFiles.isEmpty()";
        m_pEngine->play();
    }

    if (listFiles.count() == 1 && QUrl(listFiles[0]).scheme().startsWith("dvd")) {
        qCDebug(appMovie) << "listFiles.count() == 1 && QUrl(listFiles[0]).scheme().startsWith(\"dvd\")";
        m_dvdUrl = QUrl(listFiles[0]);
        if (!m_pEngine->addPlayFile(m_dvdUrl)) {
            auto msg = QString(tr("Cannot play the disc"));
            m_pCommHintWid->updateWithMessage(msg);
            qCDebug(appMovie) << "Cannot play the disc, return";
            return;
        } else {
            // todo: Disable toolbar buttons
            auto msg = QString(tr("Reading DVD files..."));
            m_pDVDHintWid->updateWithMessage(msg, true);
        }

        m_pEngine->playByName(m_dvdUrl);
        return;
    }

    for (QString strFile : listFiles) {
        if(QFileInfo(QUrl(strFile).toString()).isDir()){
            lstDir << strFile;
        } else {
            lstFile << strFile;
        }
    }

    lstValid = m_pEngine->addPlayFiles(lstFile);  // 先添加到播放列表再播放

    m_pEngine->blockSignals(true);
    lstValid << m_pEngine->addPlayFiles(lstDir);
    m_pEngine->blockSignals(false);

    if(lstValid.count() > 0) {
        qCDebug(appMovie) << "lstValid.count() > 0";
        if (!isHidden()) {
            qCDebug(appMovie) << "isHidden() == false";
            activateWindow();
        }
        m_pEngine->playByName(lstValid[0]);
        qCDebug(appMovie) << "Playing first valid file";
    } else {
        qCDebug(appMovie) << "No valid files to play";
    }
    qCDebug(appMovie) << "Exit play function";
}

void Platform_MainWindow::updateProxyGeometry()
{
    qCDebug(appMovie) << "Enter updateProxyGeometry function";
    QRect view_rect = rect();

    m_pEngine->resize(view_rect.size());

    if (!m_bMiniMode) {
        qCDebug(appMovie) << "!m_bMiniMode";
        if (m_pTitlebar) {
            qCDebug(appMovie) << "m_pTitlebar is not null";
            m_pTitlebar->setFixedWidth(view_rect.width());
        }

        if (m_pToolbox) {
            qCDebug(appMovie) << "m_pToolbox is not null";
            QRect rfs;
            QPoint mircastPoint;
            if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened) {
                qCDebug(appMovie) << "m_pPlaylist is not null and state is Opened";
                rfs = QRect(5, height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT) - rect().top() - 5,
                            rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_HEIGHT + 7));
            } else {
                qCDebug(appMovie) << "m_pPlaylist is not null and state is not Opened";
                rfs = QRect(5, height() - TOOLBOX_HEIGHT - rect().top() - 5,
                            rect().width() - 10, TOOLBOX_HEIGHT);
            }
            mircastPoint.setX(5 + rect().width() - 10);
            mircastPoint.setY(height() - TOOLBOX_HEIGHT - rect().top() - 5);
#ifdef DTKWIDGET_CLASS_DSizeMode
            if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
                if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::State::Opened) {
                    rfs = QRect(5, height() - (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT) - rect().top() - 5,
                                rect().width() - 10, (TOOLBOX_SPACE_HEIGHT + TOOLBOX_DSIZEMODE_HEIGHT + 7));
                } else {
                    rfs = QRect(5, height() - TOOLBOX_DSIZEMODE_HEIGHT - rect().top() - 5,
                                rect().width() - 10, TOOLBOX_DSIZEMODE_HEIGHT);
                }
                mircastPoint.setX(5 + rect().width() - 10);
                mircastPoint.setY(height() - TOOLBOX_DSIZEMODE_HEIGHT - rect().top() - 5);
            }
#endif
            m_pToolbox->setGeometry(rfs);
            m_pToolbox->updateMircastWidget(mircastPoint);
        }

        if (m_pPlaylist && !m_pPlaylist->toggling()) {
            qCDebug(appMovie) << "m_pPlaylist is not null and toggling is false";
            int toolbox_height = TOOLBOX_HEIGHT;
#ifdef DTKWIDGET_CLASS_DSizeMode
            if (DGuiApplicationHelper::instance()->sizeMode() == DGuiApplicationHelper::CompactMode) {
                qCDebug(appMovie) << "CompactMode";
                toolbox_height = TOOLBOX_DSIZEMODE_HEIGHT;
            }
#endif

#ifndef __sw_64__
            QRect fixed((10), (view_rect.height() - (TOOLBOX_SPACE_HEIGHT + toolbox_height + 5)),
                        view_rect.width() - 20, TOOLBOX_SPACE_HEIGHT);
            if (utils::check_wayland_env()) {
                qCDebug(appMovie) << "Wayland environment detected";
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
    qCDebug(appMovie) << "Exit updateProxyGeometry function";
}

void Platform_MainWindow::suspendToolsWindow()
{
    qCDebug(appMovie) << "Enter suspendToolsWindow function";
    if (!m_bMiniMode) {
        qCDebug(appMovie) << "!m_bMiniMode";
        if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Opened) {
            qCDebug(appMovie) << "m_pPlaylist is not null and state is Opened";
            return;
        }

//        if (qApp->applicationState() == Qt::ApplicationInactive) {

//        } else {
        // menus  are popped up
        // NOTE: menu keeps focus while hidden, so focusWindow is not used
        if (ActionFactory::get().mainContextMenu()->isVisible() ||
                ActionFactory::get().titlebarMenu()->isVisible()) {
            qCDebug(appMovie) << "ActionFactory::get().mainContextMenu()->isVisible() || ActionFactory::get().titlebarMenu()->isVisible()";
            return;
        }

        QPoint cursor = mapFromGlobal(QCursor::pos());
        if (m_pToolbox->isVisible()) {
            qCDebug(appMovie) << "m_pToolbox->isVisible()";
            if (m_pToolbox->getMircast()->isVisible() &&
                    m_pToolbox->getMircast()->geometry().contains(cursor) && !m_bLastIsTouch)
                return;
            qCDebug(appMovie) << "m_pToolbox->getMircast()->isVisible() && m_pToolbox->getMircast()->geometry().contains(cursor) && !m_bLastIsTouch";
            if (insideToolsArea(cursor) && !m_bLastIsTouch) {
                qCDebug(appMovie) << "insideToolsArea(cursor) && !m_bLastIsTouch";
                return;
            }
        } else {
            qCDebug(appMovie) << "!m_pToolbox->isVisible()";
            if (m_pToolbox->geometry().contains(mapFromGlobal(QCursor::pos()))) {
                qCDebug(appMovie) << "m_pToolbox->geometry().contains(mapFromGlobal(QCursor::pos()))";
                return;
            }
        }
//        }

        if (m_pToolbox->anyPopupShown()) {
            qCDebug(appMovie) << "m_pToolbox->anyPopupShown()";
            return;
        }

        if (m_pEngine->state() == PlayerEngine::Idle) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle";
            return;
        }

        if (m_autoHideTimer.isActive()) {
            qCDebug(appMovie) << "m_autoHideTimer.isActive()";
            return;
        }

        if (isFullScreen()) {
            qCDebug(appMovie) << "isFullScreen()";
            if (qApp->focusWindow() == this->windowHandle()) {
                qCDebug(appMovie) << "qApp->focusWindow() == this->windowHandle()";
                static QCursor blankCursor;
                if (blankCursor.shape() != Qt::BlankCursor) {
                    QPixmap emptyPixmap(1, 1);
                    emptyPixmap.fill(Qt::transparent);
                    blankCursor = QCursor(emptyPixmap);
                    qApp->setOverrideCursor(blankCursor);
                    qCDebug(appMovie) << "set blank cursor";
                }
            } else {
                qApp->setOverrideCursor(Qt::ArrowCursor);
            }
        }

        if (m_pToolbox->getbAnimationFinash()) {
            qCDebug(appMovie) << "m_pToolbox->getbAnimationFinash()";
            m_pToolbox->hide();
        }
        //reset focus to mainWindow when the titlebar and toolbox is hedden
        //the tab focus will be re-executed in the order set
        m_pTitlebar->setFocus();
        m_pTitlebar->hide();        //隐藏操作应放在设置焦点后
    } else {
        qCDebug(appMovie) << "m_bMiniMode";
        if (m_autoHideTimer.isActive()) {
            qCDebug(appMovie) << "m_autoHideTimer.isActive()";
            return;
        }

        m_pMiniPlayBtn->hide();
        m_pMiniCloseBtn->hide();
        m_pMiniQuitMiniBtn->hide();
    }
}

void Platform_MainWindow::resumeToolsWindow()
{
    qCDebug(appMovie) << "Enter resumeToolsWindow function";
    if (m_pEngine->state() != PlayerEngine::Idle &&
            qApp->applicationState() == Qt::ApplicationActive) {
        // playlist's previous state was Opened
        if (m_pPlaylist && m_pPlaylist->state() != Platform_PlaylistWidget::Closed &&
                !frameGeometry().contains(QCursor::pos())) {
            qCDebug(appMovie) << "m_pPlaylist is not null and state is not Closed and frameGeometry().contains(QCursor::pos())";
            goto _finish;
        }
    }

    qApp->restoreOverrideCursor();
    setCursor(Qt::ArrowCursor);

    qCDebug(appMovie) << "!m_bMiniMode";
    if (!m_bMiniMode) {
        qCDebug(appMovie) << "!m_bMiniMode";
        if (!m_bTouchChangeVolume) {
            m_pTitlebar->setVisible(!isFullScreen());
            m_pToolbox->show();
        } else {
            m_pToolbox->hide();
        }
    } else {
        qCDebug(appMovie) << "m_bMiniMode";
	    //迷你模式根据半屏模式显示控件
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        int nScreenHeight = QApplication::desktop()->availableGeometry().height();
#else
        int nScreenHeight = QGuiApplication::primaryScreen()->availableGeometry().height();
#endif
        QRect rt = rect();
        if(rt.height() >= nScreenHeight-100){
            qCDebug(appMovie) << "rt.height() >= nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(false);
            m_pMiniCloseBtn->setVisible(false);
            m_pMiniQuitMiniBtn->setVisible(false);
            m_pToolbox->setVisible(false);
        }else {
            qCDebug(appMovie) << "rt.height() < nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(m_bMiniMode);
            m_pMiniCloseBtn->setVisible(m_bMiniMode);
            m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);
        }
    }

_finish:
    qCDebug(appMovie) << "Exit resumeToolsWindow function";
    m_autoHideTimer.start(AUTOHIDE_TIMEOUT);
}

void Platform_MainWindow::checkOnlineState(const bool bIsOnline)
{
    qCDebug(appMovie) << "Enter checkOnlineState function";
    if (!bIsOnline) {
        qCDebug(appMovie) << "!bIsOnline";
        this->sendMessage(QIcon(":/icons/deepin/builtin/icons/ddc_warning_30px.svg"), QObject::tr("Network disconnected"));
    }
    qCDebug(appMovie) << "Exit checkOnlineState function";
}

void Platform_MainWindow::checkOnlineSubtitle(const OnlineSubtitle::FailReason reason)
{
    qCDebug(appMovie) << "Enter checkOnlineSubtitle function";
    if (OnlineSubtitle::FailReason::NoSubFound == reason) {
        qCDebug(appMovie) << "OnlineSubtitle::FailReason::NoSubFound";
        m_pCommHintWid->updateWithMessage(tr("No matching online subtitles"));
    }
    qCDebug(appMovie) << "Exit checkOnlineSubtitle function";
}

void Platform_MainWindow::checkWarningMpvLogsChanged(const QString sPrefix, const QString sText)
{
    qCDebug(appMovie) << "Enter checkWarningMpvLogsChanged function";
    QString warningMessage(sText);
    qCInfo(appMovie) << "checkWarningMpvLogsChanged" << sText;
    if (warningMessage.contains(QString("Hardware does not support image size 3840x2160"))) {
        qCDebug(appMovie) << "warningMessage.contains(QString(\"Hardware does not support image size 3840x2160\"))";
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
                if (CompositingManager::get().platform() == Platform::X86) {
                    m_pAnimationlabel->resize(100, 100);
                } else {
                    if (!m_bIsWM) {
                        m_pAnimationlabel->resize(100, 100);
                    } else {
                        m_pAnimationlabel->setGeometry(width() / 2 - 100, height() / 2 - 100, 200, 200);
                    }
                }
                m_pAnimationlabel->playAnimation();
            }
            m_pEngine->pauseResume();
        });
    }
    qCDebug(appMovie) << "Exit checkWarningMpvLogsChanged function";
}

void Platform_MainWindow::slotdefaultplaymodechanged(const QString &sKey, const QVariant &value)
{
    qCDebug(appMovie) << "Enter slotdefaultplaymodechanged function";
    if (sKey != "base.play.playmode") {
        qCInfo(appMovie) << "Settings key error";
        return;
    }
    QPointer<DSettingsOption> modeOpt = Settings::get().settings()->option("base.play.playmode");
    QString sMode = modeOpt->data("items").toStringList()[value.toInt()];
    qCDebug(appMovie) << "sMode: " << sMode;
    if (sMode == tr("Order play")) {
        qCDebug(appMovie) << "sMode == tr(\"Order play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (sMode == tr("Shuffle play")) {
        qCDebug(appMovie) << "sMode == tr(\"Shuffle play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (sMode == tr("Single play")) {
        qCDebug(appMovie) << "sMode == tr(\"Single play\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (sMode == tr("Single loop")) {
        qCDebug(appMovie) << "sMode == tr(\"Single loop\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (sMode == tr("List loop")) {
        qCDebug(appMovie) << "sMode == tr(\"List loop\")";
        m_pEngine->playlist().setPlayMode(PlaylistModel::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
    qCDebug(appMovie) << "Exit slotdefaultplaymodechanged function";
}

void Platform_MainWindow::onSetDecodeModel(const QString &key, const QVariant &value)
{
    qCDebug(appMovie) << "Enter onSetDecodeModel function";
    Q_UNUSED(key);
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy = dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());
    if(pMpvProxy && value.toInt() != 3) {
        qCDebug(appMovie) << "pMpvProxy && value.toInt() != 3";
        pMpvProxy->setDecodeModel(value);
    }
    qCDebug(appMovie) << "Exit onSetDecodeModel function";
}

void Platform_MainWindow::onRefreshDecode()
{
    qCDebug(appMovie) << "Enter onRefreshDecode function";
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy =  dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());
    if(pMpvProxy) {
        qCDebug(appMovie) << "pMpvProxy";
        pMpvProxy->refreshDecode();
    }
    qCDebug(appMovie) << "Exit onRefreshDecode function";
}

void Platform_MainWindow::syncPostion()
{
    qCDebug(appMovie) << "Enter syncPostion function";
    m_pCommHintWid->syncPosition();
}

void Platform_MainWindow::my_setStayOnTop(const QWidget *pWidget, bool bOn)
{
    qCDebug(appMovie) << "Enter my_setStayOnTop function";
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
    qCDebug(appMovie) << "Exit my_setStayOnTop function";
}

void Platform_MainWindow::slotmousePressTimerTimeOut()
{
    qCDebug(appMovie) << "Enter slotmousePressTimerTimeOut function";
    m_mousePressTimer.stop();
    if (m_bMiniMode || m_bInBurstShootMode || !m_bMousePressed) {
        qCDebug(appMovie) << "m_bMiniMode || m_bInBurstShootMode || !m_bMousePressed";
        return;
    }

    if (insideToolsArea(QCursor::pos())) {
        qCDebug(appMovie) << "insideToolsArea(QCursor::pos())";
        return;
    }

    resumeToolsWindow();
    m_bMousePressed = false;
    m_bIsTouch = false;
    qCDebug(appMovie) << "Exit slotmousePressTimerTimeOut function";
}

void Platform_MainWindow::slotPlayerStateChanged()
{
    qCDebug(appMovie) << "Enter slotPlayerStateChanged function";
    bool bAudio = false;
    PlayerEngine *pEngine = dynamic_cast<PlayerEngine *>(sender());
    if (!pEngine) {
        qCDebug(appMovie) << "pEngine is null";
        return;
    }
    setInit(pEngine->state() != PlayerEngine::Idle);
    resumeToolsWindow();
    updateWindowTitle();

    // delayed checking if engine is still idle, in case other videos are schedulered (next/prev req)
    // and another resize event will happen after that
    QTimer::singleShot(100, [ = ]() {
        if (pEngine->state() == PlayerEngine::Idle && !m_bMiniMode
                && windowState() == Qt::WindowNoState && !isFullScreen()) {
            qCDebug(appMovie) << "pEngine->state() == PlayerEngine::Idle && !m_bMiniMode && windowState() == Qt::WindowNoState && !isFullScreen()";
            this->setMinimumSize(QSize(614, 500));
            
            // 优先使用用户设置的窗口大小，而不是默认的850x600
            if (m_bUserWindowSizeSet && m_userSetWindowSize.isValid()) {
                qCDebug(appMovie) << "Using user set window size:" << m_userSetWindowSize;
                this->resize(m_userSetWindowSize.width(), m_userSetWindowSize.height());
            } else {
                qCDebug(appMovie) << "Using default window size: 850x600";
                this->resize(850, 600);
            }
        }
    });

    if (m_pEngine->playlist().count() > 0) {
        qCDebug(appMovie) << "m_pEngine->playlist().count() > 0";
        bAudio = m_pEngine->currFileIsAudio();
    }
    if (m_pEngine->state() == PlayerEngine::CoreState::Playing && bAudio) {
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Playing && bAudio";
        m_pMovieWidget->startPlaying();
    } else if ((m_pEngine->state() == PlayerEngine::CoreState::Paused) && bAudio) {
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Paused && bAudio";
        m_pMovieWidget->pausePlaying();
    } else if (pEngine->state() == PlayerEngine::CoreState::Idle) {
        qCDebug(appMovie) << "pEngine->state() == PlayerEngine::CoreState::Idle";
        m_pMovieWidget->stopPlaying();
    }
    qCDebug(appMovie) << "Exit slotPlayerStateChanged function";
}

void Platform_MainWindow::slotFocusWindowChanged()
{
    qCDebug(appMovie) << "Enter slotFocusWindowChanged function";
    if (qApp->focusWindow() != windowHandle()) {
        qCDebug(appMovie) << "qApp->focusWindow() != windowHandle()";
        suspendToolsWindow();
    } else {
        qCDebug(appMovie) << "qApp->focusWindow() == windowHandle()";
        resumeToolsWindow();
    }
    qCDebug(appMovie) << "Exit slotFocusWindowChanged function";
}

/*void MainWindow::slotElapsedChanged()
{
#ifndef __mips__
    PlayerEngine *engine = dynamic_cast<PlayerEngine *>(sender());
    if (engine && m_pProgIndicator) {
        m_pProgIndicator->updateMovieProgress(engine->duration(), engine->elapsed());
    }
#endif
}*/

void Platform_MainWindow::slotFileLoaded()
{
    qCDebug(appMovie) << "Enter slotFileLoaded function";
    PlayerEngine *pEngine = dynamic_cast<PlayerEngine *>(sender());
    if (!pEngine) {
        qCDebug(appMovie) << "pEngine is null";
        return;
    }
    m_platform_nRetryTimes = 0;
    this->resizeByConstraints();
    m_bIsFree = true;
    qCDebug(appMovie) << "Exit slotFileLoaded function";
}

void Platform_MainWindow::slotUrlpause(bool bStatus)
{
    qCDebug(appMovie) << "Enter slotUrlpause function";
    if (bStatus) {
        qCDebug(appMovie) << "bStatus";
        auto msg = QString(tr("Buffering..."));
        m_pCommHintWid->updateWithMessage(msg);
    }
    qCDebug(appMovie) << "Exit slotUrlpause function";
}

void Platform_MainWindow::slotFontChanged(const QFont &/*font*/)
{
    qCDebug(appMovie) << "Enter slotFontChanged function";
    if (CompositingManager::get().platform() != Platform::Mips) {
        qCDebug(appMovie) << "CompositingManager::get().platform() != Platform::Mips";
        QFontMetrics fm(DFontSizeManager::instance()->get(DFontSizeManager::T6));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_pToolbox->getfullscreentimeLabel()->setMinimumWidth(fm.width(m_pToolbox->getfullscreentimeLabel()->text()));
        m_pToolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.width(m_pToolbox->getfullscreentimeLabelend()->text()));
#else
        m_pToolbox->getfullscreentimeLabel()->setMinimumWidth(fm.horizontalAdvance(m_pToolbox->getfullscreentimeLabel()->text()));
        m_pToolbox->getfullscreentimeLabelend()->setMinimumWidth(fm.horizontalAdvance(m_pToolbox->getfullscreentimeLabelend()->text()));
#endif

        int pixelsWidth = m_pToolbox->getfullscreentimeLabel()->width() + m_pToolbox->getfullscreentimeLabelend()->width();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QRect deskRect = QApplication::desktop()->availableGeometry();
#else
        QRect deskRect = QGuiApplication::primaryScreen()->availableGeometry();
#endif
        if (m_pFullScreenTimeLabel)
            m_pFullScreenTimeLabel->setGeometry(deskRect.width() - pixelsWidth - 32, 40, pixelsWidth + 32, 36);
    }
    qCDebug(appMovie) << "Exit slotFontChanged function";
}

void Platform_MainWindow::slotMuteChanged(bool bMute)
{
    qCDebug(appMovie) << "Enter slotMuteChanged function";
    m_pEngine->setMute(bMute);

    if (bMute) {
        qCDebug(appMovie) << "bMute";
        m_pCommHintWid->updateWithMessage(tr("Mute"));
    } else {
        qCDebug(appMovie) << "!bMute";
        m_pCommHintWid->updateWithMessage(tr("Volume: %1%").arg(m_nDisplayVolume));   // 取消静音时显示音量提示
    }
    qCDebug(appMovie) << "Exit slotMuteChanged function";
}

/*void MainWindow::slotAwaacelModeChanged(const QString &sKey, const QVariant &value)
{
    if (sKey != "base.play.hwaccel") {
        qCInfo(appMovie) << "Settings key error";
        return;
    }

    setHwaccelMode(value);
}*/

void Platform_MainWindow::slotVolumeChanged(int nVolume)
{
    qCDebug(appMovie) << "Enter slotVolumeChanged function";
    m_nDisplayVolume = nVolume;
    m_pEngine->changeVolume(nVolume);
    if (m_pPresenter) {
        qCDebug(appMovie) << "m_pPresenter";
        m_pPresenter->slotvolumeChanged();
    }

    static bool firstInit = false;
    if (!firstInit) {
        QTimer::singleShot(50, [=](){
            if (nVolume == 0) {
                m_pCommHintWid->updateWithMessage(tr("Mute"));
            } else {
                m_pCommHintWid->updateWithMessage(tr("Volume: %1%").arg(nVolume));
            }
        });
        firstInit = true;
    } else {
        if (nVolume == 0) {
            m_pCommHintWid->updateWithMessage(tr("Mute"));
        } else {
            m_pCommHintWid->updateWithMessage(tr("Volume: %1%").arg(nVolume));
        }
    }
}

void Platform_MainWindow::slotWMChanged()
{
    qCDebug(appMovie) << "Enter slotWMChanged function";
    m_bIsWM = DWindowManagerHelper::instance()->hasBlurWindow();

    m_pAnimationlabel->setWM(m_bIsWM);
    m_pCommHintWid->setWM(m_bIsWM);
    qCDebug(appMovie) << "Exit slotWMChanged function";
}

void Platform_MainWindow::mircastSuccess(QString name)
{
    qCDebug(appMovie) << "Enter mircastSuccess function";
    if (m_pEngine->state() == PlayerEngine::Playing) {
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Playing";
        m_pEngine->pauseResume();
    }
    updateActionsState();
    m_pMircastShowWidget->setDeviceName(name);
    m_pMircastShowWidget->show();
    m_pToolbox->hideMircastWidget();
    qCDebug(appMovie) << "Exit mircastSuccess function";
}

void Platform_MainWindow::exitMircast()
{
    qCDebug(appMovie) << "Enter exitMircast function";
    if (m_pEngine->state() == PlayerEngine::Playing) {
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Playing";
        m_pEngine->pauseResume();
    }
    m_pEngine->seekAbsolute(m_pToolbox->getSlider()->value());
    updateActionsState();
    m_pToolbox->getMircast()->slotExitMircast();
    m_pMircastShowWidget->hide();
    qCDebug(appMovie) << "Exit exitMircast function";
}

void Platform_MainWindow::checkErrorMpvLogsChanged(const QString sPrefix, const QString sText)
{
    qCDebug(appMovie) << "Enter checkErrorMpvLogsChanged function";
    QString sErrorMessage(sText);
    qCInfo(appMovie) << "checkErrorMpvLogsChanged" << sText;
    if (sErrorMessage.toLower().contains(QString("avformat_open_input() failed"))) {
        qCDebug(appMovie) << "error: avformat_open_input() failed, nothing to do";
        //do nothing
    } else if (sErrorMessage.toLower().contains(QString("fail")) && sErrorMessage.toLower().contains(QString("open"))
               && !sErrorMessage.toLower().contains(QString("dlopen"))) {
        qCDebug(appMovie) << "error: fail to open file or stream";
        m_pCommHintWid->updateWithMessage(tr("Cannot open file or stream"));
        m_pEngine->playlist().remove(m_pEngine->playlist().current());
    } else if (sErrorMessage.toLower().contains(QString("fail")) &&
               (sErrorMessage.toLower().contains(QString("format")))) {
        qCDebug(appMovie) << "error: fail to open file or stream";
        //Open the URL there is three cases of legal paths, illegal paths, and semi-legal
        //paths, which only processes the prefix legality, the suffix is not legal
        //please refer to other places to modify
        //powered by xxxxp
        if (m_pEngine->playlist().currentInfo().mi.title.isEmpty()) {
            qCDebug(appMovie) << "error: title is empty";
            m_pCommHintWid->updateWithMessage(tr("Parse failed"));
            m_pEngine->playlist().remove(m_pEngine->playlist().current());
        } else {
            qCDebug(appMovie) << "error: title is not empty";
            if (m_platform_nRetryTimes < 10) {
                qCDebug(appMovie) << "m_platform_nRetryTimes < 10";
                m_platform_nRetryTimes++;
                requestAction(ActionFactory::ActionKind::StartPlay);
            } else {
                qCDebug(appMovie) << "m_platform_nRetryTimes >= 10";
                m_platform_nRetryTimes = 0;
                m_pCommHintWid->updateWithMessage(tr("Invalid file"));
                m_pEngine->playlist().remove(m_pEngine->playlist().current());
            }
        }
    } else if (sErrorMessage.toLower().contains(QString("moov atom not found"))) {
        qCDebug(appMovie) << "error: moov atom not found";
        m_pCommHintWid->updateWithMessage(tr("Invalid file"));
    } else if (sErrorMessage.toLower().contains(QString("couldn't open dvd device"))) {
        qCDebug(appMovie) << "error: couldn't open dvd device";
        m_pCommHintWid->updateWithMessage(tr("Please insert a CD/DVD"));
    } else if (sErrorMessage.toLower().contains(QString("incomplete frame")) ||
               sErrorMessage.toLower().contains(QString("MVs not available"))) {
        qCDebug(appMovie) << "error: incomplete frame or MV not available";
        m_pCommHintWid->updateWithMessage(tr("Invalid file"));
    } else if ((sErrorMessage.toLower().contains(QString("can't"))) &&
               (sErrorMessage.toLower().contains(QString("open"))) &&
               sErrorMessage.toLower().contains(m_pEngine->getplaylist()->currentInfo().info.fileName())) {
        qCDebug(appMovie) << "error: can't open file";
        m_pCommHintWid->updateWithMessage(tr("No video file found"));
        qCWarning(appMovie) << "No video file found for:" << m_pEngine->getplaylist()->currentInfo().info.fileName();
    }
    qCDebug(appMovie) << "Exit checkErrorMpvLogsChanged function";
}

void Platform_MainWindow::closeEvent(QCloseEvent *pEvent)
{
#ifdef USE_TEST
    // 测试构建: 忽略关闭事件——closeEvent 会 delete 主窗口/engine 并 _Exit, 会截断或破坏 gtest 用例序列。
    // 忽略后全部用例可执行, 进程由 cleanupTestCase 的 exit(0) 正常退出, libgcov atexit 刷新覆盖率。
    return;
#endif
    qCDebug(appMovie) << "Enter closeEvent function";
    qCInfo(appMovie) << __func__;
    if(m_pMircastShowWidget&&m_pMircastShowWidget->isVisible()) {
        qCDebug(appMovie) << "m_pMircastShowWidget is visible";
        slotExitMircast();
    }
    if (m_nLastCookie > 0) {
        utils::UnInhibitStandby(m_nLastCookie);
        qCInfo(appMovie) << "uninhibit cookie" << m_nLastCookie;
        m_nLastCookie = 0;
    }

    Settings::get().onSetCrash();

    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        qCDebug(appMovie) << "Settings::get().isSet(Settings::ResumeFromLast)";
        int nCur = 0;
        nCur = m_pEngine->playlist().current();
        if (nCur >= 0) {
            Settings::get().setInternalOption("playlist_pos", nCur);
        }
    }
    static QEventLoop loop;
    QFileSystemWatcher fileWatcher;
    bool needWait = false;
    fileWatcher.addPath(Settings::get().configPath());
    connect(&fileWatcher, &QFileSystemWatcher::fileChanged, this, [=](){
        loop.quit();
    });
    //关闭窗口时保存音量值
    //关闭窗口时保存音量值
    int volume = Settings::get().internalOption("global_volume").toInt();
    if (m_nDisplayVolume != volume) {
        qCDebug(appMovie) << "m_nDisplayVolume != volume";
        Settings::get().setInternalOption("global_volume", m_nDisplayVolume > 100 ? 100 : m_nDisplayVolume);
        needWait = true;
    }
    if (Settings::get().settings()->getOption("set.start.crash").toInt() != 0) {
        qCDebug(appMovie) << "Settings::get().settings()->getOption(\"set.start.crash\") != 0";
        Settings::get().onSetCrash();
        needWait = true;
    }
    if (needWait) {
        qCDebug(appMovie) << "needWait";
        QTimer::singleShot(2000, this, [=](){
            if (loop.isRunning())
                loop.quit();
        });
        loop.exec();
    }
    m_pEngine->savePlaybackPosition();

    pEvent->accept();

#ifndef _LIBDMR_
        if (Settings::get().isSet(Settings::ClearWhenQuit)) {
            qCDebug(appMovie) << "Settings::get().isSet(Settings::ClearWhenQuit)";
            m_pEngine->playlist().clearPlaylist();
        } else {
            qCDebug(appMovie) << "Settings::get().isSet(Settings::ClearWhenQuit) == false";
            //persistently save current playlist
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
        qCDebug(appMovie) << "Exit closeEvent function, application quitting";
        QApplication::quit();
        _Exit(0);
}

void Platform_MainWindow::wheelEvent(QWheelEvent *pEvent)
{
    qCDebug(appMovie) << "Enter wheelEvent function";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    if (insideToolsArea(pEvent->pos()) || insideResizeArea(pEvent->globalPos()))
        return;
    if (m_pToolbox->isInMircastWidget(pEvent->pos()))
        return;
#else
    if (insideToolsArea(pEvent->position().toPoint()) || insideResizeArea(pEvent->globalPosition().toPoint()))
        return;
    if (m_pToolbox->isInMircastWidget(pEvent->position().toPoint()))
        return;
#endif

    if (m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Opened) {
        qCDebug(appMovie) << "m_pPlaylist is opened, ignore and return";
        pEvent->ignore();
        return;
    }

    if (m_bLocked) {
        qCDebug(appMovie) << "m_bLocked";
        if (pEvent->buttons() == Qt::NoButton && pEvent->modifiers() == Qt::NoModifier && m_pToolbox->getVolSliderIsHided()) {
            m_iAngleDelta = pEvent->angleDelta().y() ;
            if( m_iAngleDelta < -240){     //对滚轮距离出现异常值时的约束处理
                m_iAngleDelta = -120;
            }else if(m_iAngleDelta > 240 ){
                m_iAngleDelta = 120;
            }
            requestAction(pEvent->angleDelta().y() > 0 ? ActionFactory::VolumeUp : ActionFactory::VolumeDown);
        }
    }
}

void Platform_MainWindow::focusInEvent(QFocusEvent *pEvent)
{
    qCDebug(appMovie) << "Enter focusInEvent function";
    resumeToolsWindow();

    if(pEvent->gotFocus() && isFullScreen() &&
            m_pProgIndicator && m_pFullScreenTimeLabel) {
        m_pFullScreenTimeLabel->show();
        m_pProgIndicator->setVisible(true);
    }

    QMainWindow::focusInEvent(pEvent);
}

void Platform_MainWindow::focusOutEvent(QFocusEvent *pEvent)
{
    if(pEvent->lostFocus() && isFullScreen() &&
            m_pProgIndicator && m_pFullScreenTimeLabel) {
        m_pFullScreenTimeLabel->close();
        m_pProgIndicator->setVisible(false);
    }

    QMainWindow::focusOutEvent(pEvent);
}

void Platform_MainWindow::hideEvent(QHideEvent *pEvent)
{
    qCDebug(appMovie) << "Enter hideEvent function";
    QMainWindow::hideEvent(pEvent);
}

void Platform_MainWindow::showEvent(QShowEvent *pEvent)
{
    qCDebug(appMovie) << "Enter showEvent function";
    qCInfo(appMovie) << __func__;
    /*最大化，全屏，取消全屏，会先调用hideevent,再调用showevent，此时播放状态尚未切换，导致逻辑出错*/

    m_pAnimationlabel->raise();
    m_pTitlebar->raise();
    m_pToolbox->raise();
    if (m_pPlaylist) {
        m_pPlaylist->raise();
    }
    resumeToolsWindow();

    if (!qgetenv("FLATPAK_APPID").isEmpty()) {
        qCInfo(appMovie) << "workaround for flatpak";
        if (m_pPlaylist && m_pPlaylist->isVisible())
            updateProxyGeometry();
    }

    qCDebug(appMovie) << "Exit showEvent function";
    QMainWindow::showEvent(pEvent);
}

void Platform_MainWindow::resizeByConstraints(bool bForceCentered)
{
    qCDebug(appMovie) << "Enter resizeByConstraints function";
    if (m_pEngine->state() == PlayerEngine::Idle || m_pEngine->playlist().count() == 0) {
        m_pTitlebar->setTitletxt(QString());
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle || m_pEngine->playlist().count() == 0";
        return;
    }

    if (m_bMiniMode || isFullScreen() || isMaximized()) {
        qCDebug(appMovie) << "m_bMiniMode || isFullScreen() || isMaximized()";
        return;
    }

    qCInfo(appMovie) << __func__;
    updateWindowTitle();
    const MovieInfo &mi = m_pEngine->playlist().currentInfo().mi;
    QSize vidoeSize = m_pEngine->videoSize();
    if (CompositingManager::get().platform() == Platform::Mips)
        m_pCommHintWid->syncPosition();
    if (vidoeSize.isEmpty()) {
        vidoeSize = QSize(mi.width, mi.height);
        qCInfo(appMovie) << mi.width << mi.height;
    }

    auto geom = QGuiApplication::primaryScreen()->availableGeometry();
    
    if (vidoeSize.width() > geom.width() || vidoeSize.height() > geom.height()) {
        vidoeSize.scale(geom.width(), geom.height(), Qt::KeepAspectRatio);
    }

    qCInfo(appMovie) << "original: " << size() << "requested: " << vidoeSize;
    if (size() == vidoeSize)
        return;

    if (bForceCentered) {
        QRect r;
        r.setSize(vidoeSize);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        r.moveTopLeft({(geom.width() - r.width()) / 2, (geom.height() - r.height()) / 2});
#else
        r.moveTopLeft(QPoint((geom.width() - r.width()) / 2, (geom.height() - r.height()) / 2));
#endif
    }
}

// 若长≥高,则长≤528px　　　若长≤高,则高≤528px.
// 简而言之,只看最长的那个最大为528px.
void Platform_MainWindow::updateSizeConstraints()
{
    qCDebug(appMovie) << "Enter updateSizeConstraints function";
    QSize size;

    if (m_bMiniMode) {
        qCDebug(appMovie) << "m_bMiniMode";
        size = QSize(40, 40);
    } else {
        qCDebug(appMovie) << "not m_bMiniMode";
        //This function seems to be mentally and weak, and it is unknown.
        //powered by xxxxp
//        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
//            QRect dRect = DApplication::desktop()->availableGeometry();
//            QSize sz = m_pEngine->videoSize();
//            if (sz.width() == 0 || sz.height() == 0) {
//                size = QSize(614, 500);
//            } else {
//                qreal ratio = static_cast<qreal>(sz.width()) / sz.height();
//                if (sz.width() > sz.height()) {
//                    int w = static_cast<int>(500 * ratio);
//                    size = QSize(w, 500);
//                } else {
//                    int h = static_cast<int>(614 * ratio);
//                    if (h > dRect.height()) {
//                        h = dRect.height();
//                    }
//                    size = QSize(614, h);
//                }
//            }
//        } else {
//            size = QSize(614, 500);
//        }
        size = QSize(614, 500);
    }
    this->setMinimumSize(size);
}

void Platform_MainWindow::updateGeometryNotification(const QSize &sz)
{
    qCDebug(appMovie) << "Enter updateGeometryNotification function";
    QString sMsg = QString("%1x%2").arg(sz.width()).arg(sz.height());
    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
        m_pCommHintWid->updateWithMessage(sMsg);
    }

    if (windowState() == Qt::WindowNoState && !m_bMiniMode && !m_bMaximized) {
        qCDebug(appMovie) << "windowState() == Qt::WindowNoState && !m_bMiniMode && !m_bMaximized";
        m_lastRectInNormalMode = geometry();
    }
}

void Platform_MainWindow::LimitWindowize()
{
    qCDebug(appMovie) << "Enter LimitWindowize function";
    if (!m_bMiniMode && (geometry().width() == 380 || geometry().height() == 380)) {
        qCDebug(appMovie) << "not m_bMiniMode && (geometry().width() == 380 || geometry().height() == 380)";
        setGeometry(m_lastRectInNormalMode);
    }
}

void Platform_MainWindow::resizeEvent(QResizeEvent *pEvent)
{
    qCInfo(appMovie) << __func__ << geometry();
    // modify 4.1  Limit video to mini mode size by thx
    LimitWindowize();

    // 保存用户手动设置的窗口大小（排除程序自动调整的情况）
    if (!m_bMiniMode && !isFullScreen() && !isMaximized() &&
        m_pEngine->state() != PlayerEngine::Idle) {
        m_userSetWindowSize = geometry();
        m_bUserWindowSizeSet = true;
        qCDebug(appMovie) << "User set window size saved:" << m_userSetWindowSize;
    }

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
    m_pMovieWidget->resize(rect().size());
    m_pMovieWidget->move(0, 0);
    m_pAnimationlabel->move(0, 0);
    m_pMircastShowWidget->resize(rect().size());
    m_pMircastShowWidget->move(0, 0);

    QPoint relativePoint = mapToGlobal(QPoint(0, 0));
    m_pToolbox->updateSliderPoint(relativePoint);
    if(m_bMiniMode) { //迷你模式显示与半屏模式处理
        qCDebug(appMovie) << "m_bMiniMode";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        int nScreenHeight = QApplication::desktop()->availableGeometry().height();
#else
        int nScreenHeight = QGuiApplication::primaryScreen()->availableGeometry().height();
#endif
        QRect rt = rect();
        if(rt.height() >= nScreenHeight-100){
            qCDebug(appMovie) << "rt.height() >= nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(false);
            m_pMiniCloseBtn->setVisible(false);
            m_pMiniQuitMiniBtn->setVisible(false);
            m_pToolbox->setVisible(false);
        }else {
            qCDebug(appMovie) << "rt.height() < nScreenHeight-100";
            m_pMiniPlayBtn->setVisible(m_bMiniMode);
            m_pMiniCloseBtn->setVisible(m_bMiniMode);
            m_pMiniQuitMiniBtn->setVisible(m_bMiniMode);
        }
    }
    //判断屏幕可用坐标与应用的geometry是否有交集，没有就移动到屏幕可见位置
    QRect geoRect = geometry();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect deskRect = QApplication::desktop()->availableGeometry(geoRect.topLeft());
#else
    QRect deskRect = this->screen()->availableGeometry();
#endif

    if(!deskRect.intersects(geoRect)) {
        move(deskRect.x(), deskRect.y());
    }
    qCDebug(appMovie) << "Exiting resizeEvent function";
}

void Platform_MainWindow::updateWindowTitle()
{
    qCDebug(appMovie) << "Enter updateWindowTitle function";
    if (m_pEngine->state() != PlayerEngine::Idle) {
        qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::Idle";
        const MovieInfo &mi = m_pEngine->playlist().currentInfo().mi;
        QString sTitle = m_pTitlebar->fontMetrics().elidedText(mi.title,
                         Qt::ElideMiddle, m_pTitlebar->contentsRect().width() - 400);
        m_pTitlebar->setTitletxt(sTitle);
        setWindowTitle(mi.filePath);
        m_pTitlebar->setTitleBarBackground(false);
    } else {
        qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle";
        m_pTitlebar->setTitletxt(QString());
        setWindowTitle(QString());
        m_pTitlebar->setTitleBarBackground(false);
    }
    m_pTitlebar->setProperty("idle", m_pEngine->state() == PlayerEngine::Idle);
    qCDebug(appMovie) << "Exiting updateWindowTitle function";
}

void Platform_MainWindow::moveEvent(QMoveEvent *pEvent)
{
    qCInfo(appMovie) << __func__ << "进入moveEvent";
    QWidget::moveEvent(pEvent);
    m_pCommHintWid->syncPosition();
    QPoint relativePoint = mapToGlobal(QPoint(0, 0));
    m_pToolbox->updateSliderPoint(relativePoint);
    updateGeometryNotification(geometry().size());
}

void Platform_MainWindow::keyPressEvent(QKeyEvent *pEvent)
{
    qCDebug(appMovie) << "Enter keyPressEvent function";
    if (m_pPlaylist && (m_pPlaylist->state() == Platform_PlaylistWidget::Opened) && pEvent->modifiers() == Qt::NoModifier) {
        qCDebug(appMovie) << "m_pPlaylist && (m_pPlaylist->state() == Platform_PlaylistWidget::Opened) && pEvent->modifiers() == Qt::NoModifier";
        if (pEvent) {
            m_pPlaylist->updateSelectItem(pEvent->key());
        }
        pEvent->setAccepted(true);
    }
#ifdef QT_DEBUG
    //加入一个在调试环境下切换软硬解码的快捷键
    if (pEvent->key() == Qt::Key_H) {
        qCDebug(appMovie) << "pEvent->key() == Qt::Key_H";
        if (QApplication::keyboardModifiers() == Qt::ControlModifier) {
            if (m_sCurrentHwdec == "") {
                m_sCurrentHwdec = m_pEngine->getBackendProperty("hwdec").toString();
            }
            if (m_sCurrentHwdec == "off") {
                qCDebug(appMovie) << "m_sCurrentHwdec == off";
                m_pCommHintWid->popup("current is off");
                QWidget::keyPressEvent(pEvent);
                return;
            }

            QString str = m_pEngine->getBackendProperty("hwdec").toString();
            if (str == "off") {
                qCDebug(appMovie) << "str == off";
                m_pEngine->setBackendProperty("hwdec", m_sCurrentHwdec);
            } else {
                qCDebug(appMovie) << "str != off";
                m_pEngine->setBackendProperty("hwdec", "off");
            }
            qCDebug(appMovie) << "m_pCommHintWid->popup(QString(\"hwdec is %1\").arg(m_pEngine->getBackendProperty(\"hwdec\").toString()))";
            m_pCommHintWid->popup(QString("hwdec is %1").arg(m_pEngine->getBackendProperty("hwdec").toString()));
        }
    }
#endif

    QWidget::keyPressEvent(pEvent);
}

void Platform_MainWindow::keyReleaseEvent(QKeyEvent *pEvent)
{
    qCDebug(appMovie) << "Enter keyReleaseEvent function";
    QWidget::keyReleaseEvent(pEvent);
}

static bool s_bAfterDblClick = false;

void Platform_MainWindow::capturedMousePressEvent(QMouseEvent *pEvent)
{
    qCDebug(appMovie) << "Enter capturedMousePressEvent function";
    m_bMouseMoved = false;
    m_bMousePressed = false;
    m_pCommHintWid->hide();
    m_pPopupWid->hide();
    if (qApp->focusWindow() == nullptr) return;

    if (pEvent->buttons() == Qt::LeftButton) {
        qCDebug(appMovie) << "pEvent->buttons() == Qt::LeftButton";
        m_bMousePressed = true;
        m_bStartMove = true;
    }

    m_posMouseOrigin = mapToGlobal(pEvent->pos());
    qCDebug(appMovie) << "Exiting capturedMousePressEvent function";
}

void Platform_MainWindow::capturedMouseReleaseEvent(QMouseEvent *pEvent)
{
    qCDebug(appMovie) << "Enter capturedMouseReleaseEvent function";
    if (m_bIsTouch) {
        qCDebug(appMovie) << "m_bIsTouch";
        m_bLastIsTouch = true;
        m_bIsTouch = false;

        if (m_bTouchChangeVolume) {
            qCDebug(appMovie) << "m_bTouchChangeVolume";
            m_bTouchChangeVolume = false;
            m_pToolbox->setVisible(true);
        }

        if (m_bProgressChanged) {
            qCDebug(appMovie) << "m_bProgressChanged";
            m_pToolbox->updateSlider();   //手势释放时改变影片进度
            m_bProgressChanged = false;
        }
    } else {
        qCDebug(appMovie) << "!m_bIsTouch";
        m_bLastIsTouch = false;
    }

    if (m_bDelayedResizeByConstraint) {
        qCDebug(appMovie) << "m_bDelayedResizeByConstraint";
        m_bDelayedResizeByConstraint = false;

        QTimer::singleShot(0, [ = ]() {
            qCDebug(appMovie) << "QTimer::singleShot(0, [ = ]()";
            this->setMinimumSize({0, 0});
            this->resizeByConstraints(true);
        });
    }

    m_bStartMove = false;
    qCDebug(appMovie) << "m_bStartMove = false";
    if (!m_bMousePressed) {
        qCDebug(appMovie) << "!m_bMousePressed";
        s_bAfterDblClick = false;
        m_bMouseMoved = false;
    }

    if (qApp->focusWindow() == nullptr || !m_bMousePressed) {
        qCDebug(appMovie) << "qApp->focusWindow() == nullptr || !m_bMousePressed";
        return;
    }

    m_bMousePressed = false;
    qCDebug(appMovie) << "m_bMousePressed = false";
    //NOTE: If the mouseMoveEvent of the titlebar is triggered
    // reset the status here, otherwise it cannot respond to the mini mode shortcut
    if (m_pTitlebar->geometry().contains(pEvent->pos())) {
        qCDebug(appMovie) << "m_pTitlebar->geometry().contains(pEvent->pos())";
        m_bMouseMoved = false;
    }
    qCDebug(appMovie) << "Exiting capturedMouseReleaseEvent function";
}

void Platform_MainWindow::capturedKeyEvent(QKeyEvent *pEvent)
{
    qCDebug(appMovie) << "Enter capturedKeyEvent function";
    if (pEvent->key() == Qt::Key_Tab) {
        qCDebug(appMovie) << "pEvent->key() == Qt::Key_Tab";
        if (!isFullScreen()) {
            qCDebug(appMovie) << "!isFullScreen()";
            m_pTitlebar->show();
        }
        m_pToolbox->show();
        m_autoHideTimer.start(AUTOHIDE_TIMEOUT);  //如果点击tab键，重置计时器
    }
    qCDebug(appMovie) << "Exiting capturedKeyEvent function";
}

void Platform_MainWindow::mousePressEvent(QMouseEvent *pEvent)
{
    qCDebug(appMovie) << "Enter mousePressEvent function";
    m_bMouseMoved = false;
    m_bMousePressed = false;

    m_pCommHintWid->hide();
    m_pPopupWid->hide();
    m_pToolbox->closeAnyPopup();
    if (qApp->focusWindow() == nullptr) {
        qCDebug(appMovie) << "qApp->focusWindow() == nullptr";
        return;
    }
    if (pEvent->buttons() == Qt::LeftButton) {
        qCDebug(appMovie) << "pEvent->buttons() == Qt::LeftButton";
        m_bMousePressed = true;
        m_bStartMove = true;
        if (!m_mousePressTimer.isActive() && m_bIsTouch) {
            qCDebug(appMovie) << "!m_mousePressTimer.isActive() && m_bIsTouch";
            m_mousePressTimer.stop();

            m_nLastPressX = mapToGlobal(QCursor::pos()).x();
            m_nLastPressY = mapToGlobal(QCursor::pos()).y();
            qCInfo(appMovie) << __func__ << "已经进入触屏按下事件" << m_nLastPressX << m_nLastPressY;
            m_mousePressTimer.start();
        }
    }

    m_posMouseOrigin = mapToGlobal(pEvent->pos());
    m_pressPoint = pEvent->pos();
    qCDebug(appMovie) << "Exiting mousePressEvent function";
}

void Platform_MainWindow::mouseReleaseEvent(QMouseEvent *ev)
{
    qCDebug(appMovie) << "Enter mouseReleaseEvent function";
    /// NOTE: 为了其它控件的鼠标操作与MainWindow一致，统一使用capturedMouseReleaseEvent()捕获鼠标释放
    /// 事件，若无特殊要求，请尽量在capturedMouseReleaseEvent()进行处理。

    // 以下代码貌似没什么用，可以考虑去掉
    static bool bFlags = true;
    if (bFlags) {
        qCDebug(appMovie) << "bFlags";
        repaint();
        bFlags = false;
    }

    if (!m_bMouseMoved && m_pPlaylist && (m_pPlaylist->state() != Platform_PlaylistWidget::Opened)) {
        if (!insideToolsArea(ev->pos())) {
            qCDebug(appMovie) << "!insideToolsArea(ev->pos())";
            m_delayedMouseReleaseTimer.start(120);
        } else {
            if (m_pEngine->state() == PlayerEngine::CoreState::Idle && !insideToolsArea(ev->pos())) {
                qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Idle && !insideToolsArea(ev->pos())";
                m_delayedMouseReleaseTimer.start(120);
            }
        }
    }

    m_bMouseMoved = false;
    m_bMousePressed = false;

    qCDebug(appMovie) << "进入mouseReleaseEvent";
    QWidget::mouseReleaseEvent(ev);
}

void Platform_MainWindow::mouseDoubleClickEvent(QMouseEvent *pEvent)
{
    qCDebug(appMovie) << "Enter mouseDoubleClickEvent function";
    if (!m_bMiniMode && this->m_pEngine->getplaylist()->getthreadstate()) {
        qCDebug(appMovie) << "playlist loadthread is running";
        return;
    }
    //投屏时双击操作不做处理
    if(m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) return;
    if (!m_bMiniMode && !m_bInBurstShootMode) {
        m_delayedMouseReleaseTimer.stop();
        if (m_pEngine->state() == PlayerEngine::Idle) {
            requestAction(ActionFactory::StartPlay);
        } else {
            requestAction(ActionFactory::ToggleFullscreen, false, {}, true);
        }
        pEvent->accept();
        s_bAfterDblClick = true;
    }
}

void Platform_MainWindow::mouseMoveEvent(QMouseEvent *pEvent)
{
    qCDebug(appMovie) << "Enter mouseMoveEvent function";
    if (m_bStartMini) {
        qCDebug(appMovie) << "m_bStartMini";
        return;
    }

    m_pAnimationlabel->hide();
    QPoint ptCurr = mapToGlobal(pEvent->pos());
    QPoint ptDelta = ptCurr - this->m_posMouseOrigin;
    m_posMouseOrigin = mapToGlobal(pEvent->pos());
    if (qAbs(ptDelta.x()) < 5 && qAbs(ptDelta.y()) < 5) { //避免误触
        qCDebug(appMovie) << "qAbs(ptDelta.x()) < 5 && qAbs(ptDelta.y()) < 5";
        return;
    }

    if (m_bIsTouch && isFullScreen()) { //全屏时才触发滑动改变音量和进度的操作
        if (qAbs(ptDelta.x()) > qAbs(ptDelta.y())
                && m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "qAbs(ptDelta.x()) > qAbs(ptDelta.y()) && m_pEngine->state() != PlayerEngine::CoreState::Idle";
            m_bTouchChangeVolume = false;
            m_pToolbox->updateProgress(ptDelta.x());     //改变进度条显示
            //this->m_posMouseOrigin = ptCurr;
            m_bProgressChanged = true;
            return;
        } else if (qAbs(ptDelta.x()) < qAbs(ptDelta.y())) {
            qCDebug(appMovie) << "qAbs(ptDelta.x()) < qAbs(ptDelta.y())";
            if (ptDelta.y() > 0) {
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeDown);
            } else {
                m_bTouchChangeVolume = true;
                requestAction(ActionFactory::ActionKind::VolumeUp);
            }

            //this->m_posMouseOrigin = ptCurr;
            qCDebug(appMovie) << "return";
            return;
        }
    }

    if (!isFullScreen() && m_bStartMove) {
#ifdef XCB_Platform
        qCDebug(appMovie) << "XCB_Platform";
        Utility::startWindowSystemMove(this->winId());
        if (m_bStartMove) {
            m_bStartMove = false;
            //this->m_posMouseOrigin = ptCurr;
            m_bMouseMoved = true;
            qCDebug(appMovie) << "m_posMouseOrigin";
            return Utility::updateMousePointForWindowMove(this->winId(), pEvent->globalPos() * devicePixelRatioF());
        }
#else
        QWidget::mouseMoveEvent(pEvent);
#endif
    } else {
        QWidget::mouseMoveEvent(pEvent);
    }

    //this->m_posMouseOrigin = ptCurr;
    m_bMouseMoved = true;
    qCDebug(appMovie) << "Exiting mouseMoveEvent function";
}

void Platform_MainWindow::contextMenuEvent(QContextMenuEvent *pEvent)
{
    qCDebug(appMovie) << "Enter contextMenuEvent function";
    if (m_bMiniMode || m_bInBurstShootMode) {
        qCDebug(appMovie) << "m_bMiniMode || m_bInBurstShootMode";
        return;
    }

    if (insideToolsArea(pEvent->pos())) {
        qCDebug(appMovie) << "insideToolsArea(pEvent->pos())";
        return;
    }

    if (CompositingManager::isPadSystem()) {
        qCDebug(appMovie) << "CompositingManager::isPadSystem()";
        return;
    }

    //通过窗口id查询窗口状态是否置顶，同步右键菜单中的选项状态
    QProcess xpropProc;
    xpropProc.start("xprop", QStringList{"-id", QString::number(winId())});
    xpropProc.waitForFinished();
    QString xpropOutput = xpropProc.readAllStandardOutput();
    QStringList sList = xpropOutput.split('\n');
    // Filter lines containing _NET_WM_STATE(ATOM)
    QStringList filtered;
    for (const QString &line : sList) {
        if (line.contains("_NET_WM_STATE(ATOM)")) {
            filtered.append(line);
        }
    }
    sList = filtered;
    foreach(QString drv, sList) {
        if (drv.contains("_NET_WM_STATE_ABOVE") != m_bWindowAbove) {
            m_bWindowAbove = drv.contains("_NET_WM_STATE_ABOVE");
            reflectActionToUI(ActionFactory::WindowAbove);
            break;
        }
    }

    if(m_pMircastShowWidget->isVisible() ) {//投屏中屏蔽全屏、迷你模式，置顶菜单
        qCDebug(appMovie) << "m_pMircastShowWidget->isVisible()";
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
        QList<QAction *> lstAct = ActionFactory::get().findActionsByKind(ActionFactory::ActionKind::ToggleFullscreen);
        if(!lstAct.isEmpty()) {
            QAction *pAct = lstAct.first();
            if(pAct) pAct->setChecked(isFullScreen());
        }
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
//    qCInfo(appMovie) << atom << nItem;
//    int iItem;
//    for (iItem = 0; iItem < nItem; ++iItem)
//        qCInfo(appMovie) << ((long *)(properties))[iItem];
    qCDebug(appMovie) << "Exiting contextMenuEvent function";
}

bool Platform_MainWindow::insideToolsArea(const QPoint &p)
{
    qCDebug(appMovie) << "Enter insideToolsArea function";
    if (CompositingManager::get().platform() == Platform::X86) {
        qCDebug(appMovie) << "CompositingManager::get().platform() == Platform::X86";
        return (m_pTitlebar->geometry().contains(p) && !isFullScreen()) || m_pToolbox->geometry().contains(p) || (m_pToolbox->volumeSlider()->isVisible() && m_pToolbox->volumeSlider()->geometry().contains(p)) ||
                m_pMiniPlayBtn->geometry().contains(p)|| m_pMiniCloseBtn->geometry().contains(p) || m_pMiniQuitMiniBtn->geometry().contains(p);
    } else {
        qCDebug(appMovie) << "CompositingManager::get().platform() != Platform::X86";
        return (m_pTitlebar->geometry().contains(p) && !isFullScreen()) || m_pToolbox->rect().contains(p) || m_pToolbox->geometry().contains(p) || (m_pToolbox->volumeSlider()->isVisible() && m_pToolbox->volumeSlider()->geometry().contains(p)) ||
                m_pMiniPlayBtn->geometry().contains(p)|| m_pMiniCloseBtn->geometry().contains(p) || m_pMiniQuitMiniBtn->geometry().contains(p);
    }
}

QMargins Platform_MainWindow::dragMargins() const
{
    qCDebug(appMovie) << "Enter dragMargins function";
    return QMargins {MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS, MOUSE_MARGINS};
}

bool Platform_MainWindow::insideResizeArea(const QPoint &globalPos)
{
    qCDebug(appMovie) << "Enter insideResizeArea function";
    const QRect window_visible_rect = frameGeometry() - dragMargins();
    qCDebug(appMovie) << "window_visible_rect";
    return !window_visible_rect.contains(globalPos);
}

void Platform_MainWindow::delayedMouseReleaseHandler()
{
    qCDebug(appMovie) << "Enter delayedMouseReleaseHandler function";
    if ((!s_bAfterDblClick && !m_bLastIsTouch) || m_bMiniMode) {
        qCDebug(appMovie) << "(!s_bAfterDblClick && !m_bLastIsTouch) || m_bMiniMode";
        if (!CompositingManager::isPadSystem()) {
            requestAction(ActionFactory::TogglePause, false, {}, true);
        } else {
            resumeToolsWindow();    //平板模式下，点击窗口显示工具栏
        }
    }
    s_bAfterDblClick = false;
    qCDebug(appMovie) << "Exiting delayedMouseReleaseHandler function";
}

void Platform_MainWindow::prepareSplashImages()
{
    qCDebug(appMovie) << "Enter prepareSplashImages function";
    m_imgBgDark = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
    m_imgBgLight = utils::LoadHiDPIImage(":/resources/icons/light/init-splash.svg");
    qCDebug(appMovie) << "Exiting prepareSplashImages function";
}

void Platform_MainWindow::subtitleMatchVideo(const QString &sFileName)
{
    qCDebug(appMovie) << "Enter subtitleMatchVideo function";
    QString sVideoName = sFileName;
    // Search for video files with the same name as the subtitles and play the video file.
    QFileInfo subfileInfo(sFileName);
    QDir dir(subfileInfo.canonicalPath());
    dir.setFilter(QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    dir.setSorting(QDir::Size | QDir::Reversed);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i) {
        QFileInfo info = list.at(i);
        qCInfo(appMovie) << info.absoluteFilePath();
//        if (info.completeBaseName() == subfileInfo.completeBaseName()) {
        if (subfileInfo.fileName().contains(info.completeBaseName())) {
            sVideoName = info.absoluteFilePath();
        } else {
            sVideoName = nullptr;
        }
    }

    QFileInfo vfileInfo(sVideoName);
    if (vfileInfo.exists()) {
        qCDebug(appMovie) << "vfileInfo.exists()";
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
        qCDebug(appMovie) << "!vfileInfo.exists()";
        m_pCommHintWid->updateWithMessage(tr("Please load the video first"));
    }
    qCDebug(appMovie) << "Exiting subtitleMatchVideo function";
}

void Platform_MainWindow::defaultplaymodeinit()
{
    qCDebug(appMovie) << "Enter defaultplaymodeinit function";
    QPointer<DSettingsOption> modeOpt = Settings::get().settings()->option("base.play.playmode");
    int nModeId = modeOpt->value().toInt();
    QString sMode = modeOpt->data("items").toStringList()[nModeId];
    if (sMode == tr("Order play")) {
        qCDebug(appMovie) << "sMode == Order play";
        requestAction(ActionFactory::OrderPlay);
        reflectActionToUI(ActionFactory::OrderPlay);
    } else if (sMode == tr("Shuffle play")) {
        qCDebug(appMovie) << "sMode == Shuffle play";
        requestAction(ActionFactory::ShufflePlay);
        reflectActionToUI(ActionFactory::ShufflePlay);
    } else if (sMode == tr("Single play")) {
        qCDebug(appMovie) << "sMode == Single play";
        requestAction(ActionFactory::SinglePlay);
        reflectActionToUI(ActionFactory::SinglePlay);
    } else if (sMode == tr("Single loop")) {
        qCDebug(appMovie) << "sMode == Single loop";
        requestAction(ActionFactory::SingleLoop);
        reflectActionToUI(ActionFactory::SingleLoop);
    } else if (sMode == tr("List loop")) {
        qCDebug(appMovie) << "sMode == List loop";
        requestAction(ActionFactory::ListLoop);
        reflectActionToUI(ActionFactory::ListLoop);
    }
    qCDebug(appMovie) << "Exiting defaultplaymodeinit function";
}

void Platform_MainWindow::decodeInit()
{
    qCDebug(appMovie) << "Enter decodeInit function";
    MpvProxy* pMpvProxy = nullptr;
    pMpvProxy = dynamic_cast<MpvProxy*>(m_pEngine->getMpvProxy());

    if(!pMpvProxy)
        return;

    //崩溃检测
    int bcatch = Settings::get().settings()->getOption(QString("set.start.crash")).toInt();
    if (bcatch == 1) {
        qCDebug(appMovie) << "bcatch == 1";
        pMpvProxy->setDecodeModel(DecodeMode::AUTO);
        Settings::get().settings()->setOption(QString("base.decode.select"),DecodeMode::AUTO);
    } else {
        qCDebug(appMovie) << "bcatch != 1";
        int decodeMode = Settings::get().settings()->option("base.decode.select")->value().toInt();
        pMpvProxy->setDecodeModel(decodeMode);
        if (decodeMode == 3)
            dmr::Settings::get().crashCheck();
    }
    qCDebug(appMovie) << "Exiting decodeInit function";
}

void Platform_MainWindow::popupAdapter(QIcon icon, QString sText)
{
    qCDebug(appMovie) << "Enter popupAdapter function";
    m_pPopupWid->setIcon(icon);
    DFontSizeManager::instance()->bind(this, DFontSizeManager::T6);
    QFont font = DFontSizeManager::instance()->get(DFontSizeManager::T6);
    QFontMetrics fm(font);
    auto w = fm.boundingRect(sText).width();
    m_pPopupWid->setMessage(sText);
    m_pPopupWid->resize(w + 70, 40);
    m_pPopupWid->move((width() - m_pPopupWid->width()) / 2 + geometry().x(), height() - 127 + geometry().y());
    m_pPopupWid->show();
    m_pPopupWid->raise();
    qCDebug(appMovie) << "Exiting popupAdapter function";
}

QString Platform_MainWindow::lastOpenedPath()
{
    qCDebug(appMovie) << "Enter lastOpenedPath function";
    QString lastPath = Settings::get().generalOption("last_open_path").toString();
    QDir lastDir(lastPath);
    if (lastPath.isEmpty() || !lastDir.exists()) {
        qCDebug(appMovie) << "lastPath.isEmpty() || !lastDir.exists()";
        lastPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir newLastDir(lastPath);
        if (!newLastDir.exists()) {
            qCDebug(appMovie) << "!newLastDir.exists()";
            lastPath = QDir::currentPath();
        }
    }

    qCDebug(appMovie) << "Exiting lastOpenedPath function";
    return lastPath;
}

void Platform_MainWindow::paintEvent(QPaintEvent *pEvent)
{
    QPainter painter(this);
    QRectF bgRect;
    bgRect.setSize(size());
    const QPalette pal = QGuiApplication::palette();//this->palette();
    QColor bgColor = pal.color(QPalette::Window);

    if (CompositingManager::get().platform() == Platform::X86) {
        QPainterPath path;
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            if (m_pEngine->state() != PlayerEngine::Idle && !m_pToolbox->isVisible()) {
                path.addRect(bgRect);
                painter.fillPath(path, Qt::black);
            } else {
                path.addRect(bgRect);
                painter.fillPath(path, Qt::white);
            }
        }
    }

    if (m_pEngine->state() == PlayerEngine::Idle) {
        qreal dpr = devicePixelRatioF();
        int iconSize = static_cast<int>(130 * dpr);
        QImage bg = QIcon::fromTheme("deepin-movie").pixmap(iconSize, iconSize).toImage();
        bg.setDevicePixelRatio(dpr);
        //和产品、ui商议深色主题下去除深色背景效果
//        if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
//            QImage img = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash-bac.svg");
//            QPointF pos = bgRect.center() - QPoint(img.width() / 2, img.height() / 2) / devicePixelRatioF();
//            painter.drawImage(pos, img);
//        }
        QPointF pos = bgRect.center() - QPointF(bg.width() / 2.0 / dpr, bg.height() / 2.0 / dpr);
        painter.drawImage(pos, bg);
    }

    QMainWindow::paintEvent(pEvent);
}

void Platform_MainWindow::toggleUIMode()
{
    qCDebug(appMovie) << "Enter toggleUIMode function";
    // 对于最大化的窗口，需要先恢复至正常窗口，再进行迷你模式的操作
    // 因为有个时序的问题，避免窗口跳动引起用户不适，所以加入hide和show操作
    if (!m_bMiniMode && isMaximized() && !isFullScreen()) {
        qCInfo(appMovie) << "Toggle mini mode, now is Maximized, we need show normal first.";
        m_nStateBeforeMiniMode |= SBEM_Maximized;
        showNormal();
        hide();
        QTimer::singleShot(100, [&] {
            show();
            toggleUIMode();
        });
        return;
    }

    //判断窗口是否靠边停靠（靠边停靠不支持MINI模式）thx
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect deskrect = QApplication::desktop()->availableGeometry();
#else
    QRect deskrect = QGuiApplication::primaryScreen()->availableGeometry();
#endif
    QPoint windowPos = pos();
    if (this->geometry() != deskrect) {
        qCDebug(appMovie) << "this->geometry() != deskrect";
        if (windowPos.x() == 0 && (windowPos.y() == 0 ||
                                   (abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50))) {
            qCDebug(appMovie) << "windowPos.x() == 0 && (windowPos.y() == 0 || (abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50))";
            if (abs(this->geometry().width() - deskrect.width() / 2) < 50) {
                m_pCommHintWid->updateWithMessage(tr("Please exit smart dock"));
                m_bStartMini = false;
                reflectActionToUI(ActionFactory::ToggleMiniMode);
                qCDebug(appMovie) << "return";
                return ;
            }
        }
        if ((abs(windowPos.x() + this->geometry().width() - deskrect.width()) < 50)  &&
                (windowPos.y()  == 0 || abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50)) {
            qCDebug(appMovie) << "abs(windowPos.x() + this->geometry().width() - deskrect.width()) < 50 && (windowPos.y()  == 0 || abs(windowPos.y() + this->geometry().height() - deskrect.height()) < 50)";
            if (abs(this->geometry().width() - deskrect.width() / 2) < 50) {
                m_pCommHintWid->updateWithMessage(tr("Please exit smart dock"));
                m_bStartMini = false;
                reflectActionToUI(ActionFactory::ToggleMiniMode);
                qCDebug(appMovie) << "return";
                return ;
            }
        }
    }

    m_bMiniMode = !m_bMiniMode;
    qCDebug(appMovie) << "m_bMiniMode";

    if (m_bMiniMode) {
        qCDebug(appMovie) << "m_bMiniMode";
        m_pTitlebar->titlebar()->setDisableFlags(Qt::WindowMaximizeButtonHint);
    } else {
        qCDebug(appMovie) << "!m_bMiniMode";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_pTitlebar->titlebar()->setDisableFlags(nullptr);
#else
        m_pTitlebar->titlebar()->setDisableFlags(Qt::WindowFlags());
#endif
    }

    m_pTitlebar->setVisible(!m_bMiniMode);

    m_pMiniPlayBtn->setVisible(m_bMiniMode);
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
        qCDebug(appMovie) << "m_bMiniMode";
        m_pCommHintWid->setAnchorPoint(QPoint(15, 11));    //迷你模式下提示位置稍有不同
        updateSizeConstraints();
        //设置等比缩放
        setEnableSystemResize(false);
        //m_nStateBeforeMiniMode = SBEM_None;

        if (isFullScreen()) {
            qCDebug(appMovie) << "isFullScreen()";
            m_nStateBeforeMiniMode |= SBEM_Fullscreen;
            setWindowState(windowState() & ~Qt::WindowFullScreen);
            this->setWindowState(Qt::WindowNoState);
        } else {
            qCDebug(appMovie) << "!isFullScreen() && !isMaximized()";
            m_lastRectInNormalMode = geometry();
        }

        if (!m_bWindowAbove) {
            qCDebug(appMovie) << "!m_bWindowAbove";
            m_nStateBeforeMiniMode |= SBEM_Above;
            requestAction(ActionFactory::WindowAbove);
        }

        QSize sz = QSize(380, 380);
        if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
            qreal ratio = 1920 * 1.0 / 1080;
            auto vid_size = m_pEngine->videoSize();

            if (vid_size.height() > 0 && vid_size.width() >= vid_size.height()) {
                qCDebug(appMovie) << "vid_size.height() > 0 && vid_size.width() >= vid_size.height()";
                ratio = vid_size.width() / static_cast<qreal>(vid_size.height());
                sz = QSize(380, static_cast<int>(380 / ratio) + 1);
            } else if (vid_size.height() > 0 && vid_size.width() < vid_size.height()) {
                qCDebug(appMovie) << "vid_size.height() > 0 && vid_size.width() < vid_size.height()";
                ratio = vid_size.width() / static_cast<qreal>(vid_size.height());
                sz = QSize(380, static_cast<int>(380 * ratio) + 1);
            } else {
                qCDebug(appMovie) << "else";
                sz = QSize(380, static_cast<int>(380 / ratio) + 1);
            }
        }

        setFixedSize(sz);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QRect deskGeom = QGuiApplication::primaryScreen()->availableGeometry();
#else
        QRect deskGeom = qApp->desktop()->availableGeometry(this);
#endif
        move(deskGeom.x() + (deskGeom.width() - sz.width()) / 2, deskGeom.y() + (deskGeom.height() - sz.height()) / 2); //迷你模式下窗口居中 by zhuyuliang

        m_pMiniPlayBtn->move(sz.width() - 12 - m_pMiniPlayBtn->width(),
                             sz.height() - 10 - m_pMiniPlayBtn->height());
        m_pMiniCloseBtn->move(sz.width() - 15 - m_pMiniCloseBtn->width(), 10);
        m_pMiniQuitMiniBtn->move(14, sz.height() - 10 - m_pMiniQuitMiniBtn->height());
    } else {
        qCDebug(appMovie) << "!m_bMiniMode";
        m_pCommHintWid->setAnchorPoint(QPoint(30, 58));
        // 非迷你模式下，窗口大小恢复可被修改状态
        QRect tmpRect = m_lastRectInNormalMode; // 先备份，避免被setMinimumSize()影响，后面再恢复
        this->setMinimumSize(614, 500);
        this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        m_lastRectInNormalMode = tmpRect;
        setEnableSystemResize(true);
        if (m_nStateBeforeMiniMode & SBEM_Maximized) {
            //迷你模式切换最大化时，先恢复原来窗口大小
            if (m_lastRectInNormalMode.isValid()) {
                setGeometry(m_lastRectInNormalMode);
            } else {
                resizeByConstraints();
            }
            // 由于时序问题，延迟最大化
            QTimer::singleShot(100, [&] {
                hide();
                showMaximized();
            });
        } else if (m_nStateBeforeMiniMode & SBEM_Fullscreen) {
            qCDebug(appMovie) << "m_nStateBeforeMiniMode & SBEM_Fullscreen";
            setWindowState(windowState() | Qt::WindowFullScreen);
        } else {
            qCDebug(appMovie) << "!m_nStateBeforeMiniMode & SBEM_Fullscreen";
            if (m_pToolbox->listBtn()->isChecked()) {
                m_pToolbox->listBtn()->setChecked(false);
            }

            if (m_lastRectInNormalMode.isValid()) {
                setGeometry(m_lastRectInNormalMode);
            } else {
                resizeByConstraints();
            }
        }

        if (m_nStateBeforeMiniMode & SBEM_Above) {
            qCDebug(appMovie) << "m_nStateBeforeMiniMode & SBEM_Above";
            requestAction(ActionFactory::WindowAbove);
        }

        if (m_nStateBeforeMiniMode & SBEM_PlaylistOpened &&
                m_pPlaylist && m_pPlaylist->state() == Platform_PlaylistWidget::Closed) {
            qCDebug(appMovie) << "m_nStateBeforeMiniMode & SBEM_PlaylistOpened && m_pPlaylist->state() == Platform_PlaylistWidget::Closed";
            if (m_nStateBeforeMiniMode & SBEM_Fullscreen) {
                QTimer::singleShot(100, [ = ]() {
                    qCDebug(appMovie) << "QTimer::singleShot(100, [ = ]()";
                    requestAction(ActionFactory::TogglePlaylist);
                });
            }
        }
        m_nStateBeforeMiniMode = SBEM_None;
        qCDebug(appMovie) << "m_nStateBeforeMiniMode = SBEM_None";
        
        // 退出迷你模式时，清除用户设置标志，重新开始记录
        m_bUserWindowSizeSet = false;
        m_userSetWindowSize = QRect();
        qCDebug(appMovie) << "Reset user window size settings when exiting mini mode";
    }

    m_bStartMini = false;
}

void Platform_MainWindow::miniButtonClicked(const QString &id)
{
    qCDebug(appMovie) << "Enter miniButtonClicked function";
    qCInfo(appMovie) << id;
    if (id == "play") {
        qCDebug(appMovie) << "id == play";
        if (m_pEngine->state() == PlayerEngine::CoreState::Idle) {
            qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::CoreState::Idle";
            requestAction(ActionFactory::ActionKind::StartPlay);
        } else {
            qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
            requestAction(ActionFactory::ActionKind::TogglePause);
        }

    } else if (id == "close") {
        qCDebug(appMovie) << "id == close";
        close();

    } else if (id == "quit_mini") {
        qCDebug(appMovie) << "id == quit_mini";
        requestAction(ActionFactory::ActionKind::ToggleMiniMode);
    }
    qCDebug(appMovie) << "Exiting miniButtonClicked function";
}

void Platform_MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    qCDebug(appMovie) << "Enter dragEnterEvent function";
    if (ev->mimeData()->hasUrls()) {
        qCDebug(appMovie) << "ev->mimeData()->hasUrls()";
        ev->acceptProposedAction();
    }
    qCDebug(appMovie) << "Exiting dragEnterEvent function";
}

void Platform_MainWindow::dragMoveEvent(QDragMoveEvent *ev)
{
    qCDebug(appMovie) << "Enter dragMoveEvent function";
    if (ev->mimeData()->hasUrls()) {
        qCDebug(appMovie) << "ev->mimeData()->hasUrls()";
        ev->acceptProposedAction();
    }
    qCDebug(appMovie) << "Exiting dragMoveEvent function";
}

void Platform_MainWindow::dropEvent(QDropEvent *pEvent)
{
    qCDebug(appMovie) << "Enter dropEvent function";
    //add by heyi 拖动进来时先初始化窗口
    //firstPlayInit();
    qCInfo(appMovie) << pEvent->mimeData()->formats();
    if (!pEvent->mimeData()->hasUrls()) {
        qCDebug(appMovie) << "!pEvent->mimeData()->hasUrls(), return";
        return;
    }

    QList<QString> lstFile;
    QList<QUrl> urls = pEvent->mimeData()->urls();

    for (QUrl strUrl : urls) {
        lstFile << strUrl.path();
    }

    if (urls.count() == 1) {
        qCDebug(appMovie) << "urls.count() == 1";
        // check if the dropped file is a subtitle.
        QFileInfo fileInfo(urls.first().toLocalFile());
        if (m_pEngine->isSubtitle(fileInfo.absoluteFilePath())) {
            // Search for video files with the same name as the subtitles and play the video file.
            if(m_pEngine->state() != PlayerEngine::CoreState::Idle
                    && m_pEngine->playlist().currentInfo().mi.isRawFormat()) {
                qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat(), return";
                return;
            }
            else if (m_pEngine->state() == PlayerEngine::Idle) {
                qCDebug(appMovie) << "m_pEngine->state() == PlayerEngine::Idle";
                subtitleMatchVideo(urls.first().toLocalFile());
            }
            else {
                qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle && m_pEngine->playlist().currentInfo().mi.isRawFormat()";
                bool succ = m_pEngine->loadSubtitle(fileInfo);
                m_pCommHintWid->updateWithMessage(succ ? tr("Load successfully") : tr("Load failed"));
            }

            return;
        }
    }

    qCDebug(appMovie) << "play(lstFile)";
    play(lstFile);

    pEvent->acceptProposedAction();
}

void Platform_MainWindow::setInit(bool bInit)
{
    qCDebug(appMovie) << "Enter setInit function";
    if (m_bInited != bInit) {
        qCDebug(appMovie) << "m_bInited != bInit";
        m_bInited = bInit;
        emit initChanged();
    }
    qCDebug(appMovie) << "Exiting setInit function";
}

QString Platform_MainWindow::probeCdromDevice()
{
    qCDebug(appMovie) << "Enter probeCdromDevice function";
    QFile mountFile("/proc/mounts");
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        qCDebug(appMovie) << "mountFile.open(QIODevice::ReadOnly) == false";
        return QString();
    }
    do {
        QString sLine = mountFile.readLine();
        qCDebug(appMovie) << "sLine";
        if (sLine.indexOf("/dev/sr") != -1 || sLine.indexOf("/dev/cdrom") != -1) {  //说明存在光盘的挂载。
            qCDebug(appMovie) << "sLine.indexOf(\"/dev/sr\") != -1 || sLine.indexOf(\"/dev/cdrom\") != -1";
            return sLine.split(" ").at(0);        //A B C 这样的格式，取部分
        }
    } while (!mountFile.atEnd());
    mountFile.close();
    qCDebug(appMovie) << "Exiting probeCdromDevice function";
    return QString();
}

void Platform_MainWindow::diskRemoved(QString strDiskName)
{
    qCDebug(appMovie) << "Enter diskRemoved function";
    QString sCurrFile;
    if (m_pEngine->getplaylist()->count() <= 0) {
        qCDebug(appMovie) << "m_pEngine->getplaylist()->count() <= 0";
        return;
    }
    sCurrFile = m_pEngine->getplaylist()->currentInfo().url.toString();

    if (sCurrFile.contains(strDiskName)/* && m_pEngine->state() == PlayerEngine::Playing*/) {
        qCDebug(appMovie) << "sCurrFile.contains(strDiskName)";
        m_pCommHintWid->updateWithMessage(tr("The CD/DVD has been ejected"));
    }
    qCDebug(appMovie) << "Exiting diskRemoved function";
}

void Platform_MainWindow::sleepStateChanged(bool bSleep)
{
    qCDebug(appMovie) << "Enter sleepStateChanged function";
    //休眠退出投屏
    if(bSleep && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) {
        qCDebug(appMovie) << "bSleep && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
        slotExitMircast();
    }
    qCInfo(appMovie) << __func__ << bSleep;

    //if (m_bStateInLock) {                //休眠唤醒后会先执行锁屏操作,如果已经进行锁屏操作则忽略休眠唤醒信号
     //   m_bStartSleep = bSleep;
     //   m_pEngine->seekAbsolute(static_cast<int>(m_pEngine->elapsed()));
    //    return;
    //}
    if (bSleep && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qCDebug(appMovie) << "bSleep && m_pEngine->state() == PlayerEngine::CoreState::Playing";
        m_bStartSleep = true;
        requestAction(ActionFactory::ActionKind::TogglePause);
    } else if (!bSleep && m_pEngine->state() == PlayerEngine::CoreState::Paused) {
        qCDebug(appMovie) << "!bSleep && m_pEngine->state() == PlayerEngine::CoreState::Paused";
        m_bStartSleep = false;
        m_pEngine->seekAbsolute(static_cast<int>(m_pEngine->elapsed()));      //保证休眠后不管是否播放都不会卡帧
    }
    qCDebug(appMovie) << "Exiting sleepStateChanged function";
}

void Platform_MainWindow::lockStateChanged(bool bLock)
{
    qCDebug(appMovie) << "Enter lockStateChanged function";
    //锁屏退出投屏
    if(bLock && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()) {
        qCDebug(appMovie) << "bLock && m_pMircastShowWidget && m_pMircastShowWidget->isVisible()";
        slotExitMircast();
    }
    if (!bLock) {
        qCDebug(appMovie) << "!bLock";
        m_bLocked = false;
        QTimer::singleShot(1000, [=](){
            m_bLocked = true;
        });
    }
    if (bLock && m_pEngine->state() == PlayerEngine::CoreState::Playing && !m_bStateInLock) {
        qCDebug(appMovie) << "bLock && m_pEngine->state() == PlayerEngine::CoreState::Playing && !m_bStateInLock";
        m_bStateInLock = true;
        requestAction(ActionFactory::ActionKind::TogglePause);
    } else if (!bLock && m_pEngine->state() == PlayerEngine::CoreState::Paused && m_bStateInLock) {
        qCDebug(appMovie) << "!bLock && m_pEngine->state() == PlayerEngine::CoreState::Paused && m_bStateInLock";
        m_bStateInLock = false;
        //需求变更，唤醒后不恢复播放
    }
    qCDebug(appMovie) << "Exiting lockStateChanged function";
}

void Platform_MainWindow::initMember()
{
    qCDebug(appMovie) << "Enter initMember function";
    m_pPopupWid = nullptr;
    m_pFullScreenTimeLabel = nullptr;             //全屏时右上角的影片进度
    m_pFullScreenTimeLayout = nullptr;
    m_pTitlebar = nullptr;
    m_pToolbox = nullptr;
    m_pPlaylist = nullptr;
    m_pEngine = nullptr;
    m_pAnimationlabel = nullptr;
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
    m_bStartMove = false;
    m_bMaximized = false;

    m_nDisplayVolume = 100;
    m_nLastPressX = 0;
    m_nLastPressY = 0;
    m_nStateBeforeMiniMode = 0;
    m_nLastCookie = 0;
    m_nPowerCookie = 0;
    m_dPlaySpeed = 1.0;
    m_iAngleDelta = 0;
    m_nFullscreenTime = 0;

    m_lastRectInNormalMode = QRect();
    m_userSetWindowSize = QRect();
    m_bUserWindowSizeSet = false;

    m_lastWindowState = Qt::WindowNoState;

    m_dvdUrl.clear();
    m_listOpenFiles.clear();
    m_sCurrentHwdec.clear();
    m_listBurstShoots.clear();
    qCDebug(appMovie) << "Exiting Platform_MainWindow::initMember.";
}

void Platform_MainWindow::adjustPlaybackSpeed(ActionFactory::ActionKind actionKind)
{
    qCDebug(appMovie) << "Enter adjustPlaybackSpeed function";
    if (m_pEngine->state() != PlayerEngine::CoreState::Idle) {
        qCDebug(appMovie) << "m_pEngine->state() != PlayerEngine::CoreState::Idle";
        if (actionKind == ActionFactory::ActionKind::AccelPlayback) {
            qCDebug(appMovie) << "actionKind == ActionFactory::ActionKind::AccelPlayback";
            m_dPlaySpeed = qMin(2.0, m_dPlaySpeed + 0.1);
        } else if (actionKind == ActionFactory::ActionKind::DecelPlayback) {
            qCDebug(appMovie) << "actionKind == ActionFactory::ActionKind::DecelPlayback";
            m_dPlaySpeed = qMax(0.1, m_dPlaySpeed - 0.1);
        }

        m_pEngine->setPlaySpeed(m_dPlaySpeed);
        if (qFuzzyCompare(0.5, m_dPlaySpeed)) {
            qCDebug(appMovie) << "0.5 play speed";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::ZeroPointFiveTimes);
        } else if (qFuzzyCompare(1.0, m_dPlaySpeed)) {
            qCDebug(appMovie) << "1.0 play speed";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OneTimes);
        } else if (qFuzzyCompare(1.2, m_dPlaySpeed)) {
            qCDebug(appMovie) << "1.2 play speed";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointTwoTimes);
        } else if (qFuzzyCompare(1.5, m_dPlaySpeed)) {
            qCDebug(appMovie) << "1.5 play speed";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::OnePointFiveTimes);
        } else if (qFuzzyCompare(2.0, m_dPlaySpeed)) {
            qCDebug(appMovie) << "2.0 play speed";
            setPlaySpeedMenuChecked(ActionFactory::ActionKind::Double);
        } else {
            qCDebug(appMovie) << "other play speed";
            setPlaySpeedMenuUnchecked();
        }
        m_pCommHintWid->updateWithMessage(tr("Speed: %1x").arg(m_dPlaySpeed));
    }
    qCDebug(appMovie) << "Exiting adjustPlaybackSpeed function";
}

void Platform_MainWindow::setPlaySpeedMenuChecked(ActionFactory::ActionKind actionKind)
{
    qCDebug(appMovie) << "Enter setPlaySpeedMenuChecked function";
    QList<QAction *> listActs = ActionFactory::get().findActionsByKind(actionKind);
    auto p = listActs.begin();
    (*p)->setChecked(true);
}

void Platform_MainWindow::setPlaySpeedMenuUnchecked()
{
    qCDebug(appMovie) << "Enter setPlaySpeedMenuUnchecked function";
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
    qCDebug(appMovie) << "Exiting setPlaySpeedMenuUnchecked function";
}

void Platform_MainWindow::setMusicShortKeyState(bool bState)
{
    qCDebug(appMovie) << "Enter setMusicShortKeyState function";
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
            break;
        default:
            break;
        }
    }
    qCDebug(appMovie) << "Exiting setMusicShortKeyState function";
}

void Platform_MainWindow::onSysLockState(QString, QVariantMap key2value, QStringList)
{
    qCDebug(appMovie) << "Enter onSysLockState function";
    if (m_bStartSleep) {
        qCDebug(appMovie) << "m_bStartSleep is true";
        m_bStateInLock = true;       //如果进入了休眠状态后进入锁屏,则默认执行了暂停操作
    }

    if (key2value.value("Locked").value<bool>() && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qCDebug(appMovie) << "Locked is true and playing";
        m_bStateInLock = true;
        requestAction(ActionFactory::TogglePause);
    } else if (!key2value.value("Locked").value<bool>() && m_bStateInLock) {
        qCDebug(appMovie) << "Locked is false and m_bStateInLock is true";
        m_bStateInLock = false;
        // requestAction(ActionFactory::TogglePause); //需求变更，唤醒后不恢复播放
    }
    qCDebug(appMovie) << "Exiting onSysLockState function";
}

void Platform_MainWindow::slotProperChanged(QString, QVariantMap key2value, QStringList)
{
    qCInfo(appMovie) << __func__ << key2value;
    if (key2value.value("Active").value<bool>() && m_pEngine->state() == PlayerEngine::CoreState::Playing) {
        qCDebug(appMovie) << "Active is true and playing";
        m_pEngine->seekAbsolute(m_pEngine->elapsed());
    }
}

void Platform_MainWindow::slotUnsupported()
{
    qCInfo(appMovie) << "Unsupported action attempted on current video";
    m_pCommHintWid->updateWithMessage(tr("The action is not supported in this video"));
}

void Platform_MainWindow::slotInvalidFile(QString strFileName)
{
    qCInfo(appMovie) << "Invalid file detected:" << strFileName;
    static int showTime = -1000;

    showTime += 1000;

    QTimer::singleShot(showTime, [=]{
       showTime = showTime - 1000;
       m_pCommHintWid->updateWithMessage(QString(tr("Invalid file: %1").arg(strFileName)));
    });
    qCDebug(appMovie) << "Exit slotInvalidFile function";
}

void Platform_MainWindow::slotUpdateMircastState(int state, QString msg)
{
    qCDebug(appMovie) << "Enter slotUpdateMircastState function";
    switch (state) {
    case MIRCAST_SUCCEEDED: //投屏成功
    {
        qCDebug(appMovie) << "MIRCAST_SUCCESSED";
        mircastSuccess(msg);
        emit frameMenuEnable(false);
        emit playSpeedMenuEnable(false);
        emit subtitleMenuEnable(false);
        emit soundMenuEnable(false);
    }
        break;
    case MIRCAST_EXIT://投屏退出
    {
        qCDebug(appMovie) << "MIRCAST_EXIT";
        slotExitMircast();
    }
        break;
    case MIRCAST_CONNECTION_FAILED://投屏连接失败
    {
        qCDebug(appMovie) << "MIRCAST_CONNECTION_FAILED";
        const QIcon icon = QIcon(":/resources/icons/short_fail.svg");
        QString sText = QString(tr("Connection failed"));
        popupAdapter(icon, sText);
        slotExitMircast();
    }
        break;
    case MIRCAST_DISCONNECTED://投屏丢失连接
    {
        qCDebug(appMovie) << "MIRCAST_DISCONNECTIONED";
        m_pCommHintWid->updateWithMessage(tr("Miracast disconnected"));
        slotExitMircast();
    }
        break;
    default:
        break;
    }
    qCDebug(appMovie) << "Exiting slotUpdateMircastState function";
}

void Platform_MainWindow::slotExitMircast()
{
    qCDebug(appMovie) << "Enter slotExitMircast function";
    exitMircast();
    emit frameMenuEnable(true);
    emit playSpeedMenuEnable(true);
    emit subtitleMenuEnable(true);
    emit soundMenuEnable(true);
    qCDebug(appMovie) << "Exiting slotExitMircast function";
}

void Platform_MainWindow::updateGeometry(Platform_CornerEdge edge, QPoint pos)
{
    qCDebug(appMovie) << "Enter updateGeometry function";
    bool bKeepRatio = engine()->state() != PlayerEngine::CoreState::Idle;
    QRect oldGeom = frameGeometry();
    QRect geom = frameGeometry();
    qreal ratio = static_cast<qreal>(geom.width()) / geom.height();

    // disable edges
    switch (edge) {
    case Platform_CornerEdge::Platform_BottomEdge:
    case Platform_CornerEdge::Platform_TopEdge:
    case Platform_CornerEdge::Platform_LeftEdge:
    case Platform_CornerEdge::Platform_RightEdge:
    case Platform_CornerEdge::Platform_NoneEdge:
        return;
    default:
        break;
    }

    if (bKeepRatio) {
        QSize size = engine()->videoSize();
        if (size.isEmpty()) {
            const auto &MovieInfo = engine()->playlist().currentInfo().mi;
            size = QSize(MovieInfo.width, MovieInfo.height);
        }

        ratio = size.width() / static_cast<qreal>(size.height());
        switch (edge) {
        case Platform_CornerEdge::Platform_TopLeftCorner:
            geom.setLeft(pos.x());
            geom.setTop(static_cast<int>(geom.bottom() - geom.width() / ratio));
            break;
        case Platform_CornerEdge::Platform_BottomLeftCorner:
        case Platform_CornerEdge::Platform_LeftEdge:
            geom.setLeft(pos.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case Platform_CornerEdge::Platform_BottomRightCorner:
        case Platform_CornerEdge::Platform_RightEdge:
            geom.setRight(pos.x());
            geom.setHeight(static_cast<int>(geom.width() / ratio));
            break;
        case Platform_CornerEdge::Platform_TopRightCorner:
        case Platform_CornerEdge::Platform_TopEdge:
            geom.setTop(pos.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        case Platform_CornerEdge::Platform_BottomEdge:
            geom.setBottom(pos.y());
            geom.setWidth(static_cast<int>(geom.height() * ratio));
            break;
        default:
            break;
        }
    } else {
        switch (edge) {
        case Platform_CornerEdge::Platform_BottomLeftCorner:
            geom.setBottomLeft(pos);
            break;
        case Platform_CornerEdge::Platform_TopLeftCorner:
            geom.setTopLeft(pos);
            break;
        case Platform_CornerEdge::Platform_LeftEdge:
            geom.setLeft(pos.x());
            break;
        case Platform_CornerEdge::Platform_BottomRightCorner:
            geom.setBottomRight(pos);
            break;
        case Platform_CornerEdge::Platform_RightEdge:
            geom.setRight(pos.x());
            break;
        case Platform_CornerEdge::Platform_TopRightCorner:
            geom.setTopRight(pos);
            break;
        case Platform_CornerEdge::Platform_TopEdge:
            geom.setTop(pos.y());
            break;
        case Platform_CornerEdge::Platform_BottomEdge:
            geom.setBottom(pos.y());
            break;
        default:
            break;
        }
    }

    QSize min = minimumSize();
    if (oldGeom.width() <= min.width() && geom.left() > oldGeom.left()) {
        geom.setLeft(oldGeom.left());
    }
    if (oldGeom.height() <= min.height() && geom.top() > oldGeom.top()) {
        geom.setTop(oldGeom.top());
    }

    geom.setWidth(qMax(geom.width(), min.width()));
    geom.setHeight(qMax(geom.height(), min.height()));
    updateContentGeometry(geom);
    updateGeometryNotification(geom.size());
}

void Platform_MainWindow::setPresenter(Presenter *pPresenter)
{
    qCDebug(appMovie) << "Enter setPresenter function";
    m_pPresenter = pPresenter;
    m_pPresenter->slotvolumeChanged();
}

int Platform_MainWindow::getDisplayVolume()
{
    qCDebug(appMovie) << "Enter getDisplayVolume function, return: " << m_nDisplayVolume;
    return m_nDisplayVolume;
}

bool Platform_MainWindow::getMiniMode()
{
    qCDebug(appMovie) << "Enter getMiniMode function, return: " << m_bMiniMode;
    return m_bMiniMode;
}

Platform_MainWindow::~Platform_MainWindow()
{
    qCDebug(appMovie) << "Enter ~Platform_MainWindow function";
    //Do not enter CloseEvent when exiting from the title bar menu, so add the save function here
    //powered by xxxxp
    if (Settings::get().isSet(Settings::ResumeFromLast)) {
        qCDebug(appMovie) << "Enter ~Platform_MainWindow function, isSet(Settings::ResumeFromLast)";
        int nCur = 0;
        nCur = m_pEngine->playlist().current();
        if (nCur >= 0) {
            Settings::get().setInternalOption("playlist_pos", nCur);
        }
    }
    m_pEngine->savePlaybackPosition();
    if (m_pEventListener) {
        qCDebug(appMovie) << "Enter ~Platform_MainWindow function, m_pEventListener not null";   
        this->windowHandle()->removeEventFilter(m_pEventListener);
        delete m_pEventListener;
        m_pEventListener = nullptr;
    }

    disconnect(m_pEngine, 0, 0, 0);
    disconnect(&m_pEngine->playlist(), 0, 0, 0);

    if (m_nLastCookie > 0) {
        utils::UnInhibitStandby(m_nLastCookie);
        qCDebug(appMovie) << "uninhibit cookie" << m_nLastCookie;
        m_nLastCookie = 0;
    }
    if (m_nPowerCookie > 0) {
        qCDebug(appMovie) << "Releasing power inhibit cookie:" << m_nPowerCookie;
        utils::UnInhibitPower(m_nPowerCookie);
        m_nPowerCookie = 0;
    }
    delete m_pEngine;
    m_pEngine = nullptr;

    m_diskCheckThread.stop();

    ThreadPool::instance()->quitAll();

#ifdef USE_DXCB
    if (_evm) {
        qCDebug(appMovie) << "Enter ~Platform_MainWindow function, _evm not null";
        disconnect(_evm, 0, 0, 0);
        delete _evm;
    }
#endif

    if (m_pShortcutViewProcess) {
        qCDebug(appMovie) << "Enter ~Platform_MainWindow function, m_pShortcutViewProcess not null";
        m_pShortcutViewProcess->deleteLater();
        m_pShortcutViewProcess = nullptr;
    }
    qCDebug(appMovie) << "Exit ~Platform_MainWindow function";
}
#include "platform_mainwindow.moc"

