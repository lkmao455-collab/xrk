#include <gtest/gtest.h>
#include "core/audit_logger.h"
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QDateTime>

using namespace xrk;

class AuditLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tempDir.isValid());
    }

    void TearDown() override {
    }

    QTemporaryDir m_tempDir;
};

TEST_F(AuditLoggerTest, ConstructorCreatesLogFile) {
    AuditLogger logger(m_tempDir.path());
    QString path = logger.currentLogPath();
    EXPECT_TRUE(path.startsWith(m_tempDir.path()));
    EXPECT_TRUE(path.contains("audit_"));
    EXPECT_TRUE(path.endsWith(".log"));
    EXPECT_TRUE(QFile::exists(path));
}

TEST_F(AuditLoggerTest, LogAllTypes) {
    {
        AuditLogger logger(m_tempDir.path());
        logger.logConnection("client-1", "connected", "from 127.0.0.1");
        logger.logAuth("client-1", true, "auto");
        logger.logSession("session-1", "device-1", "start");
        logger.logOperation("client-1", "send_file", "file.txt");
        logger.logError("client-1", "timeout");
    }

    AuditLogger reader(m_tempDir.path());
    QJsonArray entries = reader.recentEntries(10);
    EXPECT_EQ(entries.size(), 5);

    QJsonObject first = entries.at(0).toObject();
    EXPECT_TRUE(first.value("type").toString() == "error");
    EXPECT_TRUE(first.value("clientId").toString() == "client-1");

    bool foundAuth = false;
    for (const QJsonValue& value : entries) {
        QJsonObject obj = value.toObject();
        if (obj.value("type").toString() == "auth") {
            foundAuth = true;
            EXPECT_TRUE(obj.value("success").toBool());
            EXPECT_TRUE(obj.value("details").toString() == "auto");
        }
    }
    EXPECT_TRUE(foundAuth);
}

TEST_F(AuditLoggerTest, RecentEntriesLimitNewestFirst) {
    {
        AuditLogger logger(m_tempDir.path());
        logger.logConnection("c1", "connected");
        logger.logOperation("c2", "op-a");
        logger.logOperation("c3", "op-b");
    }

    AuditLogger reader(m_tempDir.path());
    QJsonArray entries = reader.recentEntries(2);
    EXPECT_EQ(entries.size(), 2);
    EXPECT_TRUE(entries.at(0).toObject().value("operation").toString() == "op-b");
    EXPECT_TRUE(entries.at(1).toObject().value("operation").toString() == "op-a");
}

TEST_F(AuditLoggerTest, EntriesSinceFilter) {
    {
        AuditLogger logger(m_tempDir.path());
        logger.logConnection("c1", "connected");
        logger.logOperation("c2", "op");
    }

    QDateTime now = QDateTime::currentDateTime();
    AuditLogger reader(m_tempDir.path());
    QJsonArray all = reader.entriesSince(now.addSecs(-3600));
    EXPECT_EQ(all.size(), 2);

    QJsonArray future = reader.entriesSince(now.addSecs(3600));
    EXPECT_TRUE(future.isEmpty());
}

TEST_F(AuditLoggerTest, EntryAddedSignal) {
    AuditLogger logger(m_tempDir.path());
    QSignalSpy spy(&logger, &AuditLogger::entryAdded);
    logger.logOperation("c1", "op");
    logger.logError("c1", "err");
    EXPECT_EQ(spy.count(), 2);

    QJsonObject entry = spy.at(0).at(0).value<QJsonObject>();
    EXPECT_TRUE(entry.value("operation").toString() == "op");
}

TEST_F(AuditLoggerTest, SetLogDirectory) {
    AuditLogger logger(m_tempDir.path());
    logger.logOperation("c1", "before");

    QTemporaryDir otherDir;
    ASSERT_TRUE(otherDir.isValid());
    logger.setLogDirectory(otherDir.path());

    QString newPath = logger.currentLogPath();
    EXPECT_TRUE(newPath.startsWith(otherDir.path()));
    EXPECT_FALSE(newPath.startsWith(m_tempDir.path()));

    logger.logOperation("c2", "after");

    AuditLogger reader(otherDir.path());
    EXPECT_EQ(reader.recentEntries(10).size(), 1);
    EXPECT_TRUE(reader.recentEntries(10).at(0).toObject().value("operation").toString() == "after");
}

TEST_F(AuditLoggerTest, ClearOldLogs) {
    {
        AuditLogger logger(m_tempDir.path());
        logger.logOperation("c1", "op");
    }

    QFile oldFile(m_tempDir.path() + "/audit_20200101.log");
    ASSERT_TRUE(oldFile.open(QIODevice::WriteOnly | QIODevice::Text));
    oldFile.write("{}\n");
    oldFile.close();
    EXPECT_TRUE(QFile::exists(m_tempDir.path() + "/audit_20200101.log"));

    AuditLogger logger(m_tempDir.path());
    logger.clearOldLogs(30);

    EXPECT_FALSE(QFile::exists(m_tempDir.path() + "/audit_20200101.log"));

    QString current = logger.currentLogPath();
    EXPECT_TRUE(QFile::exists(current));
}
