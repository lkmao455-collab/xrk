#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QFile>
#include <QByteArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include "ipmsg_manager.h"

namespace xrk {

// Fixture is a friend of IPMsgManager (see friend declaration in
// ipmsg_manager.h) and routes private-member access through these public
// helpers so TEST_F bodies stay clean.
class IpmsgCryptoTest : public ::testing::Test {
protected:
    QByteArray keyPair(IPMsgManager& m, QByteArray* pub) {
        return m.generateEcdhKeyPair(pub);
    }
    QByteArray shared(IPMsgManager& m, const QByteArray& peerPub, const QByteArray& priv) {
        return m.computeSharedSecret(peerPub, priv);
    }
    QByteArray gcmEnc(IPMsgManager& m, const QByteArray& plain, const QByteArray& key, QByteArray* nonce) {
        return m.aesGcmEncrypt(plain, key, nonce);
    }
    QByteArray gcmDec(IPMsgManager& m, const QByteArray& cipher, const QByteArray& key,
                      const QByteArray& nonce, const QByteArray& tag) {
        return m.aesGcmDecrypt(cipher, key, nonce, tag);
    }
    void seed(IPMsgManager& m, const QString& deviceId, const QByteArray& secret) {
        IPMsgManager::E2EESession s;
        s.sharedSecret = secret;
        m.m_e2eeSessions[deviceId] = s;
    }
    QList<QByteArray> chunks(IPMsgManager& m, const QString& path, qint64 cs) {
        return m.calculateChunkMd5s(path, cs);
    }
    QByteArray snapJson(IPMsgManager& m, const DatabaseManager::SyncSnapshot& snap) {
        return m.serializeSnapshot(snap);
    }
    DatabaseManager::SyncSnapshot snapFromJson(IPMsgManager& m, const QJsonObject& json) {
        return m.deserializeSnapshot(json);
    }
};

TEST_F(IpmsgCryptoTest, GenerateKeyPairProducesDistinctKeys) {
    IPMsgManager m;
    QByteArray pub1, pub2;
    QByteArray priv1 = keyPair(m, &pub1);
    QByteArray priv2 = keyPair(m, &pub2);

    EXPECT_EQ(priv1.size(), 32);
    EXPECT_EQ(pub1.size(), 32);
    EXPECT_NE(priv1, priv2);
    EXPECT_NE(pub1, pub2);
}

TEST_F(IpmsgCryptoTest, PublicKeyDerivedFromPrivate) {
    IPMsgManager m;
    QByteArray pub;
    QByteArray priv = keyPair(m, &pub);
    EXPECT_EQ(pub, QCryptographicHash::hash(priv, QCryptographicHash::Sha256));
}

TEST_F(IpmsgCryptoTest, SharedSecretIsSymmetric) {
    IPMsgManager alice, bob;
    QByteArray pubA, pubB, privA, privB;
    privA = keyPair(alice, &pubA);
    privB = keyPair(bob, &pubB);

    // Alice and Bob independently compute the shared secret from their own
    // private key and the peer's public key. Both must agree.
    QByteArray secretA = shared(alice, pubB, privA);
    QByteArray secretB = shared(bob, pubA, privB);

    EXPECT_EQ(secretA.size(), 32);
    EXPECT_EQ(secretA, secretB);
}

TEST_F(IpmsgCryptoTest, AesGcmRoundTrip) {
    IPMsgManager m;
    QByteArray key = QCryptographicHash::hash("test-key", QCryptographicHash::Sha256);
    QByteArray plain("Attack at dawn! \x01\x02\x80\xFF binary");

    QByteArray nonce;
    QByteArray cipher = gcmEnc(m, plain, key, &nonce);
    EXPECT_FALSE(nonce.isEmpty());
    EXPECT_GE(cipher.size(), plain.size());

    QByteArray tag = cipher.right(16);
    QByteArray body = cipher.left(cipher.size() - 16);
    QByteArray decrypted = gcmDec(m, body, key, nonce, tag);
    EXPECT_EQ(decrypted, plain);
}

TEST_F(IpmsgCryptoTest, AesGcmRejectsTamperedCiphertext) {
    IPMsgManager m;
    QByteArray key = QCryptographicHash::hash("test-key", QCryptographicHash::Sha256);
    QByteArray plain("sensitive payload");

    QByteArray nonce;
    QByteArray cipher = gcmEnc(m, plain, key, &nonce);
    QByteArray body = cipher.left(cipher.size() - 16);
    QByteArray tag = cipher.right(16);

    // Flip one byte of the ciphertext body -> tag verification must fail
    QByteArray tampered = body;
    tampered[0] = static_cast<char>(tampered[0] ^ 0x01);
    EXPECT_TRUE(gcmDec(m, tampered, key, nonce, tag).isEmpty());

    // Flip one byte of the tag -> must fail too
    QByteArray badTag = tag;
    badTag[3] = static_cast<char>(badTag[3] ^ 0x01);
    EXPECT_TRUE(gcmDec(m, body, key, nonce, badTag).isEmpty());
}

TEST_F(IpmsgCryptoTest, EncryptDecryptMessageRoundTripAcrossPeers) {
    IPMsgManager alice, bob;

    QByteArray pubA, pubB, privA, privB;
    privA = keyPair(alice, &pubA);
    privB = keyPair(bob, &pubB);

    QByteArray secretA = shared(alice, pubB, privA);
    QByteArray secretB = shared(bob, pubA, privB);
    ASSERT_EQ(secretA, secretB);

    seed(alice, "bob-id", secretA);
    seed(bob, "alice-id", secretB);

    QByteArray cipher = alice.encryptMessage("hello bob, this is secret", "bob-id");
    ASSERT_FALSE(cipher.isEmpty());
    EXPECT_NE(cipher, QByteArray("hello bob, this is secret"));

    QByteArray plain = bob.decryptMessage(cipher, "alice-id");
    EXPECT_EQ(plain, "hello bob, this is secret");
}

TEST_F(IpmsgCryptoTest, EncryptWithoutSessionReturnsPlaintext) {
    IPMsgManager alice;
    QByteArray out = alice.encryptMessage("not encrypted", "unknown-device");
    EXPECT_EQ(out, QByteArray("not encrypted"));
}

TEST_F(IpmsgCryptoTest, DecryptWithoutSessionReturnsAsIs) {
    IPMsgManager alice;
    QByteArray out = alice.decryptMessage("unknown", "unknown-device");
    EXPECT_EQ(out, QByteArray("unknown"));
}

TEST_F(IpmsgCryptoTest, HasEstablishedSessionRequiresSharedSecret) {
    IPMsgManager alice;
    EXPECT_FALSE(alice.hasEstablishedSession("dev-1"));

    seed(alice, "dev-1", QByteArray()); // empty secret
    EXPECT_FALSE(alice.hasEstablishedSession("dev-1"));

    seed(alice, "dev-1", QByteArray(32, 'K'));
    EXPECT_TRUE(alice.hasEstablishedSession("dev-1"));
}

TEST_F(IpmsgCryptoTest, CalculateFileMd5MatchesReference) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("data.bin");

