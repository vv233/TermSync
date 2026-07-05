// Integration check for resumable SFTP transfers (M18c).
//
// Exercises SftpFileEngine::setResume(true) for both directions:
//   - download: pre-seed the local file with a valid prefix, resume, verify full
//   - upload:   pre-seed the remote file with a valid prefix, resume, verify full
//
// Usage: sftp_resume <host> <port> <user> <password> <remote-dir> [size-mb]
// Exit code 0 = all checks passed.

#include <QCoreApplication>
#include <QCryptographicHash>
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
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

// Deterministic pseudo-random content so a prefix of the full file is a valid
// prefix of what a resumed transfer must reproduce.
bool writeFile(const QString &path, quint64 bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QByteArray chunk(4 * 1024 * 1024, Qt::Uninitialized);
    quint32 x = 0xDEADBEEFu;
    for (qsizetype i = 0; i < chunk.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        chunk[i] = static_cast<char>(x >> 24);
    }
    quint64 written = 0;
    while (written < bytes) {
        const qint64 want = static_cast<qint64>(
            qMin<quint64>(static_cast<quint64>(chunk.size()), bytes - written));
        if (f.write(chunk.constData(), want) != want)
            return false;
        written += static_cast<quint64>(want);
    }
    return f.flush();
}

// Copy the first `bytes` of src into dst (the "already transferred" prefix).
bool writePrefix(const QString &src, const QString &dst, quint64 bytes)
{
    QFile in(src), out(dst);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray data = in.read(static_cast<qint64>(bytes));
    return out.write(data) == data.size() && out.flush();
}

QByteArray sha(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    return h.addData(&f) ? h.result() : QByteArray{};
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
    const quint64 sizeMb = static_cast<quint64>(
        (argc > 6 ? QString(argv[6]) : envOr("SFTP_RESUME_MB", "64")).toULongLong());

    const quint64 total = sizeMb * 1024ull * 1024ull;
    const quint64 prefix = total / 3; // resume point (not on a MB boundary)

    QTemporaryDir tmp;
    const QString source = tmp.filePath("source.bin");
    const QString localDl = tmp.filePath("download.bin");
    const QString localUpCheck = tmp.filePath("upcheck.bin");
    const QString remoteRef = joinRemote(remoteDir, ".termsync-resume-ref.bin");
    const QString remoteUp = joinRemote(remoteDir, ".termsync-resume-up.bin");

    if (!writeFile(source, total)) {
        std::fprintf(stderr, "[setup] could not create source\n");
        return 1;
    }
    const QByteArray want = sha(source);

    termsync::transfer::SftpFileEngine sftp;
    if (!sftp.connectToHost(p, [](const QString &) { return true; })) {
        std::fprintf(stderr, "[connect] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    sftp.removeFile(remoteRef);
    sftp.removeFile(remoteUp);

    // Reference upload (full) so downloads have something to fetch.
    if (!sftp.uploadFile(source, remoteRef)) {
        std::fprintf(stderr, "[ref upload] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }

    // --- Download resume: seed local with a valid prefix, resume the rest. ---
    if (!writePrefix(source, localDl, prefix)) {
        std::fprintf(stderr, "[dl seed] failed\n");
        return 1;
    }
    sftp.setResume(true);
    const bool dlOk = sftp.downloadFile(remoteRef, localDl);
    sftp.setResume(false);
    if (!dlOk) {
        std::fprintf(stderr, "[dl resume] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    if (sha(localDl) != want) {
        std::fprintf(stderr, "[dl resume] CHECKSUM MISMATCH\n");
        return 1;
    }
    std::fprintf(stderr, "[dl resume] OK (seeded %llu of %llu MiB)\n",
                 static_cast<unsigned long long>(prefix / (1024 * 1024)),
                 static_cast<unsigned long long>(sizeMb));

    // --- Upload resume: seed remote with a valid prefix, resume the rest. ---
    const QString srcPrefix = tmp.filePath("source-prefix.bin");
    if (!writePrefix(source, srcPrefix, prefix)) {
        std::fprintf(stderr, "[ul seed] failed\n");
        return 1;
    }
    if (!sftp.uploadFile(srcPrefix, remoteUp)) { // fresh, resume off -> remote has `prefix` bytes
        std::fprintf(stderr, "[ul seed upload] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    sftp.setResume(true);
    const bool ulOk = sftp.uploadFile(source, remoteUp); // should resume from `prefix`
    sftp.setResume(false);
    if (!ulOk) {
        std::fprintf(stderr, "[ul resume] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    quint64 remoteSize = 0;
    if (!sftp.statSize(remoteUp, &remoteSize) || remoteSize != total) {
        std::fprintf(stderr, "[ul resume] SIZE MISMATCH expected=%llu actual=%llu\n",
                     static_cast<unsigned long long>(total),
                     static_cast<unsigned long long>(remoteSize));
        return 1;
    }
    if (!sftp.downloadFile(remoteUp, localUpCheck) || sha(localUpCheck) != want) {
        std::fprintf(stderr, "[ul resume] CHECKSUM MISMATCH\n");
        return 1;
    }
    std::fprintf(stderr, "[ul resume] OK\n");

    sftp.removeFile(remoteRef);
    sftp.removeFile(remoteUp);
    std::printf("RESUME PASS\n");
    return 0;
}
