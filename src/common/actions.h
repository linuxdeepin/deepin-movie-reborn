#ifndef _DMR_ACTIONS_H
#define _DMR_ACTIONS_H 

#include <QtWidgets>

namespace dmr {
class PlayingMovieInfo;

enum ActionKind {
    Invalid = 0,
    OpenFile = 1,
    Settings,
    LightTheme,
    About,
    Help,
    Exit,

    TogglePlaylist,
    EmptyPlaylist,
    PlaylistRemoveItem,
    PlaylistOpenItemInFM,
    MovieInfo,
    OpenUrl,
    Fullscreen,
    ToggleMiniMode,
    WindowAbove,
    LoadSubtitle,
    SelectSubtitle, // stub for subs loaded from movie
    HideSubtitle,
    Screenshot,
    BurstScreenshot,

    SeekForward,
    SeekForwardLarge,
    SeekBackward,
    SeekBackwardLarge,
    TogglePause,
    Stop,

    //play mode
    OrderPlay,
    ShufflePlay,
    SinglePlay,
    SingleLoop,
    ListLoop,

    //frame
    DefaultFrame,
    Ratio4x3Frame,
    Ratio16x9Frame,
    Ratio16x10Frame,
    Ratio185x1Frame,
    Ratio235x1Frame,
    ClockwiseFrame,
    CounterclockwiseFrame,

    //sound
    Stereo,
    LeftChannel,
    RightChannel,
    LoadTrack,
    SelectTrack, // stub for tracks loaded from movie

    GotoPlaylistNext,
    GotoPlaylistPrev,
    VolumeUp,
    VolumeDown,
    ToggleMute,
};

class ActionFactory: public QObject {
    Q_OBJECT
public:

    static ActionFactory& get();

    QMenu* titlebarMenu();
    QMenu* mainContextMenu();
    template<class UnaryFunction> 
    void forEachInMainMenu(UnaryFunction f);
    QMenu* playlistContextMenu();
    QList<QAction*> findActionsByKind(ActionKind kd);
    void updateMainActionsForMovie(const PlayingMovieInfo& pmf);

    static bool actionHasArgs(QAction* act) { return act->property("args").isValid(); }
    static QList<QVariant> actionArgs(QAction* act) { return act->property("args").toList(); }
    static ActionKind actionKind(QAction* act) {
#if QT_VERSION < QT_VERSION_CHECK(5, 6, 2)
        auto kd = (ActionKind)act->property("kind").value<int>();
#else
        auto kd = act->property("kind").value<ActionKind>();
#endif
        return kd;
    }

private:
    ActionFactory() {}
    QMenu *_titlebarMenu {nullptr};
    QMenu *_contextMenu {nullptr};
    QMenu *_subtitleMenu {nullptr};
    QMenu *_tracksMenu {nullptr};
    QMenu *_playlistMenu {nullptr};
    QList<QAction*> _contextMenuActions;
};

template<class UnaryFunction>
void ActionFactory::forEachInMainMenu(UnaryFunction f)
{
    auto p = _contextMenuActions.begin();
    while (p != _contextMenuActions.end()) {
        if (strcmp((*p)->metaObject()->className(), "QAction") == 0)
            f(*p);
        ++p;
    }
}


}

Q_DECLARE_METATYPE(dmr::ActionKind)

#endif /* ifndef _DMR_ACTIONS_H */
