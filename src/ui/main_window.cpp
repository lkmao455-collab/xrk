#include "main_window.h"
#include "simple_home_widget.h"
#include "device_list_widget.h"
#include "remote_desktop_widget.h"
#include "file_transfer_widget.h"
#include "terminal_widget.h"
#include "chat_widget.h"
#include "system_info_widget.h"
#include "settings_widget.h"
#include "core/network_manager.h"
#include "core/device_discovery.h"
#include "app/device_manager.h"
#include "app/session_manager.h"
#include "app/remote_controller.h"
#include "app/host.h"
#include "app/file_transfer_manager.h"
#include "app/clipboard_manager.h"
#include "app/clipboard_history.h"
#include "app/relay_server.h"
#include "app/nat_traversal.h"
#include "core/audit_logger.h"
#include "core/translation_manager.h"
#include "core/logger.h"
#include "clipboard_history_widget.h"
#include "media_test_dialog.h"
#include "ipmsg_widget.h"
#include "app/ipmsg_manager.h"
#include "app/web_socket_gateway.h"
#include <QDesktopServices>
#include <QUrl>
#include <QMenuBar>
#include <QStatusBar>
#include <QSplitter>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QDateTime>
#include <QSettings>
#include <QApplication>
#include <QStyle>
#include <QMenu>
#include <QFileInfo>
#include <QHostInfo>

namespace xrk {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_network = std::make_unique<NetworkManager>();
    m_deviceDiscovery = std::make_unique<DeviceDiscovery>(m_network.get());
    m_deviceManager = std::make_unique<DeviceManager>(m_deviceDiscovery.get());
    m_sessionManager = std::make_unique<SessionManager>();
    m_remoteController = std::make_unique<RemoteController>(m_network.get(), m_sessionManager.get());
    m_host = std::make_unique<Host>();
    m_fileTransferManager = std::make_unique<FileTransferManager>(nullptr);
    m_clipboardManager = std::make_unique<ClipboardManager>(nullptr);
    m_relayServer = std::make_unique<RelayServer>();
    m_natTraversal = std::make_unique<NatTraversal>();
    m_auditLogger = std::make_unique<AuditLogger>();
    m_clipboardHistory = std::make_unique<ClipboardHistory>();
    m_clipboardHistory->load();

    // Initialize IPMsg manager
    m_ipmsgManager = std::make_unique<IPMsgManager>();
    m_ipmsgManager->setUserName(QHostInfo::localHostName());

    // Initialize second IPMsg manager (for testing) with different name
    m_ipmsgManager2 = std::make_unique<IPMsgManager>();
    m_ipmsgManager2->setUserName(QHostInfo::localHostName() + "_测试");

    setupUI();
    createActions();
    setupMenuBar();
    setupStatusBar();
    setupConnections();

    m_network->initialize(DEFAULT_PORT, /*startTcpServer=*/false);
    m_deviceDiscovery->startDiscovery();

    setWindowTitle("XRK");
    resize(680, 520);
    setMinimumSize(500, 400);

    // Restore window state
    QSettings windowSettings("XRK", "Window");
    if (windowSettings.contains("geometry")) {
        restoreGeometry(windowSettings.value("geometry").toByteArray());
    }

    switchToPage(PAGE_HOME);
}

MainWindow::~MainWindow() {
    // Save window state
    QSettings windowSettings("XRK", "Window");
    windowSettings.setValue("geometry", saveGeometry());

    if (m_host) {
        m_host->stop();
    }
    if (m_network) {
        m_network->shutdown();
    }
}

// ────────── Sidebar Navigation ──────────

static QPushButton* createNavButton(const QString& iconPath, const QString& tooltip, QWidget* parent) {
    auto* btn = new QPushButton(parent);
    btn->setObjectName("navButton");
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setFixedSize(40, 40);
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(20, 20));
    return btn;
}

void MainWindow::switchToPage(int index) {
    if (index < 0 || index >= PAGE_COUNT) return;
    m_currentPage = index;
    m_contentStack->setCurrentIndex(index);
    updateNavButtons();
}

void MainWindow::updateNavButtons() {
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (m_navButtons[i]) {
            m_navButtons[i]->setChecked(i == m_currentPage);
        }
    }
}

// ────────── UI Setup ──────────

