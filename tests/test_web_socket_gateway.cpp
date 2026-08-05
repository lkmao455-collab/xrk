#include <gtest/gtest.h>
#include "web_socket_gateway.h"
#include "core/message_codec.h"
#include "core/types.h"
#include <QWebSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QRegularExpression>
#include <QTimer>

namespace {

bool waitForSpy(QSignalSpy& spy, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (spy.count() == 0 && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return spy.count() > 0;
}

bool waitUntil(const std::function<bool()>& predicate, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return predicate();
}

// A tiny TCP echo server acting as the "Host" behind the gateway.
class EchoServer : public QObject {
public:
    QTcpServer server;
    int accepted = 0;
    int disconnected = 0;

    EchoServer() {
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = server.nextPendingConnection()) {
                ++accepted;
                connect(socket, &QTcpSocket::readyRead, this, [socket]() {
                    socket->write(socket->readAll());
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                    ++disconnected;
                    socket->deleteLater();
                });
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const { return server.serverPort(); }
};

bool connectWs(QWebSocket& ws, quint16 port, int timeoutMs = 3000) {
    QSignalSpy spy(&ws, &QWebSocket::connected);
    ws.open(QUrl(QString("ws://127.0.0.1:%1").arg(port)));
    return waitForSpy(spy, timeoutMs);
}

// A distinguishable ScreenFrame-ish payload. Not a real ScreenFrame (the codec
// only validates the header + checksum), but enough to assert that a relayed
// message is byte-for-byte intact after reassembly.
QByteArray makeFrame(int id, int size = 4096) {
    QByteArray p;
    p.append("FRAME:");
    p.append(QByteArray::number(id));
    p.append(':');
    p.append(QByteArray(size, 'x'));
    return p;
}

// A TCP peer that stands in for the desktop Host and pushes XRK messages
// toward the gateway. Used to verify the gateway reassembles complete XRK
// messages before relaying them as WebSocket binary frames (regression: large
// frames used to be split across several WS messages when they arrived in
// multiple TCP segments, leaving the browser SPA with incomplete frames).
class FrameServer : public QObject {
public:
    QTcpServer server;
    QList<QTcpSocket*> clients;
    FrameServer() {
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                clients.append(s);
                onClient(s);
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }
    virtual ~FrameServer() {
        qDeleteAll(clients);
    }
    virtual void onClient(QTcpSocket* s) { Q_UNUSED(s); }
    quint16 port() const { return server.serverPort(); }
};

// Pushes TWO complete messages back-to-back. Loopback may deliver them in a
// single TCP segment; the gateway must still relay them as TWO separate WS
// binary messages (not merged, not partial).
class MultiFrameServer : public FrameServer {
public:
    void onClient(QTcpSocket* s) override {
        s->write(xrk::MessageCodec::encode(xrk::MessageType::SCREENSHOT_RESP, makeFrame(1, 5000)));
        s->write(xrk::MessageCodec::encode(xrk::MessageType::SCREENSHOT_RESP, makeFrame(2, 5000)));
    }
};

// Pushes ONE message split across TWO TCP segments with a delay between them,
// so the gateway's onTcpReadyRead first observes a partial message (buffer <
// full message -> no relay) and then the remainder (full message -> exactly
// one relay). Without the reassembly fix this arrives as two WS messages.
class SplitFrameServer : public FrameServer {
public:
    void onClient(QTcpSocket* s) override {
        QByteArray msg = xrk::MessageCodec::encode(xrk::MessageType::SCREENSHOT_RESP, makeFrame(123, 20000));
        const int half = msg.size() / 2;
        s->write(msg.left(half));
        QTimer::singleShot(40, s, [s, msg, half]() { s->write(msg.mid(half)); });
    }
};

} // namespace

namespace xrk {

class WebSocketGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(WebSocketGatewayTest, EchoRoundTrip) {
    EchoServer echo;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(echo.port() + 10, echo.port(), "127.0.0.1"));
    EXPECT_TRUE(gateway.isRunning());
    EXPECT_TRUE(gateway.httpPort() == gateway.webSocketPort() + 1);
    EXPECT_TRUE(gateway.activeSessions() == 0);

    QWebSocket ws;
    const bool wsConnected = connectWs(ws, gateway.webSocketPort());
    ASSERT_TRUE(wsConnected);

    QSignalSpy msgSpy(&ws, &QWebSocket::binaryMessageReceived);
    const QByteArray payload = makeFrame(7, 200);
    ws.sendBinaryMessage(xrk::MessageCodec::encode(xrk::MessageType::AUTH_REQ, payload));

    const bool msgReceived = waitForSpy(msgSpy, 3000);
    ASSERT_TRUE(msgReceived);
    ASSERT_GE(msgSpy.count(), 1);
    MessageType t;
    QByteArray got;
    QString sid;
    ASSERT_TRUE(xrk::MessageCodec::decode(msgSpy.at(0).at(0).toByteArray(), t, got, sid));
    EXPECT_EQ(t, xrk::MessageType::AUTH_REQ);
    EXPECT_EQ(got, payload);
    EXPECT_TRUE(gateway.activeSessions() == 1);

    ws.close();
    gateway.stop();
    EXPECT_FALSE(gateway.isRunning());
}

TEST_F(WebSocketGatewayTest, MultipleClients) {
    EchoServer echo;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(echo.port() + 20, echo.port(), "127.0.0.1"));

    QWebSocket ws1;
    QWebSocket ws2;
    const bool ws1Connected = connectWs(ws1, gateway.webSocketPort());
    ASSERT_TRUE(ws1Connected);
    const bool ws2Connected = connectWs(ws2, gateway.webSocketPort());
    ASSERT_TRUE(ws2Connected);
    EXPECT_EQ(gateway.activeSessions(), 2);

    QSignalSpy spy1(&ws1, &QWebSocket::binaryMessageReceived);
    QSignalSpy spy2(&ws2, &QWebSocket::binaryMessageReceived);
    const QByteArray p1 = makeFrame(11, 200);
    const QByteArray p2 = makeFrame(22, 200);
    ws1.sendBinaryMessage(xrk::MessageCodec::encode(xrk::MessageType::AUTH_REQ, p1));
    ws2.sendBinaryMessage(xrk::MessageCodec::encode(xrk::MessageType::AUTH_REQ, p2));

    const bool spy1Ready = waitForSpy(spy1, 3000);
    ASSERT_TRUE(spy1Ready);
    const bool spy2Ready = waitForSpy(spy2, 3000);
    ASSERT_TRUE(spy2Ready);
    MessageType t;
    QByteArray got1;
    QByteArray got2;
    QString sid;
    ASSERT_TRUE(xrk::MessageCodec::decode(spy1.at(0).at(0).toByteArray(), t, got1, sid));
    ASSERT_TRUE(xrk::MessageCodec::decode(spy2.at(0).at(0).toByteArray(), t, got2, sid));
    EXPECT_EQ(got1, p1);
    EXPECT_EQ(got2, p2);

    ws1.close();
    ws2.close();
    gateway.stop();
}

TEST_F(WebSocketGatewayTest, ClientClosePropagatesToTarget) {
    EchoServer echo;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(echo.port() + 30, echo.port(), "127.0.0.1"));

