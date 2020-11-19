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

#include "dmr_settings.h"
using namespace dmr;

TEST(MainWindow, loadFile)
{
    MainWindow* w = dApp->getMainWindow();
    PlayerEngine* engine =  w->engine();
    QList<QUrl> listPlayFiles;
    listPlayFiles<<QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                <<QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    w->show();
//    QTest::qWait(1000);
//    w->showMinimized();
//    QTest::qWait(1000);
//    w->showNormal();
//    QTest::qWait(1000);
//    w->showMaximized();
    QTest::qWait(1000);
    w->resize(800, 600);
    QTest::qWait(1000);

    const auto &valids = engine->addPlayFiles(listPlayFiles);
    QCOMPARE(engine->isPlayableFile(valids[0]), true);
    engine->playByName(valids[0]);

//    QMetaObject::invokeMethod(w, "startBurstShooting", Qt::QueuedConnection);
    QTest::qWait(500);
    w->move(0,0);
}

TEST(MainWindow,settings)
{
    Settings::get().isSet(Settings::Flag::ClearWhenQuit);
    Settings::get().isSet(Settings::Flag::ShowThumbnailMode);
    Settings::get().isSet(Settings::Flag::AutoSearchSimilar);
    Settings::get().isSet(Settings::Flag::PreviewOnMouseover);
    Settings::get().isSet(Settings::Flag::MultipleInstance);
    Settings::get().isSet(Settings::Flag::PauseOnMinimize);
    Settings::get().settings()->sync();

    Settings::get().setThumbnailState();

    Settings::get().commonPlayableProtocols();
    Settings::get().commonPlayableProtocols();
    Settings::get().iscommonPlayableProtocol("dvb");
    Settings::get().screenshotLocation();
    Settings::get().screenshotNameTemplate();
    Settings::get().screenshotNameSeqTemplate();
}

TEST(MainWindow, resizeWindow)
{
    MainWindow* w = dApp->getMainWindow();
    QTest::qWait(3000); //等待缩略图加载
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
    MainWindow* w = dApp->getMainWindow();

    w->setTouched(true);

    QTest::mouseDClick(w, Qt::LeftButton,Qt::NoModifier, QPoint(), 1000);  //fullscreen

    QCursor::setPos(100,200);
//    QTest::mouseMove(w, QPoint(100,200), 500);
    QTest::mousePress(w, Qt::LeftButton, Qt::MetaModifier, QPoint(100,200), 500);
    QCursor::setPos(400,200);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::MetaModifier, QPoint(400,200), 500);

    QTest::qWait(1000);
    QCursor::setPos(400,100);
    QTest::mousePress(w, Qt::LeftButton, Qt::MetaModifier, QPoint(400,100), 500);
    QCursor::setPos(400,300);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::MetaModifier, QPoint(400,300), 500);

//    QTest::qWait(5000);
    QTest::mouseDClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
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
    w->show();

    QTestEventList testEventList;

    //截图
    testEventList.addKeyClick(Qt::Key_A, Qt::AltModifier, 1000);    //screenshot
    testEventList.addKeyClick(Qt::Key_S, Qt::AltModifier, 1000);    //连拍
    //    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

    //    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 1000); //pause
    //    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 1000); //play
    testEventList.addKeyClick(Qt::Key_Right, Qt::NoModifier, 1000); //fast forward
    testEventList.addKeyClick(Qt::Key_Left, Qt::NoModifier, 1000);  //fast backward

    //    testEventList.addKeyClick(Qt::Key_Return, Qt::NoModifier, 1000);    //fullscreen
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1000);    //playlist
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 500);
    //    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);    //quite fullscreen

    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1000);    //playlist
    testEventList.addKeyClick(Qt::Key_Up, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 2000);
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Delete, Qt::NoModifier, 1000);    //delete from playlist

    //减速播放
    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier, 100);
    for (int i = 0; i<6 ;i++) {
        testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier, 100);
    }
    //加速播放
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier, 300);
    for (int i = 0; i<16 ;i++) {
        testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier, 100);
    }

    //还原播放速度
    testEventList.addKeyClick(Qt::Key_R, Qt::ControlModifier, 500);

    testEventList.simulate(w);
}

