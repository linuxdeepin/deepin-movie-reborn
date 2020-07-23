#ifndef DISKCHECKTHREAD_H
#define DISKCHECKTHREAD_H

#include <QMap>
#include <QTimer>
#include <QObject>

class Diskcheckthread:public QObject
{
    Q_OBJECT

signals:
    void diskRemove(QString strDiskName);
public:
    Diskcheckthread();

    void start();
    void stop();

protected slots:
    void diskChecking();

private:
    QMap<QString, QString> m_mapDisk2Name;
    QTimer m_timerDiskCheck;
};

#endif // DISKCHECKTHREAD_H
