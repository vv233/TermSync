#pragma once

#include <QDialog>

#include "model/ConnectionProfile.h"
#include "ssh/SshConnection.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace termsync::ui {

// Minimal M2 Quick Connect dialog: enough fields to open an SSH2 shell
// (host/port/username/password). This mirrors the SecureCRT "Quick Connect"
// dialog's core fields; protocol/firewall/auth-method options are added as
// their milestones land (see docs/ui-parity.md).
class QuickConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickConnectDialog(QWidget *parent = nullptr);

    core::SshConnectionParams params() const;
    QString password() const;

    // Whether the user asked to save this as a profile, and if so the profile
    // built from the fields (id is freshly generated; savePassword reflects the
    // "Save password" checkbox).
    bool saveSession() const;
    bool savePassword() const;
    core::ConnectionProfile toProfile() const;

private:
    QComboBox *m_protocol = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_username = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_saveSession = nullptr;
    QCheckBox *m_savePassword = nullptr;
};

} // namespace termsync::ui