TEST(MainWindow, shortCutVolumeAndFrame)
{
    MainWindow* w = dApp->getMainWindow();

    QTestEventList testEventList;

    //mini mode
    testEventList.addKeyClick(Qt::Key_F2, Qt::NoModifier, 1000);
    QTest::mouseMove(w, QPoint(), 1000);
    QTest::qWait(1000);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

    //volume
    for (int i = 0; i<5; i++) {
        testEventList.addKeyClick(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier, 50);    //volume up
    }
    for (int i = 0; i<2; i++) {
        testEventList.addKeyClick(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier, 50);//volume down
    }
    testEventList.addKeyClick(Qt::Key_M, Qt::NoModifier, 500); //mute

    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier, 500); //last frame
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 500); //play

    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 500); //next frame
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 300); //play

    testEventList.simulate(w);
}

TEST(MainWindow, reloadFile)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;

    //openfile
    PlayerEngine* engine =  w->engine();
    QList<QUrl> listPlayFiles;
    listPlayFiles<<QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                <<QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    engine->addPlayFiles(listPlayFiles);
}

TEST(MainWindow, movieInfoDialog)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine *engine  =  w->engine();

    QTest::qWait(1000);
    MovieInfoDialog mid(engine->playlist().currentInfo(), w);
    mid.show();

    DLabel *filePathLbl = mid.findChild<DLabel *>("filePathLabel");
    QTest::qWait(500);
    QTest::mouseMove(filePathLbl, QPoint(), 500);
    //    QPoint point(mid.x(), mid.y());
    //    qDebug() << point;
    //    QTest::mouseMove(&mid, point, 1000);

    QTest::qWait(1000);
    QTest::mouseMove(w, QPoint(200, 300), 500);
    QTest::qWait(500);
    mid.close();
    //    QTest::qWait(1000);

}

TEST(MainWindow, UrlDialog)
{
    MainWindow* w = dApp->getMainWindow();
    //    PlayerEngine *engine  =  w->engine();

    UrlDialog *uDlg = new UrlDialog(w);
    uDlg->show();
    LineEdit *lineEdit = uDlg->findChild<LineEdit *>();

    QTest::mouseMove(lineEdit, QPoint(), 500);
    QTest::keyClicks(lineEdit,
                     QString("https://stream7.iqilu.com/10339/upload_transcode/202002/18/20200218093206z8V1JuPlpe.mp4"),
                     Qt::NoModifier, 10);

    //    QUrl url = uDlg->url();
    //    w->play(url);

    QTest::mouseMove(uDlg->getButton(1), QPoint(), 500);
    QTest::mouseClick(uDlg->getButton(1), Qt::LeftButton, Qt::NoModifier,QPoint(), 1000);
    auto url = uDlg->url();
    w->play(url);
}

TEST(MainWindow, subtitle)
{
    MainWindow* w = dApp->getMainWindow();
    PlayerEngine* engine =  w->engine();

    QTest::qWait(1000);
    engine->loadSubtitle(QFileInfo(QString("/data/home/uos/Videos/subtitle/Hachiko.A.Dog's.Story.ass")));
}

TEST(ToolBox, togglePlayList)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *listBtn = toolboxProxy->listBtn();

    QTest::mouseMove(listBtn, QPoint(), 1000);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier,QPoint(), 1000);
}

