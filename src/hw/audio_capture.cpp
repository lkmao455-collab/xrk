#include "audio_capture.h"
#include "core/logger.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

namespace xrk {

AudioCapture::AudioCapture(QObject* parent, CaptureMode mode)
    : QObject(parent), m_mode(mode), m_timer(nullptr) {
}

AudioCapture::~AudioCapture() {
    shutdown();
}

bool AudioCapture::initialize() {
    if (m_initialized) return true;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_ERROR("AudioCapture: CoInitializeEx failed");
        return false;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Failed to create MMDeviceEnumerator");
        return false;
    }

    IMMDevice* device = nullptr;
    EDataFlow flow = (m_mode == Microphone) ? eCapture : eRender;
    hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Failed to get default audio endpoint");
        enumerator->Release();
        return false;
    }

    IAudioClient* audioClient = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Failed to activate audio client");
        device->Release();
        enumerator->Release();
        return false;
    }

    WAVEFORMATEX* mixFormat = nullptr;
    DWORD streamFlags = 0;

    if (m_mode == Loopback) {
        hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr)) {
            LOG_ERROR("AudioCapture: Failed to get mix format");
            audioClient->Release();
            device->Release();
            enumerator->Release();
            return false;
        }

        m_sampleRate = mixFormat->nSamplesPerSec;
        m_channels = mixFormat->nChannels;
        m_bitsPerSample = mixFormat->wBitsPerSample;

        LOG_INFO("AudioCapture: Format: " + QString::number(m_sampleRate) + "Hz, " +
                 QString::number(m_channels) + "ch, " + QString::number(m_bitsPerSample) + "bit");

        streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    } else {
        // Microphone: force a fixed format so the far end's AudioPlayer (which
        // defaults to 48kHz/2ch/16bit) matches what we capture.
        mixFormat = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        memset(mixFormat, 0, sizeof(WAVEFORMATEX));
        mixFormat->wFormatTag = WAVE_FORMAT_PCM;
        mixFormat->nChannels = 2;
        mixFormat->nSamplesPerSec = 48000;
        mixFormat->wBitsPerSample = 16;
        mixFormat->nBlockAlign = mixFormat->nChannels * mixFormat->wBitsPerSample / 8;
        mixFormat->nAvgBytesPerSec = mixFormat->nSamplesPerSec * mixFormat->nBlockAlign;
        mixFormat->cbSize = 0;

        m_sampleRate = 48000;
        m_channels = 2;
        m_bitsPerSample = 16;
        LOG_INFO("AudioCapture: Microphone mode (forced 48000Hz/2ch/16bit)");
    }

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        100000,
        0,
        mixFormat,
        nullptr
    );
    CoTaskMemFree(mixFormat);

    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Initialize failed (" +
                  QString(m_mode == Microphone ? "microphone" : "loopback") + ")");
        audioClient->Release();
        device->Release();
        enumerator->Release();
        return false;
    }

    uint32_t bufferFrames = 0;
    audioClient->GetBufferSize(&bufferFrames);

    IAudioCaptureClient* captureClient = nullptr;
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Failed to get capture client");
        audioClient->Release();
        device->Release();
        enumerator->Release();
        return false;
    }

    m_audioClient = audioClient;
    m_captureClient = captureClient;

    device->Release();
    enumerator->Release();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &AudioCapture::onCaptureTimer);
    m_timer->start(50);

    hr = ((IAudioClient*)m_audioClient)->Start();
    if (FAILED(hr)) {
        LOG_ERROR("AudioCapture: Start failed");
        shutdown();
        return false;
    }

    m_initialized = true;
    LOG_INFO("AudioCapture: WASAPI " +
             QString(m_mode == Microphone ? "microphone" : "loopback") + " started");
    return true;
}

void AudioCapture::shutdown() {
    if (m_audioClient) {
        ((IAudioClient*)m_audioClient)->Stop();
        ((IAudioClient*)m_audioClient)->Release();
        m_audioClient = nullptr;
    }
    if (m_captureClient) {
        ((IAudioCaptureClient*)m_captureClient)->Release();
        m_captureClient = nullptr;
    }
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    m_initialized = false;
    LOG_INFO("AudioCapture: Shutdown");
}

bool AudioCapture::isInitialized() const {
    return m_initialized;
}

void AudioCapture::onCaptureTimer() {
    if (!m_initialized) return;

    IAudioCaptureClient* capture = (IAudioCaptureClient*)m_captureClient;

    uint32_t packetLength = 0;
    while (capture->GetNextPacketSize(&packetLength) == S_OK && packetLength > 0) {
        BYTE* data = nullptr;
        uint32_t framesAvailable = 0;
        DWORD flags = 0;

        HRESULT hr = capture->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            uint32_t dataSize = framesAvailable * m_channels * (m_bitsPerSample / 8);
            QByteArray pcmData(reinterpret_cast<const char*>(data), dataSize);
            capture->ReleaseBuffer(framesAvailable);

            if (!pcmData.isEmpty()) {
                emit audioDataCaptured(pcmData);
            }
        }
    }
}

} // namespace xrk

#else // non-Windows stubs

namespace xrk {

AudioCapture::AudioCapture(QObject* parent, CaptureMode mode)
    : QObject(parent), m_mode(mode), m_timer(nullptr) {
}

AudioCapture::~AudioCapture() {
    shutdown();
}

bool AudioCapture::initialize() {
    LOG_WARNING("AudioCapture: not supported on this platform (Windows WASAPI only)");
    return false;
}

void AudioCapture::shutdown() {
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    m_initialized = false;
}

bool AudioCapture::isInitialized() const {
    return m_initialized;
}

void AudioCapture::onCaptureTimer() {
}

} // namespace xrk

#endif // _WIN32
