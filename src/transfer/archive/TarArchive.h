#pragma once

#include <QString>
#include <atomic>
#include <functional>

namespace termsync::transfer::archive {

// Progress callback: bytes processed so far. `total` is best-effort and may be 0
// when the total is unknown (e.g. streaming extraction).
using ProgressFn = std::function<void(quint64 done, quint64 total)>;

// Bundles many small files into a single stream so a folder transfer costs one
// round-trip instead of N. Used by the SFTP bulk-transfer path: the remote side
// runs `tar cf -` (or `czf -`) and we extract locally, and vice-versa for
// upload. Implemented in-process (no external tar) so it is dependency-light and
// unit-testable offline.

// Creates a POSIX-ustar tar archive of `sourceDir` at `outArchivePath`. Every
// entry is stored under a single top-level directory named `topName` (so the
// extracted tree reproduces the folder the user picked). When `gzip` is true the
// archive is gzip-compressed. Returns false and sets *error on failure.
bool createTarFile(const QString &sourceDir, const QString &topName,
                   const QString &outArchivePath, bool gzip, QString *error,
                   const ProgressFn &progress = {},
                   const std::atomic<bool> *cancel = nullptr);

// Extracts a tar archive at `archivePath` into `destDir` (created if needed).
// Auto-detects gzip via the magic header, so the caller need not know which the
// remote produced. Rejects unsafe member paths (absolute or "..") to prevent an
// archive escaping `destDir`. Returns false and sets *error on failure.
bool extractTarFile(const QString &archivePath, const QString &destDir,
                    QString *error, const ProgressFn &progress = {},
                    const std::atomic<bool> *cancel = nullptr);

} // namespace termsync::transfer::archive
