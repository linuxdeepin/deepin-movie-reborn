// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diskcheckthread.h"

#include <sys/ioctl.h>
#include <fcntl.h>
#include </usr/include/linux/cdrom.h>
#include <QFile>
#include <unistd.h>
#include <QDebug>

Diskcheckthread::Diskcheckthread()
{
    qDebug() << "Initializing disk check thread";
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
    qDebug() << "Starting disk check cycle";
    QFile mountFile("/proc/mounts");
    QString strDiskPath;
    QString strDiskName;
    bool bOpen = true;
    QList<QString> listDisk = m_mapDisk2Name.values();
    
    if (mountFile.open(QIODevice::ReadOnly) == false) {
        qWarning() << "Failed to open /proc/mounts for reading";
        return;
    }

    do {
        QString sLine = mountFile.readLine();
        if (sLine.indexOf("/dev/sr") != -1 || sLine.indexOf("/dev/cdrom") != -1) {
            strDiskPath = sLine.split(" ").at(0);
            strDiskName = sLine.split(" ").at(1);
            qDebug() << "Found disk device:" << strDiskPath << "mounted at:" << strDiskName;

            int nFd = open(strDiskPath.toLatin1(), O_RDWR | O_NONBLOCK);
            if (nFd && ioctl(nFd, CDROM_DRIVE_STATUS) == CDS_TRAY_OPEN) {
                bOpen = false;
            }

            if (!m_mapDisk2Name.contains(strDiskPath) && bOpen) {
                qDebug() << "New disk detected:" << strDiskPath << "->" << strDiskName;
                m_mapDisk2Name.insert(sLine.split(" ").at(0), sLine.split(" ").at(1));
            } else if (m_mapDisk2Name.contains(strDiskPath) && m_mapDisk2Name.value(strDiskPath) != strDiskName && bOpen) {
                qDebug() << "Disk mount point changed:" << strDiskPath << "from" << m_mapDisk2Name.value(strDiskPath) << "to" << strDiskName;
                listDisk.removeOne(m_mapDisk2Name.value(strDiskPath));
                m_mapDisk2Name.insert(sLine.split(" ").at(0), sLine.split(" ").at(1));
            } else {
                if (bOpen) {
                    qDebug() << "Removing disk from tracking list:" << strDiskName;
                    listDisk.removeOne(strDiskName);
                } else {
                    qDebug() << "Removing disk from map due to open tray:" << strDiskPath;
                    m_mapDisk2Name.remove(strDiskPath);
                }
            }
            close(nFd);
        }
    } while (!mountFile.atEnd());
    mountFile.close();

    foreach (QString strDiskName, listDisk) {
        qInfo() << "Emitting disk removal signal for:" << strDiskName;
        m_mapDisk2Name.clear();
        emit diskRemove(strDiskName);
    }
    
    qDebug() << "Completed disk check cycle";
}
