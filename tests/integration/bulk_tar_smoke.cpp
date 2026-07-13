// End-to-end check for the bulk tar folder-transfer path against a real server:
//   - runCommandFromFile  (upload: local tar -> remote `tar xf -`)
//   - runCommandToFile     (download: remote `tar cf -` -> local file)
//   - archive::create/extractTarFile round-trip through the wire
//
// Mirrors exactly the command strings SftpSession builds. Creates a temp tree
// with many small files, uploads it as one stream, downloads it back as one
// stream, and verifies every byte survives.
//
// Usage: bulk_tar_smoke <host> <port> <user> <password> <remote-dir>
// Exit 0 = passed.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "archive/TarArchive.h"
#include "sftp/SftpFileEngine.h"

using termsync::transfer::SftpFileEngine;

namespace {

QString envOr(const char *k, const QString &d)
{
    const QByteArray v = qgetenv(k);
    return v.isEmpty() ? d : QString::fromUtf8(v);
}

bool writeLocal(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           f.write(bytes) == bytes.size() && f.flush();
}

QByteArray readLocal(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int fail(const char *what, const QString &d = {})
{
    std::fprintf(stderr, "[FAIL] %s %s\n", what, d.toUtf8().constData());
    return 1;
}

QString shQuote(const QString &s)
{
    QString e = s;
    e.replace('\'', "'\\''");
    return "'" + e + "'";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    termsync::core::SshConnectionParams p;
    p.host = argc > 1 ? argv[1] : envOr("SSH_HOST", "");
    p.port = static_cast<quint16>(
        (argc > 2 ? QString(argv[2]) : envOr("SSH_PORT", "22")).toInt());
    p.username = argc > 3 ? argv[3] : envOr("SSH_USER", "");
    p.password = argc > 4 ? argv[4] : envOr("SSH_PASS", "");
    const QString remoteBase = argc > 5 ? argv[5] : envOr("SFTP_DIR", ".");

    SftpFileEngine sftp;
    if (!sftp.connectToHost(p, [](const QString &) { return true; }))
        return fail("connect", sftp.lastError());

    QTemporaryDir tmp;
    const QString srcDir = tmp.filePath("payload");
    const int kFiles = 200;
    for (int i = 0; i < kFiles; ++i) {
        const QString sub = QStringLiteral("sub%1").arg(i % 8);
        if (!writeLocal(srcDir + "/" + sub + QStringLiteral("/f%1.txt").arg(i),
                        QStringLiteral("content-of-file-%1\n").arg(i).toUtf8()))
            return fail("write local", QString::number(i));
    }
    writeLocal(srcDir + "/deep/a/b/c/leaf.bin", QByteArray(4096, '\x7e'));

    // --- Bulk upload: tar locally, stream into remote `tar xf -` ---
    const QString upArchive = tmp.filePath("up.tar");
    QString err;
    if (!termsync::transfer::archive::createTarFile(srcDir, "bulkpayload",
                                                    upArchive, false, &err))
        return fail("createTar", err);

    const QString remoteParent = remoteBase; // unpack "bulkpayload/" under here
    sftp.removeDirectory(remoteBase + "/bulkpayload"); // best-effort clean
    const QString upCmd = QStringLiteral("sh -c \"cd %1 && tar xf -\"")
                              .arg(shQuote(remoteParent));
    int ec = 0;
    if (!sftp.runCommandFromFile(upCmd, upArchive, {}, nullptr, &ec))
        return fail("runCommandFromFile", sftp.lastError());
    std::fprintf(stderr, "[ok] uploaded %d files as one tar stream\n", kFiles);

    // --- Bulk download: remote `tar cf -` streamed to a local archive ---
    const QString dlArchive = tmp.filePath("dl.tar");
    const QString dlCmd =
        QStringLiteral("sh -c \"cd %1 && tar cf - %2\"")
            .arg(shQuote(remoteParent), shQuote(QStringLiteral("bulkpayload")));
    if (!sftp.runCommandToFile(dlCmd, dlArchive, {}, nullptr, &ec))
        return fail("runCommandToFile", sftp.lastError());

    const QString outDir = tmp.filePath("out");
    if (!termsync::transfer::archive::extractTarFile(dlArchive, outDir, &err))
        return fail("extractTar", err);

    // --- Verify every file round-tripped ---
    const QString got = outDir + "/bulkpayload";
    for (int i = 0; i < kFiles; ++i) {
        const QString sub = QStringLiteral("sub%1").arg(i % 8);
        const QString path = got + "/" + sub + QStringLiteral("/f%1.txt").arg(i);
        if (readLocal(path) != QStringLiteral("content-of-file-%1\n").arg(i).toUtf8())
            return fail("content mismatch", path);
    }
    if (readLocal(got + "/deep/a/b/c/leaf.bin") != QByteArray(4096, '\x7e'))
        return fail("binary mismatch");

    // Clean up the remote tree.
    sftp.runCommand(QStringLiteral("rm -rf %1")
                        .arg(shQuote(remoteBase + "/bulkpayload")),
                    nullptr, &ec);

    std::fprintf(stderr, "[ok] round-tripped %d files + nested binary via tar\n",
                 kFiles);
    std::fprintf(stderr, "[PASS] bulk_tar_smoke\n");
    return 0;
}
