/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
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
#include "config.h"
#include "options.h"

namespace dmr {

static CommandLineManager* _instance = nullptr;

CommandLineManager& CommandLineManager::get()
{
    if (!_instance) {
        _instance = new CommandLineManager();
    }

    return *_instance;
}

CommandLineManager::CommandLineManager()
{
    addHelpOption();
    addVersionOption();

    addPositionalArgument("path", ("Movie file path or directory"));
    addOptions({
        {{"V", "verbose"}, ("show detail log message")},
        {"VV", ("dump all debug message")},
        {{"c", "opengl-cb"}, ("use opengl-cb interface [on/off/auto]"), "bool", "auto"},
        {{"o", "override-config"}, ("override config for libmpv"), "file", ""},
        {"dvd-device", ("specify dvd playing device or file"), "device", "/dev/sr0"},
    });
}

bool CommandLineManager::verbose() const
{
    return this->isSet("verbose");
}

bool CommandLineManager::debug() const
{
    return this->isSet("VV");
}

QString CommandLineManager::openglMode() const
{
    return this->value("c");
//    return "on";
}

QString CommandLineManager::overrideConfig() const
{
    return this->value("o");
}

QString CommandLineManager::dvdDevice() const
{
    if (this->isSet("dvd-device")) {
        return this->value("dvd-device").trimmed();
    }

    return "";
}

}
