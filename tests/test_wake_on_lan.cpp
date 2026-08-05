#include <gtest/gtest.h>
#include "core/wake_on_lan.h"

using namespace xrk;

class WakeOnLanTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(WakeOnLanTest, ValidMacFormats) {
    EXPECT_TRUE(WakeOnLan::isValidMacAddress("00:11:22:33:44:55"));
    EXPECT_TRUE(WakeOnLan::isValidMacAddress("00-11-22-33-44-55"));
    EXPECT_TRUE(WakeOnLan::isValidMacAddress("001122334455"));
    EXPECT_TRUE(WakeOnLan::isValidMacAddress("00:1B:2C:3D:4E:5F"));
    EXPECT_TRUE(WakeOnLan::isValidMacAddress(" 00:11:22:33:44:55 "));
}

TEST_F(WakeOnLanTest, InvalidMacFormats) {
    EXPECT_FALSE(WakeOnLan::isValidMacAddress(""));
    EXPECT_FALSE(WakeOnLan::isValidMacAddress("00112233445"));
    EXPECT_FALSE(WakeOnLan::isValidMacAddress("00:11:22:33:44:5"));
    EXPECT_FALSE(WakeOnLan::isValidMacAddress("00:11:22:33:44:55:66"));
    EXPECT_FALSE(WakeOnLan::isValidMacAddress("GG:11:22:33:44:55"));
    EXPECT_FALSE(WakeOnLan::isValidMacAddress("00:11:22:33:44"));
    EXPECT_TRUE(WakeOnLan::isValidMacAddress("00:11:22:33:44:55 "));
}

TEST_F(WakeOnLanTest, NormalizeMacAddress) {
    EXPECT_TRUE(WakeOnLan::normalizeMacAddress("00:11:22:33:44:55") == "001122334455");
    EXPECT_TRUE(WakeOnLan::normalizeMacAddress("00-11-22-33-44-55") == "001122334455");
    EXPECT_TRUE(WakeOnLan::normalizeMacAddress(" 00:1b:2c:3d:4e:5f ") == "001B2C3D4E5F");
    EXPECT_TRUE(WakeOnLan::normalizeMacAddress("001122334455") == "001122334455");
}

TEST_F(WakeOnLanTest, SendMagicPacketInvalidMac) {
    EXPECT_FALSE(WakeOnLan::sendMagicPacket(""));
    EXPECT_FALSE(WakeOnLan::sendMagicPacket("not-a-mac"));
}

TEST_F(WakeOnLanTest, SendMagicPacketLoopback) {
    EXPECT_TRUE(WakeOnLan::sendMagicPacket("00:11:22:33:44:55", "127.0.0.1", 9));
}
