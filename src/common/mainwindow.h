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
#ifndef _DMR_MAIN_WINDOW_H
#define _DMR_MAIN_WINDOW_H

#include <QObject>
#include <DMainWindow>
#include <DTitlebar>
#include <DPlatformWindowHandle>
//#include <QtWidgets>
#include <DFrame>
#include <QPainterPath>
#include <DPushButton>
#include <DFloatingMessage>
#include <QDBusAbstractInterface>
#include <QtX11Extras/QX11Info>

#include "widgets/titlebar.h"
#include "animationlabel.h"
#include "diskcheckthread.h"
#include "actions.h"
#include "online_sub.h"

class Presenter;

namespace Dtk {
namespace Widget {
class DImageButton;
class DSettingsDialog;
}
}

DWIDGET_USE_NAMESPACE

class MainWindowEventListener;

namespace dmr {
enum CornerEdge {
    TopLeftCorner = 0,
    TopEdge = 1,
    TopRightCorner = 2,
    RightEdge = 3,
    BottomRightCorner = 4,
    BottomEdge = 5,
    BottomLeftCorner = 6,
    LeftEdge = 7,
    NoneEdge = -1
};

class ToolboxProxy;
class EventMonitor;
class PlaylistWidget;
class PlayerEngine;
class NotificationWidget;
class MovieProgressIndicator;
class MainWindowPropertyMonitor;
class MovieWidget;

class IconButton: public DPushButton
{
public:
    explicit IconButton(QWidget *parent = 0): DPushButton(parent), m_nThemeType(0) {}

    void setIcon(QIcon icon)
    {
        m_icon = icon;
        DPushButton::setIcon(m_icon);
    };

    void changeTheme(int nThemeType = 0)
    {
        m_nThemeType = nThemeType;
        update();
    }
protected:
    void paintEvent(QPaintEvent *pEvent)
    {
        QPainter painter(this);
        QRect backgroundRect = rect();
        //QPainterPath bp1;
        //bp1.addRoundedRect(backgroundRect, 2, 2);
        painter.setPen(Qt::NoPen);
        if (m_nThemeType == 1) {
            painter.setBrush(QBrush(QColor(247, 247, 247, 220)));
        } else if (m_nThemeType == 2) {
            painter.setBrush(QBrush(QColor(42, 42, 42, 220)));
        } else {
            painter.setBrush(QBrush(QColor(247, 247, 247, 220)));
        }
        QPainterPath painterPath;
        painterPath.addRoundedRect(backgroundRect, 15, 15);
        painter.drawPath(painterPath);

        DPushButton::paintEvent(pEvent);
    };
private:
    QIcon m_icon;
    int m_nThemeType;
};

/**
 * @file 主窗口，负责显示和交互
 */
class MainWindow: public DMainWindow
{
    Q_OBJECT
    Q_PROPERTY(bool inited READ inited WRITE setInit NOTIFY initChanged)

signals:
    /**
     * @brief dxcb下窗口激活信号
     */
    void windowEntered();
    /**
     * @brief dxcb下窗口失去焦点信号
     */
    void windowLeaved();
    /**
     * @brief 播放状态改变信号
     */
    void initChanged();
    /**
     * @brief 画面菜单是否可用信号
     */
    void frameMenuEnable(bool);
    /**
     * @brief 播放速度菜单是否可用信号
     */
    void playSpeedMenuEnable(bool);
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    /**
     * @brief 返回窗口播放状态
     */
    bool inited() const
    {
        return m_bInited;
    }
    /**
     * @brief 返回播放引擎对象
     * @return 播放引擎指针
     */
    PlayerEngine *engine()
    {
        return m_pEngine;
    }
    /**
     * @brief 返回工具栏对象
     * @return 工具栏指针
     */
    ToolboxProxy *toolbox()
    {
        return m_pToolbox;
    }
    /**
     * @brief 返回播放列表对象
     * @return 播放列表指针
     */
    PlaylistWidget *playlist()
    {
        return m_pPlaylist;
    }
    /**
     * @brief 用于测试触屏效果
     */
    void setTouched(bool bTouched)
    {
        m_bIsTouch = bTouched;
    }
    /**
     * @brief 判断鼠标是否在窗口内
     * @param 当前鼠标焦点
     * @return 是否在窗口内
     */
    bool judgeMouseInWindow(QPoint pos);
    /**
     * @brief 处理菜单事件
     * @param 菜单项id，表明菜单功能
     * @param 是否是鼠标操作
     * @param 菜单项带的参数
     * @param 是否是快捷键
     */
    void requestAction(ActionFactory::ActionKind, bool bFromUI = false,
                       QList<QVariant> args = {}, bool bShortcut = false);
    bool insideResizeArea(const QPoint &globalPos);
    QMargins dragMargins() const;
    void capturedMousePressEvent(QMouseEvent *pEvent);
    void capturedMouseReleaseEvent(QMouseEvent *pEvent);
    void capturedKeyEvent(QKeyEvent *pEvent);
    void syncStaysOnTop();
    void updateGeometryNotification(const QSize &size);
    void updateContentGeometry(const QRect &rect);
    static QString lastOpenedPath();
    void reflectActionToUI(ActionFactory::ActionKind);
    bool set_playlistopen_clicktogglepause(bool bPlaylistopen);
    NotificationWidget *get_nwComm();
    /**
     * @brief 在读取光盘的时候，直接把光盘挂载点的路径加入到播放列表中
     */
    bool addCdromPath();
    /**
     * @brief 初始化播放列表
     */
    void loadPlayList();
    void setOpenFiles(QStringList &);

#ifdef USE_TEST
    void testMprisapp();
    void testCdrom();
#endif
    void setShowSetting(bool);
    void updateGeometry(CornerEdge edge, QPoint pos);
    void setPresenter(Presenter *);
    /**
     * @brief 获取播放音量
     * @return 播放音量
     */
    int getDisplayVolume();

public slots:
    /**
     * @brief 根据url地址播放影片
     * @param 影片路径
     */
    void play(const QUrl &url);

