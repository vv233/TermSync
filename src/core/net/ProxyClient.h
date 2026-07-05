#pragma once

#include <QByteArray>
#include <QString>

namespace termsync::core {

// Firewall / proxy configuration for a connection (SecureCRT's "Firewall").
struct ProxyConfig
{
    enum class Type { None, Socks5, Http };

    Type type = Type::None;
    QString host;
    quint16 port = 1080;
    QString username;   // optional
    QString password;   // optional

    bool enabled() const { return type != Type::None; }
};

// Pure client-side proxy handshake helpers, so the SOCKS5/HTTP CONNECT
// negotiation can be unit-tested without a live proxy. The connector feeds
// received bytes back in and sends the returned buffers.
namespace proxy {

// --- SOCKS5 client ---
// Greeting offering no-auth (and username/password if credentials are set).
QByteArray socks5Greeting(bool offerUserPass);
// Whether the server's method reply is acceptable, and which method (0x00 no
// auth, 0x02 user/pass). Returns -1 on failure.
int socks5ParseMethod(const QByteArray &reply);
// Username/password sub-negotiation request (RFC 1929).
QByteArray socks5UserPass(const QString &user, const QString &password);
bool socks5UserPassOk(const QByteArray &reply);
// CONNECT request to host:port (domain address type).
QByteArray socks5ConnectRequest(const QString &host, quint16 port);
// Whether the CONNECT reply indicates success (REP == 0x00).
bool socks5ConnectOk(const QByteArray &reply);

// --- HTTP CONNECT client ---
QByteArray httpConnectRequest(const QString &host, quint16 port,
                              const QString &user, const QString &password);
// Whether the HTTP response status line is 2xx. Needs the full header block.
bool httpConnectOk(const QByteArray &response);

} // namespace proxy
} // namespace termsync::core
