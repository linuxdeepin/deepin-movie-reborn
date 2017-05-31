#include "shortcut_manager.h"

namespace dmr {
static ShortcutManager* _shortcutManager = nullptr;

ShortcutManager& ShortcutManager::get() 
{
    if (!_shortcutManager) {
        _shortcutManager = new ShortcutManager();
    }
    return *_shortcutManager;
}

ShortcutManager::~ShortcutManager() 
{
}

void ShortcutManager::buildBindings() 
{
    _map.clear();
    _map.insert(QKeySequence(Qt::Key_Left), ActionKind::SeekBackward);
    _map.insert(QKeySequence(Qt::Key_Left + Qt::SHIFT), ActionKind::SeekBackwardLarge);
    _map.insert(QKeySequence(Qt::Key_Right), ActionKind::SeekForward);
    _map.insert(QKeySequence(Qt::Key_Right + Qt::SHIFT), ActionKind::SeekForwardLarge);
    _map.insert(QKeySequence(Qt::Key_Space), ActionKind::TogglePause);

    emit bindingsChanged();
}

vector<QAction*> ShortcutManager::actionsForBindings()
{
    vector<QAction*> actions;
    auto p = _map.constBegin();
    while (p != _map.constEnd()) {
        auto *act = new QAction(this);
        act->setShortcut(p.key());
        act->setShortcutContext(Qt::ApplicationShortcut);
        act->setProperty("kind", p.value());
        actions.push_back(act);

        ++p;
    }

    return actions;
}

}

