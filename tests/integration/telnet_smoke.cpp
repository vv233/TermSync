// Headless Telnet smoke test. Connects, negotiates options, and prints the
// banner/prompt the server sends. Exits 0 once any application data arrives.
//
// Usage: telnet_smoke <host> <port>

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "telnet/TelnetConnection.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: telnet_smoke <host> <port>\n");
        return 2;
    }

    termsync::core::TelnetConnection conn;
    int exitCode = 1;
    int bytes = 0;

    QObject::connect(&conn, &termsync::core::TelnetConnection::connected,
                     [] { std::fprintf(stderr, "[connected]\n"); });
    QObject::connect(&conn, &termsync::core::TelnetConnection::dataReceived,
                     [&](const QByteArray &data) {
                         bytes += data.size();
                         std::fwrite(data.constData(), 1, data.size(), stdout);
                         std::fflush(stdout);
                         if (bytes > 20) {
                             exitCode = 0;
                             QTimer::singleShot(400, [] { QCoreApplication::quit(); });
                         }
                     });
    QObject::connect(&conn, &termsync::core::TelnetConnection::errorOccurred,
                     [&](const QString &m) {
                         std::fprintf(stderr, "[error] %s\n", m.toUtf8().constData());
                         QCoreApplication::quit();
                     });

    conn.connectToHost(argv[1], static_cast<quint16>(QString(argv[2]).toInt()));
    QTimer::singleShot(12000, [] {
        std::fprintf(stderr, "[timeout]\n");
        QCoreApplication::quit();
    });

    app.exec();
    std::fprintf(stderr, "\n[exit %d, %d bytes]\n", exitCode, bytes);
    return exitCode;
}
