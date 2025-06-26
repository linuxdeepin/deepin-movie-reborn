// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "compositing_manager.h"
#include "utils.h"
#include "dmr_settings.h"
#ifndef _LIBDMR_
#include "options.h"
#endif
#ifdef DTKCORE_CLASS_DConfigFile
#include <DConfig>
#endif

#include <iostream>
#include <unistd.h>
#include <QtCore>
#include <QtGui>
#include <QDBusInterface>
#include <DStandardPaths>

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/glxext.h>
#undef Bool
#include "../vendor/qthelper.hpp"
#include "sysutils.h"

#include <QtGui/private/qtx11extras_p.h>
#include <QtGui/private/qtguiglobal_p.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QX11Info>
#else
#include <QtGui/private/qtx11extras_p.h>
#include <QtGui/private/qtguiglobal_p.h>
#endif



#define BUFFERSIZE 255

typedef const char *glXGetScreenDriver_t (Display *dpy, int scrNum);

static glXGetScreenDriver_t *GetScreenDriver;

//TODO: override by user setting

namespace dmr {
using namespace std;

static CompositingManager *_compManager = nullptr;
bool CompositingManager::m_bCanHwdec = true;
bool CompositingManager::m_hasMpv = false;

#define C2Q(cs) (QString::fromUtf8((cs).c_str()))

class PlatformChecker
{
public:
    PlatformChecker() {}
    Platform check()
    {
        QProcess uname;
        uname.setProgram("uname");
        uname.setArguments({"-m"});
        uname.start();
        if (uname.waitForStarted()) {
            if (uname.waitForFinished()) {
                auto data = uname.readAllStandardOutput();
                string machine(data.trimmed().constData());
                qInfo() << QString("machine: %1").arg(machine.c_str());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                QRegExp re("x86.*|i?86|ia64", Qt::CaseInsensitive);
                if (re.indexIn(C2Q(machine)) != -1) {
#else
                QRegularExpression re("x86.*|i?86|ia64", QRegularExpression::CaseInsensitiveOption);
                if (re.match(C2Q(machine)).hasMatch()) {
#endif
                    qInfo() << "match x86";
                    _pf = Platform::X86;

                } else if (machine.find("alpha") != string::npos
                           || machine.find("sw_64") != string::npos) {
                    // shenwei
                    qInfo() << "match shenwei";
                    _pf = Platform::Alpha;

                } else if (machine.find("mips") != string::npos
                           || machine.find("loongarch64") != string::npos) { // loongson
                    qInfo() << "match loongson";
                    _pf = Platform::Mips;
                } else if (machine.find("aarch64") != string::npos) { // ARM64
                    qInfo() << "match arm";
                    _pf = Platform::Arm64;
                }
            }
        } else {
            QString error = uname.readAllStandardError();
            qWarning() << error;
        }

        return _pf;
    }

private:
    Platform _pf {Platform::Unknown};
};

/**
   @brief 检测当前显卡是否为550系列显卡，若为则使用 hwdec=vaapi vo=vaapi
    1002:699f Lexa PRO [Radeon 540/540X/550/550X / RX 540X/550/550X]
    1002:6987 Lexa [Radeon 540X/550X/630 / RX 640 / E9171 MCM]

   @note 影响启动性能
 */
static bool detect550Series()
{
    QProcess pcicheck;
    pcicheck.start("bash -c \"lspci -nk | grep -i 'in use' -B 2 | grep -iE '1002:699f|1002:6987' \"");
    if (pcicheck.waitForFinished(1000)) {
        QByteArray readData = pcicheck.readAllStandardOutput();
        if (!readData.isEmpty()) {
            qInfo() << qPrintable("Detect 550 series, using vaapi. ") << readData;
            return true;
        }

        qInfo() << qPrintable("Detect NOT 550 series, using default.");
    } else {
        pcicheck.terminate();
        qWarning() << qPrintable("Detect 550 series, run lspci -n failed. ") << pcicheck.errorString();
    }

    return false;
}


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
    qDebug() << "Initializing CompositingManager";
    initMember();
    qDebug() << "initMember() called.";
    bool isDriverLoaded = isDriverLoadedCorrectly();
    qDebug() << "isDriverLoadedCorrectly() returned:" << isDriverLoaded;
    setProperty("directRendering", isDriverLoaded); //是否支持直接渲染
    qInfo() << "Driver loaded status:" << isDriverLoaded;
    softDecodeCheck();   //检测是否是kunpeng920（是否走软解码）
    qDebug() << "softDecodeCheck() called.";

    // 检测是否为 AMD 550 系列显卡，若为则走vaapi
    if (!m_setSpecialControls) {
        qDebug() << "m_setSpecialControls is false. Detecting 550 series.";
        m_setSpecialControls = detect550Series();
        qInfo() << "Special controls set for 550 series:" << m_setSpecialControls;
    }

