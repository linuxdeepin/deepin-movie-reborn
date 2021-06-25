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
#include "diskcheckthread.h"

#include <sys/ioctl.h>
#include <fcntl.h>
#include </usr/include/linux/cdrom.h>
#include <QFile>
#include <unistd.h>

Diskcheckthread::Diskcheckthread()
{
    m_timerDiskCheck.setInterval(2000);
    connect(&m_timerDiskCheck, &QTimer::timeout, this, &Diskcheckthread::diskChecking);
}

void Diskcheckthread::start()
{
    m_timerDiskCheck.start();
}

void Diskcheckthread::stop()
{
    m_timerDiskCheck.stop();
}

void Diskcheckthread::diskChecking()
{
    QFile mountFile("/proc/mounts");
    QString strDiskPath;
    QString strDiskName;
    bool bOpen = true;
    QList<QString> listDisk = m_mapDisk2Name.values();
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        return;
    }
    do {
        QString sLine = mountFile.readLine();
        if (sLine.indexOf("/dev/sr") != -1 || sLine.indexOf("/dev/cdrom") != -1) {
            strDiskPath = sLine.split(" ").at(0);
            strDiskName = sLine.split(" ").at(1);

            int nFd = open(strDiskPath.toLatin1(), O_RDWR | O_NONBLOCK);
            if (nFd && ioctl(nFd, CDROM_DRIVE_STATUS) == CDS_TRAY_OPEN) {
                bOpen = false;
            }

            if (!m_mapDisk2Name.contains(strDiskPath) && bOpen) {
                m_mapDisk2Name.insert(sLine.split(" ").at(0), sLine.split(" ").at(1));
            } else if (m_mapDisk2Name.contains(strDiskPath) && m_mapDisk2Name.value(strDiskPath) != strDiskName && bOpen) {
                listDisk.removeOne(m_mapDisk2Name.value(strDiskPath));
                m_mapDisk2Name.insert(sLine.split(" ").at(0), sLine.split(" ").at(1));
            } else {
                if (bOpen) {
                    listDisk.removeOne(strDiskName);
                } else {
                    m_mapDisk2Name.remove(strDiskPath);
                }
            }
            close(nFd);
        }
    } while (!mountFile.atEnd());
    mountFile.close();

    foreach (QString strDiskName, listDisk) {
        m_mapDisk2Name.clear();
        emit diskRemove(strDiskName);
    }
}
