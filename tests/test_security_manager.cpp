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
