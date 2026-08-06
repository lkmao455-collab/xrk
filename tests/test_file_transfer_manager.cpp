#include <gtest/gtest.h>
#include "app/file_transfer_manager.h"
#include "core/tcp_connection.h"
#include "core/types.h"
#include "core/protocol_manager.h"
#include <QTemporaryFile>
#include <QDir>

using namespace xrk;

class FileTransferManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        connection = nullptr;
        manager = std::make_unique<FileTransferManager>(connection);
    }
    
    void TearDown() override {
        manager.reset();
    }
    
    std::unique_ptr<FileTransferManager> manager;
    TcpConnection* connection;
};

TEST_F(FileTransferManagerTest, UploadFileNotFound) {
    QString result = manager->uploadFile("/nonexistent/file.txt");
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(FileTransferManagerTest, UploadFileSuccess) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content");
    tempFile.close();
    
    QString result = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(result.isEmpty());
    
    QList<FileRequest> transfers = manager->getActiveTransfers();
    EXPECT_EQ(transfers.size(), 1);
    EXPECT_EQ(transfers[0].fileId, result);
}

TEST_F(FileTransferManagerTest, CancelTransfer) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content");
    tempFile.close();
    
    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());
    
    manager->cancelTransfer(fileId);
    EXPECT_FALSE(manager->isTransferActive(fileId));
}

TEST_F(FileTransferManagerTest, PauseResumeTransfer) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content");
    tempFile.close();
    
    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());
    
    manager->pauseTransfer(fileId);
    EXPECT_TRUE(manager->isTransferActive(fileId));
    
    manager->resumeTransfer(fileId);
    EXPECT_TRUE(manager->isTransferActive(fileId));
}

TEST_F(FileTransferManagerTest, GetActiveTransfers) {
    QTemporaryFile tempFile1;
    tempFile1.setAutoRemove(true);
    ASSERT_TRUE(tempFile1.open());
    tempFile1.write("content 1");
    tempFile1.close();
    
    QTemporaryFile tempFile2;
    tempFile2.setAutoRemove(true);
    ASSERT_TRUE(tempFile2.open());
    tempFile2.write("content 2");
    tempFile2.close();
    
    QString fileId1 = manager->uploadFile(tempFile1.fileName());
    QString fileId2 = manager->uploadFile(tempFile2.fileName());
    
    QList<FileRequest> transfers = manager->getActiveTransfers();
    EXPECT_EQ(transfers.size(), 2);
}

TEST_F(FileTransferManagerTest, IsTransferActive) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content");
    tempFile.close();
    
    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_TRUE(manager->isTransferActive(fileId));
    
    manager->cancelTransfer(fileId);
    EXPECT_FALSE(manager->isTransferActive(fileId));
}

TEST_F(FileTransferManagerTest, DownloadFile) {
    QString remotePath = "/remote/file.txt";
    QString localPath = QDir::tempPath() + "/download_test.txt";

    QString fileId = manager->downloadFile(remotePath, localPath);
    EXPECT_FALSE(fileId.isEmpty());

    QList<FileRequest> transfers = manager->getActiveTransfers();
    EXPECT_EQ(transfers.size(), 1);

    manager->cancelTransfer(fileId);

    QFile::remove(localPath);
}

TEST_F(FileTransferManagerTest, GetTransferOffset) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content for offset");
    tempFile.close();

    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());

    EXPECT_EQ(manager->getTransferOffset(fileId), 0);

    manager->cancelTransfer(fileId);
}

TEST_F(FileTransferManagerTest, GetTransferChecksumEmptyForActive) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content");
    tempFile.close();

    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());

    QByteArray checksum = manager->getTransferChecksum(fileId);
    EXPECT_TRUE(checksum.isEmpty());

    manager->cancelTransfer(fileId);
}

TEST_F(FileTransferManagerTest, CanResumeTransfer) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    tempFile.write("test content for resume");
    tempFile.close();

    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());

    EXPECT_FALSE(manager->canResumeTransfer(fileId));

    manager->cancelTransfer(fileId);
}

TEST_F(FileTransferManagerTest, ResumeTransferWithOffset) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    QByteArray data(1024 * 1024, 'A');
    tempFile.write(data);
    tempFile.close();

    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());

    manager->pauseTransfer(fileId);

    QString resumedId = manager->resumeTransfer(fileId, 512 * 1024);
    EXPECT_EQ(resumedId, fileId);

    manager->cancelTransfer(fileId);
}

TEST_F(FileTransferManagerTest, ProtocolFileChecksumRoundTrip) {
    QByteArray checksum(32, 0);
    for (int i = 0; i < 32; ++i) {
        checksum[i] = static_cast<char>(i);
    }
    QString fileId = "test-file-checksum-id";

    QByteArray encoded = ProtocolManager::encodeFileChecksum(checksum, fileId);
    QString decodedFileId;
    QByteArray decodedChecksum = ProtocolManager::decodeFileChecksum(encoded, decodedFileId);

    EXPECT_EQ(decodedFileId, fileId);
    EXPECT_EQ(decodedChecksum, checksum);
}

TEST_F(FileTransferManagerTest, TransferRecordWithChecksumAndOffset) {
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    QByteArray data(4096, 'X');
    tempFile.write(data);
    tempFile.close();

    QString fileId = manager->uploadFile(tempFile.fileName());
    EXPECT_FALSE(fileId.isEmpty());

    QList<FileRequest> transfers = manager->getActiveTransfers();
    EXPECT_EQ(transfers.size(), 1);

    manager->pauseTransfer(fileId);
    manager->resumeTransfer(fileId, 2048);

    manager->cancelTransfer(fileId);
}
