#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QTest>
#include "app/file_sync_manager.h"

using namespace xrk;

namespace {
void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
}
} // namespace

class FileSyncTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(FileSyncTest, RelativePathAndRemoteTarget) {
    EXPECT_EQ(FileSyncManager::relativePath("C:/a/b", "C:/a/b/c.txt"), "c.txt");
    EXPECT_EQ(FileSyncManager::relativePath("C:/a/b", "C:/a/b/sub/d.txt"), "sub/d.txt");
    // root not an ancestor -> falls back to basename
    EXPECT_EQ(FileSyncManager::relativePath("C:/x", "C:/a/b/c.txt"), "c.txt");

    QString rt = FileSyncManager::remoteTargetPath("D:/Remote", "sub/d.txt");
    EXPECT_EQ(QDir::fromNativeSeparators(rt), QString("D:/Remote/sub/d.txt"));
    // trailing slash on root is tolerated
    EXPECT_EQ(QDir::fromNativeSeparators(
                  FileSyncManager::remoteTargetPath("D:/Remote/", "file.txt")),
              QString("D:/Remote/file.txt"));
}

TEST_F(FileSyncTest, AddPairDedupeAndRemove) {
    FileSyncManager mgr;
    EXPECT_TRUE(mgr.addPair("C:/local", "C:/remote"));
    EXPECT_FALSE(mgr.addPair("C:/local", "C:/other")); // duplicate local
    EXPECT_TRUE(mgr.hasPair("C:/local"));
    mgr.removePair("C:/local");
    EXPECT_FALSE(mgr.hasPair("C:/local"));
}

// Initial sync uploads existing files; a later rescan uploads only new files,
// mapping each to the correct remote path via its relative location.
TEST_F(FileSyncTest, InitialAndIncrementalUpload) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString local = dir.path();

    QString f1 = local + "/file1.txt";
    writeFile(f1, "hello");

    FileSyncManager mgr;
    QList<QPair<QString, QString>> uploads;
    mgr.setUploadCallback([&](const QString& localFile, const QString& remote) {
        uploads.append({localFile, remote});
        return "id";
    });
    mgr.setCanUpload(true);

    ASSERT_TRUE(mgr.addPair(local, "C:/Remote"));
    mgr.start();

    // Initial scan should queue the pre-existing file.
    QTest::qWait(500);
    QString nf1 = QDir::toNativeSeparators(f1);
    bool sawInitial = false;
    for (const auto& u : uploads) {
        if (u.first == nf1) {
            sawInitial = true;
            EXPECT_EQ(QDir::fromNativeSeparators(u.second), QString("C:/Remote/file1.txt"));
        }
    }
    EXPECT_TRUE(sawInitial);

    // Create a new file and rescan deterministically (no FS-watch timing).
    QString f2 = local + "/file2.txt";
    writeFile(f2, "world");
    mgr.rescan(local);
    QTest::qWait(500);

    QString nf2 = QDir::toNativeSeparators(f2);
    bool sawF2 = false;
    for (const auto& u : uploads) {
        if (u.first == nf2) {
            sawF2 = true;
            EXPECT_EQ(QDir::fromNativeSeparators(u.second), QString("C:/Remote/file2.txt"));
        }
    }
    EXPECT_TRUE(sawF2);

    // Modify f1 -> size/mtime change -> re-uploaded.
    writeFile(f1, "hello world!!");
    mgr.rescan(local);
    QTest::qWait(500);
    int f1Count = 0;
    for (const auto& u : uploads) if (u.first == nf1) ++f1Count;
    EXPECT_GE(f1Count, 2); // once at initial, once after modification

    mgr.stop();
}

// Uploads are dropped (not attempted) while canUpload is false, so a
// disconnected session does not spam the transfer manager.
TEST_F(FileSyncTest, NoUploadWhenDisconnected) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString local = dir.path();
    writeFile(local + "/only.txt", "x");

    FileSyncManager mgr;
    int uploadCalls = 0;
    mgr.setUploadCallback([&](const QString&, const QString&) {
        ++uploadCalls;
        return QString();
    });
    mgr.setCanUpload(false); // disconnected

    ASSERT_TRUE(mgr.addPair(local, "C:/Remote"));
    mgr.start();
    QTest::qWait(500);
    EXPECT_EQ(uploadCalls, 0);

    mgr.stop();
}
