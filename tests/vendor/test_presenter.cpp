#include <gtest/gtest.h>
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QTimer>
#include "application.h"
#include <unistd.h>
#include "src/vendor/presenter.h"

using namespace dmr;

TEST(Presenter, slotpause)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(1000,[=]{presenter->slotpause();});
}

TEST(Presenter, slotplay)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(1000,[=]{presenter->slotpause();});
}

TEST(Presenter, slotplaynext)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(1000,[=]{presenter->slotplaynext();});
}

TEST(Presenter, slotplayprev)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(1000,[=]{presenter->slotplayprev();});
}

TEST(Presenter, slotvolumeRequested)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(500,[=]{presenter->slotvolumeRequested(6.00);});
}

TEST(Presenter, slotstateChanged)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(500,[=]{presenter->slotstateChanged();});
}

TEST(Presenter, slotloopStatusRequested)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(500,[=]{presenter->slotloopStatusRequested((Mpris::LoopStatus)0);});
    QTimer::singleShot(500,[=]{presenter->slotloopStatusRequested((Mpris::LoopStatus)1);});
    QTimer::singleShot(500,[=]{presenter->slotloopStatusRequested((Mpris::LoopStatus)2);});
    QTimer::singleShot(500,[=]{presenter->slotloopStatusRequested((Mpris::LoopStatus)-1);});
}

TEST(Presenter, slotplayModeChanged)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(500,[=]{presenter->slotplayModeChanged((PlaylistModel::PlayMode)0);});
    QTimer::singleShot(500,[=]{presenter->slotplayModeChanged((PlaylistModel::PlayMode)3);});
    QTimer::singleShot(500,[=]{presenter->slotplayModeChanged((PlaylistModel::PlayMode)4);});
    QTimer::singleShot(500,[=]{presenter->slotplayModeChanged((PlaylistModel::PlayMode)2);});
}

TEST(Presenter, slotvolumeChanged)
{
    Presenter *presenter = dApp->initPresenter();

    QTimer::singleShot(500,[=]{presenter->slotvolumeChanged();});
}
