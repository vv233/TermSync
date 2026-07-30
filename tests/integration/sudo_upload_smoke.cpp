// Verifies uploading a file under sudo (to a root-only dir) works.
// Usage: sudo_upload_smoke <host> <port> <user> <pw> <localfile> <remotePath>

#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>
#include <cstdio>

#include "queue/SftpSession.h"

using namespace termsync;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 7) {
        std::fprintf(stderr, "usage: sudo_upload_smoke <host> <port> <user> <pw> <local> <remote>\n");
        return 2;
    }
    core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString local = QString::fromLocal8Bit(argv[5]);
    const QString remote = QString::fromLocal8Bit(argv[6]);
    const QString pw = argv[4];

    auto *s = new transfer::SftpSession(p, QString());
    int rc = 1;

    QObject::connect(s, &transfer::SftpSession::transferFinished,
                     [&](int id, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[transferFinished] id=%d ok=%d %s\n", id, ok,
                                      msg.toUtf8().constData());
                         rc = ok ? 0 : 1;
                         QCoreApplication::quit();
                     });
    QObject::connect(s, &transfer::SftpSession::sudoModeChanged,
                     [&](bool on, bool ok, const QString &msg) {
                         std::fprintf(stderr, "[sudo] on=%d ok=%d %s\n", on, ok,
                                      msg.toUtf8().constData());
                         if (on && ok) {
                             transfer::TransferItem item;
                             item.direction = transfer::TransferItem::Upload;
                             item.localPath = local;
                             item.remotePath = remote;
                             item.displayName = QFileInfo(local).fileName();
                             item.size = static_cast<quint64>(QFileInfo(local).size());
                             s->enqueue(item);
                         } else if (!ok) {
                             QCoreApplication::quit();
                         }
                     });
    QObject::connect(s, &transfer::SftpSession::connected,
                     [&] { s->setSudo(true, pw); });
    s->connectToHost();
    QTimer::singleShot(20000, [] { QCoreApplication::quit(); });
    app.exec();
    std::fprintf(stderr, "[%s] sudo upload\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
