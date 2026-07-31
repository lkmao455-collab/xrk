#pragma once

#include <QObject>
#include <QByteArray>
#include <QQueue>
#include <QMutex>

namespace xrk {

class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer();

    bool initialize(int sampleRate = 48000, int channels = 2, int bitsPerSample = 16);
    void shutdown();
    bool isInitialized() const;

    void playAudio(const QByteArray& pcmData);

private:
    bool m_initialized = false;
    int m_sampleRate = 48000;
    int m_channels = 2;
    int m_bitsPerSample = 16;
    QByteArray m_buffer;
    QMutex m_mutex;

    void* m_audioClient = nullptr;
    void* m_renderClient = nullptr;
};

} // namespace xrk
