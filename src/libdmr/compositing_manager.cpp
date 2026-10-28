// Copyright (C) 2020 ~ 2026, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"
#include "ddlog.h"
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
                qCInfo(dmrLib) << QString("machine: %1").arg(machine.c_str());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
                QRegExp re("x86.*|i?86|ia64", Qt::CaseInsensitive);
                if (re.indexIn(C2Q(machine)) != -1) {
#else
                QRegularExpression re("x86.*|i?86|ia64", QRegularExpression::CaseInsensitiveOption);
                if (re.match(C2Q(machine)).hasMatch()) {
#endif
                    qCInfo(dmrLib) << "match x86";
                    _pf = Platform::X86;

                } else if (machine.find("alpha") != string::npos
                           || machine.find("sw_64") != string::npos) {
                    // shenwei
                    qCInfo(dmrLib) << "match shenwei";
                    _pf = Platform::Alpha;

                } else if (machine.find("mips") != string::npos
                           || machine.find("loongarch64") != string::npos) { // loongson
                    qCInfo(dmrLib) << "match loongson";
                    _pf = Platform::Mips;
                } else if (machine.find("aarch64") != string::npos) { // ARM64
                    qCInfo(dmrLib) << "match arm";
                    _pf = Platform::Arm64;
                }
            }
        } else {
            QString error = uname.readAllStandardError();
            qCWarning(dmrLib) << error;
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
    QStringList sList = dmr::utils::runPipeProcess("lspci -nk", "");
    foreach(QString readData, sList) {
        if(readData.contains("1002:699f") || readData.contains("1002:6987") || readData.contains("6766:3d02")) {
            if (!readData.isEmpty()) {
                qCInfo(dmrLib) << qPrintable("Detect 550 series, using vaapi. ") << readData;
                return true;
            }
        }
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
    qCDebug(dmrLib) << "Initializing CompositingManager";
    initMember();
    qCDebug(dmrLib) << "initMember() called.";
    bool isDriverLoaded = isDriverLoadedCorrectly();
    qCDebug(dmrLib) << "isDriverLoadedCorrectly() returned:" << isDriverLoaded;
    setProperty("directRendering", isDriverLoaded); //是否支持直接渲染
    qCInfo(dmrLib) << "Driver loaded status:" << isDriverLoaded;
    softDecodeCheck();   //检测是否是kunpeng920（是否走软解码）
    detectSpecialVoFromConfig(); // 检测特殊机型（仅决定渲染策略）
    qCDebug(dmrLib) << "softDecodeCheck() called.";

    // 检测是否为 AMD 550 系列显卡，若为则走vaapi
    if (!m_setSpecialControls) {
        qCDebug(dmrLib) << "m_setSpecialControls is false. Detecting 550 series.";
        m_setSpecialControls = detect550Series();
        qCInfo(dmrLib) << "Special controls set for 550 series:" << m_setSpecialControls;
    }

    bool hasSpecialDriver = false;
    qCDebug(dmrLib) << "Starting DRM card existence check for i915/arise/cx4/zx/cx.";
    for (int id = 0; id <= 10; id++) {
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) break;
        if (is_device_viable(id)) {
            qCDebug(dmrLib) << "Device" << id << "is viable. Checking for i915/arise/cx4/zx/cx/gb02 drivers.";
            vector<string> drivers = {"i915", "arise", "cx4", "zx", "cx", "gb02"};
            hasSpecialDriver = is_card_exists(id, drivers);
            qCDebug(dmrLib) << "is_card_exists for device" << id << "returned:" << hasSpecialDriver;
            break;
        }
    }
    if (hasSpecialDriver) {
        qCInfo(dmrLib) << "Detected i915/arise/cx4/zx/cx graphics";
    }
    m_bZXIntgraphics = hasSpecialDriver ? hasSpecialDriver : m_bZXIntgraphics;
    qCDebug(dmrLib) << "m_bZXIntgraphics set to:" << m_bZXIntgraphics;

#ifndef USE_TEST
    if (dmr::utils::check_wayland_env()) {
        qCInfo(dmrLib) << "Running in Wayland environment";
        _composited = true;
        qCDebug(dmrLib) << "Composited mode set to true for Wayland.";
        //读取配置
        m_pMpvConfig = new QMap<QString, QString>;
        qCDebug(dmrLib) << "New QMap for MPV config created (Wayland).";
        utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
        qCDebug(dmrLib) << "MPV config loaded from /etc/mpv/play.conf (Wayland).";
        if (m_pMpvConfig->contains("vo")) {
            qCDebug(dmrLib) << "MPV config contains 'vo' key (Wayland).";
            QString value = m_pMpvConfig->find("vo").value();
            qCDebug(dmrLib) << "'vo' value is:" << value;
            if ("libmpv" == value) {
                _composited = true;//libmpv只能走opengl
                qCInfo(dmrLib) << "Using libmpv, forcing composited mode";
            }
        }
        if (_platform == Platform::Arm64 && isDriverLoaded) {
            qCDebug(dmrLib) << "Platform is Arm64 and driver is loaded (Wayland).";
            m_bHasCard = true;
            qCInfo(dmrLib) << "Arm64 platform with loaded driver detected";
        }
        qCInfo(dmrLib) << "Composited mode:" << _composited;
        return;
    }
#endif

    QString settingPath = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    settingPath += "/config.conf";
    QFile file(settingPath);
    qCDebug(dmrLib) << "Checking config file for compositing settings:" << settingPath;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCDebug(dmrLib) << "Config file opened successfully. Reading contents.";
        QTextStream in(&file);
        QString line;
        int decodeSelect = -1;
        int effectValue = -1;
        while (!in.atEnd()) {
            if (decodeSelect != -1 && effectValue != -1)
                break;
            line = in.readLine();
            if (line.contains("[base.decode.select]")) {
                if (in.atEnd()) break;
                line = in.readLine();
                int idx = line.indexOf("value=");
                if (idx != -1) {
                    bool ok = false;
                    int val = line.mid(idx + 6).trimmed().toInt(&ok);
                    if (ok)
                        decodeSelect = val;
                    else
                        qCWarning(dmrLib) << "Invalid base.decode.select value in config";
                }
            } else if (line.contains("[base.decode.Effect]")) {
                if (in.atEnd()) break;
                line = in.readLine();
                int idx = line.indexOf("value=");
                if (idx != -1) {
                    bool ok = false;
                    int val = line.mid(idx + 6).trimmed().toInt(&ok);
                    if (ok)
                        effectValue = val;
                    else
                        qCWarning(dmrLib) << "Invalid base.decode.Effect value in config";
                }
            }
                }
        file.close();
        qCDebug(dmrLib) << "Parsed config: decodeSelect =" << decodeSelect << ", effectValue =" << effectValue;
        const bool isLegacyMpvEffect = (effectValue == 2);
        if (effectValue == 2) {
            qCWarning(dmrLib) << "Migrating legacy base.decode.Effect value 2 to 0";
            effectValue = 0;
        }
        // Effect only applies when in Customize mode (select == 3)
        if (decodeSelect == 3 && (effectValue == 1 || isLegacyMpvEffect)) {
            _composited = (effectValue == 1);
            qCDebug(dmrLib) << "Composited mode set from config file value:" << _composited;
            m_pMpvConfig = new QMap<QString, QString>;
            qCDebug(dmrLib) << "New QMap for MPV config created (from config file path).";
            utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
            return;
        }
    }

    //TODO: 临时处理方案
    _composited = true;
    qCDebug(dmrLib) << "_composited set to true (temporary fallback).";
