#include "protocol_manager.h"
#include "message_codec.h"
#include <QDataStream>
#include <QBuffer>

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

QByteArray ProtocolManager::encodeMonitorList(const QList<MonitorInfo>& monitors) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    
    stream << static_cast<uint32_t>(monitors.size());
    
    for (const MonitorInfo& info : monitors) {
        QByteArray infoData = encodeMonitorInfo(info);
        stream << static_cast<uint32_t>(infoData.size());
        stream.writeRawData(infoData.constData(), infoData.size());
    }
    
    return result;
}

QList<MonitorInfo> ProtocolManager::decodeMonitorList(const QByteArray& data) {
    QList<MonitorInfo> monitors;
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);
    
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

} // namespace xrk
