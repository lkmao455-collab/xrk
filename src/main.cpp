#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QFile>
#include "ui/main_window.h"
#include "app/host.h"
#include "app/relay_server.h"
#include "app/web_socket_gateway.h"
#include "app/host_service.h"
#include "core/logger.h"
#include "core/translation_manager.h"
#include "core/theme_manager.h"

int main(int argc, char *argv[])
{
    bool relayMode = false;
    quint16 relayPort = 9997;
    bool wsMode = false;
    quint16 wsPort = 8080;
    bool hostMode = false;
    quint16 hostPort = 9999;
    QString logFilePath;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--install-service") == 0) {
            return xrk::HostService::installService() ? 0 : 1;
        } else if (strcmp(argv[i], "--uninstall-service") == 0) {
            return xrk::HostService::uninstallService() ? 0 : 1;
        } else if (strcmp(argv[i], "--service") == 0) {
            return xrk::HostService::runService(argc, argv);
        } else if (strcmp(argv[i], "--logfile") == 0 && i + 1 < argc) {
            logFilePath = QString::fromUtf8(argv[++i]);
        } else if (strcmp(argv[i], "--relay") == 0 && i + 1 < argc) {
            relayMode = true;
            relayPort = static_cast<quint16>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--ws") == 0 && i + 1 < argc) {
            wsMode = true;
            wsPort = static_cast<quint16>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            hostMode = true;
            hostPort = static_cast<quint16>(atoi(argv[++i]));
        }
    }

    if (!logFilePath.isEmpty()) {
        xrk::Logger::instance().setLogFile(logFilePath);
    }

    if (wsMode) {
        // Headless Phase E2 gateway: browser clients connect on ws://*:wsPort,
        // are bridged to the desktop Host on 127.0.0.1:9999, and get the web
        // client page served on http://*:wsPort+1/.
        QCoreApplication app(argc, argv);
        app.setApplicationName("XRK");
        app.setApplicationVersion("1.0.0");

        xrk::WebSocketGateway gateway;
        if (gateway.start(wsPort, 9999)) {
            LOG_INFO("WebSocket gateway running on ws port " + QString::number(wsPort)
                     + " (web page on http port " + QString::number(gateway.httpPort()) + ")");
            return app.exec();
        } else {
            LOG_ERROR("Failed to start WebSocket gateway on port " + QString::number(wsPort));
            return 1;
        }
    }

    if (hostMode) {
        // Headless Phase E2 Host: a controller (native or the WebSocket gateway)
        // connects on tcp://*:hostPort. Runs without any GUI so it can back the
        // gateway for browser/mobile end-to-end tests. Consent is auto-granted
        // locally because there is no interactive user to approve a request.
        QCoreApplication app(argc, argv);
        app.setApplicationName("XRK");
        app.setApplicationVersion("1.0.0");

        xrk::Host host;
        QObject::connect(&host, &xrk::Host::consentRequested, &host,
                         [&host](const QString& clientId, const QString&) {
                             LOG_INFO("Headless host: auto-granting consent for " + clientId);
                             host.grantConsent(clientId);
                         });

        if (host.start(hostPort)) {
            LOG_INFO("Headless host running on port " + QString::number(hostPort));
            return app.exec();
        } else {
            LOG_ERROR("Failed to start headless host on port " + QString::number(hostPort));
            return 1;
        }
    }

    if (relayMode) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("XRK");
        app.setApplicationVersion("1.0.0");

        xrk::RelayServer relay;
        if (relay.start(relayPort)) {
            LOG_INFO("Relay server running on port " + QString::number(relayPort));
            return app.exec();
        } else {
            LOG_ERROR("Failed to start relay server on port " + QString::number(relayPort));
            return 1;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("XRK");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/icons/xrk_window.png"));

    // Load theme from ThemeManager
    xrk::ThemeManager::instance().loadThemes();
    QString themeQSS = xrk::ThemeManager::instance().generateQSS();
    app.setStyleSheet(themeQSS);

    xrk::TranslationManager::instance().initialize();

    xrk::MainWindow window;
    window.show();

    return app.exec();
}
