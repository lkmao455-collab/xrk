#include <gtest/gtest.h>
#include <QApplication>
#include <QAudioInput>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaCaptureSession>
#include <QAudioFormat>
#include <QTimer>
#include <QSignalSpy>
#include <QAudioSink>

// Test audio input device enumeration
TEST(MicrophoneTest, EnumerateDevices) {
    auto audioDevices = QMediaDevices::audioInputs();
    // Just verify we can enumerate without crash
    EXPECT_GE(audioDevices.size(), 0);
}

// Test audio output device enumeration
TEST(MicrophoneTest, EnumerateOutputDevices) {
    auto audioDevices = QMediaDevices::audioOutputs();
    // Just verify we can enumerate without crash
    EXPECT_GE(audioDevices.size(), 0);
}

// Test audio input creation
TEST(MicrophoneTest, CreateAudioInput) {
    auto audioDevices = QMediaDevices::audioInputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio input devices available";
    }

    QAudioInput audioInput(audioDevices.first());
    // Audio input should be created successfully
}

// Test audio output creation
TEST(MicrophoneTest, CreateAudioOutput) {
    auto audioDevices = QMediaDevices::audioOutputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio output devices available";
    }

    QAudioOutput audioOutput;
    audioOutput.setVolume(0.8);
    EXPECT_DOUBLE_EQ(audioOutput.volume(), 0.8);
}

// Test audio format
TEST(MicrophoneTest, AudioFormat) {
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    EXPECT_EQ(format.sampleRate(), 44100);
    EXPECT_EQ(format.channelCount(), 2);
    EXPECT_EQ(format.sampleFormat(), QAudioFormat::Int16);
}

// Test audio input with capture session
TEST(MicrophoneTest, AudioInputWithSession) {
    auto audioDevices = QMediaDevices::audioInputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio input devices available";
    }

    QMediaCaptureSession session;
    QAudioInput audioInput(audioDevices.first());
    QAudioOutput audioOutput;

    session.setAudioInput(&audioInput);
    session.setAudioOutput(&audioOutput);

    // Test volume control
    audioOutput.setVolume(0.5);
    QApplication::processEvents();
}

// Test audio volume levels
TEST(MicrophoneTest, VolumeLevels) {
    QAudioOutput audioOutput;

    // Test various volume levels
    audioOutput.setVolume(0.0);
    EXPECT_DOUBLE_EQ(audioOutput.volume(), 0.0);

    audioOutput.setVolume(0.5);
    EXPECT_DOUBLE_EQ(audioOutput.volume(), 0.5);

    audioOutput.setVolume(1.0);
    EXPECT_DOUBLE_EQ(audioOutput.volume(), 1.0);
}

// Test audio device properties
TEST(MicrophoneTest, DeviceProperties) {
    auto audioDevices = QMediaDevices::audioInputs();
    for (const QAudioDevice& dev : audioDevices) {
        EXPECT_FALSE(dev.id().isEmpty());
        EXPECT_FALSE(dev.description().isEmpty());
    }
}

// Test audio output device properties
TEST(MicrophoneTest, OutputDeviceProperties) {
    auto audioDevices = QMediaDevices::audioOutputs();
    for (const QAudioDevice& dev : audioDevices) {
        EXPECT_FALSE(dev.id().isEmpty());
        EXPECT_FALSE(dev.description().isEmpty());
    }
}

// Test mute functionality
TEST(MicrophoneTest, MuteFunctionality) {
    QAudioOutput audioOutput;

    // Test mute
    audioOutput.setMuted(true);
    EXPECT_TRUE(audioOutput.isMuted());

    audioOutput.setMuted(false);
    EXPECT_FALSE(audioOutput.isMuted());
}

// Test audio sink creation
TEST(MicrophoneTest, AudioSinkCreation) {
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);

    auto audioDevices = QMediaDevices::audioOutputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio output devices available";
    }

    QAudioSink audioSink(audioDevices.first(), format);
    // Audio sink should be created successfully
}

// Test multiple audio sessions
TEST(MicrophoneTest, MultipleSessions) {
    auto audioDevices = QMediaDevices::audioInputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio input devices available";
    }

    QMediaCaptureSession session1;
    QAudioInput audioInput1(audioDevices.first());
    session1.setAudioInput(&audioInput1);

    QApplication::processEvents();
}

// Test audio device state
TEST(MicrophoneTest, DeviceState) {
    auto audioDevices = QMediaDevices::audioInputs();
    if (audioDevices.isEmpty()) {
        GTEST_SKIP() << "No audio input devices available";
    }

    // Test that we can query device properties without crash
    QAudioDevice device = audioDevices.first();
    EXPECT_FALSE(device.isNull());
}
