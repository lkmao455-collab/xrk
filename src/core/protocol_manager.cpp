#include "protocol_manager.h"
#include "message_codec.h"
#include <QDataStream>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>

namespace xrk {

QByteArray ProtocolManager::encode(MessageType type, const QByteArray& payload, const QString& sessionId) {
    return MessageCodec::encode(type, payload, sessionId);
}

bool ProtocolManager::decode(const QByteArray& data, MessageType& type, QByteArray& payload, QString& sessionId) {
    return MessageCodec::decode(data, type, payload, sessionId);
}

QByteArray ProtocolManager::encodeDeviceInfo(const DeviceInfo& info) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray deviceIdBytes = info.deviceId.toUtf8();
    stream << static_cast<uint32_t>(deviceIdBytes.size());
    stream.writeRawData(deviceIdBytes.constData(), deviceIdBytes.size());
    
    QByteArray deviceNameBytes = info.deviceName.toUtf8();
    stream << static_cast<uint32_t>(deviceNameBytes.size());
    stream.writeRawData(deviceNameBytes.constData(), deviceNameBytes.size());
    
    QByteArray ipBytes = info.ipAddress.toUtf8();
    stream << static_cast<uint32_t>(ipBytes.size());
    stream.writeRawData(ipBytes.constData(), ipBytes.size());
    
    stream << info.port;
    
    QByteArray versionBytes = info.version.toUtf8();
    stream << static_cast<uint32_t>(versionBytes.size());
    stream.writeRawData(versionBytes.constData(), versionBytes.size());
    
    stream << info.timestamp;
    
    QByteArray codeBytes = info.accessCode.toUtf8();
    stream << static_cast<uint32_t>(codeBytes.size());
    stream.writeRawData(codeBytes.constData(), codeBytes.size());
    
    return data;
}

DeviceInfo ProtocolManager::decodeDeviceInfo(const QByteArray& data) {
    DeviceInfo info;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    QByteArray deviceIdBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    info.deviceId = QString::fromUtf8(deviceIdBytes);
    
    stream >> len;
    QByteArray deviceNameBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    info.deviceName = QString::fromUtf8(deviceNameBytes);
    
    stream >> len;
    QByteArray ipBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    info.ipAddress = QString::fromUtf8(ipBytes);
    
    stream >> info.port;
    
    stream >> len;
    QByteArray versionBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    info.version = QString::fromUtf8(versionBytes);
    
    stream >> info.timestamp;
    
    if (!stream.atEnd()) {
        stream >> len;
        if (len > 0) {
            QByteArray codeBytes = data.mid(stream.device()->pos(), len);
            stream.skipRawData(len);
            info.accessCode = QString::fromUtf8(codeBytes);
        }
    }
    
    return info;
}

QByteArray ProtocolManager::encodeMouseEvent(const MouseEvent& event) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << event.x;
    stream << event.y;
    stream << static_cast<uint32_t>(event.action);
    stream << static_cast<uint32_t>(event.button);
    stream << event.delta;
    
    return data;
}

MouseEvent ProtocolManager::decodeMouseEvent(const QByteArray& data) {
    MouseEvent event;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> event.x;
    stream >> event.y;
    
    uint32_t action;
    stream >> action;
    event.action = static_cast<MouseAction>(action);
    
    uint32_t button;
    stream >> button;
    event.button = static_cast<MouseButton>(button);
    
    stream >> event.delta;
    
    return event;
}

QByteArray ProtocolManager::encodeKeyEvent(const KeyEvent& event) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << event.keyCode;

    quint8 pressed = event.pressed ? 1 : 0;
    stream << pressed;

    stream << event.modifiers;

    stream << event.text;

    return data;
}

KeyEvent ProtocolManager::decodeKeyEvent(const QByteArray& data) {
    KeyEvent event;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> event.keyCode;
    
    quint8 pressed;
    stream >> pressed;
    event.pressed = (pressed != 0);
    
    stream >> event.modifiers;

    stream >> event.text;

    return event;
}

QByteArray ProtocolManager::encodeScreenFrame(const ScreenFrame& frame) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << frame.width;
    stream << frame.height;
    stream << static_cast<uint32_t>(frame.format);
    stream << frame.timestamp;
    
    stream << static_cast<uint32_t>(frame.data.size());
    stream.writeRawData(frame.data.constData(), frame.data.size());
    
    return data;
}

ScreenFrame ProtocolManager::decodeScreenFrame(const QByteArray& data) {
    ScreenFrame frame;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> frame.width;
    stream >> frame.height;
    
    uint32_t format;
    stream >> format;
    frame.format = static_cast<FrameFormat>(format);
    
    stream >> frame.timestamp;
    
    uint32_t dataSize;
    stream >> dataSize;
    
    frame.data = data.mid(stream.device()->pos(), dataSize);
    
    return frame;
}

QByteArray ProtocolManager::encodeScreenTile(const ScreenTile& tile) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << tile.x << tile.y << tile.w << tile.h << tile.seq;
    stream << tile.frameWidth << tile.frameHeight;
    stream << tile.isKeyFrame << tile.encoding << tile.format;
    stream << tile.timestamp;

    stream << static_cast<uint32_t>(tile.hash.size());
    if (!tile.hash.isEmpty())
        stream.writeRawData(tile.hash.constData(), tile.hash.size());

    stream << static_cast<uint32_t>(tile.data.size());
    if (!tile.data.isEmpty())
        stream.writeRawData(tile.data.constData(), tile.data.size());

    return data;
}

ScreenTile ProtocolManager::decodeScreenTile(const QByteArray& data) {
    ScreenTile tile;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> tile.x >> tile.y >> tile.w >> tile.h >> tile.seq;
    stream >> tile.frameWidth >> tile.frameHeight;

    quint8 isKey, enc, fmt;
    stream >> isKey >> enc >> fmt;
    tile.isKeyFrame = isKey;
    tile.encoding = enc;
    tile.format = fmt;

    stream >> tile.timestamp;

    uint32_t hashSize;
    stream >> hashSize;
    if (hashSize > 0) tile.hash = data.mid(static_cast<int>(stream.device()->pos()), static_cast<int>(hashSize));
    stream.device()->seek(stream.device()->pos() + hashSize);

    uint32_t dataSize;
    stream >> dataSize;
    tile.data = data.mid(static_cast<int>(stream.device()->pos()), static_cast<int>(dataSize));

    return tile;
}

QByteArray ProtocolManager::encodeScreenTileRequest(const ScreenTileRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << req.frameWidth << req.frameHeight;
    stream << static_cast<uint32_t>(req.tiles.size());
    for (const QPoint& p : req.tiles) {
        stream << p.x() << p.y();
    }
    return data;
}

ScreenTileRequest ProtocolManager::decodeScreenTileRequest(const QByteArray& data) {
    ScreenTileRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> req.frameWidth >> req.frameHeight;
    uint32_t count = 0;
    stream >> count;
    req.tiles.reserve(static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i) {
        qint32 x = 0, y = 0;
        stream >> x >> y;
        req.tiles.append(QPoint(x, y));
    }
    return req;
}

QByteArray ProtocolManager::encodeScreenAck(const ScreenAck& ack) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << ack.timestamp;
    stream << ack.roundTripMs;
    stream << ack.tilesReceived;
    stream << ack.tilesLost;
    stream << ack.bufferLevel;
    return data;
}

ScreenAck ProtocolManager::decodeScreenAck(const QByteArray& data) {
    ScreenAck ack;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> ack.timestamp;
    stream >> ack.roundTripMs;
    stream >> ack.tilesReceived;
    stream >> ack.tilesLost;
    stream >> ack.bufferLevel;
    return ack;
}

bool ProtocolManager::validate(const QByteArray& data) {
    return MessageCodec::verifyChecksum(data);
}

uint32_t ProtocolManager::calculateChecksum(const QByteArray& data) {
    QByteArray checksum = MessageCodec::calculateChecksum(data);
    QDataStream stream(checksum);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t result;
    stream >> result;
    return result;
}

