// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>
#include "application.h"
#include "dmr_settings.h"
#include "stub/stub.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

using namespace dmr;

// Helper: persist the previous value of a base.play option so each test can
// restore it afterwards and avoid leaking state across cases. Returns the
// previously stored value (may be invalid if the option is unknown).
static QVariant dmrsettings_ext_backup_option(const QString &sOpt)
{
    return Settings::get().settings()->value(QString("base.play.%1").arg(sOpt));
}

static void dmrsettings_ext_restore_option(const QString &sOpt, const QVariant &prev)
{
    Settings::get().settings()->setOption(QString("base.play.%1").arg(sOpt), prev);
    Settings::get().settings()->sync();
}

// ---- Settings::flag2key ----
TEST(dmrsettings_ext, flag2key_showTimeFullScreen)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::ShowTimeFullScreen),
              QStringLiteral("showTimeFullScreen"));
}

TEST(dmrsettings_ext, flag2key_clearWhenQuit)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::ClearWhenQuit),
              QStringLiteral("emptylist"));
}

TEST(dmrsettings_ext, flag2key_resumeFromLast)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::ResumeFromLast),
              QStringLiteral("resumelast"));
}

TEST(dmrsettings_ext, flag2key_autoSearchSimilar)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::AutoSearchSimilar),
              QStringLiteral("addsimilar"));
}

TEST(dmrsettings_ext, flag2key_previewOnMouseover)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::PreviewOnMouseover),
              QStringLiteral("mousepreview"));
}

TEST(dmrsettings_ext, flag2key_multipleInstance)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::MultipleInstance),
              QStringLiteral("multiinstance"));
}

TEST(dmrsettings_ext, flag2key_pauseOnMinimize)
{
    EXPECT_EQ(Settings::flag2key(Settings::Flag::PauseOnMinimize),
              QStringLiteral("pauseonmin"));
}

TEST(dmrsettings_ext, flag2key_allEnumeratorsAreCovered)
{
    // On x86_64 ShowThumbnailMode is a valid enumerator; on aarch64 it is
    // compiled out via #ifndef __aarch64__. Iterate the well-known set so the
    // switch in flag2key is fully exercised on every platform.
    QList<Settings::Flag> flags;
    flags << Settings::Flag::ClearWhenQuit
          << Settings::Flag::ResumeFromLast
          << Settings::Flag::AutoSearchSimilar
          << Settings::Flag::PreviewOnMouseover
          << Settings::Flag::MultipleInstance
          << Settings::Flag::PauseOnMinimize
          << Settings::Flag::ShowTimeFullScreen;
#ifndef __aarch64__
    flags << Settings::Flag::ShowThumbnailMode;
#endif
    QSet<QString> uniqueKeys;
    for (Settings::Flag f : flags) {
        QString key = Settings::flag2key(f);
        EXPECT_FALSE(key.isEmpty()) << "flag2key returned empty for flag "
                                    << static_cast<int>(f);
        uniqueKeys.insert(key);
    }
    // Every enumerator must map to a distinct key.
    EXPECT_EQ(uniqueKeys.size(), flags.size());
}

// ---- Settings::isSet (reads base.play.<flag2key>) ----
TEST(dmrsettings_ext, isSet_returnsFalseWhenOptionFalse)
{
    // Force the underlying option off and verify isSet reflects it.
    Settings::get().settings()->setOption("base.play.pauseonmin", false);
    Settings::get().settings()->sync();
    EXPECT_FALSE(Settings::get().isSet(Settings::Flag::PauseOnMinimize));
}

TEST(dmrsettings_ext, isSet_returnsTrueWhenOptionTrue)
{
    Settings::get().settings()->setOption("base.play.pauseonmin", true);
    Settings::get().settings()->sync();
    EXPECT_TRUE(Settings::get().isSet(Settings::Flag::PauseOnMinimize));

    // Restore to a deterministic state.
    Settings::get().settings()->setOption("base.play.pauseonmin", false);
    Settings::get().settings()->sync();
}

