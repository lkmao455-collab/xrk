#pragma once

#include <QString>

namespace xrk {

// Resolves the MAC address of a LAN host from its IPv4 address using the
// Windows ARP table (SendARP / Iphlpapi). This is the mechanism behind the
// "get MAC from IP" feature: whenever a device is discovered, its MAC is
// looked up automatically so the UI can show it (and it can feed Wake-on-LAN).
class ArpResolver {
public:
    // Returns the MAC address formatted as "AA:BB:CC:DD:EE:FF" on success,
    // or an empty string if the address is invalid or unreachable.
    // timeoutMs is the per-attempt ARP timeout forwarded to SendARP.
    static QString resolveMac(const QString& ipAddress, unsigned long timeoutMs = 1000);
};

} // namespace xrk
