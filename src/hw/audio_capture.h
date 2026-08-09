#pragma once

#include <QObject>
#include <QByteArray>
#include <QTimer>

namespace xrk {

class AudioCapture : public QObject {
    Q_OBJECT
public:
    enum CaptureMode {
        Loopback,    // capture system/speaker output (default, host side)
        Microphone   // capture the default microphone input (controller side)
    };

    // QObject* stays first so existing `new AudioCapture(this)` keeps working.
    explicit AudioCapture(QObject* parent = nullptr, CaptureMode mode = Loopback);
    ~AudioCapture();

    bool initialize();
    void shutdown();
    bool isInitialized() const;
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }

signals:
    void audioDataCaptured(const QByteArray& pcmData);

private slots:
    void onCaptureTimer();

private:
    bool m_initialized = false;
    bool m_muted = false;
    CaptureMode m_mode = Loopback;
    int m_sampleRate = 48000;
    int m_channels = 2;
    int m_bitsPerSample = 16;
    QTimer* m_timer = nullptr;

    void* m_audioClient = nullptr;
    void* m_captureClient = nullptr;
};

} // namespace xrk
