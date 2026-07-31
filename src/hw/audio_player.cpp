#include "audio_player.h"
#include "core/logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace xrk {

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent) {
}

AudioPlayer::~AudioPlayer() {
    shutdown();
}

bool AudioPlayer::initialize(int sampleRate, int channels, int bitsPerSample) {
    if (m_initialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_ERROR("AudioPlayer: CoInitializeEx failed");
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Failed to create MMDeviceEnumerator");
        return false;
    }

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Failed to get default audio endpoint");
        enumerator->Release();
        return false;
    }

    IAudioClient* audioClient = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Failed to activate audio client");
        device->Release();
        enumerator->Release();
        return false;
    }

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = bitsPerSample;
    format.nBlockAlign = channels * bitsPerSample / 8;
    format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
    format.cbSize = 0;

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        100000,
        0,
        &format,
        nullptr
    );
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Initialize failed");
        audioClient->Release();
        device->Release();
        enumerator->Release();
        return false;
    }

    IAudioRenderClient* renderClient = nullptr;
    hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Failed to get render client");
        audioClient->Release();
        device->Release();
        enumerator->Release();
        return false;
    }

    m_sampleRate = sampleRate;
    m_channels = channels;
    m_bitsPerSample = bitsPerSample;
    m_audioClient = audioClient;
    m_renderClient = renderClient;

    device->Release();
    enumerator->Release();

    hr = ((IAudioClient*)m_audioClient)->Start();
    if (FAILED(hr)) {
        LOG_ERROR("AudioPlayer: Start failed");
        shutdown();
        return false;
    }

    m_initialized = true;
    LOG_INFO("AudioPlayer: WASAPI playback started");
    return true;
}

void AudioPlayer::shutdown() {
    if (m_audioClient) {
        ((IAudioClient*)m_audioClient)->Stop();
        ((IAudioClient*)m_audioClient)->Release();
        m_audioClient = nullptr;
    }
    if (m_renderClient) {
        ((IAudioRenderClient*)m_renderClient)->Release();
        m_renderClient = nullptr;
    }
    m_initialized = false;
}

bool AudioPlayer::isInitialized() const {
    return m_initialized;
}

void AudioPlayer::playAudio(const QByteArray& pcmData) {
    if (!m_initialized || pcmData.isEmpty()) return;

    QMutexLocker locker(&m_mutex);
    m_buffer.append(pcmData);

    if (m_audioClient && m_renderClient) {
        IAudioClient* client = (IAudioClient*)m_audioClient;
        IAudioRenderClient* render = (IAudioRenderClient*)m_renderClient;

        uint32_t bufferFrames = 0;
        client->GetBufferSize(&bufferFrames);

        uint32_t padding = 0;
        client->GetCurrentPadding(&padding);

        uint32_t availableFrames = bufferFrames - padding;
        uint32_t frameSize = m_channels * (m_bitsPerSample / 8);
        uint32_t availableBytes = availableFrames * frameSize;

        if (availableBytes > 0 && !m_buffer.isEmpty()) {
            uint32_t toWrite = qMin(availableBytes, (uint32_t)m_buffer.size());
            uint32_t framesToWrite = toWrite / frameSize;

            BYTE* data = nullptr;
            HRESULT hr = render->GetBuffer(framesToWrite, &data);
            if (SUCCEEDED(hr)) {
                memcpy(data, m_buffer.constData(), framesToWrite * frameSize);
                render->ReleaseBuffer(framesToWrite, 0);
                m_buffer.remove(0, framesToWrite * frameSize);
            }
        }
    }
}

} // namespace xrk
