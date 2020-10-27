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
#ifndef _DMR_TOOLBOX_PROXY_H
#define _DMR_TOOLBOX_PROXY_H

#include <DPlatformWindowHandle>
//#include <QtWidgets>
#include <DWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QTimer>
#include <QMutex>
#include <DIconButton>
#include <DButtonBox>
#include <DBlurEffectWidget>
#include "playlist_widget.h"
#include <DFrame>
#include <DLabel>
#include <DFloatingWidget>
#include "dguiapplicationhelper.h"
#include "videoboxbutton.h"
#include "filter.h"

#include "thumbnail_worker.h"

#include "slider.h"

namespace Dtk {
namespace Widget {
class DImageButton;
}
}

DWIDGET_USE_NAMESPACE

namespace dmr {
class PlayerEngine;
class VolumeButton;
class ToolButton;
class MainWindow;
class DMRSlider;
class ThumbnailPreview;
class SliderTime;
class SubtitlesView;
class VolumeSlider;
class ViewProgBar;
class viewProgBarLoad;
class PlaylistWidget;
class ImageItem : public DLabel
{
    Q_OBJECT
public:
    ImageItem(QPixmap image, bool isblack = false, QWidget *parent = nullptr): DLabel(parent), _pixmap(image)
    {
    }

signals:
    void imageItemclicked(int index, int indexNow);
protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
//        painter.drawPixmap(rect(),QPixmap(_path).scaled(60,50));

        painter.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform |
                               QPainter::Antialiasing);

        QSize size(_pixmap.size());
        QBitmap mask(size);

        QPainter painter1(&mask);
        painter1.setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform |
                                QPainter::Antialiasing);
        painter1.fillRect(mask.rect(), Qt::white);
        painter1.setBrush(QColor(0, 0, 0));
        painter1.drawRoundedRect(mask.rect(), 5, 5);

        QPixmap image = _pixmap;
        image.setMask(mask);

        painter.setClipping(true);
        QPainterPath bg0;
        bg0.addRoundedRect(rect(), 5, 5);
        painter.setClipPath(bg0);
        painter.drawPixmap(rect(), image);

        QPen pen;
        pen.setWidth(2);
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            pen.setColor(QColor(0, 0, 0, int(0.1 * 255)));
            painter.setPen(pen);
        } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            pen.setColor(QColor(255, 255, 255, int(0.1 * 255)));
            painter.setPen(pen);
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect(), 5, 5);
    }
private:
//    int _index;   //not used
//    int _indexNow;    //not used
    DLabel *_image = nullptr;
    QString _path = nullptr;
    QPixmap _pixmap;
};

class IndicatorItem : public QWidget
{
    Q_OBJECT
public:
    explicit IndicatorItem(QWidget *parent = nullptr): QWidget(parent)
    {
    }

    void setPressed(bool bPressed)
    {
        m_bIsPressed = bPressed;
    }

protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        QRect backgroundRect = rect();

        painter.setRenderHints(QPainter::HighQualityAntialiasing |
                               QPainter::SmoothPixmapTransform |
                               QPainter::Antialiasing);

        QPainterPath bpath;

        if (!m_bIsPressed) {
            QPen pen;
            pen.setWidth(1);
            pen.setColor(QColor(0, 0, 0));
            bpath.addRoundedRect(backgroundRect, 3, 3);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.setOpacity(0.4);
            painter.fillPath(bpath,QColor(0,0,0));

            //改变一下paint的顺序锯齿效果没有那么明显
            QPainterPath bpath1;
            painter.setOpacity(1);
            bpath1.addRoundedRect(backgroundRect.marginsRemoved(QMargins(1, 1, 1, 1)), 3, 3);
            painter.fillPath(bpath1, QColor(255, 255, 255));
        } else {
            painter.fillRect(backgroundRect, QBrush(QColor(255, 138, 0)));
        }
    }

private:
    bool m_bIsPressed {false};
};

class ToolboxProxy: public DFloatingWidget
{
    Q_OBJECT
public:
    ToolboxProxy(QWidget *mainWindow, PlayerEngine *);
    virtual ~ToolboxProxy() override;

    void updateTimeInfo(qint64 duration, qint64 pos, QLabel *_timeLabel,
                        QLabel *_timeLabelend, bool flag);
    bool anyPopupShown() const;
    void closeAnyPopup();
    void setViewProgBarWidth();
    void setPlaylist(PlaylistWidget *playlist);
    void addLabel_list(ImageItem *label)
    {
        label_list.append(label);
    }
    void addLabel_black_list(ImageItem *label_black)
    {
        label_black_list.append(label_black);
    }
    void addpm_list(QList<QPixmap> &pm)
    {
        pm_list.clear();
        pm_list.append(pm);
    }
    void addpm_black_list(QList<QPixmap> &pm_black)
    {
        pm_black_list.clear();
        pm_black_list.append(pm_black);
    }
    QLabel *getfullscreentimeLabel();
    QLabel *getfullscreentimeLabelend();
    bool getbAnimationFinash();
    int DisplayVolume();
    void setVolSliderHide();
    bool getVolSliderIsHided();
    void setButtonTooltipHide();
    void updateVolumeStateOnStopMode(uint64_t vol);

