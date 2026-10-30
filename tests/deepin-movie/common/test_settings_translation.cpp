// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// GTest cases for src/common/settings_translation.cpp.
//
// GenerateSettingTranslate() is a free function at global scope (it has no
// header declaration, so the prototype is repeated here) that only seeds
// QObject::tr() strings for the settings UI. It keeps no observable state and
// discards its locals, so per the free-function testing policy these cases
// verify the smoke contract: the call completes without crashing, the settings
// tree stays queryable, and repeated calls are idempotent with respect to the
// global settings tree.

#include <gtest/gtest.h>
#include <DSettings>

#include "dmr_settings.h"

void GenerateSettingTranslate();

class SettingsTranslationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Materialize the settings singleton before translating.
        m_settings = dmr::Settings::get().settings();
    }
    void TearDown() override
    {
        // GenerateSettingTranslate keeps no state; nothing to restore.
        m_settings.clear();
    }
    QPointer<DTK_CORE_NAMESPACE::DSettings> m_settings;
};

TEST_F(SettingsTranslationTest, GenerateSettingTranslate_Completes_WithoutCrash)
{
    // Arrange: the settings singleton exists (see SetUp).
    ASSERT_NE(m_settings.data(), nullptr);
    // Act
    EXPECT_NO_FATAL_FAILURE(GenerateSettingTranslate());
    // Assert: the call completed and the settings tree stays queryable.
    EXPECT_FALSE(m_settings->groups().isEmpty());
    EXPECT_NE(m_settings->option(QStringLiteral("base.play.playmode")), nullptr);
}

TEST_F(SettingsTranslationTest, GenerateSettingTranslate_Is_Repeatable)
{
    // Arrange
    ASSERT_NE(m_settings.data(), nullptr);
    const int groupCountBefore = m_settings->groups().size();
    // Act: seed the translations twice.
    GenerateSettingTranslate();
    GenerateSettingTranslate();
    // Assert: re-seeding neither duplicates groups nor drops options.
    EXPECT_EQ(m_settings->groups().size(), groupCountBefore);
    EXPECT_NE(m_settings->option(QStringLiteral("base.play.playmode")), nullptr);
}
