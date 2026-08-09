#include "screen_capture.h"
#include "core/logger.h"

#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <malloc.h>
using Microsoft::WRL::ComPtr;
#endif

#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstring>
#endif

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace xrk {

namespace {

// Cheap, uniform frame signature for the hasFrameChanged fallback. We hash a
// strided sample of the whole buffer (1/16 of the bytes) so a single changed
// pixel, anywhere on screen, almost always flips the hash, while the cost stays
// well under a millisecond even for a 1080p frame. Used only when DDA cannot
// report dirty regions, so an occasional miss is recovered by the next frame /
// the periodic keyframe.
quint64 computeFrameHash(const QImage& img) {
    if (img.isNull()) return 0;
    const int bytes = img.sizeInBytes();
    const uchar* p = img.constBits();
    const int stride = 64; // 16 pixels per sampled slot
    quint64 h = 1469598103934665603ULL; // FNV-1a 64-bit offset basis
    for (int i = 0; i < bytes; i += stride) {
        h ^= static_cast<quint64>(p[i]);
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

} // namespace

#ifdef _WIN32
struct ScreenCapture::DxgiContext {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> stagingTexture;
    QByteArray metadataBuffer;
    int width = 0;
    int height = 0;
    bool valid = false;
};
#else
struct ScreenCapture::DxgiContext { int dummy; };
#endif

#ifdef __linux__
struct ScreenCapture::LinuxContext {
    Display* display = nullptr;
    Window rootWindow = 0;
    XShmSegmentInfo shmInfo;
    XImage* xImage = nullptr;
    int screenWidth = 0;
    int screenHeight = 0;
    bool valid = false;
};
#else
struct ScreenCapture::LinuxContext { int dummy; };
#endif

#ifdef __APPLE__
struct ScreenCapture::MacContext {
    QList<CGDirectDisplayID> displays;
    bool valid = false;
};
#else
struct ScreenCapture::MacContext { int dummy; };
#endif

ScreenCapture::ScreenCapture(QObject* parent) : QObject(parent) {
#ifdef _WIN32
    m_dxgiContext = std::make_unique<DxgiContext>();
#endif
#ifdef __linux__
    m_linuxContext = std::make_unique<LinuxContext>();
#endif
#ifdef __APPLE__
    m_macContext = std::make_unique<MacContext>();
#endif
}

ScreenCapture::~ScreenCapture() {
    shutdown();
}

bool ScreenCapture::initialize() {
    if (m_initialized) {
        return true;
    }

    // No duplication history yet: the first dirty-rect query must report the
    // whole screen.
    m_dirtyRectsStale = true;
    m_lastFrameHash = 0;
    m_hasLastFrameHash = false;

#ifdef _WIN32
    if (initializeDxgi()) {
        m_useDxgi = true;
        m_initialized = true;
        LOG_INFO("ScreenCapture initialized (DXGI mode)");
        return true;
    }
    LOG_WARNING("DXGI failed, falling back to GDI");
#endif

#ifdef _WIN32
    if (initializeGdi()) {
        m_useDxgi = false;
        m_initialized = true;
        LOG_INFO("ScreenCapture initialized (GDI mode)");
        return true;
    }
#endif

#ifdef __linux__
    if (initializeLinux()) {
        m_initialized = true;
        LOG_INFO("ScreenCapture initialized (Linux XShm mode)");
        return true;
    }
#endif

#ifdef __APPLE__
    if (initializeMac()) {
        m_initialized = true;
        LOG_INFO("ScreenCapture initialized (macOS CoreGraphics mode)");
        return true;
    }
#endif

    LOG_ERROR("ScreenCapture initialization failed");
    emit captureError(tr("屏幕采集初始化失败"));
    return false;
}

void ScreenCapture::shutdown() {
    if (!m_initialized) {
        return;
    }

#ifdef _WIN32
    if (m_useDxgi) {
        shutdownDxgi();
    }
#endif
#ifdef __linux__
    shutdownLinux();
#endif
#ifdef __APPLE__
    shutdownMac();
#endif

    m_initialized = false;
    m_dirtyRectsStale = true;
    m_hasLastFrameHash = false;
    m_lastFrameHash = 0;
    LOG_INFO("ScreenCapture shutdown");
}

QImage ScreenCapture::captureFrame() {
    return captureFrame(static_cast<QList<QRect>*>(nullptr));
}

QImage ScreenCapture::captureFrame(QList<QRect>* dirtyRects) {
    if (dirtyRects) {
        dirtyRects->clear();
    }
    
    QMutexLocker lock(&m_mutex);
    if (!m_initialized) {
        return QImage();
    }
    
    // Cache member variables under lock to avoid race conditions
    bool useDxgi = m_useDxgi && !m_dxgiFallback;
    bool dxgiFallback = m_dxgiFallback;
    int dxFailCount = m_dxFailCount;
    lock.unlock();

#ifdef _WIN32
    if (useDxgi) {
        QImage frame = captureDxgiFrameEx(dirtyRects);
        if (!frame.isNull()) {
            QMutexLocker l(&m_mutex);
            m_dxFailCount = 0;
            return frame;
        }
        // Track consecutive failures; after 10, permanently switch to GDI
        {
            QMutexLocker l(&m_mutex);
            if (++m_dxFailCount >= 10) {
                m_dxgiFallback = true;
                LOG_WARNING("DXGI capture failed " + QString::number(m_dxFailCount) + " times, switching to GDI permanently");
            }
        }
        // Also try GDI for this frame so the user sees something. GDI has no
        // change metadata, so leave dirtyRects empty (= no hint).
        if (dirtyRects) {
            dirtyRects->clear();
        }
        return captureGdiFrame();
    }
    return captureGdiFrame();
#elif defined(__linux__)
    return captureLinuxFrame();
#elif defined(__APPLE__)
    return captureMacFrame();
#else
    return QImage();
#endif
}

QImage ScreenCapture::captureFrame(int monitorIndex, QList<QRect>* dirtyRects) {
    QMutexLocker lock(&m_mutex);
    if (!m_initialized) {
        return QImage();
    }

    if (monitorIndex != m_monitorIndex && monitorIndex >= 0 && monitorIndex < m_monitors.size()) {
        lock.unlock();
        setMonitorIndex(monitorIndex);
        lock.relock();
    }

    // Re-check after potential switch
    if (!m_initialized) {
        return QImage();
    }
    lock.unlock();
    
    return captureFrame(dirtyRects);
}

bool ScreenCapture::isInitialized() const {
    return m_initialized;
}

void ScreenCapture::setCaptureRect(const QRect& rect) {
    m_captureRect = rect;
}

QRect ScreenCapture::captureRect() const {
    return m_captureRect;
}

void ScreenCapture::setTargetFps(int fps) {
    m_targetFps = qBound(1, fps, 60);
}

int ScreenCapture::targetFps() const {
    return m_targetFps;
}

#ifdef _WIN32
bool ScreenCapture::initializeDxgi() {
    HRESULT hr;

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        m_dxgiContext->device.GetAddressOf(),
        &featureLevel,
        m_dxgiContext->context.GetAddressOf()
    );

    if (FAILED(hr)) {
        LOG_ERROR("DXGI: Failed to create D3D11 device");
        return false;
    }

    m_monitors.clear();

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_dxgiContext->device.As(&dxgiDevice);
    if (FAILED(hr)) {
        LOG_ERROR("DXGI: Failed to get IDXGIDevice");
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("DXGI: Failed to get adapter");
        return false;
    }

    IDXGIOutput* outputRaw = nullptr;
    for (UINT i = 0; adapter->EnumOutputs(i, &outputRaw) != DXGI_ERROR_NOT_FOUND; ++i) {
        ComPtr<IDXGIOutput> output;
        output.Attach(outputRaw);

        DXGI_OUTPUT_DESC outputDesc;
        hr = output->GetDesc(&outputDesc);
        if (FAILED(hr)) {
            continue;
        }

        MonitorInfo info;
        info.index = static_cast<int>(i);
        info.name = QString::fromWCharArray(outputDesc.DeviceName);
        info.x = outputDesc.DesktopCoordinates.left;
        info.y = outputDesc.DesktopCoordinates.top;
        info.width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        info.height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
        info.isPrimary = (outputDesc.DesktopCoordinates.left == 0 && outputDesc.DesktopCoordinates.top == 0);

        m_monitors.append(info);

        if (i == static_cast<UINT>(m_monitorIndex) || m_monitors.size() == 1) {
            m_dxgiContext->width = info.width;
            m_dxgiContext->height = info.height;

            ComPtr<IDXGIOutput1> output1;
            hr = output.As(&output1);
            if (FAILED(hr)) {
                continue;
            }

            hr = output1->DuplicateOutput(
                m_dxgiContext->device.Get(),
                m_dxgiContext->duplication.GetAddressOf()
            );

            if (FAILED(hr)) {
                LOG_ERROR("DXGI: DuplicateOutput failed for monitor " + QString::number(i));
                continue;
            }

            m_dxgiContext->valid = true;
            LOG_INFO("DXGI initialized for monitor " + QString::number(i) + ": " + 
                     QString::number(info.width) + "x" + QString::number(info.height));
        }
    }

    if (m_monitors.isEmpty()) {
        LOG_ERROR("DXGI: No monitors found");
        return false;
    }

    if (m_monitorIndex >= m_monitors.size()) {
        m_monitorIndex = 0;
    }

    if (!m_dxgiContext->valid && !m_monitors.isEmpty()) {
        m_monitorIndex = 0;
        return initializeDxgi();
    }

    return m_dxgiContext->valid;
}

void ScreenCapture::shutdownDxgi() {
    if (m_dxgiContext) {
        m_dxgiContext->stagingTexture.Reset();
        m_dxgiContext->duplication.Reset();
        m_dxgiContext->context.Reset();
        m_dxgiContext->device.Reset();
        m_dxgiContext->valid = false;
    }
    m_monitors.clear();
}

void ScreenCapture::collectDxgiDirtyRects(const DXGI_OUTDUPL_FRAME_INFO& frameInfo,
                                          QList<QRect>* dirtyRects,
                                          quint64 currentFrameHash) {
    const QRect fullScreen(0, 0, m_dxgiContext->width, m_dxgiContext->height);

    auto reportFullScreen = [&]() {
        dirtyRects->clear();
        if (fullScreen.isValid()) {
            dirtyRects->append(fullScreen);
        }
        m_dirtyRectsStale = false;
    };

    // The DDA metadata is the authoritative change signal. When it is missing or
    // unusable we fall back to a frame-level change check: only a genuinely
    // changed frame forces a full-screen redraw, an identical frame is reported
    // as "no change" so the encoder can skip it instead of re-encoding everything.
    const bool frameChanged = !m_hasLastFrameHash || (currentFrameHash != m_lastFrameHash);
    auto reportFullOrSkip = [&]() {
        if (!frameChanged) {
            dirtyRects->clear();
            m_dirtyRectsStale = false;
            return;
        }
        reportFullScreen();
    };

    if (m_dirtyRectsStale) {
        // The previous grab lost its metadata, so the accumulated regions no
        // longer describe everything that changed on screen.
        reportFullOrSkip();
        return;
    }

    if (frameInfo.TotalMetadataBufferSize == 0) {
        // Nothing but a pointer update; no pixels changed.
        return;
    }

    QByteArray& buffer = m_dxgiContext->metadataBuffer;
    if (buffer.size() < static_cast<int>(frameInfo.TotalMetadataBufferSize)) {
        buffer.resize(static_cast<int>(frameInfo.TotalMetadataBufferSize));
    }

    UINT moveBytes = 0;
    HRESULT hr = m_dxgiContext->duplication->GetFrameMoveRects(
        static_cast<UINT>(buffer.size()),
        reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(buffer.data()),
        &moveBytes
    );
    if (FAILED(hr)) {
        reportFullOrSkip();
        return;
    }

    const auto* moves = reinterpret_cast<const DXGI_OUTDUPL_MOVE_RECT*>(buffer.constData());
    const int moveCount = static_cast<int>(moveBytes / sizeof(DXGI_OUTDUPL_MOVE_RECT));
    for (int i = 0; i < moveCount; ++i) {
        // Both the source and the destination area have to be resent: the
        // encoder does not implement copy-blits, it just re-encodes pixels.
        const RECT& dst = moves[i].DestinationRect;
        const POINT& src = moves[i].SourcePoint;
        const int w = dst.right - dst.left;
        const int h = dst.bottom - dst.top;
        dirtyRects->append(QRect(dst.left, dst.top, w, h));
        dirtyRects->append(QRect(src.x, src.y, w, h));
    }

    UINT dirtyBytes = 0;
    hr = m_dxgiContext->duplication->GetFrameDirtyRects(
        static_cast<UINT>(buffer.size()) - moveBytes,
        reinterpret_cast<RECT*>(buffer.data() + moveBytes),
        &dirtyBytes
    );
    if (FAILED(hr)) {
        reportFullOrSkip();
        return;
    }

    const auto* dirty = reinterpret_cast<const RECT*>(buffer.constData() + moveBytes);
    const int dirtyCount = static_cast<int>(dirtyBytes / sizeof(RECT));
    for (int i = 0; i < dirtyCount; ++i) {
        const RECT& r = dirty[i];
        dirtyRects->append(QRect(r.left, r.top, r.right - r.left, r.bottom - r.top));
    }

    // Clamp to the desktop and drop degenerate entries.
    for (int i = dirtyRects->size() - 1; i >= 0; --i) {
        QRect clamped = fullScreen.isValid() ? (*dirtyRects)[i].intersected(fullScreen)
                                             : (*dirtyRects)[i];
        if (clamped.isEmpty()) {
            dirtyRects->removeAt(i);
        } else {
            (*dirtyRects)[i] = clamped;
        }
    }

    m_dirtyRectsStale = false;
}

QImage ScreenCapture::captureDxgiFrame() {
    return captureDxgiFrameEx(nullptr);
}

QImage ScreenCapture::captureDxgiFrameEx(QList<QRect>* dirtyRects) {
    if (!m_dxgiContext || !m_dxgiContext->valid) {
        m_dirtyRectsStale = true;
        return QImage();
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> resource;

    HRESULT hr = m_dxgiContext->duplication->AcquireNextFrame(
        100,
        &frameInfo,
        resource.GetAddressOf()
    );

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return QImage();
    }

    if (FAILED(hr)) {
        m_dxgiContext->duplication->ReleaseFrame();
        m_dirtyRectsStale = true;
        return QImage();
    }

    // Duplication metadata must be read before ReleaseFrame(). We defer the
    // dirty-rect collection until the frame pixels are on the CPU (see below)
    // so the hasFrameChanged fallback has a frame hash to compare against.
    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = resource.As(&desktopTexture);
    if (FAILED(hr)) {
        m_dxgiContext->duplication->ReleaseFrame();
        m_dirtyRectsStale = true;
        return QImage();
    }

    D3D11_TEXTURE2D_DESC desc;
    desktopTexture->GetDesc(&desc);

    if (!m_dxgiContext->stagingTexture) {
        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width = desc.Width;
        stagingDesc.Height = desc.Height;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        hr = m_dxgiContext->device->CreateTexture2D(
            &stagingDesc,
            nullptr,
            m_dxgiContext->stagingTexture.GetAddressOf()
        );

        if (FAILED(hr)) {
            m_dxgiContext->duplication->ReleaseFrame();
            m_dirtyRectsStale = true;
            return QImage();
        }
        LOG_INFO("DXGI: Staging texture created: " + QString::number(desc.Width) + "x" + QString::number(desc.Height));
    }

    m_dxgiContext->context->CopyResource(m_dxgiContext->stagingTexture.Get(), desktopTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_dxgiContext->context->Map(
        m_dxgiContext->stagingTexture.Get(),
        0,
        D3D11_MAP_READ,
        0,
        &mapped
    );

    if (FAILED(hr)) {
        m_dxgiContext->duplication->ReleaseFrame();
        m_dirtyRectsStale = true;
        return QImage();
    }

    D3D11_TEXTURE2D_DESC stagingDesc;
    m_dxgiContext->stagingTexture->GetDesc(&stagingDesc);

    QImage image(stagingDesc.Width, stagingDesc.Height, QImage::Format_RGB32);

    const uchar* src = static_cast<const uchar*>(mapped.pData);
    uchar* dst = image.bits();
    int srcPitch = mapped.RowPitch;
    int dstPitch = image.bytesPerLine();
    int rowBytes = stagingDesc.Width * 4;

    for (int y = 0; y < stagingDesc.Height; ++y) {
        const uint* srcRow = reinterpret_cast<const uint*>(src + y * srcPitch);
        uint* dstRow = reinterpret_cast<uint*>(dst + y * dstPitch);
        for (int x = 0; x < stagingDesc.Width; ++x) {
            uint bgra = srcRow[x];
            uchar b = bgra & 0xFF;
            uchar g = (bgra >> 8) & 0xFF;
            uchar r = (bgra >> 16) & 0xFF;
            dstRow[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }

    // Now that the frame is on the CPU we can compute its change signature and
    // resolve dirty regions. GetFrame*Rects still reads metadata owned by the
    // acquired frame, so this must run before ReleaseFrame() below.
    quint64 currentHash = computeFrameHash(image);
    if (dirtyRects) {
        collectDxgiDirtyRects(frameInfo, dirtyRects, currentHash);
    } else {
        m_dirtyRectsStale = true;
    }
    m_lastFrameHash = currentHash;
    m_hasLastFrameHash = true;

    m_dxgiContext->context->Unmap(m_dxgiContext->stagingTexture.Get(), 0);
    m_dxgiContext->duplication->ReleaseFrame();

    emit frameCaptured(image);
    return image;
}

QImage ScreenCapture::captureFrame(int monitorIndex) {
    if (!m_initialized) {
        return QImage();
    }

    if (monitorIndex != m_monitorIndex && monitorIndex >= 0 && monitorIndex < m_monitors.size()) {
        setMonitorIndex(monitorIndex);
    }

    return captureFrame();
}

QList<MonitorInfo> ScreenCapture::getMonitorList() const {
    return m_monitors;
}

int ScreenCapture::monitorCount() const {
    return m_monitors.size();
}

void ScreenCapture::setMonitorIndex(int index) {
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_monitors.size()) {
        return;
    }

    if (index == m_monitorIndex) {
        return;
    }

    m_monitorIndex = index;
    lock.unlock();
    
    // Try hot-switch first
    if (m_useDxgi && m_dxgiContext && m_dxgiContext->device) {
        if (switchDxgiOutput(index)) {
            LOG_INFO("Switched to monitor " + QString::number(index) + " (hot-switch)");
            return;
        }
        LOG_WARNING("Hot-switch failed for monitor " + QString::number(index) + ", falling back to full reinit");
    }
    
    // Fall back to full reinit
    shutdown();
    initialize();
    LOG_INFO("Switched to monitor " + QString::number(index) + " (full reinit)");
}

bool ScreenCapture::switchMonitorSafe(int index) {
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_monitors.size()) {
        LOG_WARNING("switchMonitorSafe: invalid index " + QString::number(index));
        emit monitorSwitchCompleted(false, m_monitorIndex);
        return false;
    }

    if (index == m_monitorIndex) {
        emit monitorSwitchCompleted(true, m_monitorIndex);
        return true;
    }

    m_monitorIndex = index;
    lock.unlock();
    
    bool success = false;
    
    // Try hot-switch first (no black screen)
    if (m_useDxgi && m_dxgiContext && m_dxgiContext->device) {
        success = switchDxgiOutput(index);
        if (success) {
            emit monitorSwitchCompleted(true, m_monitorIndex);
            LOG_INFO("switchMonitorSafe: hot-switch to monitor " + QString::number(index) + " OK");
            return true;
        }
        LOG_WARNING("switchMonitorSafe: hot-switch failed, falling back to full reinit");
    }
    
    // Fall back to full shutdown/initialize
    if (shutdown(); true) {
        success = initialize();
    }
    
    emit monitorSwitchCompleted(success, m_monitorIndex);
    LOG_INFO("switchMonitorSafe: switched to monitor " + QString::number(index) + 
             (success ? " OK (full reinit)" : " FAILED"));
    return success;
}

bool ScreenCapture::switchDxgiOutput(int index) {
#ifdef _WIN32
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_monitors.size()) {
        LOG_WARNING("switchDxgiOutput: invalid index " + QString::number(index));
        return false;
    }
    if (index == m_monitorIndex) {
        return true; // Already on this monitor
    }
    if (!m_dxgiContext || !m_dxgiContext->device || !m_useDxgi) {
        // Not in DXGI mode, fall back to full reinit
        m_monitorIndex = index;
        lock.unlock();
        shutdown();
        initialize();
        return m_initialized;
    }

    // Hot-switch: keep D3D device alive, only swap the IDXGIOutputDuplication
    // This eliminates the black-screen gap of full shutdown/initialize

    // 1. Release current duplication and staging texture
    m_dxgiContext->stagingTexture.Reset();
    m_dxgiContext->duplication.Reset();
    m_dxgiContext->valid = false;
    m_dirtyRectsStale = true; // Must report full screen after switch

    // 2. Find the target output and create new duplication
    HRESULT hr;
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_dxgiContext->device.As(&dxgiDevice);
    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: Failed to get IDXGIDevice");
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: Failed to get adapter");
        return false;
    }

    IDXGIOutput* outputRaw = nullptr;
    hr = adapter->EnumOutputs(static_cast<UINT>(index), &outputRaw);
    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: Failed to enumerate output " + QString::number(index));
        return false;
    }

    ComPtr<IDXGIOutput> output;
    output.Attach(outputRaw);

    DXGI_OUTPUT_DESC outputDesc;
    hr = output->GetDesc(&outputDesc);
    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: Failed to get output desc");
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: Failed to get IDXGIOutput1");
        return false;
    }

    hr = output1->DuplicateOutput(
        m_dxgiContext->device.Get(),
        m_dxgiContext->duplication.GetAddressOf()
    );

    if (FAILED(hr)) {
        LOG_ERROR("switchDxgiOutput: DuplicateOutput failed for monitor " + QString::number(index));
        return false;
    }

    // 3. Update dimensions
    m_dxgiContext->width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    m_dxgiContext->height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    m_monitorIndex = index;
    m_dxgiContext->valid = true;

    LOG_INFO("switchDxgiOutput: Hot-switched to monitor " + QString::number(index) + ": " +
             QString::number(m_dxgiContext->width) + "x" + QString::number(m_dxgiContext->height));
    return true;
