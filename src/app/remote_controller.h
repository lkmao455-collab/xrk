#pragma once

#include <QObject>
#include <memory>
#include <QTimer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QThread>
#include <atomic>
#include "core/types.h"
#include "core/encryption.h"
#include "core/frame_queue.h"

namespace xrk {

class NetworkManager;
class SessionManager;
class TcpConnection;
class AudioPlayer;
class AudioCapture;
class NatTraversal;
class P2PManager;

class RemoteController : public QObject {
    Q_OBJECT
public:
    explicit RemoteController(NetworkManager* network, SessionManager* session, QObject* parent = nullptr);
    ~RemoteController();

    bool startRemote(const QString& ip, uint16_t port);
    bool startRemote(const QString& ip, uint16_t port, const QString& password);

    // Configure relay/P2P. When set, connect via startRemoteByDevice() which
    // attempts a direct P2P hole-punch and falls back to the relay bridge.
    void configureRelay(const QString& host, uint16_t port, const QString& token,
                        const QString& myDeviceId);
    bool startRemoteByDevice(const QString& deviceId, const QString& password);

    void stopRemote();

    // Phase 6: two-way voice. Captures the local microphone and streams it to
    // the host. No echo cancellation (LAN use).
    void setMicrophoneEnabled(bool enabled);
    bool isMicrophoneEnabled() const;
    bool isRemoteActive() const;
    QString currentIp() const;
    uint16_t currentPort() const;

    void setAutoReconnect(bool enabled);
    bool isAutoReconnect() const;

    void sendMouseEvent(const MouseEvent& event);
    void sendKeyEvent(const KeyEvent& event);
    void requestScreenFrame();

    void sendTerminalStart(const QString& shellType, uint32_t cols = 80, uint32_t rows = 25);
    void sendTerminalInput(const QString& command);
    void sendTerminalStop();
    void requestScreenshot();
    void sendChatMessage(const QString& message);
    void requestFileBrowser(const QString& path);
    void requestSystemInfo();
    void startRecording(const QString& filePath = QString(), int fps = 30);
    void stopRecording();
    void setCameraMode(bool enabled);
    void setAudioEnabled(bool enabled);
    void sendPowerAction(PowerAction action);
    void sendPrivacyScreen(bool enabled);
    void sendQualityLevel(QualityLevel level, bool gameMode = false);
    void sendSyncAdd(const QString& hostDir, const QString& localDir);
    void sendSyncRemove(const QString& hostDir);
    void requestMonitorList();
    void switchMonitor(int index);
    TcpConnection* connection() const { return m_connection.get(); }

signals:
    void remoteStarted(const QString& deviceId);
    void remoteStopped();
    void transportEstablished(TransportType transport);
    void screenFrameReceived(const ScreenFrame& frame);
    void connectionError(const QString& errorString);
    void reconnecting(int attempt, int maxAttempts);
    void reconnected();
    void reconnectFailed();
    void authRequired();
    void authSuccess();
    void authFailed(const QString& reason);
    // Phase 5: host-side connection consent.
    void consentRequested();    // host is deciding; show "waiting for approval"
    void consentGranted();      // host approved; session will start
    void consentDenied();       // host rejected the connection
    void microphoneStateChanged(bool enabled);
    void terminalOutputReceived(const QString& text);
    void screenshotReceived(const QImage& image);
    void chatMessageReceived(const QString& sender, const QString& message);
    void recordingStarted();
    void recordingStopped();
    void recordingError(const QString& error);
    void fileBrowserReceived(const FileBrowserResponse& response);
    void sysInfoReceived(const SysInfo& info);
    void qualityInfoReceived(const QualityInfo& info);
    void syncNotifyReceived(const SyncNotify& note);
    void monitorListReceived(const QList<MonitorInfo>& monitors);
    void latencyUpdated(qint64 ms);

private slots:
    void onMessageReceived(const QByteArray& data);
    void onConnectionLost(const QString& deviceId);
    void onReconnecting(int attempt, int maxAttempts);
    void onReconnected();
    void onReconnectFailed();

    // P2P / relay path
    void onRelayConnected();
    void onNatError(const QString& message);
    void onPeerAddressReceived(const QString& peerId, const QHostAddress& address, quint16 port);
    void onP2PDirect(QTcpSocket* socket);
    void onPunchFailed();
    void onBridgeSocketReady(QTcpSocket* socket, const QByteArray& initialData);
    void onRelayFallbackTimeout();

private:
    void processMessage(MessageType type, const QByteArray& payload);
    void sendHeartbeat();
    void handleScreenFrame(const QByteArray& data);
    void handleAuthResponse(const QByteArray& data);
    void sendAuthRequest(const QString& password);
    void sendMicAudio(const QByteArray& pcm);   // Phase 6: stream mic PCM to host

    // Async decode worker
    void startDecodeWorker();
    void stopDecodeWorker();

    // Wire a (direct/relay-bridged) TcpConnection into the data path. Returns
    // false if the connection is unusable.
    bool setupConnection(std::shared_ptr<TcpConnection> conn, const QString& peerLabel,
                         TransportType transport, const QString& password);
    void beginP2PConnect(const QString& deviceId, const QString& password);
    void fallbackToRelay();

    NetworkManager* m_network = nullptr;
    SessionManager* m_session = nullptr;
    std::shared_ptr<TcpConnection> m_connection;
    QTimer* m_heartbeatTimer = nullptr;
    qint64 m_roundTripMs = 0;
    QString m_currentIp;
    uint16_t m_currentPort = 0;
    QString m_currentSessionId;
    QString m_password;
    bool m_active = false;
    bool m_autoReconnect = true;
    std::unique_ptr<Encryption> m_encryption;
    AudioPlayer* m_audioPlayer = nullptr;
    AudioCapture* m_micCapture = nullptr;

    // Async decode
    FrameQueue<QByteArray>* m_decodeQueue = nullptr;
    QThread* m_decodeThread = nullptr;
    QObject* m_decodeWorkerCtx = nullptr;
    std::atomic<bool> m_decodeRunning{false};

    // P2P / relay state
    NatTraversal* m_nat = nullptr;
    P2PManager* m_p2p = nullptr;
    QTimer* m_relayFallbackTimer = nullptr;
    QString m_relayHost;
    uint16_t m_relayPort = 0;
    QString m_relayToken;
    QString m_myDeviceId;
    bool m_relayConfigured = false;
    QString m_pendingDeviceId;
    QString m_pendingPassword;
    bool m_p2pInProgress = false;
    TransportType m_transport = TransportType::Unknown;
};

} // namespace xrk