#if defined (_MOVIE_USE_)
    qCDebug(dmrLib) << "_MOVIE_USE_ is defined.";
#ifdef DTKCORE_CLASS_DConfigFile
    qCDebug(dmrLib) << "DTKCORE_CLASS_DConfigFile is defined. Checking DConfig for compositedHandling.";
    //需要查询是否支持特殊特殊机型打开迷你模式，例如hw机型
    DConfig *dconfig = DConfig::create("org.deepin.movie","org.deepin.movie.minimode");
    if(dconfig && dconfig->isValid() && dconfig->keyList().contains("compositedHandling")){
        qCDebug(dmrLib) << "DConfig is valid and contains 'compositedHandling' key.";
        QString compositedHandling = dconfig->value("compositedHandling").toString();
        qCDebug(dmrLib) << "Composited handling value:" << compositedHandling;
        if (compositedHandling == "DisableComposited") {
            _composited = false;
            qCDebug(dmrLib) << "Composited mode set to false (DisableComposited).";
        } else if (compositedHandling == "EnableComposited") {
            _composited = true;
            qCDebug(dmrLib) << "Composited mode set to true (EnableComposited).";
        } else {
            qCDebug(dmrLib) << "Composited handling value is neither 'DisableComposited' nor 'EnableComposited'. Applying platform-specific logic.";
            if (_platform == Platform::X86) {
                qCDebug(dmrLib) << "Platform is X86.";
                if (m_bZXIntgraphics) {
                    _composited = false;
                    qCDebug(dmrLib) << "m_bZXIntgraphics is true, _composited set to false.";
                } else {
                    _composited = true;
                    qCDebug(dmrLib) << "m_bZXIntgraphics is false, _composited set to true.";
                }
            } else {
                if (_platform == Platform::Arm64 && isDriverLoaded)
                    m_bHasCard = true;
                _composited = false;
            }
        }
    } else {
        qCDebug(dmrLib) << "DConfig is invalid or does not contain 'compositedHandling' key. Applying platform-specific fallback.";
        if (_platform == Platform::X86) {
            qCDebug(dmrLib) << "Platform is X86 (fallback).";
            if (m_bZXIntgraphics) {
                _composited = false;
                qCDebug(dmrLib) << "m_bZXIntgraphics is true, _composited set to false (fallback).";
            } else {
                _composited = true;
                qCDebug(dmrLib) << "m_bZXIntgraphics is false, _composited set to true (fallback).";
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
    qCDebug(dmrLib) << "_MOVIE_USE_ is NOT defined. Skipping movie-specific logic.";
#endif

    //针对jm显卡适配
    qCDebug(dmrLib) << "Checking for jm GPU.";
    if (utils::isJjwGPUPresent()) {
        _composited = false;
        qCDebug(dmrLib) << "jm GPU or mwv206_0 detected, _composited set to false.";
    } else {
        qCDebug(dmrLib) << "jm GPU and mwv206_0 not detected.";
    }

    // xd分析：xd显卡不支持vaapi+glx, mpv会默认走glx，而mpv中glx只能搭配vdpau，所以不能走mpv，必须由deepin-movie来渲染。暂屏蔽
    //判断xd显卡不能通过opengl渲染
    // qCDebug(dmrLib) << "Checking for xd GPU.";
    // QDir innodir("/sys/bus/platform/drivers/inno-codec");
    // if ( innodir.exists()) {
    //    _composited = false;
    //    qCDebug(dmrLib) << "xd GPU (inno-codec) detected, _composited set to false.";
    // }

    //判断MT显卡不能通过opengl渲染
    qCDebug(dmrLib) << "Checking for MT GPU.";
    QFileInfo mtfi("/dev/mtgpu.0");
    if (mtfi.exists()) {
        qCDebug(dmrLib) << "MT GPU detected.";
        //判断是否安装核外驱动  因为mt显卡 不能通过opengl渲染
        QDir mtdir(QLibraryInfo::path(QLibraryInfo::LibrariesPath) +QDir::separator() +"musa");
        if ( mtdir.exists()) {
           _composited = false;
           qCDebug(dmrLib) << "MT GPU detected and musa driver exists, _composited set to false.";
        }
    }

    qCDebug(dmrLib) << "Checking for ljmcore driver.";
    if (QFile::exists("/sys/bus/pci/drivers/ljmcore")) {
        _composited = false;
        qCDebug(dmrLib) << "ljmcore driver detected, _composited set to false.";
    }

    //判断Fantasy显卡不能通过opengl渲染
    qCDebug(dmrLib) << "Checking for Fantasy GPU.";
    if (utils::isFantasyGPUPresent()) {
        _composited = false;
        qCDebug(dmrLib) << "Fantasy GPU detected, _composited set to false.";
    }

    if (m_setSpecialControls)
        _composited = false;

    //读取配置
    m_pMpvConfig = new QMap<QString, QString>;
    qCDebug(dmrLib) << "New QMap for MPV config created (final section).";
    utils::getPlayProperty("/etc/mpv/play.conf", m_pMpvConfig);
    qCDebug(dmrLib) << "MPV config loaded from /etc/mpv/play.conf (final section).";
    if (m_pMpvConfig->contains("vo")) {
        qCDebug(dmrLib) << "MPV config contains 'vo' key (final section).";
        QString value = m_pMpvConfig->find("vo").value();
        qCDebug(dmrLib) << "'vo' value is:" << value;
        if ("libmpv" == value) {
            _composited = true;//libmpv只能走opengl
            qCInfo(dmrLib) << "Using libmpv, forcing composited mode (final section)";
        }
    } else {
        qCDebug(dmrLib) << "MPV config does not contain 'vo' key (final section).";
    }
    //单元测试
#ifdef USE_TEST
    qCDebug(dmrLib) << "USE_TEST is defined. Loading MPV config for test.";
    utils::getPlayProperty("/data/source/deepin-movie-reborn/movie/play.conf", m_pMpvConfig);
    qCDebug(dmrLib) << "MPV config loaded from /data/source/deepin-movie-reborn/movie/play.conf (test).";
    if (m_pMpvConfig->contains("vo")) {
        qCDebug(dmrLib) << "MPV config contains 'vo' key (test).";
        QString value = m_pMpvConfig->find("vo").value();
        qCDebug(dmrLib) << "'vo' value is:" << value;
        if ("libmpv" == value) {
            _composited = true;//libmpv只能走opengl
            qCInfo(dmrLib) << "Using libmpv, forcing composited mode (test)";
        } else {
            _composited = false;//libmpv只能走opengl
            qCDebug(dmrLib) << "Not using libmpv, _composited set to false (test).";
        }
    } else {
        qCDebug(dmrLib) << "MPV config does not contain 'vo' key (test).";
    }
#endif

#ifdef DTKCORE_CLASS_DConfigFile
    DConfig *dconfig1 = DConfig::create("org.deepin.movie","org.deepin.movie.power");
    if(dconfig1 && dconfig1->isValid() && dconfig1->keyList().contains("enablePower")){
        m_enablePower = dconfig1->value("enablePower").toInt();

        if (m_enablePower > 0 && dconfig1->keyList().contains("configKey") && dconfig1->keyList().contains("configValue")) {
            m_configKey = dconfig1->value("configKey").toString();
            m_configValue = dconfig1->value("configValue").toString();
        }
    }
    delete dconfig1;
#endif
    if(!isMpvExists())
    {
        _composited = true;
        qCDebug(dmrLib) << "MPV does not exist, _composited set to true.";
    } else {
        qCDebug(dmrLib) << "MPV exists.";
    }
    qCInfo(dmrLib) << __func__ << "Composited is " << _composited;
    qCDebug(dmrLib) << "Exiting CompositingManager constructor.";
}

CompositingManager::~CompositingManager()
{
    qCDebug(dmrLib) << "Entering ~CompositingManager()";
    delete m_pMpvConfig;
    m_pMpvConfig = nullptr;
    qCDebug(dmrLib) << "Exiting ~CompositingManager()";
}

#if !defined (__x86_64__)
bool CompositingManager::hascard()
{
    qCDebug(dmrLib) << "Entering CompositingManager::hascard()";
    bool result = m_bHasCard;
    qCDebug(dmrLib) << "Exiting CompositingManager::hascard() with result:" << result;
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
    qCDebug(dmrLib) << "Entering CompositingManager::runningOnVmwgfx()";
    static bool s_runningOnVmwgfx = false;
//    static bool s_checked = false;

//    if (!s_checked) {
    for (int id = 0; id <= 10; id++) {
        qCDebug(dmrLib) << "Checking /sys/class/drm/card" << id << "for Vmwgfx driver.";
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qCDebug(dmrLib) << "Card" << id << "does not exist. Breaking loop.";
            break;
        }
        if (is_device_viable(id)) {
            qCDebug(dmrLib) << "Device" << id << "is viable. Checking for 'vmwgfx' driver.";
            vector<string> drivers = {"vmwgfx"};
            s_runningOnVmwgfx = is_card_exists(id, drivers);
            break;
        }
//        }
    }

    qCDebug(dmrLib) << "Exiting CompositingManager::runningOnVmwgfx() with result:" << s_runningOnVmwgfx;
    return s_runningOnVmwgfx;
}

bool CompositingManager::isPadSystem()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isPadSystem()";
    bool result = false;
    qCDebug(dmrLib) << "Exiting CompositingManager::isPadSystem() with result:" << result;
    return result;
}

