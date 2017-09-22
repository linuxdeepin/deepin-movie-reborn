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
#if ENABLE_VPU_PLATFORM
        {"frames", ("play only count number of frames"), "count", "0"},
        {"gal", ("use gal or not"), "bool", "on"},
#endif
        {"vpudemo", ("play in vpu demo mode")},
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
}

QString CommandLineManager::overrideConfig() const
{
    return this->value("o");
}

int CommandLineManager::debugFrameCount() const
{
    return value("frames").toInt();
}

bool CommandLineManager::useGAL() const
{
    auto v = value("gal");
    return v == "on" || v == "1";
}

bool CommandLineManager::vpuDemoMode() const
{
    return this->isSet("vpudemo");
}

}
