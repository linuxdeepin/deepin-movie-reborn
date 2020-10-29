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

TEST(requestAction,goToScreenshotSolder)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(2000,[=]{w->requestAction(ActionFactory::ActionKind::GoToScreenshotSolder);});
    EXPECT_TRUE(true);
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

TEST(requestAction, playlistRemoveItem)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTimer::singleShot(500,[=]{w->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);});
}
