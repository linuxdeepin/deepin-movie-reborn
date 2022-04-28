/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: xiepengfei <xiepengfei@uniontech.com>
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
#ifndef GSTUTILS_H
#define GSTUTILS_H

#include <QString>
#include <QObject>
#include "playlist_model.h"

extern "C" {
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
}

namespace dmr {

typedef struct CustomData {
  GstDiscoverer *discoverer;
  GMainLoop *loop;
} CustomData;

class GstUtils
{

public:
    ~GstUtils();

    static GstUtils* get();

    static void discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, GError *err, CustomData *data);

    static void finished(GstDiscoverer *discoverer, CustomData *data);
    /**
     * @brief 使用gstreamer获取影片信息
     * @param 文件信息
     * @return 影片信息
     */
    MovieInfo parseFileByGst(const QFileInfo &fi);

private:
    GstUtils();

private:
    static MovieInfo m_movieInfo;
    static GstUtils* m_pGstUtils;
    CustomData m_gstData;
};
}

#endif // GSTUTILS_H
