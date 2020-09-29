#include "src/widgets/toolbox_proxy.h"
#include <gtest/gtest.h>
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include "src/common/actions.h"
#include "src/widgets/burst_screenshots_dialog.h"
#include <unistd.h>
#include <QDebug>
#include <QTimer>
#include <QAbstractButton>
#include "dmr_settings.h"
#include "movieinfo_dialog.h"
#include <DSettingsDialog>


using namespace dmr;

TEST(MainWindow, loadFile)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine* egine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/home/uosf/Videos/unit_test/1.mp4")\
                <<QUrl::fromLocalFile("/home/uosf/Videos/unit_test/2.wmv")\
               <<QUrl::fromLocalFile("/home/uosf/Videos/unit_test/music1.mp3")\
              <<QUrl::fromLocalFile("/home/uosf/Videos/unit_test/music2.mp3");

    w->show();

    const auto &valids = egine->addPlayFiles(listPlayFiles);

    egine->playByName(valids[0]);
}

TEST(MainWindow, loadSubtitle)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::mouseClick(w,Qt::RightButton);
    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::LoadSubtitle);});
    EXPECT_TRUE(false);
}

TEST(MainWindow, sound)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{
        w->requestAction(ActionFactory::ActionKind::Stereo);
        w->requestAction(ActionFactory::ActionKind::LeftChannel);
        w->requestAction(ActionFactory::ActionKind::RightChannel);
    });
    EXPECT_TRUE(false);
}

TEST(MainWindow, playMode)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{
        w->requestAction(ActionFactory::ActionKind::OrderPlay);
        w->requestAction(ActionFactory::ActionKind::ShufflePlay);
        w->requestAction(ActionFactory::ActionKind::SinglePlay);
        w->requestAction(ActionFactory::ActionKind::SingleLoop);
        w->requestAction(ActionFactory::ActionKind::ListLoop);
    });
    EXPECT_TRUE(false);
}

TEST(MainWindow, frame)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{
        w->requestAction(ActionFactory::ActionKind::DefaultFrame);
        w->requestAction(ActionFactory::ActionKind::Ratio4x3Frame);
        w->requestAction(ActionFactory::ActionKind::Ratio16x9Frame);
        w->requestAction(ActionFactory::ActionKind::Ratio16x10Frame);
        w->requestAction(ActionFactory::ActionKind::Ratio185x1Frame);
        w->requestAction(ActionFactory::ActionKind::Ratio235x1Frame);

        w->requestAction(ActionFactory::ActionKind::ClockwiseFrame);
        w->requestAction(ActionFactory::ActionKind::CounterclockwiseFrame);

        w->requestAction(ActionFactory::ActionKind::NextFrame);
        w->requestAction(ActionFactory::ActionKind::PreviousFrame);
    });
    EXPECT_TRUE(false);
}

TEST(MainWindow, hideSubtitle)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::HideSubtitle);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(MainWindow, fullscreen)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::ToggleFullscreen);});

//    QTestEventList testEventList;

//    testEventList.addKeyClick(Qt::Key_Enter);
//    testEventList.simulate(w);
    QCOMPARE(w->isFullScreen(),true);
}

TEST(MainWindow,quitFullScreen)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::QuitFullscreen);});
    QCOMPARE(w->isFullScreen(),false);
}

TEST(MainWindow,toggleMini)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMiniMode);});
}

TEST(MainWindow,quitMini)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMiniMode);});
}

TEST(MainWindow,toggleMute)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMute);});
}

TEST(MainWindow,quiteMute)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMute);});
}

TEST(MainWindow,movieInfo)
{    //closeBtn was generated in the constructor,cannot call it

    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::MovieInfo);});
    QTimer::singleShot(1000,[=]{EXPECT_TRUE(false);});
}

TEST(MainWindow,windowAbove)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::WindowAbove);});
}

TEST(MainWindow,quiteAbove)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::WindowAbove);});
}

