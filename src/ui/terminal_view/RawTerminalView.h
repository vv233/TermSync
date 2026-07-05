#pragma once

#include <QPlainTextEdit>

#include "ssh/SshConnection.h"

namespace termsync::ui {

// M2 terminal view: a raw byte pass-through.
//
// It owns an SshConnection, forwards keystrokes to the remote shell, and
// dumps received bytes verbatim into a QPlainTextEdit — escape sequences and
// all. This intentionally looks "ugly" (control codes are visible); the real
// VT100/xterm parser and grid renderer replace it in M3.
class RawTerminalView : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit RawTerminalView(const core::SshConnectionParams &params,
                             QWidget *parent = nullptr);

signals:
    // Forwarded connection status, so the MainWindow can update tab titles
    // and the status bar.
    void statusMessage(const QString &message);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onDataReceived(const QByteArray &data);

private:
    core::SshConnection *m_connection = nullptr;
};

} // namespace termsync::ui