QByteArray ProtocolManager::encodeFileRequest(const FileRequest& request) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray fileIdBytes = request.fileId.toUtf8();
    stream << static_cast<uint32_t>(fileIdBytes.size());
    stream.writeRawData(fileIdBytes.constData(), fileIdBytes.size());
    
    QByteArray fileNameBytes = request.fileName.toUtf8();
    stream << static_cast<uint32_t>(fileNameBytes.size());
    stream.writeRawData(fileNameBytes.constData(), fileNameBytes.size());

    QByteArray pathBytes = request.path.toUtf8();
    stream << static_cast<uint32_t>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    stream << request.fileSize;
    stream << request.offset;
    stream << static_cast<quint8>(request.isUpload ? 1 : 0);
    
    return data;
}

FileRequest ProtocolManager::decodeFileRequest(const QByteArray& data) {
    FileRequest request;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    QByteArray fileIdBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    request.fileId = QString::fromUtf8(fileIdBytes);
    
    stream >> len;
    QByteArray fileNameBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    request.fileName = QString::fromUtf8(fileNameBytes);

    stream >> len;
    QByteArray pathBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    request.path = QString::fromUtf8(pathBytes);

    stream >> request.fileSize;
    stream >> request.offset;
    
    quint8 isUpload;
    stream >> isUpload;
    request.isUpload = (isUpload != 0);
    
    return request;
}

QByteArray ProtocolManager::encodeFileData(const FileData& fileData) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray fileIdBytes = fileData.fileId.toUtf8();
    stream << static_cast<uint32_t>(fileIdBytes.size());
    stream.writeRawData(fileIdBytes.constData(), fileIdBytes.size());
    
    stream << fileData.offset;
    stream << static_cast<uint32_t>(fileData.data.size());
    stream.writeRawData(fileData.data.constData(), fileData.data.size());
    
    return data;
}

FileData ProtocolManager::decodeFileData(const QByteArray& data) {
    FileData fileData;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint32_t len;

    stream >> len;
    QByteArray fileIdBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    fileData.fileId = QString::fromUtf8(fileIdBytes);

    stream >> fileData.offset;

    uint32_t dataSize;
    stream >> dataSize;

    fileData.data = data.mid(stream.device()->pos(), dataSize);

    return fileData;
}

QByteArray ProtocolManager::encodeFileChecksum(const QByteArray& checksum, const QString& fileId) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    QByteArray fileIdBytes = fileId.toUtf8();
    stream << static_cast<uint32_t>(fileIdBytes.size());
    stream.writeRawData(fileIdBytes.constData(), fileIdBytes.size());

    stream << static_cast<uint32_t>(checksum.size());
    stream.writeRawData(checksum.constData(), checksum.size());

    return data;
}

QByteArray ProtocolManager::decodeFileChecksum(const QByteArray& data, QString& fileId) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint32_t len;

    stream >> len;
    QByteArray fileIdBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    fileId = QString::fromUtf8(fileIdBytes);

    uint32_t checksumSize;
    stream >> checksumSize;

    QByteArray checksum = data.mid(stream.device()->pos(), checksumSize);
    stream.skipRawData(checksumSize);

    return checksum;
}

QByteArray ProtocolManager::encodeVoiceMessage(const VoiceMessage& msg) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << msg.messageId;
    stream << msg.senderId;
    stream << msg.senderName;
    stream << msg.voiceData;
    stream << msg.voiceFileName;
    stream << static_cast<int32_t>(msg.duration);
    stream << msg.timestamp;
    stream << static_cast<uint8_t>(msg.isRead ? 1 : 0);

    return data;
}

VoiceMessage ProtocolManager::decodeVoiceMessage(const QByteArray& data) {
    VoiceMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> msg.messageId;
    stream >> msg.senderId;
    stream >> msg.senderName;
    stream >> msg.voiceData;
    stream >> msg.voiceFileName;
    int32_t duration = 0;
    stream >> duration;
    msg.duration = duration;
    stream >> msg.timestamp;
    uint8_t isRead = 0;
    stream >> isRead;
    msg.isRead = (isRead != 0);

    return msg;
}

QByteArray ProtocolManager::encodeVideoMessage(const VideoMessage& msg) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << msg.messageId;
    stream << msg.senderId;
    stream << msg.senderName;
    stream << msg.videoData;
    stream << msg.videoFileName;
    stream << static_cast<int32_t>(msg.duration);
    stream << msg.width;
    stream << msg.height;
    stream << msg.timestamp;
    stream << static_cast<uint8_t>(msg.isRead ? 1 : 0);

    return data;
}

VideoMessage ProtocolManager::decodeVideoMessage(const QByteArray& data) {
    VideoMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> msg.messageId;
    stream >> msg.senderId;
    stream >> msg.senderName;
    stream >> msg.videoData;
    stream >> msg.videoFileName;
    int32_t duration = 0;
    stream >> duration;
    msg.duration = duration;
    stream >> msg.width;
    stream >> msg.height;
    stream >> msg.timestamp;
    uint8_t isRead = 0;
    stream >> isRead;
    msg.isRead = (isRead != 0);

    return msg;
}

QByteArray ProtocolManager::encodeLocationMessage(const LocationMessage& msg) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << msg.messageId;
    stream << msg.senderId;
    stream << msg.senderName;
    stream << msg.latitude;
    stream << msg.longitude;
    stream << msg.locationName;
    stream << msg.timestamp;
    stream << static_cast<uint8_t>(msg.isRead ? 1 : 0);

    return data;
}

LocationMessage ProtocolManager::decodeLocationMessage(const QByteArray& data) {
    LocationMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> msg.messageId;
    stream >> msg.senderId;
    stream >> msg.senderName;
    stream >> msg.latitude;
    stream >> msg.longitude;
    stream >> msg.locationName;
    stream >> msg.timestamp;
    uint8_t isRead = 0;
    stream >> isRead;
    msg.isRead = (isRead != 0);

    return msg;
}

QByteArray ProtocolManager::encodeCardMessage(const CardMessage& msg) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << msg.messageId;
    stream << msg.senderId;
    stream << msg.senderName;
    stream << msg.vCardData;
    stream << msg.timestamp;
    stream << static_cast<uint8_t>(msg.isRead ? 1 : 0);

    return data;
}

CardMessage ProtocolManager::decodeCardMessage(const QByteArray& data) {
    CardMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> msg.messageId;
    stream >> msg.senderId;
    stream >> msg.senderName;
    stream >> msg.vCardData;
    stream >> msg.timestamp;
    uint8_t isRead = 0;
    stream >> isRead;
    msg.isRead = (isRead != 0);

    return msg;
}

QByteArray ProtocolManager::encodeMergeForwardMessage(const MergeForwardMessage& msg) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << msg.messageId;
    stream << msg.senderId;
    stream << msg.senderName;
    stream << static_cast<uint32_t>(msg.messages.size());
    for (const ForwardedMessage& fwd : msg.messages) {
        stream << fwd.messageId;
        stream << fwd.senderId;
        stream << fwd.senderName;
        stream << fwd.content;
        stream << fwd.timestamp;
        stream << static_cast<int32_t>(fwd.msgType);
    }
    stream << msg.timestamp;
    stream << static_cast<uint8_t>(msg.isRead ? 1 : 0);

    return data;
}

MergeForwardMessage ProtocolManager::decodeMergeForwardMessage(const QByteArray& data) {
    MergeForwardMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> msg.messageId;
    stream >> msg.senderId;
    stream >> msg.senderName;
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        ForwardedMessage fwd;
        stream >> fwd.messageId;
        stream >> fwd.senderId;
        stream >> fwd.senderName;
        stream >> fwd.content;
        stream >> fwd.timestamp;
        int32_t msgType = 0;
        stream >> msgType;
        fwd.msgType = msgType;
        msg.messages.append(fwd);
    }
    stream >> msg.timestamp;
    uint8_t isRead = 0;
    stream >> isRead;
    msg.isRead = (isRead != 0);

    return msg;
}

QByteArray ProtocolManager::encodeCallInvite(const CallInvite& invite) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << invite.callId;
    stream << invite.callerId;
    stream << invite.callerName;
    stream << invite.callType;
    stream << invite.sdp;
    stream << invite.timestamp;

    return data;
}

CallInvite ProtocolManager::decodeCallInvite(const QByteArray& data) {
    CallInvite invite;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> invite.callId;
    stream >> invite.callerId;
    stream >> invite.callerName;
    stream >> invite.callType;
    stream >> invite.sdp;
    stream >> invite.timestamp;

    return invite;
}

QByteArray ProtocolManager::encodeCallAccept(const CallAccept& accept) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << accept.callId;
    stream << accept.calleeId;
    stream << accept.sdp;
    stream << accept.timestamp;

    return data;
}

