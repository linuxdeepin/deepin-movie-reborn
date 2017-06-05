#include "shortcut_manager.h"
#include "dmr_settings.h"

#include <option.h>
#include <group.h>
#include <settings.h>

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

ShortcutManager::ShortcutManager() 
    :QObject(0)
{
    _keyToAction = {
        {"pause_play", ActionKind::TogglePause},
        {"seek_forward", ActionKind::SeekForward},
        {"seek_forward_large", ActionKind::SeekForwardLarge},
        {"seek_backward", ActionKind::SeekBackward},
        {"seek_backward_large", ActionKind::SeekBackwardLarge},
        {"open_file", ActionKind::OpenFile},
        {"screenshot", ActionKind::Screenshot},
        {"burst_screenshot", ActionKind::BurstScreenshot},
    };

    connect(Settings::get().settings(), &Dtk::Settings::valueChanged,
        [=](QString sk, QVariant val) {

            sk.remove(0, sk.lastIndexOf('.') + 1);

            QStringList keyseqs = val.toStringList();
            Q_ASSERT (keyseqs.length() == 2);
            auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
            auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

            qDebug() << "update binding" << sk << QKeySequence(modifier + key);
            _map.remove(_map.key(_keyToAction[sk]));
            _map[QKeySequence(modifier + key)] = _keyToAction[sk];
            emit bindingsChanged();
        });
}

void ShortcutManager::buildBindings() 
{

    buildBindingsFromSettings();

    emit bindingsChanged();
}

void ShortcutManager::buildBindingsFromSettings()
{
    _map.clear();
    // default builtins 
    _map.insert(QKeySequence(Qt::Key_Left), ActionKind::SeekBackward);
    _map.insert(QKeySequence(Qt::Key_Left + Qt::SHIFT), ActionKind::SeekBackwardLarge);
    _map.insert(QKeySequence(Qt::Key_Right), ActionKind::SeekForward);
    _map.insert(QKeySequence(Qt::Key_Right + Qt::SHIFT), ActionKind::SeekForwardLarge);
    _map.insert(QKeySequence(Qt::Key_Space), ActionKind::TogglePause);

    QPointer<Dtk::Group> shortcuts = Settings::get().settings()->group("shortcuts");

    auto subgroups = shortcuts->childGroups();
    std::for_each(subgroups.begin(), subgroups.end(), [=](Dtk::GroupPtr grp) {
        auto sub = grp->childOptions();
        std::for_each(sub.begin(), sub.end(), [=](Dtk::OptionPtr opt) {
            QStringList keyseqs = opt->value().toStringList();
            Q_ASSERT (keyseqs.length() == 2);
            auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
            auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

            QString sk = opt->key();
            sk.remove(0, sk.lastIndexOf('.') + 1);
            qDebug() << sk << opt->name() << opt->viewType() << keyseqs 
                << QKeySequence(modifier + key);

            _map[QKeySequence(modifier + key)] = _keyToAction[sk];
        });
    });

}

vector<QAction*> ShortcutManager::actionsForBindings()
{
    vector<QAction*> actions;
    auto p = _map.constBegin();
    while (p != _map.constEnd()) {
        auto *act = new QAction(this);
        act->setShortcut(p.key());
        //act->setShortcutContext(Qt::ApplicationShortcut);
        act->setProperty("kind", p.value());
        actions.push_back(act);

        qDebug() << "action " << p.key() << p.value();
        ++p;
    }

    return actions;
}

}

