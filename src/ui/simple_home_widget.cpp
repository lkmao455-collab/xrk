#include "simple_home_widget.h"
#include "app/device_manager.h"
#include "core/logger.h"
#include <QScrollArea>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QParallelAnimationGroup>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>

namespace xrk {

SimpleHomeWidget::SimpleHomeWidget(DeviceManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();
    setupStyle();
    loadRecentDevices();
    updateDeviceCards();

    if (m_manager) {
        connect(m_manager, &DeviceManager::deviceAdded, this, &SimpleHomeWidget::refreshDevices);
        connect(m_manager, &DeviceManager::deviceRemoved, this, &SimpleHomeWidget::refreshDevices);
        connect(m_manager, &DeviceManager::deviceUpdated, this, &SimpleHomeWidget::refreshDevices);
    }

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &SimpleHomeWidget::refreshDevices);
    m_refreshTimer->start(5000);
}

void SimpleHomeWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 30, 40, 30);
    mainLayout->setSpacing(0);

    // ---- Logo / Title ----
    QLabel* logo = new QLabel("XRK", this);
    logo->setObjectName("appLogo");
    logo->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(logo);

    QLabel* subtitle = new QLabel("局域网远程控制", this);
    subtitle->setObjectName("appSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(subtitle);

    mainLayout->addSpacing(30);

    // ---- Connect Input Area ----
    QWidget* connectCard = new QWidget(this);
    connectCard->setObjectName("connectCard");
    connectCard->setMaximumWidth(480);
    connectCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QVBoxLayout* cardLayout = new QVBoxLayout(connectCard);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(16);

    QLabel* inputLabel = new QLabel("输入 IP 地址或识别码连接", connectCard);
    inputLabel->setObjectName("inputLabel");
    inputLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(inputLabel);

    m_connectInput = new QLineEdit(connectCard);
    m_connectInput->setObjectName("connectInput");
    m_connectInput->setPlaceholderText("例如: 192.168.1.100 或 识别码");
    m_connectInput->setAlignment(Qt::AlignCenter);
    m_connectInput->setMinimumHeight(48);
    cardLayout->addWidget(m_connectInput);

    m_connectButton = new QPushButton("连 接", connectCard);
    m_connectButton->setObjectName("connectButton");
    m_connectButton->setMinimumHeight(44);
    cardLayout->addWidget(m_connectButton);

    // Center the card
    QHBoxLayout* cardCenterLayout = new QHBoxLayout();
    cardCenterLayout->addStretch();
    cardCenterLayout->addWidget(connectCard);
    cardCenterLayout->addStretch();
    mainLayout->addLayout(cardCenterLayout);

    mainLayout->addSpacing(20);

    // ---- Quick Actions ----
    QHBoxLayout* quickLayout = new QHBoxLayout();
    quickLayout->setAlignment(Qt::AlignCenter);
    quickLayout->setSpacing(12);

    m_hostButton = new QPushButton("启动服务（被控端）", this);
    m_hostButton->setObjectName("hostButton");
    quickLayout->addWidget(m_hostButton);

    QPushButton* settingsBtn = new QPushButton("设置", this);
    settingsBtn->setObjectName("settingsButton");
    connect(settingsBtn, &QPushButton::clicked, this, &SimpleHomeWidget::openSettings);
    quickLayout->addWidget(settingsBtn);

    mainLayout->addLayout(quickLayout);

    mainLayout->addSpacing(20);

    // ---- Status Label ----
    m_statusLabel = new QLabel("", this);
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addSpacing(10);

    // ---- Recent Devices Section ----
    QLabel* recentLabel = new QLabel("最近连接", this);
    recentLabel->setObjectName("sectionTitle");
    mainLayout->addWidget(recentLabel);

    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("deviceScrollArea");
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);

    m_deviceList = new QListWidget(this);
    m_deviceList->setObjectName("deviceCardList");
    m_deviceList->setSpacing(8);
    m_deviceList->setFlow(QListWidget::LeftToRight);
    m_deviceList->setWrapping(true);
    m_deviceList->setResizeMode(QListWidget::Adjust);
    m_deviceList->setMovement(QListWidget::Static);
    m_deviceList->setMinimumHeight(120);
    m_deviceList->setMaximumHeight(200);

    scrollArea->setWidget(m_deviceList);
    mainLayout->addWidget(scrollArea);

    mainLayout->addStretch();

    // ---- Connections ----
    connect(m_connectButton, &QPushButton::clicked, this, &SimpleHomeWidget::onConnectClicked);
    connect(m_connectInput, &QLineEdit::returnPressed, this, &SimpleHomeWidget::onConnectClicked);
    connect(m_hostButton, &QPushButton::clicked, this, &SimpleHomeWidget::onHostButtonClicked);
    connect(m_deviceList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        int index = m_deviceList->row(item);
        if (index >= 0 && index < m_recentDevices.size()) {
            onDeviceCardClicked(index);
        }
    });
}

