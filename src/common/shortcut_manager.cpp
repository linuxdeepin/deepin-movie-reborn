/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
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
    : QObject(nullptr)
{
    _keyToAction = {
        {"pause_play", ActionFactory::ActionKind::TogglePause},
        {"seek_forward", ActionFactory::ActionKind::SeekForward},
        {"seek_forward_large", ActionFactory::ActionKind::SeekForwardLarge},
        {"seek_backward", ActionFactory::ActionKind::SeekBackward},
        {"seek_backward_large", ActionFactory::ActionKind::SeekBackwardLarge},
        {"fullscreen", ActionFactory::ActionKind::ToggleFullscreen},
        {"exitfullscreen", ActionFactory::ActionKind::QuitFullscreen},
        {"playlist", ActionFactory::ActionKind::TogglePlaylist},
        {"accel", ActionFactory::ActionKind::AccelPlayback},
        {"decel", ActionFactory::ActionKind::DecelPlayback},
        {"reset", ActionFactory::ActionKind::ResetPlayback},
        {"delete_from_playlist", ActionFactory::ActionKind::PlaylistRemoveItem},
        {"movie_info", ActionFactory::ActionKind::MovieInfo},

        {"mini", ActionFactory::ActionKind::ToggleMiniMode},
        {"vol_up", ActionFactory::ActionKind::VolumeUp},
        {"vol_down", ActionFactory::ActionKind::VolumeDown},
        {"mute", ActionFactory::ActionKind::ToggleMute},

        {"open_file", ActionFactory::ActionKind::OpenFileList},
        {"playlist_next", ActionFactory::ActionKind::GotoPlaylistNext},
        {"playlist_prev", ActionFactory::ActionKind::GotoPlaylistPrev},

        {"sub_forward", ActionFactory::ActionKind::SubForward},
        {"sub_backward", ActionFactory::ActionKind::SubDelay},

        {"screenshot", ActionFactory::ActionKind::Screenshot},
        {"burst_screenshot", ActionFactory::ActionKind::BurstScreenshot},

        {"next_frame", ActionFactory::ActionKind::NextFrame},
        {"previous_frame", ActionFactory::ActionKind::PreviousFrame},
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

//            QStringList keyseqs = val.toStringList();
//            auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
//            auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

            qDebug() << "update binding" << sk << QKeySequence(val.toStringList().at(0));
            QString strKey = QKeySequence(val.toStringList().at(0)).toString();
            if (strKey.contains("Return")) {
                _map[QKeySequence(val.toStringList().at(0))] = _keyToAction[sk];
                strKey = QString("%1Num+Enter").arg(strKey.remove("Return"));
                _map[strKey] = _keyToAction[sk];
                qDebug() << val << QKeySequence(strKey) << strKey;

                _map.remove(strKey);
                _map[strKey] = _keyToAction[sk];

            } else if (strKey.contains("Num+Enter")) {
                _map[QKeySequence(val.toStringList().at(0))] = _keyToAction[sk];
                strKey = QString("%1Return").arg(strKey.remove("Num+Enter"));
                _map[strKey] = _keyToAction[sk];
                qDebug() << val << QKeySequence(strKey) << strKey;
            }

            _map.remove(_map.key(_keyToAction[sk]));
            _map[QKeySequence(val.toStringList().at(0))] = _keyToAction[sk];
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

//        QStringList keyseqs = opt->value().toStringList();
//        auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
//        auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

        QString sk = opt->key();
        sk.remove(0, sk.lastIndexOf('.') + 1);
        qDebug() << opt->name()
                 << QKeySequence(opt->value().toStringList().at(0))
                 << QKeySequence(opt->value().toStringList().at(0)).toString();
        QString strKey = QKeySequence(opt->value().toStringList().at(0)).toString();

        if (strKey.contains("Return")) {
            _map[QKeySequence(opt->value().toStringList().at(0))] = _keyToAction[sk];
//                strKey = QString("%1Return, %1Num+Enter").arg(strKey.remove("Return"));
            strKey = QString("%1Num+Enter").arg(strKey.remove("Return"));
            _map[strKey] = _keyToAction[sk];
            qDebug() << opt->name() << QKeySequence(strKey) << strKey;

        } else if (strKey.contains("Num+Enter")) {
            _map[QKeySequence(opt->value().toStringList().at(0))] = _keyToAction[sk];
//                strKey = QString("%1Return, %1Num+Enter").arg(strKey.remove("Num+Enter"));
            strKey = QString("%1Return").arg(strKey.remove("Num+Enter"));
            _map[strKey] = _keyToAction[sk];
            qDebug() << opt->name() << QKeySequence(strKey) << strKey;
        }

        if (on) {
            _map[strKey] = _keyToAction[sk];
            qDebug() << opt->name() << QKeySequence(strKey) << strKey;
            _map[QKeySequence(opt->value().toStringList().at(0))] = _keyToAction[sk];
        } else {
            _map.remove(QKeySequence(opt->value().toStringList().at(0)));
        }
    });
}

