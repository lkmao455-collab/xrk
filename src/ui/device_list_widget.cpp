#include "device_list_widget.h"
#include "address_book_dialog.h"
#include "app/device_manager.h"
#include "core/wake_on_lan.h"
#include "core/arp_resolver.h"
#include "core/subnet_scanner.h"
#include "core/logger.h"
#include <QFrame>
#include <QHeaderView>
#include <QMessageBox>
#include <QMenu>
#include <QInputDialog>
#include <QMap>
#include <QColor>
#include <QSignalBlocker>
#include <QTimer>
#include <QMetaObject>
#include <thread>

namespace xrk {

namespace {
// Build the two/three-line label for a discovered device, appending the
// ARP-resolved MAC address when available, or a resolution hint otherwise.
QString deviceItemText(const DeviceInfo& info) {
    QString text = info.deviceName + "\n" + info.ipAddress;
    if (!info.accessCode.isEmpty()) {
        text += " [ID: " + info.accessCode + "]";
    }
    if (!info.macAddress.isEmpty()) {
        text += "\nMAC: " + info.macAddress;
    } else if (info.arpStatus == DeviceInfo::ArpStatus::Pending) {
        text += "\nMAC: 查询中...";
    } else if (info.arpStatus == DeviceInfo::ArpStatus::Timeout) {
        text += "\nMAC: 超时";
    }
    return text;
}

QString deviceItemToolTip(const DeviceInfo& info) {
    QString macText;
    if (!info.macAddress.isEmpty()) {
        macText = info.macAddress;
    } else if (info.arpStatus == DeviceInfo::ArpStatus::Timeout) {
        macText = "超时";
    } else if (info.arpStatus == DeviceInfo::ArpStatus::Pending) {
        macText = "查询中";
    } else {
        macText = "-";
    }
    return QString("名称: %1\nIP: %2\nMAC: %3")
        .arg(info.deviceName, info.ipAddress, macText);
}
} // namespace

DeviceListWidget::DeviceListWidget(DeviceManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager) {
    setupUI();
    loadHistory();
    m_addressBook.load();
    refreshAddressBook();
    
    if (m_manager) {
        connect(m_manager, &DeviceManager::deviceAdded, this, &DeviceListWidget::onDeviceAdded);
        connect(m_manager, &DeviceManager::deviceRemoved, this, &DeviceListWidget::onDeviceRemoved);
        connect(m_manager, &DeviceManager::deviceUpdated, this, &DeviceListWidget::onDeviceUpdated);
    }

    connect(m_abSearchEdit, &QLineEdit::textChanged, this, &DeviceListWidget::onAbSearchChanged);

    // Create subnet scanner
    m_scanner = new SubnetScanner(nullptr, this);
    connect(m_scanner, &SubnetScanner::scanProgress, this, &DeviceListWidget::onScanProgress);
    connect(m_scanner, &SubnetScanner::deviceFound, this, &DeviceListWidget::onScanDeviceFound);
    connect(m_scanner, &SubnetScanner::scanFinished, this, &DeviceListWidget::onScanFinished);
}

DeviceListWidget::~DeviceListWidget() {
}

AddressBook& DeviceListWidget::addressBook() {
    return m_addressBook;
}

void DeviceListWidget::refreshDevices() {
    if (!m_manager) return;
    
    m_deviceList->clear();
    
    QList<DeviceInfo> devices = m_manager->getDevices();
    for (const DeviceInfo& info : devices) {
        QListWidgetItem* item = new QListWidgetItem(m_deviceList);
        item->setText(deviceItemText(info));
        item->setToolTip(deviceItemToolTip(info));
        item->setData(Qt::UserRole, info.deviceId);
        item->setIcon(QIcon::fromTheme("computer"));
        m_deviceList->addItem(item);
    }
}

QString DeviceListWidget::selectedDeviceId() const {
    QListWidgetItem* item = m_deviceList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toString();
    }
    return QString();
}

void DeviceListWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        QString deviceId = item->data(Qt::UserRole).toString();
        emit deviceDoubleClicked(deviceId);
    }
}

void DeviceListWidget::onRefreshClicked() {
    refreshDevices();
    emit refreshClicked();
}

void DeviceListWidget::onConnectClicked() {
    QString deviceId = selectedDeviceId();
    if (!deviceId.isEmpty()) {
        emit connectClicked(deviceId);
    }
}

