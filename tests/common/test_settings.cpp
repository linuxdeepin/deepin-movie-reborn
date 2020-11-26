#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>

#include <unistd.h>
#include <gtest/gtest.h>

#include "dmr_settings.h"
using namespace dmr;

TEST(Settings, settings)
{
    Settings::get().isSet(Settings::Flag::ClearWhenQuit);
    Settings::get().isSet(Settings::Flag::ShowThumbnailMode);
    Settings::get().isSet(Settings::Flag::AutoSearchSimilar);
    Settings::get().isSet(Settings::Flag::PreviewOnMouseover);
    Settings::get().isSet(Settings::Flag::MultipleInstance);
    Settings::get().isSet(Settings::Flag::PauseOnMinimize);
    Settings::get().settings()->sync();

    Settings::get().commonPlayableProtocols();
    Settings::get().commonPlayableProtocols();
    Settings::get().iscommonPlayableProtocol("dvb");
    Settings::get().screenshotLocation();
    Settings::get().screenshotNameTemplate();
    Settings::get().screenshotNameSeqTemplate();
}

TEST(Settings, shortcut)
{
    Settings::get().settings()->setOption("shortcuts.play.enable", false);
    Settings::get().settings()->setOption("shortcuts.play.enable", true);
    Settings::get().settings()->setOption("shortcuts.play.playlist", "Shift+Return");
    Settings::get().settings()->setOption("shortcuts.play.movie_info", "Shift+Num+Enter");
}