bool CompositingManager::isCanHwdec()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isCanHwdec()";
    bool result = m_bCanHwdec;
    qCDebug(dmrLib) << "Exiting CompositingManager::isCanHwdec() with result:" << result;
    return result;
}

void CompositingManager::setCanHwdec(bool bCanHwdec)
{
    qCInfo(dmrLib) << "Entering CompositingManager::setCanHwdec() with bCanHwdec:" << bCanHwdec;
    m_bCanHwdec = bCanHwdec;
}

bool CompositingManager::isMpvExists()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isMpvExists()";
    if (m_hasMpv) {
        qCDebug(dmrLib) << "MPV library already loaded. Returning true.";
        return true;
    }

    qCDebug(dmrLib) << "MPV library not loaded. Attempting to load libmpv.so.";
    m_hasMpv = SysUtils::libExist("libmpv.so");

    qCDebug(dmrLib) << "Exiting CompositingManager::isMpvExists() with result:" << m_hasMpv;
    return m_hasMpv;
}

bool CompositingManager::isZXIntgraphics() const
{
    qCDebug(dmrLib) << "Entering CompositingManager::isZXIntgraphics()";
    bool result = m_bZXIntgraphics;
    qCDebug(dmrLib) << "Exiting CompositingManager::isZXIntgraphics() with result:" << result;
    return result;
}

