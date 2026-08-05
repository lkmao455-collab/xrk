#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QVariant>
#include <QDateTime>
#include "database_manager.h"
#include "ipmsg_manager.h"

using namespace xrk;

namespace {

IPMsgMessage makeMsg(const QString& senderId, const QString& content, qint64 ts) {
    IPMsgMessage msg;
    msg.senderId = senderId;
    msg.senderName = "User-" + senderId;
    msg.senderIp = "192.168.1.10";
    msg.content = content;
    msg.timestamp = ts;
    return msg;
}

class DatabaseManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(dir->isValid());
        db = &DatabaseManager::instance();
        ASSERT_TRUE(db->initialize(dir->filePath("test.db")));
    }

    void TearDown() override {
        db->shutdown();
    }

    std::unique_ptr<QTemporaryDir> dir;
    DatabaseManager* db;
};

} // namespace

TEST_F(DatabaseManagerTest, InitializeEnablesWalAndCreatesSchema) {
    QSqlQuery query(QSqlDatabase::database("ipmsg_connection"));
    ASSERT_TRUE(query.exec("PRAGMA journal_mode"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toString().toLower(), "wal");

    const QStringList tables = {"messages", "devices", "groups", "settings",
                                "recent_chats", "file_transfers", "offline_messages"};
    for (const QString& t : tables) {
        QSqlQuery q(QSqlDatabase::database("ipmsg_connection"));
        ASSERT_TRUE(q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='" + t + "'"));
        EXPECT_TRUE(q.next());
    }
}

TEST_F(DatabaseManagerTest, SaveAndLoadMessageRoundTrip) {
    IPMsgMessage msg = makeMsg("dev-a", "hello world", 1000);
    ASSERT_TRUE(db->saveMessage(msg, "dev-b", false));

    QList<IPMsgMessage> loaded = db->loadMessages("dev-b", 50, 0, false);
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_EQ(loaded[0].senderId, "dev-a");
    EXPECT_EQ(loaded[0].content, "hello world");
    EXPECT_EQ(loaded[0].timestamp, 1000);
}

TEST_F(DatabaseManagerTest, LoadMessagesPagination) {
    for (int i = 1; i <= 5; ++i) {
        ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", QString("m%1").arg(i), i * 1000), "dev-b", false));
    }

    QList<IPMsgMessage> all = db->loadMessages("dev-b", 50, 0, false);
    ASSERT_EQ(all.size(), 5);
    EXPECT_EQ(all[0].timestamp, 1000);  // chronological ascending
    EXPECT_EQ(all[4].timestamp, 5000);

    // limit returns newest N, in ascending order
    QList<IPMsgMessage> limited = db->loadMessages("dev-b", 2, 0, false);
    ASSERT_EQ(limited.size(), 2);
    EXPECT_EQ(limited[0].timestamp, 4000);
    EXPECT_EQ(limited[1].timestamp, 5000);

    // beforeTimestamp returns only older messages
    QList<IPMsgMessage> older = db->loadMessages("dev-b", 50, 4000, false);
    ASSERT_EQ(older.size(), 3);
    EXPECT_EQ(older[0].timestamp, 1000);
    EXPECT_EQ(older[2].timestamp, 3000);
}

TEST_F(DatabaseManagerTest, LoadMessagesGroupVsDirectAreSeparate) {
    ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", "direct", 1000), "dev-b", false));
    ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", "group", 2000), "grp-1", true));

    EXPECT_EQ(db->loadMessages("dev-b", 50, 0, false).size(), 1);
    EXPECT_EQ(db->loadMessages("grp-1", 50, 0, true).size(), 1);
    EXPECT_EQ(db->loadMessages("dev-b", 50, 0, true).size(), 0);
}

TEST_F(DatabaseManagerTest, DeleteMessageMarksRecalled) {
    IPMsgMessage msg = makeMsg("dev-a", "secret", 1000);
    msg.recallId = "rec-1";
    ASSERT_TRUE(db->saveMessage(msg, "dev-b", false));

    ASSERT_TRUE(db->deleteMessage("rec-1"));

    QList<IPMsgMessage> loaded = db->loadMessages("dev-b", 50, 0, false);
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_TRUE(loaded[0].isRecalled);
    EXPECT_EQ(loaded[0].content, "[已撤回]");
}

TEST_F(DatabaseManagerTest, MarkMessageReadUpdatesUnreadCount) {
    for (int i = 1; i <= 3; ++i) {
        ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", QString("m%1").arg(i), i * 1000), "dev-b", false));
    }
    EXPECT_EQ(db->getUnreadCount("dev-b", false), 3);

    // message_id is "<senderId>_<timestamp>"
    ASSERT_TRUE(db->markMessageRead("dev-a_1000", "dev-b"));
    EXPECT_EQ(db->getUnreadCount("dev-b", false), 2);
}

TEST_F(DatabaseManagerTest, DeviceCrudRoundTrip) {
    IPMsgDevice device;
    device.id = "dev-1";
    device.name = "Laptop";
    device.ip = "192.168.1.50";
    device.port = 2425;
    device.lastSeen = 111;
    ASSERT_TRUE(db->saveDevice(device));

    QList<IPMsgDevice> devices = db->loadAllDevices();
    ASSERT_EQ(devices.size(), 1);
    EXPECT_EQ(devices[0].name, "Laptop");
    EXPECT_EQ(devices[0].ip, "192.168.1.50");
    EXPECT_EQ(devices[0].port, 2425);

    ASSERT_TRUE(db->updateDeviceLastSeen("dev-1", 999));
    devices = db->loadAllDevices();
    EXPECT_EQ(devices[0].lastSeen, 999);

    ASSERT_TRUE(db->removeDevice("dev-1"));
    EXPECT_TRUE(db->loadAllDevices().isEmpty());
}

TEST_F(DatabaseManagerTest, FriendLifecycle) {
    IPMsgDevice device;
    device.id = "dev-1";
    device.name = "Alice";
    device.ip = "192.168.1.51";
    ASSERT_TRUE(db->saveDevice(device));

    EXPECT_FALSE(db->isFriend("dev-1"));
    ASSERT_TRUE(db->addFriend("dev-1"));
    EXPECT_TRUE(db->isFriend("dev-1"));
    EXPECT_TRUE(db->loadFriends() == QList<QString>{"dev-1"});

    ASSERT_TRUE(db->removeFriend("dev-1"));
    EXPECT_FALSE(db->isFriend("dev-1"));
    EXPECT_TRUE(db->loadFriends().isEmpty());
}

TEST_F(DatabaseManagerTest, GroupRoundTrip) {
    IPMsgGroup group;
    group.id = "grp-1";
    group.name = "Dev Team";
    group.memberIds = {"u1", "u2", "u3"};
    group.memberNames = {"Alice", "Bob", "Carol"};
    group.createdAt = 12345;
    ASSERT_TRUE(db->saveGroup(group));

    IPMsgGroup loaded = db->loadGroup("grp-1");
    EXPECT_EQ(loaded.id, "grp-1");
    EXPECT_EQ(loaded.name, "Dev Team");
    EXPECT_TRUE(loaded.memberIds == group.memberIds);
    EXPECT_TRUE(loaded.memberNames == group.memberNames);
    EXPECT_EQ(loaded.createdAt, 12345);

    QList<IPMsgGroup> all = db->loadAllGroups();
    ASSERT_EQ(all.size(), 1);
    EXPECT_EQ(all[0].name, "Dev Team");

    // update (member change) round trips
    group.memberNames = {"Alice", "Bob", "Carol", "Dave"};
    ASSERT_TRUE(db->updateGroup(group));
    EXPECT_EQ(db->loadGroup("grp-1").memberNames.size(), 4);

    ASSERT_TRUE(db->deleteGroup("grp-1"));
    EXPECT_TRUE(db->loadAllGroups().isEmpty());
}

TEST_F(DatabaseManagerTest, SettingsRoundTripWithDefault) {
    ASSERT_TRUE(db->setSetting("theme", "dark"));
    ASSERT_TRUE(db->setSetting("font_size", QVariant(14)));

    EXPECT_EQ(db->getSetting("theme").toString(), "dark");
    EXPECT_EQ(db->getSetting("font_size").toInt(), 14);
    EXPECT_EQ(db->getSetting("missing", QVariant(42)).toInt(), 42);
    EXPECT_TRUE(db->getSetting("missing") == QVariant()); // default empty
}

TEST_F(DatabaseManagerTest, RecentChatsOrderedByTimestamp) {
    ASSERT_TRUE(db->updateRecentChat("t1", "Contact A", "hi", 1000, false));
    ASSERT_TRUE(db->updateRecentChat("t2", "Group X", "hello", 3000, true));
    ASSERT_TRUE(db->updateRecentChat("t3", "Contact C", "hey", 2000, false));

    QList<DatabaseManager::RecentChat> chats = db->loadRecentChats(10);
    ASSERT_EQ(chats.size(), 3);
    EXPECT_EQ(chats[0].targetId, "t2");  // newest first
    EXPECT_EQ(chats[1].targetId, "t3");
    EXPECT_EQ(chats[2].targetId, "t1");
    EXPECT_TRUE(chats[0].isGroup);
    EXPECT_FALSE(chats[2].isGroup);

    QList<DatabaseManager::RecentChat> limited = db->loadRecentChats(2);
    ASSERT_EQ(limited.size(), 2);
    EXPECT_EQ(limited[0].targetId, "t2");
}

TEST_F(DatabaseManagerTest, SaveMessageCreatesRecentChatEntry) {
    ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", "hello", 1000), "dev-b", false));

    QList<DatabaseManager::RecentChat> chats = db->loadRecentChats(10);
    ASSERT_EQ(chats.size(), 1);
    EXPECT_EQ(chats[0].targetId, "dev-b");
    EXPECT_EQ(chats[0].lastMessage, "hello");
}

TEST_F(DatabaseManagerTest, TransferRecordRoundTripAndCompletion) {
    DatabaseManager::TransferRecord record;
    record.fileId = "f-1";
    record.fileName = "report.pdf";
    record.fileSize = 2048;
    record.md5 = "abc123";
    record.localPath = "C:/local/report.pdf";
    record.remotePath = "C:/remote/report.pdf";
    record.transferredSize = 0;
    record.status = 1;
    record.startTime = 1000;
    record.endTime = 0;
    record.isSender = true;
    record.targetId = "dev-b";
    record.isGroup = false;
    ASSERT_TRUE(db->saveTransferRecord(record));

    QList<DatabaseManager::TransferRecord> records = db->loadTransferRecords(10);
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].fileName, "report.pdf");
    EXPECT_EQ(records[0].md5, "abc123");
    EXPECT_TRUE(records[0].isSender);

    ASSERT_TRUE(db->updateTransferProgress("f-1", 2048, 2));
    records = db->loadTransferRecords(10);
    EXPECT_EQ(records[0].transferredSize, 2048);
    EXPECT_EQ(records[0].status, 2);
    EXPECT_GT(records[0].endTime, 0);
}

