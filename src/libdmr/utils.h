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
    void ShowInFileManager(const QString &path);
    bool CompareNames(const QString& fileName1, const QString& fileName2);
    bool IsNamesSimilar(const QString& s1, const QString& s2);
    QFileInfoList FindSimilarFiles(const QFileInfo& fi);
    QString FastFileHash(const QFileInfo& fi);
    QString FullFileHash(const QFileInfo& fi);

    QPixmap MakeRoundedPixmap(QPixmap pm, qreal rx, qreal ry, int rotation = 0);
    QPixmap MakeRoundedPixmap(QSize sz, QPixmap pm, qreal rx, qreal ry, qint64 time);

    QImage LoadHiDPIImage(const QString& filename);
    QPixmap LoadHiDPIPixmap(const QString& filename);

    uint32_t InhibitStandby();
    void UnInhibitStandby(uint32_t cookie);

    uint32_t InhibitPower();
    void UnInhibitPower(uint32_t cookie);

    void MoveToCenter(QWidget* w);

    QString Time2str(qint64 seconds);

    bool ValidateScreenshotPath(const QString& path);

    QString ElideText(const QString &text, const QSize &size,
            QTextOption::WrapMode wordWrap, const QFont &font,
            Qt::TextElideMode mode, int lineHeight, int lastLineWidth);
}
}

#endif /* ifndef _DMR_UTILS_H */