bool CompositingManager::runningOnNvidia()
{
    qCDebug(dmrLib) << "Entering CompositingManager::runningOnNvidia()";
    static bool s_runningOnNvidia = false;

    for (int id = 0; id <= 10; id++) {
        qCDebug(dmrLib) << "Checking /sys/class/drm/card" << id << "for Nvidia driver.";
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qCDebug(dmrLib) << "Card" << id << "does not exist. Breaking loop.";
            break;
        }
        if (is_device_viable(id)) {
            qCDebug(dmrLib) << "Device" << id << "is viable. Checking for 'nvidia' driver.";
            vector<string> drivers = {"nvidia"};
            s_runningOnNvidia = is_card_exists(id, drivers);
            break;
        }
    }

    qCDebug(dmrLib) << "Exiting CompositingManager::runningOnNvidia() with result:" << s_runningOnNvidia;
    return s_runningOnNvidia;
}

void CompositingManager::softDecodeCheck()
{
    qCDebug(dmrLib) << "Entering CompositingManager::softDecodeCheck()";
    //获取cpu型号
    qCDebug(dmrLib) << "Starting CPU model check via /proc/cpuinfo.";
    QFile cpuInfo("/proc/cpuinfo");
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        qCDebug(dmrLib) << "/proc/cpuinfo opened successfully.";
        QString line = cpuInfo.readLine();
        while (!cpuInfo.atEnd()) {
            line = cpuInfo.readLine();
            qCDebug(dmrLib) << "Reading line from /proc/cpuinfo:" << line.trimmed();
            QStringList listPara = line.split(":");
            if (listPara.size() < 2) {
                qCDebug(dmrLib) << "Skipping line due to insufficient parameters.";
                continue;
            }
            if (listPara.at(0).contains("model name")) {
                m_cpuModelName = listPara.at(1);
                qCInfo(dmrLib) << "CPU model detected:" << m_cpuModelName;
                break;
            }
        }
        cpuInfo.close();
        qCDebug(dmrLib) << "/proc/cpuinfo closed.";
    } else {
        qCWarning(dmrLib) << "Failed to open /proc/cpuinfo";
    }

    //获取设备名
    qCDebug(dmrLib) << "Starting board vendor check via /sys/class/dmi/id/board_vendor.";
    QFile board("/sys/class/dmi/id/board_vendor");
    if (board.open(QIODevice::ReadOnly)) {
        qCDebug(dmrLib) << "/sys/class/dmi/id/board_vendor opened successfully.";
        QString line = board.readLine();
        while (!board.atEnd()) {
            m_boardVendor = line;
            qCInfo(dmrLib) << "Board vendor detected:" << m_boardVendor;
            break;
        }
        board.close();
        qCDebug(dmrLib) << "/sys/class/dmi/id/board_vendor closed.";
    } else {
        qCWarning(dmrLib) << "Failed to open board vendor file";
    }

    if (m_cpuModelName.contains("KX-U6780A")) {
        qCDebug(dmrLib) << "CPU model contains KX-U6780A. Checking modalias.";
        QFile modaInfo("/sys/class/dmi/id/modalias");
        if (modaInfo.open(QIODevice::ReadOnly)) {
            qCDebug(dmrLib) << "/sys/class/dmi/id/modalias opened successfully.";
            QString data = modaInfo.readAll();
            QStringList modaList = data.split(":");
            qCDebug(dmrLib) << "Modalias data read:" << data.trimmed();
            if (modaList.size() >= 7) {
                qCDebug(dmrLib) << "Modalias list size is >= 7.";
                if (modaList[6].contains("M630Z")) {
                    m_bOnlySoftDecode = true;
                    qCInfo(dmrLib) << "M630Z detected, enabling soft decode only";
                }
            }
            modaInfo.close();
            qCDebug(dmrLib) << "/sys/class/dmi/id/modalias closed.";
        } else {
            qCWarning(dmrLib) << "Failed to open modalias file";
            qCDebug(dmrLib) << "Failed to open /sys/class/dmi/id/modalias.";
        }
    } else {
        qCDebug(dmrLib) << "CPU model does not contain KX-U6780A. Skipping modalias check.";
    }

    if ((runningOnNvidia() && m_boardVendor.contains("Sugon"))
            || m_cpuModelName.contains("Kunpeng 920")) {
        m_bOnlySoftDecode = true;
        qCInfo(dmrLib) << "NVIDIA with Sugon or Kunpeng 920 detected, enabling soft decode only";
    }
    if(m_boardVendor.toLower().contains("huawei")) {
        qCDebug(dmrLib) << "Board vendor contains Huawei.";
        m_bHasCard = true;
        qCInfo(dmrLib) << "Huawei board detected, setting hasCard to true";
        qCDebug(dmrLib) << "m_bHasCard set to true.";
    } else {
        qCDebug(dmrLib) << "Board vendor does not contain Huawei.";
    }

    m_setSpecialControls = m_boardVendor.contains("PHYTIUM");
    qCDebug(dmrLib) << "m_setSpecialControls set based on PHYTIUM vendor check:" << m_setSpecialControls;

    //判断N卡驱动版本
    qCDebug(dmrLib) << "Starting NVIDIA driver version check via /proc/driver/nvidia/version.";
    QFile nvidiaVersion("/proc/driver/nvidia/version");
    if (nvidiaVersion.open(QIODevice::ReadOnly)) {
        qCDebug(dmrLib) << "/proc/driver/nvidia/version opened successfully.";
        QString str = nvidiaVersion.readLine();
        int start = str.indexOf("Module");
        start += 6;
        QString version = str.mid(start, 6);
        qCDebug(dmrLib) << "Initial NVIDIA driver version string:" << str.trimmed() << ", parsed version:" << version.trimmed();
        while (version.left(1) == " ") {
            start++;
            version = str.mid(start, 6);
            qCDebug(dmrLib) << "Trimmed NVIDIA driver version (loop):" << version.trimmed();
        }
        qCInfo(dmrLib) << "NVIDIA driver version:" << version;
        if (version.toFloat() >= 460.39) {
            m_bOnlySoftDecode = true;
            qCInfo(dmrLib) << "NVIDIA driver version >= 460.39, enabling soft decode only";
        }
        nvidiaVersion.close();
    }
    qCDebug(dmrLib) << "Exiting CompositingManager::softDecodeCheck()";
}

