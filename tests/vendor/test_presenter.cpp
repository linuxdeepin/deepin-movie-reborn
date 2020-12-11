#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include "application.h"
#include <unistd.h>
#include "src/vendor/presenter.h"

using namespace dmr;

//TEST(Presenter, slotpause)
//{
//    Presenter *presenter = dApp->initPresenter();

//    QTest::qWait(500);
//    presenter->slotopenUrlRequested(QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/bensound-sunny.mp3"));
//    QTest::qWait(500);
//    presenter->slotplay();
//    QTest::qWait(1000);
//    presenter->slotpause();
//    QTest::qWait(1000);
//    presenter->slotplaynext();
//    QTest::qWait(1000);
//    presenter->slotplayprev();
//    QTest::qWait(500);
//    presenter->slotvolumeRequested(1.5);
//    QTest::qWait(500);
//    presenter->slotstateChanged();
//    QTest::qWait(500);
//    presenter->slotvolumeChanged();

//}

TEST(Presenter, slotloopStatusRequested)
{
    Presenter *presenter = dApp->initPresenter();

    QTest::qWait(500);
    presenter->slotloopStatusRequested(Mpris::LoopStatus::None);
    QTest::qWait(500);
    presenter->slotloopStatusRequested(Mpris::LoopStatus::Track);
    QTest::qWait(500);
    presenter->slotloopStatusRequested(Mpris::LoopStatus::Playlist);
    QTest::qWait(500);
    presenter->slotloopStatusRequested(Mpris::LoopStatus::InvalidLoopStatus);
}

TEST(Presenter, slotplayModeChanged)
{
    Presenter *presenter = dApp->initPresenter();

    QTest::qWait(500);
    presenter->slotplayModeChanged(PlaylistModel::PlayMode::OrderPlay);
    QTest::qWait(500);
    presenter->slotplayModeChanged(PlaylistModel::PlayMode::SingleLoop);
    QTest::qWait(500);
    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ListLoop);
    QTest::qWait(500);
    presenter->slotplayModeChanged(PlaylistModel::PlayMode::ShufflePlay);
}
