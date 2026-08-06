#include <gtest/gtest.h>
#include "app/security_manager.h"

using namespace xrk;

class SecurityManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager = std::make_unique<SecurityManager>();
    }
    
    void TearDown() override {
        manager.reset();
    }
    
    std::unique_ptr<SecurityManager> manager;
};

TEST_F(SecurityManagerTest, GenerateDeviceId) {
    QString deviceId = manager->generateDeviceId();
    EXPECT_FALSE(deviceId.isEmpty());
}

TEST_F(SecurityManagerTest, DeviceIdUnique) {
    QString deviceId1 = manager->generateDeviceId();
    QString deviceId2 = manager->generateDeviceId();
    EXPECT_NE(deviceId1, deviceId2);
}

TEST_F(SecurityManagerTest, GenerateToken) {
    QString token = manager->generateSessionToken();
    EXPECT_FALSE(token.isEmpty());
}

TEST_F(SecurityManagerTest, TokenUnique) {
    QString token1 = manager->generateSessionToken();
    QString token2 = manager->generateSessionToken();
    EXPECT_NE(token1, token2);
}

TEST_F(SecurityManagerTest, ValidateToken) {
    QString token = manager->generateSessionToken();
    EXPECT_TRUE(manager->validateToken(token));
}

TEST_F(SecurityManagerTest, ValidateTokenInvalid) {
    EXPECT_FALSE(manager->validateToken("invalid-token"));
}

TEST_F(SecurityManagerTest, EncryptDecrypt) {
    manager->setEncryptionEnabled(true);
    QByteArray key = "test-key-1234567890";
    QByteArray original = "test data to encrypt";
    QByteArray encrypted = manager->encrypt(original, key);
    EXPECT_FALSE(encrypted.isEmpty());
    EXPECT_NE(encrypted, original);

    QByteArray decrypted = manager->decrypt(encrypted, key);
    EXPECT_EQ(decrypted, original);
}

TEST_F(SecurityManagerTest, EncryptEmpty) {
    manager->setEncryptionEnabled(true);
    QByteArray key = "test-key-1234567890";
    QByteArray original;
    QByteArray encrypted = manager->encrypt(original, key);
    EXPECT_TRUE(encrypted.isEmpty());
}

TEST_F(SecurityManagerTest, HashPassword) {
    QString password = "securepassword123";
    QByteArray hash = manager->hashPassword(password);
    EXPECT_FALSE(hash.isEmpty());
    EXPECT_NE(hash, password.toUtf8());
}

TEST_F(SecurityManagerTest, VerifyPassword) {
    QString password = "securepassword123";
    QByteArray hash = manager->hashPassword(password);
    EXPECT_TRUE(manager->verifyPassword(password, hash));
}

TEST_F(SecurityManagerTest, VerifyPasswordWrong) {
    QString password = "securepassword123";
    QByteArray hash = manager->hashPassword(password);
    EXPECT_FALSE(manager->verifyPassword("wrongpassword", hash));
}

TEST_F(SecurityManagerTest, GenerateECDHKeyPair) {
    QByteArray privateKey = manager->generateECDHKeyPair();
    EXPECT_EQ(privateKey.size(), 32);

    bool isAllZero = true;
    for (int i = 0; i < privateKey.size(); ++i) {
        if (privateKey[i] != 0) {
            isAllZero = false;
            break;
        }
    }
    EXPECT_FALSE(isAllZero);
}

TEST_F(SecurityManagerTest, DeriveKeyFromPassword) {
    QString password = "test_password_123";
    QString salt = "random_salt_456";

    QByteArray key1 = manager->deriveKeyFromPassword(password, salt);
    QByteArray key2 = manager->deriveKeyFromPassword(password, salt);

    EXPECT_EQ(key1, key2);
    EXPECT_EQ(key1.size(), 32);

    QByteArray key3 = manager->deriveKeyFromPassword(password, "different_salt");
    EXPECT_NE(key1, key3);
}

TEST_F(SecurityManagerTest, CreateE2EESession) {
    QString deviceId = "test_device_001";
    QByteArray peerPublicKey(32, 0);
    QByteArray sharedSecret(32, 0);

    for (int i = 0; i < 32; ++i) {
        peerPublicKey[i] = static_cast<char>(i + 1);
        sharedSecret[i] = static_cast<char>(32 - i);
    }

    EXPECT_TRUE(manager->createE2EESession(deviceId, peerPublicKey, sharedSecret));
    EXPECT_TRUE(manager->hasE2EESession(deviceId));

    SecurityManager::E2EESession* session = manager->getE2EESession(deviceId);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->deviceId, deviceId);
    EXPECT_EQ(session->peerPublicKey.size(), 32);
    EXPECT_EQ(session->sharedSecret.size(), 32);
    EXPECT_TRUE(session->isActive);
    EXPECT_EQ(session->encryptionAlgorithm, "AES-256-GCM");
    EXPECT_EQ(session->nonce.size(), 12);
}

