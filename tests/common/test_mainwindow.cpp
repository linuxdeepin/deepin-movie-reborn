#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QTimer>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>
#include <QtCore/QMetaObject>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include "src/widgets/slider.h"
#include "src/widgets/movieinfo_dialog.h"
#include "src/widgets/url_dialog.h"
#include "src/widgets/dmr_lineedit.h"
#include "src/common/actions.h"
#include "src/backends/mpv/mpv_glwidget.h"

using namespace dmr;

TEST(MainWindow, loadFile)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();
    QList<QUrl> listPlayFiles;
    listPlayFiles << QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                  << QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    w->show();
//    QTest::qWait(1000);
//    w->showMinimized();
//    QTest::qWait(1000);
//    w->showNormal();
//    QTest::qWait(1000);
//    w->showMaximized();

    const auto &valids = engine->addPlayFiles(listPlayFiles);
    QCOMPARE(engine->isPlayableFile(valids[0]), true);
    engine->playByName(valids[0]);

    QTest::qWait(500);
    w->resize(800, 600);
    QTest::qWait(500);
    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::DarkType); 
    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::DarkType);

//    QMetaObject::invokeMethod(w, "startBurstShooting", Qt::QueuedConnection);
    QTest::qWait(500);

    w->move(0,0);   

    w->customContextMenuRequested(QPoint(200,300));
}

TEST(MainWindow, resizeWindow)
{
    MainWindow *w = dApp->getMainWindow();
    //缩放窗口
//    QPoint bot_right(w->frameGeometry().bottomRight().x()+2, w->frameGeometry().bottomRight().y()+2);
//    QTest::qWait(1000);
//    QTest::mouseMove(w, bot_right, 500);
////    QTest::qWait(10000);
//    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, bot_right, 500);
//    QTest::mouseMove(w, QPoint(bot_right.x()+30, bot_right.y()+40), 500);
//    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(bot_right.x()+30, bot_right.y()+40), 500);

//    //移动窗口
//    QTest::mouseMove(w, QPoint(w->pos().x()+50, w->pos().y()+60), 500);
//    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(w->pos().x()+50, w->pos().y()+60), 500);
//    QTest::mouseMove(w, QPoint(w->pos().x()+300, w->pos().y()+200), 1000);
//    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(w->pos().x()+300, w->pos().y()+200), 2000);

    QTest::qWait(300);
    w->move(200, 200);
    w->updateGeometry(CornerEdge::LeftEdge, QPoint(100, 100));
    w->updateGeometry(CornerEdge::TopEdge, QPoint(100, 100));
    w->updateGeometry(CornerEdge::RightEdge, QPoint(100, 100));
    w->updateGeometry(CornerEdge::BottomEdge, QPoint(100, 100));
    w->updateGeometry(CornerEdge::NoneEdge, QPoint(100, 100));
    w->updateGeometry(CornerEdge::TopLeftCorner, QPoint(100, 100));
    w->updateGeometry(CornerEdge::TopRightCorner, QPoint(1300, 100));
    w->updateGeometry(CornerEdge::BottomLeftCorner, QPoint(100, 100));
    w->updateGeometry(CornerEdge::BottomRightCorner, QPoint(100, 100));
}

TEST(MainWindow, touch)
{
    MainWindow *w = dApp->getMainWindow();

    w->setTouched(true);

    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(100, 200), 1000); //fullscreen

    QTest::mousePress(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(100, 200), 500);
    QTest::mouseRelease(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(200, 200), 500);

    QTest::mousePress(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(200, 200), 500);
    QTest::mouseRelease(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(100, 200), 500);

    QTest::mousePress(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(400, 100), 500);
    QTest::mouseRelease(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(400, 300), 500);

    QTest::mousePress(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(400, 300), 500);
    QTest::mouseRelease(w->windowHandle(), Qt::LeftButton, Qt::MetaModifier, QPoint(400, 100), 500);

    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
    w->setTouched(false);
}

/*TEST(MainWindow, mainContextMenu)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::mouseMove(w, QPoint(),300);

    dmr::ActionFactory::get().mainContextMenu()->popup(QCursor::pos());
    DMenu *menu = dmr::ActionFactory::get().mainContextMenu()->findChild<DMenu *>();
//    QTest::mouseMove(menu, QPoint(),500);
//    QTest::qWait(3000);

    QTest::mouseMove(w, QPoint(w->pos().x()-20, w->pos().y()), 500);
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(), 500);//pause
    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);//play
}*/