#else
    Q_UNUSED(index);
    return false;
#endif
}

int ScreenCapture::monitorIndex() const {
    return m_monitorIndex;
}

namespace {

struct MonitorEnumData {
    QList<xrk::MonitorInfo>* monitors;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdc, LPRECT lprect, LPARAM lParam) {
    Q_UNUSED(hdc);
    Q_UNUSED(lprect);
    MonitorEnumData* data = reinterpret_cast<MonitorEnumData*>(lParam);
    MONITORINFOEXW mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMonitor, &mi);
    
    xrk::MonitorInfo info;
    info.index = data->monitors->size();
    info.name = QString::fromWCharArray(mi.szDevice);
    info.x = mi.rcMonitor.left;
    info.y = mi.rcMonitor.top;
    info.width = mi.rcMonitor.right - mi.rcMonitor.left;
    info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    
    data->monitors->append(info);
    return TRUE;
}

} // anonymous namespace

bool ScreenCapture::initializeGdi() {
    m_monitors.clear();
    
    MonitorEnumData data = {&m_monitors};
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));
    
    if (m_monitors.isEmpty()) {
        MonitorInfo info;
        info.index = 0;
        info.name = "Primary";
        info.x = 0;
        info.y = 0;
        info.width = GetSystemMetrics(SM_CXSCREEN);
        info.height = GetSystemMetrics(SM_CYSCREEN);
        info.isPrimary = true;
        m_monitors.append(info);
    }
    
    LOG_INFO("GDI: Found " + QString::number(m_monitors.size()) + " monitors");
    return true;
}

