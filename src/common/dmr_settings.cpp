// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtCore>
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
        qDebug() << "Creating new Settings instance";
        m_pTheSettings = new Settings;
    }
    return *m_pTheSettings;
}

Settings::Settings()
    : QObject(nullptr)
{
    qDebug() << "Initializing Settings";
    m_sConfigPath = DStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    m_sConfigPath += "/config.conf";
    qInfo() << "Loading settings from:" << m_sConfigPath;
    
    QSettingBackend *pBackend = new QSettingBackend(m_sConfigPath);
    qDebug() << "Created QSettingBackend for config path:" << m_sConfigPath;

    if(!CompositingManager::isMpvExists()) {
        qDebug() << "MPV not found. Loading GStreamer settings configuration from :/resources/data/GstSettings.json";
        m_pSettings = DSettings::fromJsonFile(":/resources/data/GstSettings.json");
    } else {
#if !defined (__x86_64__)
        qDebug() << "Non-x86_64 platform detected. Loading low effect settings from :/resources/data/lowEffectSettings.json";
        m_pSettings = DSettings::fromJsonFile(":/resources/data/lowEffectSettings.json");
#else
        if (CompositingManager::get().composited()) {
            qDebug() << "Composited environment detected. Loading full settings from :/resources/data/settings.json";
            m_pSettings = DSettings::fromJsonFile(":/resources/data/settings.json");
        } else {
            qDebug() << "Non-composited environment detected. Loading low effect settings from :/resources/data/lowEffectSettings.json";
            m_pSettings = DSettings::fromJsonFile(":/resources/data/lowEffectSettings.json");
        }
#endif
    }
    m_pSettings->setBackend(pBackend);
    qDebug() << "DSettings backend set.";

    connect(m_pSettings, &DSettings::valueChanged,
    [ = ](const QString & key, const QVariant & value) {
        qDebug() << "DSettings value changed signal received. Key:" << key << ", Value:" << value.toString();
        
        if (key.startsWith("shortcuts.")) {
            qDebug() << "Shortcut setting changed. Emitting shortcutsChanged signal.";
            emit shortcutsChanged(key, value);
        } else if (key.startsWith("base.play.playmode")) {
            qDebug() << "Play mode setting changed. Emitting defaultplaymodechanged signal.";
            emit defaultplaymodechanged(key, value);
        } else if (key.startsWith("base.decode.select")) {
            //设置解码模式
            qInfo() << "Decode select mode changed to:" << value.toInt();
            emit setDecodeModel(key, value);
            if (value.toInt() == 3) {
                qDebug() << "Custom decode mode selected. Checking groups and hardware decode family.";
                auto list = m_pSettings->groups();
                auto hwdecFamily = m_pSettings->option("base.decode.Decodemode");
            } else {
                //刷新解码模式
                qDebug() << "Non-custom decode mode selected. Refreshing decode mode and performing crash check.";
                emit refreshDecode();
                //崩溃检测
                crashCheck();
            }
        } else if (key.startsWith("base.decode.Effect")) {
            qInfo() << "Effect mode changed to:" << value.toInt();
            auto effectFamily = m_pSettings->option("base.decode.Effect");
            int index = value.toInt();
            auto voFamily = m_pSettings->option("base.decode.Videoout");
            qDebug() << "Effect mode index:" << index;

            if (index  == 1) {
                qDebug() << "Effect index is 1. Setting video output to OpenGL.";
                if (voFamily) {
                    voFamily->setData("items", QStringList() << "OpenGL");
                    auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                    if (decodeFamily) {
                         qDebug() << "Setting decode mode items for OpenGL effect.";
                         decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy");
                    }
                }
            } else if (index == 2) {
                qDebug() << "Effect index is 2. Setting video output options and decode mode items.";
                if (voFamily) {
                    if (voFamily->value().toInt() == 0) {
                        auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
                        if (decodeFamily) {
                             qDebug() << "Video output is 0 and effect is 2. Clearing decode mode items.";
                             decodeFamily->setData("items", QStringList());
                        }
                    }
                    qDebug() << "Setting video output options for effect 2.";
                    voFamily->setData("items", QStringList() << "" << "gpu" << "vaapi" << "vdpau" << "xv" << "x11");
                }
            }
            emit baseChanged(key, value);
            qDebug() << "Emitted baseChanged for Effect mode.";
        } else if (key.startsWith("base.decode.Videoout")) {
            if (value.toInt() < 0) {
                qWarning() << "Invalid video output value:" << value.toInt() << ". Aborting update.";
                return;
            }
            qInfo() << "Video output changed to index:" << value.toInt();
            auto videoFamily = m_pSettings->option("base.decode.Videoout");
            QString vo = videoFamily.data()->data("items").toStringList().at(value.toInt());
            qDebug() << "Selected video output:" << vo;
            
            auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
            if (vo.contains("vaapi")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy");
                qDebug() << "Setting decode mode items for vaapi video output.";
            } else if (vo.contains("vdpau")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                qDebug() << "Setting decode mode items for vdpau video output.";
            } else if (vo.contains("xv") || vo.contains("x11")) {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                qDebug() << "Setting decode mode items for xv/x11 video output.";
            } else {
                if (decodeFamily)
                    decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy" << "nvdec" << "nvdec-copy" << "rkmpp");
                qDebug() << "Setting default decode mode items for other video output.";
            }
            emit baseChanged(key, value);
            qDebug() << "Emitted baseChanged for Videoout mode.";
        } else if (key.startsWith("base.play.hwaccel")) {
            qDebug() << "Hardware acceleration setting changed. Emitting hwaccelModeChanged signal.";
            emit hwaccelModeChanged(key, value);
        } else if (key.startsWith("base.play.mute")) {
            qDebug() << "Mute setting changed. Emitting baseMuteChanged signal.";
            emit baseMuteChanged(key, value);
        } else if (key.startsWith("base.")) {
            qDebug() << "Base setting changed. Emitting baseChanged signal.";
            emit baseChanged(key, value);
        } else if (key.startsWith("subtitle.")) {
            qDebug() << "Subtitle setting changed. Emitting subtitleChanged signal.";
            emit subtitleChanged(key, value);
        }
    });

    qDebug() << "Available settings keys after connections:" << m_pSettings->keys();

    QStringList playmodeDatabase;
    playmodeDatabase << tr("Order play")
                     << tr("Shuffle play")
                     << tr("Single play")
                     << tr("Single loop")
                     << tr("List loop");
    auto playmodeFamily = m_pSettings->option("base.play.playmode");
    if (playmodeFamily) {
        qDebug() << "Setting play mode options for playmodeFamily.";
        playmodeFamily->setData("items", playmodeDatabase);
    } else {
        qWarning() << "Play mode family option not found!";
    }

    QStringList hwaccelDatabase;
    hwaccelDatabase << tr("Auto")
                    << tr("Open")
                    << tr("Close");
    auto hwaccelFamily = m_pSettings->option("base.play.hwaccel");
    if (hwaccelFamily) {
        qDebug() << "Setting hardware acceleration options for hwaccelFamily.";
        hwaccelFamily->setData("items", hwaccelDatabase);
    } else {
        qWarning() << "Hardware acceleration family option not found!";
    }

    QFontDatabase fontDatabase;
    QPointer<DSettingsOption> fontFamliy = m_pSettings->option("subtitle.font.family");
    if(fontFamliy) {
        qDebug() << "Setting font family options for fontFamliy.";
        fontFamliy->setData("items", fontDatabase.families());
    } else {
        qWarning() << "Font family option not found!";
    }

    QFileInfo fi("/dev/mwv206_0");      //景嘉微显卡默认不勾选预览
    QFileInfo jmfi("/dev/jmgpu");
    qDebug() << "Checking for specific GPU devices: /dev/mwv206_0 exists:" << fi.exists() << ", /dev/jmgpu exists:" << jmfi.exists();

    if ((fi.exists() || jmfi.exists()) && utils::check_wayland_env()) {
        qInfo() << "Disabling mouse preview for JM GPU in Wayland environment";
        setInternalOption("mousepreview", false);
    }

    if (utils::check_wayland_env()) {
        qDebug() << "Wayland environment detected. Configuring video output and decode mode options.";
        auto voFamily = m_pSettings->option("base.decode.Videoout");
        if (voFamily) {
            qDebug() << "Setting video output to OpenGL for Wayland.";
            voFamily->setData("items", QStringList() << "OpenGL");
        } else {
            qWarning() << "Video output family option not found for Wayland configuration!";
        }
        auto decodeFamily = m_pSettings->option("base.decode.Decodemode");
        if (decodeFamily) {
            qDebug() << "Setting decode mode items for Wayland.";
            decodeFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy" << "nvdec" << "nvdec-copy" << "rkmpp");
        } else {
            qWarning() << "Decode mode family option not found for Wayland configuration!";
        }
    } else {
        qDebug() << "X11 environment detected. Configuring video output and decode mode options.";
        QStringList hwdecList, voList;
        hwdecList << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy" << "nvdec" << "nvdec-copy" << "rkmpp";
        voList << "gpu" << "vaapi" << "vdpau" << "xv" << "x11";
        int effectIndex = m_pSettings->getOption("base.decode.Effect").toInt();
        qDebug() << "X11: Current effectIndex:" << effectIndex;
        auto hwdecFamily = m_pSettings->option("base.decode.Decodemode");

        if (effectIndex == 1) {
            qDebug() << "X11: Effect index is 1. Setting high effect mode options.";
            auto voFamily = m_pSettings->option("base.decode.Videoout");
            if (voFamily) {
                qDebug() << "X11: Setting video output to OpenGL for high effect mode.";
                voFamily->setData("items", QStringList() << "OpenGL");
            } else {
                qWarning() << "X11: Video output family option not found for high effect mode!";
            }
            if (hwdecFamily) {
                qDebug() << "X11: Setting hardware decode items for high effect mode.";
                hwdecFamily->setData("items", hwdecList);
            } else {
                qWarning() << "X11: Hardware decode family option not found for high effect mode!";
            }
        } else {
            qDebug() << "X11: Effect index is not 1. Setting standard effect mode options.";
            auto voFamily = m_pSettings->option("base.decode.Videoout");
            if (voFamily) {
                qDebug() << "X11: Setting video output options for standard effect mode.";
                voFamily->setData("items", QStringList() << "" << "gpu" << "vaapi" << "vdpau" << "xv" << "x11");
            } else {
                qWarning() << "X11: Video output family option not found for standard effect mode!";
            }
            int voValue = m_pSettings->getOption("base.decode.Videoout").toInt();
            if (voValue > 0) {
                auto videoFamily = m_pSettings->option("base.decode.Videoout");
                QString vo = videoFamily.data()->data("items").toStringList().at(voValue);
                qDebug() << "X11: Configuring decode options for video output:" << vo;

                if (vo.contains("vaapi")) {
                    if (hwdecFamily)
                        hwdecFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy");
                    qDebug() << "X11: Setting decode mode items for vaapi video output.";
                } else if (vo.contains("vdpau")) {
                    if (hwdecFamily)
                        hwdecFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                    qDebug() << "X11: Setting decode mode items for vdpau video output.";
                } else if (vo.contains("xv") || vo.contains("x11")) {
                    if (hwdecFamily)
                        hwdecFamily->setData("items", QStringList() << "vdpau" << "vdpau-copy");
                    qDebug() << "X11: Setting decode mode items for xv/x11 video output.";
                } else {
                    if (hwdecFamily)
                        hwdecFamily->setData("items", QStringList() << "vaapi" << "vaapi-copy" << "vdpau" << "vdpau-copy" << "nvdec" << "nvdec-copy" << "rkmpp");
                    qDebug() << "X11: Setting default decode mode items for other video output.";
                }
            } else {
                qDebug() << "X11: Video output value is 0, skipping decode option configuration.";
            }
        }
    }
    qInfo() << "Settings initialization completed";
}