TEST_F(DatabaseManagerTest, OfflineMessageQueueLifecycle) {
    DatabaseManager::OfflineMessage m1;
    m1.id = "om-1";
    m1.targetDeviceId = "dev-b";
    m1.payload = "payload-1";
    m1.retryCount = 0;
    m1.createdAt = 1000;
    m1.expiresAt = QDateTime::currentSecsSinceEpoch() + 3600;
    ASSERT_TRUE(db->saveOfflineMessage(m1));

    DatabaseManager::OfflineMessage m2 = m1;
    m2.id = "om-2";
    m2.payload = "payload-2";
    m2.createdAt = 2000;
    ASSERT_TRUE(db->saveOfflineMessage(m2));

    QList<DatabaseManager::OfflineMessage> pending = db->loadPendingOfflineMessages("dev-b");
    ASSERT_EQ(pending.size(), 2);
    EXPECT_EQ(pending[0].id, "om-1");  // oldest first
    EXPECT_EQ(pending[1].payload, "payload-2");

    // increment retry
    ASSERT_TRUE(db->incrementOfflineRetry("om-1"));
    pending = db->loadPendingOfflineMessages("dev-b");
    ASSERT_EQ(pending.size(), 2);
    EXPECT_EQ(pending[0].retryCount, 1);

    // delivered no longer pending
    ASSERT_TRUE(db->markOfflineMessageDelivered("om-1"));
    pending = db->loadPendingOfflineMessages("dev-b");
    ASSERT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].id, "om-2");
}

