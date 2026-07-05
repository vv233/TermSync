#pragma once

#include <QString>
#include <memory>

namespace termsync::core {

// Abstraction over OS-native secret storage. The M4 implementation on Windows
// uses the Windows Credential Manager; other platforms fall back to a
// non-persistent in-memory store for now (QtKeychain / libsecret backends are
// added in M9). Secrets are keyed by an opaque string (the profile id).
class CredentialStore
{
public:
    virtual ~CredentialStore() = default;

    // Stores (or replaces) the secret for `key`. Returns false on failure.
    virtual bool store(const QString &key, const QString &secret) = 0;

    // Returns the secret for `key`, or an empty string if none / on error.
    virtual QString retrieve(const QString &key) = 0;

    // Deletes the secret for `key` (no-op if absent).
    virtual void remove(const QString &key) = 0;

    // True if this backend actually persists across process restarts.
    virtual bool isPersistent() const = 0;

    // Creates the best available backend for the current platform.
    static std::unique_ptr<CredentialStore> createDefault();
};

} // namespace termsync::core
