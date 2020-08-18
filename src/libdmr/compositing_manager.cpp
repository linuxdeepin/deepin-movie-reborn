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

#include "config.h"
#include "compositing_manager.h"
#ifndef _LIBDMR_
#include "options.h"
#endif

#include <iostream>
#include <unistd.h>
#include <QtCore>
#include <QtGui>
#include <QX11Info>
#include <QDBusInterface>

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glxext.h>
#undef Bool
#include <mpv/qthelper.hpp>

typedef const char *glXGetScreenDriver_t (Display *dpy, int scrNum);

static glXGetScreenDriver_t *GetScreenDriver;

//TODO: override by user setting

namespace dmr {
using namespace std;

static CompositingManager *_compManager = nullptr;

#define C2Q(cs) (QString::fromUtf8((cs).c_str()))

class PlatformChecker
{
public:
    Platform check()
    {
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
                } else if (machine.find("aarch64") != string::npos) { // ARM64
                    qDebug() << "match arm";
                    _pf = Platform::Arm64;
                }
            }
        }

        return _pf;
    }

private:
    Platform _pf {Platform::Unknown};
};


CompositingManager &CompositingManager::get()
{
    if (!_compManager) {
        _compManager = new CompositingManager();
    }

    return *_compManager;
}

//void compositingChanged(bool);

CompositingManager::CompositingManager()
{
    _hasCard = false;
    _platform = PlatformChecker().check();

    softDecodeCheck();   //检测是否是kunpeng920（是否走软解码）

    _composited = false;
    if (QGSettings::isSchemaInstalled("com.deepin.deepin-movie")) {
        QGSettings gsettings("com.deepin.deepin-movie", "/com/deepin/deepin-movie/");
        QString aa = gsettings.get("composited").toString();
        if ((gsettings.get("composited").toString() == "DisableComposited"
                || gsettings.get("composited").toString() == "EnableComposited")) {
            if (gsettings.keys().contains("composited")) {
                if (gsettings.get("composited").toString() == "DisableComposited") {
                    _composited = false;
                } else if (gsettings.get("composited").toString() == "EnableComposited") {
                    _composited = true;
                }
            }
        } else {
            if (QProcessEnvironment::systemEnvironment().value("SANDBOX") == "flatpak") {
                _composited = QFile::exists("/dev/dri/card0");
            } else if (isProprietaryDriver()) {
                _composited = true;
            } else if (isDriverLoadedCorrectly() || isDirectRendered()) {
#ifdef __aarch64__
                _composited = false;
                qDebug() << "__aarch64__  isDirectRendered";
                if (isDriverLoadedCorrectly()) {    //如果有独立显卡
                    _composited = true;
                    qDebug() << "__aarch64__  isDriverLoadedCorrectly";
                }
#elif defined (__mips__)
                _composited = false;
                qDebug() << "__mips__";
#else
                _composited = true;
                qDebug() << "__X86__";
#endif
            } else {
                GetScreenDriver = reinterpret_cast<glXGetScreenDriver_t *>(glXGetProcAddressARB (reinterpret_cast<const GLubyte *>("glXGetScreenDriver")));
                if (GetScreenDriver) {
                    const char *name = (*GetScreenDriver) (QX11Info::display(), QX11Info::appScreen());
                    qDebug() << "dri driver: " << name;
                    _composited = name != nullptr;
                    //        } else {
                    //            if (isDriverLoadedCorrectly() && isDirectRendered()) {
                    //                _composited = true;
                    //            }
                }
            }

#ifndef _LIBDMR_
            auto v = CommandLineManager::get().openglMode();
            if (v == "off") {
                _composited = false;
            } else if (v == "on") {
                _composited = true;
            }
#endif
        }
        qDebug() << "From gsetting, composition about opengl :" << gsettings.get("composited").toString();
    } else { /* if(gsettings.get("composited").toString() == "Default")*/
        if (QProcessEnvironment::systemEnvironment().value("SANDBOX") == "flatpak") {
            _composited = QFile::exists("/dev/dri/card0");
        } else if (isProprietaryDriver()) {
            _composited = true;
        } else if (isDriverLoadedCorrectly() || isDirectRendered()) {
            _composited = true;
        } else {
            GetScreenDriver = reinterpret_cast<glXGetScreenDriver_t *>(glXGetProcAddressARB (reinterpret_cast<const GLubyte *>("glXGetScreenDriver")));
            if (GetScreenDriver) {
                const char *name = (*GetScreenDriver) (QX11Info::display(), QX11Info::appScreen());
                qDebug() << "dri driver: " << name;
                _composited = name != nullptr;
                //        } else {
                //            if (isDriverLoadedCorrectly() && isDirectRendered()) {
                //                _composited = true;
                //            }
            }
        }

#ifndef _LIBDMR_
        auto v = CommandLineManager::get().openglMode();
        if (v == "off") {
            _composited = false;
        } else if (v == "on") {
            _composited = true;
        }
#endif
    }
#ifdef MWV206_0
    QFileInfo fi("/dev/mwv206_0"); //景嘉微显卡目前只支持vo=xv，等日后升级代码需要酌情修改。
    if (fi.exists()) {
        _composited = false;
    }
#endif
#ifdef __mips__
    bool bRet = QDBusInterface("com.deepin.wm", "/com/deepin/wm", "com.deepin.wm").property("compositingAllowSwitch").toBool();
    if (!bRet) {
        _composited = false;   //2020.3.19龙芯增加显卡需保留检测显卡方案
    }
#endif
    qDebug() << "composited:" << _composited;
    auto e = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

    if (XDG_SESSION_TYPE == QLatin1String("wayland") ||
            WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
        _composited = false;
    }
#if defined (__mips__) || defined (__aarch64__) || defined (__sw_64__)
    if (_composited) {
        _hasCard = _composited;
        _composited = false;
        qDebug() << "hasCard: " << _hasCard;
    }
#endif
    qDebug() << __func__ << "Composited is " << _composited;
}