void DeviceListWidget::onConnectToIpClicked() {
    QString ip = m_ipEdit->text().trimmed();
    uint16_t port = static_cast<uint16_t>(m_portSpinBox->value());
    
    if (ip.isEmpty()) {
        return;
    }
    
    saveHistory(ip, port);
    emit connectToIp(ip, port);
}

void DeviceListWidget::onWakeClicked() {
    QString mac = m_macEdit->text().trimmed();
    if (!WakeOnLan::isValidMacAddress(mac)) {
        QMessageBox::warning(this, "无效MAC", "请输入有效的MAC地址\n格式: AA:BB:CC:DD:EE:FF");
        return;
    }
    
    if (WakeOnLan::sendMagicPacket(mac)) {
        QMessageBox::information(this, "WOL", "唤醒包已发送到 " + mac);
    } else {
        QMessageBox::warning(this, "发送失败", "无法发送唤醒包，请检查网络连接");
    }
}

void DeviceListWidget::onCodeConnectClicked() {
    QString code = m_codeEdit->text().trimmed();
    if (code.isEmpty()) return;
    emit connectToCode(code);
}

void DeviceListWidget::onArpLookupClicked() {
    QString ip = m_arpIpEdit->text().trimmed();
    if (ip.isEmpty()) {
        m_arpResultLabel->setText("请输入要查询的 IP 地址");
        return;
    }

    // Each query gets a unique id so that results from a superseded (newer)
    // query are discarded, and rapid clicks don't stomp each other.
    const quint64 myId = ++m_arpRequestId;
    m_arpResultLabel->setText("查询中... (" + ip + ")");

    // Resolve on a worker thread so an unreachable host (which makes
    // SendARP block for seconds) never freezes the UI.
    std::thread([this, ip, myId]() {
        const QString mac = ArpResolver::resolveMac(ip);
        // Marshal the result back onto the GUI thread.
        QMetaObject::invokeMethod(this, [this, ip, mac, myId]() {
            if (myId != m_arpRequestId) {
                return; // a newer query superseded this one
            }
            if (mac.isEmpty()) {
                // Keep a "timeout" message if it was already shown.
                if (m_arpResultLabel->text().startsWith("查询超时")) {
                    return;
                }
                m_arpResultLabel->setText("未找到 MAC（" + ip + " 不可达或不是局域网 IPv4 地址）");
            } else {
                m_arpResultLabel->setText("MAC: " + mac);
                m_arpResultLabel->setToolTip(mac);
            }
        });
    }).detach();

    // Report a timeout if the resolution hasn't completed in time. A late
    // non-empty result can still update the label once it arrives.
    QTimer::singleShot(ARP_TIMEOUT_MS, this, [this, ip, myId]() {
        if (myId != m_arpRequestId) {
            return;
        }
        if (m_arpResultLabel->text().startsWith("查询中")) {
            m_arpResultLabel->setText("查询超时（" + ip + " 在 " +
                QString::number(ARP_TIMEOUT_MS) + "ms 内无响应）");
        }
    });
}

void DeviceListWidget::onDeviceAdded(const DeviceInfo& info) {
    QListWidgetItem* item = new QListWidgetItem(m_deviceList);
    item->setText(deviceItemText(info));
    item->setToolTip(deviceItemToolTip(info));
    item->setData(Qt::UserRole, info.deviceId);
    item->setIcon(QIcon::fromTheme("computer"));
    m_deviceList->addItem(item);
}

void DeviceListWidget::onDeviceRemoved(const QString& deviceId) {
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem* item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == deviceId) {
            delete m_deviceList->takeItem(i);
            break;
        }
    }
}

void DeviceListWidget::onDeviceUpdated(const DeviceInfo& info) {
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem* item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == info.deviceId) {
            item->setText(deviceItemText(info));
            item->setToolTip(deviceItemToolTip(info));
            break;
        }
    }
}

void DeviceListWidget::onHistoryItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        QString historyEntry = item->data(Qt::UserRole).toString();
        QStringList parts = historyEntry.split(":");
        if (parts.size() == 2) {
            QString ip = parts[0];
            uint16_t port = static_cast<uint16_t>(parts[1].toUInt());
            emit connectToIp(ip, port);
        }
    }
}

// --- Address book slots ---

