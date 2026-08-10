#include "subnet_scanner.h"
#include "network_manager.h"
#include "protocol_manager.h"
#include "logger.h"
#include <QtConcurrent>
#include <QNetworkInterface>
#include <QThreadPool>
#include <QRunnable>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <functional>

namespace xrk {

SubnetScanner::SubnetScanner(NetworkManager* network, QObject* parent)
    : QObject(parent), m_network(network) {
}

SubnetScanner::~SubnetScanner() {
    stopScan();
    // Join the background scan worker. The worker lambda captures `this` and keeps
    // touching this object's members until it returns, so we must not free `this`
    // (or let the test delete us) while it is still running. Blocking here guarantees
    // the worker has fully exited before destruction completes, eliminating the
    // use-after-free that previously corrupted the heap and crashed later tests.
    if (m_future.isRunning()) {
        m_future.waitForFinished();
    }
}

void SubnetScanner::startScan() {
    // Get local network address
    QString localIpStr = m_network ? m_network->localIpv4() : "127.0.0.1";
    QHostAddress localIp(localIpStr);

    // Parse to get subnet base
    quint32 ipNum = localIp.toIPv4Address();
    // Assume /24 subnet: zero out the last octet
    ipNum = (ipNum & 0xFFFFFF00);
    QHostAddress baseIp(ipNum);

    startScan(baseIp, 254);
}

void SubnetScanner::startScan(const QHostAddress& startIp, int count) {
    if (m_scanning.load()) {
        LOG_WARNING("SubnetScanner: scan already in progress");
        return;
    }

    m_scanning.store(true);
    m_foundCount = 0;
    m_totalHosts = count;
    LOG_INFO("SubnetScanner: scanning " + QString::number(count) + " hosts from " + startIp.toString());

    // Run scan in a separate thread. We track the returned QFuture so the destructor
    // can join the worker and guarantee it never outlives this object.
    m_future = QtConcurrent::run([this, startIp, count]() {
        scanWorker(startIp, 0, count);
    });
}

void SubnetScanner::stopScan() {
    m_scanning.store(false);
}

void SubnetScanner::scanWorker(const QHostAddress& baseIp, int startOffset, int count) {
    QElapsedTimer timer;
    timer.start();

    quint32 baseNum = baseIp.toIPv4Address();
    int timeout = m_connectTimeoutMs;

    for (int i = startOffset; i < count && m_scanning.load(); ++i) {
        QHostAddress targetIp(baseNum + i + 1); // +1 because 0 is network addr

        // Quick TCP connect probe to port 9999
        QTcpSocket socket;
        socket.connectToHost(targetIp, DEFAULT_PORT);

        // Wait up to configured timeout for connection
        if (socket.waitForConnected(timeout)) {
            // Connected! This is likely an XRK host.
            DeviceInfo info;
            info.ipAddress = targetIp.toString();
            info.port = DEFAULT_PORT;
            info.deviceName = "Unknown XRK Host";
            info.deviceId = targetIp.toString();

            // Send a discovery request to get device info
            if (m_network) {
                m_network->sendDiscoveryResponse(targetIp, UDP_BROADCAST_PORT);
            }

            m_foundCount++;
            // Emit signal on main thread
            QMetaObject::invokeMethod(this, [this, info]() {
                emit deviceFound(info);
            }, Qt::QueuedConnection);
            LOG_INFO("SubnetScanner: found XRK host at " + targetIp.toString());
        }

        socket.disconnectFromHost();

        // Report progress every 10 hosts
        if ((i - startOffset) % 10 == 0) {
            int current = i - startOffset + 1;
            QMetaObject::invokeMethod(this, [this, current, count]() {
                emit scanProgress(current, count);
            }, Qt::QueuedConnection);
        }
    }

    m_scanning.store(false);
    int found = m_foundCount;
    LOG_INFO("SubnetScanner: scan completed in " + QString::number(timer.elapsed()) +
             "ms, found " + QString::number(found) + " hosts");
    QMetaObject::invokeMethod(this, [this, found]() {
        emit scanFinished(found);
    }, Qt::QueuedConnection);
}

} // namespace xrk
