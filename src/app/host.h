#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QThread>
#include <QHash>
#include <QMutex>
#include <QFile>
#include <atomic>
#include <memory>
#include "core/types.h"
#include "core/frame_queue.h"
#include "core/encryption.h"
#include "core/audit_logger.h"
#include "core/network_manager.h"
#include "hw/video_encoder.h"
#include "hw/tile_encoder.h"
#include "nat_traversal.h"
#include "p2p_manager.h"
#include "clipboard_manager.h"
#include "file_sync_manager.h"

namespace xrk {

class ScreenCapture;
class CameraCapture;
class InputControl;
class Encryption;
    class AudioCapture;
    class AudioPlayer;
    class RemoteTerminal;
class ScreenRecorder;
class PrivacyScreen;

class CaptureWorker : public QObject {
    Q_OBJECT
public:
    explicit CaptureWorker(ScreenCapture* capture, CameraCapture* camera, FrameQueue<CapturedFrame>* queue, int fps);
    ~CaptureWorker();

    void setUseCamera(bool useCamera);
    bool isUsingCamera() const { return m_useCamera; }

public slots:
    // Invoked from the GUI thread via QMetaObject::invokeMethod with a
    // BlockingQueuedConnection, so it must be a slot to be invokable.
    void setFps(int fps);

    void start();
    void stop();

signals:
    void error(const QString& message);

private slots:
    void onCaptureTimer();

private:
    ScreenCapture* m_capture;
    CameraCapture* m_camera;
    FrameQueue<CapturedFrame>* m_queue;
    QTimer* m_timer;
    int m_fps;
    std::atomic<bool> m_running;
    bool m_useCamera = false;
};

class EncodeWorker : public QObject {
    Q_OBJECT
public:
    explicit EncodeWorker(FrameQueue<CapturedFrame>* inputQueue, FrameQueue<QByteArray>* outputQueue);
    ~EncodeWorker();

    void setEncoder(std::unique_ptr<VideoEncoder> encoder);
    void setEncoderAsync(std::unique_ptr<VideoEncoder> encoder);
    VideoEncoder* encoder() const;
    void setEncryptionKey(const QByteArray& key, const QByteArray& iv);
    void requestFallbackToJpeg();

    // Weak-network differential transport: instead of one JPEG per frame, send
    // only the 64x64 tiles whose content changed. All setters are safe to call
    // from any thread; they are applied at the top of the encode loop.
    void setTiledMode(bool enabled);
    bool tiledMode() const { return m_tiledMode; }
    void setTileQuality(int quality) { m_tileQuality = qBound(1, quality, 100); }
    // 0 = unlimited. Caps how many tiles one frame may emit under congestion.
    void setMaxTilesPerFrame(int maxTiles) { m_maxTilesPerFrame = qMax(0, maxTiles); }
    // Forces the next frame to re-send every tile (new client, decode error).
    void requestKeyFrame() { m_keyFrameRequest = true; }

    // NACK repair: resend just the listed tiles (by x,y) from the current frame.
    void resendTiles(const QList<QPoint>& tiles);

    // Cursor-aware prioritization: tiles covering this point go out first so the
    // region the user is actively interacting with updates fastest on a weak link.
    void setMousePosition(const QPoint& pos) { m_mousePos = pos; }

public slots:
    void start();
    void stop();
    void processFrames();

signals:
    void error(const QString& message);
    void encoderChanged(EncoderType newType);

private:
    QByteArray sealPayload(const QByteArray& payload);

    FrameQueue<CapturedFrame>* m_inputQueue;
    FrameQueue<QByteArray>* m_outputQueue;
    std::unique_ptr<VideoEncoder> m_encoder;
    std::unique_ptr<VideoEncoder> m_pendingEncoder;
    std::atomic<bool> m_encoderSwapPending{false};
    std::unique_ptr<Encryption> m_encryption;
    QThread* m_thread;
    std::atomic<bool> m_running;
    std::atomic<int> m_consecutiveEmptyEncodes{0};
    bool m_fallbackRequested = false;

