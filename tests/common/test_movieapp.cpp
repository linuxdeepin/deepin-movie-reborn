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
    QTest::qWait(300);
    w->testMprisapp();
}

