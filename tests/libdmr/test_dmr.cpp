#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QTimer>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>
#include <QWidget>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "mainwindow.h"
#include "player_widget.h"
#include "player_engine.h"
#include "compositing_manager.h"
#include "movie_configuration.h"

TEST(libdmr, libdmrTest)
{
    using namespace dmr;
    PlayerWidget *player = new PlayerWidget();
    player->engine().changeVolume(120);
    player->play(QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4"));

    auto &mc = MovieConfiguration::get();
    mc.updateUrl(QUrl("movie1"), "sub-delay", -2.5);
    mc.updateUrl(QUrl("movie1"), "sub-delay", 1.5);
    mc.updateUrl(QUrl("movie2"), "sub-delay", 1.0);
    mc.updateUrl(QUrl("movie1"), "volume", 20);

    auto res = mc.queryByUrl(QUrl("movie1"));
    mc.removeUrl(QUrl("movie1"));
    mc.updateUrl(QUrl("movie1"), "volume", 30);
    mc.updateUrl(QUrl("movie2"), "volume", 40);
    res = mc.queryByUrl(QUrl("movie1"));
    mc.clear();

//    delete player;
}
TEST(libdmr, utils)
{
    using namespace dmr;

    MainWindow* w = dApp->getMainWindow();

    utils::CompareNames("/data/source/deepin-movie-reborn/movie/demo.mp4", "/data/source/deepin-movie-reborn/movie/demo.mp4");
    utils::UnInhibitPower(20);
    utils::MoveToCenter(w);
    utils::Time2str(90000);
}