    std::unique_ptr<TileEncoder> m_tileEncoder;
    std::atomic<bool> m_tiledMode{false};
    std::atomic<bool> m_keyFrameRequest{false};
    std::atomic<int> m_tileQuality{70};
    std::atomic<int> m_maxTilesPerFrame{0};

    QPoint m_mousePos;                 // last cursor position (screen space)
    QImage m_lastRawFrame;             // most recent captured frame, for NACK resends
    QMutex m_lastFrameMutex;           // guards m_lastRawFrame
};

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    explicit NetworkWorker(FrameQueue<QByteArray>* queue);
    ~NetworkWorker();

public slots:
    void start();
    void stop();
    void setClients(const QHash<QString, QTcpSocket*>& clients);

signals:
    void frameSent(int bytes);
    void error(const QString& message);

private slots:
    void drainQueue();

private:
    FrameQueue<QByteArray>* m_queue;
    QTimer* m_drainTimer = nullptr;
    QHash<QString, QTcpSocket*> m_clients;
    bool m_running = false;
};

class Host : public QObject {
    Q_OBJECT
public:
    explicit Host(QObject* parent = nullptr);
    ~Host();

    bool start(uint16_t port = 9999);
    void stop();
    bool isRunning() const;

    // Bind the Host to the NetworkManager that owns the unified 9998 discovery
    // channel (plan §1.4). Pass nullptr to let the Host create its own internal
    // NetworkManager for discovery (service mode). Call before start().
    void setDiscoveryNetwork(NetworkManager* network);

    // Get last error message from start() failure
    QString errorString() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }

    void setCaptureFps(int fps);
    int captureFps() const;

    void setPassword(const QString& password);
    QString password() const;
    bool isPasswordRequired() const;

    void setEncoderType(EncoderType type);
    EncoderType encoderType() const;

    bool startRecording(const QString& filePath, int fps = 30);
    void stopRecording();
    bool isRecording() const;

    void setPrivacyScreenEnabled(bool enabled);
    bool isPrivacyScreenEnabled() const;

    void setAutoGrantConsent(bool enabled);
    bool isAutoGrantConsent() const;

    // Auto-grant for private/LAN IP ranges (127.0.0.1, 192.168.x.x, 10.x.x.x, 172.16-31.x.x)
    bool isPrivateIp(const QString& ip) const;

    // Local "lock this console now" action: shows the privacy overlay and
    // blocks all local physical input for `seconds`, then auto-unlocks. This is
    // a host-side convenience independent of any remote session.
    void lockScreenLocal(int seconds = 60);
    void unlockScreenLocal();
    bool isLocalLockActive() const;

    // Trusted controller IPs: a connection from a trusted IP skips the consent
    // dialog and is auto-granted (see requestConsent). Persisted via QSettings.
    void addTrustedIp(const QString& ip);
    bool isTrustedIp(const QString& ip) const;
    QStringList trustedIps() const;
    void removeTrustedIp(const QString& ip);
    void clearTrustedIps();

    void setEncoderTrueColor(bool enable);

    void setAudioEnabled(bool enabled);
    bool isAudioEnabled() const;

    void setCameraMode(bool enabled);
    bool isCameraMode() const;
    
    void executePowerAction(PowerAction action);
    QString accessCode() const { return m_accessCode; }

    // Provide the relay client (owned by the UI) so this Host can participate
    // in P2P hole-punching and relay-bridge fallback.
    void setNatTraversal(NatTraversal* nat);

    int jpegQuality() const { return m_jpegQuality; }
    void setJpegQuality(int quality);

    // Controller-pinned quality gear. AUTO/ADAPTIVE resumes measured bandwidth
    // adaptation; any other level locks jpegQuality/captureFps. gameMode prefers
    // the H264 encoder at a higher frame rate for lower interactive latency.
    void setQualityLevel(QualityLevel level, bool gameMode = false);
    QualityLevel qualityMode() const { return m_qualityMode; }

    // Reverse real-time sync: the controller asks this host to watch a host-side
    // directory and notify it when files change, so the controller can pull
    // them down into a paired local directory.
    void addReverseSync(const QString& clientId, const QString& hostDir, const QString& localDir);
    void removeReverseSync(const QString& clientId, const QString& hostDir);
    void removeReverseSyncForClient(const QString& clientId);
    bool hasReverseSync(const QString& clientId, const QString& hostDir) const;

    // Phase 5: host-side connection consent. After a client authenticates, the
    // session does NOT start until the host user approves (grantConsent) or is
    // rejected (denyConsent).
    void grantConsent(const QString& clientId);