bool CompositingManager::isOnlySoftDecode()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isOnlySoftDecode()";
    bool result = m_bOnlySoftDecode;
    qCDebug(dmrLib) << "Exiting CompositingManager::isOnlySoftDecode() with result:" << result;
    return result;
}

bool CompositingManager::isSpecialControls()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isSpecialControls()";
    bool result = m_setSpecialControls;
    qCDebug(dmrLib) << "Exiting CompositingManager::isSpecialControls() with result:" << result;
    return result;
}

void CompositingManager::detectSpecialVoFromConfig()
{
#ifdef DTKCORE_CLASS_DConfigFile
    DConfig *dconfig = DConfig::create("org.deepin.movie", "org.deepin.movie.playmode");
    if (dconfig && dconfig->isValid()) {
        const QStringList &keys = dconfig->keyList();
        if (keys.contains("IsSpecialVo") && dconfig->value("IsSpecialVo").toInt() == 1) {
            if (keys.contains("VoName")) {
                QString voName = dconfig->value("VoName").toString().trimmed();
                if (!voName.isEmpty()) {
                    m_bUseSpecialVo = true;
                    m_specialVoName = voName;
                    qCInfo(dmrLib) << "Detected special VO from dconfig, vo:" << m_specialVoName;
                }
            }
        }
    }
    delete dconfig;
#endif
}

bool CompositingManager::shouldUseSpecialVo()
{
    return m_bUseSpecialVo;
}

QString CompositingManager::getSpecialVoName()
{
    return m_specialVoName.isEmpty() ? QString("gpu") : m_specialVoName;
}

void CompositingManager::detectOpenGLEarly()
{
    qCDebug(dmrLib) << "Entering CompositingManager::detectOpenGLEarly()";
    static bool detect_run = false;

    if (detect_run) {
        qCDebug(dmrLib) << "detectOpenGLEarly() already run. Exiting.";
        return;
    }

    ///function probeHwdecInterop() always returns QString(""), this code was not used
//    auto probed = probeHwdecInterop();
//    qCInfo(dmrLib) << "probeHwdecInterop" << probed
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
    qCDebug(dmrLib) << "USE_DXCB is not defined. Checking GL integration for non-DXCB.";
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
        qCDebug(dmrLib) << "Running on Nvidia. Setting QT_XCB_GL_INTEGRATION to xcb_glx.";
        qputenv("QT_XCB_GL_INTEGRATION", "xcb_glx");
    } else if (!CompositingManager::runningOnVmwgfx()) {
#ifndef USE_TEST
        qCDebug(dmrLib) << "Not running on Vmwgfx. Checking XDG_SESSION_TYPE and WAYLAND_DISPLAY.";
        auto e = QProcessEnvironment::systemEnvironment();
        QString XDG_SESSION_TYPE = e.value(QStringLiteral("XDG_SESSION_TYPE"));
        QString WAYLAND_DISPLAY = e.value(QStringLiteral("WAYLAND_DISPLAY"));

        if (XDG_SESSION_TYPE != QLatin1String("wayland") &&
                !WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive)) {
            qCDebug(dmrLib) << "XDG_SESSION_TYPE is not wayland and WAYLAND_DISPLAY does not contain wayland. Setting QT_XCB_GL_INTEGRATION to xcb_egl.";
            qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
        } else {
            qCDebug(dmrLib) << "XDG_SESSION_TYPE is wayland or WAYLAND_DISPLAY contains wayland. Not setting QT_XCB_GL_INTEGRATION to xcb_egl.";
        }
