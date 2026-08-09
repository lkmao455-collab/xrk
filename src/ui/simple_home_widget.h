#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QProgressBar>
#include <memory>

namespace xrk {

class DeviceManager;
class NetworkManager;
class SubnetScanner;
struct DeviceInfo;

struct QuickConnectDevice {
    QString name;
    QString ip;
    uint16_t port;
    QString accessCode;
    bool online;
    QDateTime lastConnected;
};

class SimpleHomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit SimpleHomeWidget(DeviceManager* manager, QWidget* parent = nullptr);

    void setNetworkManager(NetworkManager* network);

protected:
    void paintEvent(QPaintEvent* event) override;

signals:
    void connectToIp(const QString& ip, uint16_t port);
    void connectToCode(const QString& code);
    void startHostService();
    void openSettings();
    void deviceFound(const QString& name, const QString& ip, uint16_t port);

public slots:
    void setHostButtonState(bool running);

private slots:
    void onConnectClicked();
    void onDeviceCardClicked(int index);
    void onHostButtonClicked();
    void refreshDevices();
    void onSearchClicked();
    void onSearchProgress(int current, int total);
    void onDeviceFound(const DeviceInfo& info);
    void onScanFinished(int foundCount);

private:
    void setupUI();
    void setupStyle();
    void loadRecentDevices();
    void saveRecentDevice(const QString& name, const QString& ip, uint16_t port, const QString& code);
    void updateDeviceCards();
    void updateScanUI(bool scanning);
    QWidget* createDeviceCard(const QuickConnectDevice& device, int index);

    DeviceManager* m_manager;
    NetworkManager* m_network = nullptr;
    SubnetScanner* m_scanner = nullptr;
    QLineEdit* m_connectInput;
    QPushButton* m_connectButton;
    QPushButton* m_hostButton;
    QPushButton* m_searchButton;
    QProgressBar* m_scanProgress;
    QLabel* m_scanStatusLabel;
    QListWidget* m_deviceList;
    QLabel* m_statusLabel;
    QTimer* m_refreshTimer;
    QList<QuickConnectDevice> m_recentDevices;
};

} // namespace xrk