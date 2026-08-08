#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QByteArray>
#include <QCryptographicHash>
#include "ipmsg_manager.h"

namespace xrk {

// IpmsgTransferTest is a friend of IPMsgManager (see ipmsg_manager.h). Friendship
// is not inherited by gtest's generated test subclasses, so all private-member
// access is funnelled through these helper methods defined on the fixture.
class IpmsgTransferTest : public ::testing::Test {
protected:
    IPMsgManager::SendContext* makeSendContext(const QString& fileId, int attempts) {
        IPMsgManager::SendContext* ctx = new IPMsgManager::SendContext();
        ctx->fileId = fileId;
        ctx->targetIp = "192.168.255.254"; // non-routable; connection simply fails
        ctx->relativePath = "file.bin";
        ctx->md5 = "md5";
        ctx->totalSize = 100;
        ctx->attempts = attempts;

        IPMsgFileItem item;
        item.name = "file.bin";
        item.absolutePath = "C:/tmp/file.bin";
        item.relativePath = "file.bin";
        item.size = 100;
        item.md5 = "md5";
        item.chunkSize = 1024 * 1024;
        ctx->item = item;
        return ctx;
    }

    IPMsgManager::RecvContext* makeRecvContext(const QString& fileId, const QByteArray& goodChunk) {
        IPMsgManager::RecvContext* c = new IPMsgManager::RecvContext();
        c->fileId = fileId;
        c->totalSize = goodChunk.size();
        c->chunkSize = goodChunk.size();
        c->chunkMd5s = QList<QByteArray>() << QCryptographicHash::hash(goodChunk, QCryptographicHash::Md5);
        c->chunkIndex = 0;
        return c;
    }

    // Length-prefixed frame (4-byte LE length + payload), as sent on the wire.
    static QByteArray frame(const QByteArray& payload) {
        QByteArray out;
        quint32 len = static_cast<quint32>(payload.size());
        len = qToLittleEndian(len);
        out.resize(4);
        memcpy(out.data(), &len, 4);
        out.append(payload);
        return out;
    }

