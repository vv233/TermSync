#pragma once

#include <QDialog>

#include "model/ConnectionProfile.h"
#include "ssh/SshConnection.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace termsync::ui {

// Compact connection dialog for host, protocol, authentication, proxy, and
// protocol-specific options.
class QuickConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickConnectDialog(QWidget *parent = nullptr);

    // Prefills the fields from an existing profile for editing. toProfile() then
    // keeps that profile's id (an update rather than a new host).
    void loadProfile(const core::ConnectionProfile &profile);

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
    QComboBox *m_authMethod = nullptr;
    QLineEdit *m_keyPath = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_x11Forwarding = nullptr;
    QCheckBox *m_saveSession = nullptr;
    QCheckBox *m_savePassword = nullptr;

    QString m_editId; // non-empty when editing an existing profile
};

} // namespace termsync::ui
