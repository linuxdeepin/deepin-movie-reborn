#include "compositing_manager.h"
#include "options.h"

#include <iostream>
#include <QtCore>
#include <QX11Info>

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glxext.h>
#undef Bool
#include <mpv/qthelper.hpp>

typedef const char * glXGetScreenDriver_t (Display *dpy, int scrNum);

static glXGetScreenDriver_t *GetScreenDriver;

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

    if (QProcessEnvironment::systemEnvironment().value("SANDBOX") == "flatpak") {
        _composited = QFile::exists("/dev/dri/card0");
    } else {
        GetScreenDriver = (glXGetScreenDriver_t *)glXGetProcAddressARB ((const GLubyte *)"glXGetScreenDriver");
        if (GetScreenDriver) {
            const char *name = (*GetScreenDriver) (QX11Info::display(), QX11Info::appScreen());
            qDebug() << "dri driver: " << name;
            _composited = name != NULL;
        } else {
            if (isDriverLoadedCorrectly() && isDirectRendered()) {
                _composited = true;
            }
        }
    }

    auto v = CommandLineManager::get().openglMode();
    if (v == "off") {
        _composited = false;
    } else if (v == "on") {
        _composited = true;
    }
    qDebug() << "composited:" << _composited;
}

CompositingManager::~CompositingManager() {
}

// Attempt to reuse mpv's code for detecting whether we want GLX or EGL (which
// is tricky to do because of hardware decoding concerns). This is not pretty,
// but quite effective and without having to duplicate too much GLX/EGL code.
static QString probeHwdecInterop()
{
  auto mpv = mpv::qt::Handle::FromRawHandle(mpv_create());
  if (!mpv)
    return "";
  mpv::qt::set_property(mpv, "hwdec-preload", "auto");
  // Actually creating a window is required. There is currently no way to keep
  // this window hidden or invisible.
  mpv::qt::set_property(mpv, "force-window", true);
  // As a mitigation, put the window in the top/right corner, and make it as
  // small as possible by forcing 1x1 size and removing window borders.
  mpv::qt::set_property(mpv, "geometry", "1x1+0+0");
  mpv::qt::set_property(mpv, "border", false);
  if (mpv_initialize(mpv) < 0)
    return "";
  return mpv::qt::get_property(mpv, "hwdec-interop").toString();
}

void CompositingManager::detectOpenGLEarly()
{
  // The putenv call must happen before Qt initializes its platform stuff.
  if (probeHwdecInterop() == "vaapi-egl") {
      qInfo() << "set QT_XCB_GL_INTERGRATION to xcb_egl";
      fprintf(stderr, "set QT_XCB_GL_INTERGRATION to xcb_egl\n");
      qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
  }
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

//FIXME: what about merge options from both config
PlayerOptionList CompositingManager::getProfile(const QString& name)
{
    auto localPath = QString("%1/%2/%3/%4.profile")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName())
        .arg(name);
    auto defaultPath = QString(":/resources/profiles/%1.profile").arg(name);
    auto oc = CommandLineManager::get().overrideConfig();

    PlayerOptionList ol;

    QList<QString> files = {oc, localPath, defaultPath};
    auto p = files.begin();
    while (p != files.end()) {
        QFileInfo fi(*p);
        if (fi.exists()) {
            qDebug() << "load" << fi.absoluteFilePath();
            QFile f(fi.absoluteFilePath());
            f.open(QIODevice::ReadOnly);
            QTextStream ts(&f);
            while (!ts.atEnd()) {
                auto l = ts.readLine().trimmed();
                if (l.isEmpty()) continue;

                auto kv = l.split("=");
                qDebug() << l << kv;
                if (kv.size() == 1) {
                    ol.push_back(qMakePair(kv[0], QString::fromUtf8("")));
                } else {
                    ol.push_back(qMakePair(kv[0], kv[1]));
                }
            }

            return ol;
        }
        ++p;
    }

    return ol;
}

PlayerOptionList CompositingManager::getBestProfile()
{
    QString profile_name = "default";
    switch (_platform) {
        case Platform::Alpha:
        case Platform::Mips:
            profile_name = _composited ? "composited" : "failsafe";
            break;

        case Platform::X86:
            profile_name = _composited ? "composited" : "default";
            break;
        case Platform::Unknown:
            break;
    }

    return getProfile(profile_name);
}

#undef C2Q
}

