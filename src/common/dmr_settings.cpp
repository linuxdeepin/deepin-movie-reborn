// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtCore>
#include "ddlog.h"
#include <QtGui>

#include <DStandardPaths>

#include "dmr_settings.h"
#include "compositing_manager.h"
#include "utils.h"
#include <qsettingbackend.h>

namespace dmr {
using namespace Dtk::Core;
Settings *Settings::m_pTheSettings = nullptr;
Settings &Settings::get()
{
    if (!m_pTheSettings) {
        qCDebug(appMovie) << "Creating new Settings instance";
        m_pTheSettings = new Settings;
    }
    return *m_pTheSettings;
}

Settings::Settings()
    : QObject(nullptr)
{
    qCDebug(appMovie) << "Initializing Settings";
    m_sConfigPath = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    m_sConfigPath += "/config.conf";
    qCInfo(appMovie) << "Loading settings from:" << m_sConfigPath;
    
    QSettingBackend *pBackend = new QSettingBackend(m_sConfigPath);
    qCDebug(appMovie) << "Created QSettingBackend for config path:" << m_sConfigPath;

    if(!CompositingManager::isMpvExists()) {
        qCDebug(appMovie) << "MPV not found. Loading GStreamer settings configuration from :/resources/data/GstSettings.json";
        m_pSettings = DSettings::fromJsonFile(":/resources/data/GstSettings.json");
    } else {
#if !defined (__x86_64__)
        qCDebug(appMovie) << "Non-x86_64 platform detected. Loading low effect settings from :/resources/data/lowEffectSettings.json";
        m_pSettings = DSettings::fromJsonFile(":/resources/data/lowEffectSettings.json");
#else
        if (CompositingManager::get().composited()) {
            qCDebug(appMovie) << "Composited environment detected. Loading full settings from :/resources/data/settings.json";
            m_pSettings = DSettings::fromJsonFile(":/resources/data/settings.json");
        } else {
            qCDebug(appMovie) << "Non-composited environment detected. Loading low effect settings from :/resources/data/lowEffectSettings.json";
            m_pSettings = DSettings::fromJsonFile(":/resources/data/lowEffectSettings.json");
        }
#endif
    }
    m_pSettings->setBackend(pBackend);
    qCDebug(appMovie) << "DSettings backend set.";

    auto normalizeOptionIndex = [this](const QString &key, int legacyValue = -1) {
        auto option = m_pSettings->option(key);
        if (!option)
            return 0;

        int value = m_pSettings->getOption(key).toInt();
        int normalizedValue = value;
        const QStringList items = option->data("items").toStringList();

        if (legacyValue >= 0 && normalizedValue == legacyValue)
            normalizedValue = 0;
        if (items.isEmpty() || normalizedValue < 0 || normalizedValue >= items.size())
            normalizedValue = 0;

        if (normalizedValue != value) {
            qCWarning(appMovie) << "Reset invalid setting index" << key << "from" << value << "to" << normalizedValue;
            m_pSettings->setOption(key, normalizedValue);
        }

        return normalizedValue;
    };

    // Older versions stored MPV as Effect index 2. Current settings use 0.
    normalizeOptionIndex("base.decode.Effect", 2);
    normalizeOptionIndex("base.decode.Videoout");
    normalizeOptionIndex("base.decode.Decodemode");

    connect(m_pSettings, &DSettings::valueChanged,
    [ = ](const QString & key, const QVariant & value) {
        qCDebug(appMovie) << "DSettings value changed signal received. Key:" << key << ", Value:" << value.toString();
        
        if (key.startsWith("shortcuts.")) {
            qCDebug(appMovie) << "Shortcut setting changed. Emitting shortcutsChanged signal.";
            emit shortcutsChanged(key, value);
        } else if (key.startsWith("base.play.playmode")) {
            qCDebug(appMovie) << "Play mode setting changed. Emitting defaultplaymodechanged signal.";
            emit defaultplaymodechanged(key, value);
        } else if (key.startsWith("base.decode.select")) {
            //设置解码模式
            qCInfo(appMovie) << "Decode select mode changed to:" << value.toInt();
            emit setDecodeModel(key, value);
            if (value.toInt() == 3) {
                qCDebug(appMovie) << "Custom decode mode selected. Checking groups and hardware decode family.";
                auto list = m_pSettings->groups();
                auto hwdecFamily = m_pSettings->option("base.decode.Decodemode");
            } else {
                //刷新解码模式
                qCDebug(appMovie) << "Non-custom decode mode selected. Refreshing decode mode and performing crash check.";
                emit refreshDecode();
                //崩溃检测
                crashCheck();
            }
        } else if (key.startsWith("base.decode.Effect")) {
            qCInfo(appMovie) << "Effect mode changed to:" << value.toInt();
            auto effectFamily = m_pSettings->option("base.decode.Effect");
            Q_UNUSED(effectFamily)
            int index = normalizeOptionIndex("base.decode.Effect", 2);
            if (index != value.toInt())
                return;
            auto voFamily = m_pSettings->option("base.decode.Videoout");
            qCDebug(appMovie) << "Effect mode index:" << index;

            if (index  == 1) {
                qCDebug(appMovie) << "Effect index is 1. Setting video output to OpenGL.";
                if (voFamily) {
                    voFamily->setData("items", QStringList() << "OpenGL");
                    auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                    if (decodeFamily) {
                        qCDebug(appMovie) << "Setting decode mode items for OpenGL effect.";
                        decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy");
                    }
                }
            } else if (index == 0) {
                if (voFamily)
                    voFamily->setData("items", QStringList() << "gpu"
                                                             << "vaapi"
                                                             << "vdpau"
                                                             << "xv"
                                                             << "x11");
            }
            emit baseChanged(key, value);
            qCDebug(appMovie) << "Emitted baseChanged for Effect mode.";
        } else if (key.startsWith("base.decode.Videoout")) {
            if (value.toInt() < 0) {
                qCWarning(appMovie) << "Invalid video output value:" << value.toInt() << ". Aborting update.";
                return;
            }
            qCInfo(appMovie) << "Video output changed to index:" << value.toInt();
            auto videoFamily = m_pSettings->option("base.decode.Videoout");
            if (!videoFamily) {
                qCWarning(appMovie) << "Video output family option not found!";
                return;
            }
            int voIndex = normalizeOptionIndex("base.decode.Videoout");
            if (voIndex != value.toInt())
                return;
            const QStringList voItems = videoFamily->data("items").toStringList();
            if (voItems.isEmpty()) {
                qCWarning(appMovie) << "Video output item list is empty.";
                return;
            }
            QString vo = voItems.at(voIndex);
            qCDebug(appMovie) << "Selected video output:" << vo;
            
            auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
            if (vo.contains("vaapi")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy");
                qCDebug(appMovie) << "Setting decode mode items for vaapi video output.";
            } else if (vo.contains("vdpau")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                qCDebug(appMovie) << "Setting decode mode items for vdpau video output.";
            } else if (vo.contains("xv") || vo.contains("x11")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                qCDebug(appMovie) << "Setting decode mode items for xv/x11 video output.";
            } else if (vo.contains("gpu")){
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi"
                                                                 << "vaapi-copy"
                                                                 << "vdpau"
                                                                 << "vdpau-copy"
                                                                 << "nvdec"
                                                                 << "nvdec-copy"
                                                                 << "rkmpp");
            } else if(vo.contains("opengl", Qt::CaseInsensitive)){
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi"
                                                                 << "vaapi-copy"
                                                                 << "vdpau"
                                                                 << "vdpau-copy"
                                                                 << "omx"
                                                                 << "omx-copy");
            }
            emit baseChanged(key, value);
            qCDebug(appMovie) << "Emitted baseChanged for Videoout mode.";
        } else if (key.startsWith("base.play.hwaccel")) {
            qCDebug(appMovie) << "Hardware acceleration setting changed. Emitting hwaccelModeChanged signal.";
            emit hwaccelModeChanged(key, value);
        } else if (key.startsWith("base.play.mute")) {
            qCDebug(appMovie) << "Mute setting changed. Emitting baseMuteChanged signal.";
            emit baseMuteChanged(key, value);
        } else if (key.startsWith("base.play.showTimeFullScreen")) {
            qCDebug(appMovie) << "Show time fullscreen setting changed. Emitting showTimeFullScreenChanged signal.";
            emit showTimeFullScreenChanged(key, value);
        } else if (key.startsWith("base.")) {
            qCDebug(appMovie) << "Base setting changed. Emitting baseChanged signal.";
            emit baseChanged(key, value);
        } else if (key.startsWith("subtitle.")) {
            qCDebug(appMovie) << "Subtitle setting changed. Emitting subtitleChanged signal.";
            emit subtitleChanged(key, value);
        }
    });

    qCDebug(appMovie) << "Available settings keys after connections:" << m_pSettings->keys();

    QStringList playmodeDatabase;
    playmodeDatabase << tr("Order play")
                     << tr("Shuffle play")
                     << tr("Single play")
                     << tr("Single loop")
                     << tr("List loop");
    auto playmodeFamily = m_pSettings->option("base.play.playmode");
    if (playmodeFamily) {
        qCDebug(appMovie) << "Setting play mode options for playmodeFamily.";
        playmodeFamily->setData("items", playmodeDatabase);
    } else {
        qCWarning(appMovie) << "Play mode family option not found!";
    }

    QStringList hwaccelDatabase;
    hwaccelDatabase << tr("Auto")
                    << tr("Open")
                    << tr("Close");
    auto hwaccelFamily = m_pSettings->option("base.play.hwaccel");
    if (hwaccelFamily) {
        qCDebug(appMovie) << "Setting hardware acceleration options for hwaccelFamily.";
        hwaccelFamily->setData("items", hwaccelDatabase);
    } else {
        qCWarning(appMovie) << "Hardware acceleration family option not found!";
    }

    QFontDatabase fontDatabase;
    QPointer<DSettingsOption> fontFamliy = m_pSettings->option("subtitle.font.family");
    if(fontFamliy) {
        qCDebug(appMovie) << "Setting font family options for fontFamliy.";
        fontFamliy->setData("items", fontDatabase.families());
    } else {
        qCWarning(appMovie) << "Font family option not found!";
    }

    if (utils::isJjwGPUPresent() && utils::check_wayland_env()) {
        qCInfo(appMovie) << "Disabling mouse preview for JM GPU in Wayland environment";
        setInternalOption("mousepreview", false);
    }

    if (utils::check_wayland_env()) {
        qCDebug(appMovie) << "Wayland environment detected. Configuring video output and decode mode options.";
        auto voFamily = m_pSettings->option("base.decode.Videoout");
        if (voFamily) {
            qCDebug(appMovie) << "Setting video output to OpenGL for Wayland.";
            voFamily->setData("items", QStringList() << "OpenGL");
        } else {
            qCWarning(appMovie) << "Video output family option not found for Wayland configuration!";
        }
        auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
        if (decodeFamily) {
            qCDebug(appMovie) << "Setting decode mode items for Wayland.";
            decodeFamily->setData("items", QStringList() << "vaapi"
                                                         << "vaapi-copy"
                                                         << "vdpau"
                                                         << "vdpau-copy"
                                                         << "omx"
                                                         << "omx-copy");
        } else {
            qCWarning(appMovie) << "Decode mode family option not found for Wayland configuration!";
        }
    } else {
        qCDebug(appMovie) << "X11 environment detected. Configuring video output and decode mode options.";
        QStringList hwdecList, voList;
        hwdecList << "vaapi"
                  << "vaapi-copy"
                  << "vdpau"
                  << "vdpau-copy"
                  << "omx"
                  << "omx-copy";
        voList << "gpu"
               << "vaapi"
               << "vdpau"
               << "xv"
               << "x11";
        int effectIndex = normalizeOptionIndex("base.decode.Effect", 2);
        qCDebug(appMovie) << "X11: Current effectIndex:" << effectIndex;
        auto hwdecFamily = m_pSettings->option("base.decode.Decodemode");

        if (effectIndex == 1) {
            qCDebug(appMovie) << "X11: Effect index is 1. Setting high effect mode options.";
            auto voFamily = m_pSettings->option("base.decode.Videoout");
            if (voFamily) {
                qCDebug(appMovie) << "X11: Setting video output to OpenGL for high effect mode.";
                voFamily->setData("items", QStringList() << "OpenGL");
            } else {
                qCWarning(appMovie) << "X11: Video output family option not found for high effect mode!";
            }
            if (hwdecFamily) {
                qCDebug(appMovie) << "X11: Setting hardware decode items for high effect mode.";
                hwdecFamily->setData("items", hwdecList);
            } else {
                qCWarning(appMovie) << "X11: Hardware decode family option not found for high effect mode!";
            }
        } else {
            if(effectIndex == 0){
                auto voFamily = m_pSettings->option("base.decode.Videoout");
                if (voFamily)
                    voFamily->setData("items", QStringList() << "gpu"
                                                             << "vaapi"
                                                             << "vdpau"
                                                             << "xv"
                                                             << "x11");
            }

            auto videoFamily = m_pSettings->option("base.decode.Videoout");
            if (!videoFamily) {
                qCWarning(appMovie) << "X11: Video output family option not found!";
                return;
            }
            int voValue = normalizeOptionIndex("base.decode.Videoout");
            const QStringList voItems = videoFamily->data("items").toStringList();
            if (voItems.isEmpty()) {
                qCWarning(appMovie) << "X11: Video output item list is empty.";
                return;
            }
            QString vo = voItems.at(voValue);
            if (vo.contains("vaapi")) {
                auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi"
                                                                 << "vaapi-copy");
            } else if (vo.contains("vdpau")) {
                auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau"
                                                                 << "vdpau-copy");
            } else if (vo.contains("xv") || vo.contains("x11")) {
                auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau"
                                                                 << "vdpau-copy");
            } else {
                auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi"
                                                                 << "vaapi-copy"
                                                                 << "vdpau"
                                                                 << "vdpau-copy"
                                                                 << "nvdec"
                                                                 << "nvdec-copy"
                                                                 << "rkmpp");
            }
        }
    }
    qCInfo(appMovie) << "Settings initialization completed";
}

