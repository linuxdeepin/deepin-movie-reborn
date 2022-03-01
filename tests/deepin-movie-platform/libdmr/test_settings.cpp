/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     fengli <fengli@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#include <QtTest>
#include <QTest>
#include <QTestEventList>
#include <QDebug>
#include <QAbstractButton>
#include <DSettingsDialog>
#include <dwidgetstype.h>

#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "dmr_settings.h"
#include "utils.h"
#include "movie_configuration.h"
#include "dbus_adpator.h"

using namespace dmr;
using namespace utils;

TEST(Settings, Settings)
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

    QString path("/usr/share/dde-introduction");
    Settings::get().setGeneralOption("last_open_path", path);
    QVariant v = Settings::get().generalOption("last_open_path");

    DSettingsOption settingsOption;
    emit settingsOption.valueChanged(v);

    DLineEdit edit;
    emit edit.editingFinished();
}

TEST(Settings, shortcut)
{
    Settings::get().settings()->setOption("shortcuts.play.enable", false);
    Settings::get().settings()->setOption("shortcuts.play.enable", true);
    Settings::get().settings()->setOption("shortcuts.play.playlist", "Shift+Return");
    Settings::get().settings()->setOption("shortcuts.play.movie_info", "Shift+Num+Enter");
    Settings::get().settings()->setOption("subtitle.font.size", 20);
    Settings::get().settings()->setOption("base.play.hwaccel", 1);
    Settings::get().settings()->setOption("base.decode.select", DecodeMode::AUTO);
    Settings::get().settings()->setOption("base.decode.select", DecodeMode::HARDWARE);
    Settings::get().settings()->setOption("base.decode.select", DecodeMode::SOFTWARE);
    emit Settings::get().hwaccelModeChanged("base.play.hwaccel", 1);

//    Settings::get().settings()->setOption("play.global_volume", 120);
}

TEST(Settings, mwDeconstruction)
{
    Platform_MainWindow *w = dApp->getMainWindow();
    w->close();
    delete w;
    w = nullptr;
}


