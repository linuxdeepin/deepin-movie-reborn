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

    SeekForward,
    SeekForwardLarge,
    SeekBackward,
    SeekBackwardLarge,
    TogglePause,
    Stop,
    GotoPlaylistNext,
    GotoPlaylistPrev,
};

class ActionFactory: public QObject {
    Q_OBJECT
public:

    static ActionFactory& get();

    QMenu* titlebarMenu();
    QMenu* mainContextMenu();

private:
    ActionFactory() {}
};

}

Q_DECLARE_METATYPE(dmr::ActionKind)

#endif /* ifndef _DMR_ACTIONS_H */