     //lmh0910初始化下方按键的tooltip
     void initToolTip();
    DMRSlider* getSlider()
    {
        return _progBar;
    }
    ViewProgBar *getViewProBar()
    {
        return _viewProgBar;
    }
    bool isViewProgress()
    {
        if (_progBar_Widget->currentIndex() == 2) {
            return true;
        };
    }

    void updateProgress(int nValue);    //更新进度条显示

    void updateSlider();                //根据进度条显示更新影片实际进度
    void initThumb();

    /////add for unit test/////
    DButtonBoxButton* playBtn(){return _playBtn;}
    DButtonBoxButton* prevBtn(){return _prevBtn;}
    DButtonBoxButton* nextBtn(){return _nextBtn;}
    ToolButton* listBtn(){return _listBtn;}
    ToolButton* fsBtn(){return _fsBtn;}
    VolumeButton* volBtn(){return _volBtn;}


public slots:
    void finishLoadSlot(QSize size);
    void updateplaylisticon();
    void setthumbnailmode();
    void setDisplayValue(int);
signals:
    void requestPlay();
    void requestPause();
    void requestNextInList();
    void requesstPrevInList();
    void sigstartLoad(QSize size);

protected slots:
//    void updatePosition(const QPoint &p);
    void buttonClicked(QString id);
    void buttonEnter();
    void buttonLeave();
    void updatePlayState();
    void updateFullState();
    void updateVolumeState();
    void updateMovieProgress();
    void updateButtonStates();
    void setProgress(int v);
    void updateTimeVisible(bool visible);
    /**
       更新预览图位置
    */
    void progressHoverChanged(int v);
    void updateHoverPreview(const QUrl &url, int secs);
    //lmh0706暂停延时，解决乱按卡死问题
    void waitPlay();
    //把lambda表达式改为槽函数，modify by myk
    void slotThemeTypeChanged();
    void slotLeavePreview();
    void slotLeaveSlider();
    void slotSliderPressed();
    void slotSliderReleased();
    void slotBaseMuteChanged(QString sk, const QVariant & val);
    void slotLeaveViewProgBar();
    void slotVolumeButtonClicked();
    void slotRequestVolumeUp();
    void slotRequestVolumeDown();
    void slotFileLoaded();
    //原有两个连接，合并为一个
    void slotElapsedChanged();
    void slotApplicationStateChanged(Qt::ApplicationState e);
    void slotPlayListCurrentChanged();
    void slotPlayListStateChange();
    void slotUpdateThumbnailTimeOut();
    void slotProAnimationFinished();

protected:
//    void paintEvent(QPaintEvent *pe) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
private:
    void setup();
    void updateTimeLabel();
    void updateToolTipTheme(ToolButton *btn);
    void updateThumbnail();
    void updatePreviewTime(qint64 secs, const QPoint &pos);
    void installHint(QWidget *w, QWidget *hint);

    QLabel *_fullscreentimelable {nullptr};
    QLabel *_fullscreentimelableend {nullptr};

    MainWindow *_mainWindow {nullptr};
    PlayerEngine *_engine {nullptr};
    PlaylistWidget *_playlist {nullptr};
    QLabel *_timeLabel {nullptr};
    QLabel *_timeLabelend {nullptr};
    VolumeSlider *_volSlider {nullptr};

//    DImageButton *_playBtn {nullptr};
//    DIconButton *_playBtn {nullptr};
//    DIconButton *_prevBtn {nullptr};
//    DIconButton *_nextBtn {nullptr};

    DButtonBoxButton *_playBtn {nullptr};
    DButtonBoxButton *_prevBtn {nullptr};
    DButtonBoxButton *_nextBtn {nullptr};
    DButtonBox *_palyBox{nullptr};

//    DIconButton *_subBtn {nullptr};
    VolumeButton *_volBtn {nullptr};
//    DIconButton *_listBtn {nullptr};
//    DIconButton *_fsBtn {nullptr};
    ToolButton *_subBtn {nullptr};
    ToolButton *_listBtn {nullptr};
    ToolButton *_fsBtn {nullptr};

    QHBoxLayout *_mid{nullptr};
    QHBoxLayout *_right{nullptr};
    ViewProgBar *_viewProgBar{nullptr};

