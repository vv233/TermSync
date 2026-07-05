#include "net/Socks5.h"

#include <QStringList>

namespace termsync::core::socks5 {

namespace {
constexpr quint8 kVer = 0x05;
constexpr quint8 kAtypIPv4 = 0x01;
constexpr quint8 kAtypDomain = 0x03;
constexpr quint8 kAtypIPv6 = 0x04;
} // namespace

Greeting parseGreeting(const QByteArray &in)
{
    Greeting g;
    if (in.size() < 2)
        return g; // NeedMore
    const auto ver = static_cast<quint8>(in[0]);
    const auto nmethods = static_cast<quint8>(in[1]);
    if (ver != kVer) {
        g.status = Status::Error;
        return g;
    }
    if (in.size() < 2 + nmethods)
        return g; // NeedMore
    for (int i = 0; i < nmethods; ++i)
        if (static_cast<quint8>(in[2 + i]) == 0x00)
            g.noAuthOffered = true;
    g.status = Status::Ok;
    g.consumed = 2 + nmethods;
    return g;
}

QByteArray greetingReply(bool accept)
{
    QByteArray r(2, 0);
    r[0] = static_cast<char>(kVer);
    r[1] = static_cast<char>(accept ? 0x00 : 0xFF);
    return r;
}

Request parseRequest(const QByteArray &in)
{
    Request req;
    if (in.size() < 4)
        return req; // NeedMore
    const auto ver = static_cast<quint8>(in[0]);
    if (ver != kVer) {
        req.status = Status::Error;
        return req;
    }
    req.command = static_cast<quint8>(in[1]);
    const auto atyp = static_cast<quint8>(in[3]);

    int addrStart = 4;
    int addrLen = 0;
    QString host;

    if (atyp == kAtypIPv4) {
        addrLen = 4;
        if (in.size() < addrStart + addrLen + 2)
            return req; // NeedMore
        host = QStringLiteral("%1.%2.%3.%4")
                   .arg(static_cast<quint8>(in[addrStart]))
                   .arg(static_cast<quint8>(in[addrStart + 1]))
                   .arg(static_cast<quint8>(in[addrStart + 2]))
                   .arg(static_cast<quint8>(in[addrStart + 3]));
    } else if (atyp == kAtypDomain) {
        if (in.size() < 5)
            return req; // NeedMore
        const int nameLen = static_cast<quint8>(in[4]);
        addrStart = 5;
        addrLen = nameLen;
        if (in.size() < addrStart + addrLen + 2)
            return req; // NeedMore
        host = QString::fromLatin1(in.mid(addrStart, addrLen));
    } else if (atyp == kAtypIPv6) {
        addrLen = 16;
        if (in.size() < addrStart + addrLen + 2)
            return req; // NeedMore
        QStringList groups;
        for (int i = 0; i < 16; i += 2) {
            const int hi = static_cast<quint8>(in[addrStart + i]);
            const int lo = static_cast<quint8>(in[addrStart + i + 1]);
            groups << QString::number((hi << 8) | lo, 16);
        }
        host = groups.join(':');
    } else {
        req.status = Status::Error;
        return req;
    }

    const int portOff = addrStart + addrLen;
    req.port = static_cast<quint16>((static_cast<quint8>(in[portOff]) << 8) |
                                    static_cast<quint8>(in[portOff + 1]));
    req.host = host;
    req.status = Status::Ok;
    req.consumed = portOff + 2;
    return req;
}

QByteArray requestReply(quint8 rep)
{
    // VER REP RSV ATYP=IPv4 BND.ADDR(0.0.0.0) BND.PORT(0)
    QByteArray r(10, 0);
    r[0] = static_cast<char>(kVer);
    r[1] = static_cast<char>(rep);
    r[2] = 0x00;
    r[3] = static_cast<char>(kAtypIPv4);
    // remaining 6 bytes already zero
    return r;
}

} // namespace termsync::core::socks5
