#include <gtest/gtest.h>
#include "core/translation_manager.h"

using namespace xrk;

class TranslationManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(TranslationManagerTest, AvailableLanguages) {
    TranslationManager& tm = TranslationManager::instance();
    QStringList langs = tm.availableLanguages();
    EXPECT_EQ(langs.size(), 10);
    EXPECT_TRUE(langs.contains("en_US"));
    EXPECT_TRUE(langs.contains("zh_CN"));
    EXPECT_TRUE(langs.contains("zh_TW"));
    EXPECT_TRUE(langs.contains("ja_JP"));
    EXPECT_TRUE(langs.contains("ko_KR"));
}

TEST_F(TranslationManagerTest, SystemLanguageSupported) {
    TranslationManager& tm = TranslationManager::instance();
    QString sysLang = tm.systemLanguage();
    EXPECT_FALSE(sysLang.isEmpty());
    EXPECT_TRUE(tm.availableLanguages().contains(sysLang));
}

TEST_F(TranslationManagerTest, DefaultLanguageName) {
    TranslationManager& tm = TranslationManager::instance();
    EXPECT_TRUE(tm.currentLanguage().isEmpty());
    EXPECT_TRUE(tm.currentLanguageName() == "English");
}
