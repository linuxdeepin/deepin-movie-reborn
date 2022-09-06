// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

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
#ifndef _DMR_SHORTCUT_MANAGER
#define _DMR_SHORTCUT_MANAGER 

#include <memory>
#include <vector>
#include <QtCore>
#include "actions.h"

#include <DSettingsOption>
#include <DSettingsGroup>
#include <DSettings>

namespace dmr {

using namespace std;

using BindingMap = QHash<QKeySequence, ActionFactory::ActionKind>;
using ActionMap = QHash<QString, ActionFactory::ActionKind>;

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
        QString toJson();

    public slots:
        void buildBindings();

    signals:
        void bindingsChanged();

    private:
        ShortcutManager();

        BindingMap _map;
        ActionMap _keyToAction;

        void toggleGroupShortcuts(Dtk::Core::GroupPtr grp, bool on);
};

}

#endif /* ifndef _DMR_SHORTCUT_MANAGER */

