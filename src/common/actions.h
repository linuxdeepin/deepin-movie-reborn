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
#ifndef _DMR_ACTIONS_H
#define _DMR_ACTIONS_H 

#include <QtWidgets>
#include <DMenu>

DWIDGET_USE_NAMESPACE

namespace dmr {
class PlayingMovieInfo;


class ActionFactory: public QObject {
    Q_OBJECT
public:
enum ActionKind {
    Invalid = 0,
    OpenFile = 1,
    OpenFileList,
    OpenDirectory,
    StartPlay,
    Settings,
    LightTheme,
    About,
    Help,
    Exit,

    TogglePlaylist,
    EmptyPlaylist,
    PlaylistRemoveItem,
    PlaylistOpenItemInFM,
    PlaylistItemInfo,
    MovieInfo,
    OpenUrl,
    OpenCdrom,
    ToggleFullscreen,
    QuitFullscreen,
    ToggleMiniMode,
    WindowAbove,
    LoadSubtitle,
    SelectSubtitle, // stub for subs loaded from movie
    HideSubtitle,
    MatchOnlineSubtitle,
    ChangeSubCodepage,
    Screenshot,
    BurstScreenshot,

    SeekForward,
    SeekForwardLarge,
    SeekBackward,
    SeekBackwardLarge,
    SeekAbsolute,
    TogglePause,
    Stop,
    AccelPlayback,
    DecelPlayback,
    ResetPlayback,
    SubDelay, //backward
    SubForward,

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
    NextFrame,
    PreviousFrame,

    //sound
    Stereo,
    LeftChannel,
    RightChannel,
    LoadTrack,
    SelectTrack, // stub for tracks loaded from movie

    GotoPlaylistNext,
    GotoPlaylistPrev,
    GotoPlaylistSelected,
    VolumeUp,
    VolumeDown,
    ToggleMute,
    ChangeVolume,


    ViewShortcut,
};
Q_ENUM(ActionKind)

    static ActionFactory& get();

    DMenu* titlebarMenu();
    DMenu* mainContextMenu();
    template<class UnaryFunction> 
    void forEachInMainMenu(UnaryFunction f);
    DMenu* playlistContextMenu();
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

    static bool isActionFromShortcut(QAction* act) {
        auto s = act->property("origin");
        return s.toString() == "shortcut";
    }

private:
    ActionFactory() {}
    DMenu *_titlebarMenu {nullptr};
    DMenu *_contextMenu {nullptr};
    DMenu *_subtitleMenu {nullptr};
    DMenu *_tracksMenu {nullptr};
    DMenu *_playlistMenu {nullptr};
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

#endif /* ifndef _DMR_ACTIONS_H */
