// Manual SFTP throughput benchmark for transfer::SftpFileEngine.
//
// Usage:
//   sftp_bench <host> <port> <user> <password> <remote-dir> <size-mb>
// Example:
//   sftp_bench 127.0.0.1 22222 termsync termsync upload 1024

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <cstring>
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

bool writeTestFile(const QString &path, quint64 bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QByteArray chunk(4 * 1024 * 1024, Qt::Uninitialized);
    quint32 x = 0x12345678u;
    for (qsizetype i = 0; i < chunk.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        chunk[i] = static_cast<char>(x >> 24);
    }

    quint64 written = 0;
    while (written < bytes) {
        const qint64 want = static_cast<qint64>(
            qMin<quint64>(static_cast<quint64>(chunk.size()), bytes - written));
        if (file.write(chunk.constData(), want) != want)
            return false;
        written += static_cast<quint64>(want);
    }
    return file.flush();
}

bool filesEqual(const QString &leftPath, const QString &rightPath)
{
    QFile left(leftPath);
    QFile right(rightPath);
    if (!left.open(QIODevice::ReadOnly) || !right.open(QIODevice::ReadOnly))
        return false;
    if (left.size() != right.size())
        return false;

    QByteArray leftChunk(4 * 1024 * 1024, Qt::Uninitialized);
    QByteArray rightChunk(4 * 1024 * 1024, Qt::Uninitialized);
    while (!left.atEnd()) {
        const qint64 leftRead = left.read(leftChunk.data(), leftChunk.size());
        const qint64 rightRead = right.read(rightChunk.data(), rightChunk.size());
        if (leftRead != rightRead)
            return false;
        if (std::memcmp(leftChunk.constData(), rightChunk.constData(),
                        static_cast<size_t>(leftRead)) != 0)
            return false;
    }
    return true;
}

double mibPerSecond(quint64 bytes, qint64 elapsedMs)
{
    if (elapsedMs <= 0)
        return 0.0;
    return (static_cast<double>(bytes) / (1024.0 * 1024.0)) /
           (static_cast<double>(elapsedMs) / 1000.0);
}

void printProgress(const char *label, quint64 done, quint64 total)
{
    if (!total)
        return;
    const int pct = static_cast<int>((done * 100) / total);
    static int lastUpload = -1;
    static int lastDownload = -1;
    int &last = label[0] == 'u' ? lastUpload : lastDownload;
    if (pct >= last + 10 || pct == 100) {
        last = pct;
        std::fprintf(stderr, "[%s] %d%%\n", label, pct);
    }
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
        (argc > 6 ? QString(argv[6]) : envOr("SFTP_BENCH_MB", "512")).toULongLong());

    if (p.host.isEmpty() || p.username.isEmpty() || sizeMb == 0) {
        std::fprintf(stderr,
                     "usage: sftp_bench <host> <port> <user> <password> <remote-dir> <size-mb>\n");
        return 2;
    }

    const quint64 bytes = sizeMb * 1024ull * 1024ull;
    QTemporaryDir tmp;
    const QString localUpload = tmp.filePath("termsync-sftp-bench-upload.bin");
    const QString localDownload = tmp.filePath("termsync-sftp-bench-download.bin");
    const QString remoteFile = joinRemote(remoteDir, ".termsync-sftp-bench.bin");

    std::fprintf(stderr, "[generate] %llu MiB at %s\n",
                 static_cast<unsigned long long>(sizeMb),
                 localUpload.toUtf8().constData());
    QElapsedTimer timer;
    timer.start();
    if (!writeTestFile(localUpload, bytes)) {
        std::fprintf(stderr, "[generate failed]\n");
        return 1;
    }
    std::fprintf(stderr, "[generate] %.2fs\n", timer.elapsed() / 1000.0);

    termsync::transfer::SftpFileEngine sftp;
    if (qgetenv("SFTP_RELENTLESS").toInt() != 0) {
        sftp.setRelentless(true);
        std::fprintf(stderr, "[relentless] enabled\n");
    }
    if (!sftp.connectToHost(p, [](const QString &) { return true; })) {
        std::fprintf(stderr, "[connect failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    std::fprintf(stderr, "[hostkey] %s\n", sftp.hostKeyFingerprint().toUtf8().constData());

    sftp.removeFile(remoteFile); // best effort cleanup from a previous run

    timer.restart();
    if (!sftp.uploadFile(localUpload, remoteFile, [](quint64 done, quint64 total) {
            printProgress("upload", done, total);
        })) {
        std::fprintf(stderr, "[upload failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    const qint64 uploadMs = timer.elapsed();

    quint64 remoteSize = 0;
    if (!sftp.statSize(remoteFile, &remoteSize) || remoteSize != bytes) {
        std::fprintf(stderr, "[remote size mismatch] expected=%llu actual=%llu\n",
                     static_cast<unsigned long long>(bytes),
                     static_cast<unsigned long long>(remoteSize));
        return 1;
    }

    timer.restart();
    if (!sftp.downloadFile(remoteFile, localDownload, [](quint64 done, quint64 total) {
            printProgress("download", done, total);
        })) {
        std::fprintf(stderr, "[download failed] %s\n", sftp.lastError().toUtf8().constData());
        return 1;
    }
    const qint64 downloadMs = timer.elapsed();

    QFile downloaded(localDownload);
    if (static_cast<quint64>(downloaded.size()) != bytes) {
        std::fprintf(stderr, "[download size mismatch]\n");
        return 1;
    }
    if (!filesEqual(localUpload, localDownload)) {
        std::fprintf(stderr, "[download content mismatch]\n");
        return 1;
    }

    std::printf("Size MiB,Upload Time,Upload Rate MiB/s,Download Time,Download Rate MiB/s\n");
    std::printf("%llu,%.3f,%.2f,%.3f,%.2f\n",
                static_cast<unsigned long long>(sizeMb),
                uploadMs / 1000.0,
                mibPerSecond(bytes, uploadMs),
                downloadMs / 1000.0,
                mibPerSecond(bytes, downloadMs));

    return 0;
}
