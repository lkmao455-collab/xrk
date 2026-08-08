#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QByteArray>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
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
    void processTcp(IPMsgManager& m, const QByteArray& data, QTcpSocket* socket) {
        m.processTcpCommand(data, socket);
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
    QString genFileId(IPMsgManager& m, const QString& senderId, const QString& md5,
                      const QString& relPath, qint64 size) const {
        return m.generateFileId(senderId, md5, relPath, size);
    }

    // Helpers to set up / inspect the receiver-side wiring that acceptFile()
    // expects, since the test body runs in a derived class without friendship.
    void setIncomingSocket(IPMsgManager& m, const QString& id, QTcpSocket* s) {
        m.m_incomingFileSockets[id] = s;
    }
    void setIncomingChunkSize(IPMsgManager& m, const QString& id, qint64 cs) {
        m.m_incomingChunkSizes[id] = cs;
    }
    qint64 recvFilePos(IPMsgManager& m, const QString& id) const {
        auto* c = m.m_recvContexts.value(id);
        return (c && c->file) ? c->file->pos() : -1;
    }
    qint64 recvFileSize(IPMsgManager& m, const QString& id) const {
        auto* c = m.m_recvContexts.value(id);
        return (c && c->file) ? c->file->size() : -1;
    }
    qint64 recvReceived(IPMsgManager& m, const QString& id) const {
        auto* c = m.m_recvContexts.value(id);
        return c ? c->received : -1;
    }
    int recvChunkIndex(IPMsgManager& m, const QString& id) const {
        auto* c = m.m_recvContexts.value(id);
        return c ? c->chunkIndex : -1;
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

// Sets up a connected socket pair so acceptFile() has a live, open peer socket
// to write its FILE_ACCEPT command to (mirrors the real incoming-connection
// flow without needing a second machine). The server-side socket is reparented
// to nullptr so it survives the local QTcpServer going out of scope.
namespace {
void makeConnectedSocketPair(QTcpSocket*& client, QTcpSocket*& server) {
    QTcpServer srv;
    QSignalSpy newConn(&srv, &QTcpServer::newConnection);
    ASSERT_TRUE(srv.listen(QHostAddress::LocalHost, 0));
    client = new QTcpSocket;
    client->connectToHost(QHostAddress::LocalHost, srv.serverPort());
    ASSERT_TRUE(newConn.wait(5000));
    server = srv.nextPendingConnection();
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->isOpen());
    // Detach from the (soon-to-be-destroyed) server so the socket stays alive.
    server->setParent(nullptr);
    QSignalSpy serverConnected(server, &QTcpSocket::connected);
    if (server->state() != QAbstractSocket::ConnectedState)
        ASSERT_TRUE(serverConnected.wait(5000));
}
} // namespace

// A fresh accept (no persisted state) must start from offset 0: the receiver
// file is opened truncated (WriteOnly) and the FILE_ACCEPT announces offset 0,
// so the sender streams the whole file.
TEST_F(IpmsgTransferTest, AcceptFileFreshTruncatesAndStartsAtZero) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    const QString fileId = "fresh-1";
    const QString savePath = dir.filePath("out.bin");

    // Pre-existing (stale) content on disk that a fresh accept must discard.
    {
        QFile f(savePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(5000, 'X'));
        f.close();
    }

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 5000;
    task.md5 = "abc";
    registerTask(m, task);

    QTcpSocket* client = nullptr;
    QTcpSocket* server = nullptr;
    ASSERT_NO_FATAL_FAILURE(makeConnectedSocketPair(client, server));
    setIncomingSocket(m, fileId, server);

    QSignalSpy resumingSpy(&m, &IPMsgManager::fileResuming);

    m.acceptFile(fileId, savePath);

    // FILE_ACCEPT must announce offset 0 (no resume).
    ASSERT_TRUE(client->waitForReadyRead(5000));
    QJsonDocument doc = QJsonDocument::fromJson(client->readAll());
    ASSERT_FALSE(doc.isNull());
    EXPECT_EQ(doc.object()["command"].toString(), QString::fromUtf8("FILE_ACCEPT"));
    EXPECT_EQ(doc.object()["offset"].toVariant().toLongLong(), 0);

    EXPECT_EQ(resumingSpy.count(), 0); // fresh, no resume signal
    EXPECT_EQ(recvFilePos(m, fileId), 0);
    EXPECT_EQ(recvReceived(m, fileId), 0);
    // WriteOnly truncates the stale 5000-byte file down to 0.
    EXPECT_EQ(recvFileSize(m, fileId), 0);

    delete client;
    delete server;
}

