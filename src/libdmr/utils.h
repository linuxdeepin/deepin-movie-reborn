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
#ifndef _DMR_UTILS_H
#define _DMR_UTILS_H

#include <QtGui>

namespace dmr {
namespace utils {
static unsigned int  DAYSECONDS = 86400;
void ShowInFileManager(const QString &path);
bool CompareNames(const QString &fileName1, const QString &fileName2);
bool first_check_wayland_env();
bool check_wayland_env();
void set_wayland(bool);
bool IsNamesSimilar(const QString &s1, const QString &s2);
QFileInfoList FindSimilarFiles(const QFileInfo &fi);
QString FastFileHash(const QFileInfo &fi);
QString FullFileHash(const QFileInfo &fi);

QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry, int rotation = 0);
QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time);

QImage LoadHiDPIImage(const QString &filename);
QPixmap LoadHiDPIPixmap(const QString &filename);

uint32_t InhibitStandby();
void UnInhibitStandby(uint32_t cookie);

uint32_t InhibitPower();
void UnInhibitPower(uint32_t cookie);

void MoveToCenter(QWidget *w);

QString Time2str(qint64 seconds);
QString videoIndex2str(int);
QString audioIndex2str(int);
QString subtitleIndex2str(int);

/**
 * @brief 检查截屏路径是否存在 cppcheck在使用
 */
bool ValidateScreenshotPath(const QString &path);

QString ElideText(const QString &text, const QSize &size,
                  QTextOption::WrapMode wordWrap, const QFont &font,
                  Qt::TextElideMode mode, int lineHeight, int lastLineWidth);
/**
 * @brief 获取播放配置
 * @param 配置文件path
 * @param 配置保存的map
 */
void getPlayProperty(const char *path, QMap<QString, QString> *&proMap);
}
}

#endif /* ifndef _DMR_UTILS_H */