    void putSendContext(IPMsgManager& m, IPMsgManager::SendContext* ctx) {
        m.m_activeSenders[ctx->fileId] = ctx;
    }
    void putRecvContext(IPMsgManager& m, IPMsgManager::RecvContext* c) {
        m.m_recvContexts[c->fileId] = c;
    }
    void registerTask(IPMsgManager& m, const IPMsgTransferTask& t) { m.registerTask(t); }
    void fail(IPMsgManager& m, const QString& id, const QString& reason) {
        m.handleSendFailure(id, reason);
    }
    void feed(IPMsgManager& m, const QString& id, const QByteArray& data) {
        m.writeReceivedData(id, data);
    }
    bool hasSend(IPMsgManager& m, const QString& id) const { return m.m_activeSenders.contains(id); }
    bool hasRecv(IPMsgManager& m, const QString& id) const { return m.m_recvContexts.contains(id); }
    int taskAttempts(IPMsgManager& m, const QString& id) const {
        return m.m_tasks.value(id).attempts;
    }
    TransferStatus taskStatus(IPMsgManager& m, const QString& id) const {
        return m.m_tasks.value(id).status;
    }
    // Funnel the remaining private-member access through base-class helpers too
    // (friendship is not inherited by gtest's generated TestBody subclass).
    void processTcp(IPMsgManager& m, const QByteArray& data) {
        m.processTcpCommand(data, nullptr);
    }
    void saveState(IPMsgManager& m, const IPMsgTransferState& s) {
        m.saveTransferState(s);
    }
    bool hasState(IPMsgManager& m, const QString& id) const {
        return m.hasTransferState(id);
    }
    IPMsgTransferState getState(IPMsgManager& m, const QString& id) const {
        return m.getTransferState(id);
    }
    qint64 resumeOffsetOf(IPMsgManager& m, const QString& id) const {
        return m.resumeOffsetFor(id);
    }
    QList<IPMsgTransferState> resumableTransfers(IPMsgManager& m) const {
        return m.getResumableTransfers();
    }
};

// A transient send failure (connection drop) schedules exactly one retry with
// exponential backoff, bumps the attempt counter and returns the task to Queued.
TEST_F(IpmsgTransferTest, TransientFailureSchedulesRetryWithBackoff) {
    IPMsgManager m;
    const QString fileId = "send-1";

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Send;
    task.fileName = "file.bin";
    task.totalSize = 100;
    task.status = TransferStatus::Transferring;
    task.peerId = "peer";
    registerTask(m, task);
    putSendContext(m, makeSendContext(fileId, 1));

    QSignalSpy retrySpy(&m, &IPMsgManager::fileRetryScheduled);
    QSignalSpy errSpy(&m, &IPMsgManager::fileError);

    fail(m, fileId, "Connection closed");

    ASSERT_EQ(retrySpy.count(), 1);
    EXPECT_EQ(errSpy.count(), 0); // not a terminal failure yet

    // attempt=2, base 2000ms -> 2000 * 2^(2-1) = 4000ms
    QList<QVariant> args = retrySpy.takeFirst();
    EXPECT_EQ(args.at(0).toString(), fileId);
    EXPECT_EQ(args.at(1).toInt(), 2);
    EXPECT_EQ(args.at(2).toInt(), 4000);

    EXPECT_EQ(taskAttempts(m, fileId), 2);
    EXPECT_EQ(taskStatus(m, fileId), TransferStatus::Queued);
    EXPECT_FALSE(hasSend(m, fileId)); // old context torn down
}

// After the final allowed attempt the transfer must fail terminally instead of
// scheduling another retry.
TEST_F(IpmsgTransferTest, ExhaustedRetriesFailTerminally) {
    IPMsgManager m;
    const QString fileId = "send-2";
    EXPECT_EQ(m.maxRetries(), 3); // <=3 attempts total

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Send;
    task.fileName = "file.bin";
    task.totalSize = 100;
    task.status = TransferStatus::Transferring;
    registerTask(m, task);
    putSendContext(m, makeSendContext(fileId, 3)); // last allowed attempt

    QSignalSpy retrySpy(&m, &IPMsgManager::fileRetryScheduled);
    QSignalSpy errSpy(&m, &IPMsgManager::fileError);

    fail(m, fileId, "Connection error");

    EXPECT_EQ(retrySpy.count(), 0);
    ASSERT_EQ(errSpy.count(), 1);
    EXPECT_EQ(taskStatus(m, fileId), TransferStatus::Failed);
}

// First-chunk backoff scales with the attempt number (attempt 1 -> 2s,
// attempt 2 -> 4s) so the exponential schedule is honoured.
TEST_F(IpmsgTransferTest, BackoffGrowsExponentially) {
    IPMsgManager m;
    const QString fileId = "send-3";
    IPMsgTransferTask task;
    task.fileId = fileId;
    task.status = TransferStatus::Transferring;
    registerTask(m, task);
    putSendContext(m, makeSendContext(fileId, 2));

    QSignalSpy retrySpy(&m, &IPMsgManager::fileRetryScheduled);
    fail(m, fileId, "drop");
    ASSERT_EQ(retrySpy.count(), 1);
    // attempt 2 failing -> next attempt 3 -> 2000 * 2^(3-1) = 8000ms
    EXPECT_EQ(retrySpy.takeFirst().at(2).toInt(), 8000);
}

// A correct chunk passes verification, advances the pointer and is written.
TEST_F(IpmsgTransferTest, GoodChunkVerifiesAndWrites) {
    IPMsgManager m;
    const QString fileId = "recv-1";
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("out.bin");

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 5;
    registerTask(m, task);

    auto c = makeRecvContext(fileId, QByteArray("hello"));
    c->file = new QFile(path, &m);
    ASSERT_TRUE(c->file->open(QIODevice::WriteOnly));
    putRecvContext(m, c);

    QSignalSpy errSpy(&m, &IPMsgManager::fileError);
    feed(m, fileId, frame(QByteArray("hello")));

    EXPECT_EQ(errSpy.count(), 0);
    EXPECT_EQ(c->received, 5);
    EXPECT_EQ(c->chunkIndex, 1);
    c->file->close();
    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    EXPECT_EQ(check.readAll(), QByteArray("hello"));
}

// A corrupt chunk is detected by its MD5, surfaces as fileError and fails the
// receive so corrupt data is never silently committed to disk.
TEST_F(IpmsgTransferTest, CorruptChunkDetectedAndRejected) {
    IPMsgManager m;
    const QString fileId = "recv-2";
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("out.bin");

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 5;
    registerTask(m, task);

    auto c = makeRecvContext(fileId, QByteArray("hello"));
    c->file = new QFile(path, &m);
    ASSERT_TRUE(c->file->open(QIODevice::WriteOnly));
    putRecvContext(m, c);

    QSignalSpy errSpy(&m, &IPMsgManager::fileError);
    QSignalSpy doneSpy(&m, &IPMsgManager::fileCompleted);
    feed(m, fileId, frame(QByteArray("Xello"))); // tamper

    ASSERT_EQ(errSpy.count(), 1);
    ASSERT_EQ(doneSpy.count(), 1);
    EXPECT_FALSE(doneSpy.takeFirst().at(2).toBool()); // integrityOk == false
    EXPECT_FALSE(hasRecv(m, fileId));
    // Nothing was written because the corrupt chunk was rejected.
    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    EXPECT_TRUE(check.readAll().isEmpty());
}

// ---- Resumable transfer (断点续传) ----

// A transfer interrupted midway and later resumed must reassemble the file
// byte-for-byte from the persisted offset. We simulate the "already received
// the first chunk" state by seeking the receiver file to chunk 1 and feeding
// the remaining chunks, then assert the full file matches the source.
TEST_F(IpmsgTransferTest, ResumedReceiveReassemblesFromOffset) {
    IPMsgManager m;
    const QString fileId = "resume-1";
    const int chunkSize = 1000;
    QByteArray full;
    full.resize(3000);
    for (int i = 0; i < full.size(); ++i) full[i] = static_cast<char>(i % 251);

    QList<QByteArray> chunkMd5s;
    for (int i = 0; i < full.size(); i += chunkSize)
        chunkMd5s << QCryptographicHash::hash(full.mid(i, chunkSize), QCryptographicHash::Md5);

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("out.bin");

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = full.size();
    registerTask(m, task);

    auto c = makeRecvContext(fileId, QByteArray("hello"));
    c->totalSize = full.size();
    c->chunkMd5s = chunkMd5s;
    c->chunkSize = chunkSize;
    c->chunkIndex = 0;
    c->received = 0;
    c->file = new QFile(path, &m);
    ASSERT_TRUE(c->file->open(QIODevice::ReadWrite));
    putRecvContext(m, c);

    QSignalSpy errSpy(&m, &IPMsgManager::fileError);

    // First chunk (bytes [0,1000)) already landed on disk in a previous session.
    c->file->write(full.mid(0, chunkSize));
    c->received = chunkSize;
    c->chunkIndex = 1;
    // A resumed session reopens the file; re-seek to the persisted offset.
    ASSERT_TRUE(c->file->seek(chunkSize));

    // Resume: receive chunk 1 and chunk 2.
    feed(m, fileId, frame(full.mid(1000, chunkSize)));
    EXPECT_EQ(c->received, 2000);
    EXPECT_EQ(c->chunkIndex, 2);

    feed(m, fileId, frame(full.mid(2000, chunkSize)));
    EXPECT_EQ(c->received, 3000);
    EXPECT_EQ(c->chunkIndex, 3);

    EXPECT_EQ(errSpy.count(), 0);
    c->file->close();

    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    EXPECT_EQ(check.size(), full.size());
    EXPECT_EQ(check.readAll(), full);
}

// The IPMSG_FILE_CHUNK_DATA command header must seek the receiver file to the
// declared offset so the following framed chunk is written at the right place
// (not appended). This is the exact entry point a resumed sender drives.
TEST_F(IpmsgTransferTest, ChunkDataHeaderSeeksReceiverFile) {
    IPMsgManager m;
    const QString fileId = "resume-2";
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("out.bin");

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 2000;
    registerTask(m, task);

    auto c = makeRecvContext(fileId, QByteArray("hello"));
    c->totalSize = 2000;
    // Empty digest list: skip per-chunk MD5 so we only exercise offset placement.
    c->chunkMd5s.clear();
    c->file = new QFile(path, &m);
    ASSERT_TRUE(c->file->open(QIODevice::WriteOnly));
    putRecvContext(m, c);

    const qint64 offset = 1234;
    QJsonObject json;
    json["command"] = QString::fromUtf8("FILE_CHUNK_DATA");
    json["fileId"] = fileId;
    json["offset"] = offset;
    processTcp(m, QJsonDocument(json).toJson());

    ASSERT_EQ(c->file->pos(), offset);

    // The framed chunk that follows the header lands at the seeked offset.
    // Simulate that the first `offset` bytes were already received before resume.
    c->received = offset;
    QByteArray payload("payload!");
    feed(m, fileId, frame(payload));
    EXPECT_EQ(c->received, offset + payload.size());

    c->file->close();
    QFile check(path);
    ASSERT_TRUE(check.open(QIODevice::ReadOnly));
    QByteArray data = check.readAll();
    ASSERT_EQ(data.size(), offset + payload.size());
    EXPECT_EQ(data.mid(static_cast<int>(offset)), payload);
}

// Persisted transfer state (the resume bookmark) must survive a manager
// restart: after saving on one instance and reloading on a fresh one pointed at
// the same file, the resume offset is preserved.
TEST_F(IpmsgTransferTest, PersistedTransferStateSurvivesRestart) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString stateFile = dir.filePath("transfers.json");

