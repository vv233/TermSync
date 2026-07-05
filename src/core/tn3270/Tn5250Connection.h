#pragma once

#include "AbstractTerminalConnection.h"
#include "tn3270/Tn5250Stream.h"

class QTcpSocket;

namespace termsync::core {

// TN5250 (IBM 5250 / AS-400) terminal connection, first pass: Telnet
// negotiation (binary + EOR, terminal type IBM-3179-2) and read-only
// Write-to-Display rendering. Field input/AID submission is a follow-up.
class Tn5250Connection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit Tn5250Connection(QObject *parent = nullptr);
    ~Tn5250Connection() override;

    void connectToHost(const QString &host, quint16 port);
    void sendData(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void disconnectFromHost() override;
    bool isConnected() const override;

private slots:
    void onReadyRead();

private:
    enum class State { Data, Iac, Verb, SubOption, SubData, SubIac };

    void processIncoming(const QByteArray &bytes);
    void reply(unsigned char verb, unsigned char option);
    void sendTerminalType();

    QTcpSocket *m_socket = nullptr;
    State m_state = State::Data;
    unsigned char m_verb = 0;
    QByteArray m_subBuffer;
    QByteArray m_record;
    Tn5250Stream m_stream;
    bool m_connected = false;
};

} // namespace termsync::core
