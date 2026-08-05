#pragma once

#include <QObject>
#include <QList>
#include <QByteArray>
#include <QHash>

class QTcpServer;
class QTcpSocket;
class QWebSocket;
class QWebSocketServer;

namespace xrk {

// Phase E2 (web/mobile): transparent WebSocket <-> TCP gateway.
//
// A browser or mobile client cannot speak raw TCP, so this class terminates a
// WebSocket and relays the binary payload of every WebSocket frame to a TCP
// connection it opens toward a desktop Host (default 127.0.0.1:9999). Because
// the XRK wire protocol (MessageCodec / ProtocolManager) is transport-agnostic,
// the Host treats the gateway's TCP socket exactly like a native controller:
// auth, consent, screen-frame streaming and mouse/key input all work unchanged.
//
// The gateway also runs a tiny HTTP responder on wsPort+1 that serves the
// bundled web client page (":/web/client.html"), so a browser can simply open
// http://host:wsPort+1/ and the page connects back on ws://host:wsPort.
class WebSocketGateway : public QObject {
    Q_OBJECT
public:
    explicit WebSocketGateway(QObject* parent = nullptr);
    ~WebSocketGateway();

    bool start(quint16 wsPort, quint16 tcpPort, const QString& tcpHost = QStringLiteral("127.0.0.1"));
    void stop();
    bool isRunning() const;
    quint16 webSocketPort() const;
    quint16 httpPort() const;
    int activeSessions() const;

signals:
    void clientConnected(const QString& peerAddress);
    void clientDisconnected(const QString& peerAddress);
    void relayError(const QString& message);
    void sessionCountChanged(int count);

private slots:
    void onNewWebSocketConnection();
    void onWsBinaryMessageReceived(const QByteArray& message);
    void onWsTextMessageReceived(const QString& message);
    void onWsDisconnected();
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onHttpNewConnection();

private:
    struct Session {
        QWebSocket* ws = nullptr;
        QTcpSocket* tcp = nullptr;
        QString peerAddress;
        QByteArray pendingOut;   // WS->TCP bytes buffered until TCP connects
        QByteArray tcpBuffer;    // TCP->WS bytes buffered until a full XRK message arrives
    };

    Session* findSession(QObject* sender) const;
    void closeSession(Session* session);
    QByteArray contentTypeForPath(const QByteArray& path) const;

    QWebSocketServer* m_wsServer = nullptr;
    QTcpServer* m_httpServer = nullptr;
    QList<Session*> m_sessions;
    QHash<QTcpSocket*, QByteArray> m_httpBuffers;
    QString m_tcpHost;
    quint16 m_tcpPort = 0;
    quint16 m_wsPort = 0;
    bool m_running = false;
};

} // namespace xrk