    {
        IPMsgManager m1;
        m1.setTransferStateFile(stateFile);

        IPMsgTransferState state;
        state.fileId = "resume-x";
        state.filePath = "C:/tmp/big.bin";
        state.savePath = "";
        state.totalSize = 5000000;
        state.transferredSize = 2000000;
        state.md5 = "deadbeef";
        state.isDirectory = false;
        state.isSender = true;
        state.lastOffset = 2000000;
        saveState(m1, state);

        EXPECT_TRUE(hasState(m1, "resume-x"));
        EXPECT_EQ(getState(m1, "resume-x").lastOffset, 2000000);
    }

    // A brand new manager "restarting" with the same state file.
    IPMsgManager m2;
    m2.setTransferStateFile(stateFile);

    ASSERT_TRUE(hasState(m2, "resume-x"));
    IPMsgTransferState restored = getState(m2, "resume-x");
    EXPECT_EQ(restored.fileId, QString("resume-x"));
    EXPECT_EQ(restored.totalSize, 5000000);
    EXPECT_EQ(restored.lastOffset, 2000000);
    EXPECT_EQ(restored.md5, QString("deadbeef"));
    EXPECT_TRUE(restored.isSender);
}

// When a partial download was persisted, re-accepting the same file must resume
// from the saved offset; a fresh accept (no state, or md5/size mismatch) starts
// at zero. This is what makes 断点续传 actually trigger on re-accept.
TEST_F(IpmsgTransferTest, AcceptResumesFromPersistedOffset) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    const QString fileId = "acc-1";
    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 5000;
    task.md5 = "abc";
    registerTask(m, task);

    // No saved state -> start from zero.
    EXPECT_EQ(resumeOffsetOf(m, fileId), 0);

    // Matching md5/size with a non-zero offset -> resume from there.
    IPMsgTransferState st;
    st.fileId = fileId;
    st.totalSize = 5000;
    st.md5 = "abc";
    st.lastOffset = 2000;
    saveState(m, st);
    EXPECT_EQ(resumeOffsetOf(m, fileId), 2000);

    // md5 mismatch -> must NOT resume (would corrupt the file).
    st.md5 = "wrong";
    saveState(m, st);
    EXPECT_EQ(resumeOffsetOf(m, fileId), 0);

    // size mismatch -> must NOT resume.
    st.md5 = "abc";
    st.totalSize = 9999;
    saveState(m, st);
    EXPECT_EQ(resumeOffsetOf(m, fileId), 0);
}

// After a restart, incomplete persisted transfers should be surfaced as
// resumable, while completed ones and currently-active tasks must be excluded.
TEST_F(IpmsgTransferTest, ResumableTransfersExcludesCompleteAndActive) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    IPMsgTransferState partial;
    partial.fileId = "p1";
    partial.totalSize = 5000;
    partial.lastOffset = 2000; // incomplete
    partial.md5 = "x";
    saveState(m, partial);

    IPMsgTransferState done;
    done.fileId = "c1";
    done.totalSize = 5000;
    done.lastOffset = 5000; // already complete
    done.md5 = "y";
    saveState(m, done);

    auto list = resumableTransfers(m);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list.first().fileId, QString("p1"));

    // An active task for p1 must hide it from the resume list.
    IPMsgTransferTask t;
    t.fileId = "p1";
    t.status = TransferStatus::Transferring;
    registerTask(m, t);
    EXPECT_TRUE(resumableTransfers(m).isEmpty());
}

} // namespace xrk
