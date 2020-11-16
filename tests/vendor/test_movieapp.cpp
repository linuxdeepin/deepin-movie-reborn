#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include "application.h"
#include <unistd.h>
#include "src/vendor/movieapp.h"

using namespace dmr;

TEST(MovieApp, show)
{
    MainWindow* w = dApp->getMainWindow();
//    w->show();

//    MovieApp *movieapp = dApp->initMovieApp(w);

//    QTimer::singleShot(1000,[=]{movieapp->show();});
    w->testMprisapp();
}

//TEST(MovieApp, quit)
//{
//    MainWindow* w = dApp->getMainWindow();
//    PlayerEngine* engine =  w->engine();

//    MovieApp *movieapp = dApp->initMovieApp(w);

//    QTimer::singleShot(500,[=]{movieapp->quit();});
//    QTest::qWait(500);
//    w->close();
//}