void DeviceListWidget::onAbAddClicked() {
    AddressBookDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        AddressBookEntry entry = dlg.entry();
        m_addressBook.addEntry(entry);
        refreshAddressBook();
    }
}

void DeviceListWidget::onAbEditClicked() {
    QListWidgetItem* item = m_abList->currentItem();
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    AddressBookEntry entry = m_addressBook.entry(id);
    if (entry.id.isEmpty()) return;

    AddressBookDialog dlg(entry, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_addressBook.updateEntry(dlg.entry());
        refreshAddressBook();
    }
}

void DeviceListWidget::onAbDeleteClicked() {
    QListWidgetItem* item = m_abList->currentItem();
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    AddressBookEntry entry = m_addressBook.entry(id);
    if (entry.id.isEmpty()) return;

    auto ret = QMessageBox::question(this, "删除设备",
        QString("确定要从地址簿删除 \"%1\"?").arg(entry.name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        m_addressBook.removeEntry(id);
        refreshAddressBook();
    }
}

void DeviceListWidget::onAbConnectClicked() {
    QListWidgetItem* item = m_abList->currentItem();
    if (!item) return;

    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    AddressBookEntry entry = m_addressBook.entry(id);
    if (entry.id.isEmpty()) return;

    if (!entry.accessCode.isEmpty()) {
        emit connectToCode(entry.accessCode);
    } else {
        emit connectToIp(entry.ip, entry.port);
    }
}

void DeviceListWidget::onAbItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;

    AddressBookEntry entry = m_addressBook.entry(id);
    if (entry.id.isEmpty()) return;

    if (!entry.accessCode.isEmpty()) {
        emit connectToCode(entry.accessCode);
    } else {
        emit connectToIp(entry.ip, entry.port);
    }
}

void DeviceListWidget::onAbFilterChanged(int index) {
    if (index <= 0) {
        m_abFilterGroup.clear();
    } else {
        m_abFilterGroup = m_abGroupCombo->currentText();
    }
    refreshAddressBook();
}

void DeviceListWidget::onAbSearchChanged(const QString& text) {
    m_abSearchText = text;
    refreshAddressBook();
}

void DeviceListWidget::refreshAddressBook() {
    // Block the combo's signals while we repopulate it. Repopulating
    // (clear/addItem/setCurrentIndex) emits currentIndexChanged, which is
    // connected to onAbFilterChanged -> refreshAddressBook(), causing an
    // infinite recursion (stack overflow) on startup and when filtering.
    QSignalBlocker blocker(m_abGroupCombo);

    m_abList->clear();

    QList<AddressBookEntry> entries;
    if (!m_abSearchText.isEmpty()) {
        entries = m_addressBook.search(m_abSearchText);
    } else if (!m_abFilterGroup.isEmpty()) {
        entries = m_addressBook.entriesByGroup(m_abFilterGroup);
    } else {
        entries = m_addressBook.allEntries();
    }

    QList<AddressBookEntry> favs;
    QMap<QString, QList<AddressBookEntry>> grouped;
    QStringList groupOrder;

    for (const auto& e : entries) {
        if (e.favorite) {
            favs.append(e);
        } else if (!e.group.isEmpty()) {
            grouped[e.group].append(e);
            if (!groupOrder.contains(e.group))
                groupOrder.append(e.group);
        }
    }

    auto addEntryItem = [&](const AddressBookEntry& e, const QString& indent) {
        QListWidgetItem* item = new QListWidgetItem(m_abList);
        QString text = indent + e.name + "\n" + indent + e.ip + ":" + QString::number(e.port);
        if (!e.mac.isEmpty()) text += " [WOL]";
        item->setText(text);
        item->setData(Qt::UserRole, e.id);
        item->setToolTip(QString("名称: %1\nIP: %2:%3\nMAC: %4\n分组: %5\n备注: %6")
            .arg(e.name, e.ip).arg(e.port).arg(e.mac.isEmpty() ? "-" : e.mac)
            .arg(e.group.isEmpty() ? "-" : e.group).arg(e.notes.isEmpty() ? "-" : e.notes));
        m_abList->addItem(item);
    };

    for (const auto& e : favs) {
        addEntryItem(e, "★ ");
    }

    if (!favs.isEmpty() && !grouped.isEmpty()) {
        auto* sep = new QListWidgetItem(m_abList);
        sep->setFlags(Qt::NoItemFlags);
        sep->setSizeHint(QSize(0, 4));
        m_abList->addItem(sep);
    }

    for (const auto& grp : groupOrder) {
        auto* header = new QListWidgetItem(m_abList);
        header->setText("── " + grp + " ──");
        header->setFlags(Qt::NoItemFlags);
        header->setForeground(QColor(100, 100, 100));
        m_abList->addItem(header);

        for (const auto& e : grouped[grp]) {
            addEntryItem(e, "  ");
        }
    }

    QStringList groups = m_addressBook.groups();
    QString currentGroup = m_abGroupCombo->currentText();
    m_abGroupCombo->clear();
    m_abGroupCombo->addItem("全部");
    for (const auto& g : groups) {
        m_abGroupCombo->addItem(g);
    }
    int idx = m_abGroupCombo->findText(currentGroup);
    if (idx >= 0) m_abGroupCombo->setCurrentIndex(idx);
}

void DeviceListWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setSpacing(4);

    // ---- Address book section ----
    QLabel* abLabel = new QLabel("\u5730\u5740\u7c3f:", this);
    abLabel->setObjectName("section-header");
    layout->addWidget(abLabel);

    QHBoxLayout* abSearchLayout = new QHBoxLayout();
    m_abSearchEdit = new QLineEdit(this);
    m_abSearchEdit->setPlaceholderText("搜索地址簿...");
    abSearchLayout->addWidget(m_abSearchEdit);

    m_abGroupCombo = new QComboBox(this);
    m_abGroupCombo->addItem("全部");
    abSearchLayout->addWidget(m_abGroupCombo);
    layout->addLayout(abSearchLayout);
    connect(m_abGroupCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &DeviceListWidget::onAbFilterChanged);

    m_abList = new QListWidget(this);
    m_abList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_abList, &QListWidget::itemDoubleClicked, this, &DeviceListWidget::onAbItemDoubleClicked);
    layout->addWidget(m_abList);

    auto setupAbButton = [&](const QString& text, std::function<void()> slot) -> QPushButton* {
        auto* btn = new QPushButton(text, this);
        connect(btn, &QPushButton::clicked, this, slot);
        return btn;
    };

    QHBoxLayout* abBtnLayout = new QHBoxLayout();
    m_abAddButton = new QPushButton("添加", this);
    connect(m_abAddButton, &QPushButton::clicked, this, &DeviceListWidget::onAbAddClicked);
    abBtnLayout->addWidget(m_abAddButton);

    m_abEditButton = new QPushButton("编辑", this);
    connect(m_abEditButton, &QPushButton::clicked, this, &DeviceListWidget::onAbEditClicked);
    abBtnLayout->addWidget(m_abEditButton);

    m_abDeleteButton = new QPushButton("删除", this);
    connect(m_abDeleteButton, &QPushButton::clicked, this, &DeviceListWidget::onAbDeleteClicked);
    abBtnLayout->addWidget(m_abDeleteButton);

    m_abConnectButton = new QPushButton("连接", this);
    connect(m_abConnectButton, &QPushButton::clicked, this, &DeviceListWidget::onAbConnectClicked);
    abBtnLayout->addWidget(m_abConnectButton);

    layout->addLayout(abBtnLayout);

    QFrame* abLine = new QFrame(this);
    abLine->setFrameShape(QFrame::HLine);
    abLine->setFrameShadow(QFrame::Sunken);
    layout->addWidget(abLine);

    // ---- Manual connect ----
    QLabel* manualLabel = new QLabel("\u624b\u52a8\u8fde\u63a5:", this);
    layout->addWidget(manualLabel);
    
    QHBoxLayout* ipLayout = new QHBoxLayout();
    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setPlaceholderText("IP地址 (如: 192.168.1.100)");
    ipLayout->addWidget(m_ipEdit);
    
    m_portSpinBox = new QSpinBox(this);
    m_portSpinBox->setRange(1, 65535);
    m_portSpinBox->setValue(9999);
    m_portSpinBox->setPrefix("端口: ");
    ipLayout->addWidget(m_portSpinBox);
    
    layout->addLayout(ipLayout);
    
    m_connectIpButton = new QPushButton("连接到IP", this);
    connect(m_connectIpButton, &QPushButton::clicked, this, &DeviceListWidget::onConnectToIpClicked);
    layout->addWidget(m_connectIpButton);
    
    QHBoxLayout* codeLayout = new QHBoxLayout();
    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText("识别码 (如: 123456789)");
    codeLayout->addWidget(m_codeEdit);
    
    m_codeConnectButton = new QPushButton("识别码连接", this);
    connect(m_codeConnectButton, &QPushButton::clicked, this, &DeviceListWidget::onCodeConnectClicked);
    codeLayout->addWidget(m_codeConnectButton);
    
    layout->addLayout(codeLayout);
    
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);
    
    QLabel* historyLabel = new QLabel("\u8fde\u63a5\u5386\u53f2:", this);
    layout->addWidget(historyLabel);
    
    m_historyList = new QListWidget(this);
    m_historyList->setMaximumHeight(100);
    connect(m_historyList, &QListWidget::itemDoubleClicked, this, &DeviceListWidget::onHistoryItemDoubleClicked);
    layout->addWidget(m_historyList);
    
    QHBoxLayout* historyButtonLayout = new QHBoxLayout();
    m_clearHistoryButton = new QPushButton("清空历史", this);
    connect(m_clearHistoryButton, &QPushButton::clicked, this, &DeviceListWidget::clearHistory);
    historyButtonLayout->addWidget(m_clearHistoryButton);
    historyButtonLayout->addStretch();
    layout->addLayout(historyButtonLayout);
    
    QFrame* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line2);
    
    QLabel* deviceLabel = new QLabel("\u53d1\u73b0\u7684\u8bbe\u5907:", this);
    layout->addWidget(deviceLabel);
    
    m_deviceList = new QListWidget(this);
    connect(m_deviceList, &QListWidget::itemDoubleClicked, this, &DeviceListWidget::onItemDoubleClicked);
    layout->addWidget(m_deviceList);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_refreshButton = new QPushButton("刷新设备", this);
    connect(m_refreshButton, &QPushButton::clicked, this, &DeviceListWidget::onRefreshClicked);
    buttonLayout->addWidget(m_refreshButton);
    
    m_connectButton = new QPushButton("连接选中", this);
    connect(m_connectButton, &QPushButton::clicked, this, &DeviceListWidget::onConnectClicked);
    buttonLayout->addWidget(m_connectButton);
    
    layout->addLayout(buttonLayout);

    // Subnet scan section
    QHBoxLayout* scanLayout = new QHBoxLayout();
    m_scanButton = new QPushButton("扫描局域网", this);
    m_scanButton->setToolTip("主动扫描局域网内所有XRK设备");
    connect(m_scanButton, &QPushButton::clicked, this, &DeviceListWidget::onScanClicked);
    scanLayout->addWidget(m_scanButton);

    m_scanProgressBar = new QProgressBar(this);
    m_scanProgressBar->setRange(0, 254);
    m_scanProgressBar->setValue(0);
    m_scanProgressBar->setVisible(false);
    scanLayout->addWidget(m_scanProgressBar);

    layout->addLayout(scanLayout);

    m_scanStatusLabel = new QLabel(this);
    m_scanStatusLabel->setText(QString::fromUtf8("点击\"扫描局域网\"查找可连接设备"));
    layout->addWidget(m_scanStatusLabel);
    
    QFrame* line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line3);
    
    QLabel* wolLabel = new QLabel("\u8fdc\u7a0b\u5524\u9192 (WOL):", this);
    layout->addWidget(wolLabel);
    
    QHBoxLayout* macLayout = new QHBoxLayout();
    m_macEdit = new QLineEdit(this);
    m_macEdit->setPlaceholderText("MAC地址 (如: AA:BB:CC:DD:EE:FF)");
    macLayout->addWidget(m_macEdit);
    
    m_wakeButton = new QPushButton("唤醒", this);
    connect(m_wakeButton, &QPushButton::clicked, this, &DeviceListWidget::onWakeClicked);
    macLayout->addWidget(m_wakeButton);
    
    layout->addLayout(macLayout);

    QFrame* line4 = new QFrame(this);
    line4->setFrameShape(QFrame::HLine);
    line4->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line4);

    // ---- Manual ARP lookup (IP -> MAC) ----
    QLabel* arpLabel = new QLabel("\u6839\u636e IP \u67e5\u8be2 MAC (ARP):", this);
    layout->addWidget(arpLabel);

    QHBoxLayout* arpLayout = new QHBoxLayout();
    m_arpIpEdit = new QLineEdit(this);
    m_arpIpEdit->setPlaceholderText("IP地址 (如: 192.168.1.1)");
    arpLayout->addWidget(m_arpIpEdit);

    m_arpLookupButton = new QPushButton("查询MAC", this);
    connect(m_arpLookupButton, &QPushButton::clicked, this, &DeviceListWidget::onArpLookupClicked);
    arpLayout->addWidget(m_arpLookupButton);

    layout->addLayout(arpLayout);

    m_arpResultLabel = new QLabel(this);
    m_arpResultLabel->setWordWrap(true);
    m_arpResultLabel->setText("输入局域网 IP 后点击查询");
    layout->addWidget(m_arpResultLabel);
}

