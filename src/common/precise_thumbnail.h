// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DMR_PRECISE_THUMBNAIL_H
#define DMR_PRECISE_THUMBNAIL_H

#include <QPixmap>
#include <QUrl>

namespace dmr {

class PreciseThumbnail
{
public:
    static QPixmap generate(const QUrl &url, int secs, const QSize &thumbSize, qreal dpr);
};

}

#endif // DMR_PRECISE_THUMBNAIL_H
