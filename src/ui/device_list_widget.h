#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QStringList>
#include <QComboBox>
#include <QProgressBar>
#include "core/types.h"
#include "app/address_book.h"

namespace xrk {

class DeviceManager;
class SubnetScanner;

class DeviceListWidget : public QWidget {
    Q_OBJECT
public:
    explicit DeviceListWidget(DeviceManager* manager, QWidget* parent = nullptr);
    ~DeviceListWidget();

    void refreshDevices();
    QString selectedDeviceId() const;
    AddressBook& addressBook();

signals:
    void deviceDoubleClicked(const QString& deviceId);
    void refreshClicked();
    void connectClicked(const QString& deviceId);
    void connectToIp(const QString& ip, uint16_t port);
    void connectToCode(const QString& code);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onRefreshClicked();
    void onConnectClicked();
    void onConnectToIpClicked();
    void onWakeClicked();
    void onCodeConnectClicked();
    void onArpLookupClicked();
    void onScanClicked();
    void onScanProgress(int current, int total);
    void onScanDeviceFound(const DeviceInfo& info);
    void onScanFinished(int foundCount);
    void onDeviceAdded(const DeviceInfo& info);
    void onDeviceRemoved(const QString& deviceId);
    void onDeviceUpdated(const DeviceInfo& info);
    void onHistoryItemDoubleClicked(QListWidgetItem* item);

    void onAbAddClicked();
    void onAbEditClicked();
    void onAbDeleteClicked();
    void onAbConnectClicked();
    void onAbItemDoubleClicked(QListWidgetItem* item);
    void onAbFilterChanged(int index);
    void onAbSearchChanged(const QString& text);

private:
    void setupUI();
    void updateDeviceInfo(QListWidgetItem* item, const DeviceInfo& info);
    void loadHistory();
    void saveHistory(const QString& ip, uint16_t port);
    void addHistoryItem(const QString& ip, uint16_t port);
    void clearHistory();
    void refreshAddressBook();

    DeviceManager* m_manager = nullptr;
    AddressBook m_addressBook;
    QString m_abFilterGroup;
    QString m_abSearchText;

    QListWidget* m_deviceList = nullptr;
    QListWidget* m_historyList = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_connectButton = nullptr;
    QPushButton* m_clearHistoryButton = nullptr;
    
    QLineEdit* m_ipEdit = nullptr;
    QSpinBox* m_portSpinBox = nullptr;
    QPushButton* m_connectIpButton = nullptr;
    
    QLineEdit* m_macEdit = nullptr;
    QPushButton* m_wakeButton = nullptr;

    QLineEdit* m_arpIpEdit = nullptr;
    QPushButton* m_arpLookupButton = nullptr;
    QLabel* m_arpResultLabel = nullptr;
    
    QLineEdit* m_codeEdit = nullptr;
    QPushButton* m_codeConnectButton = nullptr;

    // Subnet scanner
    QPushButton* m_scanButton = nullptr;
    QProgressBar* m_scanProgressBar = nullptr;
    QLabel* m_scanStatusLabel = nullptr;
    SubnetScanner* m_scanner = nullptr;

    QLineEdit* m_abSearchEdit = nullptr;
    QComboBox* m_abGroupCombo = nullptr;
    QListWidget* m_abList = nullptr;
    QPushButton* m_abAddButton = nullptr;
    QPushButton* m_abEditButton = nullptr;
    QPushButton* m_abDeleteButton = nullptr;
    QPushButton* m_abConnectButton = nullptr;
    
    QStringList m_history;
    static constexpr int MAX_HISTORY = 10;

    // Manual ARP lookup: timeout (ms) after which we report "timeout" while
    // the background resolution continues and may still fill in a result.
    static constexpr int ARP_TIMEOUT_MS = 1000;
    quint64 m_arpRequestId = 0;
};

} // namespace xrk