void DeviceListWidget::updateDeviceInfo(QListWidgetItem* item, const DeviceInfo& info) {
    if (item) {
        item->setText(deviceItemText(info));
        item->setToolTip(deviceItemToolTip(info));
    }
}

void DeviceListWidget::loadHistory() {
    QSettings settings("XRK", "LANRemote");
    m_history = settings.value("connectionHistory").toStringList();
    
    m_historyList->clear();
    for (const QString& entry : m_history) {
        QStringList parts = entry.split(":");
        if (parts.size() == 2) {
            QListWidgetItem* item = new QListWidgetItem(m_historyList);
            item->setText(parts[0] + ":" + parts[1]);
            item->setData(Qt::UserRole, entry);
        }
    }
}

void DeviceListWidget::saveHistory(const QString& ip, uint16_t port) {
    QString entry = ip + ":" + QString::number(port);
    m_history.removeAll(entry);
    m_history.prepend(entry);
    
    while (m_history.size() > MAX_HISTORY) {
        m_history.removeLast();
    }
    
    QSettings settings("XRK", "LANRemote");
    settings.setValue("connectionHistory", m_history);
    
    loadHistory();
}

void DeviceListWidget::addHistoryItem(const QString& ip, uint16_t port) {
    saveHistory(ip, port);
}

