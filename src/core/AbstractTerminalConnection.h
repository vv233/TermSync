#pragma once

#include <QByteArray>
#include <QObject>

namespace termsync::core {

// Common interface for anything that can drive a terminal view: an SSH shell
// channel, a Telnet/rlogin socket, or a serial port. TerminalWidget renders and
// sends keystrokes through this interface, so it is protocol-agnostic.
class AbstractTerminalConnection : public QObject
{
    Q_OBJECT

public:
    explicit AbstractTerminalConnection(QObject *parent = nullptr)
        : QObject(parent) {}
    ~AbstractTerminalConnection() override = default;

    // Sends raw bytes (typically keystrokes) to the remote end.
    virtual void sendData(const QByteArray &data) = 0;
    // Informs the remote of a new terminal size (PTY resize / NAWS / no-op).
    virtual void resize(int cols, int rows) = 0;
    // Tears down the connection.
    virtual void disconnectFromHost() = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &message);
    void disconnected();
};

} // namespace termsync::core