TEST_F(SecurityManagerTest, E2EESessionUniqueIds) {
    QString deviceId1 = "device_001";
    QString deviceId2 = "device_002";

    QByteArray pubKey1(32, 0);
    QByteArray sec1(32, 0);
    QByteArray pubKey2(32, 0);
    QByteArray sec2(32, 0);

    for (int i = 0; i < 32; ++i) {
        pubKey1[i] = static_cast<char>(i + 1);
        sec1[i] = static_cast<char>(32 - i);
        pubKey2[i] = static_cast<char>(i + 33);
        sec2[i] = static_cast<char>(64 - i);
    }

    manager->createE2EESession(deviceId1, pubKey1, sec1);
    manager->createE2EESession(deviceId2, pubKey2, sec2);

    EXPECT_TRUE(manager->hasE2EESession(deviceId1));
    EXPECT_TRUE(manager->hasE2EESession(deviceId2));

    SecurityManager::E2EESession* s1 = manager->getE2EESession(deviceId1);
    SecurityManager::E2EESession* s2 = manager->getE2EESession(deviceId2);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_NE(s1->sessionId, s2->sessionId);
}

TEST_F(SecurityManagerTest, RemoveE2EESession) {
    QString deviceId = "test_device_remove";
    QByteArray peerPublicKey(32, 0);
    QByteArray sharedSecret(32, 0);

    for (int i = 0; i < 32; ++i) {
        peerPublicKey[i] = static_cast<char>(i + 1);
        sharedSecret[i] = static_cast<char>(32 - i);
    }

    manager->createE2EESession(deviceId, peerPublicKey, sharedSecret);
    EXPECT_TRUE(manager->hasE2EESession(deviceId));

    EXPECT_TRUE(manager->removeE2EESession(deviceId));
    EXPECT_FALSE(manager->hasE2EESession(deviceId));

    EXPECT_FALSE(manager->removeE2EESession("non_existent_device"));
}

TEST_F(SecurityManagerTest, GetAllE2EESessions) {
    for (int i = 0; i < 3; ++i) {
        QString deviceId = QString("multi_device_%1").arg(i);
        QByteArray peerPublicKey(32, 0);
        QByteArray sharedSecret(32, 0);
        for (int j = 0; j < 32; ++j) {
            peerPublicKey[j] = static_cast<char>(i * 32 + j);
            sharedSecret[j] = static_cast<char>((31 - j) + i * 32);
        }
        manager->createE2EESession(deviceId, peerPublicKey, sharedSecret);
    }

    QList<SecurityManager::E2EESession> sessions = manager->getAllE2EESessions();
    EXPECT_EQ(sessions.size(), 3);

    for (const auto& session : sessions) {
        manager->removeE2EESession(session.deviceId);
    }

    EXPECT_TRUE(manager->getAllE2EESessions().isEmpty());
}

TEST_F(SecurityManagerTest, UpdateSessionActivity) {
    QString deviceId = "test_device_activity";
    QByteArray peerPublicKey(32, 0);
    QByteArray sharedSecret(32, 0);

    for (int i = 0; i < 32; ++i) {
        peerPublicKey[i] = static_cast<char>(i + 1);
        sharedSecret[i] = static_cast<char>(32 - i);
    }

    manager->createE2EESession(deviceId, peerPublicKey, sharedSecret);

    SecurityManager::E2EESession* session = manager->getE2EESession(deviceId);
    ASSERT_NE(session, nullptr);
    qint64 initialActivity = session->lastActivity;

    EXPECT_TRUE(manager->updateSessionActivity(deviceId));

    session = manager->getE2EESession(deviceId);
    ASSERT_NE(session, nullptr);
    EXPECT_GE(session->lastActivity, initialActivity);
}

TEST_F(SecurityManagerTest, GetSessionId) {
    QString deviceId = "test_device_session_id";
    QByteArray peerPublicKey(32, 0);
    QByteArray sharedSecret(32, 0);

    for (int i = 0; i < 32; ++i) {
        peerPublicKey[i] = static_cast<char>(i + 1);
        sharedSecret[i] = static_cast<char>(32 - i);
    }

    manager->createE2EESession(deviceId, peerPublicKey, sharedSecret);

    QByteArray sessionId = manager->getSessionId(deviceId);
    EXPECT_FALSE(sessionId.isEmpty());

    manager->removeE2EESession(deviceId);
    EXPECT_TRUE(manager->getSessionId(deviceId).isEmpty());
}

TEST_F(SecurityManagerTest, GetNonExistentE2EESession) {
    EXPECT_FALSE(manager->hasE2EESession("non_existent_device"));
    EXPECT_EQ(manager->getE2EESession("non_existent_device"), nullptr);
    EXPECT_TRUE(manager->getSessionId("non_existent_device").isEmpty());
}
