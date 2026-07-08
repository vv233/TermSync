// End-to-end smoke for X11 forwarding (M11). Opens an SSH shell with X11
// forwarding on, runs `xdpyinfo` on the remote (which opens an X connection back
// through our forwarder to the local X server, exercising the cookie swap), and
// checks it succeeds. Needs a live SSH server whose host runs an X client, and a
// reachable X server at x11Host:(6000+display).
//
// Usage: x11_smoke <host> <port> <user> <password> [x11Host] [display]
//   env XAUTHORITY points at the Xauthority holding the local X-server cookie.

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "ssh/SshConnection.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: x11_smoke <host> <port> <user> <pass> [x11Host] [display]\n");
        return 2;
    }

    termsync::core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    p.x11Forwarding = true;
    if (argc > 5)
        p.x11Host = argv[5];
    if (argc > 6)
        p.x11Display = QString(argv[6]).toInt();

    int exitCode = 1;
    QByteArray acc;
    bool sent = false;

    termsync::core::SshConnection conn;
    QObject::connect(&conn, &termsync::core::SshConnection::hostKeyFingerprint,
                     [&conn](const QString &) { conn.approveHostKey(true); });
    QObject::connect(&conn, &termsync::core::SshConnection::connected, [&] {
        std::fprintf(stderr, "[connected] requesting X11, running xdpyinfo\n");
        // Give the shell a moment, then run xdpyinfo against the forwarded $DISPLAY.
        QTimer::singleShot(600, [&] {
            if (sent)
                return;
            sent = true;
            conn.sendData(
                "echo DISPLAY=$DISPLAY; xdpyinfo >/tmp/xdpy.out 2>&1; "
                "echo X11RESULT=$?; sed -n '1,3p' /tmp/xdpy.out\n");
        });
    });
    QObject::connect(&conn, &termsync::core::SshConnection::dataReceived,
                     [&](const QByteArray &data) {
                         acc += data;
                         std::fwrite(data.constData(), 1, data.size(), stderr);
                         if (acc.contains("X11RESULT=0")) {
                             exitCode = 0;
                             QTimer::singleShot(300, [] { QCoreApplication::quit(); });
                         } else if (acc.contains("X11RESULT=")) {
                             QTimer::singleShot(300, [] { QCoreApplication::quit(); });
                         }
                     });
    QObject::connect(&conn, &termsync::core::SshConnection::authenticationFailed,
                     [](const QString &r) {
                         std::fprintf(stderr, "\n[auth failed] %s\n", r.toUtf8().constData());
                         QCoreApplication::quit();
                     });
    QObject::connect(&conn, &termsync::core::SshConnection::errorOccurred,
                     [](const QString &m) {
                         std::fprintf(stderr, "\n[error] %s\n", m.toUtf8().constData());
                     });

    conn.connectToHost(p);
    QTimer::singleShot(20000, [] {
        std::fprintf(stderr, "\n[timeout]\n");
        QCoreApplication::quit();
    });

    app.exec();
    std::fprintf(stderr, "\n[%s]\n", exitCode == 0 ? "PASS: X11 forwarding works"
                                                   : "FAIL: xdpyinfo did not succeed");
    return exitCode;
}
