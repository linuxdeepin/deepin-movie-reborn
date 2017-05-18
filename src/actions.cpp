#include "actions.h"

namespace dmr {

static ActionFactory* _factory = nullptr;

ActionFactory& ActionFactory::get()
{
    if (_factory == nullptr) {
        _factory = new ActionFactory();
    }

    return *_factory;
}

#define DEF_ACTION(NAME, KD) do { \
    auto *act = menu->addAction(tr(NAME)); \
    act->setProperty("kind", KD); \
} while (0) 

#define DEF_ACTION_CHECKED(NAME, KD) do { \
    auto *act = menu->addAction(tr(NAME)); \
    act->setCheckable(true); \
    act->setProperty("kind", KD); \
} while (0) 

QMenu* ActionFactory::titlebarMenu()
{
    auto *menu = new QMenu();

    DEF_ACTION("Open File", ActionKind::OpenFile);
    DEF_ACTION("Settings", ActionKind::Settings);
    DEF_ACTION_CHECKED("Light Theme", ActionKind::LightTheme);
    // these seems added by titlebar itself
    //menu->addSeparator();
    //DEF_ACTION("About", ActionKind::About);
    //DEF_ACTION("Help", ActionKind::Help);
    //DEF_ACTION("Exit", ActionKind::Exit);

    return menu;
}

QMenu* ActionFactory::mainContextMenu()
{
    auto *menu = new QMenu();

    menu->addAction(tr("Open File"));
    menu->addAction(tr("Settings"));
    auto *act = menu->addAction(tr("Light Theme"));
    act->setCheckable(true);
    menu->addSeparator();
    menu->addAction(tr("About"));
    menu->addAction(tr("Help"));
    menu->addAction(tr("Exit"));

    return menu;
}

#undef DEF_ACTION
#undef DEF_ACTION_CHECKED
}