    QByteArray content(4096, 0);
    for (int i = 0; i < content.size(); ++i)
        content[i] = static_cast<char>((i * 13 + 7) & 0xFF);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    QString expected = QCryptographicHash::hash(content, QCryptographicHash::Md5).toHex();
    EXPECT_EQ(IPMsgManager::calculateFileMd5(path), expected);
    EXPECT_TRUE(IPMsgManager::calculateFileMd5(dir.filePath("missing.bin")).isEmpty());
}

TEST_F(IpmsgCryptoTest, CalculateFileMd5QIODevice) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("io.bin");
    QByteArray content("io-device-md5-test");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QString expected = QCryptographicHash::hash(content, QCryptographicHash::Md5).toHex();
    EXPECT_EQ(IPMsgManager::calculateFileMd5(&file), expected);
    file.close();
}

TEST_F(IpmsgCryptoTest, ChunkMd5sMatchManualSegments) {
    IPMsgManager m;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("chunk.bin");

    const qint64 chunkSize = 1024 * 1024; // 1MB
    const qint64 fileSize = chunkSize * 2 + 12345; // partial last chunk
    QByteArray content(fileSize, 0);
    for (qint64 i = 0; i < fileSize; ++i)
        content[i] = static_cast<char>((i * 31 + i / 13) & 0xFF);

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    QList<QByteArray> chunkMd5s = chunks(m, path, chunkSize);
    ASSERT_EQ(chunkMd5s.size(), 3);

    for (int i = 0; i < chunkMd5s.size(); ++i) {
        EXPECT_EQ(chunkMd5s[i].size(), 16) << "md5 digest must be 16 bytes, chunk " << i;
        qint64 start = static_cast<qint64>(i) * chunkSize;
        qint64 len = qMin(chunkSize, fileSize - start);
        QByteArray segment = content.mid(static_cast<int>(start), static_cast<int>(len));
        QByteArray expected = QCryptographicHash::hash(segment, QCryptographicHash::Md5);
        EXPECT_EQ(chunkMd5s[i], expected) << "chunk md5 mismatch at chunk " << i;
    }
}

