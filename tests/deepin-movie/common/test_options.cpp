// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// GTest cases for src/common/options.cpp (CommandLineManager).
//
// CommandLineManager is a QCommandLineParser singleton; every case re-parses a
// synthetic argument list, so no real process argv and no device access is
// involved. Option values that look like file paths point into QTemporaryDir.
//
// Known QCommandLineParser tokenization facts (verified against Qt6):
//   * "-VV" and "-dvd-device" (single-dash multi-char names) are tokenized
//     per-character and can never match their registered option names; the
//     working long forms "--VV" / "--dvd-device" are used here. The single-dash
//     discrepancy is recorded in the defect list, not asserted as a contract.
//   * parse() resets per-call state, so cases are order-independent.

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QStringList>

#include "options.h"

using namespace dmr;

namespace {
QStringList ut_args(std::initializer_list<const char *> list)
{
    QStringList out;
    for (const char *s : list)
        out << QString::fromLatin1(s);
    return out;
}
}  // namespace

class CommandLineManagerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // A clean parse so every case starts from option defaults.
        CommandLineManager::get().parse(ut_args({"movie"}));
    }
    void TearDown() override
    {
        // Leave the singleton parsed-clean for whichever case follows.
        CommandLineManager::get().parse(ut_args({"movie"}));
    }
};

// ---- CommandLineManager::get ------------------------------------------------

TEST_F(CommandLineManagerTest, Get_Returns_StableSingleton)
{
    // Arrange
    CommandLineManager &a = CommandLineManager::get();
    // Act
    CommandLineManager &b = CommandLineManager::get();
    // Assert: repeated lookups return the same parser singleton.
    EXPECT_NE(&a, nullptr);
    EXPECT_EQ(&a, &b);
}

// ---- default values after a clean parse --------------------------------------

TEST_F(CommandLineManagerTest, Defaults_After_CleanParse)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie"}));
    // Assert
    ASSERT_TRUE(ok);
    EXPECT_FALSE(cm.verbose());
    EXPECT_FALSE(cm.debug());
    // The ctor registers {"c","gpu"} with defaultValue "auto".
    EXPECT_EQ(cm.openglMode(), QStringLiteral("auto"));
    // The ctor registers {"o","override-config"} with defaultValue "".
    EXPECT_TRUE(cm.overrideConfig().isEmpty());
    // dvdDevice() returns "" while the option is unset.
    EXPECT_TRUE(cm.dvdDevice().isEmpty());
}

// ---- CommandLineManager::verbose ----------------------------------------------

TEST_F(CommandLineManagerTest, Verbose_True_WithShortFlag)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "-V"}));
    // Assert
    ASSERT_TRUE(ok);
    EXPECT_TRUE(cm.verbose());
    EXPECT_FALSE(cm.debug());
}

TEST_F(CommandLineManagerTest, Verbose_LongForm_AlsoMatches)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--verbose"}));
    // Assert
    ASSERT_TRUE(ok);
    EXPECT_TRUE(cm.verbose());
    EXPECT_FALSE(cm.debug());
}

// ---- CommandLineManager::debug -------------------------------------------------

TEST_F(CommandLineManagerTest, Debug_True_WithLongForm)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--VV"}));
    // Assert
    ASSERT_TRUE(ok);
    EXPECT_TRUE(cm.debug());
    EXPECT_FALSE(cm.verbose());
}

// ---- CommandLineManager::dvdDevice ---------------------------------------------

TEST_F(CommandLineManagerTest, DvdDevice_Trims_SurroundingWhitespace)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--dvd-device", " /dev/sr1 "}));
    // Assert: the ctor trims the option value before exposing it.
    ASSERT_TRUE(ok);
    EXPECT_EQ(cm.dvdDevice(), QStringLiteral("/dev/sr1"));
    EXPECT_EQ(cm.dvdDevice(), cm.dvdDevice().trimmed());
}

TEST_F(CommandLineManagerTest, DvdDevice_Passes_PlainValueThrough)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--dvd-device", "/dev/sr0"}));
    // Assert: a value without whitespace is stored verbatim.
    ASSERT_TRUE(ok);
    EXPECT_EQ(cm.dvdDevice(), QStringLiteral("/dev/sr0"));
    EXPECT_TRUE(cm.dvdDevice().startsWith(QStringLiteral("/dev/")));
}

// ---- CommandLineManager::openglMode --------------------------------------------

TEST_F(CommandLineManagerTest, OpenglMode_ShortForm_Override)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "-c", "off"}));
    // Assert
    ASSERT_TRUE(ok);
    EXPECT_EQ(cm.openglMode(), QStringLiteral("off"));
    EXPECT_FALSE(cm.verbose());
}

TEST_F(CommandLineManagerTest, OpenglMode_LongForm_InlineValue)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--gpu=on"}));
    // Assert: the "--gpu=<value>" long form is accepted inline.
    ASSERT_TRUE(ok);
    EXPECT_EQ(cm.openglMode(), QStringLiteral("on"));
    EXPECT_FALSE(cm.debug());
}

// ---- CommandLineManager::overrideConfig ----------------------------------------

TEST_F(CommandLineManagerTest, OverrideConfig_Short_AndLongForms)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    QTemporaryDir tmpDir;
    ASSERT_TRUE(tmpDir.isValid());
    const QString shortFormPath = tmpDir.filePath(QStringLiteral("a.conf"));
    const QString longFormPath = tmpDir.filePath(QStringLiteral("b.conf"));
    // Act (short form "-o <path>")
    const bool shortOk = cm.parse(ut_args({"movie", "-o", shortFormPath.toLatin1().constData()}));
    // Assert
    ASSERT_TRUE(shortOk);
    EXPECT_EQ(cm.overrideConfig(), shortFormPath);
    // Act (long form "--override-config <path>")
    const bool longOk =
        cm.parse(ut_args({"movie", "--override-config", longFormPath.toLatin1().constData()}));
    // Assert: the later long form wins.
    ASSERT_TRUE(longOk);
    EXPECT_EQ(cm.overrideConfig(), longFormPath);
}

// ---- parse() error handling -----------------------------------------------------

TEST_F(CommandLineManagerTest, Parse_Fails_OnUnknownOption)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "--not-an-option"}));
    // Assert: unknown options fail the parse and enable no known flag.
    EXPECT_FALSE(ok);
    EXPECT_FALSE(cm.verbose());
    EXPECT_FALSE(cm.debug());
}

TEST_F(CommandLineManagerTest, Parse_Resets_StateBetweenCalls)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act: first a parse that enables the verbose flag ...
    const bool firstOk = cm.parse(ut_args({"movie", "-V"}));
    ASSERT_TRUE(firstOk);
    EXPECT_TRUE(cm.verbose());
    // ... then a clean parse.
    const bool secondOk = cm.parse(ut_args({"movie"}));
    // Assert: the flag from the previous call does not leak.
    ASSERT_TRUE(secondOk);
    EXPECT_FALSE(cm.verbose());
}

// ---- positional arguments --------------------------------------------------------

TEST_F(CommandLineManagerTest, PositionalArguments_Are_Captured)
{
    // Arrange
    CommandLineManager &cm = CommandLineManager::get();
    // Act
    const bool ok = cm.parse(ut_args({"movie", "/a/b.mkv"}));
    // Assert: a bare path lands in positionalArguments() verbatim.
    ASSERT_TRUE(ok);
    EXPECT_FALSE(cm.positionalArguments().isEmpty());
    EXPECT_EQ(cm.positionalArguments(), QStringList({QStringLiteral("/a/b.mkv")}));
}