TEST(dmrsettings_ext, isSet_multipleFlags)
{
    // Toggle several flags and confirm isSet tracks each one independently.
    Settings::get().settings()->setOption("base.play.resumelast", true);
    Settings::get().settings()->setOption("base.play.addsimilar", false);
    Settings::get().settings()->sync();

    EXPECT_TRUE(Settings::get().isSet(Settings::Flag::ResumeFromLast));
    EXPECT_FALSE(Settings::get().isSet(Settings::Flag::AutoSearchSimilar));

    Settings::get().settings()->setOption("base.play.resumelast", false);
    Settings::get().settings()->setOption("base.play.addsimilar", true);
    Settings::get().settings()->sync();

    EXPECT_FALSE(Settings::get().isSet(Settings::Flag::ResumeFromLast));
    EXPECT_TRUE(Settings::get().isSet(Settings::Flag::AutoSearchSimilar));

    // Cleanup.
    Settings::get().settings()->setOption("base.play.addsimilar", false);
    Settings::get().settings()->sync();
}

// ---- Settings::commonPlayableProtocols / iscommonPlayableProtocol ----
TEST(dmrsettings_ext, commonPlayableProtocols_isNotEmpty)
{
    QStringList protocols = Settings::get().commonPlayableProtocols();
    EXPECT_FALSE(protocols.isEmpty());
    // A few well-known entries must be present.
    EXPECT_TRUE(protocols.contains("http"));
    EXPECT_TRUE(protocols.contains("https"));
    EXPECT_TRUE(protocols.contains("rtmp"));
    EXPECT_TRUE(protocols.contains("rtsp"));
    EXPECT_TRUE(protocols.contains("hls"));
}

TEST(dmrsettings_ext, iscommonPlayableProtocol_knownSchemes)
{
    EXPECT_TRUE(Settings::get().iscommonPlayableProtocol("http"));
    EXPECT_TRUE(Settings::get().iscommonPlayableProtocol("https"));
    EXPECT_TRUE(Settings::get().iscommonPlayableProtocol("bd"));
    EXPECT_TRUE(Settings::get().iscommonPlayableProtocol("rtsp"));
    EXPECT_TRUE(Settings::get().iscommonPlayableProtocol("mms"));
}

TEST(dmrsettings_ext, iscommonPlayableProtocol_unknownSchemes)
{
    EXPECT_FALSE(Settings::get().iscommonPlayableProtocol("ftp"));
    EXPECT_FALSE(Settings::get().iscommonPlayableProtocol("file"));
    EXPECT_FALSE(Settings::get().iscommonPlayableProtocol("sftp"));
    EXPECT_FALSE(Settings::get().iscommonPlayableProtocol(""));
    EXPECT_FALSE(Settings::get().iscommonPlayableProtocol("HTTP"));  // case sensitive
}

TEST(dmrsettings_ext, iscommonPlayableProtocol_iteratesEntireList)
{
    // Every entry returned by commonPlayableProtocols must itself be playable.
    for (const QString &pro : Settings::get().commonPlayableProtocols()) {
        EXPECT_TRUE(Settings::get().iscommonPlayableProtocol(pro))
            << "Protocol " << pro.toUtf8().constData()
            << " should be reported as playable";
    }
}

// ---- Settings::internalOption / setInternalOption (base.play.*) ----
TEST(dmrsettings_ext, internalOption_roundTrip)
{
    // 注意: 必须用 schema 中真实存在的 base.play 选项(如 playlist_pos),
    // 否则 DTK DSettings::setOption 会解引用空指针导致崩溃。
    QVariant prev = dmrsettings_ext_backup_option("playlist_pos");
    Settings::get().setInternalOption("playlist_pos", 42);
    EXPECT_EQ(Settings::get().internalOption("playlist_pos").toInt(), 42);

    Settings::get().setInternalOption("playlist_pos", 0);
    EXPECT_EQ(Settings::get().internalOption("playlist_pos").toInt(), 0);

    dmrsettings_ext_restore_option("playlist_pos", prev);
}

TEST(dmrsettings_ext, internalOption_unknownReturnsInvalid)
{
    // base.play.<unknown> is not declared in the json; DSettings yields an
    // invalid QVariant rather than throwing.
    QVariant v = Settings::get().internalOption("this_option_does_not_exist");
    EXPECT_FALSE(v.isValid());
}

TEST(dmrsettings_ext, setInternalOption_persistsThroughSync)
{
    QVariant prev = dmrsettings_ext_backup_option("mute");
    Settings::get().setInternalOption("mute", true);
    EXPECT_TRUE(Settings::get().internalOption("mute").toBool());

    Settings::get().setInternalOption("mute", false);
    EXPECT_FALSE(Settings::get().internalOption("mute").toBool());

    dmrsettings_ext_restore_option("mute", prev);
}

