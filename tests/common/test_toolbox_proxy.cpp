#include "src/widgets/toolbox_proxy.h"
#include "src/widgets/toolbutton.h"
#include "src/widgets/playlist_widget.h"
#include <gtest/gtest.h>
#include "src/common/mainwindow.h"
#include "src/libdmr/player_engine.h"
#include "application.h"
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <DSlider>
#include <DListWidget>
#include <QMenu>
#include "presenter.h"

using namespace dmr;

TEST(ToolBox, reloadFile)
{
    MainWindow* w = dApp->getMainWindow();

    PlayerEngine* egine =  w->engine();

    QList<QUrl> listPlayFiles;

    listPlayFiles<<QUrl::fromLocalFile("/home/uosf/Videos/3.mp4")\
                <<QUrl::fromLocalFile("/home/uosf/Videos/4.mp4")\
               <<QUrl::fromLocalFile("/home/uosf/Music/music1.mp3");
    w->show();

    const auto &valids = egine->addPlayFiles(listPlayFiles);

    egine->playByName(valids[0]);
}
TEST(ToolBox, togglePlayList)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();

    ToolButton *listBtn = toolboxProxy->listBtn();

        QTimer::singleShot(1000,[=]{
            QTest::mouseMove(listBtn, listBtn->pos());
        });
    QTimer::singleShot(1000,[=]{QTest::mouseClick(listBtn,Qt::LeftButton);});
}

TEST(Toolbox,stop)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();
    DButtonBoxButton* playBtn = toolboxProxy->playBtn();

    QTest::mouseMove(playBtn);
    QTimer::singleShot(2000,[=]{QTest::mouseClick(playBtn,Qt::LeftButton);});
}

TEST(ToolBox, play)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();
    DButtonBoxButton* playBtn = toolboxProxy->playBtn();

    QTimer::singleShot(2000,[=]{QTest::mouseClick(playBtn,Qt::LeftButton);});
}

TEST(ToolBox, playNext)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    DButtonBoxButton* nextBtn = toolboxProxy->nextBtn();

    QTest::mouseMove(nextBtn);
    QTimer::singleShot(2000,[=]{QTest::mouseClick(nextBtn,Qt::LeftButton);});
}

TEST(ToolBox, playPrev)
{
    MainWindow* w = dApp->getMainWindow();
    ToolboxProxy* toolboxProxy = w->toolbox();
    DButtonBoxButton* prevBtn = toolboxProxy->prevBtn();

    QTest::mouseMove(prevBtn);
    QTimer::singleShot(2000,[=]{QTest::mouseClick(prevBtn,Qt::LeftButton);});
}

TEST(ToolBox, fullScreen)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();

    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTest::mouseMove(fsBtn);
    QTimer::singleShot(1000,[=]{QTest::mouseClick(fsBtn,Qt::LeftButton);});
    //QTimer::singleShot(1000,[=]{QTest::mouseClick((QWidget*)fsBtn,Qt::LeftButton);});
}

TEST(ToolBox, quitFullScreen)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();

    ToolButton *fsBtn = toolboxProxy->fsBtn();

    QTimer::singleShot(1000,[=]{QTest::mouseClick(fsBtn,Qt::LeftButton);});
}

TEST(ToolBox, mute)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();

    VolumeButton *volBtn = toolboxProxy->volBtn();

    QTimer::singleShot(1000,[=]{QTest::mouseClick(volBtn,Qt::LeftButton);});

    QTimer::singleShot(1000,[=]{QTest::mouseClick(volBtn,Qt::LeftButton);});
}

TEST(ToolBox, quitPlayList)
{
    MainWindow* w = dApp->getMainWindow();

    ToolboxProxy* toolboxProxy = w->toolbox();

    ToolButton *listBtn = toolboxProxy->listBtn();

    QTimer::singleShot(2000,[=]{QTest::mouseClick((QWidget*)listBtn,Qt::LeftButton);});
}