QString Settings::flag2key(Settings::Flag f)
{
    qCDebug(appMovie) << "Entering Settings::flag2key. Flag:" << static_cast<int>(f);
    switch (f) {
    case Settings::Flag::ShowTimeFullScreen:
        return "showTimeFullScreen";
    case Settings::Flag::ClearWhenQuit:
        qCDebug(appMovie) << "Flag ClearWhenQuit matched, returning \"emptylist\".";
        return "emptylist";
#ifndef __aarch64__
    case Settings::Flag::ShowThumbnailMode:
        qCDebug(appMovie) << "Flag ShowThumbnailMode matched, returning \"showInthumbnailmode\".";
        return "showInthumbnailmode";
#endif
    case Settings::Flag::ResumeFromLast:
        qCDebug(appMovie) << "Flag ResumeFromLast matched, returning \"resumelast\".";
        return "resumelast";
    case Settings::Flag::AutoSearchSimilar:
        qCDebug(appMovie) << "Flag AutoSearchSimilar matched, returning \"addsimilar\".";
        return "addsimilar";
    case Settings::Flag::PreviewOnMouseover:
        qCDebug(appMovie) << "Flag PreviewOnMouseover matched, returning \"mousepreview\".";
        return "mousepreview";
    case Settings::Flag::MultipleInstance:
        qCDebug(appMovie) << "Flag MultipleInstance matched, returning \"multiinstance\".";
        return "multiinstance";
    case Settings::Flag::PauseOnMinimize:
        qCDebug(appMovie) << "Flag PauseOnMinimize matched, returning \"pauseonmin\".";
        return "pauseonmin";
    }

    qCDebug(appMovie) << "No matching flag found, returning empty string.";
    return "";
}

