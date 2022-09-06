// Copyright (C) 2020 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DESKTOP_ACCESSIBLE_UI_DEFINE_H
#define DESKTOP_ACCESSIBLE_UI_DEFINE_H

#include <QString>
#include <QObject>

// 使用宏定义，方便国际化操作

//底部工具栏
#define BOTTOM_TOOL_BOX QObject::tr("bottomToolBox")
#define BOTTOM_WIDGET QObject::tr("bottomWidget")
#define BOTTOM_TOOL_BUTTON_WIDGET QObject::tr("bottomToolButtonWidget")

#define PLAY_BUTTOB_BOX QObject::tr("playButtonBox")
#define PROGBAR_WIDGET QObject::tr("progBarWidget")
#define MOVIE_PROGRESS_WIDGET QObject::tr("MovieProgressBarWidget")//进度条窗口
#define PROGBAR_SLIDER QObject::tr("progBarSlider")        //进度条

#define PREV_BUTTON QObject::tr("prevBtn") // 上一个视频按钮
#define PLAY_BUTTON QObject::tr("playBtn") // 播放、暂停按钮
#define NEXT_BUTTON QObject::tr("nextBtn") // 下一个视频按钮

#define FS_BUTTON QObject::tr("fullscreenButtion") // 全屏
#define VOLUME_BUTTON QObject::tr("volumeButton") // 音量
#define MIRVAST_BUTTON QObject::tr("mircastButton") // 投屏
#define PLAYLIST_BUTTON QObject::tr("playlistButton") // 播放列表按钮

//音量条
#define VOLUME_SLIDER_WIDGET QObject::tr("volumeSliderWidget")
#define VOLUME_SLIDER QObject::tr("volumeSlider")
#define SLIDER QObject::tr("slider")
#define MUTE_BTN QObject::tr("muteButton")



//播放列表
#define PLAYLIST_WIDGET QObject::tr("playListWidget")   //播放列表窗口
#define LEFT_WIDGET QObject::tr("leftWidget") // 左侧
#define CLEAR_PLAYLIST_BUTTON QObject::tr("clearButton") // 清空播放列表按钮

#define RIGHT_LIST_WIDGET QObject::tr("rightListWidget")//右侧文件列表
#define PLAYLIST QObject::tr("playlist")
#define FILE_LIST QObject::tr("fileList") //文件列表
#define PLAYITEM_WIDGET QObject::tr("playItemWidget") //单个视频项
#define PLAYITEN_CLOSE_BUTTON QObject::tr("playItemCloseBtn") // 关闭选中项




//movieInfo Dialog
#define MOVIE_INFO_DIALOG QObject::tr("movieInfoDialog")//影片信息窗口
#define MOVIE_INFO_SCROLL_AREA QObject::tr("movieInfoScrollArea")
#define SCROLL_AREA_VIEWPORT QObject::tr("scrollAreaViewport")
#define MOVIE_INFO_SCROLL_CONTENT QObject::tr("scrollContentWidget")

#define MOVIEINFO_CLOSE_BUTTON QObject::tr("movieInfoCloseButton") //关闭窗口按钮
#define FILM_INFO_WIDGET QObject::tr("filmInfoWidget")  //影片信息
#define CODEC_INFO_WIDGET QObject::tr("codecInfoWidget")  //编码信息
#define AUDIO_INFO_WIDGET QObject::tr("audioInfoWidget") //音频信息



////顶部菜单栏
#define TITLEBAR QObject::tr("titleBar")
//#define SETTINGS_DIALOG QObject::tr("settingsDialog")



////播放页面右键菜单
//#define PLAY_WIDGET_RIGHT_CLICK_MENU QObject::tr("playPageRightClickMenu")

//#define PLAY_MODE_MENU QObject::tr("playModeMenu")

//#define FRAME_MENU QObject::tr("frameMenu")

//#define SOUND_MENU QObject::tr("soundMenu")
//#define SOUND_CHANNEL_MENU QObject::tr("soundChannelMenu")
//#define SOUND_TRACK_MENU QObject::tr("soundTrackMenu")

//#define SUNTITLE_MENU QObject::tr("subtitleMenu")
//#define SUNTITLE_SELECT_MENU QObject::tr("subtitleSelectMenu")
//#define SUNTITLE_ENCODING_MENU QObject::tr("subtitleEncodingMenu")

//#define SCREENSHOT_MENU_MENU QObject::tr("screenshotMenu")

////投屏界面
#define MIRCAST_SUCCESSED          0
#define MIRCAST_EXIT              -1
#define MIRCAST_CONNECTION_FAILED -3
#define MIRCAST_DISCONNECTIONED   -4



#endif // DESKTOP_ACCESSIBLE_UI_DEFINE_H