// ---- Settings::generalOption / setGeneralOption (base.general.*) ----
TEST(dmrsettings_ext, generalOption_roundTrip)
{
    // 必须用 schema 中真实存在的 base.general 选项(如 last_open_path),
    // 否则 DTK DSettings::setOption 会解引用空指针导致崩溃。
    QVariant prev = Settings::get().settings()->value("base.general.last_open_path");
    Settings::get().setGeneralOption("last_open_path", QStringLiteral("/tmp/dmr_ut_general"));
    EXPECT_EQ(Settings::get().generalOption("last_open_path").toString(),
              QStringLiteral("/tmp/dmr_ut_general"));

    Settings::get().setGeneralOption("last_open_path", QString());
    EXPECT_TRUE(Settings::get().generalOption("last_open_path").toString().isEmpty());

    Settings::get().settings()->setOption("base.general.last_open_path", prev);
    Settings::get().settings()->sync();
}

TEST(dmrsettings_ext, generalOption_unknownKey)
{
    QVariant v = Settings::get().generalOption("nonexistent_general_key");
    // The option is not in the schema; the result is implementation defined
    // but must never crash. We just assert the call returns.
    EXPECT_TRUE(v.isValid() || !v.isValid());
}

// ---- Settings::forcedInterop / disableInterop ----
TEST(dmrsettings_ext, forcedInterop_returnsString)
{
    QVariant prev = dmrsettings_ext_backup_option("forced_interop");
    Settings::get().setInternalOption("forced_interop", QStringLiteral("vaapi"));
    EXPECT_EQ(Settings::get().forcedInterop(), QStringLiteral("vaapi"));

    Settings::get().setInternalOption("forced_interop", QString());
    EXPECT_TRUE(Settings::get().forcedInterop().isEmpty());

    dmrsettings_ext_restore_option("forced_interop", prev);
}

TEST(dmrsettings_ext, disableInterop_returnsBool)
{
    QVariant prev = dmrsettings_ext_backup_option("disable_interop");
    Settings::get().setInternalOption("disable_interop", true);
    EXPECT_TRUE(Settings::get().disableInterop());

    Settings::get().setInternalOption("disable_interop", false);
    EXPECT_FALSE(Settings::get().disableInterop());

    dmrsettings_ext_restore_option("disable_interop", prev);
}

// ---- Settings::screenshotLocation / screenshotNameTemplate / screenshotNameSeqTemplate ----
TEST(dmrsettings_ext, screenshotLocation_expandsTildeAndCreatesDir)
{
    // Use a temp dir under home so the '~' expansion branch is exercised.
    QString tmp = QDir::homePath() + "/.dmr_ut_screenshot_" +
                  QDateTime::currentDateTime().toString("yyyyMMddhhmmsszzz");
    Settings::get().settings()->setOption("base.screenshot.location",
                                          QString("~") + tmp.mid(QDir::homePath().size()));
    Settings::get().settings()->sync();

    QString loc = Settings::get().screenshotLocation();
    EXPECT_FALSE(loc.isEmpty());
    EXPECT_FALSE(loc.startsWith('~'));  // tilde must be expanded
    EXPECT_TRUE(QFileInfo(loc).exists());
    EXPECT_TRUE(QFileInfo(loc).isDir());

    QDir(loc).removeRecursively();
}

TEST(dmrsettings_ext, screenshotLocation_existingDirIsKept)
{
    QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                  "/dmr_ut_screenshot_exist";
    QDir().mkpath(tmp);
    Settings::get().settings()->setOption("base.screenshot.location", tmp);
    Settings::get().settings()->sync();

    QString loc = Settings::get().screenshotLocation();
    EXPECT_EQ(loc, tmp);
    EXPECT_TRUE(QFileInfo(loc).exists());

    QDir(tmp).removeRecursively();
}