    void playList(const QList<QString> &listFiles);
    void updateProxyGeometry();
    void suspendToolsWindow();
    void resumeToolsWindow();
    void checkOnlineState(const bool bIsOnline);
    void checkOnlineSubtitle(const OnlineSubtitle::FailReason reason);
    void checkErrorMpvLogsChanged(const QString sPrefix, const QString sText);
    void checkWarningMpvLogsChanged(const QString sPrefix, const QString sText);
    void slotdefaultplaymodechanged(const QString &sKey, const QVariant &value);
#if defined (__aarch64__) || defined (__mips__)
    void syncPostion();
#endif
    /**
     * @brief 设置窗口顶层
     */
    void my_setStayOnTop(const QWidget *pWidget, bool bOn);

    void slotmousePressTimerTimeOut();
    /**
     * @brief 播放引擎状态改变
     */
    void slotPlayerStateChanged();
    /**
     * @brief 窗口焦点改变
     */
    void slotFocusWindowChanged();
    /**
     * @brief 文件加载成功后做的后续操作
     */
    void slotFileLoaded();
    /**
     * @brief 显示是否在缓冲中
     */
    void slotUrlpause(bool bStatus);
    /**
     * @brief 根据字体大小改变显示
     */
    void slotFontChanged(const QFont &font);
    /**
     * @brief 改变静音状态
     */
    void slotMuteChanged(bool bMute);
    /**
     * @brief 改变硬解码模式
     */
    //void slotAwaacelModeChanged(const QString &sKey, const QVariant &value);
    /**
     * @brief 用于测试触屏效果
     */
    void slotVolumeChanged(int nVolume);
protected:
    void showEvent(QShowEvent *pEvent) override;
    void hideEvent(QHideEvent *pEvent) override;
    void closeEvent(QCloseEvent *pEvent) override;
    void resizeEvent(QResizeEvent *pEvent) override;
    void mouseMoveEvent(QMouseEvent *pEvent) override;
    void mousePressEvent(QMouseEvent *pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent *pEvent) override;
    void mouseReleaseEvent(QMouseEvent *pEvent) override;
    void focusInEvent(QFocusEvent *pEvent) override;
    void wheelEvent(QWheelEvent *pEvent) override;