QImage ScreenCapture::captureGdiFrame() {
    if (m_monitorIndex >= m_monitors.size()) {
        m_monitorIndex = 0;
    }
    
    const MonitorInfo& monitor = m_monitors[m_monitorIndex];
    
    HDC hScreenDC = CreateDCW(nullptr, monitor.name.toStdWString().c_str(), nullptr, nullptr);
    if (!hScreenDC) {
        hScreenDC = GetDC(nullptr);
    }
    
    if (!hScreenDC) return QImage();

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        ReleaseDC(nullptr, hScreenDC);
        return QImage();
    }

    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, monitor.width, monitor.height);
    if (!hBitmap) {
        DeleteDC(hMemoryDC);
        ReleaseDC(nullptr, hScreenDC);
        return QImage();
    }

    HGDIOBJ hOldBitmap = SelectObject(hMemoryDC, hBitmap);

    BitBlt(hMemoryDC, 0, 0, monitor.width, monitor.height, hScreenDC, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = monitor.width;
    bi.biHeight = -monitor.height;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    QImage image(monitor.width, monitor.height, QImage::Format_RGB32);
    GetDIBits(hMemoryDC, hBitmap, 0, monitor.height, image.bits(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    SelectObject(hMemoryDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemoryDC);
    ReleaseDC(nullptr, hScreenDC);

    emit frameCaptured(image);
    return image;
}

QImage ScreenCapture::captureGdiFrame(int monitorIndex) {
    if (monitorIndex != m_monitorIndex) {
        setMonitorIndex(monitorIndex);
    }
    return captureGdiFrame();
}

QImage ScreenCapture::captureDxgiFrame(int monitorIndex) {
    if (monitorIndex != m_monitorIndex) {
        setMonitorIndex(monitorIndex);
    }
    return captureDxgiFrame();
}

// ==================== Linux (X11/XShm) ====================

#elif defined(__linux__)

bool ScreenCapture::initializeLinux() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        LOG_ERROR("Linux: Cannot open X display");
        return false;
    }

    m_linuxContext->display = display;
    m_linuxContext->rootWindow = DefaultRootWindow(display);

    int screen = DefaultScreen(display);
    m_linuxContext->screenWidth = DisplayWidth(display, screen);
    m_linuxContext->screenHeight = DisplayHeight(display, screen);

    int major, minor;
    if (!XShmQueryVersion(display, &major, &minor)) {
        LOG_WARNING("Linux: XShm not available, using XGetImage (slower)");
    }

    XImage* img = XShmCreateImage(display, DefaultVisual(display, screen),
                                   DefaultDepth(display, screen), ZPixmap,
                                   nullptr, &m_linuxContext->shmInfo,
                                   m_linuxContext->screenWidth,
                                   m_linuxContext->screenHeight);

    if (!img) {
        LOG_ERROR("Linux: XShmCreateImage failed");
        XCloseDisplay(display);
        m_linuxContext->display = nullptr;
        return false;
    }

    m_linuxContext->shmInfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height,
                                            IPC_CREAT | 0777);
    if (m_linuxContext->shmInfo.shmid < 0) {
        LOG_ERROR("Linux: shmget failed");
        XDestroyImage(img);
        XCloseDisplay(display);
        m_linuxContext->display = nullptr;
        return false;
    }

    m_linuxContext->shmInfo.shmaddr = static_cast<char*>(shmat(m_linuxContext->shmInfo.shmid, nullptr, 0));
    m_linuxContext->shmInfo.readOnly = False;

    if (m_linuxContext->shmInfo.shmaddr == reinterpret_cast<char*>(-1)) {
        LOG_ERROR("Linux: shmat failed");
        shmctl(m_linuxContext->shmInfo.shmid, IPC_RMID, nullptr);
        XDestroyImage(img);
        XCloseDisplay(display);
        m_linuxContext->display = nullptr;
        return false;
    }

    img->data = m_linuxContext->shmInfo.shmaddr;
    m_linuxContext->xImage = img;

    if (!XShmAttach(display, &m_linuxContext->shmInfo)) {
        LOG_ERROR("Linux: XShmAttach failed");
        shmdt(m_linuxContext->shmInfo.shmaddr);
        shmctl(m_linuxContext->shmInfo.shmid, IPC_RMID, nullptr);
        XDestroyImage(img);
        XCloseDisplay(display);
        m_linuxContext->display = nullptr;
        return false;
    }

    m_linuxContext->valid = true;

    MonitorInfo info;
    info.index = 0;
    info.name = "Primary";
    info.x = 0;
    info.y = 0;
    info.width = m_linuxContext->screenWidth;
    info.height = m_linuxContext->screenHeight;
    info.isPrimary = true;
    m_monitors.append(info);

    LOG_INFO("Linux: XShm initialized: " + QString::number(info.width) + "x" + QString::number(info.height));
    return true;
}

