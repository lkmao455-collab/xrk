#include <gtest/gtest.h>
#include "app/system_info_collector.h"

using namespace xrk;

class SystemInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(SystemInfoTest, CollectBasicFields) {
    SysInfo info = SystemInfoCollector::collect();

    EXPECT_TRUE(info.osName == "Windows");
    EXPECT_FALSE(info.osVersion.isEmpty());
    EXPECT_GT(info.memoryTotal, 0u);
    EXPECT_GT(info.memoryAvailable, 0u);
    EXPECT_GT(info.diskTotal, 0u);
    EXPECT_GT(info.uptime, 0u);
    EXPECT_GT(info.processCount, 0);
}

TEST_F(SystemInfoTest, CollectUsageRanges) {
    SysInfo info = SystemInfoCollector::collect();

    EXPECT_GE(info.cpuUsage, 0.0);
    EXPECT_LE(info.cpuUsage, 100.0);
    EXPECT_GE(info.memoryUsage, 0.0);
    EXPECT_LE(info.memoryUsage, 100.0);
    EXPECT_GE(info.diskUsage, 0.0);
    EXPECT_LE(info.diskUsage, 100.0);
}
