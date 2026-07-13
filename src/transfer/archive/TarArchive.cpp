#include "archive/TarArchive.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace termsync::transfer::archive {

namespace {

constexpr int kBlock = 512;

// --- gzFile wrappers -------------------------------------------------------
// zlib transparently reads plain *and* gzip data (gzread falls back to a raw
// copy), and "wbT" writes uncompressed, so one abstraction covers both formats.

gzFile openGz(const QString &path, const char *mode)
{
#ifdef _WIN32
    return gzopen_w(reinterpret_cast<const wchar_t *>(path.utf16()), mode);
#else
    return gzopen(path.toLocal8Bit().constData(), mode);
#endif
}

// Writes the whole buffer; returns false on a short/failed write.
bool gzWriteAll(gzFile gz, const char *data, qint64 len)
{
    qint64 off = 0;
    while (off < len) {
        const unsigned chunk =
            static_cast<unsigned>(qMin<qint64>(len - off, 1 << 20));
        const int n = gzwrite(gz, data + off, chunk);
        if (n <= 0)
            return false;
        off += n;
    }
    return true;
}

// Reads exactly `len` bytes; returns bytes read (< len only at EOF, <0 on error).
qint64 gzReadFull(gzFile gz, char *buf, qint64 len)
{
    qint64 off = 0;
    while (off < len) {
        const unsigned chunk =
            static_cast<unsigned>(qMin<qint64>(len - off, 1 << 20));
        const int n = gzread(gz, buf + off, chunk);
        if (n < 0)
            return -1;
        if (n == 0)
            break; // EOF
        off += n;
    }
    return off;
}

// --- ustar header helpers --------------------------------------------------

void putOctal(char *field, int width, quint64 value)
{
    // width includes the trailing NUL: write (width-1) octal digits + NUL.
    std::snprintf(field, static_cast<size_t>(width), "%0*llo", width - 1,
                  static_cast<unsigned long long>(value));
}

void finishChecksum(char header[kBlock])
{
    std::memset(header + 148, ' ', 8); // checksum field counts as spaces
    unsigned sum = 0;
    for (int i = 0; i < kBlock; ++i)
        sum += static_cast<unsigned char>(header[i]);
    // 6 octal digits, NUL, space (the classic layout).
    std::snprintf(header + 148, 8, "%06o", sum);
    header[154] = '\0';
    header[155] = ' ';
}

// Fills a ustar header (name already fits in name[100]/prefix[155] or is a
// GNU-longlink placeholder). typeflag '0'=file, '5'=dir, 'L'=GNU long name.
QByteArray makeHeader(const QByteArray &name, const QByteArray &prefix,
                      quint64 size, quint32 mode, quint64 mtime, char typeflag)
{
    QByteArray h(kBlock, '\0');
    char *p = h.data();
    std::memcpy(p, name.constData(), qMin<int>(name.size(), 100));
    putOctal(p + 100, 8, mode & 07777);
    putOctal(p + 108, 8, 0); // uid
    putOctal(p + 116, 8, 0); // gid
    putOctal(p + 124, 12, size);
    putOctal(p + 136, 12, mtime);
    p[156] = typeflag;
    std::memcpy(p + 257, "ustar", 5); // magic (POSIX ustar\0)
    p[263] = '0';
    p[264] = '0'; // version "00"
    if (!prefix.isEmpty())
        std::memcpy(p + 345, prefix.constData(), qMin<int>(prefix.size(), 155));
    finishChecksum(p);
    return h;
}

// Emits a header for `arcName`, splitting into ustar prefix/name or, when that
// cannot fit, a preceding GNU '././@LongLink' block carrying the full name.
bool writeEntryHeader(gzFile gz, const QByteArray &arcName, quint64 size,
                      quint32 mode, quint64 mtime, char typeflag)
{
    QByteArray name = arcName;
    QByteArray prefix;
    if (name.size() > 100) {
        // Try to split at a '/' so name<=100 and prefix<=155 (ustar).
        int split = -1;
        for (int i = name.size() - 101; i < name.size(); ++i) {
            if (i > 0 && i <= 155 && name[i] == '/') {
                split = i;
                break;
            }
        }
        if (split > 0 && name.size() - split - 1 <= 100) {
            prefix = name.left(split);
            name = name.mid(split + 1);
        } else {
            // GNU long name: a placeholder header + the full name as data.
            QByteArray full = arcName;
            full.append('\0');
            const QByteArray lh = makeHeader("././@LongLink", QByteArray(),
                                             static_cast<quint64>(full.size()),
                                             0644, 0, 'L');
            if (!gzWriteAll(gz, lh.constData(), kBlock))
                return false;
            const int padded = ((full.size() + kBlock - 1) / kBlock) * kBlock;
            full.resize(padded, '\0');
            if (!gzWriteAll(gz, full.constData(), padded))
                return false;
            name = arcName.left(100);
            prefix.clear();
        }
    }
    const QByteArray h = makeHeader(name, prefix, size, mode, mtime, typeflag);
    return gzWriteAll(gz, h.constData(), kBlock);
}

quint64 parseOctal(const char *field, int len)
{
    // GNU base-256 encoding for large values: high bit of the first byte set.
    if (len > 0 && (static_cast<unsigned char>(field[0]) & 0x80)) {
        quint64 v = static_cast<unsigned char>(field[0]) & 0x7f;
        for (int i = 1; i < len; ++i)
            v = (v << 8) | static_cast<unsigned char>(field[i]);
        return v;
    }
    quint64 v = 0;
    for (int i = 0; i < len; ++i) {
        const char c = field[i];
        if (c >= '0' && c <= '7')
            v = (v << 3) | static_cast<quint64>(c - '0');
        else if (c == ' ' || c == '\0')
            continue;
    }
    return v;
}

// Rejects member paths that would escape destDir (absolute or containing "..").
QString safeJoin(const QString &destDir, const QString &member)
{
    QString rel = QDir::fromNativeSeparators(member);
    while (rel.startsWith('/'))
        rel.remove(0, 1);
    for (const QString &part : rel.split('/')) {
        if (part == QLatin1String(".."))
            return QString();
    }
    const QString joined = QDir(destDir).filePath(rel);
    const QString cleanDest = QDir::cleanPath(destDir);
    const QString cleanJoined = QDir::cleanPath(joined);
    if (cleanJoined != cleanDest &&
        !cleanJoined.startsWith(cleanDest + QLatin1Char('/')))
        return QString();
    return cleanJoined;
}

bool cancelled(const std::atomic<bool> *cancel)
{
    return cancel && cancel->load();
}

} // namespace