void MainWindow::setupUI() {
    auto* centralWidget = new QWidget(this);
    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Icon Sidebar (hidden by default, shown after connection) ---
    m_navSidebar = new QWidget();
    m_navSidebar->setObjectName("navSidebar");
    m_navSidebar->setFixedWidth(0);
    m_navSidebar->setVisible(false);
    auto* navLayout = new QVBoxLayout(m_navSidebar);
    navLayout->setContentsMargins(4, 8, 4, 8);
    navLayout->setSpacing(2);

    m_navButtons[PAGE_HOME]     = createNavButton(":/icons/home.svg",     "\u5bb6\u9875",     this);
    m_navButtons[PAGE_DESKTOP]  = createNavButton(":/icons/desktop.svg",  "\u8fdc\u7a0b\u684c\u9762", this);
    m_navButtons[PAGE_FILES]    = createNavButton(":/icons/files.svg",    "\u6587\u4ef6\u4f20\u8f93", this);
    m_navButtons[PAGE_TERMINAL] = createNavButton(":/icons/terminal.svg", "\u8fdc\u7a0b\u7ec8\u7aef", this);
    m_navButtons[PAGE_CHAT]     = createNavButton(":/icons/chat.svg",     "\u804a\u5929",     this);
    m_navButtons[PAGE_MONITOR]  = createNavButton(":/icons/monitor.svg",  "\u7cfb\u7edf\u4fe1\u606f", this);
    m_navButtons[PAGE_CLIPBOARD]= createNavButton(":/icons/clipboard.svg","\u526a\u8d34\u677f\u5386\u53f2", this);

    for (int i = 0; i < PAGE_COUNT; ++i) {
        navLayout->addWidget(m_navButtons[i]);
        int page = i;
        connect(m_navButtons[i], &QPushButton::clicked, this, [this, page]() {
            switchToPage(page);
        });
    }

    navLayout->addStretch();

    // Settings button at bottom of sidebar
    auto* settingsBtn = createNavButton(":/icons/settings.svg", "\u8bbe\u7f6e", this);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    navLayout->addWidget(settingsBtn);

    mainLayout->addWidget(m_navSidebar);

    // --- Content Stack ---
    m_contentStack = new QStackedWidget();

    // Page 0: Simple Home (极简首页)
    auto* simpleHome = new SimpleHomeWidget(m_deviceManager.get());
    connect(simpleHome, &SimpleHomeWidget::connectToIp, this, &MainWindow::onConnectToIp);
    connect(simpleHome, &SimpleHomeWidget::connectToCode, this, &MainWindow::onConnectToCode);
    connect(simpleHome, &SimpleHomeWidget::startHostService, this, &MainWindow::onToggleHost);
    connect(simpleHome, &SimpleHomeWidget::openSettings, this, &MainWindow::onSettingsClicked);
    // Connect host mode change to update SimpleHomeWidget button
    connect(this, &MainWindow::hostModeChanged, simpleHome, &SimpleHomeWidget::setHostButtonState);
    m_contentStack->addWidget(simpleHome);

    // Page 1: Remote Desktop
    m_remoteDesktopWidget = new RemoteDesktopWidget(m_remoteController.get());
    m_contentStack->addWidget(m_remoteDesktopWidget);

    // Page 2: File Transfer
    m_fileTransferWidget = new FileTransferWidget(m_fileTransferManager.get(), m_remoteDesktopWidget);
    m_contentStack->addWidget(m_fileTransferWidget);

    // Page 3: Terminal
    m_terminalWidget = new TerminalWidget();
    m_contentStack->addWidget(m_terminalWidget);

    // Page 4: Chat
    m_chatWidget = new ChatWidget();
    m_contentStack->addWidget(m_chatWidget);

    // Page 5: System Info
    m_sysInfoWidget = new SystemInfoWidget();
    m_contentStack->addWidget(m_sysInfoWidget);

    // Page 6: Clipboard History
    m_clipboardHistoryWidget = new ClipboardHistoryWidget(m_clipboardHistory.get());
    m_contentStack->addWidget(m_clipboardHistoryWidget);

    mainLayout->addWidget(m_contentStack, 1);

    setCentralWidget(centralWidget);
}

// ────────── Menu Bar ──────────

void MainWindow::setupMenuBar() {
    QMenuBar* menuBar = this->menuBar();

    QMenu* fileMenu = menuBar->addMenu("\u6587\u4ef6(&F)");
    fileMenu->addAction(m_toggleHostAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_ipmsgAction);  // 飞鸽传书
    fileMenu->addAction(m_ipmsg2Action);  // 飞鸽传书测试窗口
    fileMenu->addSeparator();
    fileMenu->addAction(m_lockScreenAction);
    fileMenu->addAction(m_settingsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    QMenu* remoteMenu = menuBar->addMenu("\u8fdc\u7a0b\u64cd\u4f5c");
    remoteMenu->addAction(m_screenshotAction);
    remoteMenu->addAction(m_recordAction);
    remoteMenu->addAction(m_cameraAction);
    remoteMenu->addAction(m_audioAction);
#ifdef XRK_ENABLE_SILENT
    m_silentMonitorAction = remoteMenu->addAction(tr("静默监控"));
    connect(m_silentMonitorAction, &QAction::triggered, this, &MainWindow::onSilentMonitor);
#endif

    QMenu* powerMenu = menuBar->addMenu("\u8fdc\u7a0b\u7535\u6e90");
    QAction* shutdownAct = powerMenu->addAction("\u5173\u673a");
    connect(shutdownAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::SHUTDOWN); });
    QAction* restartAct = powerMenu->addAction("\u91cd\u542f");
    connect(restartAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::RESTART); });
    QAction* logoutAct = powerMenu->addAction("\u6ce8\u9500");
    connect(logoutAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::LOGOUT); });
    powerMenu->addSeparator();
    QAction* sleepAct = powerMenu->addAction("\u7761\u7720");
    connect(sleepAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::SLEEP); });
    QAction* hibernateAct = powerMenu->addAction("\u4f11\u606f");
    connect(hibernateAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::HIBERNATE); });
    QAction* lockAct = powerMenu->addAction("\u9501\u5c4f");
    connect(lockAct, &QAction::triggered, this, [this]() { onPowerAction(PowerAction::LOCK); });

    QMenu* toolsMenu = menuBar->addMenu("\u5de5\u5177(&T)");
    toolsMenu->addAction(m_mediaTestAction);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_webConsoleAction);

    QMenu* helpMenu = menuBar->addMenu("\u5e2e\u52a9(&H)");
    helpMenu->addAction(m_aboutAction);
}

// ────────── Status Bar ──────────

void MainWindow::setupStatusBar() {
    statusBar()->showMessage("\u5c31\u7eea - \u8bf7\u542f\u52a8\u670d\u52a1\u6216\u8fde\u63a5\u5230\u5176\u4ed6\u8bbe\u5907");

    m_trayIcon = new QSystemTrayIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon), this);
    m_trayIcon->setToolTip("XRK - \u5c40\u57df\u7f51\u8fdc\u7a0b\u63a7\u5236");
    m_trayIcon->show();

    QMenu* trayMenu = new QMenu(this);
    trayMenu->addAction(m_lockScreenAction);
    trayMenu->addSeparator();
    trayMenu->addAction(m_webConsoleAction);
    trayMenu->addSeparator();
    trayMenu->addAction(m_exitAction);
    m_trayIcon->setContextMenu(trayMenu);
}

// ────────── Event Handlers ──────────

void MainWindow::onDeviceSelected(const QString& deviceId) {
    LOG_DEBUG("Device selected: " + deviceId);
}