void ShortcutManager::buildBindingsFromSettings()
{
    _map.clear();
    // default builtins 
//    _map.insert(QKeySequence(Qt::Key_Left), ActionFactory::SeekBackward);
//    _map.insert(QKeySequence(Qt::Key_Left + Qt::SHIFT), ActionFactory::SeekBackwardLarge);
//    _map.insert(QKeySequence(Qt::Key_Right), ActionFactory::SeekForward);
//    _map.insert(QKeySequence(Qt::Key_Right + Qt::SHIFT), ActionFactory::SeekForwardLarge);
//    _map.insert(QKeySequence(Qt::Key_Space), ActionFactory::TogglePause);
//    _map.insert(QKeySequence(Qt::Key_Escape), ActionFactory::QuitFullscreen);
    _map.insert(QKeySequence(Qt::Key_Slash + Qt::CTRL + Qt::SHIFT), ActionFactory::ViewShortcut);

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

QString ShortcutManager::toJson() 
{
    QJsonObject shortcutObj;
    QJsonArray jsonGroups;

    QPointer<DSettingsGroup> shortcuts = Settings::get().shortcuts();
    auto subgroups = shortcuts->childGroups();
    std::for_each(subgroups.begin(), subgroups.end(), [&](GroupPtr grp) {
        qDebug() << grp->name();

        QJsonObject jsonGroup;
        jsonGroup.insert("groupName", qApp->translate("QObject", grp->name().toUtf8().data()));
        QJsonArray jsonItems;
 
        auto sub = grp->childOptions();
        std::for_each(sub.begin(), sub.end(), [&](OptionPtr opt) {
            if (opt->viewType() != "shortcut") return;

            QStringList keyseqs = opt->value().toStringList();
//            auto modifier = static_cast<Qt::KeyboardModifiers>(keyseqs.value(0).toInt());
//            auto key = static_cast<Qt::Key>(keyseqs.value(1).toInt());

            QJsonObject jsonItem;
            jsonItem.insert("name", qApp->translate("QObject", opt->name().toUtf8().data()));
            jsonItem.insert("value", QKeySequence(opt->value().toStringList().at(0)).toString(QKeySequence::PortableText));
            jsonItems.append(jsonItem);

        });

//        if(grp->name() == "File")
//        {
//            QJsonObject jsonItem_space;
//            jsonItem_space.insert("name", qApp->translate("QObject", ""));
//            jsonItem_space.insert("value", "");
//            jsonItems.append(jsonItem_space);

//            QJsonObject jsonItem;
//            jsonItem.insert("name", qApp->translate("QObject", "Help"));
//            jsonItem.insert("value", "F1");
//            jsonItems.append(jsonItem);

//            QJsonObject jsonItem_show;
//            jsonItem.insert("name", qApp->translate("QObject", "Display shortcuts"));
//            jsonItem.insert("value", "Ctrl+Shift+?");
//            jsonItems.append(jsonItem);
//        }

        jsonGroup.insert("groupItems", jsonItems);
        jsonGroups.append(jsonGroup);
    });

    QJsonObject jsonGroup;
    jsonGroup.insert("groupName", qApp->translate("QObject", "Settings"));
    QJsonArray jsonItems;
    QJsonObject jsonItem;
    jsonItem.insert("name", qApp->translate("QObject", "Help"));
    jsonItem.insert("value", "F1");
    jsonItems.append(jsonItem);

    QJsonObject jsonItem_show;
    jsonItem.insert("name", qApp->translate("QObject", "Display shortcuts"));
    jsonItem.insert("value", "Ctrl+Shift+?");
    jsonItems.append(jsonItem);
    jsonGroup.insert("groupItems", jsonItems);
    jsonGroups.append(jsonGroup);

    shortcutObj.insert("shortcut", jsonGroups);

    QJsonDocument doc(shortcutObj);
    return doc.toJson().data();
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
            case ActionFactory::ActionKind::AccelPlayback:
            case ActionFactory::ActionKind::DecelPlayback:
                act->setAutoRepeat(true);
                break;
            default:
                act->setAutoRepeat(false);
                break;
        }
        act->setShortcut(p.key());
        //act->setShortcutContext(Qt::ApplicationShortcut);
        act->setProperty("kind", p.value());
        act->setProperty("origin", "shortcut");
        actions.push_back(act);

        qDebug() << "action " << p.key() << p.value();
        ++p;
    }

    return actions;
}

}

