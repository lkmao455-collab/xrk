#include <gtest/gtest.h>
#include "core/network_manager.h"
#include "core/tcp_connection.h"

using namespace xrk;

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        network = std::make_unique<NetworkManager>();
    }
    
    void TearDown() override {
        if (network->isRunning()) {
            network->shutdown();
        }
    }
    
    std::unique_ptr<NetworkManager> network;
};

TEST_F(NetworkTest, Initialize) {
    EXPECT_TRUE(network->initialize(9999));
    EXPECT_TRUE(network->isRunning());
    EXPECT_EQ(network->port(), 9999);
}

TEST_F(NetworkTest, InitializeTwice) {
    EXPECT_TRUE(network->initialize(9999));
    EXPECT_TRUE(network->initialize(9999));
    EXPECT_TRUE(network->isRunning());
}

TEST_F(NetworkTest, Shutdown) {
    network->initialize(9999);
    network->shutdown();
    EXPECT_FALSE(network->isRunning());
}

TEST_F(NetworkTest, BroadcastDiscovery) {
    network->initialize(9999);
    network->broadcastDiscovery();
    EXPECT_TRUE(network->isRunning());
}

TEST_F(NetworkTest, ConnectToNonexistent) {
    network->initialize(9998);
    auto conn = network->connectTo("192.168.1.200", 9999);
    EXPECT_EQ(conn, nullptr);
}

TEST_F(NetworkTest, DisconnectAll) {
    network->initialize(9999);
    network->disconnectAll();
    EXPECT_TRUE(network->isRunning());
}