TEST_F(DatabaseManagerTest, OfflineMessageExpiredIsExcludedAndCleaned) {
    DatabaseManager::OfflineMessage expired;
    expired.id = "om-expired";
    expired.targetDeviceId = "dev-b";
    expired.payload = "old";
    expired.retryCount = 0;
    expired.createdAt = 1;
    expired.expiresAt = QDateTime::currentSecsSinceEpoch() - 100; // already expired
    ASSERT_TRUE(db->saveOfflineMessage(expired));

    EXPECT_TRUE(db->loadPendingOfflineMessages("dev-b").isEmpty());

    db->cleanupExpiredOfflineMessages();

    QSqlQuery query(QSqlDatabase::database("ipmsg_connection"));
    ASSERT_TRUE(query.exec("SELECT COUNT(*) FROM offline_messages"));
    ASSERT_TRUE(query.next());
    EXPECT_EQ(query.value(0).toInt(), 0);
}

TEST_F(DatabaseManagerTest, OfflineMessagesAreDeviceScoped) {
    DatabaseManager::OfflineMessage m;
    m.id = "om-1";
    m.targetDeviceId = "dev-a";
    m.payload = "for-a";
    m.retryCount = 0;
    m.createdAt = 1000;
    m.expiresAt = QDateTime::currentSecsSinceEpoch() + 3600;
    ASSERT_TRUE(db->saveOfflineMessage(m));

    EXPECT_EQ(db->loadPendingOfflineMessages("dev-b").size(), 0);
    EXPECT_EQ(db->loadPendingOfflineMessages("dev-a").size(), 1);
}

