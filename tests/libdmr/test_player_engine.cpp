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
#include "player_widget.h"
#include "player_engine.h"
#include "compositing_manager.h"
#include "movie_configuration.h"

TEST(PlayerEngine, playerEngine)
{
    MainWindow *w = dApp->getMainWindow();
    PlayerEngine *engine =  w->engine();

    QString subCodepage = engine->subCodepage();
    engine->addSubSearchPath(QString("/test/"));
    engine->selectTrack(1);
    engine->volumeUp();
    engine->volumeDown();
    engine->toggleMute();
}
