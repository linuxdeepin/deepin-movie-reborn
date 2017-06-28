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

    addPositionalArgument("path", QCoreApplication::tr("Movie file path or directory"));
    addOptions({
        {"l", QCoreApplication::tr("show detail log message")},
        {{"c", "opengl-cb"}, QCoreApplication::tr("use opengl-cb interface [on/off/auto]"), "bool", "auto"},
        {{"o", "override-config"}, QCoreApplication::tr("override config for libmpv"), "file", ""},
    });
}

bool CommandLineManager::verbose() const
{
    return this->isSet("l");
}

QString CommandLineManager::openglMode() const
{
    return this->value("c");
}

QString CommandLineManager::overrideConfig() const
{
    return this->value("o");
}
}