TEST_F(DatabaseManagerTest, DataPersistsAcrossReopen) {
    IPMsgDevice device;
    device.id = "dev-1";
    device.name = "Laptop";
    device.ip = "192.168.1.50";
    ASSERT_TRUE(db->saveDevice(device));
    ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", "persisted", 1000), "dev-b", false));
    ASSERT_TRUE(db->setSetting("theme", "dark"));
    ASSERT_TRUE(db->addFriend("dev-1"));

    QString path = dir->filePath("test.db");
    db->shutdown();
    ASSERT_TRUE(db->initialize(path));

    EXPECT_EQ(db->loadAllDevices().size(), 1);
    EXPECT_EQ(db->loadMessages("dev-b", 50, 0, false).size(), 1);
    EXPECT_EQ(db->getSetting("theme").toString(), "dark");
    EXPECT_TRUE(db->isFriend("dev-1"));
}

TEST_F(DatabaseManagerTest, BuildSyncSnapshotRoundTripsAllTypes) {
    IPMsgDevice device;
    device.id = "dev-sync";
    device.name = "Desktop";
    device.ip = "192.168.1.77";
    device.port = 2425;
    device.lastSeen = 2000;
    ASSERT_TRUE(db->saveDevice(device));

    IPMsgGroup group;
    group.id = "grp-sync";
    group.name = "Family";
    group.memberIds = {"dev-a", "dev-sync"};
    group.memberNames = {"Alice", "Desktop"};
    group.createdAt = 3000;
    ASSERT_TRUE(db->saveGroup(group));

    ASSERT_TRUE(db->setSetting("sync_test_key", "sync_test_value"));
    ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", "sync msg", 4000), "dev-b", false));

    DatabaseManager::SyncSnapshot snap = db->buildSyncSnapshot();
    ASSERT_EQ(snap.devices.size(), 1);
    EXPECT_EQ(snap.devices[0].deviceId, "dev-sync");
    EXPECT_EQ(snap.devices[0].name, "Desktop");

    ASSERT_EQ(snap.groups.size(), 1);
    EXPECT_EQ(snap.groups[0].groupId, "grp-sync");
    EXPECT_EQ(snap.groups[0].memberIds, QList<QString>({"dev-a", "dev-sync"}));

    ASSERT_EQ(snap.settings.size(), 1);
    EXPECT_EQ(snap.settings[0].key, "sync_test_key");
    EXPECT_EQ(snap.settings[0].value, "sync_test_value");

    ASSERT_EQ(snap.messages.size(), 1);
    EXPECT_EQ(snap.messages[0].content, "sync msg");
    EXPECT_EQ(snap.messages[0].targetId, "dev-b");
}

TEST_F(DatabaseManagerTest, BuildSyncSnapshotLimitsMessages) {
    for (int i = 1; i <= 5; ++i) {
        ASSERT_TRUE(db->saveMessage(makeMsg("dev-a", QString("m%1").arg(i), i * 1000), "dev-b", false));
    }
    DatabaseManager::SyncSnapshot snap = db->buildSyncSnapshot(2);
    ASSERT_EQ(snap.messages.size(), 2);  // newest two
    EXPECT_EQ(snap.messages[0].content, "m5");
    EXPECT_EQ(snap.messages[1].content, "m4");
}

