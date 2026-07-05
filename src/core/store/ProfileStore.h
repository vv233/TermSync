#pragma once

#include <QString>
#include <QVector>

#include "model/ConnectionProfile.h"

namespace termsync::core {

// Persistent store for connection profiles and known host keys, backed by a
// single SQLite file (via Qt's bundled QSQLITE driver). Secrets are NOT stored
// here — only the profile row (which references a CredentialStore key by id).
//
// Schema is versioned via PRAGMA user_version so later milestones can migrate.
class ProfileStore
{
public:
    ProfileStore();
    ~ProfileStore();

    // Opens (creating if needed) the database file and applies migrations.
    // Returns false and sets lastError() on failure.
    bool open(const QString &filePath);
    QString lastError() const { return m_lastError; }

    // --- Profiles ---------------------------------------------------------
    QVector<ConnectionProfile> allProfiles() const;
    bool upsert(const ConnectionProfile &profile);   // insert or update by id
    bool remove(const QString &id);

    // --- Known host keys (trust-on-first-use) -----------------------------
    // Returns the stored SHA-256 fingerprint for host:port, or empty if none.
    QString knownFingerprint(const QString &host, quint16 port) const;
    bool setKnownFingerprint(const QString &host, quint16 port,
                             const QString &fingerprint);

    // Generates a new UUID string (helper for creating profiles).
    static QString newId();

private:
    bool applyMigrations();

    QString m_connectionName;   // QSqlDatabase connection name
    QString m_lastError;
    bool m_open = false;
};

} // namespace termsync::core