QString Settings::flag2key(Settings::Flag f)
{
    qDebug() << "Entering Settings::flag2key. Flag:" << static_cast<int>(f);
    switch (f) {
    case Settings::Flag::ClearWhenQuit:
        qDebug() << "Flag ClearWhenQuit matched, returning \"emptylist\".";
        return "emptylist";
#ifndef __aarch64__
    case Settings::Flag::ShowThumbnailMode:
        qDebug() << "Flag ShowThumbnailMode matched, returning \"showInthumbnailmode\".";
        return "showInthumbnailmode";
#endif
    case Settings::Flag::ResumeFromLast:
        qDebug() << "Flag ResumeFromLast matched, returning \"resumelast\".";
        return "resumelast";
    case Settings::Flag::AutoSearchSimilar:
        qDebug() << "Flag AutoSearchSimilar matched, returning \"addsimilar\".";
        return "addsimilar";
    case Settings::Flag::PreviewOnMouseover:
        qDebug() << "Flag PreviewOnMouseover matched, returning \"mousepreview\".";
        return "mousepreview";
    case Settings::Flag::MultipleInstance:
        qDebug() << "Flag MultipleInstance matched, returning \"multiinstance\".";
        return "multiinstance";
    case Settings::Flag::PauseOnMinimize:
        qDebug() << "Flag PauseOnMinimize matched, returning \"pauseonmin\".";
        return "pauseonmin";
    }

    qDebug() << "No matching flag found, returning empty string.";
    return "";
}

