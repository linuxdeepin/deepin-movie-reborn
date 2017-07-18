#include "config.h"
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
    auto *act = menu->addAction((NAME)); \
    act->setProperty("kind", KD); \
    _contextMenuActions.append(act); \
} while (0) 

#define DEF_ACTION_CHECKED(NAME, KD) do { \
    auto *act = menu->addAction((NAME)); \
    act->setCheckable(true); \
    act->setProperty("kind", KD); \
    _contextMenuActions.append(act); \
} while (0) 

QMenu* ActionFactory::titlebarMenu()
{
    if (!_titlebarMenu) {
        auto *menu = new QMenu();

        DEF_ACTION(tr("Open File"), ActionKind::OpenFile);
        DEF_ACTION(tr("Settings"), ActionKind::Settings);
        DEF_ACTION_CHECKED(tr("Light Theme"), ActionKind::LightTheme);
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
#ifdef ENABLE_VPU_PLATFORM
        DEF_ACTION(tr("Open File"), ActionKind::OpenFile);
        menu->addSeparator();

        DEF_ACTION_CHECKED(tr("Fullscreen"), ActionKind::Fullscreen);
        DEF_ACTION_CHECKED(tr("Always on Top"), ActionKind::WindowAbove);
        DEF_ACTION(tr("Film Info"), ActionKind::MovieInfo);
        DEF_ACTION(tr("Settings"), ActionKind::Settings);
#else


        DEF_ACTION(tr("Open File"), ActionKind::OpenFile);
        DEF_ACTION(tr("Open URL"), ActionKind::OpenUrl);
        menu->addSeparator();

        DEF_ACTION_CHECKED(tr("Fullscreen"), ActionKind::Fullscreen);
        DEF_ACTION_CHECKED(tr("Mini Mode"), ActionKind::ToggleMiniMode);
        DEF_ACTION_CHECKED(tr("Always on Top"), ActionKind::WindowAbove);
        menu->addSeparator();

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Subtitle"));
            DEF_ACTION(tr("Load"), ActionKind::LoadSubtitle);
            DEF_ACTION(tr("Select"), ActionKind::SelectSubtitle);
            DEF_ACTION_CHECKED(tr("Hide"), ActionKind::HideSubtitle);

            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Screenshot"));
            DEF_ACTION(tr("Film Screenshot"), ActionKind::Screenshot);
            DEF_ACTION(tr("Burst Shooting"), ActionKind::BurstScreenshot);

            parent->addMenu(menu);
        }

        menu->addSeparator();

        DEF_ACTION_CHECKED(tr("Playlist"), ActionKind::TogglePlaylist);
        DEF_ACTION(tr("Film Info"), ActionKind::MovieInfo);
        DEF_ACTION(tr("Settings"), ActionKind::Settings);

#endif
        _contextMenu = menu;
    }

    return _contextMenu;
}

QMenu* ActionFactory::playlistContextMenu()
{
    if (!_playlistMenu) {
        auto *menu = new QMenu();

        DEF_ACTION(tr("Clear Playlist"), ActionKind::EmptyPlaylist);
        DEF_ACTION(tr("Open File In File Manager"), ActionKind::PlaylistOpenItemInFM);
        DEF_ACTION(tr("Film Info"), ActionKind::MovieInfo);

        _playlistMenu = menu;
    }

    return _playlistMenu;

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
