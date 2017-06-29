#ifndef _DMR_ACTIONS_H
#define _DMR_ACTIONS_H 

#include <QtWidgets>

namespace dmr {
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
    SelectSubtitle,
    HideSubtitle,
    Screenshot,
    BurstScreenshot,

    SeekForward,
    SeekForwardLarge,
    SeekBackward,
    SeekBackwardLarge,
    TogglePause,
    Stop,

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

private:
    ActionFactory() {}
    QMenu *_titlebarMenu {nullptr};
    QMenu *_contextMenu {nullptr};
    QMenu *_playlistMenu {nullptr};
    QList<QAction*> _contextMenuActions;
};

template<class UnaryFunction>
void ActionFactory::forEachInMainMenu(UnaryFunction f)
{
    auto p = _contextMenuActions.begin();
    while (p != _contextMenuActions.end()) {
        f(*p);
        ++p;
    }
}


}

Q_DECLARE_METATYPE(dmr::ActionKind)

#endif /* ifndef _DMR_ACTIONS_H */
