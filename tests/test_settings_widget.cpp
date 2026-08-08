#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include "ui/settings_widget.h"

using namespace xrk;

class SettingsWidgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure consistent QSettings organization/app for both test and widget
        QCoreApplication::setOrganizationName("XRK");
        QCoreApplication::setApplicationName("SettingsTest");
        
        // Clear settings before each test
        QSettings settings;
        settings.clear();
        
        widget = new SettingsWidget();
    }
    
    void TearDown() override {
        delete widget;
        widget = nullptr;
        
        // Clean up settings
        QSettings settings;
        settings.clear();
    }
    
    // Helper: write a value to QSettings and verify the widget reads it
    void writeAndVerify(const QString& key, const QVariant& value, 
                        std::function<QVariant()> getter, const QVariant& expected) {
        {
            QSettings s;
            s.setValue(key, value);
        }
        widget->loadSettings();
        EXPECT_EQ(getter(), expected);
    }
    
    SettingsWidget* widget = nullptr;
};

TEST_F(SettingsWidgetTest, Construct) {
    EXPECT_NE(widget, nullptr);
    EXPECT_FALSE(widget->windowTitle().isEmpty());
}

TEST_F(SettingsWidgetTest, DefaultValues) {
    EXPECT_EQ(widget->port(), 9999);
    EXPECT_TRUE(widget->autoDiscovery());
    EXPECT_FALSE(widget->encryptionEnabled());
    EXPECT_TRUE(widget->deviceName().isEmpty());
    EXPECT_EQ(widget->fps(), 60);
    EXPECT_FALSE(widget->privacyScreenEnabled());
    EXPECT_FALSE(widget->autoGrantConsentEnabled());
    EXPECT_FALSE(widget->trueColorEnabled());
    EXPECT_FALSE(widget->relayEnabled());
    EXPECT_TRUE(widget->relayHost().isEmpty());
    EXPECT_EQ(widget->relayPort(), 9997);
    EXPECT_TRUE(widget->relayToken().isEmpty());
    EXPECT_EQ(widget->scanTimeoutMs(), 5000);
}

TEST_F(SettingsWidgetTest, LoadSettingsDefaults) {
    widget->loadSettings();
    
    EXPECT_EQ(widget->port(), 9999);
    EXPECT_TRUE(widget->autoDiscovery());
    EXPECT_EQ(widget->scanTimeoutMs(), 5000);
}

TEST_F(SettingsWidgetTest, SaveAndLoadPort) {
    writeAndVerify("network/port", 8888,
        [this]() -> QVariant { return widget->port(); }, 8888);
}

TEST_F(SettingsWidgetTest, SaveAndLoadAutoDiscovery) {
    writeAndVerify("network/auto_discovery", false,
        [this]() -> QVariant { return widget->autoDiscovery(); }, false);
}

TEST_F(SettingsWidgetTest, SaveAndLoadEncryption) {
    writeAndVerify("security/encryption_enabled", true,
        [this]() -> QVariant { return widget->encryptionEnabled(); }, true);
}

TEST_F(SettingsWidgetTest, SaveAndLoadPrivacyScreen) {
    writeAndVerify("security/privacy_screen", true,
        [this]() -> QVariant { return widget->privacyScreenEnabled(); }, true);
}

TEST_F(SettingsWidgetTest, SaveAndLoadAutoGrantConsent) {
    writeAndVerify("security/auto_grant_consent", true,
        [this]() -> QVariant { return widget->autoGrantConsentEnabled(); }, true);
}

TEST_F(SettingsWidgetTest, SaveAndLoadTrueColor) {
    writeAndVerify("video/true_color", true,
        [this]() -> QVariant { return widget->trueColorEnabled(); }, true);
}

TEST_F(SettingsWidgetTest, SaveAndLoadFps) {
    writeAndVerify("performance/capture_fps", 30,
        [this]() -> QVariant { return widget->fps(); }, 30);
}

TEST_F(SettingsWidgetTest, SaveAndLoadRelayEnabled) {
    writeAndVerify("relay/enabled", true,
        [this]() -> QVariant { return widget->relayEnabled(); }, true);
}

TEST_F(SettingsWidgetTest, SaveAndLoadRelayHost) {
    writeAndVerify("relay/host", "relay.example.com",
        [this]() -> QVariant { return widget->relayHost(); }, "relay.example.com");
}

TEST_F(SettingsWidgetTest, SaveAndLoadRelayPort) {
    writeAndVerify("relay/port", 7777,
        [this]() -> QVariant { return widget->relayPort(); }, 7777);
}

TEST_F(SettingsWidgetTest, SaveAndLoadRelayToken) {
    writeAndVerify("relay/token", "secret123",
        [this]() -> QVariant { return widget->relayToken(); }, "secret123");
}

TEST_F(SettingsWidgetTest, SaveAndLoadScanTimeout) {
    writeAndVerify("scan/timeout_ms", 10000,
        [this]() -> QVariant { return widget->scanTimeoutMs(); }, 10000);
}

TEST_F(SettingsWidgetTest, SaveAndLoadDeviceName) {
    writeAndVerify("device/name", "My Computer",
        [this]() -> QVariant { return widget->deviceName(); }, "My Computer");
}

TEST_F(SettingsWidgetTest, SaveSettings) {
    // Save should not crash
    widget->saveSettings();
    
    // Verify settings were saved by reading them back
    QSettings settings;
    EXPECT_TRUE(settings.contains("network/port"));
}

TEST_F(SettingsWidgetTest, PortRange) {
    writeAndVerify("network/port", 1024,
        [this]() -> QVariant { return widget->port(); }, 1024);
    
    writeAndVerify("network/port", 65535,
        [this]() -> QVariant { return widget->port(); }, 65535);
}

TEST_F(SettingsWidgetTest, FpsRange) {
    writeAndVerify("performance/capture_fps", 1,
        [this]() -> QVariant { return widget->fps(); }, 1);
    
    writeAndVerify("performance/capture_fps", 240,
        [this]() -> QVariant { return widget->fps(); }, 240);
}

TEST_F(SettingsWidgetTest, ScanTimeoutRange) {
    // Minimum: 1 second
    writeAndVerify("scan/timeout_ms", 1000,
        [this]() -> QVariant { return widget->scanTimeoutMs(); }, 1000);
    
    // Maximum: 30 seconds
    writeAndVerify("scan/timeout_ms", 30000,
        [this]() -> QVariant { return widget->scanTimeoutMs(); }, 30000);
}

TEST_F(SettingsWidgetTest, RelayPortRange) {
    writeAndVerify("relay/port", 1,
        [this]() -> QVariant { return widget->relayPort(); }, 1);
    
    writeAndVerify("relay/port", 65535,
        [this]() -> QVariant { return widget->relayPort(); }, 65535);
}

TEST_F(SettingsWidgetTest, ScanTimeoutDefault) {
    // No value set - should use default
    QSettings settings;
    settings.remove("scan/timeout_ms");
    widget->loadSettings();
    EXPECT_EQ(widget->scanTimeoutMs(), 5000);
}

TEST_F(SettingsWidgetTest, MultipleSaveLoadCycles) {
    for (int i = 0; i < 5; ++i) {
        writeAndVerify("network/port", 9000 + i,
            [this]() -> QVariant { return widget->port(); }, 9000 + i);
        
        writeAndVerify("scan/timeout_ms", (i + 1) * 1000,
            [this]() -> QVariant { return widget->scanTimeoutMs(); }, (i + 1) * 1000);
    }
}

TEST_F(SettingsWidgetTest, AcceptAndReject) {
    // Just verify these don't crash
    widget->accept();
}
