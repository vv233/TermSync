#pragma once

#include <QString>
#include <QStringList>

#include "AbstractTerminalConnection.h"

class QProcess;

namespace termsync::core {

// A local shell session (SecureCRT "Local Shell"): runs the platform command
// interpreter as a child process and drives a terminal view through it. Uses
// QProcess with merged stdout/stderr, so it needs no worker thread.
//
// NOTE: this is a pipe-backed shell, not a full pseudo-terminal — there is no
// TTY, so programs that require one (curses apps, password prompts) and window
// resizing behave differently than over SSH. A ConPTY/openpty backend is a
// follow-up; a plain shell (cmd/bash: run commands, see output) works today.
class LocalShellConnection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit LocalShellConnection(QObject *parent = nullptr);
    ~LocalShellConnection() override;

    // Starts the shell. With empty arguments the platform default shell is used
    // (%COMSPEC%/cmd.exe on Windows, $SHELL or /bin/sh elsewhere).
    void start(const QString &program = QString(),
               const QStringList &arguments = {});

    // The resolved shell program (valid after start()); handy for a tab title.
    QString shellProgram() const { return m_program; }

    // Platform default shell program.
    static QString defaultShell();

    void sendData(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void disconnectFromHost() override;
    bool isConnected() const override;

private:
    QProcess *m_process = nullptr;
    QString m_program;
    int m_cols = 80;
    int m_rows = 24;
};

} // namespace termsync::core
