#pragma once

#include <QString>

namespace termsync::core {

// The protocol a profile uses. SSH2 is the only one implemented in M4;
// the enum is future-proofed for the milestones that add the others.
enum class Protocol {
    SSH2 = 0,
    SFTP_ONLY = 1,
    FTP = 2,
    FTPS = 3,
    TELNET = 4,
    RLOGIN = 5,
    SERIAL = 6,
    TN3270 = 7,
    TN5250 = 8,
};

enum class AuthMethod {
    Password = 0,
    PublicKey = 1,
    KeyboardInteractive = 2,
    Agent = 3,
};

// A saved connection profile — the shared entity backing both terminal and
// (later) file-transfer sessions. Persisted by ProfileStore; the secret itself
// lives in the CredentialStore, keyed by `id`, never in the profile row.
struct ConnectionProfile
{
    QString id;             // UUID
    QString name;           // display name
    QString folderPath;     // tree organisation, e.g. "Work/Prod"

    Protocol protocol = Protocol::SSH2;
    QString host;
    quint16 port = 22;

    QString username;
    AuthMethod authMethod = AuthMethod::Password;
    bool savePassword = false;   // whether the password is kept in the vault
    QString privateKeyPath;      // for PublicKey auth (M9)

    // Terminal defaults (expanded in later milestones).
    int cols = 80;
    int rows = 24;

    bool isValid() const { return !host.isEmpty() && !name.isEmpty(); }
};

} // namespace termsync::core