bool Settings::isSet(Flag flag) const
{
    qCDebug(appMovie) << "Entering Settings::isSet. Flag:" << static_cast<int>(flag);
    bool bRet = false;
    QList<QPointer<DSettingsGroup> > listSubGroups = m_pSettings->group("base")->childGroups();
    qCDebug(appMovie) << "Retrieved child groups of \"base\". Count:" << listSubGroups.count();
    QList<QPointer<DSettingsGroup> >::iterator itor = std::find_if(listSubGroups.begin(), listSubGroups.end(), [ = ](GroupPtr grp) {
        qCDebug(appMovie) << "Checking group:" << grp->key() << "for \"base.play\".";
        return grp->key() == "base.play";
    });

    if (itor != listSubGroups.end()) {
        qCDebug(appMovie) << "Found \"base.play\" group.";
        QList<QPointer<DSettingsOption> > sub = (*itor)->childOptions();
        QString sKey = flag2key(flag);
        qCDebug(appMovie) << "Derived sKey from flag2key:" << sKey;

        QList<QPointer<DSettingsOption> >::iterator p = std::find_if(sub.begin(), sub.end(), [ = ](OptionPtr opt) {
            QString sOptKey = opt->key();
            sOptKey.remove(0, sOptKey.lastIndexOf('.') + 1);
            qCDebug(appMovie) << "Checking option:" << sOptKey << "against sKey:" << sKey;
            return sOptKey == sKey;
        });

        bRet = (p != sub.end() && (*p)->value().toBool());
        qCDebug(appMovie) << "Option found and value retrieved. Result bRet:" << bRet;
    } else {
        qCDebug(appMovie) << "\"base.play\" group not found.";
    }
    qCDebug(appMovie) << "Exiting Settings::isSet. Returning:" << bRet;
    return bRet;
}

