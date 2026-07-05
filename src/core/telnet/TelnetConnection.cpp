#include "telnet/TelnetConnection.h"

#include <QTcpSocket>

namespace termsync::core {

namespace {
// Telnet control bytes.
constexpr unsigned char IAC  = 255;
constexpr unsigned char DONT = 254;
constexpr unsigned char DO   = 253;
constexpr unsigned char WONT = 252;
constexpr unsigned char WILL = 251;
constexpr unsigned char SB   = 250;
constexpr unsigned char SE   = 240;
// Options.
constexpr unsigned char OPT_ECHO   = 1;
constexpr unsigned char OPT_SGA    = 3;   // suppress go-ahead
constexpr unsigned char OPT_TTYPE  = 24;  // terminal type
constexpr unsigned char OPT_NAWS   = 31;  // window size
} // namespace

TelnetConnection::TelnetConnection(QObject *parent)
    : AbstractTerminalConnection(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &TelnetConnection::onReadyRead);
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

TelnetConnection::~TelnetConnection() = default;

void TelnetConnection::connectToHost(const QString &host, quint16 port)
{
    m_socket->connectToHost(host, port);
}

bool TelnetConnection::isConnected() const
{
    return m_connected;
}

void TelnetConnection::disconnectFromHost()
{
    m_socket->disconnectFromHost();
}

void TelnetConnection::sendData(const QByteArray &data)
{
    // Escape any literal IAC (0xFF) bytes in user input.
    QByteArray out;
    for (char ch : data) {
        out.append(ch);
        if (static_cast<unsigned char>(ch) == IAC)
            out.append(static_cast<char>(IAC));
    }
    m_socket->write(out);
}

void TelnetConnection::resize(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;
    if (m_connected)
        sendNaws();
}

void TelnetConnection::reply(unsigned char verb, unsigned char option)
{
    const unsigned char msg[3] = {IAC, verb, option};
    m_socket->write(reinterpret_cast<const char *>(msg), 3);
}

void TelnetConnection::sendNaws()
{
    unsigned char msg[9] = {
        IAC, SB, OPT_NAWS,
        static_cast<unsigned char>((m_cols >> 8) & 0xFF),
        static_cast<unsigned char>(m_cols & 0xFF),
        static_cast<unsigned char>((m_rows >> 8) & 0xFF),
        static_cast<unsigned char>(m_rows & 0xFF),
        IAC, SE};
    m_socket->write(reinterpret_cast<const char *>(msg), 9);
}

void TelnetConnection::onReadyRead()
{
    processIncoming(m_socket->readAll());
}

void TelnetConnection::processIncoming(const QByteArray &bytes)
{
    QByteArray clean; // application data to hand to the terminal
    for (char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        switch (m_state) {
        case State::Data:
            if (b == IAC)
                m_state = State::Iac;
            else
                clean.append(c);
            break;

        case State::Iac:
            if (b == IAC) { // escaped literal 0xFF
                clean.append(static_cast<char>(IAC));
                m_state = State::Data;
            } else if (b == SB) {
                m_subBuffer.clear();
                m_state = State::SubOption;
            } else if (b == WILL || b == WONT || b == DO || b == DONT) {
                m_verb = b;
                m_state = State::Verb;
            } else {
                m_state = State::Data; // ignore other commands
            }
            break;

        case State::Verb: {
            // Negotiate the handful of options we care about.
            if (m_verb == DO) {
                if (b == OPT_TTYPE || b == OPT_NAWS || b == OPT_SGA)
                    reply(WILL, b);
                else
                    reply(WONT, b);
                if (b == OPT_NAWS)
                    sendNaws();
            } else if (m_verb == WILL) {
                if (b == OPT_ECHO || b == OPT_SGA)
                    reply(DO, b);
                else
                    reply(DONT, b);
            } else if (m_verb == DONT) {
                reply(WONT, b);
            } else if (m_verb == WONT) {
                reply(DONT, b);
            }
            m_state = State::Data;
            break;
        }

        case State::SubOption:
            m_subBuffer.append(c);
            m_state = State::SubData;
            break;

        case State::SubData:
            if (b == IAC)
                m_state = State::SubIac;
            else
                m_subBuffer.append(c);
            break;

        case State::SubIac:
            if (b == SE) {
                // End of subnegotiation. Handle TERMINAL-TYPE SEND request.
                if (!m_subBuffer.isEmpty() &&
                    static_cast<unsigned char>(m_subBuffer[0]) == OPT_TTYPE) {
                    // IAC SB TTYPE IS "xterm" IAC SE
                    QByteArray resp;
                    resp.append(static_cast<char>(IAC));
                    resp.append(static_cast<char>(SB));
                    resp.append(static_cast<char>(OPT_TTYPE));
                    resp.append(static_cast<char>(0)); // IS
                    resp.append("xterm");
                    resp.append(static_cast<char>(IAC));
                    resp.append(static_cast<char>(SE));
                    m_socket->write(resp);
                }
                m_state = State::Data;
            } else {
                m_subBuffer.append(static_cast<char>(IAC));
                m_subBuffer.append(c);
                m_state = State::SubData;
            }
            break;
        }
    }

    if (!clean.isEmpty())
        emit dataReceived(clean);
}

} // namespace termsync::core
