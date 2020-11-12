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

TEST(requestAction, onlineSub)
{
    MainWindow* w = dApp->getMainWindow();
    PlayerEngine* engine =  w->engine();
    QUrl url(QUrl::fromLocalFile("/home/uos/Videos/天空之眼 高清1080P.mp4"));

    if(engine->addPlayFile(url))
    {
        engine->playByName(url);
    }

    QTest::qWait(1000);
    w->requestAction(ActionFactory::ActionKind::MatchOnlineSubtitle);
    QTest::qWait(1000);
    w->requestAction(ActionFactory::ActionKind::HideSubtitle);
    QTest::qWait(1000);
    w->requestAction(ActionFactory::ActionKind::HideSubtitle);

    QTestEventList testEventList;
    testEventList.addKeyClick(Qt::Key_Left, Qt::ShiftModifier,500); //sub delay
    testEventList.addKeyClick(Qt::Key_Left, Qt::ShiftModifier,500);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ShiftModifier,500);    //sub advance
    testEventList.addKeyClick(Qt::Key_Right, Qt::ShiftModifier,500);
    testEventList.addKeyClick(Qt::Key_Right, Qt::ShiftModifier,500);
    testEventList.simulate(w);
}

TEST(requestAction,windowAbove)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(500);
    w->requestAction(ActionFactory::ActionKind::WindowAbove);   //置顶
    QTest::qWait(500);
    w->requestAction(ActionFactory::ActionKind::WindowAbove);   //取消置顶
}

TEST(requestAction, sound)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Stereo);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::LeftChannel);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::RightChannel);
}

TEST(requestAction, playMode)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::OrderPlay);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::ShufflePlay);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::SinglePlay);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::SingleLoop);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::ListLoop);
}

TEST(requestAction, playSpeed)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::ZeroPointFiveTimes);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::OneTimes);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::OnePointFiveTimes);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Double);
}

TEST(requestAction, frame)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::DefaultFrame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Ratio4x3Frame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Ratio16x9Frame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Ratio16x10Frame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Ratio185x1Frame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::Ratio235x1Frame);

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::ClockwiseFrame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::CounterclockwiseFrame);

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::NextFrame);
    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::PreviousFrame);
}

TEST(requestAction,Mute)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(200);
    w->requestAction(ActionFactory::ActionKind::ToggleMute);
    QTest::qWait(200);
    w->requestAction(ActionFactory::ActionKind::ToggleMute);
}

//TEST(requestAction, subtitle)
//{
//    MainWindow* w = dApp->getMainWindow();
//    w->show();

//    QTest::qWait(300);
//    w->requestAction(ActionFactory::ActionKind::LoadSubtitle);
//    QTest::qWait(300);
//    w->requestAction(ActionFactory::ActionKind::HideSubtitle);
//    QTest::qWait(300);
//    EXPECT_TRUE(false);
//}

TEST(requestAction,goToScreenshotSolder)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::GoToScreenshotSolder);
    EXPECT_TRUE(true);
}

//TEST(requestAction,openFileList)
//{
//    MainWindow *w = dApp->getMainWindow();
//    w->show();

//    QTest::qWait(300);
//    w->requestAction(ActionFactory::ActionKind::OpenFileList);
//    QTest::qWait(300);
//    EXPECT_TRUE(false);
//}

//TEST(requestAction,openDirectory)
//{
//    MainWindow *w = dApp->getMainWindow();
//    w->show();

//    QTest::qWait(300);
//    w->requestAction(ActionFactory::ActionKind::OpenDirectory);
//    QTest::qWait(300);
//    EXPECT_TRUE(false);
//}

TEST(requestAction,openCdrom)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::OpenCdrom);
    EXPECT_TRUE(true);
}

TEST(requestAction, playlistRemoveItem)
{
    MainWindow* w = dApp->getMainWindow();
    w->show();

    QTest::qWait(300);
    w->requestAction(ActionFactory::ActionKind::PlaylistRemoveItem);
}
