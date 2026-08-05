#include "web_socket_gateway.h"
#include "core/logger.h"
#include "core/message_codec.h"

#include <QWebSocket>
#include <QWebSocketServer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace xrk {

namespace {
// QWebSocket caps *incoming* messages/frames at a small default (the value is
// raised here so large screen frames relayed from the Host are accepted). Outgoing
// messages are fragmented automatically by QWebSocket, so no outgoing cap is needed.
constexpr quint64 kMaxWsMessageSize = 64ULL * 1024 * 1024;
}

WebSocketGateway::WebSocketGateway(QObject* parent)
    : QObject(parent) {
}

WebSocketGateway::~WebSocketGateway() {
    stop();
}

bool WebSocketGateway::start(quint16 wsPort, quint16 tcpPort, const QString& tcpHost) {
    if (m_running) {
        stop();
    }

    m_wsPort = wsPort;
    m_tcpHost = tcpHost;
    m_tcpPort = tcpPort;

    m_wsServer = new QWebSocketServer(QStringLiteral("xrk-ws-gateway"),
                                      QWebSocketServer::NonSecureMode, this);
    bool listenResult = m_wsServer->listen(QHostAddress::Any, wsPort);
    LOG_INFO("WebSocketGateway: listen() returned " + QString(listenResult ? "true" : "false") +
             " error: " + m_wsServer->errorString());
    if (!listenResult) {
        LOG_ERROR("WebSocketGateway: failed to listen on ws port " + QString::number(wsPort));
        delete m_wsServer;
        m_wsServer = nullptr;
        return false;
    }
    connect(m_wsServer, &QWebSocketServer::newConnection,
            this, &WebSocketGateway::onNewWebSocketConnection);

    m_httpServer = new QTcpServer(this);
    bool httpListenResult = m_httpServer->listen(QHostAddress::Any, wsPort + 1);
    LOG_INFO("WebSocketGateway: http listen() returned " + QString(httpListenResult ? "true" : "false") +
             " error: " + m_httpServer->errorString());
    if (!httpListenResult) {
        LOG_WARNING("WebSocketGateway: failed to serve web page on http port "
                    + QString::number(wsPort + 1));
    } else {
        connect(m_httpServer, &QTcpServer::newConnection,
                this, &WebSocketGateway::onHttpNewConnection);
    }

    m_running = true;
    LOG_INFO("WebSocketGateway: ws://*:" + QString::number(wsPort) +
             " -> tcp " + tcpHost + ":" + QString::number(tcpPort) +
             ", web page http://*:" + QString::number(wsPort + 1) + "/");
    return true;
}

void WebSocketGateway::stop() {
    if (m_wsServer) {
        m_wsServer->close();
        delete m_wsServer;
        m_wsServer = nullptr;
    }
    if (m_httpServer) {
        m_httpServer->close();
        delete m_httpServer;
        m_httpServer = nullptr;
    }
    while (!m_sessions.isEmpty()) {
        closeSession(m_sessions.first());
    }
    m_running = false;
}

bool WebSocketGateway::isRunning() const {
    return m_running;
}

quint16 WebSocketGateway::webSocketPort() const {
    return m_wsPort;
}

quint16 WebSocketGateway::httpPort() const {
    return m_wsPort + 1;
}

int WebSocketGateway::activeSessions() const {
    return m_sessions.size();
}

