#pragma once

#include <QString>

namespace xrk {

class WakeOnLan {
public:
    static bool sendMagicPacket(const QString& macAddress,
                                const QString& ipAddress = "255.255.255.255",
                                uint16_t port = 9);

    static bool isValidMacAddress(const QString& macAddress);
    static QString normalizeMacAddress(const QString& macAddress);

private:
    static QByteArray createMagicPacket(const QString& macAddress);
};

} // namespace xrk
