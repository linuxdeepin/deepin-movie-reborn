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
#include <DIconButton>
#include <DButtonBox>
#include <DBlurEffectWidget>
#include "playlist_widget.h"
#include <DFrame>
#include <DLabel>

namespace Dtk
{
namespace Widget
{
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
class SubtitlesView;
class VolumeSlider;
class ViewProgBar;
class viewProgBarLoad;
class PlaylistWidget;
class ImageItem : public DLabel{
    Q_OBJECT
public:
    ImageItem(QPixmap image ,bool isblack = false, QWidget *parent = 0):DLabel(parent){
        _pixmap = image;
    };

signals:
    void imageItemclicked(int index,int indexNow);
protected:
    void paintEvent(QPaintEvent *event){
        QPainter painter(this);
//        painter.drawPixmap(rect(),QPixmap(_path).scaled(60,50));

        painter.setRenderHints(QPainter::HighQualityAntialiasing |
                                QPainter::SmoothPixmapTransform |
                                QPainter::Antialiasing);

        QRect backgroundRect = rect();
        QRect pixmapRect;

        QPainterPath bp1;
        bp1.addRoundedRect(backgroundRect, 2, 2);
        painter.setClipPath(bp1);

        painter.drawPixmap(backgroundRect, _pixmap);

    };
private:
    int _index;
    int _indexNow;
    DLabel *_image=nullptr;
    QString _path = NULL;
    QPixmap _pixmap;
};

class ToolboxProxy: public DFrame {
    Q_OBJECT
public:
    ToolboxProxy(QWidget *mainWindow, PlayerEngine*);
    virtual ~ToolboxProxy();

    void updateTimeInfo(qint64 duration, qint64 pos, QLabel *_timeLabel,
                        QLabel *_timeLabelend ,bool flag);
    bool anyPopupShown() const;
    void closeAnyPopup();
    void setViewProgBarWidth();
    void setPlaylist(PlaylistWidget *playlist);
    void addLabel_list(ImageItem *label){ label_list.append(label);}
    void addLabel_black_list(ImageItem *label_black){ label_black_list.append(label_black);}
    void addpm_list(QList<QPixmap> pm){ pm_list.clear(); pm_list.append(pm);}
    void addpm_black_list(QList<QPixmap> pm_black){ pm_black_list.clear(); pm_black_list.append(pm_black);}
    QLabel *getfullscreentimeLabel();
    QLabel *getfullscreentimeLabelend();
public slots:
    void finishLoadSlot(QSize size);
signals:
    void requestPlay();
    void requestPause();
    void requestNextInList();
    void requesstPrevInList();
    void sigstartLoad(QSize size);

protected slots:
    void updatePosition(const QPoint& p);
    void buttonClicked(QString id);
    void buttonEnter();
    void buttonLeave();
    void updatePlayState();
    void updateFullState();
    void updateVolumeState();
    void updateMovieProgress();
    void updateButtonStates();
    void setProgress(int v);
    void progressHoverChanged(int v);
    void updateHoverPreview(const QUrl& url, int secs);


protected:
    void paintEvent(QPaintEvent *pe) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setup();
    void updateTimeLabel();
    void updateToolTipTheme(ToolButton *btn);
    void updateThumbnail();

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
    SubtitlesView *_subView {nullptr};
    int _lastHoverValue {0};
    QTimer _previewTimer;
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
};
class viewProgBarLoad: public QThread{
    Q_OBJECT
public:
    explicit viewProgBarLoad(PlayerEngine *engine = nullptr,DMRSlider *progBar = nullptr,ToolboxProxy *parent = 0);

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
    PlayerEngine *_engine {nullptr};
    ToolboxProxy *_parent{nullptr};
    int _vlastHoverValue;
    QPoint _startPos;
    bool _isBlockSignals;
    QPoint _indicatorPos {0, 0};
    QColor _indicatorColor;

//    viewProgBarLoad *_viewProgBarLoad{nullptr};
    QWidget *_back{nullptr};
    QWidget *_front{nullptr};
    DBlurEffectWidget *_indicator{nullptr};
    QGraphicsColorizeEffect *m_effect{nullptr};
    QList<QLabel*> labelList ;
    QHBoxLayout *_indicatorLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout{nullptr};
    QHBoxLayout *_viewProgBarLayout_black{nullptr};
    DMRSlider *_progBar {nullptr};
    QSize _size;
    bool isLoad = false;
};
}

//HACK: extent area for progress slider
#define TOOLBOX_TOP_EXTENT  0
#define TOOLBOX_HEIGHT  70
#define TOOLBOX_HEIGHT_EXT (TOOLBOX_HEIGHT + TOOLBOX_TOP_EXTENT)

#endif /* ifndef _DMR_TOOLBOX_PROXY_H */
