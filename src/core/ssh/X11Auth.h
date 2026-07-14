#pragma once

#include <QByteArray>
#include <QString>

// X11-forwarding authentication helpers (ssh -X cookie spoofing), kept pure and
// I/O-free so they are unit-testable. The SSH worker uses these to:
//   1. mint a random "proxy" MIT-MAGIC-COOKIE-1 to hand the remote server, and
//   2. rewrite the first X11 setup packet from a forwarded connection, swapping
//      the proxy cookie the remote app presents for the real local X-server
//      cookie so a cookie-protected local X server accepts the connection.
namespace termsync::core::x11 {

constexpr int kCookieBytes = 16; // MIT-MAGIC-COOKIE-1 is 128-bit
inline const char *kAuthProtocol() { return "MIT-MAGIC-COOKIE-1"; }

// A fresh random 16-byte cookie, and its lowercase-hex text form (as libssh2's
// x11 request wants it).
QByteArray generateCookie();
QString cookieToHex(const QByteArray &cookie);

// Builds a minimal Xauthority file containing a wildcard
// MIT-MAGIC-COOKIE-1 entry for `display`. The wildcard family lets a local
// X server authenticate IPv4 and IPv6 loopback clients without disabling
// access control.
QByteArray makeXauthority(int display, const QByteArray &cookie);

// Reads the real MIT-MAGIC-COOKIE-1 for display number `display` from an
// Xauthority file (defaults to $XAUTHORITY, else ~/.Xauthority). Returns an
// empty array if the file/entry is absent (e.g. an access-control-disabled X
// server) — the caller then forwards the setup packet unchanged.
QByteArray readLocalCookie(int display, const QString &xauthorityPath = QString());

// Parses an Xauthority blob and returns the first MIT-MAGIC-COOKIE-1 whose
// display number matches `display` (or any MIT cookie if none matches exactly).
QByteArray parseXauthority(const QByteArray &blob, int display);

enum class RewriteStatus {
    Ok,        // full setup packet present, proxy cookie matched, cookie swapped
    NeedMore,  // packet is incomplete; buffer more bytes and retry
    Mismatch,  // proxy cookie did not match what we issued — reject the channel
    Malformed, // not a valid X11 setup packet
    Passthrough // no local cookie / no auth in packet — forward as-is
};

struct RewriteResult
{
    RewriteStatus status = RewriteStatus::NeedMore;
    QByteArray rewritten;      // the (possibly cookie-swapped) setup packet
    int consumed = 0;          // bytes of `data` that form the setup packet
};

// Given the bytes received so far on a forwarded X11 channel, the proxy cookie
// we issued, and the real local cookie (may be empty), validate + rewrite the
// initial X11 connection setup packet.
RewriteResult rewriteSetup(const QByteArray &data, const QByteArray &proxyCookie,
                           const QByteArray &localCookie);

} // namespace termsync::core::x11
