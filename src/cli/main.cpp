// termsync-cli — headless SFTP/FTP transfer & sync tool (SecureFX SFXCL-style).
//
//   termsync-cli --host H --user U [--password P | --key FILE] <command> [args]
//
// Commands: ls <remote> | get <remote> <local> | put <local> <remote> |
//           mkdir <remote> | rm <remote> | mv <from> <to> | sync <local> <remote>
//
// Reuses the same FileEngine + sync engine as the GUI, so behaviour matches.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <memory>
#include <cstdio>
#ifndef _WIN32
#  include <sys/time.h>
#endif

#include "FileEngine.h"
#include "ftp/FtpFileEngine.h"
#include "sftp/SftpFileEngine.h"
#include "sync/DirectoryDiffer.h"
#include "sync/SyncEngine.h"

using namespace termsync;
using transfer::FileEngine;

namespace {

int fail(const QString &msg)
{
    std::fprintf(stderr, "error: %s\n", msg.toUtf8().constData());
    return 1;
}

QString joinPath(const QString &base, const QString &rel)
{
    if (base.isEmpty() || base == ".")
        return rel;
    return base.endsWith('/') ? base + rel : base + '/' + rel;
}

// Stamp a local file's mtime (keeps remote->local sync idempotent).
void setLocalMtime(const QString &path, qint64 secs)
{
#ifndef _WIN32
    struct timeval tv[2];
    tv[0].tv_sec = tv[1].tv_sec = static_cast<time_t>(secs);
    tv[0].tv_usec = tv[1].tv_usec = 0;
    ::utimes(path.toLocal8Bit().constData(), tv);
#else
    Q_UNUSED(path);
    Q_UNUSED(secs);
#endif
}

void printProgress(const char *label, quint64 done, quint64 total)
{
    static int last = -1;
    const int pct = total ? static_cast<int>((done * 100) / total) : 0;
    if (pct != last) {
        last = pct;
        std::fprintf(stderr, "\r%s %3d%%", label, pct);
        if (done >= total && total)
            std::fprintf(stderr, "\n");
        std::fflush(stderr);
    }
}

// Recursively upload a local directory tree to remoteRoot.
bool putTree(FileEngine &e, const QString &localRoot, const QString &remoteRoot)
{
    const transfer::sync::Listing tree = transfer::sync::enumerateLocalTree(localRoot);
    e.makeDirectory(remoteRoot); // best effort
    for (auto it = tree.constBegin(); it != tree.constEnd(); ++it) {
        const QString remote = joinPath(remoteRoot, it.key());
        if (it.value().isDir) {
            e.makeDirectory(remote); // ignore "already exists"
            continue;
        }
        const QString local = joinPath(localRoot, it.key());
        std::fprintf(stderr, "put %s\n", it.key().toUtf8().constData());
        if (!e.uploadFile(local, remote,
                          [](quint64 d, quint64 t) { printProgress("  ", d, t); }))
            return false;
    }
    return true;
}

// Recursively download a remote tree to localRoot.
bool getTree(FileEngine &e, const QString &remoteRoot, const QString &localRoot)
{
    transfer::sync::Listing tree;
    if (!e.listRecursive(remoteRoot, &tree))
        return false;
    QDir().mkpath(localRoot);
    for (auto it = tree.constBegin(); it != tree.constEnd(); ++it) {
        const QString local = joinPath(localRoot, it.key());
        if (it.value().isDir) {
            QDir().mkpath(local);
            continue;
        }
        QDir().mkpath(QFileInfo(local).absolutePath());
        std::fprintf(stderr, "get %s\n", it.key().toUtf8().constData());
        if (!e.downloadFile(joinPath(remoteRoot, it.key()), local,
                            [](quint64 d, quint64 t) { printProgress("  ", d, t); }))
            return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("termsync-cli");
    QCoreApplication::setApplicationVersion("0.1");

    QCommandLineParser p;
    p.setApplicationDescription(
        "Headless SFTP/FTP transfer & sync.\n"
        "Commands: ls <remote> | get <remote> <local> | put <local> <remote> |\n"
        "          mkdir <remote> | rm <remote> | mv <from> <to> | sync <local> <remote>");
    p.addHelpOption();
    p.addVersionOption();
    p.addPositionalArgument("command", "ls|get|put|mkdir|rm|mv|sync");
    p.addPositionalArgument("args", "command arguments", "[args...]");

    const auto opt = [&](const QStringList &names, const QString &desc,
                         const QString &valueName = {}, const QString &def = {}) {
        QCommandLineOption o(names, desc, valueName, def);
        p.addOption(o);
        return o;
    };
    QCommandLineOption oHost = opt({"host", "H"}, "Server host.", "host");
    QCommandLineOption oPort = opt({"port"}, "Port (default 22 / 21).", "port");
    QCommandLineOption oUser = opt({"user", "u"}, "Username.", "user");
    QCommandLineOption oPass = opt({"password", "P"}, "Password.", "password");
    QCommandLineOption oKey = opt({"key", "i"}, "Private key file.", "file");
    QCommandLineOption oPhrase = opt({"passphrase"}, "Key passphrase.", "phrase");
    QCommandLineOption oProto = opt({"protocol"}, "sftp|ftp|ftps (default sftp).", "proto", "sftp");
    QCommandLineOption oRecursive = opt({"recursive", "r"}, "Recurse into directories.");
    QCommandLineOption oResume = opt({"resume"}, "Resume partial transfers.");
    QCommandLineOption oRelentless = opt({"relentless"}, "Reconnect + continue on drops.");
    QCommandLineOption oThrottle = opt({"throttle-kbps"}, "Cap rate (KiB/s).", "kbps");
    QCommandLineOption oAscii = opt({"ascii"}, "Text mode (translate line endings).");
    QCommandLineOption oPreserve = opt({"preserve-perms"}, "Preserve unix perms on upload.");
    QCommandLineOption oOverwrite = opt({"overwrite"}, "skip|overwrite|resume (default overwrite).",
                                        "policy", "overwrite");
    QCommandLineOption oDown = opt({"down"}, "sync: remote -> local (default local -> remote).");
    QCommandLineOption oDelete = opt({"delete"}, "sync: delete orphaned files on the target.");
    QCommandLineOption oDryRun = opt({"dry-run"}, "sync: print actions, do not transfer.");
    p.process(app);

    const QStringList pos = p.positionalArguments();
    if (pos.isEmpty())
        return fail("no command (try --help)");
    const QString command = pos.first();
    const QStringList args = pos.mid(1);

    // --- connection parameters ---
    const QString proto = p.value(oProto).toLower();
    core::SshConnectionParams cp;
    cp.host = p.value(oHost);
    cp.port = static_cast<quint16>(p.value(oPort).toInt());
    if (cp.port == 0)
        cp.port = (proto == "sftp") ? 22 : 21;
    cp.username = p.value(oUser);
    if (p.isSet(oKey)) {
        cp.authMethod = core::SshAuthMethod::PublicKey;
        cp.privateKeyPath = p.value(oKey);
        cp.passphrase = p.value(oPhrase);
    } else {
        cp.authMethod = core::SshAuthMethod::Password;
        cp.password = p.value(oPass);
    }
    if (cp.host.isEmpty() || cp.username.isEmpty())
        return fail("--host and --user are required");

    // --- build engine ---
    std::unique_ptr<FileEngine> engine;
    if (proto == "ftp" || proto == "ftps") {
        auto ftp = std::make_unique<transfer::FtpFileEngine>();
        ftp->setExplicitTls(proto == "ftps");
        engine = std::move(ftp);
    } else if (proto == "sftp") {
        engine = std::make_unique<transfer::SftpFileEngine>();
    } else {
        return fail("unknown protocol: " + proto);
    }

    // --- apply transfer options ---
    if (p.isSet(oResume))
        engine->setResume(true);
    if (p.isSet(oRelentless))
        engine->setRelentless(true);
    if (p.isSet(oAscii))
        engine->setAsciiMode(true);
    if (p.isSet(oPreserve))
        engine->setPreservePermissions(true);
    if (p.isSet(oThrottle))
        engine->setRateLimitBytesPerSec(static_cast<quint64>(p.value(oThrottle).toLongLong()) * 1024);
    const QString overwrite = p.value(oOverwrite).toLower();
    if (overwrite == "resume")
        engine->setResume(true);

    if (!engine->connectToHost(cp, [](const QString &fp) {
            std::fprintf(stderr, "host key: %s\n", fp.toUtf8().constData());
            return true; // non-interactive: trust on first use
        }))
        return fail("connect failed: " + engine->lastError());

    auto need = [&](int n) -> bool {
        if (args.size() < n) { fail("command needs " + QString::number(n) + " argument(s)"); return false; }
        return true;
    };
    auto existsRemote = [&](const QString &path) {
        quint64 s = 0; return engine->statSize(path, &s);
    };

    // --- dispatch ---
    if (command == "ls") {
        if (!need(1)) return 2;
        QVector<transfer::FileEntry> entries;
        if (!engine->listDirectory(args[0], &entries))
            return fail(engine->lastError());
        for (const auto &e : entries)
            std::printf("%s %10llu  %s\n", e.isDirectory ? "d" : "-",
                        static_cast<unsigned long long>(e.size), e.name.toUtf8().constData());
        return 0;
    }
    if (command == "mkdir")
        return need(1) && engine->makeDirectory(args[0]) ? 0 : fail(engine->lastError());
    if (command == "rm")
        return need(1) && engine->removeFile(args[0]) ? 0 : fail(engine->lastError());
    if (command == "mv")
        return need(2) && engine->moveFile(args[0], args[1]) ? 0 : fail(engine->lastError());

    if (command == "put") {
        if (!need(2)) return 2;
        const QString local = args[0], remote = args[1];
        if (p.isSet(oRecursive) && QFileInfo(local).isDir())
            return putTree(*engine, local, remote) ? 0 : fail(engine->lastError());
        if (overwrite == "skip" && existsRemote(remote)) {
            std::fprintf(stderr, "skip (exists): %s\n", remote.toUtf8().constData());
            return 0;
        }
        return engine->uploadFile(local, remote,
                   [](quint64 d, quint64 t) { printProgress("put", d, t); })
                   ? 0 : fail(engine->lastError());
    }
    if (command == "get") {
        if (!need(2)) return 2;
        const QString remote = args[0], local = args[1];
        if (p.isSet(oRecursive))
            return getTree(*engine, remote, local) ? 0 : fail(engine->lastError());
        if (overwrite == "skip" && QFileInfo::exists(local)) {
            std::fprintf(stderr, "skip (exists): %s\n", local.toUtf8().constData());
            return 0;
        }
        return engine->downloadFile(remote, local,
                   [](quint64 d, quint64 t) { printProgress("get", d, t); })
                   ? 0 : fail(engine->lastError());
    }

    if (command == "sync") {
        if (!need(2)) return 2;
        const QString local = args[0], remote = args[1];
        const bool down = p.isSet(oDown);
        using namespace transfer::sync;
        Listing localList = enumerateLocalTree(local);
        Listing remoteList;
        if (!engine->listRecursive(remote, &remoteList))
            return fail("list remote: " + engine->lastError());
        DirectoryDiffer differ(down ? Direction::RemoteToLocal : Direction::LocalToRemote,
                               CompareStrategy::MtimeSize, ConflictPolicy::NewerWins,
                               p.isSet(oDelete));
        const QVector<SyncAction> actions = differ.diff(localList, remoteList);
        int changes = 0;
        for (const auto &a : actions)
            if (a.type != ActionType::Skip) ++changes;
        std::fprintf(stderr, "sync: %d change(s)%s\n", changes, p.isSet(oDryRun) ? " (dry-run)" : "");
        if (p.isSet(oDryRun)) {
            for (const auto &a : actions)
                if (a.type != ActionType::Skip)
                    std::printf("%-14s %s\n", actionName(a.type), a.relativePath.toUtf8().constData());
            return 0;
        }
        SyncCallbacks cb;
        cb.upload = [&](const QString &rel) {
            if (!engine->uploadFile(joinPath(local, rel), joinPath(remote, rel)))
                return false;
            if (localList.contains(rel)) // preserve mtime so re-sync is a no-op
                engine->setModifiedTime(joinPath(remote, rel), localList[rel].mtime);
            return true; };
        cb.download = [&](const QString &rel) {
            const QString dst = joinPath(local, rel);
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!engine->downloadFile(joinPath(remote, rel), dst))
                return false;
            if (remoteList.contains(rel))
                setLocalMtime(dst, remoteList[rel].mtime);
            return true; };
        cb.mkdirRemote = [&](const QString &rel) { return engine->makeDirectory(joinPath(remote, rel)); };
        cb.mkdirLocal = [&](const QString &rel) { return QDir().mkpath(joinPath(local, rel)); };
        cb.deleteRemote = [&](const QString &rel) { return engine->removeFile(joinPath(remote, rel)); };
        cb.deleteLocal = [&](const QString &rel) { return QFile::remove(joinPath(local, rel)); };
        const SyncReport rep = executeSync(actions, cb);
        std::fprintf(stderr, "uploaded=%d downloaded=%d deleted=%d failed=%d\n",
                     rep.uploaded, rep.downloaded, rep.deleted, rep.failed);
        for (const QString &e : rep.errors)
            std::fprintf(stderr, "  ! %s\n", e.toUtf8().constData());
        return rep.failed == 0 ? 0 : 1;
    }

    return fail("unknown command: " + command);
}