bool Settings::isSet(Flag flag) const
{
    qDebug() << "Entering Settings::isSet. Flag:" << static_cast<int>(flag);
    bool bRet = false;
    QList<QPointer<DSettingsGroup> > listSubGroups = m_pSettings->group("base")->childGroups();
    qDebug() << "Retrieved child groups of \"base\". Count:" << listSubGroups.count();
    QList<QPointer<DSettingsGroup> >::iterator itor = std::find_if(listSubGroups.begin(), listSubGroups.end(), [ = ](GroupPtr grp) {
        qDebug() << "Checking group:" << grp->key() << "for \"base.play\".";
        return grp->key() == "base.play";
    });

    if (itor != listSubGroups.end()) {
        qDebug() << "Found \"base.play\" group.";
        QList<QPointer<DSettingsOption> > sub = (*itor)->childOptions();
        QString sKey = flag2key(flag);
        qDebug() << "Derived sKey from flag2key:" << sKey;

        QList<QPointer<DSettingsOption> >::iterator p = std::find_if(sub.begin(), sub.end(), [ = ](OptionPtr opt) {
            QString sOptKey = opt->key();
            sOptKey.remove(0, sOptKey.lastIndexOf('.') + 1);
            qDebug() << "Checking option:" << sOptKey << "against sKey:" << sKey;
            return sOptKey == sKey;
        });

        bRet = (p != sub.end() && (*p)->value().toBool());
        qDebug() << "Option found and value retrieved. Result bRet:" << bRet;
    } else {
        qDebug() << "\"base.play\" group not found.";
    }
    qDebug() << "Exiting Settings::isSet. Returning:" << bRet;
    return bRet;
}

