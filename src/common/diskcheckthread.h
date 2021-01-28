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
#ifndef DISKCHECKTHREAD_H
#define DISKCHECKTHREAD_H

#include <QMap>
#include <QTimer>
#include <QObject>

/**
 * @file 光盘监测线程，监测光盘状态
 */
class Diskcheckthread: public QObject
{
    Q_OBJECT

signals:
    /**
     * @file 光盘移除信号
     * @param 移除的光盘名
     */
    void diskRemove(QString sDiskName);

public:
    Diskcheckthread();
    /**
     * @file 启动计时器循环检测线程
     */
    void start();
    /**
     * @file 停止定时器
     */
    void stop();

protected slots:
    void diskChecking();

private:
    QMap<QString, QString> m_mapDisk2Name; //光盘挂载路径和光盘挂载名的映射
    QTimer m_timerDiskCheck;               //监测定时器
};

#endif // DISKCHECKTHREAD_H
