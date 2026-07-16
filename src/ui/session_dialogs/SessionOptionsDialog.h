#pragma once

#include <QDialog>

#include "model/ConnectionProfile.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;

namespace termsync::ui {

// SecureCRT-style Session Options: a categorised properties editor for a saved
// ConnectionProfile. The left list picks a category (Connection, Terminal) and
// the right panel shows its fields. Edits an existing profile in place (the id
// is preserved); the password field is left blank to mean "keep the stored one".
class SessionOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SessionOptionsDialog(const core::ConnectionProfile &profile,
                                  QWidget *parent = nullptr);

    core::ConnectionProfile toProfile() const;
    // True when the user typed a new password (so the caller should re-store it).
    bool passwordChanged() const;
    QString password() const;

private:
    QWidget *buildConnectionPage();
    QWidget *buildTerminalPage();
    void syncAuthRows();

    core::ConnectionProfile m_profile; // seed; toProfile() applies the edits

    // Connection page.
    QLineEdit *m_name = nullptr;
    QComboBox *m_protocol = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_username = nullptr;
    QComboBox *m_authMethod = nullptr;
    QLineEdit *m_keyPath = nullptr;
    QCheckBox *m_savePassword = nullptr;
    QLineEdit *m_password = nullptr;

    // Terminal page.
    QSpinBox *m_cols = nullptr;
    QSpinBox *m_rows = nullptr;
    QCheckBox *m_x11 = nullptr;

    QStackedWidget *m_pages = nullptr;
};

} // namespace termsync::ui
