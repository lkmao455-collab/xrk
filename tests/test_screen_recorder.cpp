#include <gtest/gtest.h>
#include "app/screen_recorder.h"
#include <QTemporaryDir>
#include <QFile>

using namespace xrk;

class ScreenRecorderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(m_tempDir.isValid());
    }

    void TearDown() override {
    }

    static QByteArray frameData(int size) {
        QByteArray data;
        data.reserve(size);
        for (int i = 0; i < size; ++i) {
            data.append(static_cast<char>(i % 251));
        }
        return data;
    }

    static uint32_t readUint32LE(const QByteArray& buf, int offset) {
        return static_cast<unsigned char>(buf[offset]) |
               (static_cast<unsigned char>(buf[offset + 1]) << 8) |
               (static_cast<unsigned char>(buf[offset + 2]) << 16) |
               (static_cast<unsigned char>(buf[offset + 3]) << 24);
    }

    QTemporaryDir m_tempDir;
};

TEST_F(ScreenRecorderTest, StartStopBasic) {
    QString path = m_tempDir.path() + "/test.avi";
    ScreenRecorder recorder;
    EXPECT_TRUE(recorder.startRecording(path, 640, 480, 30));
    EXPECT_TRUE(recorder.isRecording());
    EXPECT_TRUE(recorder.filePath() == path);
    recorder.stopRecording();
    EXPECT_FALSE(recorder.isRecording());
    EXPECT_TRUE(QFile::exists(path));
}

TEST_F(ScreenRecorderTest, StartInvalidPath) {
    ScreenRecorder recorder;
    EXPECT_FALSE(recorder.startRecording("Z:/no/such/dir/out.avi", 640, 480, 30));
    EXPECT_FALSE(recorder.isRecording());
}

TEST_F(ScreenRecorderTest, AddFrameWhenNotRecording) {
    QString path = m_tempDir.path() + "/empty.avi";
    ScreenRecorder recorder;
    recorder.addFrame(frameData(100));
    recorder.stopRecording();
    EXPECT_FALSE(QFile::exists(path));
}

TEST_F(ScreenRecorderTest, AddEmptyFrameNoop) {
    QString path = m_tempDir.path() + "/frame.avi";
    ScreenRecorder recorder;
    ASSERT_TRUE(recorder.startRecording(path, 320, 240, 15));
    recorder.addFrame(QByteArray());
    recorder.addFrame(frameData(200));
    recorder.stopRecording();

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray buf = file.readAll();
    file.close();

    EXPECT_TRUE(buf.size() > 0);
    EXPECT_EQ(buf.left(4), QByteArray("RIFF"));
    EXPECT_EQ(buf.mid(8, 4), QByteArray("AVI "));
    EXPECT_EQ(readUint32LE(buf, 48), 1u);
}

TEST_F(ScreenRecorderTest, AviStructure) {
    QString path = m_tempDir.path() + "/struct.avi";
    ScreenRecorder recorder;
    ASSERT_TRUE(recorder.startRecording(path, 640, 480, 30));
    recorder.addFrame(frameData(100));
    recorder.addFrame(frameData(101));
    recorder.addFrame(frameData(200));
    recorder.stopRecording();

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray buf = file.readAll();
    file.close();

    EXPECT_EQ(buf.left(4), QByteArray("RIFF"));
    EXPECT_EQ(buf.mid(8, 4), QByteArray("AVI "));
    EXPECT_TRUE(buf.contains("hdrl"));
    EXPECT_TRUE(buf.contains("movi"));
    EXPECT_TRUE(buf.contains("idx1"));
    EXPECT_TRUE(buf.contains("00dc"));

    uint32_t riffSize = readUint32LE(buf, 4);
    EXPECT_EQ(riffSize, static_cast<uint32_t>(buf.size()) - 8);
    EXPECT_EQ(readUint32LE(buf, 48), 3u);
}

TEST_F(ScreenRecorderTest, FpsClamped) {
    QString path = m_tempDir.path() + "/fps.avi";
    ScreenRecorder recorder;
    ASSERT_TRUE(recorder.startRecording(path, 100, 100, 0));
    recorder.stopRecording();

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray buf = file.readAll();
    file.close();

    uint32_t microSecPerFrame = readUint32LE(buf, 32);
    EXPECT_EQ(microSecPerFrame, 1000000u);

    ScreenRecorder recorder2;
    QString path2 = m_tempDir.path() + "/fps2.avi";
    ASSERT_TRUE(recorder2.startRecording(path2, 100, 100, 100));
    recorder2.stopRecording();

    QFile file2(path2);
    ASSERT_TRUE(file2.open(QIODevice::ReadOnly));
    QByteArray buf2 = file2.readAll();
    file2.close();

    uint32_t microSecPerFrame2 = readUint32LE(buf2, 32);
    EXPECT_EQ(microSecPerFrame2, 1000000u / 60u);
}

TEST_F(ScreenRecorderTest, RestartRecording) {
    QString path = m_tempDir.path() + "/restart.avi";
    ScreenRecorder recorder;
    ASSERT_TRUE(recorder.startRecording(path, 320, 240, 30));
    recorder.addFrame(frameData(100));
    EXPECT_TRUE(recorder.isRecording());

    QString path2 = m_tempDir.path() + "/restart2.avi";
    ASSERT_TRUE(recorder.startRecording(path2, 320, 240, 30));
    EXPECT_TRUE(recorder.isRecording());
    EXPECT_TRUE(recorder.filePath() == path2);
    recorder.addFrame(frameData(200));
    recorder.stopRecording();
    EXPECT_FALSE(recorder.isRecording());
}
