#include "config.h"
#include "actions.h"
#include "player_engine.h"

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
    connect(act, &QObject::destroyed, [=](QObject* o) { \
        _contextMenuActions.removeOne((QAction*)o); \
    }); \
} while (0) 

#define DEF_ACTION_CHECKED(NAME, KD) do { \
    auto *act = menu->addAction((NAME)); \
    act->setCheckable(true); \
    act->setProperty("kind", KD); \
    _contextMenuActions.append(act); \
    connect(act, &QObject::destroyed, [=](QObject* o) { \
        _contextMenuActions.removeOne((QAction*)o); \
    }); \
} while (0) 

#define DEF_ACTION_CHECKED_GROUP(NAME, KD, GROUP) do { \
    auto *act = menu->addAction((NAME)); \
    act->setCheckable(true); \
    act->setProperty("kind", KD); \
    act->setActionGroup(GROUP); \
    _contextMenuActions.append(act); \
    connect(act, &QObject::destroyed, [=](QObject* o) { \
        _contextMenuActions.removeOne((QAction*)o); \
    }); \
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
            auto *menu = new QMenu(tr("Play Mode"));
            auto group = new QActionGroup(menu);

            DEF_ACTION_CHECKED_GROUP(tr("Order Play"), ActionKind::OrderPlay, group);
            DEF_ACTION_CHECKED_GROUP(tr("Shuffle Play"), ActionKind::ShufflePlay, group);
            DEF_ACTION_CHECKED_GROUP(tr("Single Play"), ActionKind::SinglePlay, group);
            DEF_ACTION_CHECKED_GROUP(tr("Single Loop"), ActionKind::SingleLoop, group);
            DEF_ACTION_CHECKED_GROUP(tr("List Loop"), ActionKind::ListLoop, group);

            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Frame"));
            auto group = new QActionGroup(menu);

            DEF_ACTION_CHECKED_GROUP(tr("Default"), ActionKind::DefaultFrame, group);
            DEF_ACTION_CHECKED_GROUP(("4:3"), ActionKind::Ratio4x3Frame, group);
            DEF_ACTION_CHECKED_GROUP(("16:9"), ActionKind::Ratio16x9Frame, group);
            DEF_ACTION_CHECKED_GROUP(("16:10"), ActionKind::Ratio16x10Frame, group);
            DEF_ACTION_CHECKED_GROUP(("1.85:1"), ActionKind::Ratio185x1Frame, group);
            DEF_ACTION_CHECKED_GROUP(("2.35:1"), ActionKind::Ratio235x1Frame, group);
            menu->addSeparator();

            DEF_ACTION(tr("Clockwise"), ActionKind::ClockwiseFrame);
            DEF_ACTION(tr("Counterclockwise"), ActionKind::CounterclockwiseFrame);

            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Sound"));
            {
                auto *parent = menu;
                auto *menu = new QMenu(tr("Sound"));
                auto group = new QActionGroup(menu);

                DEF_ACTION_CHECKED_GROUP(tr("Stereo"), ActionKind::Stereo, group);
                DEF_ACTION_CHECKED_GROUP(tr("Left channel"), ActionKind::LeftChannel, group);
                DEF_ACTION_CHECKED_GROUP(tr("Right channel"), ActionKind::RightChannel, group);
                parent->addMenu(menu);
            }

            {
                auto *parent = menu;
                auto *menu = new QMenu(tr("Track"));
                _tracksMenu = menu;
                //DEF_ACTION(tr("Load Track"), ActionKind::LoadTrack);
                //DEF_ACTION(tr("Select Track"), ActionKind::SelectTrack);
                parent->addMenu(menu);
            }
            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Subtitle"));
            DEF_ACTION(tr("Load"), ActionKind::LoadSubtitle);
            //DEF_ACTION(tr("Select"), ActionKind::SelectSubtitle);
            {
                auto *parent = menu;
                auto *menu = new QMenu(tr("Select"));
                _subtitleMenu = menu;
                parent->addMenu(menu);
            }
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

void ActionFactory::updateMainActionsForMovie(const PlayingMovieInfo& pmf)
{
    qDebug() << __func__;
    if (_subtitleMenu) {
        auto menu = _subtitleMenu;
        menu->clear();

        auto group = new QActionGroup(menu); // mem leak ?
        for (int i = 0; i < pmf.subs.size(); i++) {
            DEF_ACTION_CHECKED(pmf.subs[i]["title"].toString(), ActionKind::SelectSubtitle);
            auto act = menu->actions().last();
            act->setActionGroup(group);
            act->setProperty("args", QList<QVariant>() << i);
        }
    }

    if (_subtitleMenu) {
        auto menu = _tracksMenu;
        menu->clear();

        DEF_ACTION(tr("Load Track"), ActionKind::LoadTrack);

        auto group = new QActionGroup(menu); // mem leak ?
        for (int i = 0; i < pmf.audios.size(); i++) {
            DEF_ACTION_CHECKED(pmf.audios[i]["title"].toString(), ActionKind::SelectTrack);
            auto act = menu->actions().last();
            act->setActionGroup(group);
            act->setProperty("args", QList<QVariant>() << i);
        }
    }
}

#undef DEF_ACTION
#undef DEF_ACTION_CHECKED
}
