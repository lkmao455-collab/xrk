#include <gtest/gtest.h>
#include "core/theme_manager.h"

using namespace xrk;

class ThemeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_manager = &ThemeManager::instance();
        m_originalId = m_manager->currentThemeId();
    }

    void TearDown() override {
        m_manager->applyTheme(m_originalId);
    }

    ThemeManager* m_manager = nullptr;
    QString m_originalId;
};

TEST_F(ThemeManagerTest, AvailableThemes) {
    QList<Theme> themes = m_manager->availableThemes();
    EXPECT_EQ(themes.size(), 8);

    bool hasDefault = false;
    bool hasOtaku = false;
    for (const Theme& theme : themes) {
        EXPECT_FALSE(theme.id.isEmpty());
        EXPECT_TRUE(theme.primaryColor.isValid());
        EXPECT_TRUE(theme.textColor.isValid());
        EXPECT_TRUE(theme.backgroundColor.isValid());
        if (theme.id == "cute_pink") hasDefault = true;
        if (theme.id == "otaku") hasOtaku = true;
    }
    EXPECT_TRUE(hasDefault);
    EXPECT_TRUE(hasOtaku);
}

TEST_F(ThemeManagerTest, CurrentTheme) {
    Theme theme = m_manager->currentTheme();
    EXPECT_FALSE(theme.id.isEmpty());
    EXPECT_TRUE(m_manager->currentThemeId() == theme.id);
    EXPECT_TRUE(theme.name.size() > 0);
}

TEST_F(ThemeManagerTest, ApplyInvalidThemeNoop) {
    QString before = m_manager->currentThemeId();
    m_manager->applyTheme("nonexistent_theme");
    EXPECT_TRUE(m_manager->currentThemeId() == before);
}

TEST_F(ThemeManagerTest, ApplyThemeRoundTrip) {
    m_manager->applyTheme("otaku");
    EXPECT_TRUE(m_manager->currentThemeId() == "otaku");
    EXPECT_TRUE(m_manager->currentTheme().id == "otaku");

    m_manager->applyTheme("student");
    EXPECT_TRUE(m_manager->currentThemeId() == "student");
    EXPECT_TRUE(m_manager->currentTheme().id == "student");
}

TEST_F(ThemeManagerTest, GenerateQSSContainsColors) {
    Theme theme = m_manager->currentTheme();
    QString qss = m_manager->generateQSS();

    EXPECT_FALSE(qss.isEmpty());
    EXPECT_TRUE(qss.contains(theme.primaryColor.name()));
    EXPECT_TRUE(qss.contains(theme.backgroundColor.name()));
    EXPECT_TRUE(qss.contains("QPushButton"));
    EXPECT_TRUE(qss.contains("QMenuBar"));
}

TEST_F(ThemeManagerTest, GenerateQSSPerTheme) {
    m_manager->applyTheme("cute_pink");
    QString pinkQss = m_manager->generateQSS();

    m_manager->applyTheme("otaku");
    QString otakuQss = m_manager->generateQSS();

    EXPECT_FALSE(pinkQss.isEmpty());
    EXPECT_FALSE(otakuQss.isEmpty());
    EXPECT_TRUE(pinkQss != otakuQss);
}