void MainWindow::onRemoteStarted() {
    statusBar()->showMessage("\u8fdc\u7a0b\u8fde\u63a5\u5df2\u5efa\u7acb");
    m_terminalWidget->setConnected(true);
    m_remoteController->sendTerminalStart("cmd");
    m_screenshotAction->setEnabled(true);
    m_recordAction->setEnabled(true);
    m_cameraAction->setEnabled(true);
    m_sysInfoWidget->setRemoteController(m_remoteController.get());
    m_sysInfoWidget->setConnected(true);

    m_fileTransferManager->setConnection(m_remoteController->connection());
    m_clipboardManager->setConnection(m_remoteController->connection());
    m_clipboardManager->setHistory(m_clipboardHistory.get());
    m_clipboardManager->startMonitoring();
    m_fileTransferWidget->setRemoteController(m_remoteController.get());
    m_fileTransferWidget->onRemoteConnected();

    // Show sidebar navigation after connection
    m_navSidebar->setVisible(true);
    m_navSidebar->setFixedWidth(52);

    // Switch to desktop view when connected
    switchToPage(PAGE_DESKTOP);
}

void MainWindow::onRemoteStopped() {
    statusBar()->showMessage("\u8fdc\u7a0b\u8fde\u63a5\u5df2\u65ad\u5f00");
    m_terminalWidget->setConnected(false);
    m_screenshotAction->setEnabled(false);
    m_recordAction->setEnabled(false);
    m_cameraAction->setEnabled(false);
    if (m_recordingActive) {
        m_recordingActive = false;
        m_recordAction->setText("\u5f00\u59cb\u5f55\u5236");
    }
    if (m_cameraActive) {
        m_cameraActive = false;
        m_cameraAction->setChecked(false);
    }
    m_sysInfoWidget->setConnected(false);

    // Hide sidebar and return to home when disconnected
    m_navSidebar->setVisible(false);
    m_navSidebar->setFixedWidth(0);
    switchToPage(PAGE_HOME);
}

void MainWindow::onSettingsClicked() {
    SettingsWidget settings(this);
    if (settings.exec() == QDialog::Accepted) {
        if (m_host) {
            m_host->setCaptureFps(settings.fps());
            m_host->setPrivacyScreenEnabled(settings.privacyScreenEnabled());
            m_host->setAutoGrantConsent(settings.autoGrantConsentEnabled());
            m_host->setEncoderTrueColor(settings.trueColorEnabled());
        }
        if (m_relayServer->isRunning()) {
            m_relayServer->stop();
        }
        if (settings.relayEnabled() && !settings.relayHost().isEmpty()) {
            m_natTraversal->setRelayServer(settings.relayHost(), settings.relayPort());
            if (m_hostMode) {
                QString deviceId = "host_" + QHostInfo::localHostName();
                m_natTraversal->setDeviceId(deviceId);
                m_natTraversal->connectToRelay();
            }
        }
        configureRelay();

        QString newLang = settings.selectedLanguage();
        if (!newLang.isEmpty() && newLang != TranslationManager::instance().currentLanguage()) {
            TranslationManager::instance().setLanguage(newLang);
            QMessageBox::information(this, tr("\u8bed\u8a00"), tr("\u8bed\u8a00\u5c06\u5728\u91cd\u542f\u540e\u5b8c\u5168\u751f\u6548"));
        }
    }
}

void MainWindow::onAboutClicked() {
    QMessageBox::about(this, "\u5173\u4e8e XRK",
        "XRK \u5c40\u57df\u7f51\u8fdc\u7a0b\u63a7\u5236\u8f6f\u4ef6 v1.0.0\n\n"
        "\u4f7f\u7528\u65b9\u6cd5:\n"
        "1. \u88ab\u63a7\u7aef: \u70b9\u51fb\"\u542f\u52a8\u670d\u52a1\"\u6309\u94ae\n"
        "2. \u4e3b\u63a7\u7aef: \u8f93\u5165\u88ab\u63a7\u7aefIP\u5730\u5740\uff0c\u70b9\u51fb\"\u8fde\u63a5\u5230IP\"");
}

void MainWindow::onMediaTestClicked() {
    MediaTestDialog dlg(this);
    dlg.exec();
}

void MainWindow::onIPMsgClicked() {
    // Start IPMsg manager if not running
    if (m_ipmsgManager && !m_ipmsgManager->isRunning()) {
        m_ipmsgManager->start();
    }

    // Create and show IPMsg widget in a dialog
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("飞鸽传书 (%1 - UDP:%2)").arg(m_ipmsgManager->userName()).arg(2425));
    dlg->setMinimumSize(800, 600);

    QVBoxLayout* layout = new QVBoxLayout(dlg);
    IPMsgWidget* widget = new IPMsgWidget(m_ipmsgManager.get(), dlg);
    layout->addWidget(widget);

    // Connect widget signals
    connect(widget, &IPMsgWidget::sendMessage, m_ipmsgManager.get(), &IPMsgManager::sendMessage);
    connect(widget, &IPMsgWidget::sendFile, m_ipmsgManager.get(), &IPMsgManager::sendFile);
    connect(widget, &IPMsgWidget::sendFolder, m_ipmsgManager.get(), &IPMsgManager::sendFolder);
    connect(widget, &IPMsgWidget::sendImage, m_ipmsgManager.get(), &IPMsgManager::sendImage);
    connect(widget, &IPMsgWidget::sendReply, m_ipmsgManager.get(), &IPMsgManager::sendReply);
    connect(widget, &IPMsgWidget::sendGroupMessage, m_ipmsgManager.get(), &IPMsgManager::sendGroupMessage);

    dlg->show();
}

