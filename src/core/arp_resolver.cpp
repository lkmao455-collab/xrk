#include "arp_resolver.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace xrk {

QString ArpResolver::resolveMac(const QString& ipAddress, unsigned long timeoutMs) {
#ifdef Q_OS_WIN
    Q_UNUSED(timeoutMs);
    if (ipAddress.isEmpty()) {
        return {};
    }

    // Only IPv4 literals are supported (SendARP is IPv4-only). Convert the
    // address to the network-order ULONG that SendARP expects.
    const QByteArray ipBa = ipAddress.toUtf8();
#pragma warning(push)
#pragma warning(disable: 4996) // inet_addr is deprecated in favor of InetPton
    ULONG destIp = inet_addr(ipBa.constData());
#pragma warning(pop)
    if (destIp == INADDR_NONE) {
        return {};
    }

    BYTE mac[6] = {0};
    ULONG macLen = sizeof(mac);
    DWORD ret = SendARP(destIp, 0, mac, &macLen);
    if (ret == NO_ERROR && macLen == 6) {
        return QString("%1:%2:%3:%4:%5:%6")
            .arg(mac[0], 2, 16, QLatin1Char('0'))
            .arg(mac[1], 2, 16, QLatin1Char('0'))
            .arg(mac[2], 2, 16, QLatin1Char('0'))
            .arg(mac[3], 2, 16, QLatin1Char('0'))
            .arg(mac[4], 2, 16, QLatin1Char('0'))
            .arg(mac[5], 2, 16, QLatin1Char('0'))
            .toUpper();
    }
    return {};
#else
    Q_UNUSED(ipAddress);
    Q_UNUSED(timeoutMs);
    return {};
#endif
}

} // namespace xrk
