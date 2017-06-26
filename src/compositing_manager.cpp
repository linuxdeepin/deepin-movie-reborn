#include "compositing_manager.h"
#include <iostream>
#include <QtCore>
#include <QX11Info>


//TODO: override by user setting

namespace dmr {
using namespace std;

static CompositingManager* _compManager = nullptr;

#define C2Q(cs) (QString::fromUtf8((cs).c_str()))

class PlatformChecker {
public:
    Platform check() {
        QProcess uname;
        uname.start("uname -m");
        if (uname.waitForStarted()) {
            if (uname.waitForFinished()) {
                auto data = uname.readAllStandardOutput();
                string machine(data.trimmed().constData());
                qDebug() << QString("machine: %1").arg(machine.c_str());

                QRegExp re("x86.*|i?86|ia64", Qt::CaseInsensitive);
                if (re.indexIn(C2Q(machine)) != -1) {
                    qDebug() << "match x86";
                    _pf = Platform::X86;

                } else if (machine.find("alpha") != string::npos
                        || machine.find("sw_64") != string::npos) {
                    // shenwei
                    qDebug() << "match shenwei";
                    _pf = Platform::Alpha;

                } else if (machine.find("mips") != string::npos) { // loongson
                    qDebug() << "match loongson";
                    _pf = Platform::Alpha;
                }
            }
        }

        return _pf;
    }

private:
    Platform _pf {Platform::Unknown};
};


CompositingManager& CompositingManager::get() {
    if(!_compManager) {
        _compManager = new CompositingManager();
    }

    return *_compManager;
}

//void compositingChanged(bool);

CompositingManager::CompositingManager() {
    _platform = PlatformChecker().check();

    _composited = false;
    if (isDriverLoadedCorrectly() && isDirectRendered()) {
        _composited = true;
    }

    qDebug() << "composited:" << _composited;
}

CompositingManager::~CompositingManager() {
}

bool CompositingManager::isDriverLoadedCorrectly() {
    static QRegExp aiglx_err("\\(EE\\)\\s+AIGLX error");
    static QRegExp dri_ok("direct rendering: DRI\\d+ enabled");
    static QRegExp swrast("GLX: Initialized DRISWRAST");

    QString xorglog = QString("/var/log/Xorg.%1.log").arg(QX11Info::appScreen());
    qDebug() << "check " << xorglog;
    QFile f(xorglog);
    if (!f.open(QFile::ReadOnly)) {
        qWarning() << "can not open " << xorglog;
        return false;
    }

    QTextStream ts(&f);
    while (!ts.atEnd()) {
        QString ln = ts.readLine();
        if (aiglx_err.indexIn(ln) != -1) {
            qDebug() << "found aiglx error";
            return false;
        }

        if (dri_ok.indexIn(ln) != -1) {
            qDebug() << "dri enabled successfully";
            return true;
        }

        if (swrast.indexIn(ln) != -1) {
            qDebug() << "swrast driver used";
            return false;
        }
    }

    return true;
}

bool CompositingManager::isDirectRendered() {
    QProcess xdriinfo;
    xdriinfo.start("xdriinfo driver 0");
    if (xdriinfo.waitForStarted() && xdriinfo.waitForFinished()) {
        QString drv = QString::fromUtf8(xdriinfo.readAllStandardOutput().trimmed().constData());
        qDebug() << "xdriinfo: " << drv;
        return !drv.contains("is not direct rendering capable");
    }

    return true;
}

#undef C2Q
}