QStringList Settings::commonPlayableProtocols() const
{
    qDebug() << "Entering Settings::commonPlayableProtocols.";
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
    qDebug() << "Entering Settings::iscommonPlayableProtocol. Scheme:" << sScheme;
//    for (auto pro : commonPlayableProtocols()) {
//        if (pro == sScheme)
//            return true;
//    }
//    return false;

    QStringList list = commonPlayableProtocols();
    qDebug() << "Retrieved common playable protocols list. Count:" << list.count();
    bool result = std::any_of(list.begin(), list.end(), [&](QString & _pro) {
        return _pro == sScheme;
    });

    qDebug() << "Exiting Settings::iscommonPlayableProtocol. Result:" << result;
    return result;
}

QString Settings::screenshotLocation()
{
    qDebug() << "Entering Settings::screenshotLocation.";
    QString sSavePath = settings()->value("base.screenshot.location").toString();
    qDebug() << "Raw screenshot save path from settings:" << sSavePath;
    if (sSavePath.size() && sSavePath[0] == '~') {
        sSavePath.replace(0, 1, QDir::homePath());
        qDebug() << "Expanded screenshot save path (with home path):" << sSavePath;
    }

    if (!QFileInfo(sSavePath).exists()) {
        qDebug() << "Screenshot save path does not exist. Creating directory:" << sSavePath;
        QDir dir;
        dir.mkpath(sSavePath);
    } else {
        qDebug() << "Screenshot save path already exists:" << sSavePath;
    }

    qDebug() << "Exiting Settings::screenshotLocation. Returning:" << sSavePath;
    return sSavePath;
}

QString Settings::screenshotNameTemplate()
{
    qDebug() << "Entering Settings::screenshotNameTemplate.";
    QString strMovie = QObject::tr("Movie");
    QString path = screenshotLocation() + QDir::separator() + strMovie +
            QDateTime::currentDateTime().toString("yyyyMMddhhmmss") + QString(".jpg");
    qDebug() << "Generated screenshot name template path:" << path;
    qDebug() << "Exiting Settings::screenshotNameTemplate.";
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
    qInfo() << "Resetting crash state";
    settings()->setOption(QString("set.start.crash"), 0);
    settings()->sync();
    qDebug() << "Exiting Settings::onSetCrash. Crash state reset to 0.";
}

void Settings::setGeneralOption(const QString &sOpt, const QVariant &var)
{
    qDebug() << "Setting general option:" << sOpt << "=" << var.toString();
    settings()->setOption(QString("base.general.%1").arg(sOpt), var);
    settings()->sync();
    qDebug() << "Exiting Settings::setGeneralOption. Option set and synced.";
}

void Settings::crashCheck()
{
    //重置崩溃检测状态位
    qInfo() << "Setting crash check state";
    settings()->setOption(QString("set.start.crash"), 1);
    settings()->sync();
    qDebug() << "Exiting Settings::crashCheck. Crash state set to 1.";
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
    qDebug() << "Entering Settings::setInternalOption. Option:" << sOpt << ", Value:" << var.toString();
    settings()->setOption(QString("base.play.%1").arg(sOpt), var);
    settings()->sync();
    qDebug() << "Exiting Settings::setInternalOption. Option set and synced.";
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
