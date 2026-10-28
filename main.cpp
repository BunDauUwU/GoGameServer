#include "Server.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GoGameServer"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Local WebSocket JSON file server"));
    parser.addHelpOption();
    parser.addOption({{"p", "port"}, "Listen port (localhost only).", "port", "14509"});
    parser.addOption({{"d", "data-file"}, "JSON storage file.", "path", "data/state.json"});
    parser.addOption({"main-minutes", "Main time per player (10 or 15 minutes).", "minutes", "10"});
    parser.process(app);

    bool portOk = false;
    const uint port = parser.value("port").toUInt(&portOk);
    if (!portOk || port == 0 || port > 65535) {
        std::fprintf(stderr, "Port must be between 1 and 65535\n");
        return 1;
    }
    bool minutesOk = false;
    const int minutes = parser.value("main-minutes").toInt(&minutesOk);
    if (!minutesOk || (minutes != 10 && minutes != 15)) {
        std::fprintf(stderr, "Main time must be 10 or 15 minutes\n");
        return 1;
    }
    ByoYomiClock timeControl;
    timeControl.mainRemainingMs = minutes * 60000;
    Server server(parser.value("data-file"), nullptr, timeControl);
    QString error;
    if (!server.start(static_cast<quint16>(port), error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    std::fprintf(stdout,
                 "Listening on ws://127.0.0.1:%u\nJSON storage: %s\n",
                 port,
                 qPrintable(server.dataFile()));
    std::fflush(stdout);
    return app.exec();
}
