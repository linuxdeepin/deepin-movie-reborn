// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ddlog.h"
#include "options.h"
#include <QDebug>

namespace dmr {

static CommandLineManager* _instance = nullptr;

CommandLineManager& CommandLineManager::get()
{
    qCDebug(appMovie) << "Entering CommandLineManager::get().";
    if (!_instance) {
        qCDebug(appMovie) << "_instance is null, creating new CommandLineManager instance.";
        _instance = new CommandLineManager();
    }

    qCDebug(appMovie) << "Exiting CommandLineManager::get(). Returning _instance.";
    return *_instance;
}

CommandLineManager::CommandLineManager()
{
    qCDebug(appMovie) << "Entering CommandLineManager constructor.";
    addHelpOption();
    addVersionOption();

    addPositionalArgument("path", ("Movie file path or directory"));
    addOptions({
        {{"V", "verbose"}, ("show detail log message")},
        {"VV", ("dump all debug message")},
        {{"c", "gpu"}, ("use gpu interface [on/off/auto]"), "bool", "auto"},
        {{"o", "override-config"}, ("override config for libmpv"), "file", ""},
        {"dvd-device", ("specify dvd playing device or file"), "device", "/dev/sr0"},
    });
    qCDebug(appMovie) << "Exiting CommandLineManager constructor. Options added.";
}

bool CommandLineManager::verbose() const
{
    qCDebug(appMovie) << "Entering CommandLineManager::verbose().";
    bool result = this->isSet("verbose");
    qCDebug(appMovie) << "Exiting CommandLineManager::verbose(). Returning:" << result;
    return result;
}

bool CommandLineManager::debug() const
{
    qCDebug(appMovie) << "Entering CommandLineManager::debug().";
    bool result = this->isSet("VV");
    qCDebug(appMovie) << "Exiting CommandLineManager::debug(). Returning:" << result;
    return result;
}

QString CommandLineManager::openglMode() const
{
    qCDebug(appMovie) << "Entering CommandLineManager::openglMode().";
    QString result = this->value("c");
//    return "on";
    qCDebug(appMovie) << "Exiting CommandLineManager::openglMode(). Returning:" << result;
    return result;
}

QString CommandLineManager::overrideConfig() const
{
    qCDebug(appMovie) << "Entering CommandLineManager::overrideConfig().";
    QString result = this->value("o");
    qCDebug(appMovie) << "Exiting CommandLineManager::overrideConfig(). Returning:" << result;
    return result;
}

QString CommandLineManager::dvdDevice() const
{
    qCDebug(appMovie) << "Entering CommandLineManager::dvdDevice().";
    if (this->isSet("dvd-device")) {
        QString result = this->value("dvd-device").trimmed();
        qCDebug(appMovie) << "dvd-device option is set. Returning trimmed value:" << result;
        return result;
    }

    qCDebug(appMovie) << "dvd-device option is not set. Returning empty string.";
    return "";
}

}
