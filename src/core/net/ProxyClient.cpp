#include "net/ProxyClient.h"

namespace termsync::core::proxy {

QByteArray socks5Greeting(bool offerUserPass)
{
    QByteArray g;
    g.append(static_cast<char>(0x05)); // VER
    if (offerUserPass) {
        g.append(static_cast<char>(0x02)); // NMETHODS
        g.append(static_cast<char>(0x00)); // no auth
        g.append(static_cast<char>(0x02)); // user/pass
    } else {
        g.append(static_cast<char>(0x01));
        g.append(static_cast<char>(0x00));
    }
    return g;
}

int socks5ParseMethod(const QByteArray &reply)
{
    if (reply.size() < 2 || static_cast<unsigned char>(reply[0]) != 0x05)
        return -1;
    const auto method = static_cast<unsigned char>(reply[1]);
    if (method == 0xFF)
        return -1; // no acceptable method
    return method;
}

QByteArray socks5UserPass(const QString &user, const QString &password)
{
    const QByteArray u = user.toUtf8();
    const QByteArray p = password.toUtf8();
    QByteArray r;
    r.append(static_cast<char>(0x01)); // sub-negotiation version
    r.append(static_cast<char>(u.size()));
    r.append(u);
    r.append(static_cast<char>(p.size()));
    r.append(p);
    return r;
}

bool socks5UserPassOk(const QByteArray &reply)
{
    return reply.size() >= 2 && static_cast<unsigned char>(reply[1]) == 0x00;
}

QByteArray socks5ConnectRequest(const QString &host, quint16 port)
{
    const QByteArray h = host.toUtf8();
    QByteArray r;
    r.append(static_cast<char>(0x05)); // VER
    r.append(static_cast<char>(0x01)); // CONNECT
    r.append(static_cast<char>(0x00)); // RSV
    r.append(static_cast<char>(0x03)); // ATYP = domain
    r.append(static_cast<char>(h.size()));
    r.append(h);
    r.append(static_cast<char>((port >> 8) & 0xFF));
    r.append(static_cast<char>(port & 0xFF));
    return r;
}

bool socks5ConnectOk(const QByteArray &reply)
{
    return reply.size() >= 2 && static_cast<unsigned char>(reply[0]) == 0x05 &&
           static_cast<unsigned char>(reply[1]) == 0x00;
}

QByteArray httpConnectRequest(const QString &host, quint16 port,
                              const QString &user, const QString &password)
{
    const QString target = QStringLiteral("%1:%2").arg(host).arg(port);
    QByteArray req;
    req += QStringLiteral("CONNECT %1 HTTP/1.1\r\n").arg(target).toUtf8();
    req += QStringLiteral("Host: %1\r\n").arg(target).toUtf8();
    if (!user.isEmpty()) {
        const QByteArray creds = (user + ':' + password).toUtf8().toBase64();
        req += "Proxy-Authorization: Basic " + creds + "\r\n";
    }
    req += "Proxy-Connection: keep-alive\r\n\r\n";
    return req;
}

bool httpConnectOk(const QByteArray &response)
{
    // Status line: "HTTP/1.x 200 Connection established".
    const int eol = response.indexOf('\n');
    const QByteArray statusLine = eol >= 0 ? response.left(eol) : response;
    const int sp = statusLine.indexOf(' ');
    if (sp < 0)
        return false;
    const QByteArray code = statusLine.mid(sp + 1, 3);
    return code.startsWith('2');
}

} // namespace termsync::core::proxy
