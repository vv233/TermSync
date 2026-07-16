#include "session_dialogs/SessionOptionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace termsync::ui {

SessionOptionsDialog::SessionOptionsDialog(const core::ConnectionProfile &profile,
                                           QWidget *parent)
    : QDialog(parent), m_profile(profile)
{
    setWindowTitle(tr("Session Options — %1")
                       .arg(profile.name.isEmpty() ? profile.host : profile.name));

    auto *categories = new QListWidget(this);
    categories->addItem(tr("Connection"));
    categories->addItem(tr("Terminal"));
    categories->setMaximumWidth(160);
    categories->setCurrentRow(0);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildConnectionPage());
    m_pages->addWidget(buildTerminalPage());
    connect(categories, &QListWidget::currentRowChanged, m_pages,
            &QStackedWidget::setCurrentIndex);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *top = new QHBoxLayout;
    top->addWidget(categories);
    top->addWidget(m_pages, 1);
    auto *root = new QVBoxLayout(this);
    root->addLayout(top, 1);
    root->addWidget(buttons);

    resize(560, 380);
    syncAuthRows();
}

QWidget *SessionOptionsDialog::buildConnectionPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_name = new QLineEdit(m_profile.name, page);
    m_host = new QLineEdit(m_profile.host, page);
    m_port = new QSpinBox(page);
    m_port->setRange(1, 65535);
    m_port->setValue(m_profile.port);
    m_username = new QLineEdit(m_profile.username, page);

    m_protocol = new QComboBox(page);
    m_protocol->addItem(tr("SSH2"), int(core::Protocol::SSH2));
    m_protocol->addItem(tr("SFTP"), int(core::Protocol::SFTP_ONLY));
    m_protocol->addItem(tr("FTP"), int(core::Protocol::FTP));
    m_protocol->addItem(tr("FTPS"), int(core::Protocol::FTPS));
    m_protocol->addItem(tr("Telnet"), int(core::Protocol::TELNET));
    m_protocol->addItem(tr("TN3270"), int(core::Protocol::TN3270));
    m_protocol->addItem(tr("TN5250"), int(core::Protocol::TN5250));
    m_protocol->addItem(tr("Serial"), int(core::Protocol::SERIAL));
    if (int i = m_protocol->findData(int(m_profile.protocol)); i >= 0)
        m_protocol->setCurrentIndex(i);

    m_authMethod = new QComboBox(page);
    m_authMethod->addItem(tr("Password"), int(core::AuthMethod::Password));
    m_authMethod->addItem(tr("Public Key"), int(core::AuthMethod::PublicKey));
    m_authMethod->addItem(tr("Keyboard Interactive"),
                          int(core::AuthMethod::KeyboardInteractive));
    m_authMethod->addItem(tr("SSH Agent"), int(core::AuthMethod::Agent));
    if (int i = m_authMethod->findData(int(m_profile.authMethod)); i >= 0)
        m_authMethod->setCurrentIndex(i);

    auto *keyRow = new QWidget(page);
    auto *keyLayout = new QHBoxLayout(keyRow);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    m_keyPath = new QLineEdit(m_profile.privateKeyPath, keyRow);
    auto *browse = new QPushButton(tr("Browse…"), keyRow);
    keyLayout->addWidget(m_keyPath, 1);
    keyLayout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("Private Key"));
        if (!f.isEmpty())
            m_keyPath->setText(f);
    });

    m_savePassword = new QCheckBox(tr("Save password in credential vault"), page);
    m_savePassword->setChecked(m_profile.savePassword);
    m_password = new QLineEdit(page);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("(unchanged)"));

    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Protocol:"), m_protocol);
    form->addRow(tr("Host:"), m_host);
    form->addRow(tr("Port:"), m_port);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Auth:"), m_authMethod);
    form->addRow(tr("Private key:"), keyRow);
    form->addRow(tr("Password:"), m_password);
    form->addRow(QString(), m_savePassword);

    connect(m_authMethod, &QComboBox::currentIndexChanged, this,
            [this](int) { syncAuthRows(); });
    return page;
}

QWidget *SessionOptionsDialog::buildTerminalPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_cols = new QSpinBox(page);
    m_cols->setRange(20, 500);
    m_cols->setValue(m_profile.cols);
    m_rows = new QSpinBox(page);
    m_rows->setRange(5, 200);
    m_rows->setValue(m_profile.rows);
    m_x11 = new QCheckBox(tr("Forward X11 connections"), page);
    m_x11->setChecked(m_profile.x11Forwarding);

    form->addRow(tr("Columns:"), m_cols);
    form->addRow(tr("Rows:"), m_rows);
    form->addRow(QString(), m_x11);
    return page;
}

void SessionOptionsDialog::syncAuthRows()
{
    const auto method =
        static_cast<core::AuthMethod>(m_authMethod->currentData().toInt());
    const bool pubkey = method == core::AuthMethod::PublicKey;
    const bool needsPassword = method != core::AuthMethod::Agent;
    m_keyPath->setEnabled(pubkey);
    m_password->setEnabled(needsPassword);
    m_savePassword->setEnabled(needsPassword);
}

core::ConnectionProfile SessionOptionsDialog::toProfile() const
{
    core::ConnectionProfile p = m_profile; // keep id, folderPath, etc.
    p.name = m_name->text().trimmed();
    p.protocol = static_cast<core::Protocol>(m_protocol->currentData().toInt());
    p.host = m_host->text().trimmed();
    p.port = static_cast<quint16>(m_port->value());
    p.username = m_username->text().trimmed();
    p.authMethod =
        static_cast<core::AuthMethod>(m_authMethod->currentData().toInt());
    p.privateKeyPath = m_keyPath->text().trimmed();
    p.savePassword = m_savePassword->isChecked();
    p.cols = m_cols->value();
    p.rows = m_rows->value();
    p.x11Forwarding = m_x11->isChecked();
    return p;
}

bool SessionOptionsDialog::passwordChanged() const
{
    return !m_password->text().isEmpty();
}

QString SessionOptionsDialog::password() const
{
    return m_password->text();
}

} // namespace termsync::ui