    bool isI915 = false;
    qDebug() << "Starting DRM card existence check for i915.";
    for (int id = 0; id <= 10; id++) {
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) break;
        if (is_device_viable(id)) {
            qDebug() << "Device" << id << "is viable. Checking for i915/arise drivers.";
            vector<string> drivers = {"i915", "arise"};
            isI915 = is_card_exists(id, drivers);
            qDebug() << "is_card_exists for device" << id << "returned:" << isI915;
            break;
        }
    }
    if (isI915) {
        qInfo() << "Detected i915 graphics";
    }
    m_bZXIntgraphics = isI915 ? isI915 : m_bZXIntgraphics;
    qDebug() << "m_bZXIntgraphics set to:" << m_bZXIntgraphics;

    if (dmr::utils::check_wayland_env()) {
        qInfo() << "Running in Wayland environment";
        _composited = true;
        qDebug() << "Composited mode set to true for Wayland.";
        //读取配置
        m_pMpvConfig = new QMap<QString, QString>;
        qDebug() << "New QMap for MPV config created (Wayland).";
        utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
        qDebug() << "MPV config loaded from /etc/mpv/play.conf (Wayland).";
        if (m_pMpvConfig->contains("vo")) {
            qDebug() << "MPV config contains 'vo' key (Wayland).";
            QString value = m_pMpvConfig->find("vo").value();
            qDebug() << "'vo' value is:" << value;
            if ("libmpv" == value) {
                _composited = true;//libmpv只能走opengl
                qInfo() << "Using libmpv, forcing composited mode";
            }
        }
        if (_platform == Platform::Arm64 && isDriverLoaded) {
            qDebug() << "Platform is Arm64 and driver is loaded (Wayland).";
            m_bHasCard = true;
            qInfo() << "Arm64 platform with loaded driver detected";
        }
        qInfo() << "Composited mode:" << _composited;
        return;
    }

    QString settingPath = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    settingPath += "/config.conf";
    QFile file(settingPath);
    qDebug() << "Checking config file for compositing settings:" << settingPath;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Config file opened successfully. Reading contents.";
        QTextStream in(&file);
        QString line;
        while (!in.atEnd()) {
            line = in.readLine();
            if (line.contains("[base.decode.Effect]")) {
                qDebug() << "Line contains [base.decode.Effect]. Reading next line for value.";
                line = in.readLine();
                int index = line.indexOf("value=");
                if (index != -1) {
                    qDebug() << "'value=' found in line.";
                    QString value = line.mid(index + 6); // 6 is the length of "value="
                    value = value.trimmed(); // Remove leading and trailing whitespace
                    file.close();
                    qDebug() << "Config file closed after reading value. Parsed value:" << value;
                    if (value.toInt() != 0) {
                        _composited = value.toInt() == 1 ? true : false;
                        qDebug() << "Composited mode set from config file value:" << _composited;
                        m_pMpvConfig = new QMap<QString, QString>;
                        qDebug() << "New QMap for MPV config created (from config file path).";
                        utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
                        return;
                    }
                } else {
                    qDebug() << "'value=' not found in line after [base.decode.Effect].";
                }
            }
        }
    }
    file.close();
    qDebug() << "Config file explicitly closed (if not already).";

    //TODO: 临时处理方案
    _composited = true;
    qDebug() << "_composited set to true (temporary fallback).";
#if defined (_MOVIE_USE_)
    qDebug() << "_MOVIE_USE_ is defined.";
#ifdef DTKCORE_CLASS_DConfigFile
    qDebug() << "DTKCORE_CLASS_DConfigFile is defined. Checking DConfig for compositedHandling.";
    //需要查询是否支持特殊特殊机型打开迷你模式，例如hw机型
    DConfig *dconfig = DConfig::create("org.deepin.movie","org.deepin.movie.minimode");
    if(dconfig && dconfig->isValid() && dconfig->keyList().contains("compositedHandling")){
        qDebug() << "DConfig is valid and contains 'compositedHandling' key.";
        QString compositedHandling = dconfig->value("compositedHandling").toString();
        qDebug() << "Composited handling value:" << compositedHandling;
        if (compositedHandling == "DisableComposited") {
            _composited = false;
            qDebug() << "Composited mode set to false (DisableComposited).";
        } else if (compositedHandling == "EnableComposited") {
            _composited = true;
            qDebug() << "Composited mode set to true (EnableComposited).";
        } else {
            qDebug() << "Composited handling value is neither 'DisableComposited' nor 'EnableComposited'. Applying platform-specific logic.";
            if (_platform == Platform::X86) {
                qDebug() << "Platform is X86.";
                if (m_bZXIntgraphics) {
                    _composited = false;
                    qDebug() << "m_bZXIntgraphics is true, _composited set to false.";
                } else {
                    _composited = true;
                    qDebug() << "m_bZXIntgraphics is false, _composited set to true.";
                }
            } else {
                if (_platform == Platform::Arm64 && isDriverLoaded)
                    m_bHasCard = true;
                _composited = false;
            }
        }
    } else {
        qDebug() << "DConfig is invalid or does not contain 'compositedHandling' key. Applying platform-specific fallback.";
        if (_platform == Platform::X86) {
            qDebug() << "Platform is X86 (fallback).";
            if (m_bZXIntgraphics) {
                _composited = false;
                qDebug() << "m_bZXIntgraphics is true, _composited set to false (fallback).";
            } else {
                _composited = true;
                qDebug() << "m_bZXIntgraphics is false, _composited set to true (fallback).";
            }
        } else {
            if (_platform == Platform::Arm64 && isDriverLoaded)
                m_bHasCard = true;
            _composited = false;
        }
    }