CallAccept ProtocolManager::decodeCallAccept(const QByteArray& data) {
    CallAccept accept;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> accept.callId;
    stream >> accept.calleeId;
    stream >> accept.sdp;
    stream >> accept.timestamp;

    return accept;
}

QByteArray ProtocolManager::encodeCallReject(const CallReject& reject) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << reject.callId;
    stream << reject.calleeId;
    stream << reject.reason;
    stream << reject.timestamp;

    return data;
}

CallReject ProtocolManager::decodeCallReject(const QByteArray& data) {
    CallReject reject;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> reject.callId;
    stream >> reject.calleeId;
    stream >> reject.reason;
    stream >> reject.timestamp;

    return reject;
}

QByteArray ProtocolManager::encodeCallEnd(const CallEnd& end) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << end.callId;
    stream << end.peerId;
    stream << end.timestamp;

    return data;
}

CallEnd ProtocolManager::decodeCallEnd(const QByteArray& data) {
    CallEnd end;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> end.callId;
    stream >> end.peerId;
    stream >> end.timestamp;

    return end;
}

QByteArray ProtocolManager::encodeIceCandidate(const IceCandidate& candidate) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << candidate.callId;
    stream << candidate.candidate;
    stream << candidate.timestamp;

    return data;
}

IceCandidate ProtocolManager::decodeIceCandidate(const QByteArray& data) {
    IceCandidate candidate;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> candidate.callId;
    stream >> candidate.candidate;
    stream >> candidate.timestamp;

    return candidate;
}

QByteArray ProtocolManager::encodeVideoCallStart(const VideoCallStart& start) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << start.callId;
    stream << start.callerId;
    stream << start.callerName;
    stream << static_cast<int32_t>(start.width);
    stream << static_cast<int32_t>(start.height);
    stream << static_cast<int32_t>(start.fps);
    stream << start.timestamp;

    return data;
}

VideoCallStart ProtocolManager::decodeVideoCallStart(const QByteArray& data) {
    VideoCallStart start;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> start.callId;
    stream >> start.callerId;
    stream >> start.callerName;
    int32_t w = 0, h = 0, f = 0;
    stream >> w >> h >> f;
    start.width = w;
    start.height = h;
    start.fps = f;
    stream >> start.timestamp;

    return start;
}

QByteArray ProtocolManager::encodeVideoCallStop(const VideoCallStop& stop) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << stop.callId;
    stream << stop.peerId;
    stream << stop.timestamp;

    return data;
}

VideoCallStop ProtocolManager::decodeVideoCallStop(const QByteArray& data) {
    VideoCallStop stop;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> stop.callId;
    stream >> stop.peerId;
    stream >> stop.timestamp;

    return stop;
}

QByteArray ProtocolManager::encodeVideoCallFrame(const VideoCallFrame& frame) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << frame.callId;
    stream << static_cast<uint32_t>(frame.frameData.size());
    stream.writeRawData(frame.frameData.constData(), frame.frameData.size());
    stream << frame.timestamp;
    stream << frame.sequenceNumber;
    stream << static_cast<uint8_t>(frame.isKeyFrame ? 1 : 0);
    stream << frame.captureTime;

    return data;
}

VideoCallFrame ProtocolManager::decodeVideoCallFrame(const QByteArray& data) {
    VideoCallFrame frame;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> frame.callId;
    uint32_t dataSize;
    stream >> dataSize;
    frame.frameData = data.mid(stream.device()->pos(), dataSize);
    stream.skipRawData(dataSize);
    stream >> frame.timestamp;
    stream >> frame.sequenceNumber;
    uint8_t isKey = 0;
    stream >> isKey;
    frame.isKeyFrame = (isKey != 0);
    stream >> frame.captureTime;

    return frame;
}

QByteArray ProtocolManager::encodeScreenShareStart(const ScreenShareStart& start) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << start.sessionId;
    stream << start.callerId;
    stream << start.callerName;
    stream << static_cast<int32_t>(start.width);
    stream << static_cast<int32_t>(start.height);
    stream << static_cast<int32_t>(start.fps);
    stream << start.timestamp;

    return data;
}

ScreenShareStart ProtocolManager::decodeScreenShareStart(const QByteArray& data) {
    ScreenShareStart start;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> start.sessionId;
    stream >> start.callerId;
    stream >> start.callerName;
    int32_t w = 0, h = 0, f = 0;
    stream >> w >> h >> f;
    start.width = w;
    start.height = h;
    start.fps = f;
    stream >> start.timestamp;

    return start;
}

QByteArray ProtocolManager::encodeScreenShareStop(const ScreenShareStop& stop) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << stop.sessionId;
    stream << stop.peerId;
    stream << stop.timestamp;

    return data;
}

ScreenShareStop ProtocolManager::decodeScreenShareStop(const QByteArray& data) {
    ScreenShareStop stop;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> stop.sessionId;
    stream >> stop.peerId;
    stream >> stop.timestamp;

    return stop;
}

QByteArray ProtocolManager::encodeScreenShareFrame(const ScreenShareFrame& frame) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << frame.sessionId;
    stream << static_cast<uint32_t>(frame.frameData.size());
    stream.writeRawData(frame.frameData.constData(), frame.frameData.size());
    stream << frame.timestamp;
    stream << frame.sequenceNumber;
    stream << static_cast<uint8_t>(frame.isKeyFrame ? 1 : 0);
    stream << frame.captureTime;

    return data;
}

ScreenShareFrame ProtocolManager::decodeScreenShareFrame(const QByteArray& data) {
    ScreenShareFrame frame;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> frame.sessionId;
    uint32_t dataSize;
    stream >> dataSize;
    frame.frameData = data.mid(stream.device()->pos(), dataSize);
    stream.skipRawData(dataSize);
    stream >> frame.timestamp;
    stream >> frame.sequenceNumber;
    uint8_t isKey = 0;
    stream >> isKey;
    frame.isKeyFrame = (isKey != 0);
    stream >> frame.captureTime;

    return frame;
}

QByteArray ProtocolManager::encodeGroupAnnouncement(const GroupAnnouncement& announcement) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << announcement.groupId;
    stream << announcement.groupName;
    stream << announcement.announcement;
    stream << announcement.announcerId;
    stream << announcement.announcerName;
    stream << announcement.timestamp;

    return data;
}

GroupAnnouncement ProtocolManager::decodeGroupAnnouncement(const QByteArray& data) {
    GroupAnnouncement announcement;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> announcement.groupId;
    stream >> announcement.groupName;
    stream >> announcement.announcement;
    stream >> announcement.announcerId;
    stream >> announcement.announcerName;
    stream >> announcement.timestamp;

    return announcement;
}

QByteArray ProtocolManager::encodeGroupMention(const GroupMention& mention) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << mention.groupId;
    stream << mention.groupName;
    stream << mention.message;
    stream << static_cast<uint32_t>(mention.mentionedMemberIds.size());
    for (const QString& id : mention.mentionedMemberIds) {
        stream << id;
    }
    stream << static_cast<uint32_t>(mention.mentionedMemberNames.size());
    for (const QString& name : mention.mentionedMemberNames) {
        stream << name;
    }
    stream << mention.senderId;
    stream << mention.senderName;
    stream << mention.timestamp;

    return data;
}

GroupMention ProtocolManager::decodeGroupMention(const QByteArray& data) {
    GroupMention mention;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> mention.groupId;
    stream >> mention.groupName;
    stream >> mention.message;
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        QString id;
        stream >> id;
        mention.mentionedMemberIds.append(id);
    }
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        QString name;
        stream >> name;
        mention.mentionedMemberNames.append(name);
    }
    stream >> mention.senderId;
    stream >> mention.senderName;
    stream >> mention.timestamp;

    return mention;
}

QByteArray ProtocolManager::encodeGroupVote(const GroupVote& vote) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << vote.groupId;
    stream << vote.groupName;
    stream << vote.voteTitle;
    stream << static_cast<uint32_t>(vote.options.size());
    for (const QString& opt : vote.options) {
        stream << opt;
    }
    stream << vote.durationSeconds;
    stream << vote.creatorId;
    stream << vote.creatorName;
    stream << vote.timestamp;

    return data;
}

