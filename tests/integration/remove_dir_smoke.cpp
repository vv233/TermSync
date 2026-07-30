// Verifies deleting a NON-EMPTY remote directory works (recursive delete).
// Usage: remove_dir_smoke <host> <port> <user> <pw> <remoteDirToDelete>

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "queue/SftpSession.h"

using namespace termsync;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 6) {
        std::fprintf(stderr, "usage: remove_dir_smoke <host> <port> <user> <pw> <remoteDir>\n");
        return 2;
    }
    core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString dir = QString::fromLocal8Bit(argv[5]);

    auto *s = new transfer::SftpSession(p, QString());
    int rc = 1;
    QObject::connect(s, &transfer::SftpSession::operationFinished,
                     [&](const QString &op, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[op] %s ok=%d %s\n", op.toUtf8().constData(), ok,
                                      msg.toUtf8().constData());
                         if (op == QLatin1String("remove")) {
                             rc = ok ? 0 : 1;
                             QCoreApplication::quit();
                         }
                     });
    QObject::connect(s, &transfer::SftpSession::connected, [&] {
        std::fprintf(stderr, "[connected] removing non-empty dir %s\n", dir.toUtf8().constData());
        s->removeEntry(dir, /*isDir=*/true);
    });
    s->connectToHost();
    QTimer::singleShot(15000, [] { QCoreApplication::quit(); });
    app.exec();
    std::fprintf(stderr, "[%s] recursive dir delete\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