QStringList Settings::commonPlayableProtocols() const
{
    qCDebug(appMovie) << "Entering Settings::commonPlayableProtocols.";
    //from mpv and combined with stream media protocols
    return {
        "http", "https", "bd", "ytdl", "smb", "dvd", "dvdread", "tv", "pvr",
        "dvb", "cdda", "lavf", "av", "avdevice", "fd", "fdclose", "edl",
        "mf", "null", "memory", "hex", "rtmp", "rtsp", "hls", "mms", "rtp",
        "rtcp"
    };
}

bool Settings::iscommonPlayableProtocol(const QString &sScheme) const
{
    qCDebug(appMovie) << "Entering Settings::iscommonPlayableProtocol. Scheme:" << sScheme;
//    for (auto pro : commonPlayableProtocols()) {
//        if (pro == sScheme)
//            return true;
//    }
//    return false;

    QStringList list = commonPlayableProtocols();
    qCDebug(appMovie) << "Retrieved common playable protocols list. Count:" << list.count();
    bool result = std::any_of(list.begin(), list.end(), [&](QString & _pro) {
        return _pro == sScheme;
    });

    qCDebug(appMovie) << "Exiting Settings::iscommonPlayableProtocol. Result:" << result;
    return result;
}

QString Settings::screenshotLocation()
{
    qCDebug(appMovie) << "Entering Settings::screenshotLocation.";
    QString sSavePath = settings()->value("base.screenshot.location").toString();
    qCDebug(appMovie) << "Raw screenshot save path from settings:" << sSavePath;
    if (sSavePath.size() && sSavePath[0] == '~') {
        sSavePath.replace(0, 1, QDir::homePath());
        qCDebug(appMovie) << "Expanded screenshot save path (with home path):" << sSavePath;
    }

    if (!QFileInfo(sSavePath).exists()) {
        qCDebug(appMovie) << "Screenshot save path does not exist. Creating directory:" << sSavePath;
        QDir dir;
        dir.mkpath(sSavePath);
    } else {
        qCDebug(appMovie) << "Screenshot save path already exists:" << sSavePath;
    }

    qCDebug(appMovie) << "Exiting Settings::screenshotLocation. Returning:" << sSavePath;
    return sSavePath;
}