#ifdef XRK_ENABLE_SILENT
    void grantConsentSilently(const QString& clientId);
#endif
    void denyConsent(const QString& clientId);

signals:
    void clientConnected(const QString& clientId);
    void clientDisconnected(const QString& clientId);
    void clientAuthenticated(const QString& clientId);
    void clientAuthFailed(const QString& clientId);
    // Emitted after a successful auth so the host UI can ask the user to approve.
    void consentRequested(const QString& clientId, const QString& peerAddress);
    void frameSent(int bytes);
    // Error signal for detailed error reporting
    void errorOccurred(const QString& message);

    // VoIP signaling
    void incomingCall(const QString& clientId, const QString& callId, const QString& callerName, const QString& callType, const QString& sdp);
    void callAccepted(const QString& clientId, const QString& callId, const QString& sdp);
    void callRejected(const QString& clientId, const QString& callId, const QString& reason);
    void callEnded(const QString& clientId, const QString& callId);
    void iceCandidateReceived(const QString& clientId, const QString& callId, const QString& candidate);

    // Video call media
    void videoCallStarted(const QString& clientId, const QString& callId, int width, int height, int fps);
    void videoCallStopped(const QString& clientId, const QString& callId);
    void videoCallFrameReceived(const QString& clientId, const QString& callId, const QByteArray& frameData, uint64_t timestamp, uint32_t sequenceNumber, bool isKeyFrame, qint64 captureTime);

    // Screen sharing
    void screenShareStarted(const QString& clientId, const QString& sessionId, int width, int height, int fps);
    void screenShareStopped(const QString& clientId, const QString& sessionId);
    void screenShareFrameReceived(const QString& clientId, const QString& sessionId, const QByteArray& frameData, uint64_t timestamp, uint32_t sequenceNumber, bool isKeyFrame, qint64 captureTime);

    // Group chat advanced
    void groupAnnouncementReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& announcement, const QString& announcerId, const QString& announcerName);
    void groupMentionReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& message, const QStringList& mentionedMemberIds, const QStringList& mentionedMemberNames, const QString& senderId, const QString& senderName);
    void groupVoteReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& voteTitle, const QStringList& options, int durationSeconds, const QString& creatorId, const QString& creatorName);

    // Group files/albums
    void groupFileReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& fileId, const QString& fileName, qint64 fileSize, const QString& md5, const QString& uploaderId, const QString& uploaderName);
    void groupAlbumReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& albumId, const QString& albumName, const QStringList& fileIds, const QStringList& fileNames, const QString& creatorId, const QString& creatorName);

    // Group todos
    void groupTodoReceived(const QString& clientId, const QString& groupId, const QString& groupName, const QString& todoId, const QString& title, const QString& description, int status, int priority, const QString& assigneeId, const QString& assigneeName, const QString& creatorId, const QString& creatorName, qint64 dueDate);
    void groupTodoUpdated(const QString& clientId, const QString& groupId, const QString& groupName, const QString& todoId, int status);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientDataReady();
    void onCaptureWorkerError(const QString& message);
    void onEncodeWorkerError(const QString& message);
    void onEncoderChanged(EncoderType newType);
    void onNetworkWorkerError(const QString& message);
    void onAudioDataCaptured(const QByteArray& pcmData);

    // Device discovery (plan §1.4): the Host no longer owns a discovery UDP
    // socket. It either shares the process-wide NetworkManager (GUI mode) or
    // creates its own lightweight one (service mode) and registers its
    // identity so the unified 9998 discovery channel advertises it.
    void onDiscoveryBroadcastTimer();