void ScreenCapture::shutdownLinux() {
    if (m_linuxContext && m_linuxContext->display) {
        if (m_linuxContext->xImage) {
            XShmDetach(m_linuxContext->display, &m_linuxContext->shmInfo);
            XDestroyImage(m_linuxContext->xImage);
            m_linuxContext->xImage = nullptr;
        }
        if (m_linuxContext->shmInfo.shmaddr) {
            shmdt(m_linuxContext->shmInfo.shmaddr);
            shmctl(m_linuxContext->shmInfo.shmid, IPC_RMID, nullptr);
            m_linuxContext->shmInfo.shmaddr = nullptr;
        }
        XCloseDisplay(m_linuxContext->display);
        m_linuxContext->display = nullptr;
        m_linuxContext->valid = false;
    }
    m_monitors.clear();
}

QImage ScreenCapture::captureLinuxFrame() {
    if (!m_linuxContext || !m_linuxContext->valid) {
        return QImage();
    }

    if (!XShmGetImage(m_linuxContext->display, m_linuxContext->rootWindow,
                       m_linuxContext->xImage, 0, 0, AllPlanes)) {
        return QImage();
    }

    int width = m_linuxContext->screenWidth;
    int height = m_linuxContext->screenHeight;

    QImage image(width, height, QImage::Format_RGB32);

    int bytesPerLine = m_linuxContext->xImage->bytes_per_line;
    const uchar* src = reinterpret_cast<const uchar*>(m_linuxContext->xImage->data);
    uchar* dst = image.bits();
    int dstPitch = image.bytesPerLine();

    for (int y = 0; y < height; ++y) {
        const uchar* srcRow = src + y * bytesPerLine;
        uchar* dstRow = dst + y * dstPitch;
        std::memcpy(dstRow, srcRow, width * 4);
    }

    return image;
}

