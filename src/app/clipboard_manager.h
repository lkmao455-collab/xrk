#pragma once

#include <QObject>
#include <QClipboard>
#include <QTimer>
#include <QMimeData>
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

signals:
    void clipboardDataReceived(const ClipboardData& data);

private slots:
    void onClipboardChanged();
    void onCheckTimer();

private:
    void setupConnections();
    QByteArray getClipboardContent(QString& mimeType);
    void setClipboardContent(const QByteArray& data, const QString& mimeType);

    TcpConnection* m_connection = nullptr;
    QTimer* m_checkTimer = nullptr;
    QClipboard* m_clipboard = nullptr;
    bool m_enabled = true;
    QByteArray m_lastClipboardData;
    QString m_lastMimeType;
    ClipboardHistory* m_history = nullptr;
    static constexpr int CHECK_INTERVAL_MS = 500;
};

} // namespace xrk
