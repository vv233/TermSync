#include "tn3270/Tn5250Connection.h"

#include <QTcpSocket>

namespace termsync::core {

namespace {
constexpr unsigned char IAC = 255;
constexpr unsigned char DONT = 254;
constexpr unsigned char DO = 253;
constexpr unsigned char WONT = 252;
constexpr unsigned char WILL = 251;
constexpr unsigned char SB = 250;
constexpr unsigned char SE = 240;
constexpr unsigned char EOR = 239;
constexpr unsigned char OPT_BINARY = 0;
constexpr unsigned char OPT_TTYPE = 24;
constexpr unsigned char OPT_EOR = 25;
} // namespace

Tn5250Connection::Tn5250Connection(QObject *parent)
    : AbstractTerminalConnection(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &Tn5250Connection::onReadyRead);
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_connected = true;
        emit connected();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        m_connected = false;
        emit disconnected();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
                emit errorOccurred(m_socket->errorString());
            });
}

Tn5250Connection::~Tn5250Connection() = default;

void Tn5250Connection::connectToHost(const QString &host, quint16 port)
{
    m_socket->connectToHost(host, port ? port : 23);
}

bool Tn5250Connection::isConnected() const { return m_connected; }

void Tn5250Connection::disconnectFromHost() { m_socket->disconnectFromHost(); }

void Tn5250Connection::sendData(const QByteArray &) {} // input is a follow-up

void Tn5250Connection::resize(int, int) {}

void Tn5250Connection::onReadyRead()
{
    processIncoming(m_socket->readAll());
}

void Tn5250Connection::reply(unsigned char verb, unsigned char option)
{
    const unsigned char msg[3] = {IAC, verb, option};
    m_socket->write(reinterpret_cast<const char *>(msg), 3);
}

void Tn5250Connection::sendTerminalType()
{
    QByteArray resp;
    resp.append(static_cast<char>(IAC));
    resp.append(static_cast<char>(SB));
    resp.append(static_cast<char>(OPT_TTYPE));
    resp.append(static_cast<char>(0)); // IS
    resp.append("IBM-3179-2");
    resp.append(static_cast<char>(IAC));
    resp.append(static_cast<char>(SE));
    m_socket->write(resp);
}

void Tn5250Connection::processIncoming(const QByteArray &bytes)
{
    for (char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        switch (m_state) {
        case State::Data:
            if (b == IAC) m_state = State::Iac;
            else m_record.append(c);
            break;
        case State::Iac:
            if (b == IAC) { m_record.append(static_cast<char>(IAC)); m_state = State::Data; }
            else if (b == EOR) {
                m_stream.processRecord(m_record);
                emit dataReceived(m_stream.renderAsVt());
                m_record.clear();
                m_state = State::Data;
            } else if (b == SB) { m_subBuffer.clear(); m_state = State::SubOption; }
            else if (b == WILL || b == WONT || b == DO || b == DONT) {
                m_verb = b; m_state = State::Verb;
            } else { m_state = State::Data; }
            break;
        case State::Verb:
            if (m_verb == DO) {
                if (b == OPT_BINARY || b == OPT_EOR || b == OPT_TTYPE) reply(WILL, b);
                else reply(WONT, b);
            } else if (m_verb == WILL) {
                if (b == OPT_BINARY || b == OPT_EOR || b == OPT_TTYPE) reply(DO, b);
                else reply(DONT, b);
            } else if (m_verb == DONT) reply(WONT, b);
            else if (m_verb == WONT) reply(DONT, b);
            m_state = State::Data;
            break;
        case State::SubOption:
            m_subBuffer.append(c); m_state = State::SubData;
            break;
        case State::SubData:
            if (b == IAC) m_state = State::SubIac;
            else m_subBuffer.append(c);
            break;
        case State::SubIac:
            if (b == SE) {
                if (!m_subBuffer.isEmpty() &&
                    static_cast<unsigned char>(m_subBuffer[0]) == OPT_TTYPE)
                    sendTerminalType();
                m_state = State::Data;
            } else {
                m_subBuffer.append(static_cast<char>(IAC));
                m_subBuffer.append(c);
                m_state = State::SubData;
            }
            break;
        }
    }
}

} // namespace termsync::core
