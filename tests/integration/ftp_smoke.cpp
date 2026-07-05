// Manual FTP smoke test for transfer::FtpFileEngine.
//
// Usage: ftp_smoke <host> <port> <user> <password> [remote-dir] [--ftps]
// Connects, lists a directory. Read-only servers exercise connect+list.

#include <QCoreApplication>
#include <cstdio>

#include "ftp/FtpFileEngine.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 5) {
        std::fprintf(stderr, "usage: ftp_smoke <host> <port> <user> <pass> [dir] [--ftps]\n");
        return 2;
    }

    termsync::core::SshConnectionParams p;
    p.host = argv[1];
    p.port = static_cast<quint16>(QString(argv[2]).toInt());
    p.username = argv[3];
    p.password = argv[4];
    const QString dir = argc > 5 && QString(argv[5]) != "--ftps" ? argv[5] : "/";

    termsync::transfer::FtpFileEngine ftp;
    for (int i = 5; i < argc; ++i)
        if (QString(argv[i]) == "--ftps")
            ftp.setExplicitTls(true);

    if (!ftp.connectToHost(p)) {
        std::fprintf(stderr, "[connect failed] %s\n", ftp.lastError().toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[connected]\n");

    QVector<termsync::transfer::FileEntry> entries;
    if (!ftp.listDirectory(dir, &entries)) {
        std::fprintf(stderr, "[list failed] %s\n", ftp.lastError().toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[list] %s -> %lld entries\n", dir.toUtf8().constData(),
                 static_cast<long long>(entries.size()));
    for (const auto &e : entries)
        std::fprintf(stdout, "%s%s\t%llu\n", e.isDirectory ? "d " : "- ",
                     e.name.toUtf8().constData(),
                     static_cast<unsigned long long>(e.size));
    return 0;
}
