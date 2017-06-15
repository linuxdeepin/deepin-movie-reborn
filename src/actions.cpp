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
    _contextMenuActions.append(act); \
} while (0) 

#define DEF_ACTION_CHECKED(NAME, KD) do { \
    auto *act = menu->addAction(tr(NAME)); \
    act->setCheckable(true); \
    act->setProperty("kind", KD); \
    _contextMenuActions.append(act); \
} while (0) 

QMenu* ActionFactory::titlebarMenu()
{
    if (!_titlebarMenu) {
        auto *menu = new QMenu();

        DEF_ACTION("Open File", ActionKind::OpenFile);
        DEF_ACTION("Settings", ActionKind::Settings);
        DEF_ACTION_CHECKED("Light Theme", ActionKind::LightTheme);
        // these seems added by titlebar itself
        //menu->addSeparator();
        //DEF_ACTION("About", ActionKind::About);
        //DEF_ACTION("Help", ActionKind::Help);
        //DEF_ACTION("Exit", ActionKind::Exit);

        _titlebarMenu = menu;
    }
    return _titlebarMenu;
}

QMenu* ActionFactory::mainContextMenu()
{
    if (!_contextMenu) {
        auto *menu = new QMenu();

        DEF_ACTION("Open File", ActionKind::OpenFile);
        DEF_ACTION("Open Url", ActionKind::OpenUrl);
        menu->addSeparator();

        DEF_ACTION_CHECKED("Fullscreen", ActionKind::Fullscreen);
        DEF_ACTION_CHECKED("Compact Mode", ActionKind::ToggleMiniMode);
        DEF_ACTION_CHECKED("Above", ActionKind::WindowAbove);
        menu->addSeparator();

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Subtitle"));
            DEF_ACTION("Load Subtitle", ActionKind::LoadSubtitle);
            DEF_ACTION("Select Subtitle", ActionKind::SelectSubtitle);

            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Screenshot"));
            DEF_ACTION("Single Screenshot", ActionKind::Screenshot);
            DEF_ACTION("Burst Screenshot", ActionKind::BurstScreenshot);

            parent->addMenu(menu);
        }

        menu->addSeparator();

        DEF_ACTION_CHECKED("Playlist", ActionKind::TogglePlaylist);
        DEF_ACTION("Movie Info", ActionKind::MovieInfo);
        DEF_ACTION("Settings", ActionKind::Settings);

        _contextMenu = menu;
    }

    return _contextMenu;
}

QList<QAction*> ActionFactory::findActionsByKind(ActionKind target_kd)
{
    QList<QAction*> res;
    auto p = _contextMenuActions.begin();
    while (p != _contextMenuActions.end()) {
        auto prop = (*p)->property("kind");
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 2)
        auto kd = (ActionKind)(*p)->property("kind").value<int>();
#else
        auto kd = p->property("kind").value<ActionKind>();
#endif
        if (kd == target_kd) {
            res.append(*p);
        }
        ++p;
    }
    return res;
}

#undef DEF_ACTION
#undef DEF_ACTION_CHECKED
}
