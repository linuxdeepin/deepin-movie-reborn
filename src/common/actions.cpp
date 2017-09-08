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

        DEF_ACTION(tr("Open File"), ActionKind::OpenFileList);
        DEF_ACTION(tr("Open Directory"), ActionKind::OpenDirectory);
        DEF_ACTION(tr("Settings"), ActionKind::Settings);
        DEF_ACTION_CHECKED(tr("Light Theme"), ActionKind::LightTheme);
        menu->addSeparator();
        // these seems added by titlebar itself
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
        DEF_ACTION(tr("Open File"), ActionKind::OpenFileList);
        DEF_ACTION(tr("Open Directory"), ActionKind::OpenDirectory);
        menu->addSeparator();

        DEF_ACTION_CHECKED(tr("Fullscreen"), ActionKind::ToggleFullscreen);
        DEF_ACTION_CHECKED(tr("Always on Top"), ActionKind::WindowAbove);
        DEF_ACTION(tr("Film info"), ActionKind::MovieInfo);
        DEF_ACTION(tr("Settings"), ActionKind::Settings);
#else


        DEF_ACTION(tr("Open File"), ActionKind::OpenFileList);
        DEF_ACTION(tr("Open Folder"), ActionKind::OpenDirectory);
        DEF_ACTION(tr("Open URL"), ActionKind::OpenUrl);
        DEF_ACTION(tr("Open CD/DVD"), ActionKind::OpenCdrom);
        menu->addSeparator();

        DEF_ACTION_CHECKED(tr("Fullscreen"), ActionKind::ToggleFullscreen);
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
                //DEF_ACTION(tr("Select Track"), ActionKind::SelectTrack);
                parent->addMenu(menu);
            }
            parent->addMenu(menu);
        }

        { //sub menu
            auto *parent = menu;
            auto *menu = new QMenu(tr("Subtitle"));
            DEF_ACTION(tr("Load"), ActionKind::LoadSubtitle);
            DEF_ACTION(tr("Online Search"), ActionKind::MatchOnlineSubtitle);
            //DEF_ACTION(tr("Select"), ActionKind::SelectSubtitle);
            {
                auto *parent = menu;
                auto *menu = new QMenu(tr("Select"));
                _subtitleMenu = menu;
                parent->addMenu(menu);
            }
            DEF_ACTION_CHECKED(tr("Hide"), ActionKind::HideSubtitle);

            {
                auto *parent = menu;
                auto *menu = new QMenu(tr("Encodings"));
                auto group = new QActionGroup(menu);

                //title <-> codepage
                static QVector<QPair<QString, QString>> list = {
                    {"Auto", "auto"},
                    {"Universal (UTF-8)", "UTF-8"},
                    {"Universal (UTF-16)", "UTF-16"},
                    {"Universal (UTF-16BE)", "UTF-16BE"},
                    {"Universal (UTF-16LE)", "UTF-16LE"},
                    {"Arabic (ISO-8859-6)", "ISO-8859-6"},
                    {"Arabic (WINDOWS-1256)", "WINDOWS-1256"},
                    {"Baltic (LATIN7)", "LATIN7"},
                    {"Baltic (WINDOWS-1257)", "WINDOWS-1257"},
                    {"Celtic (LATIN8)", "LATIN8"},
                    {"Central European (WINDOWS-1250)", "WINDOWS-1250"},
                    {"Cyrillic (ISO-8859-5)", "ISO-8859-5"},
                    {"Cyrillic (WINDOWS-1251)", "WINDOWS-1251"},
                    {"Eastern European (ISO-8859-2)", "ISO-8859-2"},
                    {"Western Languages (WINDOWS-1252)", "WINDOWS-1252"},
                    {"Greek (ISO-8859-7)", "ISO-8859-7"},
                    {"Greek (WINDOWS-1253)", "WINDOWS-1253"},
                    {"Hebrew (ISO-8859-8)", "ISO-8859-8"},
                    {"Hebrew (WINDOWS-1255)", "WINDOWS-1255"},
                    {"Japanese (SHIFT-JIS)", "SHIFT-JIS"},
                    {"Japanese (ISO-2022-JP-2)", "ISO-2022-JP-2"},
                    {"Korean (EUC-KR)", "EUC-KR"},
                    {"Korean (CP949)", "CP949"},
                    {"Korean (ISO-2022-KR)", "ISO-2022-KR"},
                    {"Nordic (LATIN6)", "LATIN6"},
                    {"North European (LATIN4)", "LATIN4"},
                    {"Russian (KOI8-R)", "KOI8-R"},
                    {"Simplified Chinese (GBK)", "GBK"},
                    {"Simplified Chinese (GB18030)", "GB18030"},
                    {"Simplified Chinese (ISO-2022-CN-EXT)", "ISO-2022-CN-EXT"},
                    {"South European (LATIN3)", "LATIN3"},
                    {"South-Eastern European (LATIN10)", "LATIN10"},
                    {"Thai (TIS-620)", "TIS-620"},
                    {"Thai (WINDOWS-874)", "WINDOWS-874"},
                    {"Traditional Chinese (EUC-TW)", "EUC-TW"},
                    {"Traditional Chinese (BIG5)", "BIG5"},
                    {"Traditional Chinese (BIG5-HKSCS)", "BIG5-HKSCS"},
                    {"Turkish (LATIN5)", "LATIN5"},
                    {"Turkish (WINDOWS-1254)", "WINDOWS-1254"},
                    {"Ukrainian (KOI8-U)", "KOI8-U"},
                    {"Vietnamese (WINDOWS-1258)", "WINDOWS-1258"},
                    {"Vietnamese (VISCII)", "VISCII"},
                    {"Western European (LATIN1)", "LATIN1"},
                    {"Western European (LATIN-9)", "LATIN-9"}
                };

                auto p = list.begin();
                while (p != list.end()) {
                    DEF_ACTION_CHECKED_GROUP(p->first, ActionKind::ChangeSubCodepage, group);
                    auto act = menu->actions().last();
                    act->setProperty("args", QList<QVariant>() << p->second);
                    if (p->second == "auto") menu->addSeparator();
                    p++;
                }

                parent->addMenu(menu);
            }

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

        DEF_ACTION(tr("Clear playlist"), ActionKind::EmptyPlaylist);
        DEF_ACTION(tr("Display in file manager"), ActionKind::PlaylistOpenItemInFM);
        DEF_ACTION(tr("Film info"), ActionKind::PlaylistItemInfo);

        _playlistMenu = menu;
    }

    return _playlistMenu;

}

