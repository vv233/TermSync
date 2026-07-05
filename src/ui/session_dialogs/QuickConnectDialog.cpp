#include "session_dialogs/QuickConnectDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

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
