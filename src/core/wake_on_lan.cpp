#include "wake_on_lan.h"
#include <QUdpSocket>
#include <QHostAddress>
#include <QRegularExpression>
#include <cstdint>

namespace xrk {

bool WakeOnLan::sendMagicPacket(const QString& macAddress, const QString& ipAddress, uint16_t port) {
    QByteArray packet = createMagicPacket(macAddress);
    if (packet.isEmpty()) return false;

    QUdpSocket socket;
    qint64 sent = socket.writeDatagram(packet, QHostAddress(ipAddress), port);
    if (sent < 0) {
        // Try broadcast address
        sent = socket.writeDatagram(packet, QHostAddress::Broadcast, port);
    }
    return sent > 0;
}

bool WakeOnLan::isValidMacAddress(const QString& macAddress) {
    static QRegularExpression regex(
        "^([0-9A-Fa-f]{2}[:-]?){5}[0-9A-Fa-f]{2}$"
    );
    return regex.match(macAddress.trimmed()).hasMatch();
}

QString WakeOnLan::normalizeMacAddress(const QString& macAddress) {
    QString clean = macAddress.trimmed();
    clean.remove(QRegularExpression("[:-]"));
    return clean.toUpper();
}

QByteArray WakeOnLan::createMagicPacket(const QString& macAddress) {
    QString clean = normalizeMacAddress(macAddress);
    if (clean.length() != 12) return {};

    QByteArray packet;
    // 6 bytes of 0xFF
    packet.append(6, '\xFF');

    // Parse MAC address
    QByteArray mac;
    for (int i = 0; i < 6; ++i) {
        bool ok;
        mac.append(static_cast<char>(clean.mid(i * 2, 2).toUInt(&ok, 16)));
        if (!ok) return {};
    }

    // 16 repetitions of the MAC
    for (int i = 0; i < 16; ++i) {
        packet.append(mac);
    }

    return packet;
}

} // namespace xrk
