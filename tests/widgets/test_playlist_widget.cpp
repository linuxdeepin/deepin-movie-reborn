#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>

#include "src/common/mainwindow.h"
#include "application.h"
#include "playlist_model.h"
#include "playlist_widget.h"

//////////dmr settings//////////
#include "dmr_settings.h"
TEST(Settings,settings)
{
    using namespace dmr;
    Settings::get().isSet(Settings::Flag::ClearWhenQuit);
    Settings::get().isSet(Settings::Flag::AutoSearchSimilar);
    Settings::get().isSet(Settings::Flag::PreviewOnMouseover);
    Settings::get().isSet(Settings::Flag::MultipleInstance);
    Settings::get().isSet(Settings::Flag::PauseOnMinimize);

    Settings::get().commonPlayableProtocols();
    Settings::get().commonPlayableProtocols();
    Settings::get().iscommonPlayableProtocol("dvb");
    Settings::get().screenshotLocation();
    Settings::get().screenshotNameTemplate();
    Settings::get().screenshotNameSeqTemplate();
}















//TEST(ToolBox, reloadFilePlayList)
//{
//    MainWindow* w = dApp->getMainWindow();

//    PlayerEngine* egine =  w->engine();

//    QList<QUrl> listPlayFiles;

//    listPlayFiles<<QUrl::fromLocalFile("/home/uosf/Videos/3.mp4")\
//                <<QUrl::fromLocalFile("/home/uosf/Videos/4.mp4");

//    w->show();

//    const auto &valids = egine->addPlayFiles(listPlayFiles);

//    egine->playByName(valids[0]);

//    w->requestAction(ActionFactory::ActionKind::TogglePlaylist);
//}

//TEST(PlayListWidget, updateSelectItem)
//{
//    MainWindow* w = dApp->getMainWindow();

//    PlaylistWidget *playlist = w->playlist();

//    QTimer::singleShot(1000,[=]{playlist->updateSelectItem(3);});
//}

//TEST(PlayListWidget, openItemInFM)
//{
//    MainWindow* w = dApp->getMainWindow();

//    PlaylistWidget *playlist = w->playlist();

//    QTimer::singleShot(2000,[=]{playlist->openItemInFM();});
//    EXPECT_TRUE(false);
//}

//TEST(PlayListWidget, removeClickedItem)
//{
//    MainWindow* w = dApp->getMainWindow();

//    PlaylistWidget *playlist = w->playlist();

//    QTimer::singleShot(1000,[=]{playlist->removeClickedItem(true);});
//}

//TEST(PlayListWidget, clearPlaylist)
//{
//    MainWindow* w = dApp->getMainWindow();

//    PlaylistWidget *playlist = w->playlist();

//    QTimer::singleShot(1000,[=]{playlist->clear();});
//}
