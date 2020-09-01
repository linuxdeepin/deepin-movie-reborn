/**
 * Copyright (C) 2020 UOS Technology Co., Ltd.
 *
 * to mark the desktop UI
 **/

#ifndef DESKTOP_ACCESSIBLE_UI_DEFINE_H
#define DESKTOP_ACCESSIBLE_UI_DEFINE_H

#include <QString>
#include <QObject>

// 使用宏定义，方便国际化操作

#define PLAY_BUTTOB_BOX QObject::tr("playButtonBox")
#define PREV_BUTTON QObject::tr("prevBtn") // 上一个视频按钮
#define PLAY_BUTTON QObject::tr("playBtn") // 播放、暂停按钮
#define NEXT_BUTTON QObject::tr("nextBtn") // 下一个视频按钮

#define VOLUME_SLIDER QObject::tr("volumeSlider")
#define FS_BUTTON QObject::tr("fullscreenButtion") // 全屏
#define VOLUME_BUTTON QObject::tr("volumeButton") // 音量
#define PLAYLIST_BUTTON QObject::tr("playlistButton") // 播放列表按钮

#define PLAYLIST_WIDGET QObject::tr("playlistWidget") // 播放列表
#define CLEAR_PLAYLIST_BUTTON QObject::tr("clearButton") // 清空播放列表

#define PLAYITEM_WIDGET QObject::tr("playItemWidget") //
#define PLAYITEN_CLOSE_BUTTON QObject::tr("playItemCloseBtn") // 关闭选中项



#endif // DESKTOP_ACCESSIBLE_UI_DEFINE_H