TEST(MainWindow, shortCutPlay)
{
    MainWindow* w = dApp->getMainWindow();
    QTestEventList testEventList;

    //截图
    testEventList.addKeyClick(Qt::Key_A, Qt::AltModifier, 1000);    //screenshot
    testEventList.addKeyClick(Qt::Key_S, Qt::AltModifier, 1000);    //连拍

    testEventList.addKeyClick(Qt::Key_Right, Qt::NoModifier, 600); //fast forward
    for(int i=0; i<4; i++){
        testEventList.addKeyClick(Qt::Key_Left, Qt::NoModifier, 200);  //fast backward
    }

    //playlist
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1000);    //playlist popup
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 300);
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 300);      //play selected item
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1700);    //playlist
    testEventList.addKeyClick(Qt::Key_Up, Qt::NoModifier, 300);
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 300);
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1800);
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 400);
    testEventList.addKeyClick(Qt::Key_Delete, Qt::NoModifier, 400);    //delete from playlist

    //加速播放
    for (int i = 0; i<10 ;i++) {
        testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier, 100);
    }
    //减速播放
    for (int i = 0; i<16 ;i++) {
        testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier, 100);
    }

    //还原播放速度
    testEventList.addKeyClick(Qt::Key_R, Qt::ControlModifier, 500);

//    testEventList.addKeyClick(Qt::Key_Tab, Qt::NoModifier, 300);
//    testEventList.addKeyClick(Qt::Key_Tab, Qt::NoModifier, 300);

    testEventList.simulate(w);
}

TEST(MainWindow, shortCutVolumeAndFrame)
{
    MainWindow* w = dApp->getMainWindow();
    QTestEventList testEventList;

    //mini mode
    testEventList.addKeyClick(Qt::Key_F2, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

    //volume
    for (int i = 0; i<6; i++) {
        testEventList.addKeyClick(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier, 20);    //volume down
    }
    for (int i = 0; i<8; i++) {
        testEventList.addKeyClick(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier, 20);//volume up
    }
    testEventList.addKeyClick(Qt::Key_M, Qt::NoModifier, 500); //mute

    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier, 300); //last frame
    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier, 300);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 300); //next frame
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 300); //play

    testEventList.simulate(w);
}

TEST(MainWindow, reloadFile)
{
    MainWindow* w = dApp->getMainWindow();

    //openfile
    PlayerEngine *engine =  w->engine();
    QList<QUrl> listPlayFiles;
    listPlayFiles << QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                  << QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    engine->addPlayFiles(listPlayFiles);
}

TEST(MainWindow, progBar)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    DMRSlider *progBarSlider = toolboxProxy->getSlider();

    //进度条模式
    QPoint point(progBarSlider->slider()->x() + 30, progBarSlider->slider()->y());
    QTest::mouseMove(progBarSlider->slider(), point, 500);
    QTest::mouseMove(progBarSlider->slider(), QPoint(point.x(), point.y() - 40), 500);
    QTest::mouseMove(progBarSlider->slider(), point, 500);
    QTest::mouseClick(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, point, 500);
    //拖动
    QPoint startPoint(progBarSlider->slider()->x()+100, progBarSlider->slider()->y());
    QPoint endPoint(progBarSlider->slider()->x()+10, progBarSlider->slider()->y());
    QTest::mouseMove(progBarSlider->slider(), startPoint, 300);
    QTest::mousePress(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, startPoint, 100);
    QTest::mouseMove(progBarSlider->slider(), endPoint, 500);
    QTest::mouseRelease(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, endPoint, 500);

    //胶片模式
    Settings::get().settings()->setOption("base.play.showInthumbnailmode", true);
    QTest::qWait(4000); //等待胶片加载
    QWidget *viewProgBar = (QWidget *)toolboxProxy->getViewProBar();
    startPoint = QPoint(viewProgBar->x() + 100, viewProgBar->y() + 20);
    endPoint = QPoint(viewProgBar->x() + 20, viewProgBar->y() + 20);
    QTest::mouseMove(viewProgBar, QPoint(viewProgBar->x() + 50, viewProgBar->y() + 20), 500);
    QTest::mouseClick(viewProgBar, Qt::LeftButton, Qt::NoModifier, QPoint(viewProgBar->x() + 50, viewProgBar->y() + 20), 500);
    QTest::mouseMove(viewProgBar, startPoint, 300);
    QTest::mousePress(viewProgBar, Qt::LeftButton, Qt::NoModifier, startPoint, 100);
    QTest::mouseMove(viewProgBar, endPoint, 500);
    QTest::mouseRelease(viewProgBar, Qt::LeftButton, Qt::NoModifier, endPoint, 500);
    QTest::qWait(500);
    Settings::get().settings()->setOption("base.play.showInthumbnailmode", false);
}

TEST(MainWindow, movieInfoDialog)
{
    MainWindow* w = dApp->getMainWindow();
    PlayerEngine *engine  =  w->engine();
    MovieInfoDialog mid(engine->playlist().currentInfo(), w);
    DLabel *filePathLbl = mid.findChild<DLabel *>("filePathLabel");

    mid.setFont(QFont("Times"));
    QTest::qWait(500);
    mid.show();
    QTest::qWait(500);
    QTest::mouseMove(filePathLbl, QPoint(), 500);
    QTest::qWait(1000);
    QTest::mouseMove(w, QPoint(200, 300), 500);
    QTest::qWait(200);
    mid.close();

    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::LightType);
    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::LightType);
    QTest::qWait(200);
    mid.show();
    QTest::mouseMove(filePathLbl, QPoint(), 500);
    QTest::mouseMove(w, QPoint(200, 300), 1000);
    QTest::qWait(100);
    mid.close();
}