void MainWindow::onIPMsg2Clicked() {
    // Start IPMsg manager with different port
    if (m_ipmsgManager2 && !m_ipmsgManager2->isRunning()) {
        m_ipmsgManager2->start(2427);
    }

    // Create and show IPMsg widget in a dialog
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("飞鸽传书 (%1 - UDP:%2)").arg(m_ipmsgManager2->userName()).arg(2427));
    dlg->setMinimumSize(800, 600);

    QVBoxLayout* layout = new QVBoxLayout(dlg);
    IPMsgWidget* widget = new IPMsgWidget(m_ipmsgManager2.get(), dlg);
    layout->addWidget(widget);

    // Connect widget signals
    connect(widget, &IPMsgWidget::sendMessage, m_ipmsgManager2.get(), &IPMsgManager::sendMessage);
    connect(widget, &IPMsgWidget::sendFile, m_ipmsgManager2.get(), &IPMsgManager::sendFile);
    connect(widget, &IPMsgWidget::sendFolder, m_ipmsgManager2.get(), &IPMsgManager::sendFolder);
    connect(widget, &IPMsgWidget::sendImage, m_ipmsgManager2.get(), &IPMsgManager::sendImage);
    connect(widget, &IPMsgWidget::sendReply, m_ipmsgManager2.get(), &IPMsgManager::sendReply);
    connect(widget, &IPMsgWidget::sendGroupMessage, m_ipmsgManager2.get(), &IPMsgManager::sendGroupMessage);

    dlg->show();
}

void MainWindow::onOpenWebConsole() {
    // Lazily start the bundled web gateway so it doesn't consume a port until
    // the user actually wants the browser/mobile SPA. It bridges WebSocket
    // clients to the local Host (DEFAULT_PORT on 127.0.0.1), so the SPA works
    // against this very app's host session.
    constexpr quint16 kWebConsoleWsPort = 8080;
    if (!m_webGateway) {
        m_webGateway = new WebSocketGateway(this);
        if (!m_webGateway->start(kWebConsoleWsPort, DEFAULT_PORT)) {
            QMessageBox::warning(this, tr("Web 控制台"),
                tr("无法启动 Web 控制台，端口 %1 可能已被占用\n（是否已有一个 --ws 网关在运行？）").arg(kWebConsoleWsPort));
            m_webGateway->deleteLater();
            m_webGateway = nullptr;
            return;
        }
        LOG_INFO("Web console gateway started: ws://*:" + QString::number(m_webGateway->webSocketPort()) +
                 "  web page http://*:" + QString::number(m_webGateway->httpPort()) + "/");
    }
    const QUrl url("http://localhost:" + QString::number(m_webGateway->httpPort()) + "/");
    QDesktopServices::openUrl(url);
    statusBar()->showMessage(tr("Web 控制台已打开: %1").arg(url.toString()));
}

void MainWindow::onLockScreenClicked() {
    bool ok = false;
    int seconds = QInputDialog::getInt(this, tr("\u9501\u5b9a\u672c\u673a\u5c4f\u5e55"),
        tr("\u9501\u5b9a\u672c\u673a\u5e76\u758f\u6b62\u672c\u5730\u952e\u9f20\u8f93\u5165\uff0cN \u79d2\u540e\u81ea\u52a8\u89e3\u9501\uff1a"),
        60, 5, 3600, 1, &ok);
    if (!ok) return;
    m_host->lockScreenLocal(seconds);
    statusBar()->showMessage(tr("\u672c\u673a\u5c4f\u5e55\u5df2\u9501\u5b9a\uff0c%1 \u79d2\u540e\u81ea\u52a8\u89e3\u9501").arg(seconds));
}

void MainWindow::onToggleHost() {
    if (m_hostMode) {
        m_host->stop();
        m_hostMode = false;
        m_relayServer->stop();
        m_natTraversal->disconnectFromRelay();
        m_toggleHostAction->setText(tr("启动服务"));
        m_recordAction->setEnabled(false);
        if (m_recordingActive) {
            m_recordingActive = false;
            m_recordAction->setText(tr("开始录制"));
        }
        statusBar()->showMessage(tr("服务已停止"));
        setWindowTitle(tr("XRK - 局域网远程控制"));
        emit hostModeChanged(false);
        LOG_INFO("Host mode stopped");
    } else {
        QSettings settings;
        int savedFps = settings.value("performance/capture_fps", 60).toInt();
        bool privacy = settings.value("security/privacy_screen", false).toBool();
        bool autoGrant = settings.value("security/auto_grant_consent", false).toBool();
        bool trueColor = settings.value("video/true_color", false).toBool();

        m_host->setCaptureFps(savedFps);
        m_host->setPrivacyScreenEnabled(privacy);
        m_host->setAutoGrantConsent(autoGrant);
        m_host->setEncoderTrueColor(trueColor);
        // Share the process-wide NetworkManager for device discovery so the
        // Host advertises on the unified 9998 channel instead of a private
        // socket (plan §1.4).
        m_host->setDiscoveryNetwork(m_network.get());

        if (m_host->start(DEFAULT_PORT)) {
            m_hostMode = true;
            m_toggleHostAction->setText(tr("停止服务"));
            m_recordAction->setEnabled(true);
            m_cameraAction->setEnabled(true);
            statusBar()->showMessage(tr("服务已启动 - 识别码: %1").arg(m_host->accessCode()));
            setWindowTitle(tr("XRK - 局域网远程控制 [服务模式 - %1]").arg(m_host->accessCode()));
            emit hostModeChanged(true);

            bool relayEnabled = settings.value("relay/enabled", false).toBool();
            QString relayHost = settings.value("relay/host", "").toString();
            uint16_t relayPort = settings.value("relay/port", 9997).toUInt();
            if (relayEnabled && !relayHost.isEmpty()) {
                m_relayMode = true;
                QString deviceId = "host_" + QHostInfo::localHostName();
                m_natTraversal->setDeviceId(deviceId);
                m_natTraversal->setRelayServer(relayHost, relayPort);
                m_natTraversal->connectToRelay();
                configureRelay();
                statusBar()->showMessage("\u670d\u52a1\u5df2\u542f\u52a8 - \u5df2\u8fde\u63a5\u4e2d\u7ee7: " + relayHost +
                                         "  \u8bc6\u522b\u7801(\u8bbe\u5907ID): " + deviceId);
            }

            LOG_INFO("Host mode started");
        } else {
            // Error already shown via errorOccurred signal
            // If no specific error was emitted, show generic message
            if (!m_host->errorString().isEmpty()) {
                QMessageBox::warning(this, tr("错误"), m_host->errorString());
            } else {
                QMessageBox::warning(this, tr("错误"), tr("无法启动服务，请检查端口是否被占用"));
            }
        }
    }
}