void WebSocketGateway::onNewWebSocketConnection() {
    QWebSocket* ws = m_wsServer->nextPendingConnection();
    if (!ws) {
        return;
    }

    auto* session = new Session();
    session->ws = ws;
    session->peerAddress = ws->peerAddress().toString();

    ws->setParent(this);
    // Accept large relayed messages (screenshots / live frames) from clients, and
    // fragment large outgoing messages into frames. Default caps are far too small
    // for a 1920x1080 JPEG, which would otherwise tear down the relayed connection.
    ws->setMaxAllowedIncomingMessageSize(kMaxWsMessageSize);
    ws->setMaxAllowedIncomingFrameSize(kMaxWsMessageSize);
    ws->setOutgoingFrameSize(kMaxWsMessageSize);
    connect(ws, &QWebSocket::binaryMessageReceived,
            this, &WebSocketGateway::onWsBinaryMessageReceived);
    connect(ws, &QWebSocket::textMessageReceived,
            this, &WebSocketGateway::onWsTextMessageReceived);
    connect(ws, &QWebSocket::disconnected,
            this, &WebSocketGateway::onWsDisconnected);

    auto* tcp = new QTcpSocket(this);
    session->tcp = tcp;
    connect(tcp, &QTcpSocket::connected, this, &WebSocketGateway::onTcpConnected);
    connect(tcp, &QTcpSocket::readyRead, this, &WebSocketGateway::onTcpReadyRead);
    connect(tcp, &QTcpSocket::disconnected, this, &WebSocketGateway::onTcpDisconnected);
    connect(tcp, &QTcpSocket::errorOccurred, this, [this, session](QAbstractSocket::SocketError) {
        if (!m_sessions.contains(session)) {
            return;
        }
        QString errMsg = "TCP error for " + session->peerAddress;
        LOG_WARNING("WebSocketGateway: " + errMsg);
        emit relayError(errMsg);
        closeSession(session);
    });

    tcp->connectToHost(m_tcpHost, m_tcpPort);

    m_sessions.append(session);
    emit clientConnected(session->peerAddress);
    emit sessionCountChanged(m_sessions.size());
}

void WebSocketGateway::onWsBinaryMessageReceived(const QByteArray& message) {
    Session* session = findSession(sender());
    if (!session || !session->tcp) {
        return;
    }
    if (session->tcp->state() == QAbstractSocket::ConnectedState) {
        session->tcp->write(message);
    } else {
        session->pendingOut.append(message);
    }
}

void WebSocketGateway::onWsTextMessageReceived(const QString& message) {
    Session* session = findSession(sender());
    if (!session) {
        return;
    }

    // Try to parse as JSON signaling message (WebRTC SDP/ICE)
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // Not a valid JSON signaling message, ignore
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "offer" || type == "answer" || type == "candidate") {
        // Forward signaling message to the TCP host
        QJsonObject relayMsg;
        relayMsg["type"] = "webrtc_signaling";
        relayMsg["signaling"] = obj;
        QJsonDocument relayDoc(relayMsg);
        QByteArray relayData = relayDoc.toJson();

        if (session->tcp && session->tcp->state() == QAbstractSocket::ConnectedState) {
            session->tcp->write(relayData);
        } else {
            session->pendingOut.append(relayData);
        }
    }
}

void WebSocketGateway::onWsDisconnected() {
    Session* session = findSession(sender());
    if (session) {
        closeSession(session);
    }
}

void WebSocketGateway::onTcpConnected() {
    Session* session = findSession(sender());
    if (!session) {
        return;
    }
    if (!session->pendingOut.isEmpty()) {
        session->tcp->write(session->pendingOut);
        session->pendingOut.clear();
    }
}

void WebSocketGateway::onTcpReadyRead() {
    Session* session = findSession(sender());
    if (!session || !session->ws) {
        return;
    }
    // Buffer TCP bytes and relay only *complete* XRK messages as individual
    // WebSocket binary frames. A large Host message (screenshot / live screen
    // frame) can arrive across several TCP chunks; sending each chunk as its own
    // WS message would fragment the application message and break clients that
    // expect one WS message per XRK frame (e.g. the browser SPA decoder).
    session->tcpBuffer.append(session->tcp->readAll());

    while (session->tcpBuffer.size() >= MessageCodec::HEADER_FIXED_SIZE + 4) {
        MessageHeader header;
        uint32_t sidLen = 0;
        if (!MessageCodec::parseHeader(session->tcpBuffer, header, sidLen)) {
            // Header not (yet) valid: wait for more data, or resync past a
            // corrupt prefix by dropping bytes up to the next MAGIC.
            if (session->tcpBuffer.size() > MessageCodec::HEADER_FIXED_SIZE + 4) {
                int idx = session->tcpBuffer.indexOf(
                    QByteArray(reinterpret_cast<const char*>(&MAGIC), sizeof(MAGIC)), 1);
                if (idx > 0) {
                    session->tcpBuffer.remove(0, idx);
                } else {
                    session->tcpBuffer.clear();
                }
            }
            break;
        }
        const uint32_t total = MessageCodec::HEADER_FIXED_SIZE + 4 + sidLen
                               + header.length + MessageCodec::CHECKSUM_SIZE;
        if (session->tcpBuffer.size() < total) {
            break; // full message not arrived yet
        }
        QByteArray msg = session->tcpBuffer.left(total);
        session->tcpBuffer.remove(0, total);
        session->ws->sendBinaryMessage(msg);
    }
}