TEST_F(IpmsgCryptoTest, ChunkMd5sSmallFileSingleChunk) {
    IPMsgManager m;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("small.bin");
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("tiny");
    file.close();

    QList<QByteArray> chunkMd5s = chunks(m, path, 1024 * 1024);
    ASSERT_EQ(chunkMd5s.size(), 1);
    EXPECT_EQ(chunkMd5s[0].size(), 16);
}

TEST_F(IpmsgCryptoTest, SnapshotSerializeDeserializeRoundTrip) {
    IPMsgManager m;

    DatabaseManager::SyncSnapshot snap;
    snap.version = 7000;

    DatabaseManager::SyncDeviceRow dev;
    dev.deviceId = "dev-a";
    dev.name = "Laptop";
    dev.ip = "192.168.1.10";
    dev.port = 2425;
    dev.lastSeen = 1000;
    dev.updatedAt = 2000;
    dev.isFriend = true;
    snap.devices.append(dev);

    DatabaseManager::SyncGroupRow grp;
    grp.groupId = "grp-1";
    grp.name = "Team";
    grp.memberIds = {"dev-a", "dev-b"};
    grp.memberNames = {"Laptop", "Phone"};
    grp.createdAt = 3000;
    grp.updatedAt = 4000;
    snap.groups.append(grp);

    DatabaseManager::SyncSettingRow setting;
    setting.key = "theme";
    setting.value = "dark";
    setting.updatedAt = 5000;
    snap.settings.append(setting);

    DatabaseManager::SyncMessageRow msg;
    msg.messageId = "dev-a_6000";
    msg.senderId = "dev-a";
    msg.senderName = "Laptop";
    msg.senderIp = "192.168.1.10";
    msg.content = "hello sync";
    msg.timestamp = 6000;
    msg.isRead = true;
    msg.readBy = "dev-b";
    msg.targetId = "dev-b";
    snap.messages.append(msg);

    QByteArray json = snapJson(m, snap);
    ASSERT_FALSE(json.isEmpty());

    QJsonObject obj = QJsonDocument::fromJson(json).object();
    DatabaseManager::SyncSnapshot back = snapFromJson(m, obj);

    EXPECT_EQ(back.version, snap.version);
    EXPECT_EQ(back.devices.size(), 1);
    EXPECT_TRUE(back.devices[0].isFriend);
    EXPECT_EQ(back.devices[0].deviceId, "dev-a");
    EXPECT_EQ(back.groups.size(), 1);
    EXPECT_EQ(back.groups[0].memberNames.size(), 2);
    EXPECT_EQ(back.settings.size(), 1);
    EXPECT_EQ(back.settings[0].value, "dark");
    EXPECT_EQ(back.messages.size(), 1);
    EXPECT_EQ(back.messages[0].content, "hello sync");
    EXPECT_EQ(back.messages[0].readBy, "dev-b");
}

TEST_F(IpmsgCryptoTest, SnapshotRoundTripSurvivesJsonStringify) {
    IPMsgManager m;

    DatabaseManager::SyncSnapshot snap;
    DatabaseManager::SyncMessageRow msg;
    msg.messageId = "recall-123";
    msg.senderId = "dev-x";
    msg.content = QString::fromUtf8("中文消息 with 你好 and symbols 😀");
    msg.timestamp = 9000;
    msg.recallId = "recall-123";
    msg.isRecalled = true;
    msg.targetId = "grp-1";
    msg.isGroup = true;
    snap.messages.append(msg);

    // JSON stringify -> object round trip (as sendSyncSnapshotResponse does)
    QByteArray json = snapJson(m, snap);
    QJsonObject outer = QJsonDocument::fromJson(json).object();
    QJsonObject embedded = QJsonDocument::fromJson(QJsonDocument(outer).toJson(QJsonDocument::Compact)).object();
    DatabaseManager::SyncSnapshot back = snapFromJson(m, embedded);

    ASSERT_EQ(back.messages.size(), 1);
    EXPECT_EQ(back.messages[0].content, QString::fromUtf8("中文消息 with 你好 and symbols 😀"));
    EXPECT_EQ(back.messages[0].recallId, "recall-123");
    EXPECT_TRUE(back.messages[0].isRecalled);
    EXPECT_TRUE(back.messages[0].isGroup);
}

} // namespace xrk
