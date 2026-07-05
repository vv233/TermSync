#pragma once

#include "AbstractTerminalConnection.h"
#include "tn3270/Tn3270Stream.h"

class QTcpSocket;

namespace termsync::core {

class Tn3270Connection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit Tn3270Connection(QObject *parent = nullptr);
    ~Tn3270Connection() override;

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
    void processRecord(const QByteArray &record);
    void reply(unsigned char verb, unsigned char option);
    void sendTerminalType();
    void sendRecord(const QByteArray &record);
    void redraw();

    QTcpSocket *m_socket = nullptr;
    State m_state = State::Data;
    unsigned char m_verb = 0;
    QByteArray m_subBuffer;
    QByteArray m_record;
    Tn3270Stream m_stream;
    bool m_connected = false;
};

} // namespace termsync::core
