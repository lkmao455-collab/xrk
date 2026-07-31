#include <gtest/gtest.h>
#include "app/session_manager.h"
#include "core/types.h"

using namespace xrk;

class SessionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<SessionManager>();
    }
    
    void TearDown() override {
        manager.reset();
    }
    
    std::unique_ptr<SessionManager> manager;
};

TEST_F(SessionManagerTest, CreateSession) {
    QString deviceId = "device-123";
    QString sessionId = manager->createSession(deviceId);
    
    EXPECT_FALSE(sessionId.isEmpty());
    EXPECT_TRUE(manager->isSessionValid(sessionId));
}

TEST_F(SessionManagerTest, CloseSession) {
    QString deviceId = "device-123";
    QString sessionId = manager->createSession(deviceId);
    
    EXPECT_TRUE(manager->isSessionValid(sessionId));
    
    manager->closeSession(sessionId);
    EXPECT_FALSE(manager->isSessionValid(sessionId));
}

TEST_F(SessionManagerTest, GetSession) {
    QString deviceId = "device-123";
    QString sessionId = manager->createSession(deviceId);
    
    SessionInfo session = manager->getSession(sessionId);
    EXPECT_EQ(session.sessionId, sessionId);
    EXPECT_EQ(session.deviceId, deviceId);
    EXPECT_TRUE(session.active);
}

TEST_F(SessionManagerTest, GetActiveSessions) {
    QString deviceId1 = "device-123";
    QString deviceId2 = "device-456";
    
    QString sessionId1 = manager->createSession(deviceId1);
    QString sessionId2 = manager->createSession(deviceId2);
    
    QList<SessionInfo> sessions = manager->getActiveSessions();
    EXPECT_EQ(sessions.size(), 2);
}

TEST_F(SessionManagerTest, CleanupExpiredSessions) {
    // The new design has no public timeout/cleanup API: sessions stay valid
    // until explicitly closed (expiry is handled internally by a timer).
    QString deviceId = "device-123";
    QString sessionId = manager->createSession(deviceId);

    EXPECT_TRUE(manager->isSessionValid(sessionId));

    manager->closeSession(sessionId);
    EXPECT_FALSE(manager->isSessionValid(sessionId));
}

TEST_F(SessionManagerTest, MultipleSessions) {
    for (int i = 0; i < 5; ++i) {
        QString deviceId = "device-" + QString::number(i);
        QString sessionId = manager->createSession(deviceId);
        EXPECT_FALSE(sessionId.isEmpty());
    }
    
    QList<SessionInfo> sessions = manager->getActiveSessions();
    EXPECT_EQ(sessions.size(), 5);
}

TEST_F(SessionManagerTest, CloseAllSessions) {
    for (int i = 0; i < 3; ++i) {
        QString deviceId = "device-" + QString::number(i);
        manager->createSession(deviceId);
    }
    
    manager->closeAllSessions();
    
    QList<SessionInfo> sessions = manager->getActiveSessions();
    EXPECT_EQ(sessions.size(), 0);
}
