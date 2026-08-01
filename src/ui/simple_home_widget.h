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
#include <memory>

namespace xrk {

class DeviceManager;

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

signals:
    void connectToIp(const QString& ip, uint16_t port);
    void connectToCode(const QString& code);
    void startHostService();
    void openSettings();

private slots:
    void onConnectClicked();
    void onDeviceCardClicked(int index);
    void onHostButtonClicked();
    void refreshDevices();

private:
    void setupUI();
    void setupStyle();
    void loadRecentDevices();
    void saveRecentDevice(const QString& name, const QString& ip, uint16_t port, const QString& code);
    void updateDeviceCards();
    QWidget* createDeviceCard(const QuickConnectDevice& device, int index);

    DeviceManager* m_manager;
    QLineEdit* m_connectInput;
    QPushButton* m_connectButton;
    QPushButton* m_hostButton;
    QListWidget* m_deviceList;
    QLabel* m_statusLabel;
    QTimer* m_refreshTimer;
    QList<QuickConnectDevice> m_recentDevices;
};

} // namespace xrk