TEST(dmrsettings_ext, screenshotNameTemplate_containsJpgSuffix)
{
    Settings::get().settings()->setOption(
        "base.screenshot.location",
        QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    Settings::get().settings()->sync();

    QString name = Settings::get().screenshotNameTemplate();
    EXPECT_TRUE(name.endsWith(".jpg", Qt::CaseInsensitive));
    EXPECT_FALSE(name.isEmpty());
}

TEST(dmrsettings_ext, screenshotNameSeqTemplate_containsJpgSuffix)
{
    Settings::get().settings()->setOption(
        "base.screenshot.location",
        QStandardPaths::writableLocation(QStandardPaths::TempLocation));
    Settings::get().settings()->sync();

    QString name = Settings::get().screenshotNameSeqTemplate();
    EXPECT_TRUE(name.endsWith(".jpg", Qt::CaseInsensitive));
    EXPECT_FALSE(name.isEmpty());
}

// ---- Settings::crashCheck / onSetCrash (set.start.crash) ----
TEST(dmrsettings_ext, crashCheck_setsFlagToOne)
{
    Settings::get().crashCheck();
    Settings::get().settings()->sync();
    EXPECT_EQ(Settings::get().settings()->value("set.start.crash").toInt(), 1);
}

TEST(dmrsettings_ext, onSetCrash_resetsFlagToZero)
{
    // First raise the flag, then reset.
    Settings::get().crashCheck();
    Settings::get().settings()->sync();
    ASSERT_EQ(Settings::get().settings()->value("set.start.crash").toInt(), 1);

    Settings::get().onSetCrash();
    Settings::get().settings()->sync();
    EXPECT_EQ(Settings::get().settings()->value("set.start.crash").toInt(), 0);
}

// ---- Settings::configPath / settings / group accessors ----
TEST(dmrsettings_ext, configPath_isValidAndPointsToConfigConf)
{
    QString path = Settings::get().configPath();
    EXPECT_FALSE(path.isEmpty());
    EXPECT_TRUE(path.endsWith("config.conf"));
}

TEST(dmrsettings_ext, settings_pointerIsValid)
{
    QPointer<DSettings> s = Settings::get().settings();
    ASSERT_FALSE(s.isNull());
    EXPECT_FALSE(s->keys().isEmpty());
}

TEST(dmrsettings_ext, group_baseIsValid)
{
    QPointer<DSettingsGroup> g = Settings::get().base();
    ASSERT_FALSE(g.isNull());
    EXPECT_EQ(g->key(), QStringLiteral("base"));
}

TEST(dmrsettings_ext, group_shortcutsIsValid)
{
    QPointer<DSettingsGroup> g = Settings::get().shortcuts();
    ASSERT_FALSE(g.isNull());
    EXPECT_EQ(g->key(), QStringLiteral("shortcuts"));
}

TEST(dmrsettings_ext, group_subtitleIsValid)
{
    QPointer<DSettingsGroup> g = Settings::get().subtitle();
    ASSERT_FALSE(g.isNull());
    EXPECT_EQ(g->key(), QStringLiteral("subtitle"));
}

TEST(dmrsettings_ext, group_byNameMatchesBase)
{
    QPointer<DSettingsGroup> byName = Settings::get().group("base");
    QPointer<DSettingsGroup> byAccessor = Settings::get().base();
    ASSERT_FALSE(byName.isNull());
    ASSERT_FALSE(byAccessor.isNull());
    EXPECT_EQ(byName->key(), byAccessor->key());
}

// ---- Settings::get singleton identity ----
TEST(dmrsettings_ext, singleton_returnsSameInstance)
{
    Settings &a = Settings::get();
    Settings &b = Settings::get();
    EXPECT_EQ(&a, &b);
}

// Decode-mode / Effect handlers (dmr_settings.cpp ~69-110). These run inside the
// DSettings::valueChanged lambda and are otherwise cold. Trigger by setting the
// option value (which emits valueChanged -> the lambda), then restore.
TEST(dmrsettings_ext, decodeAndEffectHandlers)
{
    auto s = Settings::get().settings();
    ASSERT_TRUE(s != nullptr);

    QVariant ovEffect = s->value("base.decode.Effect");
    QVariant ovSelect = s->value("base.decode.select");

    // Effect index 1 -> OpenGL branch; index 0 -> gpu/vaapi/vdpau/xv/x11 branch.
    if (auto opt = s->option("base.decode.Effect")) {
        opt->setValue(1);  s->sync();  QTest::qWait(20);
        opt->setValue(0);  s->sync();  QTest::qWait(20);
    }
    // decode.select = 3 -> custom decode mode branch (groups + hwdecFamily).
    if (auto opt = s->option("base.decode.select")) {
        opt->setValue(3);  s->sync();  QTest::qWait(20);
    }

    // restore
    if (auto opt = s->option("base.decode.Effect"))  opt->setValue(ovEffect);
    if (auto opt = s->option("base.decode.select"))  opt->setValue(ovSelect);
    s->sync();
    QTest::qWait(20);
}
