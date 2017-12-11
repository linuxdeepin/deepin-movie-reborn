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
#ifndef _DMR_EVENT_RELAYER_H
#define _DMR_EVENT_RELAYER_H 

#include <QWindow>
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <QPoint>

namespace dmr {

class EventRelayer: public QObject, public QAbstractNativeEventFilter 
{
    Q_OBJECT
public:
    EventRelayer(QWindow* src, QWindow *dest);
    ~EventRelayer();

signals:
    void targetNeedsUpdatePosition(const QPoint& p);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, long *) override;

private:
    QWindow *_source, *_target;
};

}

#endif /* ifndef _DMR_EVENT_RELAYER_H */
