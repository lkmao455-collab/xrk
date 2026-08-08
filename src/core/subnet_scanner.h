#pragma once

#include <QObject>
#include <QHostAddress>
#include <QList>
#include <atomic>
#include "types.h"

namespace xrk {

class NetworkManager;

// Actively scans the local subnet for XRK hosts by probing TCP port 9999.
// Useful when UDP broadcast doesn't work (different subnets, firewalls).
class SubnetScanner : public QObject {
    Q_OBJECT
public:
    explicit SubnetScanner(NetworkManager* network, QObject* parent = nullptr);
    ~SubnetScanner();

    // Start scanning the local subnet. Emits progress and deviceFound signals.
    void startScan();

    // Scan a specific IP range.
    void startScan(const QHostAddress& startIp, int count);

    // Stop an ongoing scan.
    void stopScan();

    bool isScanning() const { return m_scanning.load(); }

signals:
    void scanProgress(int current, int total);
    void deviceFound(const DeviceInfo& info);
    void scanFinished(int foundCount);

private:
    void scanWorker(const QHostAddress& baseIp, int startOffset, int count);

    NetworkManager* m_network = nullptr;
    std::atomic<bool> m_scanning{false};
    int m_totalHosts = 0;
    int m_foundCount = 0;
};

} // namespace xrk
