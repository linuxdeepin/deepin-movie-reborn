#include "shortcut_manager.h"
#include "dmr_settings.h"

#include <DSettingsOption>
#include <DSettingsGroup>
#include <DSettings>

namespace dmr {
using namespace Dtk::Core;
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
        {"pause_play", ActionFactory::ActionKind::TogglePause},
        {"seek_forward", ActionFactory::ActionKind::SeekForward},
        {"seek_forward_large", ActionFactory::ActionKind::SeekForwardLarge},
        {"seek_backward", ActionFactory::ActionKind::SeekBackward},
        {"seek_backward_large", ActionFactory::ActionKind::SeekBackwardLarge},
        {"fullscreen", ActionFactory::ActionKind::Fullscreen},
        {"playlist", ActionFactory::ActionKind::TogglePlaylist},
        {"accel", ActionFactory::ActionKind::AccelPlayback},
        {"decel", ActionFactory::ActionKind::DecelPlayback},
        {"reset", ActionFactory::ActionKind::ResetPlayback},

        {"mini", ActionFactory::ActionKind::ToggleMiniMode},
        {"vol_up", ActionFactory::ActionKind::VolumeUp},
        {"vol_down", ActionFactory::ActionKind::VolumeDown},
        {"mute", ActionFactory::ActionKind::ToggleMute},

        {"open_file", ActionFactory::ActionKind::OpenFile},
        {"playlist_next", ActionFactory::ActionKind::GotoPlaylistNext},
        {"playlist_prev", ActionFactory::ActionKind::GotoPlaylistPrev},

        {"sub_forward", ActionFactory::ActionKind::SubForward},
        {"sub_backward", ActionFactory::ActionKind::SubDelay},

        {"screenshot", ActionFactory::ActionKind::Screenshot},
        {"burst_screenshot", ActionFactory::ActionKind::BurstScreenshot},
    };

    connect(&Settings::get(), &Settings::shortcutsChanged,
        [=](QString sk, const QVariant& val) {
            if (sk.endsWith(".enable")) {
                auto grp_key = sk.left(sk.lastIndexOf('.'));
                qDebug() << "update group binding" << grp_key;

                QPointer<DSettingsGroup> shortcuts = Settings::get().shortcuts();
                auto grps = shortcuts->childGroups();
                auto grp_ptr = std::find_if(grps.begin(), grps.end(), [=](GroupPtr grp) {
                    return grp->key() == grp_key;
                });

                toggleGroupShortcuts(*grp_ptr, val.toBool());
                emit bindingsChanged();
                return;
            }

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

void ShortcutManager::toggleGroupShortcuts(GroupPtr grp, bool on)
{
    auto sub = grp->childOptions();
    std::for_each(sub.begin(), sub.end(), [=](OptionPtr opt) {
        if (opt->viewType() != "shortcut") return;

        QStringList keyseqs = opt->value().toStringList();
        Q_ASSERT (keyseqs.length() == 2);
        auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
        auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

        QString sk = opt->key();
        sk.remove(0, sk.lastIndexOf('.') + 1);
        qDebug() << opt->name() << QKeySequence(modifier + key);

        if (on) {
            _map[QKeySequence(modifier + key)] = _keyToAction[sk];
        } else {
            _map.remove(QKeySequence(modifier + key));
        }
    });
}

void ShortcutManager::buildBindingsFromSettings()
{
    _map.clear();
    // default builtins 
    _map.insert(QKeySequence(Qt::Key_Left), ActionFactory::ActionKind::SeekBackward);
    _map.insert(QKeySequence(Qt::Key_Left + Qt::SHIFT), ActionFactory::ActionKind::SeekBackwardLarge);
    _map.insert(QKeySequence(Qt::Key_Right), ActionFactory::ActionKind::SeekForward);
    _map.insert(QKeySequence(Qt::Key_Right + Qt::SHIFT), ActionFactory::ActionKind::SeekForwardLarge);
    _map.insert(QKeySequence(Qt::Key_Space), ActionFactory::ActionKind::TogglePause);

    QPointer<DSettingsGroup> shortcuts = Settings::get().shortcuts();

    auto subgroups = shortcuts->childGroups();
    std::for_each(subgroups.begin(), subgroups.end(), [=](GroupPtr grp) {
        auto enabled = Settings::get().settings()->option(grp->key() + ".enable");
        qDebug() << grp->name() << enabled->value();
        Q_ASSERT(enabled && enabled->viewType() == "checkbox");
        if (!enabled->value().toBool()) 
            return;

        toggleGroupShortcuts(grp, true);
    });

}

vector<QAction*> ShortcutManager::actionsForBindings()
{
    vector<QAction*> actions;
    auto p = _map.constBegin();
    while (p != _map.constEnd()) {
        auto *act = new QAction(this);
        switch (p.value()) {
            case ActionFactory::ActionKind::SeekForward:
            case ActionFactory::ActionKind::SeekForwardLarge:
            case ActionFactory::ActionKind::SeekBackward:
            case ActionFactory::ActionKind::SeekBackwardLarge:
            case ActionFactory::ActionKind::VolumeUp:
            case ActionFactory::ActionKind::VolumeDown:
                act->setAutoRepeat(true);
                break;
            default:
                act->setAutoRepeat(false);
                break;
        }
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