GroupVote ProtocolManager::decodeGroupVote(const QByteArray& data) {
    GroupVote vote;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> vote.groupId;
    stream >> vote.groupName;
    stream >> vote.voteTitle;
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        QString opt;
        stream >> opt;
        vote.options.append(opt);
    }
    stream >> vote.durationSeconds;
    stream >> vote.creatorId;
    stream >> vote.creatorName;
    stream >> vote.timestamp;

    return vote;
}

QByteArray ProtocolManager::encodeGroupFile(const GroupFile& file) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << file.groupId;
    stream << file.groupName;
    stream << file.fileId;
    stream << file.fileName;
    stream << file.fileSize;
    stream << file.md5;
    stream << file.uploaderId;
    stream << file.uploaderName;
    stream << file.timestamp;

    return data;
}

GroupFile ProtocolManager::decodeGroupFile(const QByteArray& data) {
    GroupFile file;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> file.groupId;
    stream >> file.groupName;
    stream >> file.fileId;
    stream >> file.fileName;
    stream >> file.fileSize;
    stream >> file.md5;
    stream >> file.uploaderId;
    stream >> file.uploaderName;
    stream >> file.timestamp;

    return file;
}

QByteArray ProtocolManager::encodeGroupAlbum(const GroupAlbum& album) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << album.groupId;
    stream << album.groupName;
    stream << album.albumId;
    stream << album.albumName;
    stream << static_cast<uint32_t>(album.fileIds.size());
    for (const QString& id : album.fileIds) {
        stream << id;
    }
    stream << static_cast<uint32_t>(album.fileNames.size());
    for (const QString& name : album.fileNames) {
        stream << name;
    }
    stream << album.creatorId;
    stream << album.creatorName;
    stream << album.timestamp;

    return data;
}

GroupAlbum ProtocolManager::decodeGroupAlbum(const QByteArray& data) {
    GroupAlbum album;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> album.groupId;
    stream >> album.groupName;
    stream >> album.albumId;
    stream >> album.albumName;
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        QString id;
        stream >> id;
        album.fileIds.append(id);
    }
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        QString name;
        stream >> name;
        album.fileNames.append(name);
    }
    stream >> album.creatorId;
    stream >> album.creatorName;
    stream >> album.timestamp;

    return album;
}

QByteArray ProtocolManager::encodeGroupTodo(const GroupTodo& todo) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << todo.groupId;
    stream << todo.groupName;
    stream << todo.todoId;
    stream << todo.title;
    stream << todo.description;
    stream << static_cast<int32_t>(todo.status);
    stream << static_cast<int32_t>(todo.priority);
    stream << todo.assigneeId;
    stream << todo.assigneeName;
    stream << todo.creatorId;
    stream << todo.creatorName;
    stream << todo.dueDate;
    stream << todo.timestamp;

    return data;
}

GroupTodo ProtocolManager::decodeGroupTodo(const QByteArray& data) {
    GroupTodo todo;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> todo.groupId;
    stream >> todo.groupName;
    stream >> todo.todoId;
    stream >> todo.title;
    stream >> todo.description;
    int32_t status = 0, priority = 0;
    stream >> status >> priority;
    todo.status = status;
    todo.priority = priority;
    stream >> todo.assigneeId;
    stream >> todo.assigneeName;
    stream >> todo.creatorId;
    stream >> todo.creatorName;
    stream >> todo.dueDate;
    stream >> todo.timestamp;

    return todo;
}

QByteArray ProtocolManager::encodeSyncRequest(const SyncRequest& request) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << request.requestId;
    stream << request.accountHash;
    stream << request.syncKeyHash;
    stream << request.timestamp;

    return data;
}

SyncRequest ProtocolManager::decodeSyncRequest(const QByteArray& data) {
    SyncRequest request;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> request.requestId;
    stream >> request.accountHash;
    stream >> request.syncKeyHash;
    stream >> request.timestamp;

    return request;
}

QByteArray ProtocolManager::encodeSyncSnapshot(const SyncSnapshot& snapshot) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << snapshot.version;

    stream << static_cast<uint32_t>(snapshot.devices.size());
    for (const auto& d : snapshot.devices) {
        stream << d.deviceId;
        stream << d.name;
        stream << d.ip;
        stream << d.port;
        stream << d.lastSeen;
        stream << d.updatedAt;
        stream << static_cast<uint8_t>(d.isFriend ? 1 : 0);
    }

    stream << static_cast<uint32_t>(snapshot.groups.size());
    for (const auto& g : snapshot.groups) {
        stream << g.groupId;
        stream << g.name;
        stream << static_cast<uint32_t>(g.memberIds.size());
        for (const QString& id : g.memberIds) stream << id;
        stream << static_cast<uint32_t>(g.memberNames.size());
        for (const QString& name : g.memberNames) stream << name;
        stream << g.createdAt;
        stream << g.updatedAt;
    }

    stream << static_cast<uint32_t>(snapshot.settings.size());
    for (const auto& s : snapshot.settings) {
        stream << s.key;
        stream << s.value;
        stream << s.updatedAt;
    }

    stream << static_cast<uint32_t>(snapshot.messages.size());
    for (const auto& m : snapshot.messages) {
        stream << m.messageId;
        stream << m.senderId;
        stream << m.senderName;
        stream << m.senderIp;
        stream << m.content;
        stream << m.timestamp;
        stream << static_cast<uint8_t>(m.isFile ? 1 : 0);
        stream << m.filePath;
        stream << m.fileSize;
        stream << static_cast<uint8_t>(m.isDirectory ? 1 : 0);
        stream << static_cast<uint8_t>(m.isImage ? 1 : 0);
        stream << m.imageFileName;
        stream << m.replyTo;
        stream << m.replyContent;
        stream << m.recallId;
        stream << static_cast<uint8_t>(m.isRecalled ? 1 : 0);
        stream << m.targetId;
        stream << static_cast<uint8_t>(m.isGroup ? 1 : 0);
        stream << static_cast<uint8_t>(m.isRead ? 1 : 0);
        stream << m.readBy;
    }

    return data;
}

SyncSnapshot ProtocolManager::decodeSyncSnapshot(const QByteArray& data) {
    SyncSnapshot snapshot;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> snapshot.version;

    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        SyncSnapshot::Device d;
        stream >> d.deviceId;
        stream >> d.name;
        stream >> d.ip;
        stream >> d.port;
        stream >> d.lastSeen;
        stream >> d.updatedAt;
        uint8_t isFriend;
        stream >> isFriend;
        d.isFriend = (isFriend != 0);
        snapshot.devices.append(d);
    }

    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        SyncSnapshot::Group g;
        stream >> g.groupId;
        stream >> g.name;
        uint32_t idCount;
        stream >> idCount;
        for (uint32_t i = 0; i < idCount; ++i) {
            QString id;
            stream >> id;
            g.memberIds.append(id);
        }
        stream >> idCount;
        for (uint32_t i = 0; i < idCount; ++i) {
            QString name;
            stream >> name;
            g.memberNames.append(name);
        }
        stream >> g.createdAt;
        stream >> g.updatedAt;
        snapshot.groups.append(g);
    }

    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        SyncSnapshot::Setting s;
        stream >> s.key;
        stream >> s.value;
        stream >> s.updatedAt;
        snapshot.settings.append(s);
    }

    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        SyncSnapshot::Message m;
        stream >> m.messageId;
        stream >> m.senderId;
        stream >> m.senderName;
        stream >> m.senderIp;
        stream >> m.content;
        stream >> m.timestamp;
        uint8_t isFile;
        stream >> isFile;
        m.isFile = (isFile != 0);
        stream >> m.filePath;
        stream >> m.fileSize;
        uint8_t isDir;
        stream >> isDir;
        m.isDirectory = (isDir != 0);
        uint8_t isImg;
        stream >> isImg;
        m.isImage = (isImg != 0);
        stream >> m.imageFileName;
        stream >> m.replyTo;
        stream >> m.replyContent;
        stream >> m.recallId;
        uint8_t isRecalled;
        stream >> isRecalled;
        m.isRecalled = (isRecalled != 0);
        stream >> m.targetId;
        uint8_t isGroup;
        stream >> isGroup;
        m.isGroup = (isGroup != 0);
        uint8_t isRead;
        stream >> isRead;
        m.isRead = (isRead != 0);
        stream >> m.readBy;
        snapshot.messages.append(m);
    }

    return snapshot;
}

