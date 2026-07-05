// Unit tests for the SOCKS5 parser used by dynamic port forwarding.

#include <gtest/gtest.h>

#include "net/Socks5.h"

using namespace termsync::core::socks5;

namespace {
QByteArray bytes(std::initializer_list<int> vals)
{
    QByteArray b;
    for (int v : vals)
        b.append(static_cast<char>(v));
    return b;
}
} // namespace

TEST(Socks5, GreetingNeedsMore)
{
    EXPECT_EQ(parseGreeting(bytes({0x05})).status, Status::NeedMore);
    // Announces 2 methods but only one present.
    EXPECT_EQ(parseGreeting(bytes({0x05, 0x02, 0x00})).status, Status::NeedMore);
}

TEST(Socks5, GreetingNoAuth)
{
    const auto g = parseGreeting(bytes({0x05, 0x02, 0x00, 0x02}));
    EXPECT_EQ(g.status, Status::Ok);
    EXPECT_TRUE(g.noAuthOffered);
    EXPECT_EQ(g.consumed, 4);
}

TEST(Socks5, GreetingWithoutNoAuth)
{
    const auto g = parseGreeting(bytes({0x05, 0x01, 0x02})); // only GSSAPI
    EXPECT_EQ(g.status, Status::Ok);
    EXPECT_FALSE(g.noAuthOffered);
}

TEST(Socks5, GreetingBadVersion)
{
    EXPECT_EQ(parseGreeting(bytes({0x04, 0x01, 0x00})).status, Status::Error);
}

TEST(Socks5, RequestIPv4)
{
    // CONNECT 1.2.3.4:0x1234
    const auto r = parseRequest(bytes({0x05, 0x01, 0x00, 0x01, 1, 2, 3, 4, 0x12, 0x34}));
    EXPECT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.command, 0x01);
    EXPECT_EQ(r.host, "1.2.3.4");
    EXPECT_EQ(r.port, 0x1234);
    EXPECT_EQ(r.consumed, 10);
}

TEST(Socks5, RequestDomain)
{
    // CONNECT "ab.co":80
    QByteArray in = bytes({0x05, 0x01, 0x00, 0x03, 5});
    in.append("ab.co");
    in.append(bytes({0x00, 0x50}));
    const auto r = parseRequest(in);
    EXPECT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.host, "ab.co");
    EXPECT_EQ(r.port, 80);
}

TEST(Socks5, RequestDomainNeedsMore)
{
    // Announces a 5-char name but only 3 bytes of it are present.
    QByteArray in = bytes({0x05, 0x01, 0x00, 0x03, 5});
    in.append("ab");
    EXPECT_EQ(parseRequest(in).status, Status::NeedMore);
}

TEST(Socks5, RequestIPv6)
{
    QByteArray in = bytes({0x05, 0x01, 0x00, 0x04});
    for (int i = 0; i < 16; ++i)
        in.append(static_cast<char>(i == 15 ? 1 : 0)); // ::1
    in.append(bytes({0x00, 0x50}));
    const auto r = parseRequest(in);
    EXPECT_EQ(r.status, Status::Ok);
    EXPECT_EQ(r.port, 80);
    EXPECT_TRUE(r.host.endsWith("1"));
}

TEST(Socks5, ReplyShapes)
{
    EXPECT_EQ(greetingReply(true), bytes({0x05, 0x00}));
    EXPECT_EQ(greetingReply(false), bytes({0x05, 0xFF}));
    const QByteArray r = requestReply(kSucceeded);
    EXPECT_EQ(r.size(), 10);
    EXPECT_EQ(static_cast<quint8>(r[1]), kSucceeded);
}
