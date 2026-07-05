#include "session_dialogs/QuickConnectDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "store/ProfileStore.h"

namespace termsync::ui {

QuickConnectDialog::QuickConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Quick Connect"));
    setModal(true);

    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(tr("hostname or IP"));

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(22);

    m_username = new QLineEdit(this);

    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout;
    form->addRow(tr("Protocol:"), new QLineEdit(QStringLiteral("SSH2"), this));
    form->itemAt(0, QFormLayout::FieldRole)->widget()->setEnabled(false);
    form->addRow(tr("Hostname:"), m_host);
    form->addRow(tr("Port:"), m_port);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Password:"), m_password);

    m_saveSession = new QCheckBox(tr("Save session"), this);
    m_savePassword = new QCheckBox(tr("Save password"), this);
    m_savePassword->setEnabled(false);
    // Saving a password only makes sense when the session itself is saved.
    connect(m_saveSession, &QCheckBox::toggled, m_savePassword,
            &QWidget::setEnabled);
    connect(m_saveSession, &QCheckBox::toggled, this, [this](bool on) {
        if (!on)
            m_savePassword->setChecked(false);
    });
    form->addRow(QString(), m_saveSession);
    form->addRow(QString(), m_savePassword);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                             QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    m_host->setFocus();
}

QString QuickConnectDialog::password() const
{
    return m_password->text();
}

bool QuickConnectDialog::saveSession() const
{
    return m_saveSession->isChecked();
}

bool QuickConnectDialog::savePassword() const
{
    return m_saveSession->isChecked() && m_savePassword->isChecked();
}

core::ConnectionProfile QuickConnectDialog::toProfile() const
{
    core::ConnectionProfile p;
    p.id = core::ProfileStore::newId();
    p.host = m_host->text().trimmed();
    p.port = static_cast<quint16>(m_port->value());
    p.username = m_username->text().trimmed();
    // Name defaults to user@host when saving; the session tree shows this.
    p.name = p.username.isEmpty() ? p.host : (p.username + '@' + p.host);
    p.protocol = core::Protocol::SSH2;
    p.authMethod = core::AuthMethod::Password;
    p.savePassword = savePassword();
    return p;
}

core::SshConnectionParams QuickConnectDialog::params() const
{
    core::SshConnectionParams p;
    p.host = m_host->text().trimmed();
    p.port = static_cast<quint16>(m_port->value());
    p.username = m_username->text().trimmed();
    p.password = m_password->text();
    return p;
}

} // namespace termsync::ui
