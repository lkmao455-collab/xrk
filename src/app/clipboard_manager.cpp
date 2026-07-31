#include "clipboard_manager.h"
#include "clipboard_history.h"
#include "core/tcp_connection.h"
#include "core/protocol_manager.h"
#include "core/logger.h"
#include <QApplication>
#include <QMimeData>
#include <QBuffer>
#include <QUrl>

namespace xrk {

ClipboardManager::ClipboardManager(TcpConnection* connection, QObject* parent)
    : QObject(parent), m_connection(connection) {
    m_clipboard = QApplication::clipboard();
    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, &ClipboardManager::onCheckTimer);
}

ClipboardManager::~ClipboardManager() {
    stopMonitoring();
}

void ClipboardManager::startMonitoring() {
    if (!m_enabled) return;
    
    connect(m_clipboard, &QClipboard::changed, this, &ClipboardManager::onClipboardChanged);
    m_checkTimer->start(CHECK_INTERVAL_MS);
    
    LOG_INFO("Clipboard monitoring started");
}

void ClipboardManager::stopMonitoring() {
    m_checkTimer->stop();
    if (m_clipboard) {
        disconnect(m_clipboard, &QClipboard::changed, this, &ClipboardManager::onClipboardChanged);
    }
    LOG_INFO("Clipboard monitoring stopped");
}

void ClipboardManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (enabled) {
        startMonitoring();
    } else {
        stopMonitoring();
    }
}

bool ClipboardManager::isEnabled() const {
    return m_enabled;
}

void ClipboardManager::sendClipboardData(const ClipboardData& data) {
    if (!m_connection || !m_connection->isConnected()) {
        return;
    }
    
    QByteArray payload = ProtocolManager::encodeClipboardData(data);
    QByteArray message = ProtocolManager::encode(MessageType::CLIPBOARD_DATA, payload);
    m_connection->send(message);
}

void ClipboardManager::onClipboardChanged() {
    if (!m_enabled) return;
    
    QString mimeType;
    QByteArray content = getClipboardContent(mimeType);
    
    if (content.isEmpty() || content == m_lastClipboardData) {
        return;
    }
    
    m_lastClipboardData = content;
    m_lastMimeType = mimeType;
    
    ClipboardData data;
    data.mimeType = mimeType;
    data.data = content;
    data.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    
    sendClipboardData(data);
    LOG_DEBUG("Clipboard data sent: " + QString::number(content.size()) + " bytes");

    if (m_history) {
        m_history->addEntry(mimeType, content);
    }
}

void ClipboardManager::onCheckTimer() {
    if (!m_enabled || !m_clipboard) return;
    
    QString mimeType;
    QByteArray content = getClipboardContent(mimeType);
    
    if (!content.isEmpty() && content != m_lastClipboardData) {
        m_lastClipboardData = content;
        m_lastMimeType = mimeType;
        
        ClipboardData data;
        data.mimeType = mimeType;
        data.data = content;
        data.timestamp = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
        
        sendClipboardData(data);

        if (m_history) {
            m_history->addEntry(mimeType, content);
        }
    }
}

void ClipboardManager::setupConnections() {
}

void ClipboardManager::setConnection(TcpConnection* connection) {
    m_connection = connection;
    if (m_connection) {
        connect(m_connection, &TcpConnection::readyRead, this, [this](const QByteArray& data) {
            MessageType type;
            QByteArray payload;
            QString sessionId;
            
            if (ProtocolManager::decode(data, type, payload, sessionId)) {
                if (type == MessageType::CLIPBOARD_DATA) {
                    ClipboardData clipData = ProtocolManager::decodeClipboardData(payload);
                    emit clipboardDataReceived(clipData);
                    setClipboardContent(clipData.data, clipData.mimeType);

                    if (m_history) {
                        m_history->addEntry(clipData.mimeType, clipData.data);
                    }
                }
            }
        });
    }
}

void ClipboardManager::setHistory(ClipboardHistory* history) {
    m_history = history;
}

QByteArray ClipboardManager::getClipboardContent(QString& mimeType) {
    if (!m_clipboard) return QByteArray();
    
    const QMimeData* mimeData = m_clipboard->mimeData();
    if (!mimeData) return QByteArray();
    
    if (mimeData->hasText()) {
        mimeType = "text/plain";
        return mimeData->text().toUtf8();
    } else if (mimeData->hasHtml()) {
        mimeType = "text/html";
        return mimeData->html().toUtf8();
    } else if (mimeData->hasImage()) {
        mimeType = "image/png";
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        QByteArray data;
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return data;
    } else if (mimeData->hasUrls()) {
        mimeType = "text/uri-list";
        QByteArray data;
        for (const QUrl& url : mimeData->urls()) {
            data.append(url.toString().toUtf8() + "\n");
        }
        return data;
    }
    
    return QByteArray();
}

void ClipboardManager::setClipboardContent(const QByteArray& data, const QString& mimeType) {
    if (!m_clipboard || data.isEmpty()) return;
    
    QMimeData* mimeData = new QMimeData();
    
    if (mimeType == "text/plain") {
        mimeData->setText(QString::fromUtf8(data));
    } else if (mimeType == "text/html") {
        mimeData->setHtml(QString::fromUtf8(data));
    } else if (mimeType == "image/png") {
        QImage image;
        image.loadFromData(data, "PNG");
        mimeData->setImageData(image);
    } else if (mimeType == "text/uri-list") {
        QList<QUrl> urls;
        QByteArray textData = data;
        while (!textData.isEmpty()) {
            int newlinePos = textData.indexOf('\n');
            QByteArray line;
            if (newlinePos >= 0) {
                line = textData.left(newlinePos);
                textData = textData.mid(newlinePos + 1);
            } else {
                line = textData;
                textData.clear();
            }
            if (!line.isEmpty()) {
                urls.append(QUrl(QString::fromUtf8(line.trimmed())));
            }
        }
        mimeData->setUrls(urls);
    }
    
    m_clipboard->setMimeData(mimeData);
}

} // namespace xrk