QString Settings::screenshotNameTemplate()
{
    qCDebug(appMovie) << "Entering Settings::screenshotNameTemplate.";
    QString strMovie = QObject::tr("Movie");
    QString path = screenshotLocation() + QDir::separator() + strMovie +
            QDateTime::currentDateTime().toString("yyyyMMddhhmmss") + QString(".jpg");
    qCDebug(appMovie) << "Generated screenshot name template path:" << path;
    qCDebug(appMovie) << "Exiting Settings::screenshotNameTemplate.";
    return path;
}

//cppcheck 单元测试使用
QString Settings::screenshotNameSeqTemplate()
{
    return tr("%1/Movie%2(%3).jpg").arg(screenshotLocation())
           .arg(QDateTime::currentDateTime().toString("yyyyMMddhhmmss"));
}

void Settings::onSetCrash()
{
    qCInfo(appMovie) << "Resetting crash state";
    settings()->setOption(QString("set.start.crash"), 0);
    settings()->sync();
    qCDebug(appMovie) << "Exiting Settings::onSetCrash. Crash state reset to 0.";
}

void Settings::setGeneralOption(const QString &sOpt, const QVariant &var)
{
    qCDebug(appMovie) << "Setting general option:" << sOpt << "=" << var.toString();
    settings()->setOption(QString("base.general.%1").arg(sOpt), var);
    settings()->sync();
    qCDebug(appMovie) << "Exiting Settings::setGeneralOption. Option set and synced.";
}

void Settings::crashCheck()
{
    //重置崩溃检测状态位
    qCInfo(appMovie) << "Setting crash check state";
    settings()->setOption(QString("set.start.crash"), 1);
    settings()->sync();
    qCDebug(appMovie) << "Exiting Settings::crashCheck. Crash state set to 1.";
}

QVariant Settings::generalOption(const QString &sOpt)
{
    return settings()->getOption(QString("base.general.%1").arg(sOpt));
}

QVariant Settings::internalOption(const QString &sOpt)
{
    return settings()->getOption(QString("base.play.%1").arg(sOpt));
}

void Settings::setInternalOption(const QString &sOpt, const QVariant &var)
{
    qCDebug(appMovie) << "Entering Settings::setInternalOption. Option:" << sOpt << ", Value:" << var.toString();
    settings()->setOption(QString("base.play.%1").arg(sOpt), var);
    settings()->sync();
    qCDebug(appMovie) << "Exiting Settings::setInternalOption. Option set and synced.";
}

QString Settings::forcedInterop()
{
    return internalOption("forced_interop").toString();
}

bool Settings::disableInterop()
{
    return internalOption("disable_interop").toBool();
}

}
