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

TEST(requestAction,quitFullScreen)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(4000,[=]{w->requestAction(ActionFactory::ActionKind::QuitFullscreen);});
}

TEST(requestAction,toggleMini)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(6000,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMiniMode);});
}

TEST(requestAction,quitMini)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(8000,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMiniMode);});
}

TEST(requestAction,windowAbove)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::WindowAbove);});
}

TEST(requestAction,quiteAbove)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::WindowAbove);});
}

TEST(requestAction,togglePlayList)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(6000,[=]{w->requestAction(ActionFactory::ActionKind::TogglePlaylist);});
}

TEST(requestAction,gotoPlaylistNext)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=](){w->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);});
}

TEST(requestAction,screenshot)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::Screenshot);});
}

TEST(requestAction,gotoPlaylistPrev)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=](){w->requestAction(ActionFactory::ActionKind::GotoPlaylistNext);});
}

TEST(requestAction, sound)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{
        w->requestAction(ActionFactory::ActionKind::Stereo);
        w->requestAction(ActionFactory::ActionKind::LeftChannel);
        w->requestAction(ActionFactory::ActionKind::RightChannel);
    });
    QTimer::singleShot(500,[=]{EXPECT_TRUE(true);});
}

TEST(requestAction, playMode)
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
    EXPECT_TRUE(true);
}

TEST(requestAction, frame)
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
    EXPECT_TRUE(true);
}

TEST(requestAction,toggleMute)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMute);});
}

TEST(requestAction,quiteMute)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::ToggleMute);});
}

TEST(requestAction, volumeUp)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    w->requestAction(ActionFactory::ActionKind::VolumeUp);
}

TEST(requestAction, volumeDown)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    w->requestAction(ActionFactory::ActionKind::VolumeDown);
}

TEST(requestAction, loadSubtitle)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::mouseClick(w,Qt::RightButton);
    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::LoadSubtitle);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(true);});
}

TEST(requestAction, hideSubtitle)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::HideSubtitle);});
    EXPECT_TRUE(true);
}

TEST(requestAction,movieInfo)
{    //closeBtn was generated in the constructor,cannot call it

    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::MovieInfo);});
    QTimer::singleShot(2000,[=]{EXPECT_TRUE(false);});
}

TEST(requestAction,goToScreenshotSolder)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::GoToScreenshotSolder);});
    EXPECT_TRUE(true);
}

TEST(requestAction,seekForward)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::SeekForward);});
    EXPECT_TRUE(true);
}

TEST(requestAction,seekForwardLarge)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::SeekForwardLarge);});
    EXPECT_TRUE(false);
}

TEST(requestAction,seekBackward)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::SeekBackward);});
    EXPECT_TRUE(false);
}

TEST(requestAction,seekBackwardLarge)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::SeekBackwardLarge);});
    EXPECT_TRUE(false);
}

TEST(requestAction,openFileList)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();
    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::OpenFileList);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(requestAction,openDirectory)
{
    MainWindow *w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(1000,[=]{w->requestAction(ActionFactory::ActionKind::OpenDirectory);});
    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(requestAction,openCdrom)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::OpenCdrom);});
    EXPECT_TRUE(true);
}

TEST(requestAction,openUrl)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

//    w->dlg = new UrlDialog(w);
    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::OpenUrl);});

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

TEST(requestAction, settings)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

//    QTimer::singleShot(1000,[=]{
//        w->showSettingsDialog();
////        DSettingsDialog* dsd = new DSettingsDialog(w);
////        dsd->show();

////        QTimer::singleShot(1000,[=]{dsd->close();});
//    });
    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::Settings);});

    QTimer::singleShot(500,[=]{EXPECT_TRUE(false);});
}

TEST(requestAction, playlistRemoveItem)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);});
}