#endif
    } else {
        qCDebug(dmrLib) << "Running on Vmwgfx. Not setting QT_XCB_GL_INTEGRATION.";
    }
#else
    qCDebug(dmrLib) << "USE_DXCB is defined. Checking _interopKind for DXCB.";
    if (_interopKind == INTEROP_VAAPI_EGL) {
        qCDebug(dmrLib) << "_interopKind is INTEROP_VAAPI_EGL. Setting _interopKind to INTEROP_VAAPI_GLX.";
        _interopKind = INTEROP_VAAPI_GLX;
    } else {
        qCDebug(dmrLib) << "_interopKind is not INTEROP_VAAPI_EGL.";
    }

#endif

    detect_run = true;
    qCDebug(dmrLib) << "Exiting CompositingManager::detectOpenGLEarly()";
}

void CompositingManager::detectPciID()
{
    qCDebug(dmrLib) << "Entering CompositingManager::detectPciID()";
    QProcess pcicheck;
    qCDebug(dmrLib) << "Starting lspci -vn process.";
    pcicheck.start("lspci", QStringList{"-vn"});
    if (pcicheck.waitForStarted() && pcicheck.waitForFinished()) {
        qCDebug(dmrLib) << "lspci -vn process started and finished successfully.";

        auto data = pcicheck.readAllStandardOutput();

        QString output(data.trimmed().constData());
        qCInfo(dmrLib) << "CompositingManager::detectPciID()" << output.split(QChar('\n')).count();

        QStringList outlist = output.split(QChar('\n'));
        foreach (QString line, outlist) {
//            qCInfo(dmrLib)<<"CompositingManager::detectPciID():"<<line;
            qCDebug(dmrLib) << "Processing line:" << line.trimmed();
            if (line.contains(QString("00:02.0"))) {
                qCDebug(dmrLib) << "Line contains 00:02.0.";
                if (line.contains(QString("8086")) && line.contains(QString("1912"))) {
                    qCInfo(dmrLib) << "CompositingManager::detectPciID():need to change to iHD";
                    qputenv("LIBVA_DRIVER_NAME", "iHD");
                    break;
                }
            }
        }
    } else {
        qCDebug(dmrLib) << "lspci -vn process failed to start or finish.";
    }
    qCDebug(dmrLib) << "Exiting CompositingManager::detectPciID()";
}

void CompositingManager::getMpvConfig(QMap<QString, QString> *&aimMap)
{
    qCDebug(dmrLib) << "Entering CompositingManager::getMpvConfig()";
    aimMap = nullptr;
    qCDebug(dmrLib) << "aimMap initialized to nullptr.";
    if (nullptr != m_pMpvConfig) {
        qCDebug(dmrLib) << "m_pMpvConfig is not nullptr. Assigning to aimMap.";
        aimMap = m_pMpvConfig;
    }
    qCDebug(dmrLib) << "Exiting CompositingManager::getMpvConfig()";
}

OpenGLInteropKind CompositingManager::interopKind()
{
    return _interopKind;
}