    void keyPressEvent(QKeyEvent *pEvent) override;
    void keyReleaseEvent(QKeyEvent *pEvent) override;
    void moveEvent(QMoveEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *pEvent) override;
    void paintEvent(QPaintEvent *pEvent) override;
    void dragEnterEvent(QDragEnterEvent *pEvent) override;
    void dragMoveEvent(QDragMoveEvent *pEvent) override;
    void dropEvent(QDropEvent *pEvent) override;

    bool event(QEvent *pEvent) override;
    void leaveEvent(QEvent *pEvent) override;

protected slots:
    void setInit(bool bInit);
    void menuItemInvoked(QAction *pAction);
#ifdef USE_DXCB
    void onApplicationStateChanged(Qt::ApplicationState e);
#endif
    void onBindingsChanged();
    void updateActionsState();
    void animatePlayState();
    void resizeByConstraints(bool bForceCentered = false);
    void onWindowStateChanged();

    void miniButtonClicked(QString sId);

    void startBurstShooting();
    void onBurstScreenshot(const QImage &imgFrame, qint64 timestamp);
    void delayedMouseReleaseHandler();
#ifdef USE_DXCB
    void onMonitorButtonPressed(int nX, int nY);
    void onMonitorMotionNotify(int nX, int nY);
    _miniPlayBtn
    void onMonitorButtonReleased(int nX, int nY);

    void updateShadow();
#endif
    void updateMiniBtnTheme(int);
    void diskRemoved(QString sDiskName);

    void sleepStateChanged(bool bSleep);
private:
    void initMember();
    void setupTitlebar();
    void handleSettings(DSettingsDialog *);
    DSettingsDialog *initSettings();
    void updateSizeConstraints();
    void toggleUIMode();

    bool insideToolsArea(const QPoint &pos);
    void switchTheme();
    bool isActionAllowed(ActionFactory::ActionKind kd, bool bFromUI, bool bIsShortcut);
    QString probeCdromDevice();
    void updateWindowTitle();
    //void toggleShapeMask();
    void prepareSplashImages();
    void saveWindowState();
    void loadWindowState();
    void subtitleMatchVideo(const QString &sFileName);
    void defaultplaymodeinit();
    void readSinkInputPath();
    void setAudioVolume(int);
    void setMusicMuted(bool bMuted);
    void popupAdapter(QIcon, QString);
    //void setHwaccelMode(const QVariant &value = -1);

    //Limit video to mini mode size
    void LimitWindowize();
    void mipsShowFullScreen();
    //hide pop windows when dragging window
    void hidePopWindow();
    void adjustPlaybackSpeed(ActionFactory::ActionKind);
    void setPlaySpeedMenuChecked(ActionFactory::ActionKind);
    void setPlaySpeedMenuUnchecked();
    void setMusicShortKeyState(bool bState);

private:
    DFloatingMessage *m_pPopupWid;                  ///操作提示窗
    QLabel *m_pFullScreenTimeLable;                 ///全屏时右上角的影片进度
    QHBoxLayout *m_pFullScreenTimeLayout;           ///右上角的影片进度框布局器
    Titlebar *m_pTitlebar;                          ///标题栏
    ToolboxProxy *m_pToolbox;                       ///工具栏
    PlaylistWidget *m_pPlaylist;                    ///播放列表
    PlayerEngine *m_pEngine;                        ///播放引擎
    AnimationLabel *m_pAnimationlable;              ///点击暂停和播放时动画
    MovieProgressIndicator *m_pProgIndicator;       ///全屏时右上角的系统时间
    QList<QPair<QImage, qint64>> m_listBurstShoots; ///存储连拍截图
    bool m_bInBurstShootMode;                       ///是否处于截图状态
    bool m_bPausedBeforeBurst;                      ///截图时暂停播放
#ifdef __mips__
    QAbstractButton *m_pMiniPlayBtn;
    QAbstractButton *m_pMiniCloseBtn;
    QAbstractButton *m_pMiniQuitMiniBtn;
#else
    DIconButton *m_pMiniPlayBtn;                    ///迷你模式播放按钮
    DIconButton *m_pMiniCloseBtn;                   ///迷你模式关闭按钮
    DIconButton *m_pMiniQuitMiniBtn;                ///退出迷你模式按钮
#endif

