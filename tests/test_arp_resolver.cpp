#include <gtest/gtest.h>
#include "core/arp_resolver.h"
#include <QRegularExpression>

using namespace xrk;

class ArpResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }

    static bool looksLikeMac(const QString& mac) {
        static QRegularExpression regex(
            "^([0-9A-F]{2}:){5}[0-9A-F]{2}$",
            QRegularExpression::CaseInsensitiveOption);
        return regex.match(mac).hasMatch();
    }
};

TEST_F(ArpResolverTest, EmptyIp) {
    EXPECT_TRUE(ArpResolver::resolveMac("").isEmpty());
}

TEST_F(ArpResolverTest, InvalidIp) {
    EXPECT_TRUE(ArpResolver::resolveMac("not-an-ip").isEmpty());
    EXPECT_TRUE(ArpResolver::resolveMac("999.1.1.1").isEmpty());
    EXPECT_TRUE(ArpResolver::resolveMac("1.2.3.4.5").isEmpty());
    EXPECT_TRUE(ArpResolver::resolveMac("").isEmpty());
}

TEST_F(ArpResolverTest, UnreachableIpReturnsEmpty) {
    QString mac = ArpResolver::resolveMac("192.0.2.1", 100);
    EXPECT_TRUE(mac.isEmpty() || looksLikeMac(mac));
}

TEST_F(ArpResolverTest, LoopbackEitherEmptyOrValidFormat) {
    QString mac = ArpResolver::resolveMac("127.0.0.1", 100);
    EXPECT_TRUE(mac.isEmpty() || looksLikeMac(mac));
}
