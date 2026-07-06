#pragma once

#include <QDialog>

#include "tftp/TftpServer.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QLabel;

namespace termsync::ui {

// Control panel for the built-in TFTP server (M20): pick a root folder + port,
// toggle read-only/overwrite, start/stop, and watch a live log. Non-modal so
// the server keeps running while the user works; closing stops it.
class TftpServerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TftpServerDialog(QWidget *parent = nullptr);

private slots:
    void browseRoot();
    void toggleServer();

private:
    void setRunningUi(bool running);

    core::tftp::TftpServer m_server;

    QLineEdit *m_rootEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QCheckBox *m_readOnly = nullptr;
    QCheckBox *m_allowOverwrite = nullptr;
    QPushButton *m_startStop = nullptr;
    QLabel *m_status = nullptr;
    QPlainTextEdit *m_log = nullptr;
};

} // namespace termsync::ui