// The core 断点续传 behaviour: when a partial download (2000 of 5000 bytes,
// chunk size 1000) was persisted, re-accepting must open the receiver file in
// ReadWrite, seek to 2000, align chunkIndex to 2, announce offset 2000 in
// FILE_ACCEPT (so the SENDER skips the bytes we already have), and checkpoint
// the receiver-side state with isSender=false.
TEST_F(IpmsgTransferTest, AcceptFilePerformsRealResume) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    const QString fileId = "resume-acc";
    const QString savePath = dir.filePath("out.bin");
    const qint64 chunkSize = 1000;
    const qint64 offset = 2000;

    // Pre-write the already-received prefix to disk.
    {
        QFile f(savePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(static_cast<int>(offset), 'P'));
        f.close();
    }

    IPMsgTransferTask task;
    task.fileId = fileId;
    task.direction = TransferDirection::Receive;
    task.totalSize = 5000;
    task.md5 = "abc";
    registerTask(m, task);

    // Persisted bookmark from a previous session (receiver side).
    IPMsgTransferState st;
    st.fileId = fileId;
    st.totalSize = 5000;
    st.md5 = "abc";
    st.lastOffset = offset;
    st.isSender = false;
    saveState(m, st);

    // Tell acceptFile the chunk size so chunkIndex alignment is meaningful.
    setIncomingChunkSize(m, fileId, chunkSize);

    QTcpSocket* client = nullptr;
    QTcpSocket* server = nullptr;
    ASSERT_NO_FATAL_FAILURE(makeConnectedSocketPair(client, server));
    setIncomingSocket(m, fileId, server);

    QSignalSpy resumingSpy(&m, &IPMsgManager::fileResuming);

    m.acceptFile(fileId, savePath);

    // FILE_ACCEPT announces the resume offset so the sender skips ahead.
    ASSERT_TRUE(client->waitForReadyRead(5000));
    QJsonDocument doc = QJsonDocument::fromJson(client->readAll());
    ASSERT_FALSE(doc.isNull());
    EXPECT_EQ(doc.object()["command"].toString(), QString::fromUtf8("FILE_ACCEPT"));
    EXPECT_EQ(doc.object()["offset"].toVariant().toLongLong(), offset);

    // fileResuming emitted with the right offset.
    ASSERT_EQ(resumingSpy.count(), 1);
    EXPECT_EQ(resumingSpy.takeFirst().at(1).toLongLong(), offset);

    // Receiver file opened ReadWrite and positioned at the offset.
    EXPECT_EQ(recvFilePos(m, fileId), offset);
    EXPECT_EQ(recvReceived(m, fileId), offset);
    EXPECT_EQ(recvChunkIndex(m, fileId), static_cast<int>(offset / chunkSize));
    // Prefix preserved (ReadWrite, not truncated).
    EXPECT_EQ(recvFileSize(m, fileId), offset);

    // Receiver-side state check-pointed for a later (post-restart) resume.
    IPMsgTransferState persisted = getState(m, fileId);
    EXPECT_EQ(persisted.lastOffset, offset);
    EXPECT_FALSE(persisted.isSender);

    delete client;
    delete server;
}

