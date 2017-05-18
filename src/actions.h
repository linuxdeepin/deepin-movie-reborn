#ifndef _DMR_ACTIONS_H
#define _DMR_ACTIONS_H 

#include <QtWidgets>

namespace dmr {

class ActionFactory: public QObject {
    Q_OBJECT
public:
    enum ActionKind {
        Invalid = 0,
        OpenFile = 1,
        Settings,
        LightTheme,
        About,
        Help,
        Exit,
    };

    Q_ENUM(ActionKind)

    static ActionFactory& get();

    QMenu* titlebarMenu();
    QMenu* mainContextMenu();

private:
    ActionFactory() {}
};

}

#endif /* ifndef _DMR_ACTIONS_H */