CompositingManager::~CompositingManager()
{
}

bool CompositingManager::hascard()
{
    return _hasCard;
}

// Attempt to reuse mpv's code for detecting whether we want GLX or EGL (which
// is tricky to do because of hardware decoding concerns). This is not pretty,
// but quite effective and without having to duplicate too much GLX/EGL code.
static QString probeHwdecInterop()
{
//    auto mpv = mpv::qt::Handle::FromRawHandle(mpv_create());
//    if (!mpv)
//        return "";
//    mpv::qt::set_property(mpv, "hwdec-preload", "auto");
//    // Actually creating a window is required. There is currently no way to keep
//    // this window hidden or invisible.
//    mpv::qt::set_property(mpv, "force-window", true);
//    // As a mitigation, put the window in the top/right corner, and make it as
//    // small as possible by forcing 1x1 size and removing window borders.
//    mpv::qt::set_property(mpv, "geometry", "1x1+0+0");
//    mpv::qt::set_property(mpv, "border", false);
//    if (mpv_initialize(mpv) < 0)
//        return "";
//    // return "auto"
//    return mpv::qt::get_property(mpv, "gpu-hwdec-interop").toString();
    return QString("");
}

static OpenGLInteropKind _interopKind = OpenGLInteropKind::INTEROP_NONE;

bool CompositingManager::runningOnVmwgfx()
{
    static bool s_runningOnVmwgfx = false;
    static bool s_checked = false;

    if (!s_checked) {
        for (int id = 0; id <= 10; id++) {
            if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) break;
            if (is_device_viable(id)) {
                vector<string> drivers = {"vmwgfx"};
                s_runningOnVmwgfx = is_card_exists(id, drivers);
                break;
            }
        }
    }

    return s_runningOnVmwgfx;
}

bool CompositingManager::runningOnNvidia()
{
    static bool s_runningOnNvidia = false;

    for (int id = 0; id <= 10; id++) {
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) break;
        if (is_device_viable(id)) {
            vector<string> drivers = {"nvidia"};
            s_runningOnNvidia = is_card_exists(id, drivers);
            break;
        }
    }

    return s_runningOnNvidia;
}

void CompositingManager::softDecodeCheck(){
//暂时不合
//    QProcess uname;
//    char* data = (char*)malloc(100);
//    uname.start("cat /proc/cpuinfo");
//    if (uname.waitForStarted()) {
//        if (uname.waitForFinished()) {
//            while (uname.readLine(data,99)>0) {
//                QString strData(data);
//                QStringList listPara = strData.split(":");

//                if(listPara.size()<2)
//                {
//                    continue;
//                }

//                if(listPara.at(0).contains("model name")
//                   && listPara.at(1).contains("Kunpeng 920"))
//                {
//                    m_bOnlySoftDecode = true;
//                }
//            }
//        }
//    }
//    free(data);
    //浪潮 inspur softdecode
    QProcess inspur;
    inspur.start("cat /sys/class/dmi/id/board_vendor");
    if (inspur.waitForStarted() && inspur.waitForFinished()) {
        QString drv = QString::fromUtf8(inspur.readAllStandardOutput().trimmed().constData());
        qDebug() << "inspur check : " << drv;
        m_bOnlySoftDecode =  m_bOnlySoftDecode || drv.contains("Inspur");
    }
}

bool CompositingManager::isOnlySoftDecode(){
    return m_bOnlySoftDecode;
}