QByteArray ProtocolManager::encodeSyncAck(const SyncAck& ack) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << ack.requestId;
    stream << static_cast<uint8_t>(ack.success ? 1 : 0);
    stream << static_cast<int32_t>(ack.appliedCount);
    stream << ack.errorMessage;

    return data;
}

SyncAck ProtocolManager::decodeSyncAck(const QByteArray& data) {
    SyncAck ack;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> ack.requestId;
    uint8_t success;
    stream >> success;
    ack.success = (success != 0);
    stream >> ack.appliedCount;
    stream >> ack.errorMessage;

    return ack;
}

QByteArray ProtocolManager::encodeClipboardData(const ClipboardData& data) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray mimeBytes = data.mimeType.toUtf8();
    stream << static_cast<uint32_t>(mimeBytes.size());
    stream.writeRawData(mimeBytes.constData(), mimeBytes.size());
    
    stream << static_cast<uint32_t>(data.data.size());
    stream.writeRawData(data.data.constData(), data.data.size());
    
    stream << data.timestamp;
    
    return result;
}

ClipboardData ProtocolManager::decodeClipboardData(const QByteArray& data) {
    ClipboardData result;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    QByteArray mimeBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    result.mimeType = QString::fromUtf8(mimeBytes);
    
    stream >> len;
    result.data = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    
    stream >> result.timestamp;
    
    return result;
}

QByteArray ProtocolManager::encodeMonitorInfo(const MonitorInfo& info) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << info.index;
    
    QByteArray nameBytes = info.name.toUtf8();
    stream << static_cast<uint32_t>(nameBytes.size());
    stream.writeRawData(nameBytes.constData(), nameBytes.size());
    
    stream << info.x << info.y << info.width << info.height;
    stream << static_cast<quint8>(info.isPrimary ? 1 : 0);
    
    return result;
}

MonitorInfo ProtocolManager::decodeMonitorInfo(const QByteArray& data) {
    MonitorInfo info;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream >> info.index;
    
    uint32_t len;
    stream >> len;
    QByteArray nameBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    info.name = QString::fromUtf8(nameBytes);
    
    stream >> info.x >> info.y >> info.width >> info.height;
    
    quint8 isPrimary;
    stream >> isPrimary;
    info.isPrimary = (isPrimary != 0);
    
    return info;
}

QByteArray ProtocolManager::encodeMonitorList(const QList<MonitorInfo>& monitors, int currentMonitorIndex) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << static_cast<int32_t>(currentMonitorIndex);
    stream << static_cast<uint32_t>(monitors.size());
    
    for (const MonitorInfo& info : monitors) {
        QByteArray infoData = encodeMonitorInfo(info);
        stream << static_cast<uint32_t>(infoData.size());
        stream.writeRawData(infoData.constData(), infoData.size());
    }
    
    return result;
}

QList<MonitorInfo> ProtocolManager::decodeMonitorList(const QByteArray& data, int& currentMonitorIndex) {
    QList<MonitorInfo> monitors;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    int32_t currentIdx = 0;
    stream >> currentIdx;
    currentMonitorIndex = static_cast<int>(currentIdx);
    
    uint32_t count;
    stream >> count;
    
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t len;
        stream >> len;
        QByteArray infoData = data.mid(stream.device()->pos(), len);
        stream.skipRawData(len);
        monitors.append(decodeMonitorInfo(infoData));
    }
    
    return monitors;
}

QByteArray ProtocolManager::encodeChatMessage(const ChatMessage& msg) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray senderBytes = msg.sender.toUtf8();
    stream << static_cast<uint32_t>(senderBytes.size());
    stream.writeRawData(senderBytes.constData(), senderBytes.size());
    
    QByteArray contentBytes = msg.content.toUtf8();
    stream << static_cast<uint32_t>(contentBytes.size());
    stream.writeRawData(contentBytes.constData(), contentBytes.size());
    
    stream << msg.timestamp;
    
    return result;
}

ChatMessage ProtocolManager::decodeChatMessage(const QByteArray& data) {
    ChatMessage msg;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    QByteArray senderBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    msg.sender = QString::fromUtf8(senderBytes);
    
    stream >> len;
    QByteArray contentBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    msg.content = QString::fromUtf8(contentBytes);
    
    stream >> msg.timestamp;
    
    return msg;
}

QByteArray ProtocolManager::encodeEncryptionKeyExchange(const EncryptionKeyExchange& exchange) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << static_cast<uint32_t>(exchange.publicKey.size());
    stream.writeRawData(exchange.publicKey.constData(), exchange.publicKey.size());
    
    stream << static_cast<uint32_t>(exchange.nonce.size());
    stream.writeRawData(exchange.nonce.constData(), exchange.nonce.size());
    
    return result;
}

EncryptionKeyExchange ProtocolManager::decodeEncryptionKeyExchange(const QByteArray& data) {
    EncryptionKeyExchange exchange;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    exchange.publicKey = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    
    stream >> len;
    exchange.nonce = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    
    return exchange;
}

QByteArray ProtocolManager::encodeTerminalData(const TerminalData& data) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray dataBytes = data.data.toUtf8();
    stream << static_cast<uint32_t>(dataBytes.size());
    stream.writeRawData(dataBytes.constData(), dataBytes.size());
    
    stream << data.cols << data.rows;
    
    QByteArray shellBytes = data.shellType.toUtf8();
    stream << static_cast<uint32_t>(shellBytes.size());
    stream.writeRawData(shellBytes.constData(), shellBytes.size());
    
    return result;
}

TerminalData ProtocolManager::decodeTerminalData(const QByteArray& data) {
    TerminalData result;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    
    stream >> len;
    QByteArray dataBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    result.data = QString::fromUtf8(dataBytes);
    
    stream >> result.cols >> result.rows;
    
    stream >> len;
    QByteArray shellBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    result.shellType = QString::fromUtf8(shellBytes);
    
    return result;
}

QByteArray ProtocolManager::encodeRecordControl(const RecordControl& control) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    QByteArray pathBytes = control.filePath.toUtf8();
    stream << static_cast<uint32_t>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());
    
    stream << control.fps;
    
    return result;
}

RecordControl ProtocolManager::decodeRecordControl(const QByteArray& data) {
    RecordControl control;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
    uint32_t len;
    stream >> len;
    QByteArray pathBytes = data.mid(stream.device()->pos(), len);
    stream.skipRawData(len);
    control.filePath = QString::fromUtf8(pathBytes);
    
    stream >> control.fps;
    
    return control;
}

QByteArray ProtocolManager::encodeFileBrowserRequest(const FileBrowserRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    QByteArray pathBytes = req.path.toUtf8();
    stream << static_cast<uint32_t>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    QByteArray filterBytes = req.filter.toUtf8();
    stream << static_cast<uint32_t>(filterBytes.size());
    stream.writeRawData(filterBytes.constData(), filterBytes.size());

    return data;
}

FileBrowserRequest ProtocolManager::decodeFileBrowserRequest(const QByteArray& data) {
    FileBrowserRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint32_t len;
    stream >> len;
    req.path = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    if (!stream.atEnd()) {
        stream >> len;
        req.filter = QString::fromUtf8(data.mid(stream.device()->pos(), len));
        stream.skipRawData(len);
    }
    return req;
}

QByteArray ProtocolManager::encodeFileBrowserEntry(const FileBrowserEntry& entry) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    QByteArray nameBytes = entry.name.toUtf8();
    stream << static_cast<uint32_t>(nameBytes.size());
    stream.writeRawData(nameBytes.constData(), nameBytes.size());

    QByteArray pathBytes = entry.path.toUtf8();
    stream << static_cast<uint32_t>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    stream << static_cast<uint8_t>(entry.isDir ? 1 : 0);
    stream << entry.fileSize;

    QByteArray modifiedBytes = entry.lastModified.toUtf8();
    stream << static_cast<uint32_t>(modifiedBytes.size());
    stream.writeRawData(modifiedBytes.constData(), modifiedBytes.size());

    return data;
}

FileBrowserEntry ProtocolManager::decodeFileBrowserEntry(QDataStream& stream, const QByteArray& data) {
    FileBrowserEntry entry;
    uint32_t len;

    stream >> len;
    entry.name = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    stream >> len;
    entry.path = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    uint8_t isDir;
    stream >> isDir;
    entry.isDir = (isDir != 0);

    stream >> entry.fileSize;

    stream >> len;
    entry.lastModified = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    return entry;
}

