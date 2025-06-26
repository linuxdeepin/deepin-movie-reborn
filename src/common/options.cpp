// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "options.h"
#include <QDebug>

namespace dmr {

static CommandLineManager* _instance = nullptr;

CommandLineManager& CommandLineManager::get()
{
    qDebug() << "Entering CommandLineManager::get().";
    if (!_instance) {
        qDebug() << "_instance is null, creating new CommandLineManager instance.";
        _instance = new CommandLineManager();
    }

    qDebug() << "Exiting CommandLineManager::get(). Returning _instance.";
    return *_instance;
}

CommandLineManager::CommandLineManager()
{
    qDebug() << "Entering CommandLineManager constructor.";
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
    qDebug() << "Exiting CommandLineManager constructor. Options added.";
}

bool CommandLineManager::verbose() const
{
    qDebug() << "Entering CommandLineManager::verbose().";
    bool result = this->isSet("verbose");
    qDebug() << "Exiting CommandLineManager::verbose(). Returning:" << result;
    return result;
}

bool CommandLineManager::debug() const
{
    qDebug() << "Entering CommandLineManager::debug().";
    bool result = this->isSet("VV");
    qDebug() << "Exiting CommandLineManager::debug(). Returning:" << result;
    return result;
}

QString CommandLineManager::openglMode() const
{
    qDebug() << "Entering CommandLineManager::openglMode().";
    QString result = this->value("c");
//    return "on";
    qDebug() << "Exiting CommandLineManager::openglMode(). Returning:" << result;
    return result;
}

QString CommandLineManager::overrideConfig() const
{
    qDebug() << "Entering CommandLineManager::overrideConfig().";
    QString result = this->value("o");
    qDebug() << "Exiting CommandLineManager::overrideConfig(). Returning:" << result;
    return result;
}

QString CommandLineManager::dvdDevice() const
{
    qDebug() << "Entering CommandLineManager::dvdDevice().";
    if (this->isSet("dvd-device")) {
        QString result = this->value("dvd-device").trimmed();
        qDebug() << "dvd-device option is set. Returning trimmed value:" << result;
        return result;
    }

    qDebug() << "dvd-device option is not set. Returning empty string.";
    return "";
}

}