QImage ScreenCapture::captureLinuxFrame(int monitorIndex) {
    Q_UNUSED(monitorIndex);
    return captureLinuxFrame();
}

// ==================== macOS (CoreGraphics) ====================

#elif defined(__APPLE__)

bool ScreenCapture::initializeMac() {
    CGDisplayCount count;
    CGDirectDisplayID displayIDs[16];
    CGGetActiveDisplayList(16, displayIDs, &count);

    if (count == 0) {
        LOG_ERROR("macOS: No active displays found");
        return false;
    }

    m_macContext->displays.clear();
    for (CGDisplayCount i = 0; i < count && i < 16; ++i) {
        m_macContext->displays.append(displayIDs[i]);

        CGRect bounds = CGDisplayBounds(displayIDs[i]);

        MonitorInfo info;
        info.index = static_cast<int>(i);
        info.name = QString("Display %1").arg(i);
        info.x = static_cast<int>(bounds.origin.x);
        info.y = static_cast<int>(bounds.origin.y);
        info.width = static_cast<int>(bounds.size.width);
        info.height = static_cast<int>(bounds.size.height);
        info.isPrimary = (displayIDs[i] == CGMainDisplayID());
        m_monitors.append(info);
    }

    m_macContext->valid = true;
    LOG_INFO("macOS: Found " + QString::number(count) + " displays");
    return true;
}

