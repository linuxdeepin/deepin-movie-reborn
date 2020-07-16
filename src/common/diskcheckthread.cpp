#include "diskcheckthread.h"

#include <sys/ioctl.h>
#include <fcntl.h>
#include </usr/include/linux/cdrom.h>
#include <QFile>


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
        QString strLine = mountFile.readLine();
        if ( strLine.indexOf("/dev/sr") != -1 || strLine.indexOf("/dev/cdrom") != -1) {
            strDiskPath = strLine.split(" ").at(0);
            strDiskName = strLine.split(" ").at(1);

            int nFd = open(strDiskPath.toLatin1(), O_RDWR | O_NONBLOCK);
            if (nFd && ioctl(nFd, CDROM_DRIVE_STATUS) == CDS_TRAY_OPEN) {
                bOpen = false;
            }

            if (!m_mapDisk2Name.contains(strDiskPath) && bOpen) {
                m_mapDisk2Name.insert(strLine.split(" ").at(0), strLine.split(" ").at(1));
            } else if (m_mapDisk2Name.contains(strDiskPath) && m_mapDisk2Name.value(strDiskPath) != strDiskName && bOpen) {
                listDisk.removeOne(m_mapDisk2Name.value(strDiskPath));
                m_mapDisk2Name.insert(strLine.split(" ").at(0), strLine.split(" ").at(1));
            } else {
                if (bOpen) {
                    listDisk.removeOne(strDiskName);
                } else {
                    m_mapDisk2Name.remove(strDiskPath);
                }
            }
        }
    } while (!mountFile.atEnd() );

    foreach (QString strDiskName, listDisk) {
        emit diskRemove(strDiskName);
    }
}
