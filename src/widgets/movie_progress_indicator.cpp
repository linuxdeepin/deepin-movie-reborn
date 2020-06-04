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
#include "movie_progress_indicator.h"


namespace dmr {

MovieProgressIndicator::MovieProgressIndicator(QWidget *parent)
    : QFrame(parent)
{
    QFont ft;
    ft.setPixelSize(14);
    QFontMetrics fm(ft);
    this->setFont(ft);

    _fixedSize = QSize(qMax(52, fm.width("999:99")), fm.height() + 10);
    this->setFixedSize(_fixedSize);
#ifdef __mips__ && __aarch64__ && __sw_64__
    this->setAttribute(Qt::WA_TranslucentBackground);
    this->setWindowFlags(Qt::FramelessWindowHint);
    setWindowFlags(this->windowFlags() | Qt::Dialog);
#endif
}

void MovieProgressIndicator::paintEvent(QPaintEvent *pe)
{
    auto time_text = QTime::currentTime().toString("hh:mm");

    QPainter p(this);

    p.setFont(font());
    p.setPen(QColor(255, 255, 255, 255 * .4));

    QFontMetrics fm(font());
    auto fr = fm.boundingRect(time_text);
    fr.moveCenter(QPoint(rect().center().x(), fr.height() / 2));
    p.drawText(fr, time_text);


    QPoint pos((_fixedSize.width() - 48) / 2, rect().height() - 5);
    int pert = qMin(_pert * 10, 10.0);
    for (int i = 0; i < 10; i++) {
        if (i >= pert) {
            p.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .25));
        } else {
            p.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .5));
        }
        pos.rx() += 5;
    }
}

void MovieProgressIndicator::updateMovieProgress(qint64 duration, qint64 pos)
{
    _elapsed = pos;
    _pert = (qreal)pos / duration;
    update();
}

}
