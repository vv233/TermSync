#include "session_dialogs/QuickConnectDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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

    m_authMethod = new QComboBox(this);
    m_authMethod->addItem(tr("Password"), static_cast<int>(core::AuthMethod::Password));
    m_authMethod->addItem(tr("Public Key"), static_cast<int>(core::AuthMethod::PublicKey));
    m_authMethod->addItem(tr("Keyboard Interactive"),
                          static_cast<int>(core::AuthMethod::KeyboardInteractive));
    m_authMethod->addItem(tr("SSH Agent"), static_cast<int>(core::AuthMethod::Agent));

    m_keyPath = new QLineEdit(this);
    m_keyPath->setPlaceholderText(tr("path to private key"));

    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);

    m_protocol = new QComboBox(this);
    // userData carries the core::Protocol enum value.
    m_protocol->addItem(tr("SSH2"), static_cast<int>(core::Protocol::SSH2));
    m_protocol->addItem(tr("SFTP"), static_cast<int>(core::Protocol::SFTP_ONLY));
    m_protocol->addItem(tr("FTP"), static_cast<int>(core::Protocol::FTP));
    m_protocol->addItem(tr("FTPS"), static_cast<int>(core::Protocol::FTPS));
    m_protocol->addItem(tr("Telnet"), static_cast<int>(core::Protocol::TELNET));
    m_protocol->addItem(tr("TN3270"), static_cast<int>(core::Protocol::TN3270));
    m_protocol->addItem(tr("TN5250"), static_cast<int>(core::Protocol::TN5250));
    m_protocol->addItem(tr("Serial"), static_cast<int>(core::Protocol::SERIAL));
    // Default the port and relabel the host field per protocol.
    connect(m_protocol, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto proto = static_cast<core::Protocol>(m_protocol->currentData().toInt());
        int port = 22;
        if (proto == core::Protocol::FTP || proto == core::Protocol::FTPS)
            port = 21;
        else if (proto == core::Protocol::TELNET || proto == core::Protocol::TN3270 ||
                 proto == core::Protocol::TN5250)
            port = 23;
        m_port->setValue(port);
        const bool serial = proto == core::Protocol::SERIAL;
        // For serial, the "hostname" field holds the port name (COM3, ttyUSB0)
        // and the numeric port is repurposed as the baud rate.
        m_host->setPlaceholderText(serial ? tr("serial port, e.g. COM3")
                                          : tr("hostname or IP"));
        if (serial)
            m_port->setValue(115200);
        m_port->setMaximum(serial ? 4000000 : 65535);
    });

    auto *form = new QFormLayout;
    form->addRow(tr("Protocol:"), m_protocol);
    form->addRow(tr("Hostname:"), m_host);
    form->addRow(tr("Port:"), m_port);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Auth:"), m_authMethod);

    // Key path row with a Browse button (only relevant for Public Key auth).
    auto *keyRow = new QWidget(this);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton(tr("Browse..."), keyRow);
    keyLayout->addWidget(m_keyPath, 1);
    keyLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Select Private Key"));
        if (!f.isEmpty())
            m_keyPath->setText(f);
    });
    form->addRow(tr("Private key:"), keyRow);

    auto *passwordLabel = new QLabel(tr("Password:"), this);
    form->addRow(passwordLabel, m_password);

    // Toggle the key row / password label meaning based on the auth method.
    auto updateAuthUi = [this, keyRow, passwordLabel] {
        const auto m = static_cast<core::AuthMethod>(m_authMethod->currentData().toInt());
        const bool pubkey = m == core::AuthMethod::PublicKey;
        keyRow->setVisible(pubkey);
        passwordLabel->setText(pubkey ? tr("Passphrase:") : tr("Password:"));
        m_password->setVisible(m != core::AuthMethod::Agent);
        passwordLabel->setVisible(m != core::AuthMethod::Agent);
    };
    connect(m_authMethod, &QComboBox::currentIndexChanged, this,
            [updateAuthUi](int) { updateAuthUi(); });
    updateAuthUi();

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
    p.protocol = static_cast<core::Protocol>(m_protocol->currentData().toInt());
    p.authMethod = static_cast<core::AuthMethod>(m_authMethod->currentData().toInt());
    p.privateKeyPath = m_keyPath->text().trimmed();
    p.savePassword = savePassword();
    return p;
}

core::SshConnectionParams QuickConnectDialog::params() const
{
    core::SshConnectionParams p;
    p.host = m_host->text().trimmed();
    p.port = static_cast<quint16>(m_port->value());
    p.username = m_username->text().trimmed();
    // core::AuthMethod and core::SshAuthMethod share ordering.
    p.authMethod = static_cast<core::SshAuthMethod>(m_authMethod->currentData().toInt());
    p.privateKeyPath = m_keyPath->text().trimmed();
    // For Public Key the secret field is the passphrase; otherwise a password.
    if (p.authMethod == core::SshAuthMethod::PublicKey)
        p.passphrase = m_password->text();
    else
        p.password = m_password->text();
    return p;
}

} // namespace termsync::ui
