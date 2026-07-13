// Verifies the SFTP "sudo mode" commands work over a real SSH exec channel:
// credential validation, a root-only listing, and reading a root-only file.
// These are the exact command shapes SftpSession builds for sudo operations.
//
// Usage: sudo_smoke <host> <port> <user> <password> <sudo-password>
// Exit 0 = passed.

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "sftp/SftpFileEngine.h"

using termsync::transfer::SftpFileEngine;

namespace {
QString envOr(const char *k, const QString &d)
{
    const QByteArray v = qgetenv(k);
    return v.isEmpty() ? d : QString::fromUtf8(v);
}
QString shQuote(const QString &s)
{
    QString e = s;
    e.replace('\'', "'\\''");
    return "'" + e + "'";
}
int fail(const char *what, const QString &d = {})
{
    std::fprintf(stderr, "[FAIL] %s %s\n", what, d.toUtf8().constData());
    return 1;
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
    const QString sudoPw = argc > 5 ? argv[5] : p.password;

    SftpFileEngine sftp;
    if (!sftp.connectToHost(p, [](const QString &) { return true; }))
        return fail("connect", sftp.lastError());

    // 1) Validate sudo credentials.
    QString out;
    int ec = -1;
    if (!sftp.runCommand(
            QStringLiteral("echo %1 | sudo -S -p '' -v").arg(shQuote(sudoPw)), &out, &ec) ||
        ec != 0)
        return fail("sudo validate", QString("ec=%1 %2").arg(ec).arg(out));
    std::fprintf(stderr, "[ok] sudo credentials validated\n");

    // 2) Root-only listing (parseable by SftpSession::parseLsLine).
    out.clear();
    if (!sftp.runCommand(
            QStringLiteral("echo %1 | sudo -S -p '' ls -la --full-time /root 2>&1")
                .arg(shQuote(sudoPw)), &out, &ec) || ec != 0)
        return fail("sudo ls /root", out);
    if (!out.contains(QStringLiteral(".bashrc")))
        return fail("sudo ls missing expected entry", out.left(200));
    std::fprintf(stderr, "[ok] listed /root as root (%d lines)\n",
                 int(out.split('\n').size()));

    // 3) Read a root-only file via `sudo cat` streamed to a local file.
    QTemporaryDir tmp;
    const QString local = tmp.filePath("shadow.copy");
    if (!sftp.runCommandToFile(
            QStringLiteral("echo %1 | sudo -S -p '' cat /etc/shadow").arg(shQuote(sudoPw)),
            local, {}, nullptr, &ec) || ec != 0)
        return fail("sudo cat /etc/shadow", QString("ec=%1").arg(ec));
    QFile f(local);
    if (!f.open(QIODevice::ReadOnly) || f.size() == 0)
        return fail("shadow copy empty");
    std::fprintf(stderr, "[ok] read root-only /etc/shadow via sudo (%lld bytes)\n",
                 static_cast<long long>(f.size()));

    std::fprintf(stderr, "[PASS] sudo_smoke\n");
    return 0;
}