void CompositingManager::detectOpenGLEarly()
{
    static bool detect_run = false;

    if (detect_run) return;

    auto probed = probeHwdecInterop();
    qDebug() << "probeHwdecInterop" << probed
             << qgetenv("QT_XCB_GL_INTERGRATION");

    if (probed == "auto") {
        _interopKind = INTEROP_AUTO;
    } else if (probed == "vaapi-egl") {
        _interopKind = INTEROP_VAAPI_EGL;
    } else if (probed == "vaapi-glx") {
        _interopKind = INTEROP_VAAPI_GLX;
    } else if (probed == "vdpau-glx") {
        _interopKind = INTEROP_VDPAU_GLX;
    }

#ifndef USE_DXCB
    /*
     * see mpv/render_gl.h for more details, below is copied verbatim:
     *
     * - Intel/Linux: EGL is required, and also the native display resource needs
     *                to be provided (e.g. MPV_RENDER_PARAM_X11_DISPLAY for X11 and
     *                MPV_RENDER_PARAM_WL_DISPLAY for Wayland)
     * - nVidia/Linux: Both GLX and EGL should work (GLX is required if vdpau is
     *                 used, e.g. due to old drivers.)
     *
     * mpv hwdec is broken with vmwgfx and should use glx
     */
    if (CompositingManager::runningOnNvidia()) {
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
    } else if (!CompositingManager::runningOnVmwgfx()) {
        auto e = QProcessEnvironment::systemEnvironment();
        QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
        QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

        if (XDG_SESSION_TYPE != QLatin1String("wayland") &&
                !WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
        }
    }
#else
    if (_interopKind == INTEROP_VAAPI_EGL) {
        _interopKind = INTEROP_VAAPI_GLX;
    }

#endif

    detect_run = true;
}

void CompositingManager::detectPciID()
{
    QProcess pcicheck;
    pcicheck.start("lspci -vn");
    if (pcicheck.waitForStarted() && pcicheck.waitForFinished()) {

        auto data = pcicheck.readAllStandardOutput();

        QString output(data.trimmed().constData());
        qDebug() << "CompositingManager::detectPciID()" << output.split(QChar('\n')).count();

        QStringList outlist = output.split(QChar('\n'));
        foreach (QString line, outlist) {
//            qDebug()<<"CompositingManager::detectPciID():"<<line;
            if (line.contains(QString("00:02.0"))) {
                if (line.contains(QString("8086")) && line.contains(QString("1912"))) {
                    qDebug() << "CompositingManager::detectPciID():need to change to iHD";
                    qputenv("LIBVA_DRIVER_NAME", "iHD");
                    break;
                }
            }
        }
    }
}

OpenGLInteropKind CompositingManager::interopKind()
{
    return _interopKind;
}

bool CompositingManager::isDriverLoadedCorrectly()
{
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

void CompositingManager::overrideCompositeMode(bool useCompositing)
{
    if (_composited != useCompositing) {
        qInfo() << "override composited = " << useCompositing;
        _composited = useCompositing;
    }
}

using namespace std;

bool CompositingManager::is_card_exists(int id, const vector<string> &drivers)
{
    char buf[1024] = {0};
    snprintf(buf, sizeof buf, "/sys/class/drm/card%d/device/driver", id);

    char buf2[1024] = {0};
    if (readlink(buf, buf2, sizeof buf2) < 0) {
        return false;
    }

    string driver = basename(buf2);
    qDebug() << "drm driver " << driver.c_str();
    if (std::any_of(drivers.cbegin(), drivers.cend(), [ = ](string s) {
    return s == driver;
})) {
        return true;
    }

    return false;
}

bool CompositingManager::is_device_viable(int id)
{
    char path[128];
    snprintf(path, sizeof path, "/sys/class/drm/card%d", id);
    if (access(path, F_OK) != 0) {
        return false;
    }

    //OK, on shenwei, this file may have no read permission for group/other.
    char buf[512];
    snprintf(buf, sizeof buf, "%s/device/enable", path);
    if (access(buf, R_OK) == 0) {
        FILE *fp = fopen(buf, "r");
        if (!fp) {
            return false;
        }

        int enabled = 0;
        fscanf(fp, "%d", &enabled);
        fclose(fp);

        // nouveau may write 2, others 1
        return enabled > 0;
    }

    return false;
}

bool CompositingManager::isProprietaryDriver()
{
    for (int id = 0; id <= 10; id++) {
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) break;
        if (is_device_viable(id)) {
            vector<string> drivers = {"nvidia", "fglrx", "vmwgfx", "hibmc-drm", "radeon", "i915", "amdgpu"};
            return is_card_exists(id, drivers);
        }
    }

    return false;
}

//this is not accurate when proprietary driver used
bool CompositingManager::isDirectRendered()
{
    QProcess xdriinfo;
    xdriinfo.start("xdriinfo driver 0");
    if (xdriinfo.waitForStarted() && xdriinfo.waitForFinished()) {
        QString drv = QString::fromUtf8(xdriinfo.readAllStandardOutput().trimmed().constData());
        qDebug() << "xdriinfo: " << drv;
        return !drv.contains("not direct rendering capable");
    }

    return true;
}

//FIXME: what about merge options from both config
PlayerOptionList CompositingManager::getProfile(const QString &name)
{
    auto localPath = QString("%1/%2/%3/%4.profile")
                     .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                     .arg(qApp->organizationName())
                     .arg(qApp->applicationName())
                     .arg(name);
    auto defaultPath = QString(":/resources/profiles/%1.profile").arg(name);
#ifdef _LIBDMR_
    QString oc;
#else
    auto oc = CommandLineManager::get().overrideConfig();
#endif

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
    case Platform::Arm64:
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

