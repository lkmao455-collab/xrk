#pragma once

#include <QByteArray>
#include "types.h"

namespace xrk {

class MessageCodec {
public:
    static QByteArray encode(MessageType type, const QByteArray& payload, const QString& sessionId = QString());
    static QByteArray encode(const MessageHeader& header, const QByteArray& payload);
    static bool decode(const QByteArray& data, MessageType& type, QByteArray& payload, QString& sessionId);
    static bool decode(const QByteArray& data, MessageHeader& header, QByteArray& payload);
    
    static QByteArray createHeader(MessageType type, uint32_t payloadSize, const QString& sessionId = QString());
    static bool parseHeader(const QByteArray& data, MessageHeader& header, uint32_t& sessionIdLen);
    
    static QByteArray calculateChecksum(const QByteArray& data);
    static bool verifyChecksum(const QByteArray& data);
    
    static constexpr size_t MIN_MESSAGE_SIZE = 28;
    static constexpr size_t HEADER_FIXED_SIZE = 24;
    static constexpr size_t CHECKSUM_SIZE = 4;
};

} // namespace xrk
