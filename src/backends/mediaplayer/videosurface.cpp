/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     zhuyuliang <zhuyuliang@uniontech.com>
 *
 * Maintainer: zhuyuliang <zhuyuliang@uniontech.com>
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
#include "videosurface.h"

VideoSurface::VideoSurface(QObject *parent)
    : QAbstractVideoSurface(parent)
{
}

VideoSurface::~VideoSurface()
{
}

QList<QVideoFrame::PixelFormat> VideoSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const
{
    QList<QVideoFrame::PixelFormat> listPixelFormats;

//    listPixelFormats << QVideoFrame::Format_ARGB32
//        << QVideoFrame::Format_ARGB32_Premultiplied
//        << QVideoFrame::Format_RGB32
//        << QVideoFrame::Format_RGB24
//        << QVideoFrame::Format_RGB565
//        << QVideoFrame::Format_RGB555
//        << QVideoFrame::Format_ARGB8565_Premultiplied
//        << QVideoFrame::Format_BGRA32
//        << QVideoFrame::Format_BGRA32_Premultiplied
//        << QVideoFrame::Format_BGR32
//        << QVideoFrame::Format_BGR24
//        << QVideoFrame::Format_BGR565
//        << QVideoFrame::Format_BGR555
//        << QVideoFrame::Format_BGRA5658_Premultiplied
//        << QVideoFrame::Format_AYUV444
//        << QVideoFrame::Format_AYUV444_Premultiplied
//        << QVideoFrame::Format_YUV444
//        << QVideoFrame::Format_YUV420P
//        << QVideoFrame::Format_YV12
//        << QVideoFrame::Format_UYVY
//        << QVideoFrame::Format_YUYV
//        << QVideoFrame::Format_NV12
//        << QVideoFrame::Format_NV21
//        << QVideoFrame::Format_IMC1
//        << QVideoFrame::Format_IMC2
//        << QVideoFrame::Format_IMC3
//        << QVideoFrame::Format_IMC4
//        << QVideoFrame::Format_Y8
//        << QVideoFrame::Format_Y16
//        << QVideoFrame::Format_Jpeg
//        << QVideoFrame::Format_CameraRaw
//        << QVideoFrame::Format_AdobeDng;

    listPixelFormats << QVideoFrame::Format_RGB32;

    //qDebug() << listPixelFormats;

    // Return the formats you will support
    return listPixelFormats;
}

bool  VideoSurface::present(const QVideoFrame &frame)
{
    // Handle the frame and do your processing
    if (frame.isValid())
    {
        QVideoFrame cloneFrame(frame);
        emit frameAvailable(cloneFrame);

        return true;
    }

    return false;
}