TEST(ToolBox, playListWidget)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    ToolButton *listBtn = toolboxProxy->listBtn();
    PlaylistWidget *playlistWidget = w->playlist();
    DListWidget *playlist = playlistWidget->get_playlist();

    QTest::mouseMove(playlist->itemWidget(playlist->item(0)), QPoint(), 500);
    QTest::qWait(1000);
    QTest::mouseMove(playlist->itemWidget(playlist->item(1)), QPoint(), 500);
    QTest::mouseClick(playlist->itemWidget(playlist->item(1)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
    QTest::mouseMove(playlist->itemWidget(playlist->item(0)), QPoint(), 500);
    QTest::mouseClick(playlist->itemWidget(playlist->item(0)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);
    QTest::mouseMove(playlist->itemWidget(playlist->item(1)), QPoint(), 500);
    QTest::mouseDClick(playlist->itemWidget(playlist->item(1)), Qt::LeftButton, Qt::NoModifier, QPoint(), 500);

//    QPoint point(playlist->itemWidget(playlist->item(0))->pos().rx(), playlist->itemWidget(playlist->item(0))->pos().ry()-50);
    QTest::mouseMove(listBtn, QPoint(), 1000);
    QTest::mouseClick(listBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500);
//    QTest::mouseMove(playlist->itemWidget(playlist->item(1)), QPoint(), 500);
//    QTest::mousePress(playlist->itemWidget(playlist->item(1)), Qt::RightButton, Qt::NoModifier, QPoint(), 500);
//    QTest::mouseMove(playlist->itemWidget(playlist->item(0)), point, 1000);
//    QTest::mouseClick(playlist->itemWidget(playlist->item(0)), Qt::LeftButton, Qt::NoModifier, point, 500);
//    QTest::qWait(10000);
}

TEST(ToolBox, progBar)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DMRSlider *progBarSlider = toolboxProxy->getSlider();

    QTest::mouseMove(progBarSlider->slider(), QPoint(), 500);
    QTest::mouseClick(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, QPoint(), 1000);

    QPoint point(progBarSlider->slider()->x()+20, progBarSlider->slider()->y());
    QTest::mouseMove(progBarSlider->slider(), point, 1000);
    QTest::mouseClick(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, point, 1000);

    QPoint startPoint(progBarSlider->slider()->x()+30, progBarSlider->slider()->y());
    QPoint endPoint(progBarSlider->slider()->x()+100, progBarSlider->slider()->y());
    QTest::mouseMove(progBarSlider->slider(), startPoint, 300);
    QTest::mousePress(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, startPoint, 100);
    QTest::mouseMove(progBarSlider->slider(), endPoint, 500);
    QTest::mouseRelease(progBarSlider->slider(), Qt::LeftButton, Qt::NoModifier, endPoint, 500);
}

TEST(ToolBox, playBtnBox)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DButtonBoxButton* playBtn = toolboxProxy->playBtn();
    DButtonBoxButton* nextBtn = toolboxProxy->nextBtn();
    DButtonBoxButton* prevBtn = toolboxProxy->prevBtn();

    QTest::mouseMove(playBtn, QPoint(), 500);
    QTest::mouseClick(playBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);//pause

    QTest::mouseMove(playBtn, QPoint(), 500);
    QTest::mouseClick(playBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);//play

    w->requestAction(ActionFactory::ActionKind::OrderPlay);
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000); //play prev

    w->requestAction(ActionFactory::ActionKind::ShufflePlay);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play prev

    w->requestAction(ActionFactory::ActionKind::SinglePlay);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play prev

    w->requestAction(ActionFactory::ActionKind::SingleLoop);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play prev

    w->requestAction(ActionFactory::ActionKind::ListLoop);
    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play next
    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),500); //play prev
}

TEST(ToolBox, fullScreenBtn)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);

    toolboxProxy->updateSlider();

    QTest::mouseMove(fsBtn, QPoint(), 500);
    QTest::mouseClick(fsBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, volBtn)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    VolumeButton *volBtn = toolboxProxy->volBtn();

    QTest::mouseMove(volBtn, QPoint(), 500);
    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);

    //    QPoint point(volSlider,volBtn->pos().y()-50);
    //    QTest::mouseMove(volBtn, point, 500);
    //    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, quitPlayList)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *listBtn = toolboxProxy->listBtn();

    QTest::mouseMove(listBtn, QPoint(), 500);
    QTest::mouseClick(listBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}
