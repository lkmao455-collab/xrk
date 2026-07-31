#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QFile>
#include "ui/main_window.h"
#include "app/relay_server.h"
#include "app/host_service.h"
#include "core/logger.h"
#include "core/translation_manager.h"

int main(int argc, char *argv[])
{
    bool relayMode = false;
    quint16 relayPort = 9997;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--install-service") == 0) {
            return xrk::HostService::installService() ? 0 : 1;
        } else if (strcmp(argv[i], "--uninstall-service") == 0) {
            return xrk::HostService::uninstallService() ? 0 : 1;
        } else if (strcmp(argv[i], "--service") == 0) {
            return xrk::HostService::runService(argc, argv);
        } else if (strcmp(argv[i], "--relay") == 0 && i + 1 < argc) {
            relayMode = true;
            relayPort = static_cast<quint16>(atoi(argv[++i]));
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

    // Load centralized dark theme
    QFile themeFile(":/theme.qss");
    if (themeFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(themeFile.readAll());
        themeFile.close();
    }

    xrk::TranslationManager::instance().initialize();

    xrk::MainWindow window;
    window.show();

    return app.exec();
}