TEST_F(DatabaseManagerTest, ApplySyncSnapshotSettingLwwNewerWins) {
    DatabaseManager::SyncSnapshot snap;
    DatabaseManager::SyncSettingRow newer;
    newer.key = "theme";
    newer.value = "dark";
    newer.updatedAt = 9999;
    snap.settings.append(newer);
    ASSERT_EQ(db->applySyncSnapshot(snap), 1);
    EXPECT_EQ(db->getSetting("theme").toString(), "dark");

    // older remote snapshot must not overwrite newer local value
    DatabaseManager::SyncSnapshot olderSnap;
    DatabaseManager::SyncSettingRow older;
    older.key = "theme";
    older.value = "light";
    older.updatedAt = 100;
    olderSnap.settings.append(older);
    ASSERT_EQ(db->applySyncSnapshot(olderSnap), 0);
    EXPECT_EQ(db->getSetting("theme").toString(), "dark");
}

TEST_F(DatabaseManagerTest, ApplySyncSnapshotDeviceMerge) {
    DatabaseManager::SyncSnapshot snap;
    DatabaseManager::SyncDeviceRow row;
    row.deviceId = "dev-sync2";
    row.name = "Remote Laptop";
    row.ip = "192.168.1.99";
    row.port = 2425;
    row.lastSeen = 5000;
    row.updatedAt = 5000;
    row.isFriend = true;
    snap.devices.append(row);
    ASSERT_EQ(db->applySyncSnapshot(snap), 1);

    EXPECT_TRUE(db->isFriend("dev-sync2"));

    // newer local wins
    IPMsgDevice device;
    device.id = "dev-sync2";
    device.name = "Remote Laptop (updated)";
    device.ip = "192.168.1.100";
    ASSERT_TRUE(db->saveDevice(device));
    DatabaseManager::SyncSnapshot staleSnap;
    DatabaseManager::SyncDeviceRow stale = row;
    stale.name = "stale";
    stale.updatedAt = 1;
    staleSnap.devices.append(stale);
    ASSERT_EQ(db->applySyncSnapshot(staleSnap), 0);
    EXPECT_FALSE(db->loadAllDevices().isEmpty());
}

TEST_F(DatabaseManagerTest, ApplySyncSnapshotMessageDedupById) {
    DatabaseManager::SyncSnapshot snap;
    DatabaseManager::SyncMessageRow row;
    row.messageId = "dev-a_1000";
    row.senderId = "dev-a";
    row.senderName = "Alice";
    row.senderIp = "192.168.1.10";
    row.content = "hello sync";
    row.timestamp = 1000;
    row.targetId = "dev-b";
    row.isGroup = false;
    snap.messages.append(row);

    // apply twice -> idempotent (INSERT OR REPLACE), same count
    ASSERT_EQ(db->applySyncSnapshot(snap), 1);
    ASSERT_EQ(db->applySyncSnapshot(snap), 1);

    QList<IPMsgMessage> msgs = db->loadMessages("dev-b", 50, 0, false);
    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].content, "hello sync");
}

TEST_F(DatabaseManagerTest, ApplySyncSnapshotGroupMerge) {
    DatabaseManager::SyncSnapshot snap;
    DatabaseManager::SyncGroupRow row;
    row.groupId = "grp-merge";
    row.name = "Project";
    row.memberIds = {"dev-a"};
    row.memberNames = {"Alice"};
    row.createdAt = 6000;
    row.updatedAt = 6000;
    snap.groups.append(row);
    ASSERT_EQ(db->applySyncSnapshot(snap), 1);

    DatabaseManager::SyncSnapshot stale;
    DatabaseManager::SyncGroupRow staleRow = row;
    staleRow.memberIds = {"dev-a", "dev-b"};
    staleRow.updatedAt = 10;
    stale.groups.append(staleRow);
    ASSERT_EQ(db->applySyncSnapshot(stale), 0);

    DatabaseManager::SyncSnapshot fresher;
    DatabaseManager::SyncGroupRow fresherRow = row;
    fresherRow.memberIds = {"dev-a", "dev-b"};
    fresherRow.updatedAt = 7000;
    fresher.groups.append(fresherRow);
    ASSERT_EQ(db->applySyncSnapshot(fresher), 1);

    DatabaseManager::SyncSnapshot check = db->buildSyncSnapshot();
    bool found = false;
    for (const auto& g : check.groups) {
        if (g.groupId == "grp-merge") {
            found = true;
            EXPECT_EQ(g.memberIds.size(), 2);
        }
    }
    EXPECT_TRUE(found);
}
