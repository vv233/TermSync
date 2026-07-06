#include "session_dialogs/TftpServerDialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTime>
#include <QVBoxLayout>

namespace termsync::ui {

TftpServerDialog::TftpServerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("TFTP Server"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 440);

    auto *outer = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    auto *rootRow = new QHBoxLayout;
    m_rootEdit = new QLineEdit(
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), this);
    auto *browse = new QPushButton(tr("Browse..."), this);
    connect(browse, &QPushButton::clicked, this, &TftpServerDialog::browseRoot);
    rootRow->addWidget(m_rootEdit, 1);
    rootRow->addWidget(browse);
    form->addRow(tr("Root folder:"), rootRow);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(69);
    form->addRow(tr("Port:"), m_portSpin);

    m_readOnly = new QCheckBox(tr("Read-only (refuse uploads)"), this);
    form->addRow(QString(), m_readOnly);
    m_allowOverwrite = new QCheckBox(tr("Allow overwriting existing files"), this);
    m_allowOverwrite->setChecked(true);
    form->addRow(QString(), m_allowOverwrite);
    outer->addLayout(form);

    auto *controls = new QHBoxLayout;
    m_startStop = new QPushButton(tr("Start Server"), this);
    connect(m_startStop, &QPushButton::clicked, this,
            &TftpServerDialog::toggleServer);
    m_status = new QLabel(tr("Stopped"), this);
    controls->addWidget(m_startStop);
    controls->addWidget(m_status, 1);
    outer->addLayout(controls);

    outer->addWidget(new QLabel(tr("Log:"), this));
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    outer->addWidget(m_log, 1);

    auto append = [this](const QString &line) {
        m_log->appendPlainText(
            QStringLiteral("%1  %2")
                .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), line));
    };
    connect(&m_server, &core::tftp::TftpServer::logMessage, this, append);
    connect(&m_server, &core::tftp::TftpServer::transferStarted, this,
            [append](const QString &name, bool writing, const QHostAddress &peer) {
                append(QStringLiteral("→ %1 %2 (%3)")
                           .arg(writing ? QStringLiteral("upload") : QStringLiteral("download"),
                                name, peer.toString()));
            });
    connect(&m_server, &core::tftp::TftpServer::transferFinished, this,
            [append](const QString &name, bool ok, const QString &detail) {
                append(QStringLiteral("✓ %1 %2 — %3")
                           .arg(name, ok ? QStringLiteral("done") : QStringLiteral("failed"),
                                detail));
            });
}

void TftpServerDialog::browseRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("TFTP Root Folder"), m_rootEdit->text());
    if (!dir.isEmpty())
        m_rootEdit->setText(dir);
}

void TftpServerDialog::toggleServer()
{
    if (m_server.isRunning()) {
        m_server.stop();
        setRunningUi(false);
        return;
    }

    m_server.setRootDirectory(m_rootEdit->text());
    m_server.setReadOnly(m_readOnly->isChecked());
    m_server.setAllowOverwrite(m_allowOverwrite->isChecked());
    if (!m_server.start(static_cast<quint16>(m_portSpin->value()),
                        QHostAddress(QHostAddress::Any))) {
        QMessageBox::warning(
            this, tr("TFTP Server"),
            tr("Could not start on port %1:\n%2\n\n"
               "Ports below 1024 (including the default 69) usually require "
               "administrator privileges; try a higher port such as 6900.")
                .arg(m_portSpin->value())
                .arg(m_server.lastError()));
        return;
    }
    setRunningUi(true);
}

void TftpServerDialog::setRunningUi(bool running)
{
    m_startStop->setText(running ? tr("Stop Server") : tr("Start Server"));
    m_status->setText(running ? tr("Listening on port %1").arg(m_server.port())
                              : tr("Stopped"));
    m_rootEdit->setEnabled(!running);
    m_portSpin->setEnabled(!running);
    m_readOnly->setEnabled(!running);
    m_allowOverwrite->setEnabled(!running);
}

} // namespace termsync::ui
