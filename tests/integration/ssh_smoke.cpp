// Headless integration smoke test for core::SshConnection.
//
// Connects to an SSH server, opens a shell, and prints whatever bytes come
// back. Exits 0 once any shell output is received, 1 on error/timeout.
//
// Usage:
//   ssh_smoke <host> <port> <user> <password>
// or via environment: SSH_HOST SSH_PORT SSH_USER SSH_PASS
//
// This is NOT part of the unit test suite (it needs a live server); it is a
// manual/CI aid. Point it at the Docker OpenSSH container (see the plan's M2
// verification) or any reachable SSH server.

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "ssh/SshConnection.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    auto envOr = [](const char *key, const QString &fallback) {
        const QByteArray v = qgetenv(key);
        return v.isEmpty() ? fallback : QString::fromUtf8(v);
    };

    termsync::core::SshConnectionParams p;
    p.host = argc > 1 ? argv[1] : envOr("SSH_HOST", "");
    p.port = static_cast<quint16>((argc > 2 ? QString(argv[2]) : envOr("SSH_PORT", "22")).toInt());
    p.username = argc > 3 ? argv[3] : envOr("SSH_USER", "");
    p.password = argc > 4 ? argv[4] : envOr("SSH_PASS", "");

    if (p.host.isEmpty() || p.username.isEmpty()) {
        std::fprintf(stderr, "usage: ssh_smoke <host> <port> <user> <password>\n");
        return 2;
    }

    int exitCode = 1;
    int bytesSeen = 0;

    termsync::core::SshConnection conn;

    QObject::connect(&conn, &termsync::core::SshConnection::hostKeyFingerprint,
                     [](const QString &fp) {
                         std::fprintf(stderr, "[hostkey] %s\n", fp.toUtf8().constData());
                     });
    QObject::connect(&conn, &termsync::core::SshConnection::connected, [] {
        std::fprintf(stderr, "[connected] shell open\n");
    });
    QObject::connect(&conn, &termsync::core::SshConnection::dataReceived,
                     [&](const QByteArray &data) {
                         bytesSeen += data.size();
                         std::fwrite(data.constData(), 1, data.size(), stdout);
                         std::fflush(stdout);
                         if (bytesSeen > 0) {
                             exitCode = 0;
                             QTimer::singleShot(500, [] { QCoreApplication::quit(); });
                         }
                     });
    QObject::connect(&conn, &termsync::core::SshConnection::authenticationFailed,
                     [&](const QString &r) {
                         std::fprintf(stderr, "[auth failed] %s\n", r.toUtf8().constData());
                         QCoreApplication::quit();
                     });
    QObject::connect(&conn, &termsync::core::SshConnection::errorOccurred,
                     [&](const QString &m) {
                         std::fprintf(stderr, "[error] %s\n", m.toUtf8().constData());
                         QCoreApplication::quit();
                     });

    conn.connectToHost(p);

    // Overall watchdog.
    QTimer::singleShot(15000, [] {
        std::fprintf(stderr, "[timeout]\n");
        QCoreApplication::quit();
    });

    app.exec();
    std::fprintf(stderr, "[exit %d, %d bytes]\n", exitCode, bytesSeen);
    return exitCode;
}