void DeviceListWidget::clearHistory() {
    m_history.clear();
    QSettings settings("XRK", "LANRemote");
    settings.remove("connectionHistory");
    m_historyList->clear();
}

// Subnet scan slots
void DeviceListWidget::onScanClicked() {
    if (!m_scanner) return;

    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
        m_scanButton->setText("扫描局域网");
        m_scanProgressBar->setVisible(false);
        m_scanStatusLabel->setText("扫描已停止");
        return;
    }

    m_scanButton->setText("停止扫描");
    m_scanProgressBar->setValue(0);
    m_scanProgressBar->setVisible(true);
    m_scanStatusLabel->setText("正在扫描局域网...");
    m_scanner->startScan();
}

void DeviceListWidget::onScanProgress(int current, int total) {
    m_scanProgressBar->setRange(0, total);
    m_scanProgressBar->setValue(current);
    m_scanStatusLabel->setText(QString("正在扫描... %1/%2").arg(current).arg(total));
}

void DeviceListWidget::onScanDeviceFound(const DeviceInfo& info) {
    // Add found device to the list if not already there
    bool exists = false;
    for (int i = 0; i < m_deviceList->count(); ++i) {
        QListWidgetItem* item = m_deviceList->item(i);
        if (item->data(Qt::UserRole).toString() == info.deviceId) {
            exists = true;
            break;
        }
    }

    if (!exists) {
        QListWidgetItem* item = new QListWidgetItem(m_deviceList);
        item->setText(deviceItemText(info));
        item->setToolTip(deviceItemToolTip(info));
        item->setData(Qt::UserRole, info.deviceId);
        item->setIcon(QIcon::fromTheme("network-transmit-receive"));
        m_deviceList->addItem(item);
    }

    m_scanStatusLabel->setText(QString("已发现: %1 (%2)").arg(info.deviceName, info.ipAddress));
}

void DeviceListWidget::onScanFinished(int foundCount) {
    m_scanButton->setText("扫描局域网");
    m_scanProgressBar->setVisible(false);
    m_scanStatusLabel->setText(QString("扫描完成，发现 %1 台设备").arg(foundCount));
}

} // namespace xrk