TEST(MainWindow,screenshot)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::Screenshot);});
}

TEST(MainWindow,goToScreenshotSolder)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::GoToScreenshotSolder);});
    EXPECT_TRUE(true);
}

TEST(MainWindow,seekForward)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::SeekForward);});
    EXPECT_TRUE(true);
}

TEST(MainWindow,seekForwardLarge)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::SeekForwardLarge);});
    EXPECT_TRUE(false);
}

TEST(MainWindow,seekBackward)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::SeekBackward);});
    EXPECT_TRUE(false);
}

TEST(MainWindow,seekBackwardLarge)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::SeekBackwardLarge);});
    EXPECT_TRUE(false);
}

//TEST(MainWindow,togglePlayList)
//{
//    MainWindow* w = dApp->getMainWindow();
//    w->show();

//    w->requestAction(ActionFactory::ActionKind::TogglePlaylist);

////    QTestEventList testEventList;

////    testEventList.addKeyClick(Qt::Key_Space);
////    testEventList.simulate(w);
//}

TEST(MainWindow,gotoPlaylistNext)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=](){w->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);});
}

TEST(MainWindow,gotoPlaylistPrev)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=](){w->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);});
}

TEST(MainWindow,openFileList)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();
    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::OpenFileList);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(MainWindow,openDirectory)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::OpenDirectory);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(MainWindow,openCdrom)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    w->requestAction(ActionFactory::ActionKind::OpenCdrom);
//    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::OpenCdrom);});
    EXPECT_TRUE(true);
}

TEST(MainWindow,openUrl)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

//    w->dlg = new UrlDialog(w);
    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::OpenUrl);});

//    QTimer::singleShot(1000,[=]{
//        w->dlg->show();
//        w->dlg->setFocus();

//        LineEdit * lineEdit = w->dlg->getLineEdit();
//        QTest::keyClicks((QWidget*)lineEdit, "hello world");

//        QTimer::singleShot(1000,[=]{w->dlg->close();});
//    });

//    QTimer::singleShot(1000,[=]{QTest::keyClick(w->dlg,Qt::Key_Escape);});

    EXPECT_TRUE(false);
}

TEST(MainWindow, settings)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

//    QTimer::singleShot(1000,[=]{
//        w->showSettingsDialog();
////        DSettingsDialog* dsd = new DSettingsDialog(w);
////        dsd->show();

////        QTimer::singleShot(1000,[=]{dsd->close();});
//    });
    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::Settings);});

    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(MainWindow, volumeUp)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    w->requestAction(ActionFactory::ActionKind::VolumeUp);
}

TEST(MainWindow, volumeDown)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    w->requestAction(ActionFactory::ActionKind::VolumeDown);
}

TEST(MainWindow, playlistRemoveItem)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);});
}

TEST(MainWindow,quitePlaylist)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::TogglePlaylist);});
    EXPECT_TRUE(false);
}

TEST(MainWindow, menuLightTheme)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::LightTheme,true);});
}

TEST(MainWindow, menuAbout)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::About);});
}

TEST(MainWindow, menuHelp)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

//    w->requestAction(ActionFactory::ActionKind::Help);
    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::Help);});
}

TEST(MainWindow, emptyPlaylist)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::EmptyPlaylist);});
}

//TEST(MainWindow, menuExit)
//{
//    MainWindow* w = dApp->getMainWindow();
//    w->show();

//    w->requestAction(ActionFactory::ActionKind::Exit);
//}

/*TEST(MainWindow, reloadFile)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine* engine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/home/uosf/Videos/1.mp4")\
                <<QUrl::fromLocalFile("/home/uosf/Videos/2.mp4")\
               <<QUrl::fromLocalFile("/home/uosf/Videos/3.mp4");
    const auto &valids = engine->addPlayFiles(listPlayFiles);

    engine->playByName(valids[0]);

}*/