QList<QAction*> ActionFactory::findActionsByKind(ActionKind target_kd)
{
    QList<QAction*> res;
    auto p = _contextMenuActions.begin();
    while (p != _contextMenuActions.end()) {
#if QT_VERSION < QT_VERSION_CHECK(5, 7, 0)
        auto kd = (ActionKind)(*p)->property("kind").value<int>();
#else
        auto kd = (*p)->property("kind").value<ActionKind>();
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
            DEF_ACTION_CHECKED_GROUP(pmf.subs[i]["title"].toString(), ActionKind::SelectSubtitle, group);
            auto act = menu->actions().last();
            act->setProperty("args", QList<QVariant>() << i);
        }

        _subtitleMenu->setEnabled(pmf.subs.size() > 0);
    }

    if (_subtitleMenu) {
        auto menu = _tracksMenu;
        menu->clear();

        auto group = new QActionGroup(menu); // mem leak ?
        for (int i = 0; i < pmf.audios.size(); i++) {
            DEF_ACTION_CHECKED_GROUP(pmf.audios[i]["title"].toString(), ActionKind::SelectTrack, group);
            auto act = menu->actions().last();
            act->setProperty("args", QList<QVariant>() << i);
        }

        _tracksMenu->setEnabled(pmf.audios.size() > 0);
    }
}

#undef DEF_ACTION
#undef DEF_ACTION_CHECKED
}
