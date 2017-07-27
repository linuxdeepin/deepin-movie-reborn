#ifndef _DMR_SHORTCUT_MANAGER
#define _DMR_SHORTCUT_MANAGER 

#include <memory>
#include <vector>
#include <QtCore>
#include "actions.h"

namespace dmr {

using namespace std;

using BindingMap = QHash<QKeySequence, ActionKind>;
using ActionMap = QHash<QString, ActionKind>;

    // keys comes from profiles, user configurations etc
class ShortcutManager: public QObject {
    Q_OBJECT
    public:
        static ShortcutManager& get();
        virtual ~ShortcutManager();
        BindingMap& map() { return _map; }
        const BindingMap& map() const { return _map; }
        vector<QAction*> actionsForBindings();
        void buildBindingsFromSettings();

    public slots:
        void buildBindings();

    signals:
        void bindingsChanged();

    private:
        ShortcutManager();

        BindingMap _map;
        ActionMap _keyToAction;
};

}

#endif /* ifndef _DMR_SHORTCUT_MANAGER */