#else
    if (_platform == Platform::X86) {
        if (m_bZXIntgraphics) {
            _composited = false;
        } else {
            _composited = true;
        }
    } else {
        if (_platform == Platform::Arm64 && isDriverLoaded)
            m_bHasCard = true;
        _composited = false;
    }
#endif
#else
    qDebug() << "_MOVIE_USE_ is NOT defined. Skipping movie-specific logic.";
#endif

    //针对jm显卡适配
    qDebug() << "Checking for jm GPU.";
    QFileInfo jmfi("/dev/jmgpu");
    QFileInfo fi("/dev/mwv206_0");
    if (jmfi.exists() || fi.exists()) {
        _composited = false;
        qDebug() << "jm GPU or mwv206_0 detected, _composited set to false.";
    } else {
        qDebug() << "jm GPU and mwv206_0 not detected.";
    }

    //判断xd显卡不能通过opengl渲染
    qDebug() << "Checking for xd GPU.";
    QDir innodir("/sys/bus/platform/drivers/inno-codec");
    if ( innodir.exists()) {
       _composited = false;
       qDebug() << "xd GPU (inno-codec) detected, _composited set to false.";
    }

    //判断MT显卡不能通过opengl渲染
    qDebug() << "Checking for MT GPU.";
    QFileInfo mtfi("/dev/mtgpu.0");
    if (mtfi.exists()) {
        qDebug() << "MT GPU detected.";
        //判断是否安装核外驱动  因为mt显卡 不能通过opengl渲染
        QDir mtdir(QLibraryInfo::location(QLibraryInfo::LibrariesPath) +QDir::separator() +"musa");
        if ( mtdir.exists()) {
           _composited = false;
           qDebug() << "MT GPU detected and musa driver exists, _composited set to false.";
        }
    }

    qDebug() << "Checking for ljmcore driver.";
    if (QFile::exists("/sys/bus/pci/drivers/ljmcore")) {
        _composited = false;
        qDebug() << "ljmcore driver detected, _composited set to false.";
    }

    //读取配置
    m_pMpvConfig = new QMap<QString, QString>;
    qDebug() << "New QMap for MPV config created (final section).";
    utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
    qDebug() << "MPV config loaded from /etc/mpv/play.conf (final section).";
    if (m_pMpvConfig->contains("vo")) {
        qDebug() << "MPV config contains 'vo' key (final section).";
        QString value = m_pMpvConfig->find("vo").value();
        qDebug() << "'vo' value is:" << value;
        if ("libmpv" == value) {
            _composited = true;//libmpv只能走opengl
            qInfo() << "Using libmpv, forcing composited mode (final section)";
        }
    } else {
        qDebug() << "MPV config does not contain 'vo' key (final section).";
    }
    //单元测试
#ifdef USE_TEST
    qDebug() << "USE_TEST is defined. Loading MPV config for test.";
    utils::getPlayProperty("/data/source/deepin-movie-reborn/movie/play.conf", m_pMpvConfig);
    qDebug() << "MPV config loaded from /data/source/deepin-movie-reborn/movie/play.conf (test).";
    if (m_pMpvConfig->contains("vo")) {
        qDebug() << "MPV config contains 'vo' key (test).";
        QString value = m_pMpvConfig->find("vo").value();
        qDebug() << "'vo' value is:" << value;
        if ("libmpv" == value) {
            _composited = true;//libmpv只能走opengl
            qInfo() << "Using libmpv, forcing composited mode (test)";
        } else {
            _composited = false;//libmpv只能走opengl
            qDebug() << "Not using libmpv, _composited set to false (test).";
        }
    } else {
        qDebug() << "MPV config does not contain 'vo' key (test).";
    }
#endif
    if(!isMpvExists()) {
        _composited = true;
        qDebug() << "MPV does not exist, _composited set to true.";
    } else {
        qDebug() << "MPV exists.";
    }
    qInfo() << __func__ << "Composited is " << _composited;
    qDebug() << "Exiting CompositingManager constructor.";
}

CompositingManager::~CompositingManager()
{
    qDebug() << "Entering ~CompositingManager()";
    delete m_pMpvConfig;
    m_pMpvConfig = nullptr;
    qDebug() << "Exiting ~CompositingManager()";
}

#if !defined (__x86_64__)
bool CompositingManager::hascard()
{
    qDebug() << "Entering CompositingManager::hascard()";
    bool result = m_bHasCard;
    qDebug() << "Exiting CompositingManager::hascard() with result:" << result;
    return result;
}
#endif

// Attempt to reuse mpv's code for detecting whether we want GLX or EGL (which
// is tricky to do because of hardware decoding concerns). This is not pretty,
// but quite effective and without having to duplicate too much GLX/EGL code.
/*static QString probeHwdecInterop()
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
}*/