void WebSocketGateway::onTcpDisconnected() {
    Session* session = findSession(sender());
    if (session) {
        closeSession(session);
    }
}

void WebSocketGateway::onHttpNewConnection() {
    QTcpSocket* socket = m_httpServer->nextPendingConnection();
    if (!socket) {
        return;
    }
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        m_httpBuffers[socket].append(socket->readAll());
        QByteArray& request = m_httpBuffers[socket];

        if (!request.contains("\r\n\r\n")) {
            return;
        }

        // Parse the request line: "GET <path> HTTP/1.x"
        const int firstCr = request.indexOf("\r\n");
        const QByteArray requestLine = request.left(firstCr);
        QByteArray path = "/index.html";
        if (requestLine.startsWith("GET ")) {
            const int firstSpace = requestLine.indexOf(' ');
            const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
            if (firstSpace > 0 && secondSpace > firstSpace) {
                path = requestLine.mid(firstSpace + 1, secondSpace - firstSpace - 1);
            }
        }
        if (path.isEmpty() || path == "/") {
            path = "/index.html";
        }

        // Serve the bundled web client. Every URL (except the root) maps
        // straight onto a Qt resource under :/web, so the React SPA's hashed
        // assets (/assets/*.js, *.css) and /favicon.svg are delivered too.
        // Reject path traversal for safety.
        QByteArray body;
        bool ok = false;
        if (!path.contains("..")) {
            const QString resource = QStringLiteral(":/web") + QString::fromUtf8(path);
            QFile file(resource);
            if (file.open(QIODevice::ReadOnly)) {
                body = file.readAll();
                ok = true;
            }
        }

        QByteArray response;
        if (ok) {
            response.append("HTTP/1.1 200 OK\r\n");
            response.append("Content-Type: " + contentTypeForPath(path) + "\r\n");
            response.append("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
            response.append("Connection: close\r\n\r\n");
            response.append(body);
        } else {
            response.append("HTTP/1.1 404 Not Found\r\n");
            response.append("Content-Length: 0\r\n");
            response.append("Connection: close\r\n\r\n");
        }
        socket->write(response);
        socket->flush();
        socket->disconnectFromHost();
        m_httpBuffers.remove(socket);
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

QByteArray WebSocketGateway::contentTypeForPath(const QByteArray& path) const {
    if (path.endsWith(".html") || path.endsWith(".htm")) {
        return "text/html; charset=utf-8";
    }
    if (path.endsWith(".js") || path.endsWith(".mjs")) {
        return "application/javascript; charset=utf-8";
    }
    if (path.endsWith(".css")) {
        return "text/css; charset=utf-8";
    }
    if (path.endsWith(".svg")) {
        return "image/svg+xml";
    }
    if (path.endsWith(".json")) {
        return "application/json; charset=utf-8";
    }
    if (path.endsWith(".png")) {
        return "image/png";
    }
    if (path.endsWith(".ico")) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

WebSocketGateway::Session* WebSocketGateway::findSession(QObject* senderObject) const {
    for (Session* session : m_sessions) {
        if (session->ws == senderObject || session->tcp == senderObject) {
            return session;
        }
    }
    return nullptr;
}

void WebSocketGateway::closeSession(Session* session) {
    m_sessions.removeAll(session);
    if (session->ws) {
        session->ws->disconnect(this);
        session->ws->close();
        session->ws->deleteLater();
    }
    if (session->tcp) {
        session->tcp->disconnect(this);
        session->tcp->abort();
        session->tcp->deleteLater();
    }
    emit clientDisconnected(session->peerAddress);
    emit sessionCountChanged(m_sessions.size());
    delete session;
}

} // namespace xrk
