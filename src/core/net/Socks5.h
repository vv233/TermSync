#pragma once

#include <QByteArray>
#include <QString>

namespace termsync::core::socks5 {

// Minimal, pure-logic SOCKS5 parsing for the dynamic port-forwarding proxy.
// No I/O here so it can be unit-tested exhaustively; the forwarder feeds it the
// bytes it has buffered so far.

enum class Status {
    NeedMore,   // not enough bytes yet; call again after reading more
    Ok,         // parsed successfully
    Error,      // malformed / unsupported
};

// Result of parsing the initial method-selection greeting.
struct Greeting
{
    Status status = Status::NeedMore;
    int consumed = 0;      // bytes consumed from the input on Ok
    bool noAuthOffered = false;
};

// Parses "VER NMETHODS METHODS..." and reports whether no-auth (0x00) is
// offered. Only no-auth is supported by this proxy.
Greeting parseGreeting(const QByteArray &in);

// The reply to a greeting: choose no-auth (0x00) or "no acceptable" (0xFF).
QByteArray greetingReply(bool accept);

// Result of parsing a CONNECT request.
struct Request
{
    Status status = Status::NeedMore;
    int consumed = 0;
    quint8 command = 0;    // 1=CONNECT (others unsupported here)
    QString host;          // domain or textual IP
    quint16 port = 0;
};

// Parses "VER CMD RSV ATYP ADDR PORT". Supports IPv4, IPv6 and domain address
// types; command is reported so the caller can reject non-CONNECT.
Request parseRequest(const QByteArray &in);

// Builds the server reply after attempting the connection. `rep` 0x00=success.
// The bound address is reported as 0.0.0.0:0 (sufficient for a CONNECT proxy).
QByteArray requestReply(quint8 rep);

// Common reply codes.
constexpr quint8 kSucceeded = 0x00;
constexpr quint8 kGeneralFailure = 0x01;
constexpr quint8 kCommandNotSupported = 0x07;

} // namespace termsync::core::socks5