void ScreenCapture::shutdownMac() {
    m_macContext->displays.clear();
    m_macContext->valid = false;
    m_monitors.clear();
}

QImage ScreenCapture::captureMacFrame() {
    if (!m_macContext || !m_macContext->valid || m_macContext->displays.isEmpty()) {
        return QImage();
    }

    int idx = qBound(0, m_monitorIndex, m_macContext->displays.size() - 1);
    CGDirectDisplayID displayId = m_macContext->displays[idx];

    CGImageRef cgImage = CGDisplayCreateImage(displayId);
    if (!cgImage) {
        return QImage();
    }

    size_t w = CGImageGetWidth(cgImage);
    size_t h = CGImageGetHeight(cgImage);
    QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
    image.fill(0);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(image.bits(), w, h, 8, image.bytesPerLine(),
                                             colorSpace, kCGImageAlphaPremultipliedFirst);

    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImage);
        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(colorSpace);
    CGImageRelease(cgImage);

    return image;
}

QImage ScreenCapture::captureMacFrame(int monitorIndex) {
    if (monitorIndex >= 0 && monitorIndex < m_macContext->displays.size()) {
        CGDirectDisplayID displayId = m_macContext->displays[monitorIndex];
        CGImageRef cgImage = CGDisplayCreateImage(displayId);
        if (!cgImage) return QImage();

        size_t w = CGImageGetWidth(cgImage);
        size_t h = CGImageGetHeight(cgImage);
        QImage image(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB32);
        image.fill(0);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(image.bits(), w, h, 8, image.bytesPerLine(),
                                                 colorSpace, kCGImageAlphaPremultipliedFirst);
        if (ctx) {
            CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImage);
            CGContextRelease(ctx);
        }
        CGColorSpaceRelease(colorSpace);
        CGImageRelease(cgImage);
        return image;
    }
    return captureMacFrame();
}