TEST(MainWindow, UrlDialog)
{
    MainWindow* w = dApp->getMainWindow();
    UrlDialog *uDlg = new UrlDialog(w);
    LineEdit *lineEdit = uDlg->findChild<LineEdit *>();

    uDlg->show();
    QTest::mouseMove(uDlg->getButton(0), QPoint(), 500);
    QTest::mouseClick(uDlg->getButton(0), Qt::LeftButton, Qt::NoModifier,QPoint(), 500);

    uDlg->show();
    QTest::mouseMove(lineEdit, QPoint(), 500);
    QTest::keyClicks(lineEdit,QString("mail.263.net/"), Qt::NoModifier, 1);
    QTest::mouseMove(uDlg->getButton(1), QPoint(), 500);
    QTest::mouseClick(uDlg->getButton(1), Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);

    uDlg->show();
    QTest::mouseMove(lineEdit, QPoint(), 500);
    QTest::keyClicks(lineEdit,
                     QString("https://stream7.iqilu.com/10339/upload_transcode/202002/18/20200218093206z8V1JuPlpe.mp4"),
                     Qt::NoModifier, 1);
    QTest::mouseMove(uDlg->getButton(1), QPoint(), 500);
    QTest::mouseClick(uDlg->getButton(1), Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
    auto url = uDlg->url();
    w->play(url);
}

TEST(MainWindow, loadSubtitle)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();

    //load subtitles
    QTest::qWait(500);
    engine->loadSubtitle(QFileInfo(QString("/data/home/uos/Videos/subtitle/Hachiko.A.Dog's.Story.ass")));

    //subtitle matches video
//    QTest::qWait(500);
//    w->requestAction(ActionFactory::EmptyPlaylist);

}

TEST(ToolBox, togglePlayList)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();

    QTest::mouseMove(listBtn, QPoint(), 500);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier,QPoint(), 1000);
}

TEST(ToolBox, playListWidget)
{
    MainWindow *w = dApp->getMainWindow();
    ToolboxProxy *toolboxProxy = w->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();
    PlaylistWidget *playlistWidget = w->playlist();
    DListWidget *playlist = playlistWidget->get_playlist();
    PlayItemWidget *playItemWidget = reinterpret_cast<PlayItemWidget *>(playlist->itemWidget(0));

    QTest::mouseMove(playlist->itemWidget(playlist->item(0)), QPoint(), 500);
    QTest::qWait(1000);
    QTest::mouseMove(playlist->itemWidget(playlist->item(1)), QPoint(), 500);
    QTest::mouseClick(playlist->itemWidget(playlist->item(1)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
    QTest::mouseMove(playlist->itemWidget(playlist->item(0)), QPoint(), 500);
    QTest::mouseClick(playlist->itemWidget(playlist->item(0)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
    QTest::mouseMove(playlist->itemWidget(playlist->item(1)), QPoint(), 500);
    QTest::mouseDClick(playlist->itemWidget(playlist->item(1)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);

    QTest::mouseMove(listBtn, QPoint(), 1000);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
}

TEST(ToolBox, playBtnBox)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    DButtonBoxButton* playBtn = toolboxProxy->playBtn();
    DButtonBoxButton* nextBtn = toolboxProxy->nextBtn();
    DButtonBoxButton* prevBtn = toolboxProxy->prevBtn();

    QTest::mouseMove(playBtn, QPoint(), 500);
    QTest::mouseClick(playBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);//pause
    QTest::mouseMove(w, QPoint(200, 300), 200);
    QTest::mouseMove(playBtn, QPoint(), 500);
    QTest::mouseClick(playBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play

    w->requestAction(ActionFactory::ActionKind::OrderPlay);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000); //play prev

    w->requestAction(ActionFactory::ActionKind::ShufflePlay);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play prev

    w->requestAction(ActionFactory::ActionKind::SinglePlay);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play prev

    w->requestAction(ActionFactory::ActionKind::SingleLoop);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play prev

    w->requestAction(ActionFactory::ActionKind::ListLoop);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 500); //play prev
}

TEST(ToolBox, fullScreenBtn)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);

    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::DarkType);
    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::DarkType);

    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
}

TEST(ToolBox, volBtn)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    VolumeButton *volBtn = toolboxProxy->volBtn();

    QTest::mouseMove(volBtn, QPoint(), 500);
    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
    QTest::mouseClick(volBtn,Qt::MiddleButton,Qt::NoModifier,QPoint(),1000);

    //    QPoint point(volSlider,volBtn->pos().y()-50);
    //    QTest::mouseMove(volBtn, point, 500);
    //    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, quitPlayList)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();

    DGuiApplicationHelper::instance()->setThemeType(DGuiApplicationHelper::UnknownType);
    emit DGuiApplicationHelper::instance()->paletteTypeChanged(DGuiApplicationHelper::UnknownType);

    QTest::mouseMove(listBtn, QPoint(), 500);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);
}
