#include "local/LocalShellConnection.h"

#include <QProcess>
#include <QProcessEnvironment>

namespace termsync::core {

LocalShellConnection::LocalShellConnection(QObject *parent)
    : AbstractTerminalConnection(parent)
{
}

LocalShellConnection::~LocalShellConnection()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString LocalShellConnection::defaultShell()
{
#ifdef Q_OS_WIN
    const QString comspec = qEnvironmentVariable("COMSPEC");
    return comspec.isEmpty() ? QStringLiteral("cmd.exe") : comspec;
#else
    const QString shell = qEnvironmentVariable("SHELL");
    return shell.isEmpty() ? QStringLiteral("/bin/sh") : shell;
#endif
}

void LocalShellConnection::start(const QString &program,
                                 const QStringList &arguments)
{
    if (m_process)
        return; // already started

    m_program = program.isEmpty() ? defaultShell() : program;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    // Advertise a terminal size to the child (best effort; no real TTY).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLUMNS"), QString::number(m_cols));
    env.insert(QStringLiteral("LINES"), QString::number(m_rows));
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray chunk = m_process->readAllStandardOutput();
        if (!chunk.isEmpty())
            emit dataReceived(chunk);
    });
    connect(m_process, &QProcess::started, this,
            [this] { emit connected(); });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                emit errorOccurred(m_process->errorString());
            });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { emit disconnected(); });

    m_process->start(m_program, arguments);
}

void LocalShellConnection::sendData(const QByteArray &data)
{
    if (m_process && m_process->state() == QProcess::Running)
        m_process->write(data);
}

void LocalShellConnection::resize(int cols, int rows)
{
    // No PTY: record the size for any future (re)start; cannot signal a running
    // pipe-backed child of a window-size change.
    m_cols = cols;
    m_rows = rows;
}

void LocalShellConnection::disconnectFromHost()
{
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(1000))
        m_process->kill();
}

bool LocalShellConnection::isConnected() const
{
    return m_process && m_process->state() == QProcess::Running;
}

} // namespace termsync::core