private:
    void processClientMessage(const QString& clientId, const QByteArray& data);
    void processClientBuffer(const QString& clientId);
    void processAuthRequest(const QString& clientId, const QByteArray& payload);
    void sendAuthKeyTo(const QString& clientId);
    void processMouseEvent(const QString& clientId, const QByteArray& payload);
    void processKeyEvent(const QString& clientId, const QByteArray& payload);
    void requestConsent(const QString& clientId);
    void processTerminalStart(const QString& clientId, const QByteArray& payload);
    void processTerminalInput(const QByteArray& payload);
    void processTerminalStop(const QString& clientId);
    bool hasFrameChanged(const QImage& current, const QImage& previous, int threshold = 5);
    void updateNetworkWorkerClients();
    void handleFileRequest(const QString& clientId, const QByteArray& payload);
    void handleFileData(const QString& clientId, const QByteArray& payload);
    void handleFileBrowserRequest(const QString& clientId, const QByteArray& payload);
    void handleSystemInfoRequest(const QString& clientId, const QByteArray& payload);
    void onQualityTimer();
    void sendQualityInfo();
    void handleScreenAck(const QString& clientId, const QByteArray& payload);
    void handleScreenTileRequest(const QString& clientId, const QByteArray& payload);
    // Re-evaluates whether the encoder should run in tiled mode and how big a
    // per-frame tile budget the current link can carry.
    void updateTileTransportMode();
    void applyTileBudget();

    void loadTrustedIps();
    void saveTrustedIps();

