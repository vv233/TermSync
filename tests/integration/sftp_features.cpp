// Integration checks for M18e transfer-fidelity features:
//   - permission preservation on upload
//   - SFTP ASCII (line-ending) mode round-trip
//   - symlink create / readlink / realpath
//   - move (server-side rename)
//
// Usage: sftp_features <host> <port> <user> <password> <remote-dir>
// Exit 0 = all checks passed.

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QVector>
#include <cstdio>

#include "sftp/SftpFileEngine.h"

using termsync::transfer::SftpEntry;

namespace {

QString envOr(const char *k, const QString &d)
{
    const QByteArray v = qgetenv(k);
    return v.isEmpty() ? d : QString::fromUtf8(v);
}

QString join(const QString &dir, const QString &name)
{
    if (dir.isEmpty() || dir == ".") return name;
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

bool writeLocal(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           f.write(bytes) == bytes.size() && f.flush();
}

// Look up one entry's permission bits via a directory listing.
bool remotePerms(termsync::transfer::SftpFileEngine &sftp, const QString &dir,
                 const QString &name, quint32 *modeOut)
{
    QVector<SftpEntry> entries;
    if (!sftp.listDirectory(dir, &entries))
        return false;
    for (const SftpEntry &e : entries) {
        if (e.name == name) {
            *modeOut = e.permissions & 0777;
            return true;
        }
    }
    return false;
}

bool fail(const char *what, const QString &detail = {})
{
    std::fprintf(stderr, "[FAIL] %s %s\n", what, detail.toUtf8().constData());
    return false;
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
    const QString dir = argc > 5 ? argv[5] : envOr("SFTP_DIR", ".");

    QTemporaryDir tmp;
    termsync::transfer::SftpFileEngine sftp;
    if (!sftp.connectToHost(p, [](const QString &) { return true; }))
        return fail("connect", sftp.lastError()) ? 1 : 1;

    // --- Permission preservation ---
    const QString permLocal = tmp.filePath("perm.txt");
    if (!writeLocal(permLocal, "perm-test\n"))
        return 1;
    QFile::setPermissions(permLocal,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup); // 0640
    const QString permName = ".termsync-perm.txt";
    const QString permRemote = join(dir, permName);
    sftp.removeFile(permRemote);
    sftp.setPreservePermissions(true);
    if (!sftp.uploadFile(permLocal, permRemote))
        return fail("perm upload", sftp.lastError()) ? 1 : 1;
    sftp.setPreservePermissions(false);
    quint32 mode = 0;
    if (!remotePerms(sftp, dir, permName, &mode))
        return fail("perm stat") ? 1 : 1;
    if (mode != 0640u)
        return fail("perm mismatch", QString("got %1 want 0640").arg(mode, 0, 8)) ? 1 : 1;
    std::fprintf(stderr, "[ok] permission preservation (0640)\n");

    // --- ASCII line-ending round trip ---
    const QString asciiLocal = tmp.filePath("text.txt");
    if (!writeLocal(asciiLocal, "alpha\nbeta\ngamma\n")) // LF-only
        return 1;
    const QString asciiName = ".termsync-text.txt";
    const QString asciiRemote = join(dir, asciiName);
    sftp.removeFile(asciiRemote);
    sftp.setAsciiMode(true);
    if (!sftp.uploadFile(asciiLocal, asciiRemote))
        return fail("ascii upload", sftp.lastError()) ? 1 : 1;
    sftp.setAsciiMode(false);
    // Binary download must now show CRLFs.
    const QString rawDl = tmp.filePath("raw.txt");
    if (!sftp.downloadFile(asciiRemote, rawDl))
        return fail("ascii raw download", sftp.lastError()) ? 1 : 1;
    QFile raw(rawDl);
    if (!raw.open(QIODevice::ReadOnly))
        return fail("ascii raw reopen") ? 1 : 1;
    const QByteArray rawBytes = raw.readAll();
    if (rawBytes != QByteArray("alpha\r\nbeta\r\ngamma\r\n"))
        return fail("ascii CRLF not applied") ? 1 : 1;
    std::fprintf(stderr, "[ok] ASCII mode wrote CRLF line endings\n");

    // --- Symlink create / readlink / realpath ---
    const QString linkName = ".termsync-link";
    const QString linkRemote = join(dir, linkName);
    sftp.removeFile(linkRemote);
    if (!sftp.createSymlink(asciiName, linkRemote)) // link -> the text file
        return fail("symlink create", sftp.lastError()) ? 1 : 1;
    QString target;
    if (!sftp.readlink(linkRemote, &target) || target != asciiName)
        return fail("readlink", QString("got '%1'").arg(target)) ? 1 : 1;
    QString resolved;
    if (!sftp.realpath(dir, &resolved) || !resolved.startsWith('/'))
        return fail("realpath", sftp.lastError()) ? 1 : 1;
    std::fprintf(stderr, "[ok] symlink create/readlink/realpath (%s)\n",
                 resolved.toUtf8().constData());

    // --- Move (server-side rename) ---
    const QString moveName = ".termsync-moved.txt";
    const QString moveRemote = join(dir, moveName);
    sftp.removeFile(moveRemote);
    if (!sftp.moveFile(asciiRemote, moveRemote))
        return fail("move", sftp.lastError()) ? 1 : 1;
    quint64 sz = 0;
    if (sftp.statSize(asciiRemote, &sz))
        return fail("move: source still exists") ? 1 : 1;
    if (!sftp.statSize(moveRemote, &sz))
        return fail("move: destination missing") ? 1 : 1;
    std::fprintf(stderr, "[ok] move (rename)\n");

    // --- Keepalive: a configured session sends and reports the next-due time. ---
    termsync::transfer::SftpFileEngine ka;
    ka.setKeepaliveSeconds(5);
    if (!ka.connectToHost(p, [](const QString &) { return true; }))
        return fail("keepalive connect", ka.lastError()) ? 1 : 1;
    if (ka.keepalive() < 0)
        return fail("keepalive send") ? 1 : 1;
    std::fprintf(stderr, "[ok] keepalive send\n");

    // cleanup
    sftp.removeFile(permRemote);
    sftp.removeFile(moveRemote);
    sftp.removeFile(linkRemote);
    sftp.removeFile(rawDl);
    std::printf("FEATURES PASS\n");
    return 0;
}