    QImage m_imgBgDark;
    QImage m_imgBgLight;
    bool m_bMiniMode;                               ///记录迷你模式
    QRect m_lastRectInNormalMode;                   /// used to restore to recent geometry when quit fullscreen or minVolumeMonitoringi mode
    bool m_bInited;                                 /// the first time a play happens, we consider it inited.
    EventMonitor *m_pEventMonitor;                  ///x11事件处理器
    bool m_bMovieSwitchedInFsOrMaxed;               /// track if next/prev is triggered in fs/maximized mode
    bool m_bDelayedResizeByConstraint;
    bool m_bLightTheme;                             ///是否是浅色主题
    bool m_bWindowAbove;                            ///是否是置顶窗口
    bool m_bMouseMoved;                             ///鼠标是否按下移动
    bool m_bMousePressed;                           ///鼠标是否安下
    bool m_bPlaylistopen_clicktogglepause;
    double m_dPlaySpeed;                            ///当前播放速度

    bool m_bQuitfullscreenstopflag;
    bool m_bQuitfullscreenflag;
    bool m_bMaxfornormalflag;                       ///is the window maximized
    QPoint m_posMouseOrigin;                        ///记录前一次鼠标移动点
    QPoint m_pressPoint;                            ///记录当前鼠标按下时的点
    bool m_bStartMini;                              ///开始进入迷你模式

    enum StateBeforeEnterMiniMode {
        SBEM_None = 0x0,
        SBEM_Above = 0x01,
        SBEM_Fullscreen = 0x02,
        SBEM_PlaylistOpened = 0x04,
        SBEM_Maximized = 0x08,
    };
    int m_nStateBeforeMiniMode;
    Qt::WindowStates m_lastWindowState;
    uint32_t m_nLastCookie;
    uint32_t m_nPowerCookie;
    MainWindowEventListener *m_pEventListener;
    NotificationWidget *m_pDVDHintWid;               ///dvd读取提示
    NotificationWidget *m_pCommHintWid;              ///窗口左上角提示
    QTimer m_autoHideTimer;
    QTimer m_delayedMouseReleaseTimer;
    QUrl m_dvdUrl;                                   ///播放dvd的url
    QProcess *m_pShortcutViewProcess;
    int m_nDisplayVolume;                            ///记录播放音量
    bool m_bIsFree;                                  ///播放器是否空闲，和IDel的定义不同
    static int m_nRetryTimes;                        ///播放失败后重试次数
    bool m_bIsJinJia;                                ///是否是景嘉微显卡
    //add by heyi 解决触屏右键菜单bug
    int m_nLastPressX;                               ///左键按下时保存的点
    int m_nLastPressY;                               ///左键按下时保存的点
    bool m_bIsTouch;                                 ///是否是触摸屏按下
    QTimer m_mousePressTimer;
    qint64 m_nOldDuration;
    qint64 m_nOldElapsed;
    Diskcheckthread m_diskCheckThread;
    bool m_bClosed;                                  ///用于景嘉微显卡下过滤metacall事件
    bool m_bIsFileLoadNotFinished;
    QStringList m_listOpenFiles;
    QString m_sCurrentHwdec;                         ///当前的硬解码模式
    bool m_bProgressChanged;                         ///进度条是否被拖动
    bool m_bFirstInit;
    bool m_bLastIsTouch;
    bool m_bTouchChangeVolume;                       ///是否触发了触屏改变音量
    QDBusInterface *m_pDBus;
    MainWindowPropertyMonitor *m_pMWPM;
    bool m_bIsFirstLoadDBus;
    Presenter *m_pPresenter;
    MovieWidget *m_pMovieWidget;
    bool m_bFinishLoadList;                           ///初始打开时是否完成加载播放列表
};

//窗管返回事件过滤器
class MainWindowPropertyMonitor: public QAbstractNativeEventFilter
{
public:
    explicit MainWindowPropertyMonitor(MainWindow *);
    ~MainWindowPropertyMonitor();
    /**
     * @brief 事件过滤器 cppcheck 误报
     */
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *);

    MainWindow *m_pMainWindow {nullptr};
    xcb_atom_t m_atomWMState;
    QList<unsigned int> m_list;
    bool m_bStart;
};
};


#endif /* ifndef _MAIN_WINDOW_H */