// ---------------------------------------------------------------------------
bool createTarFile(const QString &sourceDir, const QString &topName,
                   const QString &outArchivePath, bool gzip, QString *error,
                   const ProgressFn &progress, const std::atomic<bool> *cancel)
{
    const QDir root(sourceDir);
    if (!root.exists()) {
        if (error)
            *error = QStringLiteral("Source folder does not exist: %1").arg(sourceDir);
        return false;
    }
    gzFile gz = openGz(outArchivePath, gzip ? "wb6" : "wbT");
    if (!gz) {
        if (error)
            *error = QStringLiteral("Cannot create archive: %1").arg(outArchivePath);
        return false;
    }

    auto fail = [&](const QString &msg) {
        gzclose(gz);
        QFile::remove(outArchivePath);
        if (error)
            *error = msg;
        return false;
    };

    quint64 done = 0;
    const QByteArray top = topName.toUtf8();

    // The top-level directory entry itself.
    if (!writeEntryHeader(gz, top + '/', 0, 0755, 0, '5'))
        return fail(QStringLiteral("Write failed"));

    QDirIterator it(sourceDir,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                        QDir::System,
                    QDirIterator::Subdirectories);
    char pad[kBlock] = {0};
    while (it.hasNext()) {
        const QString path = it.next();
        if (cancelled(cancel))
            return fail(QStringLiteral("Cancelled"));
        const QFileInfo fi = it.fileInfo();
        const QString rel = root.relativeFilePath(path);
        const QByteArray arc = top + '/' + rel.toUtf8();
        const quint64 mtime =
            static_cast<quint64>(qMax<qint64>(0, fi.lastModified().toSecsSinceEpoch()));

        if (fi.isDir() && !fi.isSymLink()) {
            if (!writeEntryHeader(gz, arc + '/', 0, 0755, mtime, '5'))
                return fail(QStringLiteral("Write failed"));
            continue;
        }
        if (!fi.isFile()) // sockets/fifos/dir-symlinks: skip contents
            continue;

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("Cannot read %1").arg(path));
        const quint64 size = static_cast<quint64>(f.size());
        if (!writeEntryHeader(gz, arc, size, fi.isExecutable() ? 0755 : 0644,
                              mtime, '0'))
            return fail(QStringLiteral("Write failed"));

        quint64 written = 0;
        char buf[1 << 16];
        while (written < size) {
            if (cancelled(cancel))
                return fail(QStringLiteral("Cancelled"));
            const qint64 n = f.read(buf, sizeof(buf));
            if (n <= 0)
                break;
            if (!gzWriteAll(gz, buf, n))
                return fail(QStringLiteral("Write failed"));
            written += static_cast<quint64>(n);
            done += static_cast<quint64>(n);
            if (progress)
                progress(done, 0);
        }
        // Zero-fill a short read so the declared size stays consistent.
        while (written < size) {
            if (!gzWriteAll(gz, pad, 1))
                return fail(QStringLiteral("Write failed"));
            ++written;
        }
        const int rem = static_cast<int>(size % kBlock);
        if (rem && !gzWriteAll(gz, pad, kBlock - rem))
            return fail(QStringLiteral("Write failed"));
    }

    // Two zero blocks terminate the archive.
    if (!gzWriteAll(gz, pad, kBlock) || !gzWriteAll(gz, pad, kBlock))
        return fail(QStringLiteral("Write failed"));
    if (gzclose(gz) != Z_OK) {
        QFile::remove(outArchivePath);
        if (error)
            *error = QStringLiteral("Failed to finalize archive");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool extractTarFile(const QString &archivePath, const QString &destDir,
                    QString *error, const ProgressFn &progress,
                    const std::atomic<bool> *cancel)
{
    gzFile gz = openGz(archivePath, "rb");
    if (!gz) {
        if (error)
            *error = QStringLiteral("Cannot open archive: %1").arg(archivePath);
        return false;
    }
    QDir().mkpath(destDir);
    auto fail = [&](const QString &msg) {
        gzclose(gz);
        if (error)
            *error = msg;
        return false;
    };

    char header[kBlock];
    QByteArray pendingLongName; // set by a GNU 'L' block for the next header
    quint64 done = 0;

    for (;;) {
        if (cancelled(cancel))
            return fail(QStringLiteral("Cancelled"));
        const qint64 got = gzReadFull(gz, header, kBlock);
        if (got == 0)
            break; // clean EOF
        if (got < 0)
            return fail(QStringLiteral("Read error"));
        if (got < kBlock)
            return fail(QStringLiteral("Truncated archive"));

        // All-zero header marks the end of the archive.
        bool allZero = true;
        for (int i = 0; i < kBlock; ++i)
            if (header[i]) { allZero = false; break; }
        if (allZero)
            break;

        const char typeflag = header[156];
        const quint64 size = parseOctal(header + 124, 12);

        auto skipData = [&]() -> bool {
            quint64 left = ((size + kBlock - 1) / kBlock) * kBlock;
            char buf[1 << 16];
            while (left > 0) {
                const qint64 n =
                    gzReadFull(gz, buf, qMin<quint64>(left, sizeof(buf)));
                if (n <= 0)
                    return false;
                left -= static_cast<quint64>(n);
            }
            return true;
        };

        if (typeflag == 'L') { // GNU long name
            QByteArray name(static_cast<int>(size), '\0');
            const quint64 padded = ((size + kBlock - 1) / kBlock) * kBlock;
            if (gzReadFull(gz, name.data(), static_cast<qint64>(size)) !=
                static_cast<qint64>(size))
                return fail(QStringLiteral("Truncated archive"));
            if (padded > size) {
                char pad[kBlock];
                if (gzReadFull(gz, pad, static_cast<qint64>(padded - size)) < 0)
                    return fail(QStringLiteral("Read error"));
            }
            if (int nul = name.indexOf('\0'); nul >= 0)
                name.truncate(nul);
            pendingLongName = name;
            continue;
        }
        if (typeflag == 'K' || typeflag == 'x' || typeflag == 'g') {
            if (!skipData())
                return fail(QStringLiteral("Truncated archive"));
            continue;
        }

        // Resolve the member name (long name overrides; else prefix + name).
        QString member;
        if (!pendingLongName.isEmpty()) {
            member = QString::fromUtf8(pendingLongName);
        } else {
            QByteArray name(header, 100);
            if (int nul = name.indexOf('\0'); nul >= 0)
                name.truncate(nul);
            QByteArray prefix;
            if (std::memcmp(header + 257, "ustar", 5) == 0) {
                prefix = QByteArray(header + 345, 155);
                if (int nul = prefix.indexOf('\0'); nul >= 0)
                    prefix.truncate(nul);
            }
            member = prefix.isEmpty()
                         ? QString::fromUtf8(name)
                         : QString::fromUtf8(prefix + '/' + name);
        }
        pendingLongName.clear();

        const bool isDir = typeflag == '5' || member.endsWith('/');
        const QString target = safeJoin(destDir, member);
        if (target.isEmpty())
            return fail(QStringLiteral("Unsafe path in archive: %1").arg(member));

        if (isDir) {
            QDir().mkpath(target);
            if (!skipData()) // dirs carry no data, but be defensive
                return fail(QStringLiteral("Truncated archive"));
            continue;
        }
        if (typeflag != '0' && typeflag != '\0' && typeflag != '7') {
            // Hard/sym links, devices: not reproduced — skip their (empty) data.
            if (!skipData())
                return fail(QStringLiteral("Truncated archive"));
            continue;
        }

        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(QStringLiteral("Cannot write %1").arg(target));
        quint64 left = size;
        char buf[1 << 16];
        while (left > 0) {
            if (cancelled(cancel))
                return fail(QStringLiteral("Cancelled"));
            const qint64 n = gzReadFull(gz, buf, qMin<quint64>(left, sizeof(buf)));
            if (n <= 0)
                return fail(QStringLiteral("Truncated archive"));
            if (out.write(buf, n) != n)
                return fail(QStringLiteral("Write failed: %1").arg(target));
            left -= static_cast<quint64>(n);
            done += static_cast<quint64>(n);
            if (progress)
                progress(done, 0);
        }
        out.close();
        const int rem = static_cast<int>(size % kBlock);
        if (rem) {
            char pad[kBlock];
            if (gzReadFull(gz, pad, kBlock - rem) < 0)
                return fail(QStringLiteral("Read error"));
        }
    }

    gzclose(gz);
    return true;
}

} // namespace termsync::transfer::archive
