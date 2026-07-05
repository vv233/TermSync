#pragma once

#include "AbstractTerminalConnection.h"

class QTcpSocket;

namespace termsync::core {

// A Telnet client that drives a terminal view. Handles IAC option negotiation
// (echo, suppress-go-ahead, terminal-type, NAWS window size) and strips control
// sequences from the data stream. Uses an event-driven QTcpSocket, so it needs
// no worker thread.
class TelnetConnection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit TelnetConnection(QObject *parent = nullptr);
    ~TelnetConnection() override;

    void connectToHost(const QString &host, quint16 port);

    void sendData(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void disconnectFromHost() override;
    bool isConnected() const override;

private slots:
    void onReadyRead();

private:
    void processIncoming(const QByteArray &bytes);
    void sendNaws();
    void reply(unsigned char verb, unsigned char option);

    enum class State { Data, Iac, Verb, SubOption, SubData, SubIac };

    QTcpSocket *m_socket = nullptr;
    State m_state = State::Data;
    unsigned char m_verb = 0;
    QByteArray m_subBuffer;
    int m_cols = 80;
    int m_rows = 24;
    bool m_connected = false;
};

} // namespace termsync::core
