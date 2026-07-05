#include "credential/CredentialStore.h"

#include <QHash>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <wincred.h>
#endif

namespace termsync::core {

namespace {

// The Credential Manager target name namespace, so our entries are grouped and
// don't collide with other apps.
QString targetName(const QString &key)
{
    return QStringLiteral("TermSync:") + key;
}

#ifdef _WIN32
// Windows Credential Manager backend (CRED_TYPE_GENERIC).
class WinCredentialStore : public CredentialStore
{
public:
    bool store(const QString &key, const QString &secret) override
    {
        const std::wstring target = targetName(key).toStdWString();
        const QByteArray blob(reinterpret_cast<const char *>(secret.utf16()),
                              secret.size() * 2); // UTF-16LE bytes

        CREDENTIALW cred{};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR>(target.c_str());
        cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
        cred.CredentialBlob =
            reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

        return CredWriteW(&cred, 0) == TRUE;
    }

    QString retrieve(const QString &key) override
    {
        const std::wstring target = targetName(key).toStdWString();
        PCREDENTIALW cred = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) != TRUE)
            return {};
        QString result;
        if (cred->CredentialBlob && cred->CredentialBlobSize > 0) {
            result = QString::fromUtf16(
                reinterpret_cast<const char16_t *>(cred->CredentialBlob),
                cred->CredentialBlobSize / 2);
        }
        CredFree(cred);
        return result;
    }

    void remove(const QString &key) override
    {
        const std::wstring target = targetName(key).toStdWString();
        CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
    }

    bool isPersistent() const override { return true; }
};
#endif // _WIN32

// Non-persistent fallback (used where no OS backend is wired yet). Keeps
// secrets only for the current process, so the user is prompted each run.
class MemoryCredentialStore : public CredentialStore
{
public:
    bool store(const QString &key, const QString &secret) override
    {
        m_map.insert(key, secret);
        return true;
    }
    QString retrieve(const QString &key) override
    {
        return m_map.value(key);
    }
    void remove(const QString &key) override { m_map.remove(key); }
    bool isPersistent() const override { return false; }

private:
    QHash<QString, QString> m_map;
};

} // namespace

std::unique_ptr<CredentialStore> CredentialStore::createDefault()
{
#ifdef _WIN32
    return std::make_unique<WinCredentialStore>();
#else
    return std::make_unique<MemoryCredentialStore>();
#endif
}

} // namespace termsync::core
