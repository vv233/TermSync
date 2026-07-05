#pragma once

#include "AbstractTerminalConnection.h"

class QThread;

namespace termsync::core {

// Serial-port parameters.
struct SerialParams
{
    QString portName;        // "COM3" (Windows) or "/dev/ttyUSB0" (POSIX)
    int baudRate = 115200;
    int dataBits = 8;        // 5..8
    int stopBits = 1;        // 1 or 2
    char parity = 'N';       // 'N', 'E', 'O'
    bool rtsCtsFlow = false;
};

class SerialWorker; // internal

// A serial-port connection that drives a terminal view. Uses the native OS
// serial API (Win32 / POSIX termios) on a worker thread, so it needs no extra
// Qt module. Implements the same AbstractTerminalConnection surface as SSH and
// Telnet, so TerminalWidget renders it unchanged.
//
// Live verification needs a real or virtual serial device (e.g. com0com / a
// `socat` PTY pair), which the build sandbox lacks; the engine is code-complete.
class SerialConnection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit SerialConnection(QObject *parent = nullptr);
    ~SerialConnection() override;

    void open(const SerialParams &params);

    void sendData(const QByteArray &data) override;
    void resize(int cols, int rows) override; // no-op for serial
    void disconnectFromHost() override;
    bool isConnected() const override { return m_connected; }

private:
    QThread *m_thread = nullptr;
    SerialWorker *m_worker = nullptr;
    bool m_connected = false;
};

} // namespace termsync::core

Q_DECLARE_METATYPE(termsync::core::SerialParams)