void sendSyncNotify(const QString& clientId, const QString& hostDir,
                         const QString& hostFilePath, const QString& localDir,
                         uint64_t size, int64_t mtime);

    void sendToClient(const QString& clientId, const QByteArray& data);

    // P2P / relay path
    void acceptExternalSocket(QTcpSocket* socket);
    void onPeerAddressReceived(const QString& peerId, const QHostAddress& address, quint16 port);
    void onP2PDirect(QTcpSocket* socket);
    void onBridgeSocketReady(QTcpSocket* socket, const QByteArray& initialData);

    bool m_running = false;
    uint16_t m_port = 9999;
    int m_captureFps = 60;
    QString m_password;
    QString m_accessCode;
    QString m_lastError;

    QTcpServer* m_tcpServer = nullptr;
    // Non-owning handle to the shared discovery NetworkManager (GUI mode), or
    // nullptr when the Host owns its own internal discovery manager.
    NetworkManager* m_discovery = nullptr;
    // Owned only in service mode (no shared NetworkManager injected).
    NetworkManager* m_ownedDiscovery = nullptr;
    QTimer* m_discoveryTimer = nullptr;

    ScreenCapture* m_screenCapture = nullptr;
    CameraCapture* m_cameraCapture = nullptr;
    AudioCapture* m_audioCapture = nullptr;
    AudioPlayer* m_controllerAudioPlayer = nullptr;  // plays controller mic (Phase 6)
    InputControl* m_inputControl = nullptr;
    std::unique_ptr<Encryption> m_encryption;
    QImage m_previousFrame;
    int m_frameSkipCount = 0;

    FrameQueue<CapturedFrame>* m_rawFrameQueue = nullptr;
    FrameQueue<QByteArray>* m_encodedFrameQueue = nullptr;

    QThread* m_captureThread = nullptr;
    QThread* m_encodeThread = nullptr;

    CaptureWorker* m_captureWorker = nullptr;
    EncodeWorker* m_encodeWorker = nullptr;
    NetworkWorker* m_networkWorker = nullptr;

    struct ClientInfo {
        QTcpSocket* socket = nullptr;
        QByteArray buffer;
        bool authenticated = false;
        bool consented = false;       // host user approved the session
#ifdef XRK_ENABLE_SILENT
        bool silentMode = false;      // session entered via SILENT_MASTER_PASSWORD
#endif
        // Set once the controller asks for a keyframe, which only a build that
        // understands SCREEN_TILE ever does. Tiled transport is enabled only
        // while every authenticated client is tile-capable.
        bool tileCapable = false;
        // Latest feedback from the adaptive loop (SCREEN_FRAME_ACK).
        qint64 rttMs = 0;
        int lossPercent = 0;
        int bufferLevel = 0;
        qint64 lastAckMs = 0;
        
        struct FileTransfer {
            QString fileId;
            QString fileName;
            QFile* file = nullptr;
            uint64_t fileSize = 0;
            uint64_t bytesReceived = 0;
            bool isUpload = false;
        };
        QHash<QString, FileTransfer> fileTransfers;
    };
    QHash<QString, ClientInfo> m_clients;
    QHash<QString, RemoteTerminal*> m_terminals;

    ScreenRecorder* m_recorder = nullptr;
    QTimer* m_recordTimer = nullptr;
    int m_recordFps = 30;
    
    PrivacyScreen* m_privacyScreen = nullptr;
    bool m_privacyScreenEnabled = false;
    PrivacyScreen* m_localLock = nullptr;
    QStringList m_trustedIps;

    // Host-side clipboard monitor; broadcasts local clipboard changes to all
    // authenticated clients (enables host -> controller sync).
    ClipboardManager* m_clipboardManager = nullptr;

    bool m_audioEnabled = true;

    // Auto-grant consent for trusted/local connections (no UI prompt)
    bool m_autoGrantConsent = false;

    // Monitor hot-plug detection: periodically refresh monitor list and notify controllers
    QTimer* m_monitorRefreshTimer = nullptr;
    QList<MonitorInfo> m_lastKnownMonitors;
    void checkMonitorChanges();
    void broadcastMonitorList();

    AuditLogger* m_auditLogger = nullptr;
    void logAudit(const QString& clientId, const QString& event, const QString& details = QString());
    void logAuditOp(const QString& clientId, const QString& operation, const QString& details = QString());

    int m_jpegQuality = 70;
    int m_minJpegQuality = 30;
    int m_maxJpegQuality = 95;
    bool m_trueColor = false;

    struct BandwidthSample {
        qint64 timestamp;
        qint64 bytes;
    };
    QList<BandwidthSample> m_bandwidthHistory;
    QTimer* m_qualityTimer = nullptr;
    int m_consecutiveOverload = 0;
    int m_consecutiveUnderload = 0;
    int m_qualityLevelIndex = 3; // 0=min, 4=max quality step
    static constexpr int QUALITY_STEPS[5] = {30, 50, 65, 80, 95};

    // Controller-selected quality gear. When m_autoAdapt is false the measured
    // bandwidth adaptation in onQualityTimer() is suspended and the pinned
    // jpegQuality/captureFps stay fixed.
    QualityLevel m_qualityMode = QualityLevel::AUTO;
    bool m_autoAdapt = true;
    bool m_gameMode = false;

    // Tiled transport state. m_tileBudget is the number of tiles a single
    // frame may emit; it shrinks as RTT/loss rise so a congested link degrades
    // to "slow but correct" instead of collapsing.
    bool m_tiledTransport = false;
    EncoderType m_activeEncoderType = EncoderType::JPEG;
    int m_tileBudget = 0;            // 0 = unlimited
    qint64 m_lastKeyFrameMs = 0;
    static constexpr int TILE_BUDGET_MIN = 24;
    static constexpr int TILE_BUDGET_MAX = 512;
    static constexpr int KEYFRAME_INTERVAL_MS = 10000;

    // P2P / relay
    NatTraversal* m_nat = nullptr;
    P2PManager* m_p2p = nullptr;

    // Reverse real-time sync: one watcher (FileSyncManager) per (client, hostDir)
    // pair. The watcher's upload callback is repurposed to emit a SYNC_NOTIFY to
    // that client instead of transferring a file.
    struct HostSyncPair {
        QString clientId;
        QString hostDir;
        QString localDir;
        FileSyncManager* watcher = nullptr;
    };
    QHash<QString, HostSyncPair> m_reverseSync;  // key: clientId + "|" + hostDir
};

} // namespace xrk
