#include <gtest/gtest.h>
#include "core/encryption.h"
#include <QByteArray>

using namespace xrk;

class EncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        enc = std::make_unique<Encryption>();
        key.resize(32);
        iv.resize(16);
        for (int i = 0; i < 32; ++i) key[i] = static_cast<char>(i * 7 + 3);
        for (int i = 0; i < 16; ++i) iv[i] = static_cast<char>(i * 11 + 5);
        ASSERT_TRUE(enc->setKey(key, iv));
    }
    std::unique_ptr<Encryption> enc;
    QByteArray key, iv;
};

TEST_F(EncryptionTest, RoundTripASCII) {
    QByteArray plain("Hello, World! This is a test.");
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    EXPECT_NE(cipher, plain);
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, RoundTripBinaryWith0x80) {
    QByteArray plain(256, 0);
    for (int i = 0; i < 256; ++i) plain[i] = static_cast<char>(i);
    // This contains many 0x80 bytes (at positions 128, 256, etc.)
    // The old 0x80-sentinel padding would break here.
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted.size(), plain.size());
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, RoundTripAllZeros) {
    QByteArray plain(160, 0);
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, RoundTripSingleByte) {
    QByteArray plain(1, static_cast<char>(0x80));
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, RoundTripExactBlockSize) {
    QByteArray plain(32, 'X');
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, RoundTripLargeFrame) {
    // Simulate a compressed video frame (~50KB) with random binary data
    QByteArray plain(50000, 0);
    for (int i = 0; i < plain.size(); ++i)
        plain[i] = static_cast<char>((i * 31 + i / 7) & 0xFF);
    QByteArray cipher = enc->encrypt(plain);
    ASSERT_FALSE(cipher.isEmpty());
    EXPECT_GT(cipher.size(), plain.size());
    QByteArray decrypted = enc->decrypt(cipher);
    EXPECT_EQ(decrypted.size(), plain.size());
    EXPECT_EQ(decrypted, plain);
}

TEST_F(EncryptionTest, EncryptProducesValidCiphertext) {
    QByteArray plain("Test padding block alignment");
    QByteArray cipher = enc->encrypt(plain);
    // Encrypted size must be multiple of 16
    EXPECT_EQ(cipher.size() % 16, 0);
    // Must be at least one block larger than plaintext (padding always adds >=1 block)
    EXPECT_GT(cipher.size(), plain.size());
}

TEST_F(EncryptionTest, DecryptRejectsInvalidSize) {
    QByteArray bad(15, 'x'); // not multiple of 16
    QByteArray result = enc->decrypt(bad);
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(EncryptionTest, DecryptRejectsEmpty) {
    QByteArray result = enc->decrypt(QByteArray());
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(EncryptionTest, DifferentKeysProduceDifferentOutput) {
    QByteArray plain("Same plaintext, different key");
    QByteArray cipher1 = enc->encrypt(plain);

    Encryption enc2;
    QByteArray key2(32, 'B'), iv2(16, 'C');
    ASSERT_TRUE(enc2.setKey(key2, iv2));
    QByteArray cipher2 = enc2.encrypt(plain);

    // Different keys → different ciphertext
    EXPECT_NE(cipher1, cipher2);
}