void SimpleHomeWidget::setupStyle() {
    setStyleSheet(R"(
        #appLogo {
            font-size: 36px;
            font-weight: bold;
            color: #e94560;
            padding: 0;
        }
        #appSubtitle {
            font-size: 14px;
            color: #888888;
            padding: 0;
        }
        #connectCard {
            background-color: #1f2940;
            border-radius: 12px;
            border: 1px solid #2a2a4a;
        }
        #inputLabel {
            font-size: 13px;
            color: #b0b0b0;
            border: none;
            background: transparent;
        }
        #connectInput {
            background-color: #16213e;
            border: 2px solid #2a2a4a;
            border-radius: 8px;
            padding: 8px 16px;
            font-size: 16px;
            color: #e0e0e0;
        }
        #connectInput:focus {
            border-color: #e94560;
        }
        #connectButton {
            background-color: #e94560;
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: bold;
        }
        #connectButton:hover {
            background-color: #ff6b81;
        }
        #connectButton:pressed {
            background-color: #c0392b;
        }
        #hostButton {
            background-color: #0f3460;
            color: #e0e0e0;
            border: 1px solid #2a2a4a;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
        }
        #hostButton:hover {
            background-color: #1a4a7a;
        }
        #settingsButton {
            background-color: transparent;
            color: #888888;
            border: 1px solid #2a2a4a;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
        }
        #settingsButton:hover {
            color: #e0e0e0;
            border-color: #e0e0e0;
        }
        #statusLabel {
            font-size: 12px;
            color: #666666;
            border: none;
            background: transparent;
        }
        #sectionTitle {
            font-size: 13px;
            color: #888888;
            font-weight: bold;
            border: none;
            background: transparent;
        }
        #deviceCardList {
            background-color: transparent;
            border: none;
        }
        #deviceScrollArea {
            background-color: transparent;
        }
    )");
}

void SimpleHomeWidget::onConnectClicked() {
    QString input = m_connectInput->text().trimmed();
    if (input.isEmpty()) return;

    // Try to parse as IP:port
    if (input.contains(':')) {
        QStringList parts = input.split(':');
        QString ip = parts[0];
        uint16_t port = (parts.size() > 1) ? static_cast<uint16_t>(parts[1].toUShort()) : 9999;

        QHostAddress addr(ip);
        if (!addr.isNull()) {
            saveRecentDevice("", ip, port, "");
            emit connectToIp(ip, port);
            return;
        }
    }

    // Try to parse as pure IP
    QHostAddress addr(input);
    if (!addr.isNull()) {
        saveRecentDevice("", input, 9999, "");
        emit connectToIp(input, 9999);
        return;
    }

    // Otherwise treat as access code
    saveRecentDevice("", "", 9999, input);
    emit connectToCode(input);
}

void SimpleHomeWidget::onDeviceCardClicked(int index) {
    if (index < 0 || index >= m_recentDevices.size()) return;

    const QuickConnectDevice& device = m_recentDevices[index];
    if (!device.accessCode.isEmpty()) {
        emit connectToCode(device.accessCode);
    } else {
        emit connectToIp(device.ip, device.port);
    }
}

void SimpleHomeWidget::onHostButtonClicked() {
    emit startHostService();
}

void SimpleHomeWidget::refreshDevices() {
    if (!m_manager) return;

    QList<DeviceInfo> devices = m_manager->getDevices();

    // Update online status for recent devices
    for (auto& recent : m_recentDevices) {
        recent.online = false;
        for (const DeviceInfo& info : devices) {
            if (info.ipAddress == recent.ip || info.accessCode == recent.accessCode) {
                recent.online = true;
                if (!info.deviceName.isEmpty()) {
                    recent.name = info.deviceName;
                }
                break;
            }
        }
    }

    // Add new discovered devices to recent list
    for (const DeviceInfo& info : devices) {
        bool found = false;
        for (const auto& recent : m_recentDevices) {
            if (recent.ip == info.ipAddress || recent.accessCode == info.accessCode) {
                found = true;
                break;
            }
        }

        if (!found) {
            QuickConnectDevice device;
            device.name = info.deviceName;
            device.ip = info.ipAddress;
            device.port = info.port;
            device.accessCode = info.accessCode;
            device.online = true;
            device.lastConnected = QDateTime::currentDateTime();
            m_recentDevices.prepend(device);
        }
    }

    // Keep only top 8 devices
    while (m_recentDevices.size() > 8) {
        m_recentDevices.removeLast();
    }

    updateDeviceCards();
}