bool CompositingManager::isDriverLoadedCorrectly()
{
    qCDebug(dmrLib) << "Entering isDriverLoadedCorrectly()";
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
    qCDebug(dmrLib) << "Checking platform for Xorg log path.";
    if (_platform == Platform::Mips) {
        qCDebug(dmrLib) << "Platform is Mips. Searching for Xorg log files.";
        QDir logDir("/var/log/");
        QStringList filters;
        filters << "Xorg.*.log";
        QStringList xorglogs = logDir.entryList(filters, QDir::Files);
        if (xorglogs.isEmpty()) {
            qCWarning(dmrLib) << "No Xorg log files found";
            return false;
        }
        xorglog = xorglogs.last();
        qCDebug(dmrLib) << "Found Xorg log for Mips:" << xorglog;
    } else {
        qCDebug(dmrLib) << "Platform is not Mips. Constructing Xorg log path from primary screen.";
        xorglog = QString("/var/log/Xorg.%1.log").arg(QGuiApplication::primaryScreen()->name().split("-").last());
        qCDebug(dmrLib) << "Constructed Xorg log path:" << xorglog;
    }
    qCInfo(dmrLib) << "Checking Xorg log:" << xorglog;
    
    QFile f(xorglog);
    qCDebug(dmrLib) << "Attempting to open Xorg log file.";
    if (!f.open(QFile::ReadOnly)) {
        qCWarning(dmrLib) << "Failed to open Xorg log:" << xorglog;
        qCDebug(dmrLib) << "Exiting isDriverLoadedCorrectly() due to failed file open.";
        return false;
    }

    QTextStream ts(&f);
    qCDebug(dmrLib) << "Reading Xorg log file.";
    while (!ts.atEnd()) {
        QString ln = ts.readLine();
        qCDebug(dmrLib) << "Read line:" << ln;
        
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        if (aiglx_err.indexIn(ln) != -1) {
            qCInfo(dmrLib) << "found aiglx error";
            return false;
        }

        if (dri_ok.indexIn(ln) != -1) {
            qCInfo(dmrLib) << "dri enabled successfully";
            return true;
        }

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
        if (rendering.indexIn(ln.toLower()) != -1) {
            qCInfo(dmrLib) << "_loongarch dri enabled successfully";
            return true;
        }
#endif
        if (swrast.indexIn(ln) != -1) {
            qCInfo(dmrLib) << "swrast driver used";
            return false;
        }

        if (regZX.indexIn(ln) != -1 || regCX4.indexIn(ln) != -1 || arise.indexIn(ln) != -1) {
            m_bZXIntgraphics = true;
            qCDebug(dmrLib) << "ZX/CX4/Arise driver detected. m_bZXIntgraphics set to true.";
        }

        if (controller.indexIn(ln) != -1) {
            qCInfo(dmrLib) << ln;
            qCDebug(dmrLib) << "Controller detected. Returning true.";
            return true;
        }
#else
        if (aiglx_err.match(ln).hasMatch()) {
            qCInfo(dmrLib) << "found aiglx error";
            return false;
        }

        if (dri_ok.match(ln).hasMatch()) {
            qCInfo(dmrLib) << "dri enabled successfully";
            return true;
        }

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
        if (rendering.match(ln.toLower()).hasMatch()) {
            qCInfo(dmrLib) << "_loongarch dri enabled successfully";
            return true;
        }
#endif
        if (swrast.match(ln).hasMatch()) {
            qCInfo(dmrLib) << "swrast driver used";
            return false;
        }

        if (regZX.match(ln).hasMatch() || regCX4.match(ln).hasMatch() || arise.match(ln).hasMatch()) {
            m_bZXIntgraphics = true;
            qCDebug(dmrLib) << "ZX/CX4/Arise driver detected. m_bZXIntgraphics set to true.";
        }

        if (controller.match(ln).hasMatch()) {
            qCInfo(dmrLib) << ln;
            qCDebug(dmrLib) << "Controller detected. Returning true.";
            return true;
        }
#endif
    }
    f.close();
    qCDebug(dmrLib) << "Finished reading Xorg log file.";

#if defined(_loongarch) || defined(__loongarch__) || defined(__loongarch64)
    qCDebug(dmrLib) << "Loongarch defined. Returning false.";
    return false;
#endif
    qCDebug(dmrLib) << "No specific driver issues found. Returning true.";
    return true;
}

void CompositingManager::overrideCompositeMode(bool useCompositing)
{
    qCDebug(dmrLib) << "Entering CompositingManager::overrideCompositeMode() with useCompositing:" << useCompositing;
    if (_composited != useCompositing) {
        qCInfo(dmrLib) << "override composited = " << useCompositing;
        _composited = useCompositing;
    }
    qCDebug(dmrLib) << "Exiting CompositingManager::overrideCompositeMode()";
}

using namespace std;

bool CompositingManager::is_card_exists(int id, const vector<string> &drivers)
{
    qCDebug(dmrLib) << "Entering CompositingManager::is_card_exists() with id:" << id;
    char buf[1024] = {0};
    qCDebug(dmrLib) << "Constructing path for driver check using id:" << id;
    snprintf(buf, sizeof buf, "/sys/class/drm/card%d/device/driver", id);

    char buf2[1024] = {0};
    qCDebug(dmrLib) << "Attempting to readlink from:" << buf;
    if (readlink(buf, buf2, sizeof buf2) < 0) {
        qCDebug(dmrLib) << "readlink failed. Exiting is_card_exists() with result: false";
        return false;
    }
    qCDebug(dmrLib) << "readlink successful. Read:" << buf2;

    string driver = basename(buf2);
    qCInfo(dmrLib) << "drm driver " << driver.c_str();
    qCDebug(dmrLib) << "Extracted DRM driver:" << QString::fromStdString(driver);
    if (std::any_of(drivers.cbegin(), drivers.cend(), [ = ](string s) {return s == driver;})) {
        qCDebug(dmrLib) << "Driver found in list. Exiting is_card_exists() with result: true";
        return true;
    }

    return false;
}

bool CompositingManager::is_device_viable(int id)
{
    qCDebug(dmrLib) << "Entering CompositingManager::is_device_viable() with id:" << id;
    char path[128];
    snprintf(path, sizeof path, "/sys/class/drm/card%d", id);
    qCDebug(dmrLib) << "Checking path:" << path;
    if (access(path, F_OK) != 0) {
        qCDebug(dmrLib) << "Access to path failed (F_OK). Exiting is_device_viable() with result: false";
        return false;
    }
    qCDebug(dmrLib) << "Access to path successful (F_OK).";

    //OK, on shenwei, this file may have no read permission for group/other.
    char buf[512];
    snprintf(buf, sizeof buf, "%s/device/enable", path);
    qCDebug(dmrLib) << "Checking buffer path:" << buf;
    if (access(buf, R_OK) == 0) {
        qCDebug(dmrLib) << "Access to buffer path successful (R_OK). Opening file.";
        FILE *fp = fopen(buf, "r");
        if (!fp) {
            qCDebug(dmrLib) << "Failed to open file pointer. Exiting is_device_viable() with result: false";
            return false;
        }
        qCDebug(dmrLib) << "File pointer opened successfully.";

        int enabled = 0;
        int error = fscanf(fp, "%d", &enabled);
        if (error < 0) {
            qCInfo(dmrLib) << "someting error";
        }
        fclose(fp);
        qCDebug(dmrLib) << "File pointer closed.";

        // nouveau may write 2, others 1
        bool result = enabled > 0;
        qCDebug(dmrLib) << "Exiting is_device_viable() with result:" << result;
        return result;
    } else {
        qCDebug(dmrLib) << "Access to buffer path failed (R_OK). Exiting is_device_viable() with result: false";
    }

    qCDebug(dmrLib) << "Exiting is_device_viable() with result: false (default return).";
    return false;
}

