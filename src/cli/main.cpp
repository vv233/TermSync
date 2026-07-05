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
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <memory>
#include <cstdio>
#ifndef _WIN32
#  include <sys/time.h>
#endif

#include "FileEngine.h"
#include "ftp/FtpFileEngine.h"
#include "schedule/JobScheduler.h"
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

// One-way sync between a local dir and a remote dir, preserving mtime so repeat
// runs are no-ops. Returns 0 on success. Shared by the `sync` command and jobs.
int runSync(FileEngine &engine, const QString &local, const QString &remote,
            bool down, bool del, bool dryRun)
{
    using namespace transfer::sync;
    Listing localList = enumerateLocalTree(local);
    Listing remoteList;
    if (!engine.listRecursive(remote, &remoteList))
        return fail("list remote: " + engine.lastError());
    DirectoryDiffer differ(down ? Direction::RemoteToLocal : Direction::LocalToRemote,
                           CompareStrategy::MtimeSize, ConflictPolicy::NewerWins, del);
    const QVector<SyncAction> actions = differ.diff(localList, remoteList);
    int changes = 0;
    for (const auto &a : actions)
        if (a.type != ActionType::Skip) ++changes;
    std::fprintf(stderr, "sync: %d change(s)%s\n", changes, dryRun ? " (dry-run)" : "");
    if (dryRun) {
        for (const auto &a : actions)
            if (a.type != ActionType::Skip)
                std::printf("%-14s %s\n", actionName(a.type), a.relativePath.toUtf8().constData());
        return 0;
    }
    SyncCallbacks cb;
    cb.upload = [&](const QString &rel) {
        if (!engine.uploadFile(joinPath(local, rel), joinPath(remote, rel)))
            return false;
        if (localList.contains(rel))
            engine.setModifiedTime(joinPath(remote, rel), localList[rel].mtime);
        return true; };
    cb.download = [&](const QString &rel) {
        const QString dst = joinPath(local, rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (!engine.downloadFile(joinPath(remote, rel), dst))
            return false;
        if (remoteList.contains(rel))
            setLocalMtime(dst, remoteList[rel].mtime);
        return true; };
    cb.mkdirRemote = [&](const QString &rel) { return engine.makeDirectory(joinPath(remote, rel)); };
    cb.mkdirLocal = [&](const QString &rel) { return QDir().mkpath(joinPath(local, rel)); };
    cb.deleteRemote = [&](const QString &rel) { return engine.removeFile(joinPath(remote, rel)); };
    cb.deleteLocal = [&](const QString &rel) { return QFile::remove(joinPath(local, rel)); };
    const SyncReport rep = executeSync(actions, cb);
    std::fprintf(stderr, "uploaded=%d downloaded=%d deleted=%d failed=%d\n",
                 rep.uploaded, rep.downloaded, rep.deleted, rep.failed);
    for (const QString &e : rep.errors)
        std::fprintf(stderr, "  ! %s\n", e.toUtf8().constData());
    return rep.failed == 0 ? 0 : 1;
}

// Build + connect an engine for a scheduled job and run its action headlessly.
int executeJob(const transfer::schedule::ScheduledJob &j)
{
    std::unique_ptr<FileEngine> engine;
    if (j.protocol == "ftp" || j.protocol == "ftps") {
        auto ftp = std::make_unique<transfer::FtpFileEngine>();
        ftp->setExplicitTls(j.protocol == "ftps");
        engine = std::move(ftp);
    } else {
        engine = std::make_unique<transfer::SftpFileEngine>();
    }
    if (j.resume) engine->setResume(true);
    if (j.relentless) engine->setRelentless(true);
    if (j.ascii) engine->setAsciiMode(true);
    if (j.preservePerms) engine->setPreservePermissions(true);
    if (j.throttleKbps > 0)
        engine->setRateLimitBytesPerSec(static_cast<quint64>(j.throttleKbps) * 1024);

    core::SshConnectionParams cp;
    cp.host = j.host;
    cp.port = static_cast<quint16>(j.port ? j.port : (j.protocol == "sftp" ? 22 : 21));
    cp.username = j.user;
    if (!j.keyPath.isEmpty()) {
        cp.authMethod = core::SshAuthMethod::PublicKey;
        cp.privateKeyPath = j.keyPath;
        cp.passphrase = j.passphrase;
    } else {
        cp.authMethod = core::SshAuthMethod::Password;
        cp.password = j.password;
    }
    if (!engine->connectToHost(cp, [](const QString &) { return true; }))
        return fail("job '" + j.name + "' connect: " + engine->lastError());

    if (j.command == "sync")
        return runSync(*engine, j.localPath, j.remotePath, j.down, j.deleteOrphans, false);
    if (j.command == "put") {
        if (j.recursive && QFileInfo(j.localPath).isDir())
            return putTree(*engine, j.localPath, j.remotePath) ? 0 : fail(engine->lastError());
        return engine->uploadFile(j.localPath, j.remotePath) ? 0 : fail(engine->lastError());
    }
    if (j.command == "get") {
        if (j.recursive)
            return getTree(*engine, j.remotePath, j.localPath) ? 0 : fail(engine->lastError());
        return engine->downloadFile(j.remotePath, j.localPath) ? 0 : fail(engine->lastError());
    }
    return fail("job '" + j.name + "': unknown command " + j.command);
}

QString defaultJobsFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/jobs.json";
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
    QCommandLineOption oJobsFile = opt({"jobs-file"}, "Scheduler jobs file.", "path");
    QCommandLineOption oId = opt({"id"}, "schedule: job id.", "id");
    QCommandLineOption oName = opt({"name"}, "schedule: job name.", "name");
    QCommandLineOption oInterval = opt({"interval"}, "schedule: repeat every N seconds (0=once).", "secs");
    QCommandLineOption oPoll = opt({"poll"}, "schedule daemon: poll interval seconds.", "secs", "60");
    p.process(app);

    const QStringList pos = p.positionalArguments();
    if (pos.isEmpty())
        return fail("no command (try --help)");
    const QString command = pos.first();
    const QStringList args = pos.mid(1);

    // --- scheduler: manage/run persisted jobs (no global connection needed) ---
    if (command == "schedule") {
        using namespace transfer::schedule;
        const QString jobsFile = p.isSet(oJobsFile) ? p.value(oJobsFile) : defaultJobsFile();
        JobScheduler sched;
        if (!sched.load(jobsFile))
            return fail("could not read jobs file: " + jobsFile);
        const QString sub = args.value(0);

        if (sub == "list") {
            std::printf("jobs (%s):\n", jobsFile.toUtf8().constData());
            for (const ScheduledJob &j : sched.jobs())
                std::printf("  %-12s %-6s %s -> %s  every %llds  %s\n",
                            j.id.toUtf8().constData(), j.command.toUtf8().constData(),
                            j.localPath.toUtf8().constData(), j.remotePath.toUtf8().constData(),
                            static_cast<long long>(j.intervalSecs),
                            j.enabled ? "" : "(disabled)");
            return 0;
        }
        if (sub == "remove") {
            if (args.size() < 2) return fail("schedule remove <id>");
            if (!sched.remove(args[1])) return fail("no such job: " + args[1]);
            return sched.save(jobsFile) ? 0 : fail("save failed");
        }
        if (sub == "add") {
            // schedule add <put|get|sync> <local> <remote>
            if (args.size() < 4) return fail("schedule add <put|get|sync> <local> <remote>");
            ScheduledJob j;
            j.id = p.isSet(oId) ? p.value(oId)
                                : QString::number(QDateTime::currentSecsSinceEpoch());
            j.name = p.isSet(oName) ? p.value(oName) : j.id;
            j.protocol = p.value(oProto).toLower();
            j.host = p.value(oHost);
            j.port = p.value(oPort).toInt();
            j.user = p.value(oUser);
            j.password = p.value(oPass);
            j.keyPath = p.value(oKey);
            j.passphrase = p.value(oPhrase);
            j.command = args[1];
            j.localPath = args[2];
            j.remotePath = args[3];
            j.down = p.isSet(oDown);
            j.recursive = p.isSet(oRecursive);
            j.deleteOrphans = p.isSet(oDelete);
            j.resume = p.isSet(oResume);
            j.relentless = p.isSet(oRelentless);
            j.ascii = p.isSet(oAscii);
            j.preservePerms = p.isSet(oPreserve);
            j.throttleKbps = p.value(oThrottle).toLongLong();
            j.intervalSecs = p.value(oInterval).toLongLong();
            if (j.host.isEmpty() || j.user.isEmpty())
                return fail("--host and --user are required");
            sched.add(j);
            if (!sched.save(jobsFile)) return fail("save failed");
            std::printf("added job %s\n", j.id.toUtf8().constData());
            return 0;
        }
        if (sub == "run-due" || sub == "daemon") {
            const int poll = qMax(1, p.value(oPoll).toInt());
            for (;;) {
                JobScheduler s;
                s.load(jobsFile);
                const qint64 now = QDateTime::currentSecsSinceEpoch();
                const QVector<ScheduledJob> due = s.dueJobs(now);
                for (const ScheduledJob &j : due) {
                    std::fprintf(stderr, "== running job '%s' ==\n", j.name.toUtf8().constData());
                    executeJob(j);
                    s.markRan(j.id, QDateTime::currentSecsSinceEpoch());
                }
                if (!due.isEmpty())
                    s.save(jobsFile);
                if (sub == "run-due")
                    return 0;
                QThread::sleep(static_cast<unsigned long>(poll)); // daemon: keep polling
            }
        }
        return fail("schedule: unknown subcommand (add|list|remove|run-due|daemon)");
    }

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
        return runSync(*engine, args[0], args[1], p.isSet(oDown), p.isSet(oDelete), p.isSet(oDryRun));
    }

    return fail("unknown command: " + command);
}