void SimpleHomeWidget::updateDeviceCards() {
    m_deviceList->clear();

    for (int i = 0; i < m_recentDevices.size(); ++i) {
        const QuickConnectDevice& device = m_recentDevices[i];

        QListWidgetItem* item = new QListWidgetItem(m_deviceList);
        item->setSizeHint(QSize(140, 100));
        item->setData(Qt::UserRole, i);

        QWidget* card = createDeviceCard(device, i);
        item->setSizeHint(card->sizeHint());

        m_deviceList->addItem(item);
        m_deviceList->setItemWidget(item, card);
    }
}

QWidget* SimpleHomeWidget::createDeviceCard(const QuickConnectDevice& device, int index) {
    QWidget* card = new QWidget();
    card->setObjectName("deviceCard");
    card->setFixedSize(140, 100);

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(4);

    // Online status dot
    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    QLabel* statusDot = new QLabel(card);
    statusDot->setFixedSize(8, 8);
    statusDot->setStyleSheet(device.online ?
        "background-color: #2ecc71; border-radius: 4px;" :
        "background-color: #666666; border-radius: 4px;");
    statusLayout->addWidget(statusDot);
    statusLayout->addStretch();

    layout->addLayout(statusLayout);

    // Device name
    QString displayName = device.name.isEmpty() ? device.ip : device.name;
    if (displayName.length() > 12) {
        displayName = displayName.left(11) + "…";
    }
    QLabel* nameLabel = new QLabel(displayName, card);
    nameLabel->setObjectName("deviceName");
    nameLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(nameLabel);

    // IP or Code
    QString subText = device.accessCode.isEmpty() ? device.ip : "ID: " + device.accessCode;
    if (subText.length() > 16) {
        subText = subText.left(15) + "…";
    }
    QLabel* subLabel = new QLabel(subText, card);
    subLabel->setObjectName("deviceSubInfo");
    subLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(subLabel);

    layout->addStretch();

    // Style
    card->setStyleSheet(R"(
        #deviceCard {
            background-color: #1f2940;
            border-radius: 8px;
            border: 1px solid #2a2a4a;
        }
        #deviceCard:hover {
            border-color: #e94560;
            background-color: #253352;
        }
        #deviceName {
            font-size: 12px;
            font-weight: bold;
            color: #e0e0e0;
            border: none;
            background: transparent;
        }
        #deviceSubInfo {
            font-size: 10px;
            color: #888888;
            border: none;
            background: transparent;
        }
    )");

    return card;
}

void SimpleHomeWidget::loadRecentDevices() {
    QSettings settings("XRK", "RecentDevices");
    QJsonArray arr = QJsonDocument::fromJson(settings.value("devices").toByteArray()).array();

    m_recentDevices.clear();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        QuickConnectDevice device;
        device.name = obj["name"].toString();
        device.ip = obj["ip"].toString();
        device.port = static_cast<uint16_t>(obj["port"].toInt(9999));
        device.accessCode = obj["code"].toString();
        device.online = false;
        device.lastConnected = QDateTime::fromString(obj["lastConnected"].toString(), Qt::ISODate);
        m_recentDevices.append(device);
    }
}

void SimpleHomeWidget::saveRecentDevice(const QString& name, const QString& ip, uint16_t port, const QString& code) {
    // Remove existing entry if present
    for (int i = m_recentDevices.size() - 1; i >= 0; --i) {
        if (m_recentDevices[i].ip == ip && m_recentDevices[i].accessCode == code) {
            m_recentDevices.removeAt(i);
        }
    }

    // Add to front
    QuickConnectDevice device;
    device.name = name;
    device.ip = ip;
    device.port = port;
    device.accessCode = code;
    device.online = true;
    device.lastConnected = QDateTime::currentDateTime();
    m_recentDevices.prepend(device);

    // Keep only top 8
    while (m_recentDevices.size() > 8) {
        m_recentDevices.removeLast();
    }

    // Save to settings
    QJsonArray arr;
    for (const auto& d : m_recentDevices) {
        QJsonObject obj;
        obj["name"] = d.name;
        obj["ip"] = d.ip;
        obj["port"] = d.port;
        obj["code"] = d.accessCode;
        obj["lastConnected"] = d.lastConnected.toString(Qt::ISODate);
        arr.append(obj);
    }

    QSettings settings("XRK", "RecentDevices");
    settings.setValue("devices", QJsonDocument(arr).toJson());

    updateDeviceCards();
}

} // namespace xrk