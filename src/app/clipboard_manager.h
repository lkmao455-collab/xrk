#pragma once

#include <QObject>
#include <QClipboard>
#include <QTimer>
#include <QMimeData>
#include <functional>
#include "core/types.h"

namespace xrk {

class TcpConnection;
class ClipboardHistory;

class ClipboardManager : public QObject {
    Q_OBJECT
public:
    explicit ClipboardManager(TcpConnection* connection, QObject* parent = nullptr);
    ~ClipboardManager();

    void startMonitoring();
    void stopMonitoring();
    void setEnabled(bool enabled);
    bool isEnabled() const;

    void sendClipboardData(const ClipboardData& data);
    void setConnection(TcpConnection* connection);
    void setHistory(ClipboardHistory* history);

    // Host-side: when set, local clipboard changes are delivered to this
    // callback (which broadcasts to all clients) instead of a single connection.
    void setBroadcastCallback(std::function<void(const ClipboardData&)> cb);

    // Write remote clipboard content to the local clipboard WITHOUT triggering
    // a local-change broadcast (prevents infinite sync loops).
    void applyRemoteClipboard(const QByteArray& data, const QString& mimeType);

signals:
    void clipboardDataReceived(const ClipboardData& data);

private slots:
    void onClipboardChanged();
    void onCheckTimer();

private:
    QByteArray getClipboardContent(QString& mimeType);
    void setClipboardContent(const QByteArray& data, const QString& mimeType);

    TcpConnection* m_connection = nullptr;
    QTimer* m_checkTimer = nullptr;
    QClipboard* m_clipboard = nullptr;
    bool m_enabled = true;
    QByteArray m_lastClipboardData;
    QString m_lastMimeType;
    ClipboardHistory* m_history = nullptr;
    std::function<void(const ClipboardData&)> m_broadcast;
    bool m_applyingRemote = false;
    static constexpr int CHECK_INTERVAL_MS = 500;
};

} // namespace xrk