static OpenGLInteropKind _interopKind = OpenGLInteropKind::INTEROP_NONE;

bool CompositingManager::runningOnVmwgfx()
{
    qDebug() << "Entering CompositingManager::runningOnVmwgfx()";
    static bool s_runningOnVmwgfx = false;
//    static bool s_checked = false;

//    if (!s_checked) {
    for (int id = 0; id <= 10; id++) {
        qDebug() << "Checking /sys/class/drm/card" << id << "for Vmwgfx driver.";
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qDebug() << "Card" << id << "does not exist. Breaking loop.";
            break;
        }
        if (is_device_viable(id)) {
            qDebug() << "Device" << id << "is viable. Checking for 'vmwgfx' driver.";
            vector<string> drivers = {"vmwgfx"};
            s_runningOnVmwgfx = is_card_exists(id, drivers);
            break;
        }
//        }
    }

    qDebug() << "Exiting CompositingManager::runningOnVmwgfx() with result:" << s_runningOnVmwgfx;
    return s_runningOnVmwgfx;
}

bool CompositingManager::isPadSystem()
{
    qDebug() << "Entering CompositingManager::isPadSystem()";
    bool result = false;
    qDebug() << "Exiting CompositingManager::isPadSystem() with result:" << result;
    return result;
}

bool CompositingManager::isCanHwdec()
{
    qDebug() << "Entering CompositingManager::isCanHwdec()";
    bool result = m_bCanHwdec;
    qDebug() << "Exiting CompositingManager::isCanHwdec() with result:" << result;
    return result;
}

void CompositingManager::setCanHwdec(bool bCanHwdec)
{
    qInfo() << "Entering CompositingManager::setCanHwdec() with bCanHwdec:" << bCanHwdec;
    m_bCanHwdec = bCanHwdec;
}

bool CompositingManager::isMpvExists()
{
    qDebug() << "Entering CompositingManager::isMpvExists()";
    if (m_hasMpv) {
        qDebug() << "MPV library already loaded. Returning true.";
        return true;
    }

    qDebug() << "MPV library not loaded. Attempting to load libmpv.so.";
    m_hasMpv = SysUtils::libExist("libmpv.so");

    qDebug() << "Exiting CompositingManager::isMpvExists() with result:" << m_hasMpv;
    return m_hasMpv;
}

bool CompositingManager::isZXIntgraphics() const
{
    qDebug() << "Entering CompositingManager::isZXIntgraphics()";
    bool result = m_bZXIntgraphics;
    qDebug() << "Exiting CompositingManager::isZXIntgraphics() with result:" << result;
    return result;
}

bool CompositingManager::runningOnNvidia()
{
    qDebug() << "Entering CompositingManager::runningOnNvidia()";
    static bool s_runningOnNvidia = false;

    for (int id = 0; id <= 10; id++) {
        qDebug() << "Checking /sys/class/drm/card" << id << "for Nvidia driver.";
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qDebug() << "Card" << id << "does not exist. Breaking loop.";
            break;
        }
        if (is_device_viable(id)) {
            qDebug() << "Device" << id << "is viable. Checking for 'nvidia' driver.";
            vector<string> drivers = {"nvidia"};
            s_runningOnNvidia = is_card_exists(id, drivers);
            break;
        }
    }

    qDebug() << "Exiting CompositingManager::runningOnNvidia() with result:" << s_runningOnNvidia;
    return s_runningOnNvidia;
}