QPair<QString, QString> CompositingManager::getEnablePowerConfig() const
{
    return QPair<QString, QString>(m_configKey, m_configValue);
}

int CompositingManager::enablePower() const
{
    return m_enablePower;
}

bool CompositingManager::isProprietaryDriver()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isProprietaryDriver()";
    for (int id = 0; id <= 10; id++) {
        qCDebug(dmrLib) << "Checking DRM card with id:" << id;
        if (!QFile::exists(QString("/sys/class/drm/card%1").arg(id))) {
            qCDebug(dmrLib) << "DRM card" << id << "does not exist. Breaking loop.";
            break;
        }
        qCDebug(dmrLib) << "DRM card" << id << "exists. Checking if device is viable.";
        if (is_device_viable(id)) {
            qCDebug(dmrLib) << "Device" << id << "is viable. Checking for proprietary drivers.";
            vector<string> drivers = {"nvidia", "fglrx", "vmwgfx", "hibmc-drm", "radeon", "i915", "amdgpu", "phytium_display"};
            return is_card_exists(id, drivers);
        }
    }

    qCDebug(dmrLib) << "No proprietary driver found after checking all cards. Exiting isProprietaryDriver() with result: false";
    return false;
}

void CompositingManager::initMember()
{
    qCDebug(dmrLib) << "Entering initMember()";
    m_pMpvConfig = nullptr;
    _platform = PlatformChecker().check();

    m_bZXIntgraphics = false;
    m_bHasCard = false;
    m_bUseSpecialVo = false;
    m_specialVoName.clear();
    qCDebug(dmrLib) << "Exiting initMember()";
}

//this is not accurate when proprietary driver used
bool CompositingManager::isDirectRendered()
{
    qCDebug(dmrLib) << "Entering CompositingManager::isDirectRendered()";
//避免klu 上产生xdriinfo的coredump
//    QProcess xdriinfo;
//    xdriinfo.start("xdriinfo driver 0");
//    if (xdriinfo.waitForStarted() && xdriinfo.waitForFinished()) {
//        QString drv = QString::fromUtf8(xdriinfo.readAllStandardOutput().trimmed().constData());
//        qCInfo(dmrLib) << "xdriinfo: " << drv;
//        return !drv.contains("not direct rendering capable");
//    }

    qCDebug(dmrLib) << "Exiting CompositingManager::isDirectRendered() with result: true";
    return true;
}

//FIXME: what about merge options from both config
PlayerOptionList CompositingManager::getProfile(const QString &name)
{
    qCDebug(dmrLib) << "Entering CompositingManager::getProfile() with name:" << name;
    auto localPath = QString("%1/%2/%3/%4.profile")
                     .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                     .arg(qApp->organizationName())
                     .arg(qApp->applicationName())
                     .arg(name);
    qCDebug(dmrLib) << "Local profile path:" << localPath;
    auto defaultPath = QString(":/resources/profiles/%1.profile").arg(name);
    qCDebug(dmrLib) << "Default profile path:" << defaultPath;
#ifdef _LIBDMR_
    qCDebug(dmrLib) << "_LIBDMR_ is defined.";
    QString oc;
#else
    qCDebug(dmrLib) << "_LIBDMR_ is not defined. Getting override config.";
    auto oc = CommandLineManager::get().overrideConfig();
    qCDebug(dmrLib) << "Override config:" << oc;
#endif

    PlayerOptionList ol;

    QList<QString> files = {oc, localPath, defaultPath};
    qCDebug(dmrLib) << "Files to check:" << files;
    auto p = files.begin();
    while (p != files.end()) {
        QFileInfo fi(*p);
        if (fi.exists()) {
            qCInfo(dmrLib) << "load" << fi.absoluteFilePath();
            QFile f(fi.absoluteFilePath());
            f.open(QIODevice::ReadOnly);
            qCDebug(dmrLib) << "File opened for read-only.";
            QTextStream ts(&f);
            qCDebug(dmrLib) << "Reading file content.";
            while (!ts.atEnd()) {
                auto l = ts.readLine().trimmed();
                if (l.isEmpty()) continue;

                auto kv = l.split("=");
                qCInfo(dmrLib) << l << kv;
                if (kv.size() == 1) {
                    qCDebug(dmrLib) << "Key-value pair size is 1. Pushing back key with empty string value.";
                    ol.push_back(qMakePair(kv[0], QString::fromUtf8("")));
                } else {
                    qCDebug(dmrLib) << "Key-value pair size is not 1. Pushing back key and value.";
                    ol.push_back(qMakePair(kv[0], kv[1]));
                }
            }
            f.close();

            return ol;
        } else {
            qCDebug(dmrLib) << "File does not exist:" << *p << ". Trying next file.";
        }
        ++p;
    }

    qCDebug(dmrLib) << "No profile found. Exiting getProfile() with empty options.";
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
