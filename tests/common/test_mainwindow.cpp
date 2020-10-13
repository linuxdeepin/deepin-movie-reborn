#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QTimer>
#include <QAbstractButton>
#include <DSettingsDialog>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"


using namespace dmr;

TEST(MainWindow, loadFile)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine* egine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                <<QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");

    w->show();

    const auto &valids = egine->addPlayFiles(listPlayFiles);

    egine->playByName(valids[0]);
}

TEST(MainWindow, mouseSimulate)
{
    MainWindow* w = dApp->getMainWindow();

    w->show();

    QTest::mouseMove(w, QPoint(),1000);

    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),2000);//pause
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),2000);//play

    QTest::mouseDClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);  //fullscreen
    QTest::mouseDClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),2000);

    QTest::keyPress(w,Qt::Key_Enter,Qt::NoModifier);
}

TEST(shortcutKey, play)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 1000); //pause
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 1000); //play
    testEventList.addKeyClick(Qt::Key_Right, Qt::NoModifier, 1000); //fast forward
    testEventList.addKeyClick(Qt::Key_Left, Qt::NoModifier, 1000);  //fast backward

    testEventList.addKeyClick(Qt::Key_Return, Qt::NoModifier, 1000);    //fullscreen
    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1000);    //playlist
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);    //quite fullscreen

    testEventList.addKeyClick(Qt::Key_F3, Qt::NoModifier, 1000);    //playlist
    testEventList.addKeyClick(Qt::Key_Up, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Delete, Qt::NoModifier, 1000);    //delete from playlist

    //加速播放
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier, 300);
    for (int i = 0; i<7 ;i++) {
        testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier, 50);
    }

    //减速播放
    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier, 300);
    for (int i = 0; i<5 ;i++) {
        testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier, 50);
    }

    //还原播放速度
    testEventList.addKeyClick(Qt::Key_R, Qt::ControlModifier, 500);

    //movie info dialog
    testEventList.addKeyClick(Qt::Key_Return, Qt::AltModifier, 1000);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

    testEventList.simulate(w);

    EXPECT_TRUE(true);
}

TEST(shortcutKey, volumeAndFrame)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;

    //mini mode
    testEventList.addKeyClick(Qt::Key_F2, Qt::NoModifier, 1000);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 00);

    //volume
    for (int i = 0; i<5; i++) {
        testEventList.addKeyClick(Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier, 100);    //volume up
    }
    for (int i = 0; i<2; i++) {
            testEventList.addKeyClick(Qt::Key_Up, Qt::ControlModifier | Qt::AltModifier, 100);//volume down
    }
    testEventList.addKeyClick(Qt::Key_M, Qt::NoModifier, 1000); //mute

    testEventList.addKeyClick(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier, 500); //last frame
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 100); //play

    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 500); //next frame
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, 100);
    testEventList.addKeyClick(Qt::Key_Space, Qt::NoModifier, 300); //play

    testEventList.simulate(w);
}

TEST(shortcutKey, file)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;

    //openfile
//    testEventList.addKeyClick(Qt::Key_O, Qt::ControlModifier, 1000);
    PlayerEngine* egine =  w->engine();
    QList<QUrl> listPlayFiles;
    listPlayFiles<<QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                <<QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");
    egine->addPlayFiles(listPlayFiles);

    testEventList.addKeyClick(Qt::Key_PageDown, Qt::NoModifier, 1000);
    testEventList.addKeyClick(Qt::Key_PageUp, Qt::NoModifier, 1000);

    testEventList.simulate(w);
}

TEST(ToolBox, togglePlayList)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *listBtn = toolboxProxy->listBtn();

    QTest::mouseMove(listBtn, QPoint(), 2000);
    QTest::mouseClick(listBtn, Qt::LeftButton, Qt::NoModifier,QPoint(), 2000);
}

TEST(ToolBox,stop)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DButtonBoxButton* playBtn = toolboxProxy->playBtn();

    QTest::mouseMove(playBtn, QPoint(), 2000);
//    QTimer::singleShot(2000,[=]{QTest::mouseClick(playBtn,Qt::LeftButton);});
    QTest::mouseClick(playBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, play)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DButtonBoxButton* playBtn = toolboxProxy->playBtn();

    QTest::mouseMove(playBtn, QPoint(), 1000);
//    QTimer::singleShot(2000,[=]{QTest::mouseClick(playBtn,Qt::LeftButton);});
    QTest::mouseClick(playBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);

}

TEST(ToolBox, playNext)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DButtonBoxButton* nextBtn = toolboxProxy->nextBtn();

    QTest::mouseMove(nextBtn, QPoint(), 2000);
//    QTimer::singleShot(2000,[=]{QTest::mouseClick(nextBtn,Qt::LeftButton);});
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, playPrev)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    DButtonBoxButton* prevBtn = toolboxProxy->prevBtn();

    QTest::mouseMove(prevBtn, QPoint(), 1000);
//    QTimer::singleShot(2000,[=]{QTest::mouseClick(prevBtn,Qt::LeftButton);});
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),2000);
}

TEST(ToolBox, fullScreen)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTest::mouseMove(fsBtn, QPoint(), 1000);
//    QTimer::singleShot(2000,[=]{QTest::mouseClick(fsBtn,Qt::LeftButton);});
    QTest::mouseClick(fsBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),2000);
}

TEST(ToolBox, quitFullScreen)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTest::mouseMove(fsBtn, QPoint(), 2000);
//    QTimer::singleShot(1000,[=]{QTest::mouseClick(fsBtn,Qt::LeftButton);});
    QTest::mouseClick(fsBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, volBtn)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    VolumeButton *volBtn = toolboxProxy->volBtn();

    QTest::mouseMove(volBtn, QPoint(), 2000);
//    QTimer::singleShot(1000,[=]{QTest::mouseClick(volBtn,Qt::LeftButton);});
    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);

    QPoint point(volBtn->pos().x(),volBtn->pos().y());
//    QTest::mouseMove(volBtn,point,1000);
    QTest::mouseClick(volBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(ToolBox, quitPlayList)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    ToolboxProxy* toolboxProxy = w->toolbox();
    toolboxProxy->show();

    ToolButton *listBtn = toolboxProxy->listBtn();

    QTest::mouseMove(listBtn, QPoint(), 2000);
//    QTimer::singleShot(5000,[=]{QTest::mouseClick((QWidget*)listBtn,Qt::LeftButton);});
    QTest::mouseClick(listBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}