// ==================== Platform stubs ====================

#else

bool ScreenCapture::initializeDxgi() { return false; }
void ScreenCapture::shutdownDxgi() {}
QImage ScreenCapture::captureDxgiFrame() { return QImage(); }
QImage ScreenCapture::captureDxgiFrame(int) { return QImage(); }
QImage ScreenCapture::captureDxgiFrameEx(QList<QRect>*) { return QImage(); }
bool ScreenCapture::initializeGdi() {
    MonitorInfo info;
    info.index = 0; info.name = "Primary";
    info.x = 0; info.y = 0;
    info.width = 1920; info.height = 1080;
    info.isPrimary = true;
    m_monitors.append(info);
    return true;
}
QImage ScreenCapture::captureGdiFrame() { return QImage(); }
QImage ScreenCapture::captureGdiFrame(int) { return QImage(); }
bool ScreenCapture::initializeLinux() { return false; }
void ScreenCapture::shutdownLinux() {}
QImage ScreenCapture::captureLinuxFrame() { return QImage(); }
QImage ScreenCapture::captureLinuxFrame(int) { return QImage(); }
bool ScreenCapture::initializeMac() { return false; }
void ScreenCapture::shutdownMac() {}
QImage ScreenCapture::captureMacFrame() { return QImage(); }
QImage ScreenCapture::captureMacFrame(int) { return QImage(); }

#endif

} // namespace xrk