    DMRSlider *_progBar {nullptr};
    DWidget *_progBarspec {nullptr};
    ThumbnailPreview *_previewer {nullptr};
    SliderTime *_previewTime {nullptr};
    SubtitlesView *_subView {nullptr};
    int _lastHoverValue {0};
    QWidget *_bot_spec {nullptr};
    QWidget *bot_toolWgt {nullptr};

//    QStackedLayout *_progBar_stacked {nullptr};
    QStackedWidget *_progBar_Widget {nullptr};
    QTimer _autoResizeTimer;
    QSize _oldsize;
    QSize _loadsize;
    bool _isresize;
//    viewProgBarLoad *_viewProgBarLoad{nullptr};
//    QThread *_loadThread{nullptr};
    QList<ImageItem *>label_list ;
    QList<ImageItem *>label_black_list;
    QList<QPixmap >pm_list ;
    QList<QPixmap >pm_black_list ;

    viewProgBarLoad *m_worker = nullptr;
    bool m_mouseFlag = false;
    bool m_mousePree = false;   //thx
    int m_mouseRelesePos = 0;
    bool _bthumbnailmode;
    bool isStillShowThumbnail{true};

    //动画是否完成
    bool bAnimationFinash {true};

    QPropertyAnimation *paopen;
    QPropertyAnimation *paClose;

    QMutex m_listPixmapMutex;       //缩略图list的锁


    QString m_UrloldThumbUrl;       //当前加载的文件，目的是为缩略图服务

    DBlurEffectWidget *bot_widget {nullptr };
    HintFilter        *hintFilter {nullptr };
    bool m_isMouseIn = false;
    QTimer _hideTime;
    bool _isJinJia = false;//是否是景嘉微显卡
    qint64 oldDuration = 0;
    qint64 oldElapsed = 0;
    QTimer _progressTimer;
};
class viewProgBarLoad: public QThread
{
    Q_OBJECT
public:
    explicit viewProgBarLoad(PlayerEngine *engine = nullptr, DMRSlider *progBar = nullptr, ToolboxProxy *parent = nullptr);

    //退出线程直接调用这个函数
    void quitLoad();
    //告诉线程需要加载一个缩略图，线程会停止正在加载的项目，重新加载新的缩略图
    void load();
    //必须调用这个函数加锁
    void setListPixmapMutex(QMutex *pMutex);
public slots:
    void loadViewProgBar(QSize size);
signals:
    void leaveViewProgBar();
    void hoverChanged(int);
    void sliderMoved(int);
    void indicatorMoved(int);
    void sigFinishiLoad(QSize size);
    void finished();

protected:
    void run();
private:
    void initThumb();
    PlayerEngine *_engine {nullptr};
    ToolboxProxy *_parent{nullptr};
/*    int _vlastHoverValue;
    QPoint _startPos;
    bool _isBlockSignals;
    QPoint _indicatorPos {0, 0};
    QColor _indicatorColor;

    viewProgBarLoad *_viewProgBarLoad{nullptr};
    QWidget *_back{nullptr};
    QWidget *_front{nullptr};
    DBlurEffectWidget *_indicator{nullptr};
    QGraphicsColorizeEffect *m_effect{nullptr};
    QList<QLabel *> labelList ;
    QHBoxLayout *_indicatorLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout_black{nullptr};
*/
    DMRSlider *_progBar {nullptr};
//    QSize _size;

    //加载缩略图是否加载完成，控制线程是否休眠
    bool m_bisload {false};
    //是否退出当前线程(退出while(1))
    bool m_bQuit  {false};

    QMutex m_mutex;

    QMutex *pListPixmapMutex;

    video_thumbnailer *m_video_thumbnailer = nullptr;
    image_data *m_image_data = nullptr;

    mvideo_thumbnailer m_mvideo_thumbnailer = nullptr;
    mvideo_thumbnailer_destroy m_mvideo_thumbnailer_destroy = nullptr;
    mvideo_thumbnailer_create_image_data m_mvideo_thumbnailer_create_image_data = nullptr;
    mvideo_thumbnailer_destroy_image_data m_mvideo_thumbnailer_destroy_image_data = nullptr;
    mvideo_thumbnailer_generate_thumbnail_to_buffer m_mvideo_thumbnailer_generate_thumbnail_to_buffer = nullptr;

};
}

//HACK: extent area for progress slider
#define TOOLBOX_TOP_EXTENT  0
#define TOOLBOX_SPACE_HEIGHT 314
#define TOOLBOX_HEIGHT  80
#define TOOLBOX_HEIGHT_EXT (TOOLBOX_HEIGHT + TOOLBOX_TOP_EXTENT)

#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