void CompositingManager::softDecodeCheck()
{
    qDebug() << "Entering CompositingManager::softDecodeCheck()";
    //获取cpu型号
    qDebug() << "Starting CPU model check via /proc/cpuinfo.";
    QFile cpuInfo("/proc/cpuinfo");
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        qDebug() << "/proc/cpuinfo opened successfully.";
        QString line = cpuInfo.readLine();
        while (!cpuInfo.atEnd()) {
            line = cpuInfo.readLine();
            qDebug() << "Reading line from /proc/cpuinfo:" << line.trimmed();
            QStringList listPara = line.split(":");
            if (listPara.size() < 2) {
                qDebug() << "Skipping line due to insufficient parameters.";
                continue;
            }
            if (listPara.at(0).contains("model name")) {
                m_cpuModelName = listPara.at(1);
                qInfo() << "CPU model detected:" << m_cpuModelName;
                break;
            }
        }
        cpuInfo.close();
        qDebug() << "/proc/cpuinfo closed.";
    } else {
        qWarning() << "Failed to open /proc/cpuinfo";
    }

    //获取设备名
    qDebug() << "Starting board vendor check via /sys/class/dmi/id/board_vendor.";
    QFile board("/sys/class/dmi/id/board_vendor");
    if (board.open(QIODevice::ReadOnly)) {
        qDebug() << "/sys/class/dmi/id/board_vendor opened successfully.";
        QString line = board.readLine();
        while (!board.atEnd()) {
            m_boardVendor = line;
            qInfo() << "Board vendor detected:" << m_boardVendor;
            break;
        }
        board.close();
        qDebug() << "/sys/class/dmi/id/board_vendor closed.";
    } else {
        qWarning() << "Failed to open board vendor file";
    }

    if (m_cpuModelName.contains("KX-U6780A")) {
        qDebug() << "CPU model contains KX-U6780A. Checking modalias.";
        QFile modaInfo("/sys/class/dmi/id/modalias");
        if (modaInfo.open(QIODevice::ReadOnly)) {
            qDebug() << "/sys/class/dmi/id/modalias opened successfully.";
            QString data = modaInfo.readAll();
            QStringList modaList = data.split(":");
            qDebug() << "Modalias data read:" << data.trimmed();
            if (modaList.size() >= 7) {
                qDebug() << "Modalias list size is >= 7.";
                if (modaList[6].contains("M630Z")) {
                    m_bOnlySoftDecode = true;
                    qInfo() << "M630Z detected, enabling soft decode only";
                }
            }
            modaInfo.close();
            qDebug() << "/sys/class/dmi/id/modalias closed.";
        } else {
            qWarning() << "Failed to open modalias file";
            qDebug() << "Failed to open /sys/class/dmi/id/modalias.";
        }
    } else {
        qDebug() << "CPU model does not contain KX-U6780A. Skipping modalias check.";
    }

    if ((runningOnNvidia() && m_boardVendor.contains("Sugon"))
            || m_cpuModelName.contains("Kunpeng 920")) {
        m_bOnlySoftDecode = true;
        qInfo() << "NVIDIA with Sugon or Kunpeng 920 detected, enabling soft decode only";
    }
    if(m_boardVendor.toLower().contains("huawei")) {
        qDebug() << "Board vendor contains Huawei.";
        m_bHasCard = true;
        qInfo() << "Huawei board detected, setting hasCard to true";
        qDebug() << "m_bHasCard set to true.";
    } else {
        qDebug() << "Board vendor does not contain Huawei.";
    }

    m_setSpecialControls = m_boardVendor.contains("PHYTIUM");
    qDebug() << "m_setSpecialControls set based on PHYTIUM vendor check:" << m_setSpecialControls;

    //判断N卡驱动版本
    qDebug() << "Starting NVIDIA driver version check via /proc/driver/nvidia/version.";
    QFile nvidiaVersion("/proc/driver/nvidia/version");
    if (nvidiaVersion.open(QIODevice::ReadOnly)) {
        qDebug() << "/proc/driver/nvidia/version opened successfully.";
        QString str = nvidiaVersion.readLine();
        int start = str.indexOf("Module");
        start += 6;
        QString version = str.mid(start, 6);
        qDebug() << "Initial NVIDIA driver version string:" << str.trimmed() << ", parsed version:" << version.trimmed();
        while (version.left(1) == " ") {
            start++;
            version = str.mid(start, 6);
            qDebug() << "Trimmed NVIDIA driver version (loop):" << version.trimmed();
        }
        qInfo() << "NVIDIA driver version:" << version;
        if (version.toFloat() >= 460.39) {
            m_bOnlySoftDecode = true;
            qInfo() << "NVIDIA driver version >= 460.39, enabling soft decode only";
        }
        nvidiaVersion.close();
    }
    qDebug() << "Exiting CompositingManager::softDecodeCheck()";
}

bool CompositingManager::isOnlySoftDecode()
{
    qDebug() << "Entering CompositingManager::isOnlySoftDecode()";
    bool result = m_bOnlySoftDecode;
    qDebug() << "Exiting CompositingManager::isOnlySoftDecode() with result:" << result;
    return result;
}

bool CompositingManager::isSpecialControls()
{
    qDebug() << "Entering CompositingManager::isSpecialControls()";
    bool result = m_setSpecialControls;
    qDebug() << "Exiting CompositingManager::isSpecialControls() with result:" << result;
    return result;
}

void CompositingManager::detectOpenGLEarly()
{
    qDebug() << "Entering CompositingManager::detectOpenGLEarly()";
    static bool detect_run = false;

    if (detect_run) {
        qDebug() << "detectOpenGLEarly() already run. Exiting.";
        return;
    }

    ///function probeHwdecInterop() always returns QString(""), this code was not used
//    auto probed = probeHwdecInterop();
//    qInfo() << "probeHwdecInterop" << probed
//             << qgetenv("QT_XCB_GL_INTERGRATION");

//    if (probed == "auto") {
//        _interopKind = INTEROP_AUTO;
//    } else if (probed == "vaapi-egl") {
//        _interopKind = INTEROP_VAAPI_EGL;
//    } else if (probed == "vaapi-glx") {
//        _interopKind = INTEROP_VAAPI_GLX;
//    } else if (probed == "vdpau-glx") {
//        _interopKind = INTEROP_VDPAU_GLX;
//    }

#ifndef USE_DXCB
    qDebug() << "USE_DXCB is not defined. Checking GL integration for non-DXCB.";
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
        qDebug() << "Running on Nvidia. Setting QT_XCB_GL_INTEGRATION to xcb_glx.";
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
    } else if (!CompositingManager::runningOnVmwgfx()) {
        qDebug() << "Not running on Vmwgfx. Checking XDG_SESSION_TYPE and WAYLAND_DISPLAY.";
        auto e = QProcessEnvironment::systemEnvironment();
        QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
        QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

        if (XDG_SESSION_TYPE != QLatin1String("wayland") &&
                !WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            qDebug() << "XDG_SESSION_TYPE is not wayland and WAYLAND_DISPLAY does not contain wayland. Setting QT_XCB_GL_INTEGRATION to xcb_egl.";
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
        } else {
            qDebug() << "XDG_SESSION_TYPE is wayland or WAYLAND_DISPLAY contains wayland. Not setting QT_XCB_GL_INTEGRATION to xcb_egl.";
        }
    } else {
        qDebug() << "Running on Vmwgfx. Not setting QT_XCB_GL_INTEGRATION.";
    }