    QWebSocket ws;
    const bool wsConnected = connectWs(ws, gateway.webSocketPort());
    ASSERT_TRUE(wsConnected);

    const bool accepted = waitUntil([&]() { return echo.accepted >= 1; }, 3000);
    EXPECT_TRUE(accepted);

    ws.close();
    const bool disconnected = waitUntil([&]() { return echo.disconnected >= 1; }, 3000);
    EXPECT_TRUE(disconnected);
    const bool noSessions = waitUntil([&]() { return gateway.activeSessions() == 0; }, 3000);
    EXPECT_TRUE(noSessions);

    gateway.stop();
}

TEST_F(WebSocketGatewayTest, HttpServesWebPage) {
    EchoServer echo;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(echo.port() + 40, echo.port(), "127.0.0.1"));

    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(QNetworkRequest(
        QUrl(QString("http://127.0.0.1:%1/").arg(gateway.httpPort()))));
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    const bool finished = waitForSpy(finishedSpy, 3000);
    ASSERT_TRUE(finished);
    QByteArray body = reply->readAll();
    EXPECT_EQ(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    EXPECT_TRUE(body.contains("XRK"));
    // The gateway serves the bundled React SPA shell (index.html), whose static
    // markup references the JavaScript asset that implements the WebSocket
    // client. Verifying the asset reference confirms the real SPA (not a stale
    // placeholder) is being served.
    EXPECT_TRUE(body.contains("/assets/"));

    // The SPA shell references hashed assets under /assets/. Verify the gateway
    // actually serves those static resources (regression: it used to 404 them,
    // leaving the page blank in the browser).
    QRegularExpression assetRe("/assets/[^\\\"']+");
    QRegularExpressionMatchIterator it = assetRe.globalMatch(QString::fromUtf8(body));
    int servedAssets = 0;
    while (it.hasNext()) {
        const QString assetPath = it.next().captured(0);
        QNetworkReply* assetReply = nam.get(QNetworkRequest(
            QUrl(QString("http://127.0.0.1:%1%2").arg(gateway.httpPort()).arg(assetPath))));
        QSignalSpy assetSpy(assetReply, &QNetworkReply::finished);
        ASSERT_TRUE(waitForSpy(assetSpy, 3000));
        EXPECT_EQ(assetReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200)
            << "asset " << assetPath.toStdString();
        assetReply->deleteLater();
        ++servedAssets;
    }
    EXPECT_GE(servedAssets, 1);

    // The page also references /favicon.svg; the gateway must serve it too.
    QNetworkReply* favicon = nam.get(QNetworkRequest(
        QUrl(QString("http://127.0.0.1:%1/favicon.svg").arg(gateway.httpPort()))));
    QSignalSpy faviconSpy(favicon, &QNetworkReply::finished);
    ASSERT_TRUE(waitForSpy(faviconSpy, 3000));
    EXPECT_EQ(favicon->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    favicon->deleteLater();
    reply->deleteLater();

    QNetworkReply* notFound = nam.get(QNetworkRequest(
        QUrl(QString("http://127.0.0.1:%1/nope").arg(gateway.httpPort()))));
    QSignalSpy notFoundSpy(notFound, &QNetworkReply::finished);
    const bool notFoundFinished = waitForSpy(notFoundSpy, 3000);
    ASSERT_TRUE(notFoundFinished);
    EXPECT_EQ(notFound->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    notFound->deleteLater();

    gateway.stop();
}

TEST_F(WebSocketGatewayTest, StopWithActiveClients) {
    EchoServer echo;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(echo.port() + 50, echo.port(), "127.0.0.1"));

    QWebSocket ws;
    const bool wsConnected = connectWs(ws, gateway.webSocketPort());
    ASSERT_TRUE(wsConnected);
    const bool accepted = waitUntil([&]() { return echo.accepted >= 1; }, 3000);
    EXPECT_TRUE(accepted);

    gateway.stop();
    const bool notRunning = !gateway.isRunning();
    EXPECT_TRUE(notRunning);
    const bool noSessions = gateway.activeSessions() == 0;
    EXPECT_TRUE(noSessions);
}

TEST_F(WebSocketGatewayTest, ReassemblesMultipleCoalescedMessages) {
    MultiFrameServer host;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(host.port() + 10, host.port(), "127.0.0.1"));

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, gateway.webSocketPort()));

    QSignalSpy spy(&ws, &QWebSocket::binaryMessageReceived);
    const bool gotTwo = waitUntil([&]() { return spy.count() >= 2; }, 3000);
    ASSERT_TRUE(gotTwo);
    EXPECT_EQ(spy.count(), 2);

    MessageType t;
    QByteArray p;
    QString sid;
    ASSERT_TRUE(MessageCodec::decode(spy.at(0).at(0).toByteArray(), t, p, sid));
    EXPECT_EQ(t, MessageType::SCREENSHOT_RESP);
    ASSERT_TRUE(MessageCodec::decode(spy.at(1).at(0).toByteArray(), t, p, sid));
    EXPECT_EQ(t, MessageType::SCREENSHOT_RESP);

    ws.close();
    gateway.stop();
}

TEST_F(WebSocketGatewayTest, ReassemblesFragmentedSingleMessage) {
    SplitFrameServer host;
    WebSocketGateway gateway;
    ASSERT_TRUE(gateway.start(host.port() + 11, host.port(), "127.0.0.1"));

    QWebSocket ws;
    ASSERT_TRUE(connectWs(ws, gateway.webSocketPort()));

    QSignalSpy spy(&ws, &QWebSocket::binaryMessageReceived);
    const bool gotOne = waitUntil([&]() { return spy.count() >= 1; }, 3000);
    ASSERT_TRUE(gotOne);
    // The fix: a message split across TCP segments must be relayed as EXACTLY
    // one WebSocket binary message, never two.
    EXPECT_EQ(spy.count(), 1);

    QByteArray m = spy.at(0).at(0).toByteArray();
    MessageType t;
    QByteArray p;
    QString sid;
    ASSERT_TRUE(MessageCodec::decode(m, t, p, sid));
    EXPECT_EQ(t, MessageType::SCREENSHOT_RESP);
    EXPECT_EQ(p, makeFrame(123, 20000));

    ws.close();
    gateway.stop();
}

} // namespace xrk