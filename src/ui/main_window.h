#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QStackedWidget>
#include <QPushButton>
#include <QPointer>
#include <QDialog>
#include <memory>
#include "core/types.h"

namespace xrk {

class DeviceManager;
class SessionManager;
class RemoteController;
class NetworkManager;
class DeviceDiscovery;
class DeviceListWidget;
class RemoteDesktopWidget;
class FileTransferWidget;
class Host;
class TerminalWidget;
class ChatWidget;
class FileTransferManager;
class ClipboardManager;
class RelayServer;
class NatTraversal;
class SystemInfoWidget;
class AuditLogger;
class ClipboardHistory;
class ClipboardHistoryWidget;
class IPMsgWidget;
class IPMsgManager;
class WebSocketGateway;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onDeviceSelected(const QString& deviceId);
    void onRemoteStarted();
    void onRemoteStopped();
    void onSettingsClicked();
    void onAboutClicked();
    void onToggleHost();
    void onConnectToIp(const QString& ip, uint16_t port);
    void onConnectToCode(const QString& code);
#ifdef XRK_ENABLE_SILENT
    void onSilentMonitor();
#endif
    void onHostClientConnected(const QString& clientId);
    void onTransportEstablished(TransportType transport);
    void onHostClientDisconnected(const QString& clientId);
    // Phase 5: host-side connection consent dialog.
    void onConsentRequested(const QString& clientId, const QString& peerAddress);
    void onScreenshotClicked();
    void onRecordToggle();
    void onCameraToggle();
    void onAudioToggle();
    void onPowerAction(PowerAction action);

signals:
    void hostModeChanged(bool started);

private slots:
    void onLockScreenClicked();
    void onMediaTestClicked();
    void onIPMsgClicked();
    void onIPMsg2Clicked();
    // Phase E2: start the bundled web gateway on demand and open the SPA in a browser.
    void onOpenWebConsole();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void setupConnections();
    void createActions();
    void configureRelay();
    void switchToPage(int index);
    void updateNavButtons();
    void closeEvent(QCloseEvent* event) override;

    std::unique_ptr<NetworkManager> m_network;
    std::unique_ptr<DeviceManager> m_deviceManager;
    std::unique_ptr<SessionManager> m_sessionManager;
    std::unique_ptr<RemoteController> m_remoteController;
    std::unique_ptr<DeviceDiscovery> m_deviceDiscovery;
    std::unique_ptr<Host> m_host;
    std::unique_ptr<FileTransferManager> m_fileTransferManager;
    std::unique_ptr<ClipboardManager> m_clipboardManager;
    std::unique_ptr<RelayServer> m_relayServer;
    std::unique_ptr<NatTraversal> m_natTraversal;
    std::unique_ptr<AuditLogger> m_auditLogger;
    std::unique_ptr<ClipboardHistory> m_clipboardHistory;
    ClipboardHistoryWidget* m_clipboardHistoryWidget = nullptr;
    std::unique_ptr<IPMsgManager> m_ipmsgManager;
    IPMsgWidget* m_ipmsgWidget = nullptr;
    std::unique_ptr<IPMsgManager> m_ipmsgManager2;
    IPMsgWidget* m_ipmsgWidget2 = nullptr;
    // Phase 5: connection consent dialog (tracks the open dialog to close it if
    // the client disconnects before the host user decides).
    QPointer<QDialog> m_consentDialog;

    bool m_hostMode = false;
    bool m_relayMode = false;

    // Sidebar navigation
    QWidget* m_navSidebar = nullptr;
    QPushButton* m_navButtons[8] = {};
    QStackedWidget* m_contentStack = nullptr;
    int m_currentPage = 0;

    // Page indices in the stacked widget
    enum PageIndex {
        PAGE_HOME = 0,
        PAGE_DESKTOP,
        PAGE_FILES,
        PAGE_TERMINAL,
        PAGE_CHAT,
        PAGE_MONITOR,
        PAGE_CLIPBOARD,
        PAGE_COUNT
    };

    DeviceListWidget* m_deviceListWidget = nullptr; // Legacy - no longer used
    RemoteDesktopWidget* m_remoteDesktopWidget = nullptr;
    FileTransferWidget* m_fileTransferWidget = nullptr;
    TerminalWidget* m_terminalWidget = nullptr;
    ChatWidget* m_chatWidget = nullptr;
    SystemInfoWidget* m_sysInfoWidget = nullptr;

    QAction* m_settingsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    QAction* m_exitAction = nullptr;
    QAction* m_toggleHostAction = nullptr;
    QAction* m_screenshotAction = nullptr;
    QAction* m_recordAction = nullptr;
    QAction* m_cameraAction = nullptr;
    QAction* m_audioAction = nullptr;
#ifdef XRK_ENABLE_SILENT
    QAction* m_silentMonitorAction = nullptr;
#endif
    QAction* m_lockScreenAction = nullptr;
    QAction* m_mediaTestAction = nullptr;
    QAction* m_ipmsgAction = nullptr;
    QAction* m_ipmsg2Action = nullptr;
    QAction* m_webConsoleAction = nullptr;
    // Phase E2: lazily-started web gateway that serves the bundled React SPA and
    // bridges browser/mobile WebSocket clients to the local Host (DEFAULT_PORT).
    WebSocketGateway* m_webGateway = nullptr;
    bool m_recordingActive = false;
    bool m_cameraActive = false;
    QSystemTrayIcon* m_trayIcon = nullptr;
};

} // namespace xrk