// A re-sent file must produce a stable fileId (derived from sender+md5+path+
// size) so the receiver's persisted resume bookmark still matches after a
// restart. This is what makes 断点续传 work across process restarts rather
// than only within a single session.
TEST_F(IpmsgTransferTest, DeterministicFileIdEnablesCrossRestartResume) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    const QString sender = "userA";
    const QString md5 = "abc";
    const QString rel = "doc/file.bin";
    const qint64 size = 5000;

    // Same inputs -> identical id (stable across "restarts").
    const QString id1 = genFileId(m, sender, md5, rel, size);
    const QString id2 = genFileId(m, sender, md5, rel, size);
    EXPECT_EQ(id1, id2);
    EXPECT_FALSE(id1.isEmpty());

    // Different content -> different id.
    EXPECT_NE(id1, genFileId(m, sender, "other", rel, size));
    EXPECT_NE(id1, genFileId(m, sender, md5, "doc/other.bin", size));

    // Receiver persisted a bookmark from a previous session, keyed by the very
    // same deterministic id a fresh re-send will produce.
    IPMsgTransferState st;
    st.fileId = id1;
    st.totalSize = size;
    st.md5 = md5;
    st.lastOffset = 2000;
    st.isSender = false;
    saveState(m, st);

    IPMsgTransferTask task;
    task.fileId = id1;
    task.direction = TransferDirection::Receive;
    task.totalSize = size;
    task.md5 = md5;
    registerTask(m, task);

    // The receiver recognises the re-sent file and resumes from the bookmark.
    EXPECT_EQ(resumeOffsetOf(m, id1), 2000);
}

// When a sender auto-retries a failed transfer it re-sends FILE_START. If the
// receiver already holds a partial-download bookmark for that file, it must
// accept and resume silently (no user prompt) and tell the sender to seek to
// the bookmark offset. This is what makes 断点续传 fully automatic end-to-end.
TEST_F(IpmsgTransferTest, RetriedFileStartAutoResumesWithoutPrompt) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    IPMsgManager m;
    m.setTransferStateFile(dir.filePath("transfers.json"));

    const QString fileId = "auto-resume-1";
    const qint64 total = 5000;
    const qint64 offset = 2000;
    const QString savePath = dir.filePath("partial.bin");

    // Pre-write the already-received prefix so the receiver can safely seek.
    {
        QFile f(savePath);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(static_cast<int>(offset), 'P'));
        f.close();
    }

    // Persisted receiver bookmark from the interrupted attempt.
    IPMsgTransferState st;
    st.fileId = fileId;
    st.totalSize = total;
    st.md5 = "abc";
    st.lastOffset = offset;
    st.savePath = savePath;
    st.isSender = false;
    saveState(m, st);

    QSignalSpy promptSpy(&m, &IPMsgManager::fileReceiveRequest);

    // Connected socket pair: the "server" socket stands in for the receiver's
    // incoming connection; the "client" simulates the retrying sender.
    QTcpSocket* client = nullptr;
    QTcpSocket* server = nullptr;
    ASSERT_NO_FATAL_FAILURE(makeConnectedSocketPair(client, server));

    QJsonObject start;
    start["command"] = QString::fromUtf8("FILE_START");
    start["fileId"] = fileId;
    start["fileName"] = "partial.bin";
    start["filePath"] = "";          // relative path (single file)
    start["fileSize"] = total;
    start["isDirectory"] = false;
    start["senderId"] = "userA";
    start["senderName"] = "Alice";
    start["md5"] = "abc";
    start["chunkSize"] = 1000;
    processTcp(m, QJsonDocument(start).toJson(), server);

    // No manual prompt: resume was automatic.
    EXPECT_EQ(promptSpy.count(), 0);

    // The receiver answered with FILE_ACCEPT carrying the resume offset.
    ASSERT_TRUE(client->waitForReadyRead(5000));
    QJsonDocument doc = QJsonDocument::fromJson(client->readAll());
    ASSERT_FALSE(doc.isNull());
    EXPECT_EQ(doc.object()["command"].toString(), QString::fromUtf8("FILE_ACCEPT"));
    EXPECT_EQ(doc.object()["offset"].toVariant().toLongLong(), offset);

    // Receiver context positioned at the offset, ready to receive the remainder.
    EXPECT_EQ(recvFilePos(m, fileId), offset);
    EXPECT_EQ(recvReceived(m, fileId), offset);

    delete client;
    delete server;
}

} // namespace xrk