QByteArray ProtocolManager::encodeFileBrowserResponse(const FileBrowserResponse& resp) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    QByteArray pathBytes = resp.path.toUtf8();
    stream << static_cast<uint32_t>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    stream << static_cast<uint8_t>(resp.success ? 1 : 0);

    QByteArray errorBytes = resp.errorMessage.toUtf8();
    stream << static_cast<uint32_t>(errorBytes.size());
    stream.writeRawData(errorBytes.constData(), errorBytes.size());

    stream << static_cast<uint32_t>(resp.entries.size());
    for (const auto& entry : resp.entries) {
        stream.writeRawData(encodeFileBrowserEntry(entry).constData(), encodeFileBrowserEntry(entry).size());
    }

    return data;
}

FileBrowserResponse ProtocolManager::decodeFileBrowserResponse(const QByteArray& data) {
    FileBrowserResponse resp;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint32_t len;
    stream >> len;
    resp.path = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    uint8_t success;
    stream >> success;
    resp.success = (success != 0);

    stream >> len;
    resp.errorMessage = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    uint32_t entryCount;
    stream >> entryCount;
    for (uint32_t i = 0; i < entryCount; ++i) {
        resp.entries.append(decodeFileBrowserEntry(stream, data));
    }

    return resp;
}

QByteArray ProtocolManager::encodeFileOpRequest(const FileOpRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << static_cast<quint8>(req.op);

    QByteArray pathBytes = req.path.toUtf8();
    stream << static_cast<quint32>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    QByteArray newPathBytes = req.newPath.toUtf8();
    stream << static_cast<quint32>(newPathBytes.size());
    stream.writeRawData(newPathBytes.constData(), newPathBytes.size());

    return data;
}

FileOpRequest ProtocolManager::decodeFileOpRequest(const QByteArray& data) {
    FileOpRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    quint8 op;
    stream >> op;
    req.op = static_cast<FileOp>(op);

    quint32 len;
    stream >> len;
    req.path = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    stream >> len;
    req.newPath = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    return req;
}

QByteArray ProtocolManager::encodeFileOpResponse(const FileOpResponse& resp) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << static_cast<quint8>(resp.op);

    QByteArray pathBytes = resp.path.toUtf8();
    stream << static_cast<quint32>(pathBytes.size());
    stream.writeRawData(pathBytes.constData(), pathBytes.size());

    stream << static_cast<quint8>(resp.success ? 1 : 0);

    QByteArray errBytes = resp.errorMessage.toUtf8();
    stream << static_cast<quint32>(errBytes.size());
    stream.writeRawData(errBytes.constData(), errBytes.size());

    return data;
}

FileOpResponse ProtocolManager::decodeFileOpResponse(const QByteArray& data) {
    FileOpResponse resp;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    quint8 op;
    stream >> op;
    resp.op = static_cast<FileOp>(op);

    quint32 len;
    stream >> len;
    resp.path = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    quint8 success;
    stream >> success;
    resp.success = (success != 0);

    stream >> len;
    resp.errorMessage = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    return resp;
}

QByteArray ProtocolManager::encodeSysInfo(const SysInfo& info) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << info.cpuUsage;
    stream << info.memoryUsage;
    stream << info.memoryTotal;
    stream << info.memoryAvailable;
    stream << info.diskUsage;
    stream << info.diskTotal;
    stream << info.diskFree;
    stream << info.networkRx;
    stream << info.networkTx;

    QByteArray osBytes = info.osName.toUtf8();
    stream << static_cast<uint32_t>(osBytes.size());
    stream.writeRawData(osBytes.constData(), osBytes.size());

    QByteArray verBytes = info.osVersion.toUtf8();
    stream << static_cast<uint32_t>(verBytes.size());
    stream.writeRawData(verBytes.constData(), verBytes.size());

    QByteArray cpuBytes = info.cpuName.toUtf8();
    stream << static_cast<uint32_t>(cpuBytes.size());
    stream.writeRawData(cpuBytes.constData(), cpuBytes.size());

    stream << info.uptime;
    stream << info.processCount;

    return data;
}

SysInfo ProtocolManager::decodeSysInfo(const QByteArray& data) {
    SysInfo info;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> info.cpuUsage;
    stream >> info.memoryUsage;
    stream >> info.memoryTotal;
    stream >> info.memoryAvailable;
    stream >> info.diskUsage;
    stream >> info.diskTotal;
    stream >> info.diskFree;
    stream >> info.networkRx;
    stream >> info.networkTx;

    uint32_t len;
    stream >> len;
    info.osName = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    stream >> len;
    info.osVersion = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    stream >> len;
    info.cpuName = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    stream.skipRawData(len);

    stream >> info.uptime;
    stream >> info.processCount;

    return info;
}

// ───────────── Remote Process Manager (v1.5.0) ─────────────
QByteArray ProtocolManager::encodeProcessListResponse(const ProcessListResponse& resp) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << static_cast<uint8_t>(resp.success ? 1 : 0);
    stream << resp.errorMessage;
    stream << static_cast<uint32_t>(resp.entries.size());
    for (const auto& e : resp.entries) {
        stream << e.pid;
        stream << e.name;
        stream << e.memoryBytes;
    }
    return data;
}

ProcessListResponse ProtocolManager::decodeProcessListResponse(const QByteArray& data) {
    ProcessListResponse resp;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint8_t ok = 0;
    stream >> ok;
    resp.success = (ok != 0);
    stream >> resp.errorMessage;

    uint32_t count = 0;
    stream >> count;
    resp.entries.reserve(static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i) {
        ProcessEntry e;
        stream >> e.pid;
        stream >> e.name;
        stream >> e.memoryBytes;
        resp.entries.append(e);
    }
    return resp;
}

QByteArray ProtocolManager::encodeProcessKillRequest(const ProcessKillRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << req.pid;
    return data;
}

ProcessKillRequest ProtocolManager::decodeProcessKillRequest(const QByteArray& data) {
    ProcessKillRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> req.pid;
    return req;
}

QByteArray ProtocolManager::encodeProcessKillResponse(const ProcessKillResponse& resp) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(resp.success ? 1 : 0);
    stream << resp.pid;
    stream << resp.errorMessage;
    return data;
}

ProcessKillResponse ProtocolManager::decodeProcessKillResponse(const QByteArray& data) {
    ProcessKillResponse resp;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint8_t ok = 0;
    stream >> ok;
    resp.success = (ok != 0);
    stream >> resp.pid;
    stream >> resp.errorMessage;
    return resp;
}

QByteArray ProtocolManager::encodeProcessStartRequest(const ProcessStartRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << req.command;
    stream << req.workingDir;
    return data;
}

ProcessStartRequest ProtocolManager::decodeProcessStartRequest(const QByteArray& data) {
    ProcessStartRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> req.command;
    stream >> req.workingDir;
    return req;
}

QByteArray ProtocolManager::encodeProcessStartResponse(const ProcessStartResponse& resp) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(resp.success ? 1 : 0);
    stream << resp.pid;
    stream << resp.errorMessage;
    return data;
}

ProcessStartResponse ProtocolManager::decodeProcessStartResponse(const QByteArray& data) {
    ProcessStartResponse resp;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint8_t ok = 0;
    stream >> ok;
    resp.success = (ok != 0);
    stream >> resp.pid;
    stream >> resp.errorMessage;
    return resp;
}

QByteArray ProtocolManager::encodePrivacyScreen(bool enabled) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(enabled ? 1 : 0);
    return data;
}

bool ProtocolManager::decodePrivacyScreen(const QByteArray& data) {
    if (data.size() < 1) return false;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint8_t val;
    stream >> val;
    return val != 0;
}

QByteArray ProtocolManager::encodeAnnotationUpdate(const AnnotationUpdate& update) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << update.frameWidth;
    stream << update.frameHeight;
    stream << static_cast<uint32_t>(update.strokes.size());
    for (const AnnotationStroke& st : update.strokes) {
        stream << static_cast<quint32>(st.color.rgba());
        stream << static_cast<int32_t>(st.width);
        stream << static_cast<uint32_t>(st.points.size());
        for (const QPoint& p : st.points) {
            stream << p.x();
            stream << p.y();
        }
    }
    return data;
}