#else
    qDebug() << "USE_DXCB is defined. Checking _interopKind for DXCB.";
    if (_interopKind == INTEROP_VAAPI_EGL) {
        qDebug() << "_interopKind is INTEROP_VAAPI_EGL. Setting _interopKind to INTEROP_VAAPI_GLX.";
        _interopKind = INTEROP_VAAPI_GLX;
    } else {
        qDebug() << "_interopKind is not INTEROP_VAAPI_EGL.";
    }

#endif

    detect_run = true;
    qDebug() << "Exiting CompositingManager::detectOpenGLEarly()";
}

void CompositingManager::detectPciID()
{
    qDebug() << "Entering CompositingManager::detectPciID()";
    QProcess pcicheck;
    qDebug() << "Starting lspci -vn process.";
    pcicheck.start("lspci -vn");
    if (pcicheck.waitForStarted() && pcicheck.waitForFinished()) {
        qDebug() << "lspci -vn process started and finished successfully.";

        auto data = pcicheck.readAllStandardOutput();

        QString output(data.trimmed().constData());
        qInfo() << "CompositingManager::detectPciID()" << output.split(QChar('\n')).count();

        QStringList outlist = output.split(QChar('\n'));
        foreach (QString line, outlist) {
//            qInfo()<<"CompositingManager::detectPciID():"<<line;
            qDebug() << "Processing line:" << line.trimmed();
            if (line.contains(QString("00:02.0"))) {
                qDebug() << "Line contains 00:02.0.";
                if (line.contains(QString("8086")) && line.contains(QString("1912"))) {
                    qInfo() << "CompositingManager::detectPciID():need to change to iHD";
                    qputenv("LIBVA_DRIVER_NAME", "iHD");
                    break;
                }
            }
        }
    } else {
        qDebug() << "lspci -vn process failed to start or finish.";
    }
    qDebug() << "Exiting CompositingManager::detectPciID()";
}

void CompositingManager::getMpvConfig(QMap<QString, QString> *&aimMap)
{
    qDebug() << "Entering CompositingManager::getMpvConfig()";
    aimMap = nullptr;
    qDebug() << "aimMap initialized to nullptr.";
    if (nullptr != m_pMpvConfig) {
        qDebug() << "m_pMpvConfig is not nullptr. Assigning to aimMap.";
        aimMap = m_pMpvConfig;
    }
    qDebug() << "Exiting CompositingManager::getMpvConfig()";
}

OpenGLInteropKind CompositingManager::interopKind()
{
    return _interopKind;
}

