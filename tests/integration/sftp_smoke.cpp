// Manual SFTP smoke test for transfer::SftpFileEngine.
//
// Usage:
//   sftp_smoke <host> <port> <user> <password> [remote-dir]
// or via environment: SSH_HOST SSH_PORT SSH_USER SSH_PASS SFTP_DIR
//
// The harness connects, lists the directory, uploads a small temp file, then
// downloads it back and verifies the bytes match.

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "sftp/SftpFileEngine.h"

namespace {

QString envOr(const char *key, const QString &fallback)
{
    const QByteArray v = qgetenv(key);
    return v.isEmpty() ? fallback : QString::fromUtf8(v);
}

QString joinRemote(const QString &dir, const QString &name)
{
    if (dir.isEmpty() || dir == ".")
        return name;
    if (dir.endsWith('/'))
        return dir + name;
    return dir + '/' + name;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    termsync::core::SshConnectionParams p;
    p.host = argc > 1 ? argv[1] : envOr("SSH_HOST", "");
    p.port = static_cast<quint16>((argc > 2 ? QString(argv[2]) : envOr("SSH_PORT", "22")).toInt());
    p.username = argc > 3 ? argv[3] : envOr("SSH_USER", "");
    p.password = argc > 4 ? argv[4] : envOr("SSH_PASS", "");
    const QString remoteDir = argc > 5 ? argv[5] : envOr("SFTP_DIR", ".");

    if (p.host.isEmpty() || p.username.isEmpty()) {
        std::fprintf(stderr, "usage: sftp_smoke <host> <port> <user> <password> [remote-dir]\n");
        return 2;
    }

    termsync::transfer::SftpFileEngine sftp;
    if (!sftp.connectToHost(p)) {
        std::fprintf(stderr, "[connect failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[hostkey] %s\n", sftp.hostKeyFingerprint().toUtf8().constData());

    QVector<termsync::transfer::SftpEntry> entries;
    if (!sftp.listDirectory(remoteDir, &entries)) {
        std::fprintf(stderr, "[list failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[list] %s -> %lld entries\n",
                 remoteDir.toUtf8().constData(),
                 static_cast<long long>(entries.size()));
    for (const auto &entry : entries)
        std::fprintf(stdout, "%s%s\t%llu\n",
                     entry.isDirectory ? "d " : "- ",
                     entry.name.toUtf8().constData(),
                     static_cast<unsigned long long>(entry.size));

    QTemporaryDir tmp;
    const QByteArray payload("termsync-sftp-smoke\n");
    const QString localUpload = tmp.filePath("upload.txt");
    const QString localDownload = tmp.filePath("download.txt");
    QFile out(localUpload);
    if (!out.open(QIODevice::WriteOnly) || out.write(payload) != payload.size()) {
        std::fprintf(stderr, "[local write failed]\n");
        return 1;
    }
    out.close();

    const QString remoteFile = joinRemote(remoteDir, ".termsync-sftp-smoke.txt");
    if (!sftp.uploadFile(localUpload, remoteFile)) {
        std::fprintf(stderr, "[upload failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    if (!sftp.downloadFile(remoteFile, localDownload)) {
        std::fprintf(stderr, "[download failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }

    QFile in(localDownload);
    if (!in.open(QIODevice::ReadOnly) || in.readAll() != payload) {
        std::fprintf(stderr, "[verify failed]\n");
        return 1;
    }

    std::fprintf(stderr, "[ok] upload/download verified at %s\n",
                 remoteFile.toUtf8().constData());
    return 0;
}
