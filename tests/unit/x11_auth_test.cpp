#include <gtest/gtest.h>

#include "ssh/X11Auth.h"

using namespace termsync::core::x11;

namespace {

int pad4(int n) { return (4 - (n & 3)) & 3; }

// Builds an X11 connection setup packet (client->server first message).
QByteArray makeSetup(bool bigEndian, const QByteArray &name,
                     const QByteArray &cookie)
{
    QByteArray b;
    b.append(bigEndian ? char(0x42) : char(0x6C));
    b.append('\0');
    auto u16 = [&](int v) {
        if (bigEndian) {
            b.append(char((v >> 8) & 0xff));
            b.append(char(v & 0xff));
        } else {
            b.append(char(v & 0xff));
            b.append(char((v >> 8) & 0xff));
        }
    };
    u16(11);            // protocol-major
    u16(0);             // protocol-minor
    u16(name.size());   // auth-protocol-name length
    u16(cookie.size()); // auth-protocol-data length
    u16(0);             // unused
    b.append(name);
    b.append(QByteArray(pad4(name.size()), '\0'));
    b.append(cookie);
    b.append(QByteArray(pad4(cookie.size()), '\0'));
    return b;
}

QByteArray cookieA()
{
    return QByteArray::fromHex("00112233445566778899aabbccddeeff");
}
QByteArray cookieB()
{
    return QByteArray::fromHex("ffeeddccbbaa99887766554433221100");
}

} // namespace

TEST(X11Auth, CookieGeneration)
{
    const QByteArray a = generateCookie();
    const QByteArray b = generateCookie();
    EXPECT_EQ(a.size(), kCookieBytes);
    EXPECT_NE(a, b); // astronomically unlikely to collide
    EXPECT_EQ(cookieToHex(cookieA()),
              QStringLiteral("00112233445566778899aabbccddeeff"));
}

TEST(X11Auth, SwapCookieLittleEndian)
{
    const QByteArray proxy = cookieA(), real = cookieB();
    const QByteArray pkt = makeSetup(false, kAuthProtocol(), proxy);
    const RewriteResult r = rewriteSetup(pkt, proxy, real);
    ASSERT_EQ(r.status, RewriteStatus::Ok);
    EXPECT_EQ(r.consumed, pkt.size());
    // The real cookie must now be where the proxy cookie was.
    EXPECT_TRUE(r.rewritten.contains(real));
    EXPECT_FALSE(r.rewritten.contains(proxy));
}

TEST(X11Auth, SwapCookieBigEndian)
{
    const QByteArray proxy = cookieA(), real = cookieB();
    const QByteArray pkt = makeSetup(true, kAuthProtocol(), proxy);
    const RewriteResult r = rewriteSetup(pkt, proxy, real);
    ASSERT_EQ(r.status, RewriteStatus::Ok);
    EXPECT_TRUE(r.rewritten.contains(real));
}

TEST(X11Auth, RejectsCookieMismatch)
{
    const QByteArray pkt = makeSetup(false, kAuthProtocol(), cookieB());
    // We issued cookieA but the packet presents cookieB -> reject.
    const RewriteResult r = rewriteSetup(pkt, cookieA(), cookieB());
    EXPECT_EQ(r.status, RewriteStatus::Mismatch);
}

TEST(X11Auth, NeedMoreOnTruncation)
{
    const QByteArray pkt = makeSetup(false, kAuthProtocol(), cookieA());
    EXPECT_EQ(rewriteSetup(pkt.left(6), cookieA(), cookieB()).status,
              RewriteStatus::NeedMore);
    EXPECT_EQ(rewriteSetup(pkt.left(pkt.size() - 2), cookieA(), cookieB()).status,
              RewriteStatus::NeedMore);
}

TEST(X11Auth, PassthroughWhenNoLocalCookie)
{
    const QByteArray pkt = makeSetup(false, kAuthProtocol(), cookieA());
    const RewriteResult r = rewriteSetup(pkt, cookieA(), QByteArray());
    EXPECT_EQ(r.status, RewriteStatus::Passthrough);
    EXPECT_EQ(r.rewritten, pkt);
}

TEST(X11Auth, PassthroughWhenNoAuthInPacket)
{
    const QByteArray pkt = makeSetup(false, QByteArray(), QByteArray());
    const RewriteResult r = rewriteSetup(pkt, cookieA(), cookieB());
    EXPECT_EQ(r.status, RewriteStatus::Passthrough);
}

TEST(X11Auth, MalformedByteOrder)
{
    QByteArray pkt = makeSetup(false, kAuthProtocol(), cookieA());
    pkt[0] = 'X';
    EXPECT_EQ(rewriteSetup(pkt, cookieA(), cookieB()).status,
              RewriteStatus::Malformed);
}

// ---- Xauthority parsing ----
namespace {
QByteArray makeXauthEntry(int family, const QByteArray &addr,
                          const QByteArray &number, const QByteArray &name,
                          const QByteArray &data)
{
    auto be16 = [](QByteArray &b, int v) {
        b.append(char((v >> 8) & 0xff));
        b.append(char(v & 0xff));
    };
    QByteArray b;
    be16(b, family);
    be16(b, addr.size());
    b.append(addr);
    be16(b, number.size());
    b.append(number);
    be16(b, name.size());
    b.append(name);
    be16(b, data.size());
    b.append(data);
    return b;
}
} // namespace

TEST(X11Auth, XauthorityExactDisplayMatch)
{
    QByteArray blob;
    blob += makeXauthEntry(256, "host", "0", kAuthProtocol(), cookieA());
    blob += makeXauthEntry(256, "host", "1", kAuthProtocol(), cookieB());
    EXPECT_EQ(parseXauthority(blob, 1), cookieB());
    EXPECT_EQ(parseXauthority(blob, 0), cookieA());
}

TEST(X11Auth, XauthorityGenerationRoundTrips)
{
    const QByteArray blob = makeXauthority(3, cookieA());
    ASSERT_FALSE(blob.isEmpty());
    EXPECT_EQ(parseXauthority(blob, 3), cookieA());
    EXPECT_TRUE(makeXauthority(-1, cookieA()).isEmpty());
    EXPECT_TRUE(makeXauthority(0, QByteArray("short")).isEmpty());
}

TEST(X11Auth, XauthorityFallsBackToAnyCookie)
{
    QByteArray blob = makeXauthEntry(256, "host", "0", kAuthProtocol(), cookieA());
    // Requesting display 5 (absent) falls back to the only MIT cookie present.
    EXPECT_EQ(parseXauthority(blob, 5), cookieA());
}

TEST(X11Auth, XauthorityEmptyOnNoMatch)
{
    QByteArray blob = makeXauthEntry(256, "host", "0", "XDM-AUTHORIZATION-1",
                                     cookieA());
    EXPECT_TRUE(parseXauthority(blob, 0).isEmpty());
}