bool CompositingManager::isDriverLoadedCorrectly()
{
    qDebug() << "Entering isDriverLoadedCorrectly()";
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    static QRegExp aiglx_err("\\(EE\\)\\s+AIGLX error");
    static QRegExp dri_ok("direct rendering: DRI\\d+ enabled");
    static QRegExp swrast("GLX: Initialized DRISWRAST");
    static QRegExp regZX("loading driver: zx");
    static QRegExp regCX4("loading driver: cx4");
    static QRegExp arise("loading driver: arise");
    static QRegExp controller("1ec8");
#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
    static QRegExp rendering("direct rendering");
#endif
#else
    static QRegularExpression aiglx_err("\\(EE\\)\\s+AIGLX error");
    static QRegularExpression dri_ok("direct rendering: DRI\\d+ enabled");
    static QRegularExpression swrast("GLX: Initialized DRISWRAST");
    static QRegularExpression regZX("loading driver: zx");
    static QRegularExpression regCX4("loading driver: cx4");
    static QRegularExpression arise("loading driver: arise");
    static QRegularExpression controller("1ec8");
#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
    static QRegularExpression rendering("direct rendering");
#endif
#endif

    QString xorglog;
    qDebug() << "Checking platform for Xorg log path.";
    if (_platform == Platform::Mips) {
        qDebug() << "Platform is Mips. Searching for Xorg log files.";
        QDir logDir("/var/log/");
        QStringList filters;
        filters << "Xorg.*.log";
        QStringList xorglogs = logDir.entryList(filters, QDir::Files);
        if (xorglogs.isEmpty()) {
            qWarning() << "No Xorg log files found";
            return false;
        }
        xorglog = xorglogs.last();
        qDebug() << "Found Xorg log for Mips:" << xorglog;
    } else {
        qDebug() << "Platform is not Mips. Constructing Xorg log path from primary screen.";
        xorglog = QString("/var/log/Xorg.%1.log").arg(QGuiApplication::primaryScreen()->name().split("-").last());
        qDebug() << "Constructed Xorg log path:" << xorglog;
    }
    qInfo() << "Checking Xorg log:" << xorglog;
    
    QFile f(xorglog);
    qDebug() << "Attempting to open Xorg log file.";
    if (!f.open(QFile::ReadOnly)) {
        qWarning() << "Failed to open Xorg log:" << xorglog;
        qDebug() << "Exiting isDriverLoadedCorrectly() due to failed file open.";
        return false;
    }

    QTextStream ts(&f);
    qDebug() << "Reading Xorg log file.";
    while (!ts.atEnd()) {
        QString ln = ts.readLine();
        qDebug() << "Read line:" << ln;
        
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (aiglx_err.indexIn(ln) != -1) {
            qInfo() << "found aiglx error";
            return false;
        }

        if (dri_ok.indexIn(ln) != -1) {
            qInfo() << "dri enabled successfully";
            return true;
        }

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
        if (rendering.indexIn(ln.toLower()) != -1) {
            qInfo() << "_loongarch dri enabled successfully";
            return true;
        }
#endif
        if (swrast.indexIn(ln) != -1) {
            qInfo() << "swrast driver used";
            return false;
        }

        if (regZX.indexIn(ln) != -1 || regCX4.indexIn(ln) != -1 || arise.indexIn(ln) != -1) {
            m_bZXIntgraphics = true;
            qDebug() << "ZX/CX4/Arise driver detected. m_bZXIntgraphics set to true.";
        }

        if (controller.indexIn(ln) != -1) {
            qInfo() << ln;
            qDebug() << "Controller detected. Returning true.";
            return true;
        }
#else
        if (aiglx_err.match(ln).hasMatch()) {
            qInfo() << "found aiglx error";
            return false;
        }

        if (dri_ok.match(ln).hasMatch()) {
            qInfo() << "dri enabled successfully";
            return true;
        }

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
        if (rendering.match(ln.toLower()).hasMatch()) {
            qInfo() << "_loongarch dri enabled successfully";
            return true;
        }
#endif
        if (swrast.match(ln).hasMatch()) {
            qInfo() << "swrast driver used";
            return false;
        }

        if (regZX.match(ln).hasMatch() || regCX4.match(ln).hasMatch() || arise.match(ln).hasMatch()) {
            m_bZXIntgraphics = true;
            qDebug() << "ZX/CX4/Arise driver detected. m_bZXIntgraphics set to true.";
        }

        if (controller.match(ln).hasMatch()) {
            qInfo() << ln;
            qDebug() << "Controller detected. Returning true.";
            return true;
        }
#endif
    }
    f.close();
    qDebug() << "Finished reading Xorg log file.";

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
    qDebug() << "Loongarch defined. Returning false.";
    return false;
#endif
    qDebug() << "No specific driver issues found. Returning true.";
    return true;
}

void CompositingManager::overrideCompositeMode(bool useCompositing)
{
    qDebug() << "Entering CompositingManager::overrideCompositeMode() with useCompositing:" << useCompositing;
    if (_composited != useCompositing) {
        qInfo() << "override composited = " << useCompositing;
        _composited = useCompositing;
    }
    qDebug() << "Exiting CompositingManager::overrideCompositeMode()";
}

using namespace std;

bool CompositingManager::is_card_exists(int id, const vector<string> &drivers)
{
    qDebug() << "Entering CompositingManager::is_card_exists() with id:" << id;
    char buf[1024] = {0};
    qDebug() << "Constructing path for driver check using id:" << id;
    snprintf(buf, sizeof buf, "/sys/class/drm/card%d/device/driver", id);

    char buf2[1024] = {0};
    qDebug() << "Attempting to readlink from:" << buf;
    if (readlink(buf, buf2, sizeof buf2) < 0) {
        qDebug() << "readlink failed. Exiting is_card_exists() with result: false";
        return false;
    }
    qDebug() << "readlink successful. Read:" << buf2;

    string driver = basename(buf2);
    qInfo() << "drm driver " << driver.c_str();
    qDebug() << "Extracted DRM driver:" << QString::fromStdString(driver);
    if (std::any_of(drivers.cbegin(), drivers.cend(), [ = ](string s) {return s == driver;})) {
        qDebug() << "Driver found in list. Exiting is_card_exists() with result: true";
        return true;
    }

    return false;
}