AnnotationUpdate ProtocolManager::decodeAnnotationUpdate(const QByteArray& data) {
    AnnotationUpdate update;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> update.frameWidth;
    stream >> update.frameHeight;
    uint32_t count = 0;
    stream >> count;
    update.strokes.reserve(static_cast<int>(count));
    for (uint32_t i = 0; i < count; ++i) {
        AnnotationStroke st;
        quint32 rgba = 0;
        int32_t w = 3;
        stream >> rgba;
        stream >> w;
        st.color = QColor::fromRgba(rgba);
        st.width = w;
        uint32_t pc = 0;
        stream >> pc;
        st.points.reserve(static_cast<int>(pc));
        for (uint32_t j = 0; j < pc; ++j) {
            int x = 0, y = 0;
            stream >> x;
            stream >> y;
            st.points.append(QPoint(x, y));
        }
        update.strokes.append(st);
    }
    return update;
}

QByteArray ProtocolManager::encodeConsent(bool allowed, const QString& deviceName) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(allowed ? 1 : 0);
    QByteArray nameBytes = deviceName.toUtf8();
    stream << static_cast<uint32_t>(nameBytes.size());
    stream.writeRawData(nameBytes.constData(), nameBytes.size());
    return data;
}

void ProtocolManager::decodeConsent(const QByteArray& data, bool& allowed, QString& deviceName) {
    allowed = false;
    deviceName.clear();
    if (data.size() < 1) return;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint8_t val;
    stream >> val;
    allowed = (val != 0);
    uint32_t len;
    stream >> len;
    deviceName = QString::fromUtf8(data.mid(stream.device()->pos(), len));
}

// ───────────── User Permission Management (v1.8.0) ─────────────

QByteArray ProtocolManager::encodeAuthRequest(const AuthRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    if (req.legacy) {
        // Old format: the entire payload IS the password (no version byte), so
        // legacy controllers/Web clients that only send a single code still work.
        QByteArray pwd = req.password.toUtf8();
        stream.writeRawData(pwd.constData(), pwd.size());
        return data;
    }
    // v2: [0x02][u8 userLen][user][u16 pwdLen BE][pwd]
    stream << static_cast<uint8_t>(0x02);
    QByteArray user = req.username.toUtf8();
    // Truncate the payload itself, not just the length byte: writing more bytes
    // than the declared length would desynchronise the pwdLen field that follows.
    if (user.size() > 255) user.truncate(255);
    stream << static_cast<uint8_t>(user.size());
    stream.writeRawData(user.constData(), user.size());
    QByteArray pwd = req.password.toUtf8();
    stream << static_cast<uint16_t>(pwd.size());
    stream.writeRawData(pwd.constData(), pwd.size()); // size()==pwd.size()
    return data;
}

AuthRequest ProtocolManager::decodeAuthRequest(const QByteArray& data) {
    AuthRequest req;
    if (data.size() >= 4 && data.at(0) == static_cast<char>(0x02)) {
        QDataStream stream(data);
        stream.setByteOrder(QDataStream::BigEndian);
        uint8_t magic;
        stream >> magic; // 0x02
        uint8_t userLen;
        stream >> userLen;
        req.username = QString::fromUtf8(data.mid(stream.device()->pos(), userLen));
        stream.skipRawData(userLen);
        uint16_t pwdLen;
        stream >> pwdLen;
        req.password = QString::fromUtf8(data.mid(stream.device()->pos(), pwdLen));
        req.legacy = false;
    } else {
        req.legacy = true;
        req.password = QString::fromUtf8(data);
    }
    return req;
}

QByteArray ProtocolManager::encodeAuthResponse(const AuthResponse& resp) {
    if (!resp.ok) {
        return QByteArray("FAILED");
    }
    // "OK"(2) + key(32) + iv(16) + [u8 level][u32 caps BE] = 55 bytes.
    // Trailing bytes are ignored by legacy clients (length is >= check).
    QByteArray body = QByteArray("OK");
    body.append(resp.sessionKey.left(32));
    while (body.size() < 34) body.append(static_cast<char>(0));
    body.append(resp.iv.left(16));
    while (body.size() < 50) body.append(static_cast<char>(0));
    QByteArray tail;
    QDataStream ts(&tail, QIODevice::WriteOnly);
    ts.setByteOrder(QDataStream::BigEndian);
    ts << static_cast<uint8_t>(resp.grantedLevel);
    ts << static_cast<uint32_t>(resp.grantedCaps);
    body.append(tail);
    return body;
}

AuthResponse ProtocolManager::decodeAuthResponse(const QByteArray& data) {
    AuthResponse resp;
    if (data.size() >= 2 && data.left(2) == QByteArray("OK")) {
        resp.ok = true;
        resp.sessionKey = data.mid(2, 32);
        resp.iv = data.mid(34, 16);
        if (data.size() >= 55) {
            QDataStream tail(data.mid(50));
            tail.setByteOrder(QDataStream::BigEndian);
            uint8_t lvl;
            tail >> lvl;
            resp.grantedLevel = lvl;
            uint32_t caps;
            tail >> caps;
            resp.grantedCaps = caps;
        }
        // else: legacy 50-byte response -> level/caps default to 0.
    } else {
        resp.ok = false;
    }
    return resp;
}

QByteArray ProtocolManager::encodePermissionDenied(const PermissionDenied& denied) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(denied.capability);
    QByteArray reason = denied.reason.toUtf8();
    stream << static_cast<uint32_t>(reason.size());
    stream.writeRawData(reason.constData(), reason.size());
    return data;
}

PermissionDenied ProtocolManager::decodePermissionDenied(const QByteArray& data) {
    PermissionDenied denied;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t cap;
    stream >> cap;
    denied.capability = cap;
    uint32_t len;
    stream >> len;
    denied.reason = QString::fromUtf8(data.mid(stream.device()->pos(), len));
    return denied;
}

QByteArray ProtocolManager::encodeUserListResponse(const QList<UserRecord>& users) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(users.size());
    for (const auto& u : users) {
        QByteArray un = u.username.toUtf8();
        stream << static_cast<uint32_t>(un.size());
        stream.writeRawData(un.constData(), un.size());
        stream << static_cast<uint8_t>(u.level);
        stream << static_cast<uint8_t>(u.enabled ? 1 : 0);
        stream << static_cast<qint64>(u.lastLogin);
    }
    return data;
}

QList<UserRecord> ProtocolManager::decodeUserListResponse(const QByteArray& data) {
    QList<UserRecord> out;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        UserRecord u;
        uint32_t len;
        stream >> len;
        u.username = QString::fromUtf8(data.mid(stream.device()->pos(), len));
        stream.skipRawData(len);
        uint8_t lvl;
        stream >> lvl;
        u.level = lvl;
        uint8_t en;
        stream >> en;
        u.enabled = (en != 0);
        qint64 ll;
        stream >> ll;
        u.lastLogin = ll;
        out.append(u);
    }
    return out;
}

QByteArray ProtocolManager::encodeDevicePermissionResponse(
    const QList<DevicePermission>& devices) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(devices.size());
    for (const auto& d : devices) {
        QByteArray id = d.deviceId.toUtf8();
        stream << static_cast<uint32_t>(id.size());
        stream.writeRawData(id.constData(), id.size());
        stream << static_cast<int32_t>(d.level);
        stream << static_cast<int32_t>(d.capMask);
        QByteArray note = d.note.toUtf8();
        stream << static_cast<uint32_t>(note.size());
        stream.writeRawData(note.constData(), note.size());
    }
    return data;
}

QList<DevicePermission> ProtocolManager::decodeDevicePermissionResponse(
    const QByteArray& data) {
    QList<DevicePermission> out;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        DevicePermission d;
        uint32_t len;
        stream >> len;
        d.deviceId = QString::fromUtf8(data.mid(stream.device()->pos(), len));
        stream.skipRawData(len);
        qint32 lvl;
        stream >> lvl;
        d.level = lvl;
        qint32 cm;
        stream >> cm;
        d.capMask = cm;
        uint32_t nlen;
        stream >> nlen;
        d.note = QString::fromUtf8(data.mid(stream.device()->pos(), nlen));
        out.append(d);
    }
    return out;
}

QByteArray ProtocolManager::encodePermissionToggleResponse(uint32_t toggles) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << toggles;
    return data;
}

uint32_t ProtocolManager::decodePermissionToggleResponse(const QByteArray& data) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t t;
    stream >> t;
    return t;
}

QByteArray ProtocolManager::encodePermissionToggleRequest(uint32_t capability, bool enabled) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << capability;
    stream << static_cast<uint8_t>(enabled ? 1 : 0);
    return data;
}

