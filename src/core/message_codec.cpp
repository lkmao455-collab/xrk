#include "message_codec.h"
#include <QDataStream>
#include <QBuffer>
#include <QtEndian>

namespace xrk {

QByteArray MessageCodec::encode(MessageType type, const QByteArray& payload, const QString& sessionId) {
    MessageHeader header;
    header.type = type;
    header.length = static_cast<uint32_t>(payload.size());
    header.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    header.sessionId = sessionId;
    return encode(header, payload);
}

bool MessageCodec::decode(const QByteArray& data, MessageType& type, QByteArray& payload, QString& sessionId) {
    MessageHeader header;
    uint32_t sessionIdLen = 0;
    if (!parseHeader(data, header, sessionIdLen)) {
        return false;
    }
    
    if (static_cast<size_t>(data.size()) < MIN_MESSAGE_SIZE + header.length) {
        return false;
    }
    
    type = header.type;
    sessionId = header.sessionId;
    // The wire format is: [fixed header (HEADER_FIXED_SIZE)][sessionId length (4)]
    // [sessionId bytes][payload][checksum (CHECKSUM_SIZE)]. The payload starts
    // after the sessionId field, not directly after the fixed header.
    payload = data.mid(HEADER_FIXED_SIZE + 4 + sessionIdLen, header.length);
    
    return true;
}

QByteArray MessageCodec::createHeader(MessageType type, uint32_t payloadSize, const QString& sessionId) {
    MessageHeader header;
    header.type = type;
    header.length = payloadSize;
    header.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    header.sessionId = sessionId;
    
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << header.magic;
    stream << header.version;
    stream << static_cast<uint32_t>(header.type);
    stream << header.length;
    stream << header.timestamp;
    
    QByteArray sessionIdBytes = sessionId.toUtf8();
    stream << static_cast<uint32_t>(sessionIdBytes.size());
    stream.writeRawData(sessionIdBytes.constData(), sessionIdBytes.size());
    
    return data;
}

bool MessageCodec::parseHeader(const QByteArray& data, MessageHeader& header, uint32_t& sessionIdLen) {
    sessionIdLen = 0;
    if (static_cast<size_t>(data.size()) < HEADER_FIXED_SIZE) {
        return false;
    }
    
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> header.magic;
    if (header.magic != MAGIC) {
        return false;
    }
    
    stream >> header.version;
    if (header.version != PROTOCOL_VERSION) {
        return false;
    }
    
    uint32_t typeValue;
    stream >> typeValue;
    header.type = static_cast<MessageType>(typeValue);
    
    stream >> header.length;
    stream >> header.timestamp;
    
    if (static_cast<size_t>(data.size()) < HEADER_FIXED_SIZE + 4) {
        return false;
    }
    
    stream >> sessionIdLen;
    
    if (static_cast<size_t>(data.size()) < HEADER_FIXED_SIZE + 4 + sessionIdLen) {
        return false;
    }
    
    QByteArray sessionIdBytes = data.mid(HEADER_FIXED_SIZE + 4, sessionIdLen);
    header.sessionId = QString::fromUtf8(sessionIdBytes);
    
    return true;
}

QByteArray MessageCodec::calculateChecksum(const QByteArray& data) {
    uint32_t checksum = 0;
    for (int i = 0; i < data.size(); ++i) {
        checksum = (checksum << 1) + static_cast<uint8_t>(data[i]);
    }
    
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << checksum;
    return result;
}

bool MessageCodec::verifyChecksum(const QByteArray& data) {
    if (static_cast<size_t>(data.size()) < CHECKSUM_SIZE) {
        return false;
    }
    
    QByteArray payload = data.left(data.size() - CHECKSUM_SIZE);
    QByteArray expectedChecksum = data.right(CHECKSUM_SIZE);
    QByteArray calculatedChecksum = calculateChecksum(payload);
    
    return expectedChecksum == calculatedChecksum;
}

QByteArray MessageCodec::encode(const MessageHeader& header, const QByteArray& payload) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << header.magic;
    stream << header.version;
    stream << static_cast<uint32_t>(header.type);
    stream << header.length;
    stream << header.timestamp;
    
    QByteArray sessionIdBytes = header.sessionId.toUtf8();
    stream << static_cast<uint32_t>(sessionIdBytes.size());
    stream.writeRawData(sessionIdBytes.constData(), sessionIdBytes.size());

    stream.writeRawData(payload.constData(), payload.size());

    // Checksum must be computed over the full body (fixed header + sessionId
    // field + payload) so it matches what verifyChecksum() recomputes on the
    // receiving side, which hashes everything except the trailing checksum.
    QByteArray checksum = calculateChecksum(data);
    stream.writeRawData(checksum.constData(), checksum.size());

    return data;
}

} // namespace xrk