bool CompositingManager::is_device_viable(int id)
{
    qDebug() << "Entering CompositingManager::is_device_viable() with id:" << id;
    char path[128];
    snprintf(path, sizeof path, "/sys/class/drm/card%d", id);
    qDebug() << "Checking path:" << path;
    if (access(path, F_OK) != 0) {
        qDebug() << "Access to path failed (F_OK). Exiting is_device_viable() with result: false";
        return false;
    }
    qDebug() << "Access to path successful (F_OK).";

    //OK, on shenwei, this file may have no read permission for group/other.
    char buf[512];
    snprintf(buf, sizeof buf, "%s/device/enable", path);
    qDebug() << "Checking buffer path:" << buf;
    if (access(buf, R_OK) == 0) {
        qDebug() << "Access to buffer path successful (R_OK). Opening file.";
        FILE *fp = fopen(buf, "r");
        if (!fp) {
            qDebug() << "Failed to open file pointer. Exiting is_device_viable() with result: false";
            return false;
        }
        qDebug() << "File pointer opened successfully.";

        int enabled = 0;
        int error = fscanf(fp, "%d", &enabled);
        if (error < 0) {
            qInfo() << "someting error";
        }
        fclose(fp);
        qDebug() << "File pointer closed.";

        // nouveau may write 2, others 1
        bool result = enabled > 0;
        qDebug() << "Exiting is_device_viable() with result:" << result;
        return result;
    } else {
        qDebug() << "Access to buffer path failed (R_OK). Exiting is_device_viable() with result: false";
    }

    qDebug() << "Exiting is_device_viable() with result: false (default return).";
    return false;
}

bool CompositingManager::isProprietaryDriver()
{
    qDebug() << "Entering CompositingManager::isProprietaryDriver()";
    for (int id = 0; id <= 10; id++) {
        qDebug() << "Checking DRM card with id:" << id;
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qDebug() << "DRM card" << id << "does not exist. Breaking loop.";
            break;
        }
        qDebug() << "DRM card" << id << "exists. Checking if device is viable.";
        if (is_device_viable(id)) {
            qDebug() << "Device" << id << "is viable. Checking for proprietary drivers.";
            vector<string> drivers = {"nvidia", "fglrx", "vmwgfx", "hibmc-drm", "radeon", "i915", "amdgpu", "phytium_display"};
            return is_card_exists(id, drivers);
        }
    }

    qDebug() << "No proprietary driver found after checking all cards. Exiting isProprietaryDriver() with result: false";
    return false;
}

void CompositingManager::initMember()
{
    qDebug() << "Entering initMember()";
    m_pMpvConfig = nullptr;
    _platform = PlatformChecker().check();

    m_bZXIntgraphics = false;
    m_bHasCard = false;
    qDebug() << "Exiting initMember()";
}

//this is not accurate when proprietary driver used
bool CompositingManager::isDirectRendered()
{
    qDebug() << "Entering CompositingManager::isDirectRendered()";
//避免klu 上产生xdriinfo的coredump
//    QProcess xdriinfo;
//    xdriinfo.start("xdriinfo driver 0");
//    if (xdriinfo.waitForStarted() && xdriinfo.waitForFinished()) {
//        QString drv = QString::fromUtf8(xdriinfo.readAllStandardOutput().trimmed().constData());
//        qInfo() << "xdriinfo: " << drv;
//        return !drv.contains("not direct rendering capable");
//    }

    qDebug() << "Exiting CompositingManager::isDirectRendered() with result: true";
    return true;
}

//FIXME: what about merge options from both config
PlayerOptionList CompositingManager::getProfile(const QString &name)
{
    qDebug() << "Entering CompositingManager::getProfile() with name:" << name;
    auto localPath = QString("%1/%2/%3/%4.profile")
                     .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                     .arg(qApp->organizationName())
                     .arg(qApp->applicationName())
                     .arg(name);
    qDebug() << "Local profile path:" << localPath;
    auto defaultPath = QString(":/resources/profiles/%1.profile").arg(name);
    qDebug() << "Default profile path:" << defaultPath;
#ifdef _LIBDMR_
    qDebug() << "_LIBDMR_ is defined.";
    QString oc;
#else
    qDebug() << "_LIBDMR_ is not defined. Getting override config.";
    auto oc = CommandLineManager::get().overrideConfig();
    qDebug() << "Override config:" << oc;
#endif

    PlayerOptionList ol;

    QList<QString> files = {oc, localPath, defaultPath};
    qDebug() << "Files to check:" << files;
    auto p = files.begin();
    while (p != files.end()) {
        QFileInfo fi(*p);
        if (fi.exists()) {
            qInfo() << "load" << fi.absoluteFilePath();
            QFile f(fi.absoluteFilePath());
            f.open(QIODevice::ReadOnly);
            qDebug() << "File opened for read-only.";
            QTextStream ts(&f);
            qDebug() << "Reading file content.";
            while (!ts.atEnd()) {
                auto l = ts.readLine().trimmed();
                if (l.isEmpty()) continue;

                auto kv = l.split("=");
                qInfo() << l << kv;
                if (kv.size() == 1) {
                    qDebug() << "Key-value pair size is 1. Pushing back key with empty string value.";
                    ol.push_back(qMakePair(kv[0], QString::fromUtf8("")));
                } else {
                    qDebug() << "Key-value pair size is not 1. Pushing back key and value.";
                    ol.push_back(qMakePair(kv[0], kv[1]));
                }
            }
            f.close();

            return ol;
        } else {
            qDebug() << "File does not exist:" << *p << ". Trying next file.";
        }
        ++p;
    }

    qDebug() << "No profile found. Exiting getProfile() with empty options.";
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

