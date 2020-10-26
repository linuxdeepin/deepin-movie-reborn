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
#include "src/widgets/slider.h"
#include "src/widgets/movieinfo_dialog.h"
#include "src/widgets/url_dialog.h"
#include "src/widgets/dmr_lineedit.h"

#include "dmr_settings.h"
TEST(Settings,settings)
{
    using namespace dmr;
    Settings::get().isSet(Settings::Flag::ClearWhenQuit);
    Settings::get().isSet(Settings::Flag::ShowThumbnailMode);
    Settings::get().isSet(Settings::Flag::AutoSearchSimilar);
    Settings::get().isSet(Settings::Flag::PreviewOnMouseover);
    Settings::get().isSet(Settings::Flag::MultipleInstance);
    Settings::get().isSet(Settings::Flag::PauseOnMinimize);
    Settings::get().settings()->sync();

    Settings::get().commonPlayableProtocols();
    Settings::get().commonPlayableProtocols();
    Settings::get().iscommonPlayableProtocol("dvb");
    Settings::get().screenshotLocation();
    Settings::get().screenshotNameTemplate();
    Settings::get().screenshotNameSeqTemplate();
}

using namespace dmr;

TEST(MainWindow, loadFile)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine* engine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/usr/share/dde-introduction/demo.mp4")\
                <<QUrl::fromLocalFile("/usr/share/music/bensound-sunny.mp3");

    w->show();

    const auto &valids = engine->addPlayFiles(listPlayFiles);

    engine->playByName(valids[0]);
}

TEST(MainWindow, mouseSimulate)
{
    MainWindow* w = dApp->getMainWindow();

    w->show();

    QTest::qWait(3000); //等待加载胶片进度条
    QTest::mouseMove(w, QPoint(),300);
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),500);//pause
    QTest::mouseClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);//play

    QTest::mouseDClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);  //fullscreen
    QTest::mouseDClick(w,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
}

TEST(MainWindow, shortCutPlay)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;

    //截图
    testEventList.addKeyClick(Qt::Key_A, Qt::AltModifier, 1000);    //screenshot
    testEventList.addKeyClick(Qt::Key_S, Qt::AltModifier, 1000);    //连拍
//    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

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
    testEventList.addKeyClick(Qt::Key_Enter, Qt::NoModifier, 500);
    testEventList.addKeyClick(Qt::Key_Down, Qt::NoModifier, 500);
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

//    //movie info dialog
//    testEventList.addKeyClick(Qt::Key_Return, Qt::AltModifier, 1000);
//    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

    testEventList.simulate(w);
}

TEST(MainWindow, shortCutVolumeAndFrame)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTestEventList testEventList;

    //mini mode
    testEventList.addKeyClick(Qt::Key_F2, Qt::NoModifier, 1000);
    QTest::qWait(2000);
    testEventList.addKeyClick(Qt::Key_Escape, Qt::NoModifier, 1000);

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

//    testEventList.addKeyClick(Qt::Key_PageDown, Qt::NoModifier, 1000);
//    testEventList.addKeyClick(Qt::Key_PageUp, Qt::NoModifier, 1000);

//    testEventList.simulate(w);
}

TEST(MainWindow, movieInfoDialog)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine *engine  =  w->engine();

    QTest::qWait(1000);
    MovieInfoDialog mid(engine->playlist().currentInfo(), w);
    mid.show();

//    QPoint point(mid.x(), mid.y());
//    qDebug() << point;
//    QTest::mouseMove(&mid, point, 1000);

    QTest::qWait(2000);
    mid.close();
    QTest::qWait(1000);

}

TEST(MainWindow, UrlDialog)
{
    MainWindow* w = dApp->getMainWindow();
//    PlayerEngine *engine  =  w->engine();

    UrlDialog *uDlg = new UrlDialog(w);
    uDlg->show();
    LineEdit *lineEdit = uDlg->findChild<LineEdit *>();

    QTest::mouseMove(lineEdit, QPoint(), 500);
    QTest::keyClicks(lineEdit, QString("www.baidu.com"), Qt::NoModifier, 10);

    QUrl url = uDlg->url();
    w->play(url);

    QTest::mouseMove(uDlg->getButton(1), QPoint(), 500);
    QTest::mouseClick(uDlg->getButton(1), Qt::LeftButton, Qt::NoModifier,QPoint(), 1000);

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

    QTest::mouseMove(nextBtn, QPoint(), 500);
    QTest::mouseClick(nextBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);

    QTest::mouseMove(prevBtn, QPoint(), 500);
    QTest::mouseClick(prevBtn,Qt::LeftButton,Qt::NoModifier,QPoint(),1000);
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