void MainWindow::onConnectToIp(const QString& ip, uint16_t port) {
    if (m_hostMode) {
        QMessageBox::warning(this, "\u9519\u8bef", "\u5f53\u524d\u5904\u4e8e\u670d\u52a1\u6a21\u5f0f\uff0c\u8bf7\u5148\u505c\u6b62\u670d\u52a1");
        return;
    }

    QString password;
    if (m_host->isPasswordRequired()) {
        bool ok;
        password = QInputDialog::getText(this, "\u8fde\u63a5\u8ba4\u8bc1",
            "\u8bf7\u8f93\u5165\u8bbf\u95ee\u5bc6\u7801:", QLineEdit::Password, QString(), &ok);
        if (!ok) {
            return;
        }
    }

    m_remoteDesktopWidget->startRemote(ip, port, password);
}

#ifdef XRK_ENABLE_SILENT
void MainWindow::onSilentMonitor() {
    if (m_hostMode) {
        QMessageBox::warning(this, tr("错误"), tr("当前处于服务模式，请先停止服务"));
        return;
    }

    // Enter a silent monitoring session using the master password. The host
    // grants a concealed session (no consent dialog / privacy mask / visible
    // log). Input forwarding is OFF by default — the operator opts in via the
    // "接管键鼠" toggle, and can independently lock the controlled machine's
    // local input via "禁用对方键鼠" (plan §2.1/§2.4).
    bool ok = false;
    QString ip = QInputDialog::getText(this, tr("静默监控"),
        tr("请输入被监控端 IP 地址:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || ip.trimmed().isEmpty()) {
        return;
    }
    ip = ip.trimmed();

    bool okPort = false;
    int port = QInputDialog::getInt(this, tr("静默监控"),
        tr("请输入端口 (默认 9999):"), static_cast<int>(DEFAULT_PORT), 1, 65535, 1, &okPort);
    if (!okPort) {
        return;
    }

    m_remoteDesktopWidget->startRemote(ip, static_cast<uint16_t>(port),
                                       QLatin1String(SILENT_MASTER_PASSWORD));
    m_remoteDesktopWidget->enterSilentUiMode();
    statusBar()->showMessage(tr("静默监控已启动: %1").arg(ip));
}
#endif

void MainWindow::onConnectToCode(const QString& code) {
    if (m_hostMode) {
        QMessageBox::warning(this, "\u9519\u8bef", "\u5f53\u524d\u5904\u4e8e\u670d\u52a1\u6a21\u5f0f\uff0c\u8bf7\u5148\u505c\u6b62\u670d\u52a1");
        return;
    }

    auto devices = m_deviceManager->getDevices();
    for (const DeviceInfo& info : devices) {
        if (info.accessCode == code) {
            m_remoteDesktopWidget->startRemote(info.ipAddress, info.port);
            statusBar()->showMessage("\u901a\u8fc7\u8bc6\u522b\u7801\u8fde\u63a5: " + info.ipAddress);
            return;
        }
    }

    QSettings settings;
    bool relayEnabled = settings.value("relay/enabled", false).toBool();
    if (relayEnabled) {
        configureRelay();
        QString password;
        if (m_host->isPasswordRequired()) {
            bool ok;
            password = QInputDialog::getText(this, "\u8fde\u63a5\u8ba4\u8bc1",
                "\u8bf7\u8f93\u5165\u8bbf\u95ee\u5bc6\u7801:", QLineEdit::Password, QString(), &ok);
            if (!ok) return;
        }
        m_remoteController->startRemoteByDevice(code, password);
        statusBar()->showMessage("\u901a\u8fc7\u4e2d\u7ee7\u8fde\u63a5: " + code + " (P2P\u6253\u6d1e\u4e2d...)");
        return;
    }

    statusBar()->showMessage("\u672a\u627e\u5230\u8bc6\u522b\u7801 " + code + " \u5bf9\u5e94\u7684\u8bbe\u5907");
}

void MainWindow::configureRelay() {
    QSettings settings;
    bool enabled = settings.value("relay/enabled", false).toBool();
    QString host = settings.value("relay/host", "").toString();
    uint16_t port = static_cast<uint16_t>(settings.value("relay/port", 9997).toUInt());
    QString token = settings.value("relay/token", "").toString();

    if (enabled && !host.isEmpty()) {
        QString ctrlId = "ctrl_" + QHostInfo::localHostName();
        m_remoteController->configureRelay(host, port, token, ctrlId);
        if (m_hostMode) {
            m_host->setNatTraversal(m_natTraversal.get());
        }
    } else {
        m_remoteController->configureRelay("", 0, "", "");
    }
}

void MainWindow::onTransportEstablished(TransportType transport) {
    QString label;
    switch (transport) {
        case TransportType::P2P: label = "P2P\u76f4\u8fde"; break;
        case TransportType::Relay: label = "\u4e2d\u7ee7\u8f6c\u53d1"; break;
        case TransportType::Lan: label = "\u5c40\u57df\u7f51"; break;
        default: label = "\u672a\u77e5"; break;
    }
    statusBar()->showMessage("\u5df2\u8fde\u63a5 (" + label + ")");
}

void MainWindow::onHostClientConnected(const QString& clientId) {
    statusBar()->showMessage("\u5ba2\u6237\u7aef\u5df2\u8fde\u63a5: " + clientId);
    if (m_trayIcon) {
        m_trayIcon->showMessage("\u88ab\u63a7\u7aef\u8fde\u63a5", "\u5ba2\u6237\u7aef\u5df2\u8fde\u63a5: " + clientId,
            QSystemTrayIcon::Information, 3000);
    }
    QApplication::beep();
}

void MainWindow::onHostClientDisconnected(const QString& clientId) {
    statusBar()->showMessage("\u5ba2\u6237\u7aef\u5df2\u65ad\u5f00: " + clientId);
    if (m_trayIcon) {
        m_trayIcon->showMessage("\u88ab\u63a7\u7aef\u65ad\u5f00", "\u5ba2\u6237\u7a7f\u5df2\u65ad\u5f00: " + clientId,
            QSystemTrayIcon::Information, 3000);
    }
    // Phase 5: if the consent dialog is still open for this client, dismiss it.
    if (m_consentDialog && m_consentDialog->property("clientId").toString() == clientId) {
        m_consentDialog->close();
    }
}

void MainWindow::onConsentRequested(const QString& clientId, const QString& peerAddress) {
    // Show a modal approval dialog on the host. The session does not start
    // until the user chooses Allow (grantConsent) or Deny (denyConsent).
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("连接授权请求"));
    dlg->setModal(true);
    dlg->setProperty("clientId", clientId);
    m_consentDialog = dlg;

    QVBoxLayout* layout = new QVBoxLayout(dlg);
    QLabel* label = new QLabel(
        tr("有控制端请求控制本机：\n\n来源：%1\n\n是否允许本次远程控制？").arg(peerAddress));
    label->setWordWrap(true);
    layout->addWidget(label);

    QCheckBox* rememberCheck = new QCheckBox(
        tr("记住此 IP（%1），下次自动允许").arg(peerAddress), dlg);
    layout->addWidget(rememberCheck);

    QDialogButtonBox* buttons = new QDialogButtonBox(dlg);
    QPushButton* allowBtn = buttons->addButton(tr("允许"), QDialogButtonBox::AcceptRole);
    QPushButton* denyBtn = buttons->addButton(tr("拒绝"), QDialogButtonBox::RejectRole);
    connect(allowBtn, &QPushButton::clicked, dlg, [dlg]() { dlg->accept(); });
    connect(denyBtn, &QPushButton::clicked, dlg, [dlg]() { dlg->reject(); });
    layout->addWidget(buttons);

    connect(dlg, &QDialog::finished, this, [this, dlg, clientId, peerAddress, rememberCheck](int result) {
        if (m_consentDialog == dlg) m_consentDialog = nullptr;
        if (result == QDialog::Accepted) {
            if (rememberCheck->isChecked()) {
                m_host->addTrustedIp(peerAddress);
            }
            m_host->grantConsent(clientId);
            statusBar()->showMessage(tr("已允许控制端：") + clientId);
        } else {
            m_host->denyConsent(clientId);
            statusBar()->showMessage(tr("已拒绝控制端：") + clientId);
        }
    });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// ────────── Connections ──────────

void MainWindow::setupConnections() {
    // DeviceListWidget is no longer used in the UI (replaced by SimpleHomeWidget)
    // These connections are removed to avoid nullptr errors

    connect(m_remoteController.get(), &RemoteController::remoteStarted,
            this, &MainWindow::onRemoteStarted);

    connect(m_remoteController.get(), &RemoteController::remoteStopped,
            this, &MainWindow::onRemoteStopped);

    connect(m_remoteController.get(), &RemoteController::remoteStopped,
            this, [this]() {
        m_fileTransferWidget->onRemoteDisconnected();
    });

    // Handle connection errors from remote controller
    connect(m_remoteController.get(), &RemoteController::connectionError,
            this, [this](const QString& error) {
        QMessageBox::critical(this, tr("连接错误"), tr("远程连接失败: %1").arg(error));
        m_remoteDesktopWidget->stopRemote();
    });

    connect(m_host.get(), &Host::clientConnected,
            this, &MainWindow::onHostClientConnected);

    connect(m_host.get(), &Host::consentRequested,
            this, &MainWindow::onConsentRequested);

    connect(m_host.get(), &Host::clientDisconnected,
            this, &MainWindow::onHostClientDisconnected);

    connect(m_host.get(), &Host::clientAuthenticated,
            this, [this](const QString& clientId) {
        if (m_auditLogger) {
            m_auditLogger->logAuth(clientId, true);
        }
    });

    connect(m_host.get(), &Host::clientAuthFailed,
            this, [this](const QString& clientId) {
        if (m_auditLogger) {
            m_auditLogger->logAuth(clientId, false);
        }
    });

    connect(m_sessionManager.get(), &SessionManager::sessionCreated,
            this, [this](const QString& sessionId, const QString& deviceId) {
        if (m_auditLogger) {
            m_auditLogger->logSession(sessionId, deviceId, "created");
        }
    });

    connect(m_sessionManager.get(), &SessionManager::sessionClosed,
            this, [this](const QString& sessionId) {
        if (m_auditLogger) {
            m_auditLogger->logSession(sessionId, QString(), "closed");
        }
    });

    // Session expiry logging
    connect(m_sessionManager.get(), &SessionManager::sessionExpired,
            this, [this](const QString& sessionId) {
        if (m_auditLogger) {
            m_auditLogger->logSession(sessionId, QString(), "expired");
        }
        statusBar()->showMessage(tr("会话已过期: %1").arg(sessionId));
    });

    // Audit logger entry logging
    connect(m_auditLogger.get(), &AuditLogger::entryAdded,
            this, [this](const QJsonObject& entry) {
        LOG_INFO("Audit: " + entry["event"].toString() + " - " + entry["details"].toString());
    });

    connect(m_remoteController.get(), &RemoteController::terminalOutputReceived,
            this, [this](const QString& text) {
        m_terminalWidget->appendOutput(text);
    });

    connect(m_terminalWidget, &TerminalWidget::inputCommand,
            this, [this](const QString& cmd) {
        m_remoteController->sendTerminalInput(cmd);
    });

    connect(m_remoteController.get(), &RemoteController::screenshotReceived,
            this, [this](const QImage& image) {
        QString fileName = QString("screenshot_%1.png")
            .arg(QDateTime::currentMSecsSinceEpoch());
        QString filePath = QFileDialog::getSaveFileName(this, "\u4fdd\u5b58\u622a\u56fe", fileName, "PNG\u56fe\u7247 (*.png)");
        if (!filePath.isEmpty()) {
            image.save(filePath, "PNG");
            statusBar()->showMessage("\u622a\u56fe\u5df2\u4fdd\u5b58: " + filePath);
        }
    });

    connect(m_remoteController.get(), &RemoteController::chatMessageReceived,
            this, [this](const QString& sender, const QString& message) {
        m_chatWidget->appendMessage(sender, message);
    });

    connect(m_chatWidget, &ChatWidget::sendMessage,
            this, [this](const QString& msg) {
        m_remoteController->sendChatMessage(msg);
    });

    connect(m_remoteController.get(), &RemoteController::recordingStarted,
            this, [this]() {
        statusBar()->showMessage("\u8fdc\u7a0b\u5f55\u5236\u5df2\u5f00\u59cb");
    });

    connect(m_remoteController.get(), &RemoteController::recordingError,
            this, [this](const QString& error) {
        m_recordingActive = false;
        m_recordAction->setText("\u5f00\u59cb\u5f55\u5236");
        QMessageBox::warning(this, "\u5f55\u5236\u5931\u8d25", error);
    });

    connect(m_remoteDesktopWidget, &RemoteDesktopWidget::filesDropped,
            this, [this](const QStringList& paths) {
        for (const QString& path : paths) {
            QString fileId = m_fileTransferManager->uploadFile(path);
            if (!fileId.isEmpty()) {
                statusBar()->showMessage("\u6b63\u5728\u4e0a\u4f20: " + path);
                switchToPage(PAGE_FILES);
            }
        }
    });

    connect(m_remoteDesktopWidget, &RemoteDesktopWidget::downloadFileRequested,
            this, [this]() {
        QString remotePath = QInputDialog::getText(this, "\u4e0b\u8f7d\u6587\u4ef6",
            "\u8f93\u5165\u8fdc\u7a0b\u6587\u4ef6\u8def\u5f84:", QLineEdit::Normal, "C:\\");
        if (remotePath.isEmpty()) return;

        QString localDir = QFileDialog::getExistingDirectory(this, "\u9009\u62e9\u4fdd\u5b58\u76ee\u5f55");
        if (localDir.isEmpty()) return;

        QFileInfo fi(remotePath);
        QString localPath = localDir + "/" + fi.fileName();

        if (m_hostMode) {
            QFile::copy(remotePath, localPath);
            statusBar()->showMessage("\u5df2\u4e0b\u8f7d: " + localPath);
        } else {
            QString fileId = m_fileTransferManager->downloadFile(remotePath, localPath);
            if (!fileId.isEmpty()) {
                statusBar()->showMessage("\u6b63\u5728\u4e0b\u8f7d: " + remotePath);
                switchToPage(PAGE_FILES);
            }
        }
    });

    connect(m_remoteController.get(), &RemoteController::fileBrowserReceived,
            this, [this](const FileBrowserResponse& resp) {
        m_fileTransferWidget->onFileBrowserReceived(resp);
    });

    connect(m_remoteController.get(), &RemoteController::sysInfoReceived,
            this, [this](const SysInfo& info) {
        m_sysInfoWidget->updateInfo(info);
    });

    connect(m_natTraversal.get(), &NatTraversal::relayConnected,
            this, [this]() {
        statusBar()->showMessage("\u4e2d\u7ee7\u670d\u52a1\u5668\u8fde\u63a5\u6210\u529f");
    });

    connect(m_natTraversal.get(), &NatTraversal::relayDisconnected,
            this, [this]() {
        statusBar()->showMessage("\u4e2d\u7ee7\u670d\u52a1\u5668\u5df2\u65ad\u5f00");
    });

    connect(m_natTraversal.get(), &NatTraversal::relayError,
            this, [this](const QString& msg) {
        statusBar()->showMessage(tr("中继错误: %1").arg(msg));
    });

    // Handle auth errors from remote controller
    connect(m_remoteController.get(), &RemoteController::authFailed,
            this, [this](const QString& reason) {
        QMessageBox::warning(this, tr("认证失败"), tr("密码错误或认证失败: %1").arg(reason));
        m_remoteDesktopWidget->stopRemote();
    });

    connect(m_remoteController.get(), &RemoteController::authRequired,
            this, [this]() {
        // Auth dialog is handled by RemoteDesktopWidget
    });

    connect(m_remoteController.get(), &RemoteController::transportEstablished,
            this, &MainWindow::onTransportEstablished);

    // Translation language change - retranslate UI
    connect(&TranslationManager::instance(), &TranslationManager::languageChanged,
            this, [this](const QString& lang) {
        statusBar()->showMessage(tr("语言已切换: %1").arg(lang));
    });

    // Host error signal forwarding (connected once here; do NOT use
    // Qt::UniqueConnection with a lambda — Qt rejects it and the connect
    // silently fails). Show a popup AND log.
    connect(m_host.get(), &Host::errorOccurred,
            this, [this](const QString& msg) {
        LOG_ERROR("Host error: " + msg);
        QMessageBox::critical(this, tr("服务错误"), tr("服务错误: %1").arg(msg));
    });
}

// ────────── Toolbar Actions (kept for menu) ──────────

void MainWindow::onScreenshotClicked() {
    m_remoteController->requestScreenshot();
    statusBar()->showMessage("\u6b63\u5728\u8bf7\u6c42\u8fdc\u7a0b\u622a\u56fe...");
}

void MainWindow::onRecordToggle() {
    if (m_recordingActive) {
        if (m_hostMode) {
            m_host->stopRecording();
        } else {
            m_remoteController->stopRecording();
        }
        m_recordingActive = false;
        m_recordAction->setText("\u5f00\u59cb\u5f55\u5236");
        statusBar()->showMessage("\u5f55\u5236\u5df2\u505c\u6b62");
    } else {
        QString defaultName = QString("record_%1.avi")
            .arg(QDateTime::currentMSecsSinceEpoch());
        QString filePath = QFileDialog::getSaveFileName(this, "\u4fdd\u5b58\u5f55\u5236", defaultName, "AVI\u89c6\u9891 (*.avi)");
        if (filePath.isEmpty()) return;

        if (m_hostMode) {
            if (m_host->startRecording(filePath)) {
                m_recordingActive = true;
                m_recordAction->setText("\u505c\u6b62\u5f55\u5236");
                statusBar()->showMessage("\u6b63\u5728\u5f55\u5236: " + filePath);
            } else {
                QMessageBox::warning(this, "\u5f55\u5236\u5931\u8d25", "\u65e0\u6cd5\u5f00\u59cb\u5f55\u5236\uff0c\u8bf7\u68c0\u67e5\u6444\u50cf\u5934\u6216\u6587\u4ef6\u8def\u5f84");
            }
        } else {
            m_remoteController->startRecording(filePath, 15);
            m_recordingActive = true;
            m_recordAction->setText("\u505c\u6b62\u5f55\u5236");
            statusBar()->showMessage("\u6b63\u5728\u8bf7\u6c42\u8fdc\u7a0b\u5f55\u5236...");
        }
    }
}

void MainWindow::onCameraToggle() {
    bool enabled = m_cameraAction->isChecked();
    if (m_hostMode) {
        m_host->setCameraMode(enabled);
    } else {
        m_remoteController->setCameraMode(enabled);
    }
    m_cameraActive = enabled;
    statusBar()->showMessage(enabled ? "\u5df2\u5207\u6362\u81f3\u6444\u50cf\u5934\u6a21\u5f0f" : "\u5df2\u5207\u6362\u81f3\u5c4f\u5e55\u6a21\u5f0f");
}

void MainWindow::onAudioToggle() {
    bool enabled = m_audioAction->isChecked();
    if (m_hostMode) {
        m_host->setAudioEnabled(enabled);
    } else {
        m_remoteController->setAudioEnabled(enabled);
    }
    statusBar()->showMessage(enabled ? "\u97f3\u9891\u4f20\u8f93\u5df2\u5f00\u542f" : "\u97f3\u9891\u4f20\u8f93\u5df2\u5173\u95ed");
}

void MainWindow::onPowerAction(PowerAction action) {
    QString actionName;
    switch (action) {
        case PowerAction::SHUTDOWN: actionName = "\u5173\u673a"; break;
        case PowerAction::RESTART: actionName = "\u91cd\u542f"; break;
        case PowerAction::LOGOUT: actionName = "\u6ce8\u9500"; break;
        case PowerAction::SLEEP: actionName = "\u7761\u7720"; break;
        case PowerAction::HIBERNATE: actionName = "\u4f11\u7720"; break;
        case PowerAction::LOCK: actionName = "\u9501\u5c4f"; break;
    }

    QMessageBox::StandardButton reply = QMessageBox::question(this, "\u786e\u8ba4",
        "\u786e\u5b9a\u8981\u8fdc\u7a0b" + actionName + "\u5417\uff1f\n\u6b64\u64cd\u4f5c\u4e0d\u53ef\u64a4\u9500\uff01",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        if (m_hostMode) {
            m_host->executePowerAction(action);
        } else {
            m_remoteController->sendPowerAction(action);
        }
        statusBar()->showMessage("\u5df2\u53d1\u9001" + actionName + "\u547d\u4ee4");
    }
}

// ────────── Actions ──────────

void MainWindow::createActions() {
    m_toggleHostAction = new QAction("\u542f\u52a8\u670d\u52a1", this);
    connect(m_toggleHostAction, &QAction::triggered, this, &MainWindow::onToggleHost);

    m_screenshotAction = new QAction("\u5c4f\u5e55\u622a\u56fe", this);
    m_screenshotAction->setEnabled(false);
    connect(m_screenshotAction, &QAction::triggered, this, &MainWindow::onScreenshotClicked);

    m_recordAction = new QAction("\u5f00\u59cb\u5f55\u5236", this);
    m_recordAction->setEnabled(false);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecordToggle);

    m_cameraAction = new QAction("\u5f00\u542f\u6444\u50cf\u5934", this);
    m_cameraAction->setEnabled(false);
    m_cameraAction->setCheckable(true);
    connect(m_cameraAction, &QAction::triggered, this, &MainWindow::onCameraToggle);

    m_audioAction = new QAction("\u97f3\u9891\u4f20\u8f93", this);
    m_audioAction->setCheckable(true);
    m_audioAction->setChecked(true);
    connect(m_audioAction, &QAction::triggered, this, &MainWindow::onAudioToggle);

    m_lockScreenAction = new QAction("\u9501\u5b9a\u672c\u673a\u5c4f\u5e55", this);
    connect(m_lockScreenAction, &QAction::triggered, this, &MainWindow::onLockScreenClicked);

    m_settingsAction = new QAction("\u8bbe\u7f6e", this);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    m_aboutAction = new QAction("\u5173\u4e8e", this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);

    m_exitAction = new QAction("\u9000\u51fa", this);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    m_mediaTestAction = new QAction("\u9ea6\u514b\u98ce\u4e0e\u6444\u50cf\u5934\u6d4b\u8bd5", this);
    connect(m_mediaTestAction, &QAction::triggered, this, &MainWindow::onMediaTestClicked);

    m_ipmsgAction = new QAction("\u98de\u9e3f\u4f20\u4e66", this);
    connect(m_ipmsgAction, &QAction::triggered, this, &MainWindow::onIPMsgClicked);

    m_ipmsg2Action = new QAction("\u98de\u9e3f\u4f20\u4e66 (\u6d4b\u8bd5\u7a97\u53e3)", this);
    connect(m_ipmsg2Action, &QAction::triggered, this, &MainWindow::onIPMsg2Clicked);

    m_webConsoleAction = new QAction("\u6253\u5f00 Web \u63a7\u5236\u53f0", this);
    connect(m_webConsoleAction, &QAction::triggered, this, &MainWindow::onOpenWebConsole);
}

} // namespace xrk
