// Exercises sudo through the REAL SftpSession (worker thread + queued slots),
// the way the browser does — not the raw engine. Mirrors the app: connect,
// list, enable sudo, list a root-only dir.
//
// Usage: sudo_session_smoke <host> <port> <user> <password> [sudo-pw]

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "queue/SftpSession.h"

using namespace termsync;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr, "usage: sudo_session_smoke <host> <port> <user> <pw> [sudopw]\n");
        return 2;
    }
    core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString sudoPw = argc > 5 ? argv[5] : argv[4];

    auto *s = new transfer::SftpSession(p, QString());
    int rc = 1;

    QObject::connect(s, &transfer::SftpSession::connectionFailed, [&](const QString &r) {
        std::fprintf(stderr, "[fail] connect: %s\n", r.toUtf8().constData());
        QCoreApplication::quit();
    });
    QObject::connect(s, &transfer::SftpSession::sudoModeChanged,
                     [&](bool on, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[sudoModeChanged] on=%d ok=%d msg=%s\n",
                                      on, ok, msg.toUtf8().constData());
                         if (on && ok)
                             s->listDirectory(QStringLiteral("/root")); // root-only
                     });
    QObject::connect(s, &transfer::SftpSession::directoryListed,
                     [&](const QString &path, const QVector<transfer::SftpEntry> &e) {
                         std::fprintf(stderr, "[listed] %s -> %d entries\n",
                                      path.toUtf8().constData(), int(e.size()));
                         if (path == QLatin1String("/root")) {
                             rc = e.isEmpty() ? 1 : 0; // sudo list must return entries
                             QCoreApplication::quit();
                         }
                     });
    QObject::connect(s, &transfer::SftpSession::operationFinished,
                     [&](const QString &op, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[op] %s ok=%d msg=%s\n",
                                      op.toUtf8().constData(), ok, msg.toUtf8().constData());
                         if (op == QLatin1String("list") && !ok)
                             QCoreApplication::quit();
                     });
    QObject::connect(s, &transfer::SftpSession::connected, [&] {
        std::fprintf(stderr, "[connected] enabling sudo\n");
        s->listDirectory(QStringLiteral(".")); // like the browser's initial list
        QTimer::singleShot(600, [&] { s->setSudo(true, sudoPw); });
    });

    s->connectToHost();
    QTimer::singleShot(15000, [] { QCoreApplication::quit(); });
    app.exec();
    std::fprintf(stderr, "[%s] sudo via real SftpSession\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