bool ProtocolManager::decodePermissionToggleRequest(const QByteArray& data,
                                                    uint32_t& capability, bool& enabled) {
    if (data.size() < 5) return false;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> capability;
    uint8_t en;
    stream >> en;
    enabled = (en != 0);
    return true;
}

QByteArray ProtocolManager::encodeUserMutation(const UserMutation& mutation) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    QByteArray un = mutation.username.toUtf8();
    stream << static_cast<uint32_t>(un.size());
    stream.writeRawData(un.constData(), un.size());
    QByteArray pw = mutation.password.toUtf8();
    stream << static_cast<uint32_t>(pw.size());
    stream.writeRawData(pw.constData(), pw.size());
    stream << static_cast<uint8_t>(mutation.level);
    stream << static_cast<uint8_t>(mutation.enabled ? 1 : 0);
    stream << static_cast<uint8_t>(mutation.fields);
    return data;
}

UserMutation ProtocolManager::decodeUserMutation(const QByteArray& data) {
    UserMutation m;
    if (data.size() < 4) return m;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t ulen;
    stream >> ulen;
    if (ulen > static_cast<uint32_t>(data.size())) return m;
    m.username = QString::fromUtf8(data.mid(stream.device()->pos(), ulen));
    stream.skipRawData(static_cast<int>(ulen));
    uint32_t plen;
    stream >> plen;
    if (plen > static_cast<uint32_t>(data.size())) return m;
    m.password = QString::fromUtf8(data.mid(stream.device()->pos(), plen));
    stream.skipRawData(static_cast<int>(plen));
    uint8_t lvl = 1, en = 1, fields = 0;
    stream >> lvl;
    stream >> en;
    stream >> fields;
    m.level = lvl;
    m.enabled = (en != 0);
    m.fields = fields;
    return m;
}

QByteArray ProtocolManager::encodeAuditLogResponse(const QJsonArray& entries) {
    QByteArray json = QJsonDocument(entries).toJson(QJsonDocument::Compact);
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint32_t>(json.size());
    stream.writeRawData(json.constData(), json.size());
    return data;
}

QJsonArray ProtocolManager::decodeAuditLogResponse(const QByteArray& data) {
    QJsonArray out;
    if (data.size() < 4) return out;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t len;
    stream >> len;
    if (len == 0 || len > static_cast<uint32_t>(data.size())) return out;
    QByteArray json = data.mid(stream.device()->pos(), static_cast<int>(len));
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return out;
    return doc.array();
}

QByteArray ProtocolManager::encodeTemporaryGrant(const TemporaryGrant& grant) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    QByteArray id = grant.deviceId.toUtf8();
    stream << static_cast<uint32_t>(id.size());
    stream.writeRawData(id.constData(), id.size());
    stream << static_cast<int32_t>(grant.level);
    stream << static_cast<int32_t>(grant.capMask);
    stream << static_cast<qint64>(grant.expiresAt);
    return data;
}

TemporaryGrant ProtocolManager::decodeTemporaryGrant(const QByteArray& data) {
    TemporaryGrant g;
    if (data.size() < 4 + 4 + 4 + 8) return g;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint32_t len;
    stream >> len;
    if (len > static_cast<uint32_t>(data.size())) return g;
    g.deviceId = QString::fromUtf8(data.mid(stream.device()->pos(), static_cast<int>(len)));
    stream.skipRawData(static_cast<int>(len));
    qint32 level, mask;
    qint64 exp;
    stream >> level;
    stream >> mask;
    stream >> exp;
    g.level = level;
    g.capMask = mask;
    g.expiresAt = exp;
    return g;
}

QByteArray ProtocolManager::encodeQualityInfo(const QualityInfo& info) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << static_cast<uint32_t>(info.jpegQuality);
    stream << static_cast<uint32_t>(info.captureFps);
    stream << static_cast<uint32_t>(info.targetFps);
    stream << static_cast<uint32_t>(info.bandwidthKbps);
    stream << static_cast<int64_t>(info.roundTripMs);

    return data;
}

QualityInfo ProtocolManager::decodeQualityInfo(const QByteArray& data) {
    QualityInfo info;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    uint32_t tmp;
    stream >> tmp; info.jpegQuality = static_cast<int>(tmp);
    stream >> tmp; info.captureFps = static_cast<int>(tmp);
    stream >> tmp; info.targetFps = static_cast<int>(tmp);
    stream >> tmp; info.bandwidthKbps = static_cast<int>(tmp);
    stream >> info.roundTripMs;

    return info;
}

QByteArray ProtocolManager::encodeQualityRequest(const QualityRequest& req) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<uint8_t>(req.level);
    stream << static_cast<uint8_t>(req.gameMode ? 1 : 0);
    return data;
}

QualityRequest ProtocolManager::decodeQualityRequest(const QByteArray& data) {
    QualityRequest req;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    uint8_t level = 0;
    uint8_t game = 0;
    stream >> level;
    stream >> game;
    req.level = static_cast<QualityLevel>(level);
    req.gameMode = (game != 0);
    return req;
}

QByteArray ProtocolManager::encodeSyncPair(const SyncPair& pair) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << pair.hostDir;
    stream << pair.localDir;
    return data;
}

SyncPair ProtocolManager::decodeSyncPair(const QByteArray& data) {
    SyncPair pair;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> pair.hostDir;
    stream >> pair.localDir;
    return pair;
}

QByteArray ProtocolManager::encodeSyncNotify(const SyncNotify& note) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << note.hostDir;
    stream << note.hostFilePath;
    stream << note.localDir;
    stream << static_cast<uint64_t>(note.size);
    stream << static_cast<int64_t>(note.mtime);
    return data;
}

SyncNotify ProtocolManager::decodeSyncNotify(const QByteArray& data) {
    SyncNotify note;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> note.hostDir;
    stream >> note.hostFilePath;
    stream >> note.localDir;
    uint64_t size = 0;
    int64_t mtime = 0;
    stream >> size;
    stream >> mtime;
    note.size = size;
    note.mtime = mtime;
    return note;
}

// ────────── E2EE (End-to-End Encryption) encode/decode ──────────

QByteArray ProtocolManager::encodeE2EEKeyExchange(const QByteArray& publicKey, const QString& sessionId) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << sessionId;
    stream << publicKey;
    return data;
}

QByteArray ProtocolManager::decodeE2EEKeyExchange(const QByteArray& data, QByteArray& publicKey, QString& sessionId) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> sessionId;
    stream >> publicKey;
    return data;
}

QByteArray ProtocolManager::encodeE2EEKeyResponse(const QByteArray& publicKey, const QByteArray& encryptedSharedSecret, const QString& sessionId) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << sessionId;
    stream << publicKey;
    stream << encryptedSharedSecret;
    return data;
}

QByteArray ProtocolManager::decodeE2EEKeyResponse(const QByteArray& data, QByteArray& publicKey, QByteArray& encryptedSharedSecret, QString& sessionId) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> sessionId;
    stream >> publicKey;
    stream >> encryptedSharedSecret;
    return data;
}

QByteArray ProtocolManager::encodeE2EESessionEstablished(const QString& sessionId, const QByteArray& encryptedNonce) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << sessionId;
    stream << encryptedNonce;
    return data;
}

QByteArray ProtocolManager::decodeE2EESessionEstablished(const QByteArray& data, QString& sessionId, QByteArray& encryptedNonce) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> sessionId;
    stream >> encryptedNonce;
    return data;
}

QByteArray ProtocolManager::encodeE2EEMessage(const QByteArray& encryptedPayload, const QString& sessionId, const QByteArray& nonce) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << sessionId;
    stream << nonce;
    stream << encryptedPayload;
    return data;
}

QByteArray ProtocolManager::decodeE2EEMessage(const QByteArray& data, QByteArray& encryptedPayload, QString& sessionId, QByteArray& nonce) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> sessionId;
    stream >> nonce;
    stream >> encryptedPayload;
    return data;
}

QByteArray ProtocolManager::encodeE2EEAck(const QString& sessionId, const QByteArray& messageId) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << sessionId;
    stream << messageId;
    return data;
}

QByteArray ProtocolManager::decodeE2EEAck(const QByteArray& data, QString& sessionId, QByteArray& messageId) {
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> sessionId;
    stream >> messageId;
    return data;
}

} // namespace xrk
